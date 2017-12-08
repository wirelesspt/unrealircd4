// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

// Command to override
#define OVR_PRIVMSG "PRIVMSG"
#define OVR_NOTICE "NOTICE"

// Config block
#define MYCONF "anticaps"

// Quality fowod declarations
int anticaps_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int anticaps_configrun(ConfigFile *cf, ConfigEntry *ce, int type);
int anticaps_rehash(void);
static int anticaps_override(Cmdoverride *ovr, aClient *cptr, aClient *sptr, int parc, char *parv[]);

// Muh globals
static ModuleInfo *anticapsMI = NULL; // Store ModuleInfo so we can use it to check for errors in MOD_LOAD
Cmdoverride *anticapsOVR, *anticapsOVR2; // Pointers to the overrides we're gonna add
int capsLimit = 50; // Default to blocking >= 50% caps
int minLength = 30; // Minimum length of 30 before we checkem
int lcIt = 0; // Lowercase 'em instead

// Dat dere module header
ModuleHeader MOD_HEADER(m_anticaps) = {
	"m_anticaps", // Module name
	"$Id: v1.04 2017/09/13 Gottem$", // Version
	"Block messages that contain a configurable amount of capital letters", // Description
	"3.2-b8-1", // Modversion, not sure wat do
	NULL
};

// Configuration testing-related hewks go in testing phase obv
MOD_TEST(m_anticaps) {
	// We have our own config block so we need to checkem config obv m9
	// Priorities don't really matter here
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, anticaps_configtest);
	return MOD_SUCCESS;
}

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT(m_anticaps) {
	HookAdd(modinfo->handle, HOOKTYPE_REHASH, 0, anticaps_rehash);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, anticaps_configrun);
	anticapsMI = modinfo; // Store module info yo
	return MOD_SUCCESS; // Let MOD_LOAD handle errors and registering of overrides
}

// Actually load the module here (also command overrides as they may not exist in MOD_INIT yet)
MOD_LOAD(m_anticaps) {
	anticapsOVR = CmdoverrideAdd(anticapsMI->handle, OVR_PRIVMSG, anticaps_override); // Attempt to add command override for PRIVMSG
	anticapsOVR2 = CmdoverrideAdd(anticapsMI->handle, OVR_NOTICE, anticaps_override); // Attempt to add command override for NOTICE

	// Did the module throw an error when adding override(s), or is anticapsOVR(2) null even?
	if(ModuleGetError(anticapsMI->handle) != MODERR_NOERROR || !anticapsOVR || !anticapsOVR2) {
		// Display error string kek
		config_error("A critical error occurred when loading module %s: %s", MOD_HEADER(m_anticaps).name, ModuleGetErrorStr(anticapsMI->handle));
		return MOD_FAILED; // No good
	}

	return MOD_SUCCESS; // We good
}

// Called on unload/rehash obv
MOD_UNLOAD(m_anticaps) {
	return MOD_SUCCESS; // We good
}

int anticaps_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs) {
	int errors = 0; // Error count
	int i, limit; // Iterat0r
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
		if(!cep->ce_varname || !cep->ce_vardata) {
			config_error("%s:%i: blank %s item", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF); // Rep0t error
			errors++; // Increment err0r count fam
			continue; // Next iteration imo tbh
		}

		if(!strcmp(cep->ce_varname, "capslimit")) {
			limit = atoi(cep->ce_vardata);
			if(limit <= 0 || limit > 100) {
				config_error("%s:%i: %s::capslimit must be an integer from 1 to 100 (represents a percentage)", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF);
				errors++;
			}
			continue;
		}

		if(!strcmp(cep->ce_varname, "minlength")) {
			for(i = 0; cep->ce_vardata[i]; i++) {
				if(!isdigit(cep->ce_vardata[i])) {
					config_error("%s:%i: %s::minlength must be an integer of zero or larger m8", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF);
					errors++; // Increment err0r count fam
					break;
				}
			}
			continue;
		}

		if(!strcmp(cep->ce_varname, "lowercase_it")) {
			if(!cep->ce_vardata || (strcmp(cep->ce_vardata, "0") && strcmp(cep->ce_vardata, "1"))) {
				config_error("%s:%i: %s::lowercase_it must be either 0 or 1 fam", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF);
				errors++; // Increment err0r count fam
			}
			continue;
		}
	}

	*errs = errors;
	// Returning 1 means "all good", -1 means we shat our panties
	return errors ? -1 : 1;
}

// "Run" the config (everything should be valid at this point)
int anticaps_configrun(ConfigFile *cf, ConfigEntry *ce, int type) {
	ConfigEntry *cep; // To store the current variable/value pair etc, nested

	// Since we'll add a top-level block to unrealircd.conf, need to filter on CONFIG_MAIN lmao
	if(type != CONFIG_MAIN)
		return 0; // Returning 0 means idgaf bout dis

	// Check for valid config entries first
	if(!ce || !ce->ce_varname)
		return 0;

	// If it isn't anticaps, idc
	if(strcmp(ce->ce_varname, MYCONF))
		return 0;

	for(cep = ce->ce_entries; cep; cep = cep->ce_next) {
		// Do we even have a valid name l0l?
		if(!cep->ce_varname || !cep->ce_vardata)
			continue; // Next iteration imo tbh

		if(!strcmp(cep->ce_varname, "capslimit")) {
			capsLimit = atoi(cep->ce_vardata);
			continue;
		}

		if(!strcmp(cep->ce_varname, "minlength")) {
			minLength = atoi(cep->ce_vardata);
			continue;
		}

		if(!strcmp(cep->ce_varname, "lowercase_it")) {
			lcIt = atoi(cep->ce_vardata);
			continue;
		}
	}

	return 1; // We good
}

int anticaps_rehash(void) {
	// Reset config defaults
	capsLimit = 50;
	minLength = 30;
	lcIt = 0;
	return HOOK_CONTINUE;
}

// Now for the actual override
static int anticaps_override(Cmdoverride *ovr, aClient *cptr, aClient *sptr, int parc, char *parv[]) {
	/* Gets args: Cmdoverride *ovr, aClient *cptr, aClient *sptr, int parc, char *parv[]
	**
	** ovr: Pointer to the override we're attached to
	** cptr: Pointer to directly attached client -- if remote user this is the remote server instead
	** sptr: Pointer to user executing command
	** parc: Amount of arguments (also includes the command in the count)
	** parv: Contains the actual args, first one starts at parv[1]
	**
	** So "PRIVMSG test" would result in parc = 2 and parv[1] = "test"
	** Also, parv[0] seems to always be NULL, so better not rely on it fam
	*/
	char *plaintext; // We gonna fix up da string fam
	int perc; // Store percentage etc
	int i, len, caps; // To count full length as well as caps

	if(BadPtr(parv[1]) || BadPtr(parv[2]) || !sptr || IsULine(sptr) || IsServer(sptr) || IsMe(sptr) || IsOper(sptr) || strlen(parv[2]) < minLength)
		return CallCmdoverride(ovr, cptr, sptr, parc, parv); // Run original function yo

	char p[strlen(parv[2]) + 1]; // Let's not modify parv[2] directly =]
	strncpy(p, parv[2], sizeof(p)); // Copy that shit fam

	// Some shitty ass scripts may use different colours/markup across chans, so fuck that
	if(!(plaintext = (char *)StripColors(p)) || !(plaintext = (char *)StripControlCodes(plaintext)))
		return CallCmdoverride(ovr, cptr, sptr, parc, parv);

	perc = len = caps = 0;

	for(i = 0; plaintext[i]; i++) {
		if(plaintext[i] == 32) // Let's skip spaces too
			continue;

		if(plaintext[i] >= 65 && plaintext[i] <= 90) // Premium ASCII yo
			caps++;
		len++;
	}

	if(!caps || !len) // Inb4division by zero lmao
		return CallCmdoverride(ovr, cptr, sptr, parc, parv); // Run original function yo

	perc = (int)(((float)caps / (float)len) * 100);
	if(perc >= capsLimit) {
		if(!lcIt) { // If not configured to lowercase em, deny/drop the message
			sendnotice(sptr, "*** Cannot send to %s: your message contains too many capital letters (%d%% >= %d%%)", parv[1], perc, capsLimit);
			return 0; // Stop processing yo
		}

		if(*plaintext == '\001') { // Might be an ACTION or CTCP
			if(len >= 7 && !strncmp(&plaintext[1], "ACTION ", 7)) // Let's not lowercase the ACTION bit ;];];]];]
				i = 8; // Skippem
			else if(plaintext[len - 1] == '\001') // Not an ACTION so it's a CTCP, ignore it all
				i = len + 1; // Dirty shit =]
		}
		else
			i = 0; // No CTCP/ACTION found, lowercase all lol

		for(; plaintext[i]; i++)
			plaintext[i] = tolower(plaintext[i]);
		parv[2] = plaintext;
	}

	return CallCmdoverride(ovr, cptr, sptr, parc, parv); // Run original function yo
}
