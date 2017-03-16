// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

#define MYCONF "autovhost"
#define HEWK HOOKTYPE_LOCAL_CONNECT

// Big hecks go here
typedef struct t_vhostEntry vhostEntry;
struct t_vhostEntry {
	char *mask;
	char *vhost;
	vhostEntry *next;
};

// Quality fowod declarations
int autovhost_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int autovhost_configposttest(int *errs);
int autovhost_configrun(ConfigFile *cf, ConfigEntry *ce, int type);
int autovhost_connect(aClient *sptr);

// Muh globals
static ModuleInfo *autovhostMI = NULL; // Store ModuleInfo so we can use it to check for errors in MOD_LOAD
vhostEntry *vhostList = NULL; // Stores the mask <-> vhost pairs
int vhostCount = 0;

// Dat dere module header
ModuleHeader MOD_HEADER(m_autovhost) = {
	"m_autovhost", // Module name
	"$Id: v1.02 2017/03/11 Gottem$", // Version
	"Apply vhosts at connect time based on users' raw nick formats or IPs", // Description
	"3.2-b8-1", // Modversion, not sure wat do
	NULL
};

// Configuration testing-related hewks go in testing phase obv
// This function is entirely optional
MOD_TEST(m_autovhost) {
	// We have our own config block so we need to checkem config obv m9
	// Priorities don't really matter here
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, autovhost_configtest);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGPOSTTEST, 0, autovhost_configposttest);
	return MOD_SUCCESS;
}

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT(m_autovhost) {
	autovhostMI = modinfo; // Store module info yo
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, autovhost_configrun);
	HookAdd(modinfo->handle, HOOKTYPE_LOCAL_CONNECT, 0, autovhost_connect);
	return MOD_SUCCESS; // Let MOD_LOAD handle errors and registering of overrides
}

MOD_LOAD(m_autovhost) {
	// Check if module handle is available, also check for overrides/commands that weren't added for some raisin
	if(ModuleGetError(autovhostMI->handle) != MODERR_NOERROR) {
		// Display error string kek
		config_error("A critical error occurred when loading module %s: %s", MOD_HEADER(m_autovhost).name, ModuleGetErrorStr(autovhostMI->handle));
		return MOD_FAILED; // No good
	}

	return MOD_SUCCESS; // We good
}

// Called on unload/rehash obv
MOD_UNLOAD(m_autovhost) {
	if(vhostList) {
		// This shit is a bit convoluted to prevent memory issues obv famalmalmalmlmalm
		vhostEntry *vEntry;
		while((vEntry = vhostList) != NULL) {
			vhostList = vhostList->next;
			free(vEntry);
		}
		vhostList = NULL;
	}
	vhostCount = 0; // Just to maek shur
	return MOD_SUCCESS; // We good
}

int autovhost_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs) {
	int errors = 0; // Error count
	int i; // Iterat0r
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

		if(!cep->ce_vardata) {
			config_error("%s:%i: blank %s value", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF); // Rep0t error
			errors++; // Increment err0r count fam
			continue; // Next iteration imo tbh
		}

		if(strlen(cep->ce_vardata) <= 5) {
			config_error("%s:%i: vhost should be at least 5 characters (%s)", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, cep->ce_vardata); // Rep0t error
			errors++; // Increment err0r count fam
			continue; // Next iteration imo tbh
		}

		if(strchr(cep->ce_vardata, '@')) {
			config_error("%s:%i: only use the hostname part for vhosts (%s)", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, cep->ce_vardata); // Rep0t error
			errors++; // Increment err0r count fam
			continue; // Next iteration imo tbh
		}

		if(strchr(cep->ce_vardata, '!')) {
			config_error("%s:%i: vhosts can't contain a nick or ident part (%s)", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, cep->ce_vardata); // Rep0t error
			errors++; // Increment err0r count fam
			continue; // Next iteration imo tbh
		}

		vhostCount++;
	}

	*errs = errors;
	// Returning 1 means "all good", -1 means we shat our panties
	return errors ? -1 : 1;
}

int autovhost_configposttest(int *errs) {
	if(!vhostCount)
		config_warn("%s was loaded but there aren't any configured vhost entries (autovhost {} block)", MOD_HEADER(m_autovhost).name);
	return 1;
}

// "Run" the config (everything should be valid at this point)
int autovhost_configrun(ConfigFile *cf, ConfigEntry *ce, int type) {
	ConfigEntry *cep; // To store the current variable/value pair etc
	vhostEntry *last = NULL; // Initialise to NULL so the loop requires minimal l0gic
	vhostEntry **vEntry = &vhostList; // Hecks so the ->next chain stays intact

	// Since we'll add a top-level block to unrealircd.conf, need to filter on CONFIG_MAIN lmao
	if(type != CONFIG_MAIN)
		return 0; // Returning 0 means idgaf bout dis

	// Check for valid config entries first
	if(!ce || !ce->ce_varname)
		return 0;

	// If it isn't autovhost, idc
	if(strcmp(ce->ce_varname, MYCONF))
		return 0;

		// Loop dat shyte fam
	for(cep = ce->ce_entries; cep; cep = cep->ce_next) {
		// Do we even have a valid name l0l?
		if(!cep->ce_varname || !cep->ce_vardata)
			continue; // Next iteration imo tbh

		// Lengths to alloc8 the struct vars with in a bit
		size_t masklen = sizeof(char) * (strlen(cep->ce_varname) + 1);
		size_t vhostlen = sizeof(char) * (strlen(cep->ce_vardata) + 1);

		// Allocate mem0ry for the current entry
		*vEntry = malloc(sizeof(vhostEntry));

		// Allocate/initialise shit here
		(*vEntry)->mask = malloc(masklen);
		(*vEntry)->vhost = malloc(vhostlen);
		(*vEntry)->next = NULL;

		// Copy that shit fam
		strncpy((*vEntry)->mask, cep->ce_varname, masklen);
		strncpy((*vEntry)->vhost, cep->ce_vardata, vhostlen);

		// Premium linked list fam
		if(last)
			last->next = *vEntry;

		last = *vEntry;
		vEntry = &(*vEntry)->next;
	}

	return 1; // We good
}

int autovhost_connect(aClient *sptr) {
	if(!sptr || !sptr->user)
		return HOOK_CONTINUE;

	vhostEntry *vEntry;
	for(vEntry = vhostList; vEntry; vEntry = vEntry->next) {
		// Check if the mask matches the user's full nick mask (with REAL host) or IP
		if(!match(vEntry->mask, make_nick_user_host(sptr->name, sptr->user->username, sptr->user->realhost)) || !match(vEntry->mask, GetIP(sptr))) {
			// Free cloakedhost first obv
			memset(sptr->user->cloakedhost, '\0', sizeof(sptr->user->cloakedhost));

			// Also free virthost
			if(sptr->user->virthost) {
				MyFree(sptr->user->virthost);
				sptr->user->virthost = NULL;
			}
			sendnotice(sptr, "*** Setting your cloaked host to %s", vEntry->vhost);
			sptr->user->virthost = strdup(vEntry->vhost);
			strncpy(sptr->user->cloakedhost, vEntry->vhost, sizeof(sptr->user->cloakedhost));
			sendto_server(NULL, 0, 0, ":%s SETHOST %s", sptr->name, vEntry->vhost);
			break;
		}
	}
	return HOOK_CONTINUE;
}
