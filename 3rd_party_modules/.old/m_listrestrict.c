// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

#define MYCONF "listrestrict"
#define OVR_LIST "LIST"

// Big hecks go here
typedef struct t_restrictex restrictExcept;
struct t_restrictex {
	char type;
	char *mask;
	restrictExcept *next;
};

// Quality fowod declarations
static int listrestrict_override(Cmdoverride *ovr, aClient *cptr, aClient *sptr, int parc, char *parv[]);
int listrestrict_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int listrestrict_configrun(ConfigFile *cf, ConfigEntry *ce, int type);
int listrestrict_rehash(void);

// Muh globals
static ModuleInfo *lrestrictMI = NULL; // Store ModuleInfo so we can use it to check for errors in MOD_LOAD
Cmdoverride *lrestrictOVR; // Pointer to the override we're gonna add
restrictExcept *exceptList = NULL; // Stores exceptions yo

// Deez defaults
int muhDelay = 0; // Default to off yo
unsigned short needAuth = 0; // Must be identified w/ NickServ

// Dat dere module header
ModuleHeader MOD_HEADER(m_listrestrict) = {
	"m_listrestrict", // Module name
	"$Id: v1.02 2017/09/02 Gottem$", // Version
	"Impose certain restrictions on /LIST usage", // Description
	"3.2-b8-1", // Modversion, not sure wat do
	NULL
};

// Configuration testing-related hewks go in testing phase obv
MOD_TEST(m_listrestrict) {
	// We have our own config block so we need to checkem config obv m9
	// Priorities don't really matter here
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, listrestrict_configtest);
	return MOD_SUCCESS;
}

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT(m_listrestrict) {
	lrestrictMI = modinfo; // Store module info yo

	// Muh config hewks
	HookAdd(modinfo->handle, HOOKTYPE_REHASH, 0, listrestrict_rehash);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, listrestrict_configrun);

	return MOD_SUCCESS; // Let MOD_LOAD handle errors and shyte
}

// Actually load the module here
MOD_LOAD(m_listrestrict) {
	lrestrictOVR = CmdoverrideAdd(lrestrictMI->handle, OVR_LIST, listrestrict_override); // Attempt to add command override
	// Check if module handle is available, also check for hooks that weren't added for some raisin
	if(ModuleGetError(lrestrictMI->handle) != MODERR_NOERROR || !lrestrictOVR) {
		// Display error string kek
		config_error("A critical error occurred when loading module %s: %s", MOD_HEADER(m_listrestrict).name, ModuleGetErrorStr(lrestrictMI->handle));
		return MOD_FAILED; // No good
	}
	return MOD_SUCCESS; // We good
}

// Called on unload/rehash obv
MOD_UNLOAD(m_listrestrict) {
	if(exceptList) {
		// This shit is a bit convoluted to prevent memory issues obv famalmalmalmlmalm
		restrictExcept *exEntry;
		while((exEntry = exceptList) != NULL) {
			exceptList = exceptList->next;
			free(exEntry);
		}
		exceptList = NULL;
	}

	return MOD_SUCCESS; // We good
}

// Now for the actual override
static int listrestrict_override(Cmdoverride *ovr, aClient *cptr, aClient *sptr, int parc, char *parv[]) {
	/* Gets args: Cmdoverride *ovr, aClient *cptr, aClient *sptr, int parc, char *parv[]
	**
	** ovr: Pointer to the override we're attached to
	** cptr: Pointer to directly attached client -- if remote user this is the remote server instead
	** sptr: Pointer to user executing command
	** parc: Amount of arguments (also includes the command in the count)
	** parv: Contains the actual args, first one starts at parv[1]
	**
	** So "LIST test" would result in parc = 2 and parv[1] = "test"
	** Also, parv[0] seems to always be NULL, so better not rely on it fam
	*/
	restrictExcept *exEntry; // For iteration yo
	unsigned short except_connect = 0; // We gottem exception?
	unsigned short except_auth = 0; // Ditt0

	// Checkem exceptions bro
	if(!MyConnect(sptr) || !IsPerson(sptr) || IsOper(sptr) || IsULine(sptr)) { // Default set lel
		except_connect = 1;
		except_auth = 1;
	}
	else { // Not an oper/U:Line/server, checkem whitelist (if ne)
		for(exEntry = exceptList; exEntry; exEntry = exEntry->next) {
			if(!match(exEntry->mask, make_user_host(sptr->user->username, sptr->user->realhost)) || !match(exEntry->mask, make_user_host(sptr->user->username, sptr->ip))) {
				switch(exEntry->type) {
					case '*':
						except_connect = 1;
						except_auth = 1;
						break;
					case 'a':
						except_auth = 1;
						break;
					case 'c':
						except_connect = 1;
						break;
					default:
						break;
				}
				break;
			}
		}
	}

	// Sanity check + delay check =]
	if(!except_connect && muhDelay > 0 && sptr->local && TStime() - sptr->local->firsttime < muhDelay) {
		sendnotice(sptr, "You have to be connected for at least %d seconds before being able to /%s", muhDelay, OVR_LIST);
		return 0;
	}

	// Need identified check ;];;]
	if(!except_auth && needAuth && !IsLoggedIn(sptr)) {
		sendnotice(sptr, "You have to be identified with services before being able to /%s", OVR_LIST);
		return 0;
	}

	return CallCmdoverride(ovr, cptr, sptr, parc, parv); // Run original function yo
}

int listrestrict_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs) {
	ConfigEntry *cep, *cep2; // For looping through our bl0cc, nested
	int errors = 0; // Error count
	int i; // iter8or m8

	// Since we'll add a new top-level block to unrealircd.conf, need to filter on CONFIG_MAIN lmao
	if(type != CONFIG_MAIN)
		return 0; // Returning 0 means idgaf bout dis

	// Check for valid config entries first
	if(!ce || !ce->ce_varname)
		return 0;

	// If it isn't our bl0ck, idc
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

		if(!strcmp(cep->ce_varname, "connectdelay")) {
			if(!cep->ce_vardata) {
				config_error("%s:%i: %s::%s must be an integer of >= 10", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
				continue; // Next iteration imo tbh
			}
			// Should be an integer yo
			for(i = 0; cep->ce_vardata[i]; i++) {
				if(!isdigit(cep->ce_vardata[i])) {
					config_error("%s:%i: %s::%s must be an integer of >= 10", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
					errors++; // Increment err0r count fam
					break;
				}
			}
			if(!errors && atoi(cep->ce_vardata) < 10) {
				config_error("%s:%i: %s::%s must be an integer of >= 10", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
			}
			continue;
		}

		if(!strcmp(cep->ce_varname, "needauth")) {
			if(!cep->ce_vardata || (strcmp(cep->ce_vardata, "0") && strcmp(cep->ce_vardata, "1"))) {
				config_error("%s:%i: %s::%s must be either 0 or 1 fam", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
			}
			continue;
		}

		// Here comes a nested block =]
		if(!strcmp(cep->ce_varname, "exceptions")) {
			// Loop 'em again
			for(cep2 = cep->ce_entries; cep2; cep2 = cep2->ce_next) {
				if(!cep2->ce_varname || !cep2->ce_vardata) {
					config_error("%s:%i: blank/incomplete %s::exceptions entry", cep2->ce_fileptr->cf_filename, cep2->ce_varlinenum, MYCONF); // Rep0t error
					errors++; // Increment err0r count fam
					continue; // Next iteration imo tbh
				}

				if(strcmp(cep2->ce_varname, "connect") && strcmp(cep2->ce_varname, "auth") && strcmp(cep2->ce_varname, "all")) {
					config_error("%s:%i: invalid %s::exceptions type (must be one of: connect, auth, all)", cep2->ce_fileptr->cf_filename, cep2->ce_varlinenum, MYCONF); // Rep0t error
					errors++; // Increment err0r count fam
					continue; // Next iteration imo tbh
				}

				if(!match("*!*@*", cep2->ce_vardata) || match("*@*", cep2->ce_vardata) || strlen(cep2->ce_vardata) < 3) {
					config_error("%s:%i: invalid %s::exceptions mask (must be of the format ident@hostip)", cep2->ce_fileptr->cf_filename, cep2->ce_varlinenum, MYCONF); // Rep0t error
					errors++; // Increment err0r count fam
					continue; // Next iteration imo tbh
				}
			}
			continue;
		}

		// Anything else is unknown to us =]
		config_warn("%s:%i: unknown item %s::%s", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname); // So display just a warning
	}

	*errs = errors;
	return errors ? -1 : 1; // Returning 1 means "all good", -1 means we shat our panties
}

// "Run" the config (everything should be valid at this point)
int listrestrict_configrun(ConfigFile *cf, ConfigEntry *ce, int type) {
	ConfigEntry *cep, *cep2; // For looping through our bl0cc, nested
	restrictExcept *last = NULL; // Initialise to NULL so the loop requires minimal l0gic
	restrictExcept **exEntry = &exceptList; // Hecks so the ->next chain stays intact

	// Since we'll add a new top-level block to unrealircd.conf, need to filter on CONFIG_MAIN lmao
	if(type != CONFIG_MAIN)
		return 0; // Returning 0 means idgaf bout dis

	// Check for valid config entries first
	if(!ce || !ce->ce_varname)
		return 0;

	// If it isn't our bl0cc, idc
	if(strcmp(ce->ce_varname, MYCONF))
		return 0;

	// Loop dat shyte fam
	for(cep = ce->ce_entries; cep; cep = cep->ce_next) {
		// Do we even have a valid name l0l?
		if(!cep->ce_varname)
			continue; // Next iteration imo tbh

		if(cep->ce_vardata && !strcmp(cep->ce_varname, "connectdelay")) {
			muhDelay = atoi(cep->ce_vardata);
			continue;
		}

		if(cep->ce_vardata && !strcmp(cep->ce_varname, "needauth")) {
			needAuth = atoi(cep->ce_vardata);
			continue;
		}

		if(!strcmp(cep->ce_varname, "exceptions")) {
			// Loop 'em
			for(cep2 = cep->ce_entries; cep2; cep2 = cep2->ce_next) {
				if(!cep2->ce_varname || !cep2->ce_vardata) // Sanity checks imo
					continue; // Next iteration imo tbh

				size_t masklen = sizeof(char) * (strlen(cep2->ce_vardata) + 1); // Get size
				char etype; // Wat type etc

				if(!strcmp(cep2->ce_varname, "all"))
					etype = '*';
				else if(!strcmp(cep2->ce_varname, "auth"))
					etype = 'a';
				else if(!strcmp(cep2->ce_varname, "connect"))
					etype = 'c';

				// Allocate mem0ry for the current entry
				*exEntry = malloc(sizeof(restrictExcept));

				// Allocate/initialise shit here
				(*exEntry)->mask = malloc(masklen);
				(*exEntry)->next = NULL;

				// Copy that shit fam
				(*exEntry)->type = etype;
				strncpy((*exEntry)->mask, cep2->ce_vardata, masklen);

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

int listrestrict_rehash(void) {
	// Reset config defaults
	muhDelay = 0;
	needAuth = 0;
	return HOOK_CONTINUE;
}
