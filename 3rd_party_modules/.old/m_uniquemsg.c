// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

#define CHMODE_FLAG 'U'

// Dem macros yo
#define IsUMsg(x) ((x) && (x)->mode.extmode & uniquemsg_extcmode)
#define IsMDErr(x, y, z) \
	do { \
		if(!(x)) { \
			config_error("A critical error occurred when registering ModData for %s: %s", MOD_HEADER(y).name, ModuleGetErrorStr((z)->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)

// Quality fowod declarations
int is_chanowner_prot(aClient *sptr, aChannel *chptr);
int wasRepeat(aChannel *chptr, char *text);
void uniquemsg_md_free(ModData *md);
int uniquemsg_chmode_isok(aClient *sptr, aChannel *chptr, char mode, char *para, int checkt, int what);
char *uniquemsg_hook_prechanmsg(aClient *sptr, aChannel *chptr, char *text, int notice);

// Muh globals
static ModuleInfo *uniquemsgMI = NULL; // Store ModuleInfo so we can use it to check for errors in MOD_LOAD
Cmode_t uniquemsg_extcmode = 0L; // Store bitwise value latur
ModDataInfo *umsgMDI; // To store some shit with the channel ;]

// Dat dere module header
ModuleHeader MOD_HEADER(m_uniquemsg) = {
	"m_uniquemsg", // Module name
	"$Id: v1.02 2018/04/16 Gottem$", // Version
	"Implements chmode +U to prevent people from repeating messages", // Description
	"3.2-b8-1", // Modversion, not sure wat do
	NULL
};

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT(m_uniquemsg) {
	uniquemsgMI = modinfo;

	// Request the mode flag
	CmodeInfo cmodereq;
	memset(&cmodereq, 0, sizeof(cmodereq));
	cmodereq.flag = CHMODE_FLAG; // Flag yo
	cmodereq.paracount = 0; // No params needed lol
	cmodereq.is_ok = uniquemsg_chmode_isok; // Custom verification function
	CmodeAdd(modinfo->handle, cmodereq, &uniquemsg_extcmode); // Now finally add the mode lol

	// Request moddata for storing the last message etc
	ModDataInfo mreq;
	memset(&mreq, 0, sizeof(mreq));
	mreq.type = MODDATATYPE_CHANNEL; // Apply to channels only
	mreq.name = "uniquemsg"; // Name it
	mreq.free = uniquemsg_md_free; // Function to free 'em
	umsgMDI = ModDataAdd(modinfo->handle, mreq);
	IsMDErr(umsgMDI, m_uniquemsg, modinfo);

	HookAddPChar(modinfo->handle, HOOKTYPE_PRE_CHANMSG, 0, uniquemsg_hook_prechanmsg);

	return MOD_SUCCESS; // Let MOD_LOAD handle errors
}

// Actually load the module here (also command overrides as they may not exist in MOD_INIT yet)
MOD_LOAD(m_uniquemsg) {
	// Did the module throw an error during initialisation?
	if(ModuleGetError(uniquemsgMI->handle) != MODERR_NOERROR || !uniquemsg_extcmode) {
		// Display error string kek
		config_error("A critical error occurred when loading module %s: %s", MOD_HEADER(m_uniquemsg).name, ModuleGetErrorStr(uniquemsgMI->handle));
		return MOD_FAILED; // No good
	}

	return MOD_SUCCESS; // We good
}

// Called on unload/rehash obv
MOD_UNLOAD(m_uniquemsg) {
	// Clean up any structs and other shit
	return MOD_SUCCESS; // We good
}

int is_chanowner_prot(aClient *sptr, aChannel *chptr) {
	Membership *lp; // For checkin' em list access level =]

	if(IsServer(sptr) || IsMe(sptr)) // Allow servers always lel
		return 1;

	if(chptr) { // Sanity cheqq
		if((lp = find_membership_link(sptr->user->channel, chptr))) {
			#ifdef PREFIX_AQ
				if(lp->flags & (CHFL_CHANOWNER|CHFL_CHANPROT))
			#else
				if(lp->flags & CHFL_CHANOP)
			#endif
				return 1;
		}
	}

	return 0; // No valid channel/membership or doesn't have enough axx lol
}

int wasRepeat(aChannel *chptr, char *text) {
	char *last, *clean;
	int repeat = 0;
	if((last = moddata_channel(chptr, umsgMDI).str)) {
		clean = (char *)StripControlCodes(text); // Strip all markup shit (bold, italikk etc) and colours
		if(clean && !stricmp(last, clean)) // Case-insensitive pls
			repeat = 1;
	}
	return repeat;
}

void uniquemsg_md_free(ModData *md) {
	if(md->str) // r u insaiyan?
		free(md->str);
}

int uniquemsg_chmode_isok(aClient *sptr, aChannel *chptr, char mode, char *para, int checkt, int what) {
	/* Args:
	** sptr: Client who issues the MODE change
	** chptr: Channel to which the MODE change applies
	** mode: The mode character for completeness
	** para: Parameter to the channel mode (will be NULL for paramless modes)
	** checkt: Check type, one of EXCHK_*. Explained later.
	** what: Used to differentiate between adding and removing the mode, one of MODE_ADD or MODE_DEL
	*/

	/* Access types:
	** EXCHK_ACCESS: Verify if the user may (un)set the mode, do NOT send error messages for this (just check access)
	** EXCHK_ACCESS_ERR: Similar to above, but you SHOULD send an error message here
	** EXCHK_PARAM: Check the sanity of the parameter(s)
	*/

	/* Return values:
	** EX_ALLOW: Allow it
	** EX_DENY: Deny for most people (only IRC opers w/ override may use it)
	** EX_ALWAYS_DENY: Even prevent IRC opers from overriding shit
	*/
	if((checkt == EXCHK_ACCESS) || (checkt == EXCHK_ACCESS_ERR)) { // Access check lol
		// Check if the user has +a or +q (OperOverride automajikally overrides this bit ;])
		if(!is_chanowner_prot(sptr, chptr)) {
			if(checkt == EXCHK_ACCESS_ERR)
				sendto_one(sptr, err_str(ERR_CHANOWNPRIVNEEDED), me.name, sptr->name, chptr->chname);
			return EX_DENY;
		}
		return EX_ALLOW;
	}
	return EX_ALLOW; // Falltrough, like when someone attempts +U 10 it'll simply do +U
}

char *uniquemsg_hook_prechanmsg(aClient *sptr, aChannel *chptr, char *text, int notice) {
	char *clean;
	if(IsUMsg(chptr)) { // Only do shit when the chmode is set =]
		if(!is_chanownprotop(sptr, chptr) && wasRepeat(chptr, text)) { // People with +o and higher are exempt from the limitation
			sendto_one(sptr, ":%s NOTICE %s :This channel doesn't allow repeated messages", me.name, chptr->chname);
			return NULL; // Then fuck off =]
		}
		if(moddata_channel(chptr, umsgMDI).str) // Already got a string
			free(moddata_channel(chptr, umsgMDI).str); // Free it first
		clean = (char *)StripControlCodes(text);
		moddata_channel(chptr, umsgMDI).str = (clean ? strdup(clean) : NULL); // Then dup 'em
	}
	return text;
}

