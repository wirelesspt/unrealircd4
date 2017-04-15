// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

#define MYHEWK HOOKTYPE_PRE_USERMSG
#define MYCONF "pmdelay"

// Muh macros
#define DelHook(x) if(x) HookDel(x); x = NULL

// Quality fowod declarations
char *pmdelay_hook_preusermsg(aClient *sptr, aClient *to, char *text, int notice);
int pmdelay_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int pmdelay_configrun(ConfigFile *cf, ConfigEntry *ce, int type);
int pmdelay_rehash(void);

// Muh globals
static ModuleInfo *pmdelayMI = NULL; // Store ModuleInfo so we can use it to check for errors in MOD_LOAD
Hook *pmdelayHook = NULL;
int muhDelay = 60; // Default to 1 minute yo

// Dat dere module header
ModuleHeader MOD_HEADER(m_pmdelay) = {
	"m_pmdelay", // Module name
	"$Id: v1.0 2017/03/15 Gottem$", // Version
	"Disallow new clients trying to send private messages until exceeding a certain timeout", // Description
	"3.2-b8-1", // Modversion, not sure wat do
	NULL
};

// Configuration testing-related hewks go in testing phase obv
MOD_TEST(m_pmdelay) {
	// We have our own config block so we need to checkem config obv m9
	// Priorities don't really matter here
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, pmdelay_configtest);
	return MOD_SUCCESS;
}

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT(m_pmdelay) {
	pmdelayMI = modinfo; // Store module info yo

	// Add a hook with priority 0 (i.e. normal) that returns a char *
	pmdelayHook = HookAddPChar(modinfo->handle, MYHEWK, 0, pmdelay_hook_preusermsg);

	// Muh config hewks
	HookAdd(modinfo->handle, HOOKTYPE_REHASH, 0, pmdelay_rehash);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, pmdelay_configrun);

	return MOD_SUCCESS; // Let MOD_LOAD handle errors and shyte
}

// Actually load the module here
MOD_LOAD(m_pmdelay) {
	// Check if module handle is available, also check for hooks that weren't added for some raisin
	if(ModuleGetError(pmdelayMI->handle) != MODERR_NOERROR || !pmdelayHook) {
		// Display error string kek
		config_error("A critical error occurred when loading module %s: %s", MOD_HEADER(m_pmdelay).name, ModuleGetErrorStr(pmdelayMI->handle));
		return MOD_FAILED; // No good
	}
	return MOD_SUCCESS; // We good
}

// Called on unload/rehash obv
MOD_UNLOAD(m_pmdelay) {
	// Clean up any structs and other shit
	DelHook(pmdelayHook);
	return MOD_SUCCESS; // We good
}

// Actual hewk functions m8
char *pmdelay_hook_preusermsg(aClient *sptr, aClient *to, char *text, int notice) {
	/* Arg00ments:
	** sptr: Pointer to user executing command -- you'll probably wanna use this fam
	** to: Similar pointer to the target user
	** text: Raw text yo
	** notice: Should be obvious ;];]
	*/
	// Let's allow opers/servers/U:Lines to always send, also from anyone TO U:Lines (muh /ns identify lol)
	if(!IsServer(sptr) && !IsMe(sptr) && !IsOper(sptr) && !IsULine(sptr) && !IsULine(to)) {
		// Sanity check + delay check =]
		if(sptr->local && TStime() - sptr->local->firsttime < muhDelay) {
			sendnotice(sptr, "You have to be connected for at least %d seconds before sending private messages", muhDelay);
			return NULL;
		}
	}
	return text;
}

int pmdelay_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs) {
	int errors = 0; // Error count
	int i; // iter8or m8

	// Since we'll add a directive to the already existing set { } block in unrealircd.conf, need to filter on CONFIG_SET lmao
	if(type != CONFIG_SET)
		return 0; // Returning 0 means idgaf bout dis

	// Check for valid config entries first
	if(!ce || !ce->ce_varname)
		return 0;

	// If it isn't our directive, idc
	if(strcmp(ce->ce_varname, MYCONF))
		return 0;

	// r u insaiyan?
	if(!ce->ce_vardata || !strlen(ce->ce_vardata)) {
		config_error("%s:%i: no argument given for set::%s (should be an integer of >= 10)", ce->ce_fileptr->cf_filename, ce->ce_varlinenum, MYCONF); // Rep0t error
		errors++; // Increment err0r count fam
		*errs = errors;
		return -1; // KBAI
	}

	// Should be an integer yo
	for(i = 0; ce->ce_vardata[i]; i++) {
		if(!isdigit(ce->ce_vardata[i])) {
			config_error("%s:%i: set::%s must be an integer of >= 10", ce->ce_fileptr->cf_filename, ce->ce_varlinenum, MYCONF);
			errors++; // Increment err0r count fam
		}
	}
	if(!errors && atoi(ce->ce_vardata) < 10) {
		config_error("%s:%i: set::%s must be an integer of >= 10", ce->ce_fileptr->cf_filename, ce->ce_varlinenum, MYCONF);
		errors++; // Increment err0r count fam
	}

	*errs = errors;
	return errors ? -1 : 1; // Returning 1 means "all good", -1 means we shat our panties
}

// "Run" the config (everything should be valid at this point)
int pmdelay_configrun(ConfigFile *cf, ConfigEntry *ce, int type) {
	// Since we'll add a directive to the already existing set { } block in unrealircd.conf, need to filter on CONFIG_SET lmao
	if(type != CONFIG_SET)
		return 0; // Returning 0 means idgaf bout dis

	// Check for valid config entries first
	if(!ce || !ce->ce_varname || !ce->ce_vardata)
		return 0;

	// If it isn't pmdelay, idc
	if(strcmp(ce->ce_varname, MYCONF))
		return 0;

	muhDelay = atoi(ce->ce_vardata);
	return 1; // We good
}

int pmdelay_rehash(void) {
	// Reset config defaults
	muhDelay = 60;
	return HOOK_CONTINUE;
}