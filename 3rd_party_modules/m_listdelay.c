// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

#define MYCONF "listdelay"
#define OVR_LIST "LIST"

// Muh macros
#define DelOverride(cmd, ovr) if(ovr && CommandExists(cmd)) CmdoverrideDel(ovr); ovr = NULL; // Unregister override

// Quality fowod declarations
static int listdelay_override(Cmdoverride *ovr, aClient *cptr, aClient *sptr, int parc, char *parv[]);
int listdelay_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int listdelay_configrun(ConfigFile *cf, ConfigEntry *ce, int type);
int listdelay_rehash(void);

// Muh globals
static ModuleInfo *listdelayMI = NULL; // Store ModuleInfo so we can use it to check for errors in MOD_LOAD
Cmdoverride *listdelayOVR; // Pointer to the override we're gonna add
int muhDelay = 60; // Default to 1 minute yo

// Dat dere module header
ModuleHeader MOD_HEADER(m_listdelay) = {
	"m_listdelay", // Module name
	"$Id: v1.0 2017/03/17 Gottem$", // Version
	"Disallow new clients trying list all channels until exceeding a certain timeout", // Description
	"3.2-b8-1", // Modversion, not sure wat do
	NULL
};

// Configuration testing-related hewks go in testing phase obv
MOD_TEST(m_listdelay) {
	// We have our own config block so we need to checkem config obv m9
	// Priorities don't really matter here
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, listdelay_configtest);
	return MOD_SUCCESS;
}

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT(m_listdelay) {
	listdelayMI = modinfo; // Store module info yo

	// Muh config hewks
	HookAdd(modinfo->handle, HOOKTYPE_REHASH, 0, listdelay_rehash);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, listdelay_configrun);

	return MOD_SUCCESS; // Let MOD_LOAD handle errors and shyte
}

// Actually load the module here
MOD_LOAD(m_listdelay) {
	listdelayOVR = CmdoverrideAdd(listdelayMI->handle, OVR_LIST, listdelay_override); // Attempt to add command override
	// Check if module handle is available, also check for hooks that weren't added for some raisin
	if(ModuleGetError(listdelayMI->handle) != MODERR_NOERROR || !listdelayOVR) {
		// Display error string kek
		config_error("A critical error occurred when loading module %s: %s", MOD_HEADER(m_listdelay).name, ModuleGetErrorStr(listdelayMI->handle));
		return MOD_FAILED; // No good
	}
	return MOD_SUCCESS; // We good
}

// Called on unload/rehash obv
MOD_UNLOAD(m_listdelay) {
	// Attempt to free override
	DelOverride(OVR_LIST, listdelayOVR);
	return MOD_SUCCESS; // We good
}

// Now for the actual override
static int listdelay_override(Cmdoverride *ovr, aClient *cptr, aClient *sptr, int parc, char *parv[]) {
	/* Gets args: Cmdoverride *ovr, aClient *cptr, aClient *sptr, int parc, char *parv[]
	**
	** ovr: Pointer to the override we're attached to
	** cptr: Pointer to directly attached client -- if remote user this is the remote server instead
	** sptr: Pointer to user executing command -- you'll probably wanna use this fam
	** parc: Amount of arguments (also includes the command in the count)
	** parv: Contains the actual args, first one starts at parv[1]
	**
	** So "LIST test" would result in parc = 2 and parv[1] = "test"
	** Also, parv[0] seems to always be NULL, so better not rely on it fam
	*/

	// Let's allow opers/servers/U:Lines shall we? =]
	if(MyConnect(sptr) && !IsServer(sptr) && !IsMe(sptr) && !IsOper(sptr) && !IsULine(sptr)) {
		// Sanity check + delay check =]
		if(sptr->local && TStime() - sptr->local->firsttime < muhDelay) {
			sendnotice(sptr, "You have to be connected for at least %d seconds before being able to /%s", muhDelay, OVR_LIST);
			return 0;
		}
	}
	return CallCmdoverride(ovr, cptr, sptr, parc, parv); // Run original function yo
}

int listdelay_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs) {
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
int listdelay_configrun(ConfigFile *cf, ConfigEntry *ce, int type) {
	// Since we'll add a directive to the already existing set { } block in unrealircd.conf, need to filter on CONFIG_SET lmao
	if(type != CONFIG_SET)
		return 0; // Returning 0 means idgaf bout dis

	// Check for valid config entries first
	if(!ce || !ce->ce_varname || !ce->ce_vardata)
		return 0;

	// If it isn't listdelay, idc
	if(strcmp(ce->ce_varname, MYCONF))
		return 0;

	muhDelay = atoi(ce->ce_vardata);
	return 1; // We good
}

int listdelay_rehash(void) {
	// Reset config defaults
	muhDelay = 60;
	return HOOK_CONTINUE;
}