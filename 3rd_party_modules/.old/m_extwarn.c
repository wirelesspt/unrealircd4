// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

// Quality fowod declarations
void extwarn_main(void); // Void cuz only throws warnings, so no return value is required =]

// Muh globals
static ModuleInfo *extwarnMI = NULL; // Store ModuleInfo so we can use it to check for errors in MOD_LOAD

// Dat dere module header
ModuleHeader MOD_HEADER(m_extwarn) = {
	"m_extwarn", // Module name
	"$Id: v1.0 2017/05/18 Gottem$", // Version
	"Enables additional configuration error checking", // Description
	"3.2-b8-1", // Modversion, not sure wat do
	NULL
};

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT(m_extwarn) {
	extwarnMI = modinfo;
	return MOD_SUCCESS; // Let MOD_LOAD handle errors
}

// Actually load the module here (also command overrides as they may not exist in MOD_INIT yet)
MOD_LOAD(m_extwarn) {
	// Did the module throw an error during initialisation?
	if(ModuleGetError(extwarnMI->handle) != MODERR_NOERROR) {
		// Display error string kek
		config_error("A critical error occurred when loading module %s: %s", MOD_HEADER(m_extwarn).name, ModuleGetErrorStr(extwarnMI->handle));
		return MOD_FAILED; // No good
	}
	extwarn_main(); // Throws warnings only as to not break shit ;];]
	return MOD_SUCCESS;
}

// Called on unload/rehash obv
MOD_UNLOAD(m_extwarn) {
	return MOD_SUCCESS; // We good
}

void extwarn_main(void) {
	// Check for missing operclasses etc
	ConfigItem_oper *oper;
	ConfigItem_operclass *operclass;
	for(oper = conf_oper; oper; oper = (ConfigItem_oper *)oper->next) { // Checkem configured opers
		if(!(operclass = Find_operclass(oper->operclass))) // None found, throw warning yo
			config_warn("[extwarn] Unknown operclass '%s' found in oper block for '%s'", oper->operclass, oper->name);
	}
}