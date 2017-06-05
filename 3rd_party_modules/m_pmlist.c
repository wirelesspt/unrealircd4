// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

#define MSG_PMLIST "PMLIST" // Display whitelist lol
#define MSG_OPENPM "OPENPM" // Accept PM
#define MSG_CLOSEPM "CLOSEPM" // Stop accepting etc
#define MSG_PMHALP "PMHELP" // Stop accepting etc
#define MYCONF "pmlist" // Config block
#define UMODE_FLAG 'P' // User mode lol
#define MYHEWK HOOKTYPE_PRE_USERMSG // Captain hewk

// Dem macros yo
#define IsServerOrMe(x) (IsServer(x) || IsMe(x))
#define HasPMList(x) (IsPerson(x) && (x)->umodes & pmlist_extumode)
#define DelCommand(x) if(x) CommandDel(x); x = NULL // For unregistering our command when unloading/rehashing
#define DelHook(x) if(x) HookDel(x); x = NULL
#define IsMDErr(x, y, z) \
	do { \
		if(!(x)) { \
			config_error("A critical error occurred when registering ModData for %s: %s", MOD_HEADER(y).name, ModuleGetErrorStr((z)->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)

// Register command functions
CMD_FUNC(m_pmhalp);
CMD_FUNC(m_openpm);
CMD_FUNC(m_closepm);
CMD_FUNC(m_pmlist);

// Big hecks go here
typedef struct t_pmlistEntry pmEntry;
struct t_pmlistEntry {
	pmEntry *next, *prev;
	char *nick;
	char *uid;
	int persist;
};

// Quality fowod declarations
static int dumpit(aClient *sptr, char **p);
char *pmlist_hook_preusermsg(aClient *sptr, aClient *to, char *text, int notice);
void tryNotif(aClient *sptr, aClient *to, char *text, int notice);
void pmlist_md_free(ModData *md);
void pmlist_md_notice_free(ModData *md);
int match_pmentry(aClient *sptr, char *uid, char *nick);
void add_pmentry(aClient *sptr, pmEntry *pm);
void delete_pmentry(aClient *sptr, pmEntry *pm);
void free_pmentry(pmEntry *pm);
int pmlist_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs);
int pmlist_configrun(ConfigFile *cf, ConfigEntry *ce, int type);
int pmlist_rehash(void);

// Muh globals
static ModuleInfo *pmlistMI = NULL; // Store ModuleInfo so we can use it to check for errors in MOD_LOAD
Hook *preumsgHook; // Pointer to le hook
Command *openCmd, *closeCmd, *pmlistCmd, *halpCmd; // Pointers to the commands we're gonna add
Umode *pmlist_umode = NULL; // Store umode handle
long pmlist_extumode = 0L; // Store bitwise value latur
ModDataInfo *pmlistMDI, *noticeMDI; // To store some shit with the user's aClient pointur ;]

// Set config defaults here
int noticeTarget = 0; // Notice the target instead if the source is a regged and logged-in-to nick?
int noticeDelay = 60; // How many seconds to wait before sending another notice

// Halp strings in case someone does just /<CMD>
/* Special characters:
** \002 = bold -- \x02
** \037 = underlined -- \x1F
*/
static char *pmlistHalp[] = {
	"*** \002Help on /OPENPM\002, \002/CLOSEPM\002, \002/PMLIST\002 ***",
	"Keep a whitelist of people allowed to send you private messages.",
	"Their UID is stored so it persists through nickchanges too.",
	"\002\037Requires usermode +P to actually do something.\037\002",
	"If you set +P and privately message someone else, they will",
	"automatically be added to \037your\037 whitelist.",
	"Stale entries (UID no longer exists on network) will be",
	"automatically purged as well (as long as they're not persistent).",
	" ",
	"Syntax:",
	"    \002/OPENPM\002 \037nick\037 [\037-persist\037]",
	"        Allow messages from the given user",
	"        The argument \037must be an actual, existing nick\037",
	"        Also accepts -persist to keep an entry through",
	"        reconnects. Requires the nick to be registered and",
	"        logged into after they reconnect.",
	"    \002/CLOSEPM\002 \037nickmask\037",
	"        Clear matching entries from your list",
	"        Supports wildcard matches too (* and ?)",
	"    \002/PMLIST\002",
	"        Display your current whitelist",
	" ",
	"Examples:",
	"    \002/OPENPM guest8\002",
	"    \002/OPENPM muhb0i -persist\002",
	"    \002/CLOSEPM guest*\002",
	"    \002/CLOSEPM *\002",
	NULL
};

// Dat dere module header
ModuleHeader MOD_HEADER(m_pmlist) = {
	"m_pmlist", // Module name
	"$Id: v1.05 2017/05/01 Gottem$", // Version
	"Implements umode +P to only allow only certain people to privately message you", // Description
	"3.2-b8-1", // Modversion, not sure wat do
	NULL
};

// Configuration testing-related hewks go in testing phase obv
MOD_TEST(m_pmlist) {
	// We have our own config block so we need to checkem config obv m9
	// Priorities don't really matter here
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGTEST, 0, pmlist_configtest);
	return MOD_SUCCESS;
}

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT(m_pmlist) {
	// If command already exists for some reason, bail out
	if(CommandExists(MSG_OPENPM)) {
		config_error("Command %s already exists", MSG_OPENPM);
		return MOD_FAILED;
	}
	if(CommandExists(MSG_CLOSEPM)) {
		config_error("Command %s already exists", MSG_CLOSEPM);
		return MOD_FAILED;
	}
	if(CommandExists(MSG_PMLIST)) {
		config_error("Command %s already exists", MSG_PMLIST);
		return MOD_FAILED;
	}
	if(CommandExists(MSG_PMHALP)) {
		config_error("Command %s already exists", MSG_PMHALP);
		return MOD_FAILED;
	}
	// Add a global umode (i.e. propagate to all servers), allow anyone to set/remove it on themselves
	pmlist_umode = UmodeAdd(modinfo->handle, UMODE_FLAG, UMODE_GLOBAL, 0, NULL, &pmlist_extumode);

	// Dem hewks yo
	HookAdd(modinfo->handle, HOOKTYPE_REHASH, 0, pmlist_rehash);
	HookAdd(modinfo->handle, HOOKTYPE_CONFIGRUN, 0, pmlist_configrun);
	preumsgHook = HookAddPChar(modinfo->handle, MYHEWK, -9999, pmlist_hook_preusermsg); // High prio hewk pls

	// Commands lol
	openCmd = CommandAdd(modinfo->handle, MSG_OPENPM, m_openpm, 2, M_USER);
	closeCmd = CommandAdd(modinfo->handle, MSG_CLOSEPM, m_closepm, 1, M_USER);
	pmlistCmd = CommandAdd(modinfo->handle, MSG_PMLIST, m_pmlist, 0, M_USER);
	halpCmd = CommandAdd(modinfo->handle, MSG_PMHALP, m_pmhalp, 0, M_USER);

	// Request moddata for storing the actual whitelists
	ModDataInfo mreq;
	memset(&mreq, 0, sizeof(mreq));
	mreq.type = MODDATATYPE_CLIENT; // Apply to users only (CLIENT actually includes servers but we'll disregard that here =])
	mreq.name = "pmlist"; // Name it
	mreq.free = pmlist_md_free; // Function to free 'em
	pmlistMDI = ModDataAdd(modinfo->handle, mreq);
	IsMDErr(pmlistMDI, m_pmlist, modinfo);

	// And another for delaying notices =]
	ModDataInfo mreq2;
	memset(&mreq2, 0, sizeof(mreq2));
	mreq2.type = MODDATATYPE_CLIENT; // Apply to users only (CLIENT actually includes servers but we'll disregard that here =])
	mreq2.name = "pmlist_lastnotice"; // Name it
	mreq2.free = pmlist_md_notice_free; // Function to free 'em
	noticeMDI = ModDataAdd(modinfo->handle, mreq2);
	IsMDErr(noticeMDI, m_pmlist, modinfo);

	pmlistMI = modinfo; // st0re module info etc
	return MOD_SUCCESS; // Let MOD_LOAD handle errors
}

// Actually load the module here (also command overrides as they may not exist in MOD_INIT yet)
MOD_LOAD(m_pmlist) {
	// Did the module throw an error during initialisation?
	if(ModuleGetError(pmlistMI->handle) != MODERR_NOERROR || !pmlist_umode || !openCmd || !closeCmd || !pmlistCmd || !halpCmd || !preumsgHook) {
		// Display error string kek
		config_error("A critical error occurred when loading module %s: %s", MOD_HEADER(m_pmlist).name, ModuleGetErrorStr(pmlistMI->handle));
		return MOD_FAILED; // No good
	}
	return MOD_SUCCESS; // We good
}

// Called on unload/rehash obv
MOD_UNLOAD(m_pmlist) {
	// Attempt to unregister custom commands
	DelCommand(openCmd);
	DelCommand(closeCmd);
	DelCommand(pmlistCmd);
	DelCommand(halpCmd);
	DelHook(preumsgHook); // Kbye
	return MOD_SUCCESS; // We good
}

// Dump a NULL-terminated array of strings to user sptr using the numeric rplnum, and then return 0 (taken from DarkFire IRCd)
static int dumpit(aClient *sptr, char **p) {
	for(; *p != NULL; p++)
		sendto_one(sptr, ":%s %03d %s :%s", me.name, RPL_TEXT, sptr->name, *p);

	// Let user take 8 seconds to read it
	sptr->local->since += 8;
	return 0;
}

char *pmlist_hook_preusermsg(aClient *sptr, aClient *to, char *text, int notice) {
	/* Arguments depend on hook type used obv
	**
	** sptr: Pointer to user executing command -- you'll probably wanna use this fam
	** to: Pointer to the user receiving that shit
	** text: Should b obv yo
	** isnotice: Was it a notice?
	*/
	pmEntry *pm; // Dat entry fam

	if(IsServerOrMe(sptr) || IsServerOrMe(to) || IsULine(sptr) || IsULine(to) || IsOper(sptr)) // Check for exclusions imo
		return text;

	if(!MyConnect(to)) // If the recipient is not on this server, we can't check _their_ pmlist anyways, so fuck off here =]
		return text;

	if(HasPMList(sptr) && !match_pmentry(sptr, to->id, to->name)) { // If YOU have +P and target is not on the list, add 'em
		pm = (pmEntry *)MyMallocEx(sizeof(pmEntry)); // Alloc8 new entry pls
		pm->uid = strdup(to->id); // Set 'em UID
		pm->nick = strdup(to->name); // And current nick
		pm->persist = 0; // No persistence for this one
		add_pmentry(sptr, pm); // Add 'em lol
		sendnotice(sptr, "[pmlist] Added %s to your whitelist", to->name); // Notify 'em
	}

	if(!HasPMList(to)) // No need to checkem entries if target doesn't have +P
		return text;

	if(!match_pmentry(to, sptr->id, sptr->name)) { // Attempt to match UID lol
		tryNotif(sptr, to, text, notice); // Send notice if applicable atm
		return NULL; // N O P E
	}

	return text; // Seems ok
}

void tryNotif(aClient *sptr, aClient *to, char *text, int notice) {
	long last = moddata_client(sptr, noticeMDI).l; // Get timestamp of last notice yo
	int sendem = (!last || TStime() - last >= noticeDelay); // We past the delay nao?
	moddata_client(sptr, noticeMDI).l = TStime(); // Set it to current time

	if(!IsLoggedIn(sptr) || !noticeTarget) { // User has not identified with services
		if(sendem)
			sendnotice(sptr, "[pmlist] %s does not accept private messages from you, please instruct them to do /openpm %s", to->name, sptr->name);
		return;
	}
	// Source user has identified with services, maybe need to send a notice to the target
	if(sendem)
		sendnotice(to, "[pmlist] %s just tried to send you a private %s, use /openpm %s to allow all of their messages [msg: %s]", sptr->name, (notice ? "notice" : "message"), sptr->name, text);
}

void pmlist_md_free(ModData *md) {
	if(md->ptr) { // r u insaiyan?
		pmEntry *pmList, *pm, *next; // Sum iter8ors lol
		pmList = md->ptr; // Get pointur to head of teh list

		for(pm = pmList; pm; pm = next) { // Let's do all entries yo
			next = pm->next; // Get next entry in advance lol
			free_pmentry(pm); // Free 'em imo
		}
		md->ptr = NULL; // Shit rip's if we don't kek
	}
}

void pmlist_md_notice_free(ModData *md) {
	if(md) // gg
		md->l = 0L; // ez
}

int match_pmentry(aClient *sptr, char *uid, char *nick) {
	pmEntry *pmList, *pm; // Some iter80rs lol
	aClient *acptr; // Check if the other user (still) exists
	if(!sptr || !uid || !uid[0]) // Sanity checks
		return 0; // Lolnope

	if(nick)
		acptr = find_person(nick, NULL); // Attempt to find em

	if((pmList = moddata_client(sptr, pmlistMDI).ptr)) { // Something st0red?
		for(pm = pmList; pm; pm = pm->next) { // Iter8 em
			// Checkem UID and nick (if no UID match, check if the entry allows persistence and the nick is regged + authed to)
			if(!strcmp(pm->uid, uid) || (pm->persist && acptr && !stricmp(acptr->name, pm->nick) && IsLoggedIn(acptr))) {
				if(strcmp(pm->uid, uid)) { // Maybe need2update the entry lol
					free(pm->uid); // Gotta free first ;]
					pm->uid = strdup(uid); // Nao dup em
				}
				return 1; // Gottem
			}
		}
	}
	return 0; // Loln0pe
}

void add_pmentry(aClient *sptr, pmEntry *pm) {
	pmEntry *pmList, *last, *cur; // Sum iterators famalam
	if(!sptr || !pm || !pm->uid) // Sanity checks
		return; // kbai

	pm->next = NULL; // Inb4rip
	pm->prev = NULL; // ditt0
	if(!(pmList = moddata_client(sptr, pmlistMDI).ptr)) { // One of the for loops MIGHT have cleared the entire list ;]
		moddata_client(sptr, pmlistMDI).ptr = pm; // Necessary to properly st0re that shit
		return; // We good
	}

	// Dirty shit to get the last entry lol
	for(cur = pmList; cur; cur = cur->next)
		last = cur; // cur will end up pointing to NULL, so let's use the entry just before ;];]

	pm->prev = last; // The new entry's prev should point to the actual last entry
	last->next = pm; // And the last entry's next should be the new one obv =]
}

void delete_pmentry(aClient *sptr, pmEntry *pm) {
	pmEntry *pmList, *last; // Sum iter8ors lol
	if(!pm || !pm->uid) // r u insaiyan?
		return;

	if(sptr && (pmList = moddata_client(sptr, pmlistMDI).ptr)) { // One of the for loops MIGHT have cleared the entire list ;]
		for(last = pmList; last; last = last->next) { // Iterate em lol
			if(last == pm) { // We gottem match?
				// Doubly linked lists ftw yo
				if(last->prev) // Is anything but the FIRST entry
					last->prev->next = last->next; // Previous entry should skip over dis one
				else { // Is the first entry
					moddata_client(sptr, pmlistMDI).ptr = last->next; // So make the moddata thingy point to the second one
					pmList = last->next; // Really just for the if below =]
				}

				if(last->next) // If anything but the LAST entry
					last->next->prev = last->prev; // Next entry should skip over dis one

				free_pmentry(last); // Free 'em lol
				break; // Gtfo imo tbh famlammlmflma
			}
		}

		if(!pmList) // We empty nao?
			moddata_client(sptr, pmlistMDI).ptr = NULL; // Cuz inb4ripperoni
	}
}

void free_pmentry(pmEntry *pm) {
	if(!pm) // LOLNOPE
		return;
	if(pm->uid) // Sanity cheqq lol
		free(pm->uid); // Gotta
	if(pm->nick) // free
		free(pm->nick); // 'em
	free(pm); // all
}

CMD_FUNC(m_pmhalp) {
	return dumpit(sptr, pmlistHalp); // Return help string always
}

CMD_FUNC(m_openpm) {
	/* Gets args: aClient *cptr, aClient *sptr, int parc, char[] parv
	**
	** cptr: Pointer to directly attached client -- if remote user this is the remote server instead
	** sptr: Pointer to user executing command -- you'll probably wanna use this fam
	** parc: Amount of arguments (also includes the command in the count)
	** parv: Contains the actual args, first one starts at parv[1]
	**
	** So "OPENPM test" would result in parc = 2 and parv[1] = "test"
	** Also, parv[0] seems to always be NULL, so better not rely on it fam
	*/
	aClient *acptr; // Who r u allowin?
	pmEntry *pmList, *pm, *next; // Iterators imho tbh fambi
	int gtfo; // Maybe won't need to add an entry ;]
	int persist; // Whether to persist this entry or no
	 // If first argument is a bad pointer or user doesn't even have umode +P, don't proceed (also if the optional -persist is written incorrectly]
	if(BadPtr(parv[1]) || !HasPMList(sptr) || (!BadPtr(parv[2]) && stricmp(parv[2], "-persist")))
		return dumpit(sptr, pmlistHalp); // Return help string instead

	if(!(acptr = find_person(parv[1], NULL))) { // Verify target user
		sendto_one(sptr, err_str(ERR_NOSUCHNICK), me.name, sptr->name, parv[1]); // Send error lol
		return 0; // Correct usage so non-error ;]
	}

	if(IsULine(acptr)) { // Checkem U:Line lol
		sendnotice(sptr, "[pmlist] There's no need to whitelist U:Lined users (%s)", acptr->name);
		return 0;
	}

	if(IsOper(acptr)) { // Checkem opers lol
		sendnotice(sptr, "[pmlist] There's no need to whitelist IRC operators (%s)", acptr->name);
		return 0;
	}

	if(!acptr->id) { // Sanity check lol
		sendnotice(sptr, "[pmlist] Something went wrong getting %s's UID", acptr->name);
		return -1; // rip
	}

	persist = (!BadPtr(parv[2]) && !stricmp(parv[2], "-persist") ? 1 : 0);
	if(!(pmList = moddata_client(sptr, pmlistMDI).ptr)) { // If no list yet
		pm = (pmEntry *)MyMallocEx(sizeof(pmEntry)); // Alloc8 new entray
		pm->uid = strdup(acptr->id); // Set 'em UID
		pm->nick = strdup(acptr->name); // And current nick
		pm->persist = persist; // Set persistence yo
		add_pmentry(sptr, pm); // Add 'em lol
		sendnotice(sptr, "[pmlist] Added %s to your whitelist%s", acptr->name, (persist ? ", persistently" : "")); // Notify 'em
		return 0; // We good
	}

	gtfo = 0;
	for(pm = pmList; pm; pm = next) { // Check if the UID is already whitelisted (check for stale entries too ;];])
		next = pm->next; // Get next entry in advance lol

		if(!find_person(pm->uid, NULL) && !pm->persist) { // Check for stale entry lol (UID no longer existing on the netwerk)
			delete_pmentry(sptr, pm); // Delete from list lol
			continue; // No need to check below if =]
		}

		if(!gtfo && (!strcmp(pm->uid, acptr->id) || !stricmp(pm->nick, acptr->name))) // UID/nick already listed
			gtfo = 1; // Flippem ;]
	}

	if(gtfo) {
		sendnotice(sptr, "[pmlist] You've already whitelisted %s", acptr->name);
		return -1; // BUH BAI
	}

	// New entry, alloc8 memory m8
	pm = (pmEntry *)MyMallocEx(sizeof(pmEntry));
	pm->uid = strdup(acptr->id); // Set 'em UID
	pm->nick = strdup(acptr->name); // And current nick
	pm->persist = persist; // Set persistence yo
	add_pmentry(sptr, pm); // Add 'em yo
	sendnotice(sptr, "[pmlist] Added %s to your whitelist%s", acptr->name, (persist ? ", persistently" : "")); // Notify 'em
	return 0; // All good
}

CMD_FUNC(m_closepm) {
	pmEntry *pmList, *pm, *next; // Iterators imho tbh fambi
	int found; // Maybe won't need to delete an entry ;]
	if(BadPtr(parv[1]) || !HasPMList(sptr)) // If first argument is a bad pointer or user doesn't even have umode +P, don't proceed
		return dumpit(sptr, pmlistHalp); // Return help string instead

	if(!(pmList = moddata_client(sptr, pmlistMDI).ptr)) {
		sendnotice(sptr, "[pmlist] You don't have any entries in your whitelist");
		return 0; // Correct usage so non-error ;]
	}

	char buf[256]; // For outputting multiple nicks etc lol
	memset(buf, 0, sizeof(buf));
	for(pm = pmList; pm; pm = next) { // Remove all =]
		next = pm->next; // Get next entry in advance lol

		if(!find_person(pm->uid, NULL) && !pm->persist) { // Check for stale entry lol (UID no longer existing on the netwerk)
			delete_pmentry(sptr, pm); // Delete from list lol
			continue; // No need to check below if =]
		}

		if(match(parv[1], pm->nick)) // If the entry's nick doesn't match the given mask
			continue; // Let's fuck off

		found = 1; // Ayy we gottem

		if(!buf[0]) {// First nick in this set
			strlcpy(buf, pm->nick, sizeof(buf)); // Need cpy instead of cat ;]
			if(pm->persist) strlcat(buf, " (P)", sizeof(buf)); // Dat persistence
		}
		else {
			strlcat(buf, ", ", sizeof(buf)); // Dat separator lol
			strlcat(buf, pm->nick, sizeof(buf)); // Now append non-first nikk =]
			if(pm->persist) strlcat(buf, " (P)", sizeof(buf)); // Dat persistence
		}

		if(strlen(buf) > (sizeof(buf) - NICKLEN - 4 - 3)) { // If another nick won't fit (-4 , -3 cuz optional " (P)" plus mandatory ", " and nullbyet)
			sendnotice(sptr, "[pmlist] Removed from whitelist: %s", buf); // Send what we have
			memset(buf, 0, sizeof(buf)); // And reset buffer lmoa
		}
		delete_pmentry(sptr, pm); // Delete from list lol
	}
	if(buf[0]) // If we still have some nicks (i.e. we didn't exceed buf's size for the last set)
		sendnotice(sptr, "[pmlist] Removed from whitelist: %s", buf); // Dump whatever's left

	if(!found)
		sendnotice(sptr, "[pmlist] No matches found for usermask %s", parv[1]);
	return 0; // All good
}

CMD_FUNC(m_pmlist) {
	pmEntry *pmList, *pm, *next; // Iterators imho tbh fambi
	int found; // Gottem

	if(!HasPMList(sptr)) { // If user doesn't even have umode +P, don't proceed
		sendnotice(sptr, "[pmlist] You need to have umode +%c set to use this feature", UMODE_FLAG);
		return -1; // Gtfo
	}

	if(!(pmList = moddata_client(sptr, pmlistMDI).ptr)) {
		sendnotice(sptr, "[pmlist] You don't have any entries in your whitelist");
		return 0; // Correct usage so non-error imo tbh ;]
	}

	char buf[256]; // For outputting multiple nicks etc lol
	memset(buf, 0, sizeof(buf));
	found = 0;
	for(pm = pmList; pm; pm = next) { // Check if the UID is already whitelisted (check for stale entries too ;];])
		next = pm->next; // Get next entry in advance lol

		if(!find_person(pm->uid, NULL) && !pm->persist) { // Check for stale entry lol (UID no longer existing on the netwerk)
			delete_pmentry(sptr, pm); // Delete from list lol
			continue; // No need to check below if =]
		}

		found = 1;

		if(!buf[0]) { // First nick in this set
			strlcpy(buf, pm->nick, sizeof(buf)); // Need cpy instead of cat ;]
			if(pm->persist) strlcat(buf, " (P)", sizeof(buf)); // Dat persistence
		}
		else {
			strlcat(buf, ", ", sizeof(buf)); // Dat separator lol
			strlcat(buf, pm->nick, sizeof(buf)); // Now append non-first nikk =]
			if(pm->persist) strlcat(buf, " (P)", sizeof(buf)); // Dat persistence
		}

		if(strlen(buf) > (sizeof(buf) - NICKLEN - 4 - 3)) { // If another nick won't fit (-4 , -3 cuz optional " (P)" plus mandatory ", " and nullbyet)
			sendnotice(sptr, "[pmlist] Whitelist: %s", buf); // Send what we have
			memset(buf, 0, sizeof(buf)); // And reset buffer just in caes lmoa
		}
	}
	if(buf[0]) // If we still have some nicks
		sendnotice(sptr, "[pmlist] Whitelist: %s", buf); // Dump whatever's left

	if(!found) // The for loop above MIGHT have cleared the entire list ;]
		sendnotice(sptr, "[pmlist] You don't have any entries in your whitelist");
	return 0;
}

int pmlist_configtest(ConfigFile *cf, ConfigEntry *ce, int type, int *errs) {
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

		if(!strcmp(cep->ce_varname, "noticetarget")) {
			if(!cep->ce_vardata || (strcmp(cep->ce_vardata, "0") && strcmp(cep->ce_vardata, "1"))) {
				config_error("%s:%i: %s::%s must be either 0 or 1 fam", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
				continue;
			}
		}

		if(!strcmp(cep->ce_varname, "noticedelay")) {
			// Should be an integer yo
			if(!cep->ce_vardata) {
				config_error("%s:%i: %s::%s must be an integer of zero or larger m8", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
				errors++; // Increment err0r count fam
				continue;
			}
			for(i = 0; cep->ce_vardata[i]; i++) {
				if(!isdigit(cep->ce_vardata[i])) {
					config_error("%s:%i: %s::%s must be an integer of zero or larger m8", cep->ce_fileptr->cf_filename, cep->ce_varlinenum, MYCONF, cep->ce_varname);
					errors++; // Increment err0r count fam
					continue;
				}
			}
		}
	}

	*errs = errors;
	return errors ? -1 : 1; // Returning 1 means "all good", -1 means we shat our panties
}

// "Run" the config (everything should be valid at this point)
int pmlist_configrun(ConfigFile *cf, ConfigEntry *ce, int type) {
	ConfigEntry *cep; // To store the current variable/value pair etc

	// Since we'll add a top-level block to unrealircd.conf, need to filter on CONFIG_MAIN lmao
	if(type != CONFIG_MAIN)
		return 0; // Returning 0 means idgaf bout dis

	// Check for valid config entries first
	if(!ce || !ce->ce_varname)
		return 0;

	// If it isn't pmlist, idc
	if(strcmp(ce->ce_varname, MYCONF))
		return 0;

		// Loop dat shyte fam
	for(cep = ce->ce_entries; cep; cep = cep->ce_next) {
		// Do we even have a valid name l0l?
		if(!cep->ce_varname)
			continue; // Next iteration imo tbh

		if(!strcmp(cep->ce_varname, "noticetarget")) {
			noticeTarget = atoi(cep->ce_vardata);
			continue;
		}

		if(!strcmp(cep->ce_varname, "noticedelay")) {
			noticeDelay = atoi(cep->ce_vardata);
			continue;
		}
	}

	return 1; // We good
}

int pmlist_rehash(void) {
	// Reset config defaults
	noticeTarget = 0;
	noticeDelay = 60;
	return HOOK_CONTINUE;
}