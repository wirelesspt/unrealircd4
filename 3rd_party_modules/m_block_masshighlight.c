/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gitgud.malvager.net/Wazakindjes/unrealircd_mods
** License: https://gitgud.malvager.net/Wazakindjes/unrealircd_mods/raw/master/LICENSE
**
** Contains edits by k4be to make the threshold checking more robust
*/

// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

// Config block
#define MYCONF "block_masshighlight"

// Channel mode for exempting from highlight checks
#define IsNocheckHighlights(chptr) (chptr->mode.extmode & EXTCMODE_NOCHECK_MASSHIGHLIGHT)
#define CHMODE_CHAR 'H'

// Dem macros yo
#define IsMDErr(x, y, z) \
	do { \
		if(!(x)) { \
			config_error("A critical error occurred when registering ModData for %s: %s", MOD_HEADER(y).name, ModuleGetErrorStr((z)->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)

// Quality fowod declarations
int is_accessmode_exempt(aClient *sptr, aChannel *chptr);
int extcmode_requireowner(aClient *sptr, aChannel *chptr, char mode, char *para, int checkt, int what);
void doXLine(char flag, aClient *sptr);
int masshighlight_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int masshighlight_configposttest(int *errs);
int masshighlight_configrun(ConfigFile *cf, ConfigEntry *ce, int type);
void masshighlight_md_free(ModData *md);
char *masshighlight_hook_prechanmsg(aClient *sptr, aChannel *chptr, char *text, int notice);

// Muh globals
int spamf_ugly_vchanoverride = 0; // For viruschan shit =]
static ModuleInfo *massHLMI = NULL; // Store ModuleInfo so we can use it to check for errors in MOD_LOAD
ModDataInfo *massHLMDI; // To store some shit with the channel ;]
Cmode_t EXTCMODE_NOCHECK_MASSHIGHLIGHT; // For storing the exemption chanmode =]

struct {
	unsigned short int maxnicks; // Maxnicks, unsigned cuz can't be negative anyways lol
	char *delimiters; // List of delimiters for splitting a sentence into "words"
	char action; // Simple char like 'g' for gline etc
	time_t duration; // How long to ban for
	char *reason; // Reason for X:Lines or for the notice to offending users
	unsigned short int snotice; // Whether to send snotices or n0, simply 0 or 1
	unsigned short int banident; // Whether to ban ident@host or simply *@host
	unsigned short int multiline; // Check over multiple lines or just the one ;]
	unsigned short int allow_authed; // Allow identified users to bypass this shit
	unsigned int allow_accessmode; // Lowest channel access mode privilege to bypass the limit
	unsigned short int percent; // How many characters in a message recognised as a nickname is enough for the message to be rejected
	unsigned short int show_opers_origmsg; // Display the suspicious message to operators

	// These are just for setting to 0 or 1 to see if we got em config directives ;]
	unsigned short int got_maxnicks;
	unsigned short int got_delimiters;
	unsigned short int got_action;
	unsigned short int got_duration;
	unsigned short int got_reason;
	unsigned short int got_snotice;
	unsigned short int got_banident;
	unsigned short int got_multiline;
	unsigned short int got_allow_authed;
	unsigned short int got_allow_accessmode;
	unsigned short int got_percent;
	unsigned short int got_show_opers_origmsg;
} muhcfg;

// Dat dere module header
ModuleHeader MOD_HEADER(m_block_masshighlight) = {
	"m_block_masshighlight", // Module name
	"$Id: v1.07 2018/04/16 Gottem/k4be$", // Version
	"Prevent mass highlights network-wide", // Description
	"3.2-b8-1", // Modversion, not sure wat do
	NULL
};

// Configuration testing-related hewks go in testing phase obv
MOD_TEST(m_masshighlight) {
	// We have our own config block so we need to checkem config obv m9
	// Priorities don't really matter here
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, masshighlight_configtest);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGPOSTTEST, 0, masshighlight_configposttest);
	return MOD_SUCCESS;
}

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT(m_block_masshighlight) {
	massHLMI = modinfo;

	// Request moddata for storing the last counter etc
	ModDataInfo mreq;
	memset(&mreq, 0, sizeof(mreq));
	mreq.type = MODDATATYPE_MEMBERSHIP; // Apply to memberships only (look at the user and then iterate through the channel list)
	mreq.name = "masshighlight"; // Name it
	mreq.free = masshighlight_md_free; // Function to free 'em
	massHLMDI = ModDataAdd(modinfo->handle, mreq);
	IsMDErr(massHLMDI, m_block_masshighlight, modinfo);

	// Also request +H channel mode fam
	CmodeInfo req;
	memset(&req, 0, sizeof(req));
	req.paracount = 0; // No args required ;]
	req.flag = CHMODE_CHAR;
	req.is_ok = extcmode_requireowner; // Need owner privs to set em imo tbh
	CmodeAdd(modinfo->handle, req, &EXTCMODE_NOCHECK_MASSHIGHLIGHT);

	// Register hewks m8
	HookAddPChar(modinfo->handle, HOOKTYPE_PRE_CHANMSG, 0, masshighlight_hook_prechanmsg);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, masshighlight_configrun);

	return MOD_SUCCESS; // Let MOD_LOAD handle errors
}

// Actually load the module here (also command overrides as they may not exist in MOD_INIT yet)
MOD_LOAD(m_block_masshighlight) {
	// Did the module throw an error during initialisation?
	if(ModuleGetError(massHLMI->handle) != MODERR_NOERROR) {
		// Display error string kek
		config_error("A critical error occurred when loading module %s: %s", MOD_HEADER(m_block_masshighlight).name, ModuleGetErrorStr(massHLMI->handle));
		return MOD_FAILED; // No good
	}

	return MOD_SUCCESS; // We good
}

// Called on unload/rehash obv
MOD_UNLOAD(m_block_masshighlight) {
	if(muhcfg.reason) // inb4rip
		free(muhcfg.reason); // Let's free this lol
	if(muhcfg.delimiters)
		free(muhcfg.delimiters);
	return MOD_SUCCESS; // We good
}

// Client exempted through allow_accessmode?
int is_accessmode_exempt(aClient *sptr, aChannel *chptr) {
	Membership *lp; // For checkin' em list access level =]

	if(IsServer(sptr) || IsMe(sptr)) // Allow servers always lel
		return 1;

	if(!muhcfg.allow_accessmode) // Don't even bother ;]
		return 0;

	if(chptr) { // Sanity cheqq
		if((lp = find_membership_link(sptr->user->channel, chptr))) {
			if(lp->flags & muhcfg.allow_accessmode)
				return 1;
		}
	}

	return 0; // No valid channel/membership or doesn't have enough axx lol
}

// Testing for owner status on channel
int extcmode_requireowner(aClient *sptr, aChannel *chptr, char mode, char *para, int checkt, int what) {
	if(IsPerson(sptr) && is_chanowner(sptr, chptr))
		return EX_ALLOW;
	if(checkt == EXCHK_ACCESS_ERR)
		sendto_one(sptr, err_str(ERR_CHANOWNPRIVNEEDED), me.name, sptr->name, chptr->chname);
	return EX_DENY;
}

// Not using place_host_ban() cuz I need more control over the mask to ban ;];;];]
void doXLine(char flag, aClient *sptr) {
	// Double check for sptr existing, cuz inb4segfault
	if(sptr) {
		char setTime[100], expTime[100];
		ircsnprintf(setTime, sizeof(setTime), "%li", TStime());
		ircsnprintf(expTime, sizeof(expTime), "%li", TStime() + muhcfg.duration);
		char *tkltype = malloc(sizeof(char) * 2); // Convert the single char to a char *
		tkltype[0] = (flag == 's' ? flag : toupper(flag)); // Uppercase that shit if not shunning y0
		tkltype[1] = '\0';

		// Build TKL args
		char *tkllayer[9] = {
			// :SERVER +FLAG IDENT HOST SETBY EXPIRATION SETAT :REASON
			me.name,
			"+",
			tkltype,
			(muhcfg.banident ? sptr->user->username : "*"), // Wildcard ident if banident == 0
			(flag == 'z' ? GetIP(sptr) : sptr->user->realhost), // Let's use the IP in case of Z:Lines lel
			me.name,
			expTime,
			setTime,
			muhcfg.reason
		};

		m_tkl(&me, &me, 9, tkllayer); // Ban 'em
		free(tkltype); // Free that shit lol
	}
}

int masshighlight_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs) {
	int errors = 0; // Error count
	int i; // Iterat0r
	int tmp; // f0 checking integer values
	ConfigEntry *cep; // To store the current variable/value pair etc

	// Since we'll add a top-level block to unrealircd.conf, need to filter on CONFIG_MAIN lmao
	if(type != CONFIG_MAIN)
		return 0; // Returning 0 means idgaf bout dis

	// Check for valid config entries first
	if(!ce || !ce->ce_varname)
		return 0;

	// If it isn't our block, idc
	if(strcmp(ce->ce_varname, MYCONF))
		return 0;

	// Loop dat shyte fam
	for(cep = ce->ce_entries; cep; cep = cep->ce_next) {
		// Do we even have a valid name l0l?
		if(!cep->ce_varname) {
			config_error("%s:%i: blank %s item", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF); // Rep0t error
			errors++; // Increment err0r count fam
			continue; // Next iteration imo tbh
		}

		if(!strcmp(cep->ce_varname, "action")) {
			muhcfg.got_action = 1;
			if(!cep->ce_vardata || !strlen(cep->ce_vardata)) {
				config_error("%s:%i: %s::%s must be non-empty fam", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
				continue;
			}

			// Checkem valid actions
			if(strcmp(cep->ce_vardata, "drop") && strcmp(cep->ce_vardata, "notice") && strcmp(cep->ce_vardata, "gline") && strcmp(cep->ce_vardata, "zline") &&
				strcmp(cep->ce_vardata, "kill") && strcmp(cep->ce_vardata, "tempshun") && strcmp(cep->ce_vardata, "shun") && strcmp(cep->ce_vardata, "viruschan")) {
				config_error("%s:%i: %s::%s must be one of: drop, notice, gline, zline, shun, tempshun, kill, viruschan", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++;
			}

			if(!errors)
				muhcfg.action = cep->ce_vardata[0]; // We need this value to be set in posttest
			continue;
		}

		if(!strcmp(cep->ce_varname, "reason")) {
			muhcfg.got_reason = 1;
			if(!cep->ce_vardata || strlen(cep->ce_vardata) < 4) {
				config_error("%s:%i: %s::%s must be at least 4 characters long", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
			}
			continue;
		}

		if(!strcmp(cep->ce_varname, "delimiters")) {
			muhcfg.got_delimiters = 1;
			if(!cep->ce_vardata || !strlen(cep->ce_vardata)) {
				config_error("%s:%i: %s::%s must contain at least one character", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
			}
			continue;
		}

		if(!strcmp(cep->ce_varname, "duration")) {
			muhcfg.got_duration = 1;
			// Should be a time string imo (7d10s etc, or just 20)
			if(!cep->ce_vardata || config_checkval(cep->ce_vardata, CFG_TIME) <= 0) {
				config_error("%s:%i: %s::%s must be a time string like '7d10m' or simply '20'", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
			}
			continue;
		}

		if(!strcmp(cep->ce_varname, "maxnicks")) {
			muhcfg.got_maxnicks = 1;
			// Should be an integer yo
			if(!cep->ce_vardata) {
				config_error("%s:%i: %s::%s must be an integer between 1 and 512 m8", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
				continue;
			}
			for(i = 0; cep->ce_vardata[i]; i++) {
				if(!isdigit(cep->ce_vardata[i])) {
					config_error("%s:%i: %s::%s must be an integer between 1 and 512 m8", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
					errors++; // Increment err0r count fam
					break;
				}
			}
			if(!errors) {
				tmp = atoi(cep->ce_vardata);
				if(tmp <= 0 || tmp > 512) {
					config_error("%s:%i: %s::%s must be an integer between 1 and 512 m8", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
					errors++; // Increment err0r count fam
				}
			}
			continue;
		}

		if(!strcmp(cep->ce_varname, "snotice")) {
			muhcfg.got_snotice = 1;
			if(!cep->ce_vardata || (strcmp(cep->ce_vardata, "0") && strcmp(cep->ce_vardata, "1"))) {
				config_error("%s:%i: %s::%s must be either 0 or 1 fam", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
			}
			continue;
		}

		if(!strcmp(cep->ce_varname, "banident")) {
			muhcfg.got_banident = 1;
			if(!cep->ce_vardata || (strcmp(cep->ce_vardata, "0") && strcmp(cep->ce_vardata, "1"))) {
				config_error("%s:%i: %s::%s must be either 0 or 1 fam", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
			}
			continue;
		}

		if(!strcmp(cep->ce_varname, "multiline")) {
			muhcfg.got_multiline = 1;
			if(!cep->ce_vardata || (strcmp(cep->ce_vardata, "0") && strcmp(cep->ce_vardata, "1"))) {
				config_error("%s:%i: %s::%s must be either 0 or 1 fam", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
			}
			continue;
		}

		if(!strcmp(cep->ce_varname, "allow_authed")) {
			muhcfg.got_allow_authed = 1;
			if(!cep->ce_vardata || (strcmp(cep->ce_vardata, "0") && strcmp(cep->ce_vardata, "1"))) {
				config_error("%s:%i: %s::%s must be either 0 or 1 fam", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
			}
			continue;
		}

		if(!strcmp(cep->ce_varname, "allow_accessmode")) {
			muhcfg.got_allow_accessmode = 1;
			if(!cep->ce_vardata || !strlen(cep->ce_vardata)) {
				config_error("%s:%i: %s::%s must be either non-empty or not specified at all", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
				continue;
			}

			if(strlen(cep->ce_vardata) > 1) {
				config_error("%s:%i: %s::%s must be exactly one character (mode) in length", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
				continue;
			}

			if(strcmp(cep->ce_vardata, "v") && strcmp(cep->ce_vardata, "h") && strcmp(cep->ce_vardata, "o")
		#ifdef PREFIX_AQ
				&& strcmp(cep->ce_vardata, "a") && strcmp(cep->ce_vardata, "q")
		#endif
			) {
				config_error("%s:%i: %s::%s must be one of: v, h, o"
		#ifdef PREFIX_AQ
				", a, q"
		#endif
				, cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++;
			}
			continue;
		}

		if(!strcmp(cep->ce_varname, "percent")) {
			muhcfg.got_percent = 1;
			// Should be an integer yo
			if(!cep->ce_vardata) {
				config_error("%s:%i: %s::%s must be an integer between 1 and 100 m8", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
				continue;
			}
			for(i = 0; cep->ce_vardata[i]; i++) {
				if(!isdigit(cep->ce_vardata[i])) {
					config_error("%s:%i: %s::%s must be an integer between 1 and 100 m8", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
					errors++; // Increment err0r count fam
					break;
				}
			}
			if(!errors) {
				tmp = atoi(cep->ce_vardata);
				if(tmp <= 0 || tmp > 100) {
					config_error("%s:%i: %s::%s must be an integer between 1 and 100 m8", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
					errors++; // Increment err0r count fam
				}
			}
			continue;
		}

		if(!strcmp(cep->ce_varname, "show_opers_origmsg")) {
			muhcfg.got_show_opers_origmsg = 1;
			if(!cep->ce_vardata || (strcmp(cep->ce_vardata, "0") && strcmp(cep->ce_vardata, "1"))) {
				config_error("%s:%i: %s::%s must be either 0 or 1 fam", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
			}
			continue;
		}

		// Anything else is unknown to us =]
		config_warn("%s:%i: unknown item %s::%s", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname); // So display just a warning
	}

	*errs = errors;
	return errors ? -1 : 1; // Returning 1 means "all good", -1 means we shat our panties
}

int masshighlight_configposttest(int *errs) {
	int errors = 0;
	// Set defaults and display warnings where needed
	if(!muhcfg.got_maxnicks) {
		muhcfg.maxnicks = 5;
		config_warn("[block_masshighlight] Unable to find 'maxnicks' directive, defaulting to: %d", muhcfg.maxnicks);
	}

	if(!muhcfg.got_action) {
		muhcfg.action = 'g'; // Gline em by default lol
		config_warn("[block_masshighlight] Unable to find 'action' directive, defaulting to: gline", muhcfg.action);
	}

	if(strchr("gzs", muhcfg.action) && !muhcfg.got_duration) { // Duration is only required for X:Lines =]
		muhcfg.duration = 604800; // 7 days yo
		config_warn("[block_masshighlight] Unable to find 'duration' directive, defaulting to: %li seconds", muhcfg.duration);
	}

	if(muhcfg.action != 'd' && !muhcfg.got_reason) // For everything besides drop, we need a reason =]
		muhcfg.reason = strdup("No mass highlighting allowed"); // So it doesn't fuck with free(), also no need to display config_warn() imo tbh

	if(!muhcfg.got_delimiters)
		muhcfg.delimiters = strdup("	 ,.-_/\\:;"); // Ditto =]

	if(!muhcfg.got_snotice)
		muhcfg.snotice = 1; // Show 'em, no need to display config_warn() imo tbh

	if(!muhcfg.got_banident) // Lazy mode, even though it's not required for all actions, set it anyways =]
		muhcfg.banident = 1; // Default to ident@host imo tbh

	if(!muhcfg.got_multiline)
		muhcfg.multiline = 0; // Default to single line imo

	if(!muhcfg.got_allow_authed)
		muhcfg.allow_authed = 0; // Default to n0 fam

	if(!muhcfg.got_allow_accessmode)
		muhcfg.allow_accessmode = 0; // None ;]

	if(!muhcfg.got_percent)
		muhcfg.percent = 1; // 1%, max sensitivity

	if(!muhcfg.got_show_opers_origmsg)
		muhcfg.show_opers_origmsg = 1; // Default to showing em

	*errs = errors;
	return errors ? -1 : 1;
}

// "Run" the config (everything should be valid at this point)
int masshighlight_configrun(ConfigFile *cf, ConfigEntry *ce, int type) {
	ConfigEntry *cep; // To store the current variable/value pair etc

	// Since we'll add a top-level block to unrealircd.conf, need to filter on CONFIG_MAIN lmao
	if(type != CONFIG_MAIN)
		return 0; // Returning 0 means idgaf bout dis

	// Check for valid config entries first
	if(!ce || !ce->ce_varname)
		return 0;

	// If it isn't masshighlight, idc
	if(strcmp(ce->ce_varname, MYCONF))
		return 0;

	// Loop dat shyte fam
	for(cep = ce->ce_entries; cep; cep = cep->ce_next) {
		// Do we even have a valid name l0l?
		if(!cep->ce_varname)
			continue; // Next iteration imo tbh

		if(!strcmp(cep->ce_varname, "delimiters")) {
			if(muhcfg.delimiters) // Let's free this first lol (should never happen but let's just in case xd)
				free(muhcfg.delimiters);
			muhcfg.delimiters = strdup(cep->ce_vardata);
			continue;
		}

		if(!strcmp(cep->ce_varname, "reason")) {
			if(muhcfg.reason) // Let's free this first lol (should never happen but let's just in case xd)
				free(muhcfg.reason);
			muhcfg.reason = strdup(cep->ce_vardata);
			continue;
		}

		if(!strcmp(cep->ce_varname, "duration")) {
			muhcfg.duration = config_checkval(cep->ce_vardata, CFG_TIME);
			continue;
		}

		if(!strcmp(cep->ce_varname, "maxnicks")) {
			muhcfg.maxnicks = atoi(cep->ce_vardata);
			continue;
		}

		if(!strcmp(cep->ce_varname, "snotice")) {
			muhcfg.snotice = atoi(cep->ce_vardata);
			continue;
		}

		if(!strcmp(cep->ce_varname, "banident")) {
			muhcfg.banident = atoi(cep->ce_vardata);
			continue;
		}

		if(!strcmp(cep->ce_varname, "multiline")) {
			muhcfg.multiline = atoi(cep->ce_vardata);
			continue;
		}

		if(!strcmp(cep->ce_varname, "allow_authed")) {
			muhcfg.allow_authed = atoi(cep->ce_vardata);
			continue;
		}

		if(!strcmp(cep->ce_varname, "allow_accessmode")) {
			muhcfg.allow_accessmode = 0;
			switch(cep->ce_vardata[0]) {
				case 'v':
					muhcfg.allow_accessmode |= CHFL_VOICE;
				case 'h':
					muhcfg.allow_accessmode |= CHFL_HALFOP;
				case 'o':
					muhcfg.allow_accessmode |= CHFL_CHANOP;
		#ifdef PREFIX_AQ
				case 'a':
					muhcfg.allow_accessmode |= CHFL_CHANPROT;
				case 'q':
					muhcfg.allow_accessmode |= CHFL_CHANOWNER;
		#endif
				default:
					break;
			}
			continue;
		}

		if(!strcmp(cep->ce_varname, "percent")) {
			muhcfg.percent = atoi(cep->ce_vardata);
			continue;
		}

		if(!strcmp(cep->ce_varname, "show_opers_origmsg")) {
			muhcfg.show_opers_origmsg = atoi(cep->ce_vardata);
			continue;
		}

	}

	return 1; // We good
}

void masshighlight_md_free(ModData *md) {
	if(md && md->i) // Just in case kek, as this function is required
		md->i = 0;
}

char *masshighlight_hook_prechanmsg(aClient *sptr, aChannel *chptr, char *text, int notice) {
	/* Args:
	** sptr: Client that sent the message
	** chptr: Channel to which it was sent
	** text: The message body obv fambi
	** notice: Was it a NOTICE or simply PRIVMSG?
	*/
	int hl_cur; // Current highlight count yo
	char *p; // For tokenising that shit
	char *werd; // Store current token etc
	char *cleantext; // Let's not modify char *text =]
	aClient *acptr; // Temporarily store a mentioned nick to get the membership link
	Membership *lp; // For actually storing the link
	MembershipL *lp2; // Required for moddata call apparently
	int blockem; // Need to block the message?
	int clearem; // For clearing the counter
	int ret; // For checking the JOIN return status for viruschan action
	char joinbuf[CHANNELLEN + 4]; // Also for viruschan, need to make room for "0,#viruschan" etc ;];]
	char *vcparv[3]; // Arguments for viruschan JOIN
	int bypass_nsauth; // If config var allow_authed is tr00 and user es logged in
	char gotnicks[1024]; // For storing nicks that were mentioned =]
	int hl_new; // Store highlight count for further processing
	size_t werdlen; // Word length ;]
	size_t hl_nickslen; // Amount of chars that are part of highlights
	size_t msglen; // Full message length (both of these are required for calculating percent etc), excludes delimiters

	// Initialise some shit lol
	bypass_nsauth = (muhcfg.allow_authed && IsPerson(sptr) && IsLoggedIn(sptr));
	blockem = 0;
	clearem = 1;
	p = NULL;
	cleantext = NULL;
	memset(gotnicks, '\0', sizeof(gotnicks));
	hl_new = 0;
	hl_nickslen = 0;
	msglen = 0;

	if(IsNocheckHighlights(chptr))
		return text; // if channelmode is set, allow without further checking

	// Some sanity + privilege checks ;];] (allow_accessmode, U:Lines and opers are exempt from this shit)
	if(text && IsPerson(sptr) && MyClient(sptr) && !bypass_nsauth && !is_accessmode_exempt(sptr, chptr) && !IsULine(sptr) && !IsOper(sptr)) {
		if(!(lp = find_membership_link(sptr->user->channel, chptr))) // Not even a member tho lol
			return text; // Process normally cuz can't do shit w/ moddata =]

		lp2 = (MembershipL *)lp; // Cast that shit
		hl_cur = moddata_membership(lp2, massHLMDI).i; // Get current count

		// In case someone tries some funny business =]
		if(!(cleantext = (char *)StripControlCodes(text)))
			return text;

		for(werd = strtoken(&p, cleantext, muhcfg.delimiters); werd; werd = strtoken(&p, NULL, muhcfg.delimiters)) { // Split that shit
			werdlen = strlen(werd);
			msglen += werdlen; // We do not count ze delimiters, otherwise the percents would get strangely low
			if((acptr = find_person(werd, NULL)) && (find_membership_link(acptr->user->channel, chptr))) { // User mentioned another user in this channel
				if(!(strstr(gotnicks, acptr->id))) { // Do not count the same nickname appended multiple times to a single message
					ircsnprintf(gotnicks, sizeof(gotnicks), "%s%s,", gotnicks, acptr->id);
					hl_new++; // Got another highlight this round
					hl_nickslen += werdlen; // Also add the nick's length
				}
			}
		}

		if(msglen && 100 * hl_nickslen / msglen > muhcfg.percent) { // Check if we exceed the max allowed percentage
			hl_cur += hl_new; // Set moddata counter to include the ones found this round
			clearem = 0; // And don't clear moddata
			if(hl_cur > muhcfg.maxnicks) // Check if we also exceed max allowed nicks
				blockem = 1; // Flip flag to blockin' dat shit
		}

		if(!muhcfg.multiline && !clearem) // If single line mode and found a nick
			clearem = 1; // Need to clear that shit always lol

		moddata_membership(lp2, massHLMDI).i = (clearem ? 0 : hl_cur); // Actually set the counter =]
		if(blockem) { // Need to bl0ck?
			switch(muhcfg.action) {
				case 'd': // Drop silently
					if(muhcfg.snotice)
						sendto_snomask_global(SNO_EYES, "*** [block_masshighlight] Detected highlight spam in %s by %s, dropping silently", chptr->chname, sptr->name);
					break;

				case 'n': // Drop, but send notice
					if(muhcfg.snotice)
						sendto_snomask_global(SNO_EYES, "*** [block_masshighlight] Detected highlight spam in %s by %s, dropping with a notice", chptr->chname, sptr->name);
					sendto_one(sptr, ":%s NOTICE %s :%s", me.name, chptr->chname, muhcfg.reason);
					break;

				case 'k': // Kill em all
					if(muhcfg.snotice)
						sendto_snomask_global(SNO_EYES, "*** [block_masshighlight] Detected highlight spam in %s by %s, killing 'em", chptr->chname, sptr->name);
					dead_link(sptr, muhcfg.reason);
					break;

				case 't': // Tempshun kek
					if(muhcfg.snotice)
						sendto_snomask_global(SNO_EYES, "*** [block_masshighlight] Detected highlight spam in %s by %s, setting tempshun", chptr->chname, sptr->name);

					// Nicked this from place_host_ban() tbh =]
					sendto_snomask(SNO_TKL, "Temporary shun added at user %s (%s@%s) [%s]", sptr->name,
						(sptr->user ? sptr->user->username : "unknown"),
						(sptr->user ? sptr->user->realhost : GetIP(sptr)),
						muhcfg.reason);
					SetShunned(sptr); // Ez m0de
					break;

				case 's': // Shun, ...
				case 'g': // ...G:Line and ...
				case 'z': // ...Z:Line share the same internal function ;];]
					if(muhcfg.snotice) {
						if(muhcfg.action == 's')
							sendto_snomask_global(SNO_EYES, "*** [block_masshighlight] Detected highlight spam in %s by %s, shunning 'em", chptr->chname, sptr->name);
						else
							sendto_snomask_global(SNO_EYES, "*** [block_masshighlight] Detected highlight spam in %s by %s, setting %c:Line", chptr->chname, sptr->name, toupper(muhcfg.action));
					}
					doXLine(muhcfg.action, sptr);
					break;

				case 'v': // Viruschan lol
					if(muhcfg.snotice)
						sendto_snomask_global(SNO_EYES, "*** [block_masshighlight] Detected highlight spam in %s by %s, following viruschan protocol", chptr->chname, sptr->name);

					// This bit is ripped from m_tkl with some logic changes to suit what we need to do =]
					snprintf(joinbuf, sizeof(joinbuf), "0,%s", SPAMFILTER_VIRUSCHAN);
					vcparv[0] = sptr->name;
					vcparv[1] = joinbuf;
					vcparv[2] = NULL;

					spamf_ugly_vchanoverride = 1;
					ret = do_cmd(sptr, sptr, "JOIN", 2, vcparv); // Need to get return value
					spamf_ugly_vchanoverride = 0;
					if(ret != FLUSH_BUFFER) { // In case something went horribly wrong lmao
						sendnotice(sptr, "You are now restricted to talking in %s: %s", SPAMFILTER_VIRUSCHAN, muhcfg.reason);
						SetVirus(sptr); // Ayy rekt
					}
					break;

				default:
					break;
			}
			if(muhcfg.show_opers_origmsg)
				sendto_snomask_global(SNO_EYES, "*** [block_masshighlight] The message was: %s", text);
			text = NULL; // NULL makes Unreal drop the message entirely afterwards ;3
		}
	}

	return text;
}
