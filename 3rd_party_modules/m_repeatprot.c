// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

#define OVR_INVITE "INVITE"
#define OVR_OPER "OPER"
#define OVR_NOTICE "NOTICE"
#define OVR_PRIVMSG "PRIVMSG"

// Muh macros/typedefs
#define DelOverride(cmd, ovr) if(ovr && CommandExists(cmd)) { CmdoverrideDel(ovr); ovr = NULL; } // Unregister override

#define IsMDErr(x, y, z) \
	do { \
		if(!(x)) { \
			config_error("A critical error occurred when registering ModData for %s: %s", MOD_HEADER(y).name, ModuleGetErrorStr((z)->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)

typedef struct t_exemption muhExempt;
typedef struct t_msg muhMessage;
struct t_exemption {
	char *mask;
	muhExempt *next;
};
struct t_msg {
	char *last;
	char *prev;
	int count;
	time_t ts;
};

// Quality fowod declarations
int repeatprot_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int repeatprot_configposttest(int *errs);
int repeatprot_configrun(ConfigFile *cf, ConfigEntry *ce, int type);
int repeatprot_rehash(void);
int dropMessage(aClient *cptr, aClient *sptr);
void blockIt(aClient *sptr);
int doKill(aClient *cptr, aClient *sptr);
void doXLine(char flag, aClient *sptr);
static int repeatprot_override(Cmdoverride *ovr, aClient *cptr, aClient *sptr, int parc, char *parv[]);
void repeatprot_free(ModData *md);

// Muh globals
static ModuleInfo *repeatprotMI = NULL; // Store ModuleInfo so we can use it to check for errors in MOD_LOAD
Cmdoverride *inviteOvr, *operOvr, *noticeOvr, *privmsgOvr; // Pointers to the overrides we're gonna add
ModDataInfo *repeatprotMDI; // To store every user's last message with their client pointer ;3
muhExempt *exemptList = NULL; // Stores exempted masks

// Deez defaults
int repeatThreshold = 3; // After how many repeated message the action kicks in
char tklAction = 'G'; // Action to take when threshold has been reached -- b = block, k = kill, z = gzline, g = gline
int tklTime = 60; // How long to gzline for, in seconds -- doesn't apply to block/kill actions obv fam
char *tklMessage = "Nice spam m8"; // Quit message =]
int showBlocked = 0; // Whether to show tklMessage for the block action
int trigOper = 0; // Do we trigger on OPER?
int trigNotice = 0; // Ditto for NOTICE
int trigPrivmsg = 0; // Ditto^ditto for PRIVMSG
int trigCTCP = 0; // D I T T O
int trigInvite = 0;
int trigCount = 0; // Need at least one lol
time_t trigTimespan = 0;

// Dat dere module header
ModuleHeader MOD_HEADER(m_repeatprot) = {
	"m_repeatprot", // Module name
	"$Id: v1.23 2016/12/30 Gottem$", // Version
	"G(Z):Line/kill users (or block their messages) who spam through CTCP, INVITE, OPER, NOTICE and/or PRIVMSG", // Description
	"3.2-b8-1", // Modversion, not sure wat do
	NULL
};

// Configuration testing-related hewks go in testing phase obv
// This function is entirely optional
MOD_TEST(m_repeatprot) {
	// We have our own config block so we need to checkem config obv m9
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, repeatprot_configtest);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGPOSTTEST, 0, repeatprot_configposttest);
	return MOD_SUCCESS;
}

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT(m_repeatprot) {
	ModDataInfo mreq; // Request that shit
	memset(&mreq, 0, sizeof(mreq));
	mreq.type = MODDATATYPE_CLIENT; // Apply to users only (CLIENT actually includes servers but we'll disregard that =])
	mreq.name = "lastmessage"; // Name it
	mreq.free = repeatprot_free; // Function to free 'em
	repeatprotMDI = ModDataAdd(modinfo->handle, mreq);
	IsMDErr(repeatprotMDI, m_repeatprot, modinfo);

	repeatprotMI = modinfo; // Store module info yo
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, repeatprot_configrun);
	HookAdd(modinfo->handle, HOOKTYPE_REHASH, 0, repeatprot_rehash);
	return MOD_SUCCESS; // Let MOD_LOAD handle errors and registering of overrides
}

// Actually load the module here (also command overrides as they may not exist in MOD_INIT yet)
MOD_LOAD(m_repeatprot) {
	inviteOvr = CmdoverrideAdd(repeatprotMI->handle, OVR_INVITE, repeatprot_override); // Attempt to add INVITE override
	operOvr = CmdoverrideAdd(repeatprotMI->handle, OVR_OPER, repeatprot_override); // Attempt to add OPER override
	noticeOvr = CmdoverrideAdd(repeatprotMI->handle, OVR_NOTICE, repeatprot_override); // Attempt to add NOTICE override
	privmsgOvr = CmdoverrideAdd(repeatprotMI->handle, OVR_PRIVMSG, repeatprot_override); // Attempt to add PRIVMSG override

	// Did the module throw an error when adding override(s), or is one of the overrides null even?
	if(ModuleGetError(repeatprotMI->handle) != MODERR_NOERROR || !inviteOvr || !operOvr || !noticeOvr || !privmsgOvr) {
		// Display error string kek
		config_error("A critical error occurred when loading module %s: %s", MOD_HEADER(m_repeatprot).name, ModuleGetErrorStr(repeatprotMI->handle));
		return MOD_FAILED; // No good
	}

	return MOD_SUCCESS; // We good
}

// Called on unload/rehash obv
MOD_UNLOAD(m_repeatprot) {
	if(exemptList) {
		// This shit is a bit convoluted to prevent memory issues obv famalmalmalmlmalm
		muhExempt *exEntry;
		while((exEntry = exemptList) != NULL) {
			exemptList = exemptList->next;
			free(exEntry);
		}
		exemptList = NULL;
	}
	// Attempt to free overrides
	DelOverride(OVR_NOTICE, inviteOvr);
	DelOverride(OVR_NOTICE, operOvr);
	DelOverride(OVR_NOTICE, noticeOvr);
	DelOverride(OVR_NOTICE, privmsgOvr);
	return MOD_SUCCESS; // We good
}

int repeatprot_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs) {
	int errors = 0; // Error count
	int i; // Iterat0r
	ConfigEntry *cep, *cep2; // To store the current variable/value pair etc, nested

	// Since we'll add a top-level block to unrealircd.conf, need to filter on CONFIG_MAIN lmao
	if(type != CONFIG_MAIN)
		return 0; // Returning 0 means idgaf bout dis

	// Check for valid config entries first
	if(!ce || !ce->ce_varname)
		return 0;

	// If it isn't repeatprot, idc
	if(strcmp(ce->ce_varname, "repeatprot"))
		return 0;

		// Loop dat shyte fam
	for(cep = ce->ce_entries; cep; cep = cep->ce_next) {
		// Do we even have a valid name l0l?
		if(!cep->ce_varname) {
			config_error("%s:%i: blank repeatprot item", cep->ce_fileptr->cf_filename, cep->ce_varlinenum); // Rep0t error
			errors++; // Increment err0r count fam
			continue; // Next iteration imo tbh
		}

		// Check for optionals first =]
		if(!strcmp(cep->ce_varname, "action")) {
			if(!cep->ce_vardata || (strcmp(cep->ce_vardata, "block") && strcmp(cep->ce_vardata, "kill") && strcmp(cep->ce_vardata, "gzline") && strcmp(cep->ce_vardata, "gline"))) {
				config_error("%s:%i: repeatprot::action must be either 'block', 'kill', 'gline' or 'gzline'", cep->ce_fileptr->cf_filename, cep->ce_varlinenum);
				errors++; // Increment err0r count fam
				continue;
			}
		}

		if(!strcmp(cep->ce_varname, "banmsg")) {
			if(!cep->ce_vardata || !strlen(cep->ce_vardata)) {
				config_error("%s:%i: repeatprot::banmsg must be non-empty fam", cep->ce_fileptr->cf_filename, cep->ce_varlinenum);
				errors++; // Increment err0r count fam
				continue;
			}
		}

		if(!strcmp(cep->ce_varname, "showblocked")) {
			if(!cep->ce_vardata || (strcmp(cep->ce_vardata, "0") && strcmp(cep->ce_vardata, "1"))) {
				config_error("%s:%i: repeatprot::showblocked must be either 0 or 1 fam", cep->ce_fileptr->cf_filename, cep->ce_varlinenum);
				errors++; // Increment err0r count fam
				continue;
			}
		}

		if(!strcmp(cep->ce_varname, "tkltime")) {
			// Should be a time string imo (7d10s etc, or just 20)
			if(!cep->ce_vardata || config_checkval(cep->ce_vardata, CFG_TIME) <= 0) {
				config_error("%s:%i: repeatprot::tkltime must be a time string like '7d10m' or simply '20'", cep->ce_fileptr->cf_filename, cep->ce_varlinenum);
				errors++; // Increment err0r count fam
				continue;
			}
		}

		if(!strcmp(cep->ce_varname, "threshold")) {
			// Should be an integer yo
			if(!cep->ce_vardata) {
				config_error("%s:%i: repeatprot::threshold must be an integer of zero or larger m8", cep->ce_fileptr->cf_filename, cep->ce_varlinenum);
				errors++; // Increment err0r count fam
				continue;
			}
			for(i = 0; cep->ce_vardata[i]; i++) {
				if(!isdigit(cep->ce_vardata[i])) {
					config_error("%s:%i: repeatprot::threshold must be an integer of zero or larger m8", cep->ce_fileptr->cf_filename, cep->ce_varlinenum);
					errors++; // Increment err0r count fam
					continue;
				}
			}
		}

		if(!strcmp(cep->ce_varname, "timespan")) {
			// Should be a time string imo (7d10s etc, or just 20)
			if(!cep->ce_vardata || config_checkval(cep->ce_vardata, CFG_TIME) <= 0) {
				config_error("%s:%i: repeatprot::timespan must be a time string like '7d10m' or simply '20'", cep->ce_fileptr->cf_filename, cep->ce_varlinenum);
				errors++; // Increment err0r count fam
				continue;
			}
		}

		// Now check for the repeatprot::triggers bl0qq
		if(!strcmp(cep->ce_varname, "triggers")) {
			// Loop 'em
			for(cep2 = cep->ce_entries; cep2; cep2 = cep2->ce_next) {
				if(!cep2->ce_varname) {
					config_error("%s:%i: blank repeatprot trigger", cep2->ce_fileptr->cf_filename, cep2->ce_varlinenum); // Rep0t error
					errors++; // Increment err0r count fam
					continue; // Next iteration imo tbh
				}

				if(strcmp(cep2->ce_varname, "notice") && strcmp(cep2->ce_varname, "privmsg") && strcmp(cep2->ce_varname, "oper") && strcmp(cep2->ce_varname, "ctcp") && strcmp(cep2->ce_varname, "invite")) {
					config_error("%s:%i: invalid repeatprot trigger", cep2->ce_fileptr->cf_filename, cep2->ce_varlinenum); // Rep0t error
					errors++; // Increment err0r count fam
					continue; // Next iteration imo tbh
				}
				trigCount++; // Seems to be a valid trigger yo
			}
			continue;
		}

		// Also dem exemptions
		if(!strcmp(cep->ce_varname, "exemptions")) {
			// Loop 'em
			for(cep2 = cep->ce_entries; cep2; cep2 = cep2->ce_next) {
				if(!cep2->ce_varname) {
					config_error("%s:%i: blank exemption mask", cep2->ce_fileptr->cf_filename, cep2->ce_varlinenum); // Rep0t error
					errors++; // Increment err0r count fam
					continue; // Next iteration imo tbh
				}

				if(match("*!*@*", cep2->ce_varname)) {
					config_error("%s:%i: invalid nick!user@host exemption mask", cep2->ce_fileptr->cf_filename, cep2->ce_varlinenum); // Rep0t error
					errors++; // Increment err0r count fam
					continue; // Next iteration imo tbh
				}
			}
			continue;
		}
	}

	*errs = errors;
	// Returning 1 means "all good", -1 means we shat our panties
	return errors ? -1 : 1;
}

// Post test, check for missing shit here
int repeatprot_configposttest(int *errs) {
	int errors = 0;

	// Let's croak when there are no items in our block, even though the module was loaded
	if(!trigCount) {
		config_error("Empty/No repeatprot::triggers block found"); // Rep0t error
		errors++; // Increment err0r count fam
	}
	*errs = errors;
	return errors ? -1 : 1;
}

// "Run" the config (everything should be valid at this point)
int repeatprot_configrun(ConfigFile *cf, ConfigEntry *ce, int type) {
	ConfigEntry *cep, *cep2; // To store the current variable/value pair etc, nested
	muhExempt *last = NULL; // Initialise to NULL so the loop requires minimal l0gic
	muhExempt **exEntry = &exemptList; // Hecks so the ->next chain stays intact

	// Since we'll add a top-level block to unrealircd.conf, need to filter on CONFIG_MAIN lmao
	if(type != CONFIG_MAIN)
		return 0; // Returning 0 means idgaf bout dis

	// Check for valid config entries first
	if(!ce || !ce->ce_varname)
		return 0;

	// If it isn't repeatprot, idc
	if(strcmp(ce->ce_varname, "repeatprot"))
		return 0;

		// Loop dat shyte fam
	for(cep = ce->ce_entries; cep; cep = cep->ce_next) {
		// Do we even have a valid name l0l?
		if(!cep->ce_varname)
			continue; // Next iteration imo tbh

		// Check for optionals first =]
		if(!strcmp(cep->ce_varname, "action")) {
			switch(cep->ce_vardata[1]) {
				case 'l':
					tklAction = (cep->ce_vardata[0] == 'b' ? 'b' : 'G');
					break;
				case 'i':
					tklAction = 'k';
					break;
				case 'z':
					tklAction = 'Z';
					break;
				default:
					break;
			}
			continue;
		}

		if(!strcmp(cep->ce_varname, "banmsg")) {
			tklMessage = strdup(cep->ce_vardata);
			continue;
		}

		if(!strcmp(cep->ce_varname, "showblocked")) {
			showBlocked = atoi(cep->ce_vardata);
			continue;
		}

		if(!strcmp(cep->ce_varname, "tkltime")) {
			tklTime = config_checkval(cep->ce_vardata, CFG_TIME);
			continue;
		}

		if(!strcmp(cep->ce_varname, "threshold")) {
			repeatThreshold = atoi(cep->ce_vardata);
			continue;
		}

		if(!strcmp(cep->ce_varname, "timespan")) {
			trigTimespan = config_checkval(cep->ce_vardata, CFG_TIME);
			continue;
		}

		// Now check for the repeatprot::triggers bl0qq
		if(!strcmp(cep->ce_varname, "triggers")) {
			// Loop 'em
			for(cep2 = cep->ce_entries; cep2; cep2 = cep2->ce_next) {
				if(!cep2->ce_varname)
					continue; // Next iteration imo tbh

				else if(!strcmp(cep2->ce_varname, "oper"))
					trigOper = 1;

				else if(!strcmp(cep2->ce_varname, "notice"))
					trigNotice = 1;

				else if(!strcmp(cep2->ce_varname, "privmsg"))
					trigPrivmsg = 1;

				else if(!strcmp(cep2->ce_varname, "ctcp"))
					trigCTCP = 1;

				else if(!strcmp(cep2->ce_varname, "invite"))
					trigInvite = 1;
			}
			continue;
		}

		if(!strcmp(cep->ce_varname, "exemptions")) {
			// Loop 'em
			for(cep2 = cep->ce_entries; cep2; cep2 = cep2->ce_next) {
				if(!cep2->ce_varname)
					continue; // Next iteration imo tbh

				// Get size
				size_t masklen = sizeof(char) * (strlen(cep2->ce_varname) + 1);

				// Allocate mem0ry for the current entry
				*exEntry = malloc(sizeof(muhExempt));

				// Allocate/initialise shit here
				(*exEntry)->mask = malloc(masklen);
				(*exEntry)->next = NULL;

				// Copy that shit fam
				strncpy((*exEntry)->mask, cep2->ce_varname, masklen);

				// Premium linked list fam
				if(last)
					last->next = *exEntry;

				last = *exEntry;
				exEntry = &(*exEntry)->next;
			}
			continue;
		}
	}

	return 1; // We good
}

int repeatprot_rehash(void) {
	// Reset em defaults
	repeatThreshold = 3;
	tklAction = 'G';
	tklTime = 60;
	tklMessage = "Nice spam m8";
	showBlocked = 0;
	trigOper = trigNotice = trigPrivmsg = trigCTCP = trigInvite = 0;
	trigCount = trigTimespan = 0;
	return HOOK_CONTINUE;
}

int dropMessage(aClient *cptr, aClient *sptr) {
	switch(tklAction) {
		case 'b':
			blockIt(sptr);
			break;
		case 'k':
			return doKill(cptr, sptr);
		case 'G':
		case 'Z':
			doXLine(tklAction, sptr);
			return FLUSH_BUFFER;
		default:
			break;
	}

	return 0;
}

void blockIt(aClient *sptr) {
	if(showBlocked)
		sendnotice(sptr, "*** Message blocked (%s)", tklMessage);
}

int doKill(aClient *cptr, aClient *sptr) {
	char msg[BUFSIZE];
	snprintf(msg, sizeof(msg), "[%s] %s (%s)", me.name, (MyConnect(sptr) ? "Local kill" : "Killed"), tklMessage);

	char *parv[3] = {
		NULL,
		sptr->name,
		msg
	};

	sendto_snomask_normal(SNO_KILLS, "*** [repeatprot] Received KILL message for %s!%s@%s from %s",
		sptr->name, sptr->user->username,
		IsHidden(sptr) ? sptr->user->virthost : sptr->user->realhost,
		me.name);

	return exit_client(cptr, sptr, &me, msg);
}

void doXLine(char flag, aClient *sptr) {
	// Double check for sptr existing, cuz inb4segfault
	if(sptr) {
		char setTime[100], expTime[100];
		ircsnprintf(setTime, sizeof(setTime), "%li", TStime());
		ircsnprintf(expTime, sizeof(expTime), "%li", TStime() + tklTime);
		char *tkltype = malloc(sizeof(char) * 2);
		tkltype[0] = flag;
		tkltype[1] = '\0';

		// Build TKL args
		char *tkllayer[9] = {
			me.name,
			"+",
			tkltype,
			sptr->user->username,
			(flag == 'Z' ? GetIP(sptr) : sptr->user->realhost),
			me.name,
			expTime,
			setTime,
			tklMessage
		};

		m_tkl(&me, &me, 9, tkllayer); // Ban 'em
		free(tkltype);
	}
}

// Now for the actual override
static int repeatprot_override(Cmdoverride *ovr, aClient *cptr, aClient *sptr, int parc, char *parv[]) {
	/* Gets args: Cmdoverride *ovr, aClient *cptr, aClient *sptr, int parc, char *parv[]
	**
	** ovr: Pointer to the override we're attached to
	** cptr: Pointer to directly attached client -- if remote user this is the remote server instead
	** sptr: Pointer to user executing command -- you'll probably wanna use this fam
	** parc: Amount of arguments (also includes the command in the count)
	** parv: Contains the actual args, first one starts at parv[1]
	**
	** So "NOTICE test" would result in parc = 2 and parv[1] = "test"
	** Also, parv[0] seems to always be NULL, so better not rely on it fam
	*/
	char *cmd; // One override function for multiple commands ftw
	int invite, noticed, oper, privmsg; // "Booleans"
	int exempt; // Is exempted?
	int ctcp, ctcpreply; // CTCP?
	int i, j; // Iterat0rs
	char msg[BUFSIZE]; // Store this message
	char *plaintext, *pr, *pw; // To get a plaintext version of msg (i.e. without bold/underline/etc shit and col0urs)
	char *werd; // Store each w0rd from the message
	aClient *acptr; // Store /message target
	muhMessage *message; // Current/new message struct
	muhExempt *exEntry; // For iteration yo

	// Lest we massively shit ourselves =]
	if(!sptr || BadPtr(parv[1]) || BadPtr(parv[2]) || BadPtr(parv[parc - 1]))
		return CallCmdoverride(ovr, cptr, sptr, parc, parv); // Run original function yo

	// Preemptively allow servers, U:Lines and 0pers
	if(IsServer(sptr) || IsMe(sptr) || IsULine(sptr) || IsOper(sptr))
		return CallCmdoverride(ovr, cptr, sptr, parc, parv); // Run original function yo

	exempt = 0;
	cmd = ovr->command->cmd;
	ctcp = (parv[2][0] == 1 && parv[parc - 1][0] == 1);
	noticed = (!strcmp(cmd, "NOTICE") && !ctcp);
	oper = (!strcmp(cmd, "OPER"));
	privmsg = (!strcmp(cmd, "PRIVMSG") && !ctcp);
	ctcpreply = (!strcmp(cmd, "NOTICE") && ctcp);
	invite = (!strcmp(cmd, "INVITE"));

	for(exEntry = exemptList; exEntry; exEntry = exEntry->next) {
		if(!match(exEntry->mask, make_nick_user_host(sptr->name, sptr->user->username, sptr->user->realhost))) {
			exempt = 1;
			break;
		}
	}

	// Check for enabled triggers, also exclude CTCP _replies_ always ;3
	if(exempt || (noticed && !trigNotice) || (privmsg && !trigPrivmsg) || (oper && !trigOper) || (ctcp && !trigCTCP) || (invite && !trigInvite) || parv[1][0] == '#' || ctcpreply)
		return CallCmdoverride(ovr, cptr, sptr, parc, parv); // Run original function yo

	if(!oper) {
		acptr = find_person(parv[1], NULL); // Attempt to find message target
		// Allow opers always, also messages to self and sending TO ulines ;3
		if(!acptr || IsOper(sptr) || acptr == sptr || IsULine(acptr))
			return CallCmdoverride(ovr, cptr, sptr, parc, parv); // Run original function yo
	}

	// Build full message, leave out spaces on purpose ;]
	memset(msg, '\0', sizeof(msg)); // Premium nullbytes
	for(i = 2; i < parc; i++) {
		char *nTemp = strdup(parv[i]); // Clone that shit cuz we gonna b destructive
		char *sp; // Garbage pointer =]
		for(j = 0; (werd = strtoken(&sp, nTemp, " ")); nTemp = NULL, j++) {
			// Only strncpy on the first pass obv
			if(j == 0) {
				strncpy(msg, werd, strlen(werd));
				// Only care about the user for OPER commands
				if(oper)
					break;
			}
			else
				strncat(msg, werd, strlen(werd));
		}
	}

	// Case-insansativatay imo tbh
	for(i = 0; msg[i]; i++)
		msg[i] = tolower(msg[i]);

	plaintext = StripColors(msg);
	pr = plaintext;
	pw = plaintext;
	while(*pr) {
		*pw = *pr++;
		if(*pw >= 32)
			pw++;
	}
	*pw = '\0';

	message = moddata_client(sptr, repeatprotMDI).ptr; // Get last message info
	// We ain't gottem struct yet
	if(!message) {
		message = malloc(sizeof(muhMessage)); // Alloc8 a fresh strukk
		message->count = (oper ? 0 : 1); // Set count to 1 obv
		message->prev = NULL; // Required
		message->ts = 0;
	}

	// Or we d0
	else {
		// Check if we need to expire the counter first kek
		// trigTimespan == 0 if not specified in the config, meaning never expire
		if(message->ts > 0 && trigTimespan > 0) {
			if(TStime() - message->ts >= trigTimespan) {
				message->count = (oper ? 0 : 1);
			}
		}

		// Nigga may be alternating messages
		if(!strcmp(message->last, plaintext) || (message->prev && !strcmp(message->prev, plaintext)))
			message->count++;
		else  {
			// In case we just blocked it and this isn't a known message, reset the counter to allow it through
			if(message->count >= (repeatThreshold + 1))
				message->count = 1; // Reset it in case it's just a bl0ck lol
		}

		if(message->prev)
			free(message->prev); // Free 'em lmao

		message->prev = strdup(message->last);
		free(message->last);
	}

	message->ts = TStime();
	message->last = strdup(plaintext);
	moddata_client(sptr, repeatprotMDI).ptr = message; // Store that shit within Unreal

	if(message->count >= repeatThreshold) {
		// Let's not reset the counter here, causing it to keep getting blocked ;3
		// No need to free the message object as repeatprot_free flushes it when the client quits anywyas =]
		return dropMessage(cptr, sptr); // R E K T
	}

	return CallCmdoverride(ovr, cptr, sptr, parc, parv); // Run original function yo
}

void repeatprot_free(ModData *md) {
	if(md->ptr) {
		muhMessage *message = md->ptr;
		message->count = 0;
		if(message->last)
			free(message->last);

		if(message->prev)
			free(message->prev);

		md->ptr = NULL;
	}
}
