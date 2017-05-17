// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

// Hooktype to use
#define MYHEWK HOOKTYPE_PRE_USERMSG

// Muh macros
#define DelHook(x) if(x) HookDel(x); x = NULL

#define BACKPORT (UNREAL_VERSION_GENERATION == 4 && UNREAL_VERSION_MAJOR == 0 && UNREAL_VERSION_MINOR < 12)

// Quality fowod declarations
char *allowctcp_opers_preusermsg(aClient *sptr, aClient *to, char *text, int notice);
static int IsACTCP(char *s);

#if BACKPORT
	long find_user_mode(char flag);
	int has_user_mode(aClient *acptr, char mode);
#endif

// Muh globals
static ModuleInfo *allowctcp_opersMI = NULL; // Store ModuleInfo so we can use it to check for errors in MOD_LOAD
Hook *preUMsgHook = NULL;

// Dat dere module header
ModuleHeader MOD_HEADER(m_allowctcp_opers) = {
	"m_allowctcp_opers", // Module name
	"$Id: v1.0 2017/05/13 Gottem$", // Version
	"Allows opers to override someone's umode +T (noctcp)", // Description
	"3.2-b8-1", // Modversion, not sure wat do
	NULL
};

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT(m_allowctcp_opers) {
	allowctcp_opersMI = modinfo; // Store module info yo
	preUMsgHook = HookAddPChar(modinfo->handle, MYHEWK, -1, allowctcp_opers_preusermsg); // Add a hook with higher priority than noctcp_preusermsg()
	return MOD_SUCCESS; // Let MOD_LOAD handle errors and shyte
}

// Actually load the module here
MOD_LOAD(m_allowctcp_opers) {
	// Check if module handle is available, also check for hooks that weren't added for some raisin
	if(ModuleGetError(allowctcp_opersMI->handle) != MODERR_NOERROR || !preUMsgHook) {
		// Display error string kek
		config_error("A critical error occurred when loading module %s: %s", MOD_HEADER(m_allowctcp_opers).name, ModuleGetErrorStr(allowctcp_opersMI->handle));
		return MOD_FAILED; // No good
	}
	return MOD_SUCCESS; // We good
}

// Called on unload/rehash obv
MOD_UNLOAD(m_allowctcp_opers) {
	DelHook(preUMsgHook); // Delete hewk
	return MOD_SUCCESS; // We good
}

// Actual hewk functions m8
char *allowctcp_opers_preusermsg(aClient *sptr, aClient *to, char *text, int notice) {
	/* Arguments depend on hook type used obv
	**
	** sptr: Pointer to user executing command
	** to: Same sort of pointer to the target client
	** text: Actual text obv
	** notice: Was it a notice?
	*/
	if(MyClient(sptr) && has_user_mode(to, 'T') && IsACTCP(text) && IsOper(sptr)) {
		// Dirty hack goes here lol
		sendto_one(to, ":%s %s %s :%s", sptr->name, (notice ? "NOTICE" : "PRIVMSG"), to->name, text);
		return NULL;
	}
	return text; // Process normally
}

// Ripped from src/modules/usermodes/noctcp.c =]
static int IsACTCP(char *s) {
	if(!s)
		return 0;
	if((*s == '\001') && strncmp(&s[1], "ACTION ", 7) && strncmp(&s[1], "DCC ", 4))
		return 1;
	return 0;
}

#if BACKPORT
	// Both functions were ripped from src/umodes.c in U4.0.12 =]
	long find_user_mode(char flag) {
		int i;
		for (i = 0; i < UMODETABLESZ; i++) {
			if((Usermode_Table[i].flag == flag) && !(Usermode_Table[i].unloaded))
				return Usermode_Table[i].mode;
		}
		return 0;
	}

	int has_user_mode(aClient *acptr, char mode) {
		long m = find_user_mode(mode);
		if(acptr->umodes & m)
			return 1; // We gottem

		return 0;
	}
#endif