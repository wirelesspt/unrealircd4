// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

#define TKL_DB "data/tkl.db"
#define TKL_DB_VERSION 1000

// Muh macros lol
#define DelHook(x) if(x) HookDel(x); x = NULL

#ifndef _WIN32
	#define OpenFile(fd, file, flags) fd = open(file, flags, S_IRUSR | S_IWUSR)
#else
	#define OpenFile(fd, file, flags) fd = open(file, flags, S_IREAD | S_IWRITE)
#endif

#define R_SAFE(x) \
	do { \
		if((x)) \
		{ \
			close(fd); \
			config_warn("[storetkl] Read error from the persistent storage file '%s/%s' on server %s", SCRIPTDIR, TKL_DB, me.name); \
			return -1; \
		} \
	} while (0)

#define W_SAFE(x) \
	do { \
		if((x)) \
		{ \
			close(fd); \
			config_warn("[storetkl] Write error from the persistent storage file '%s/%s' on server %s", SCRIPTDIR, TKL_DB, me.name); \
			return -1; \
		} \
	} while (0)

#define IsMDErr(x, y, z) \
	do { \
		if(!(x)) { \
			config_error("A critical error occurred when registering ModData for %s: %s", MOD_HEADER(y).name, ModuleGetErrorStr((z)->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)

// Quality fowod declarations
void storetkl_moddata_free(ModData *md);
int storetkl_hook_tkl_add(aClient *cptr, aClient *sptr, aTKline *tkl, int parc, char *parv[]);
int storetkl_hook_tkl_del(aClient *cptr, aClient *sptr, aTKline *tkl, int parc, char *parv[]);
int readDB(void);
int writeDB(aTKline *origtkl, char what);
static inline int read_data(int fd, void *buf, size_t count);
static inline int write_data(int fd, void *buf, size_t count);
static int write_str(int fd, char *x);
static int read_str(int fd, char **x);

// Muh globals
static ModDataInfo *storetklMDI; // For storing that we read the DB at startup
Hook *hookAddTKL, *hookDelTKL; // Muh hooks
static ModuleInfo *storetklMI = NULL; // Store ModuleInfo so we can use it to check for errors in MOD_LOAD
static unsigned tkl_db_version = TKL_DB_VERSION; // TKL DB version kek

// Dat dere module header
ModuleHeader MOD_HEADER(m_storetkl) = {
	"m_storetkl", // Module name
	"$Id: v1.0 2017/04/04 Gottem$", // Version
	"Store TKL entries persistently across IRCd restarts", // Description
	"3.2-b8-1", // Modversion, not sure wat do
	NULL
};

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT(m_storetkl) {
	if(!(storetklMDI = findmoddata_byname("storetkl_inited", MODDATATYPE_CLIENT))) { // Attempt to find active moddata (like in case of a rehash)
		ModDataInfo mreq; // No moddata, let's request that shit
		memset(&mreq, 0, sizeof(mreq)); // Set 'em lol
		mreq.type = MODDATATYPE_CLIENT; // Apply to users only (CLIENT actually includes servers but we'll disregard that =])
		mreq.name = "storetkl_inited"; // Name it
		mreq.free = storetkl_moddata_free; // Function to free 'em
		mreq.serialize = NULL; // Shouldn't be necessary but let's =]
		mreq.unserialize = NULL; // Ditto
		mreq.sync = 0; // Ditto
		storetklMDI = ModDataAdd(modinfo->handle, mreq); // Add 'em yo
		IsMDErr(storetklMDI, m_storetkl, modinfo); // Check for errors when adding (like hitting the slot limit xd)
		readDB(); // Read DB if we good
		moddata_client((&me), storetklMDI).i = 1; // Set ModData
	}

	// Low priority hooks to make sure we go after everything else =]
	hookAddTKL = HookAdd(modinfo->handle, HOOKTYPE_TKL_ADD, 999, storetkl_hook_tkl_add);
	hookDelTKL = HookAdd(modinfo->handle, HOOKTYPE_TKL_DEL, 999, storetkl_hook_tkl_del);

	storetklMI = modinfo; // Store module info yo
	return MOD_SUCCESS; // Let MOD_LOAD handle errors
}

// Actually load the module here (also command overrides as they may not exist in MOD_INIT yet)
MOD_LOAD(m_storetkl) {
	// Did the module throw an error during initialisation?
	if(ModuleGetError(storetklMI->handle) != MODERR_NOERROR) {
		// Display error string kek
		config_error("A critical error occurred when loading module %s: %s", MOD_HEADER(m_storetkl).name, ModuleGetErrorStr(storetklMI->handle));
		return MOD_FAILED; // No good
	}
	return MOD_SUCCESS; // We good
}

// Called on unload/rehash obv
MOD_UNLOAD(m_storetkl) {
	// Remove hewks imo tbh
	DelHook(hookAddTKL);
	DelHook(hookDelTKL);
	return MOD_SUCCESS; // We good
}

// Required function for ModData kek
void storetkl_moddata_free(ModData *md) {
	if(md->i) // gg
		md->i = 0; // ez
}

// TKL_ADD/DEL hook functions
int storetkl_hook_tkl_add(aClient *cptr, aClient *sptr, aTKline *tkl, int parc, char *parv[]) {
	writeDB(tkl, '+'); // Literally all we need lol
	return HOOK_CONTINUE; // We good
}
int storetkl_hook_tkl_del(aClient *cptr, aClient *sptr, aTKline *tkl, int parc, char *parv[]) {
	writeDB(tkl, '-'); // Literally all we need lol
	return HOOK_CONTINUE; // We good
}

int writeDB(aTKline *origtkl, char what) {
	int fd; // File descriptor
	size_t count; // Amount of X:Lines
	int index; // For iterating over all the types in the tklines "hash"
	aTKline *tkl; // Actual iter8or =]
	size_t pathlen = strlen(SCRIPTDIR) + strlen(TKL_DB) + 1; // Includes a slash lol
	char filepath[pathlen + 1]; // Includes a nullbyet yo

	snprintf(filepath, sizeof(filepath), "%s/%s", SCRIPTDIR, TKL_DB); // Store 'em
	filepath[pathlen] = '\0'; // Shouldn't be necessary but let's =]
	OpenFile(fd, filepath, O_CREAT | O_WRONLY | O_TRUNC); // Open ze fiel

	if(fd == -1) { // Error opening that shit
		config_warn("[storetkl] Unable to open the persistent storage file '%s/%s' for writing on server %s: %s", SCRIPTDIR, TKL_DB, me.name, strerror(errno));
		return -1; // Gtfo
	}

	W_SAFE(write_data(fd, &tkl_db_version, sizeof(tkl_db_version))); // Write our DB version =]

	count = 0; // Let's count 'em X:Lines
	for(index = 0; index < TKLISTLEN; index++) { // All X:Lines share the same list, sort of
		for(tkl = tklines[index]; tkl; tkl = tkl->next) { // As long as this particular TKL type has entries
			if(origtkl && what == '-' && origtkl == tkl) // Hook fires before Unreal actually removes the X:Line
				continue; // So skip it
			count++; // Increment countur
		}
	}

	W_SAFE(write_data(fd, &count, sizeof(count))); // Amount of X:Lines to expect when reading the DB

	for(index = 0; index < TKLISTLEN; index++) { // Iter8 'em
		for(tkl = tklines[index]; tkl; tkl = tkl->next) { // again =]
			if(origtkl && what == '-' && origtkl == tkl) // Hook fires before Unreal actually removes the X:Line
				continue; // So skip it

			// Since we can't just write 'tkl' in its entirety, we have to get the relevant variables instead
			// These will be used to reconstruct the proper internal m_tkl() call ;]
			W_SAFE(write_data(fd, &tkl->type, sizeof(tkl->type))); // Integer (G:Line, Q:Line, etc; also refer to TKL_*)
			W_SAFE(write_data(fd, &tkl->subtype, sizeof(tkl->subtype))); // Unsigned short (only used for spamfilters but set to 0 for errythang else anyways)
			W_SAFE(write_str(fd, tkl->usermask)); // User mask (targets for spamfilter, like cp)
			W_SAFE(write_str(fd, tkl->hostmask)); // Host mask (action for spamfilter, like block)
			W_SAFE(write_str(fd, tkl->reason)); // Ban reason (TKL time for spamfilters in case of G:Line action etc)
			W_SAFE(write_str(fd, tkl->setby)); // Set by who
			W_SAFE(write_data(fd, &tkl->expire_at, sizeof(tkl->expire_at))); // Expiration timestamp
			W_SAFE(write_data(fd, &tkl->set_at, sizeof(tkl->set_at))); // Set-at timestamp

			if(tkl->ptr.spamf) { // Obv only exists when this pertains een spamfilterin0
				W_SAFE(write_str(fd, "SPAMF")); // Write a string so we know to expect more when reading the DB
				W_SAFE(write_data(fd, &tkl->ptr.spamf->action, sizeof(tkl->ptr.spamf->action))); // Unsigned short (block, GZ:Line, etc; also refer to BAN_ACT_*)
				W_SAFE(write_str(fd, tkl->ptr.spamf->tkl_reason)); // Underscore-escaped string of why the spamfilter was set
				W_SAFE(write_data(fd, &tkl->ptr.spamf->tkl_duration, sizeof(tkl->ptr.spamf->tkl_duration))); // How long to set a ban for (if applicable)
				W_SAFE(write_str(fd, tkl->ptr.spamf->expr->str)); // Actual expression/regex/etc
				W_SAFE(write_data(fd, &tkl->ptr.spamf->expr->type, sizeof(tkl->ptr.spamf->expr->type))); // Integer (expression type [simple/POSIX/PCRE]; see also enum MatchType)
			}
			else
				W_SAFE(write_str(fd, "NOSPAMF")); // No spamfilter, so let's write that too =]
		}
	}

 	close(fd); // Don't f0get to close 'em lol
	return 0; // We good (non-zero = error)
}

int readDB(void) {
	int fd; // File descriptor
	size_t count; // Amount of X:Lines
	int i; // For iterating over all the entries in ze DB
	int num = 0; // Amount of X:Lines we actually ended up re-adding
	int rewrite = 0; // If we got expired X:Lines etc, let's rewrite (i.e. clean up) the DB file =]
	unsigned version; // For checking 'em DB version
	aTKline *tkl; // Iter8or
	size_t pathlen = strlen(SCRIPTDIR) + strlen(TKL_DB) + 1; // Includes a slash lol
	char filepath[pathlen + 1]; // Includes a nullbyet yo

	snprintf(filepath, sizeof(filepath), "%s/%s", SCRIPTDIR, TKL_DB); // Store 'em
	filepath[pathlen] = '\0'; // Shouldn't be necessary but let's =]

	// Let's send a message saying we loading some shi ;]
	ircd_log(LOG_ERROR, "[storetkl] Reading stored X:Lines from '%s'", filepath);
	sendto_realops("[storetkl] Reading stored X:Lines from '%s'", filepath); // Probably won't be seen ever, but just in case ;]
	OpenFile(fd, filepath, O_RDONLY); // Open 'em

	if(fd == -1) { // Error when opening
		if(errno != ENOENT) // If file doesn't even exists, don't show a warning =]
			config_warn("[storetkl] Unable to open the persistent storage file '%s' for reading on server %s: %s", filepath, me.name, strerror(errno));
		return -1; // And return error
	}

	R_SAFE(read_data(fd, &version, sizeof(version))); // Read the DB version

	if(version != tkl_db_version) { // Got een mismatch yo (probably never happens tho lol)
		// N.B.: I can probably heck something to provide backwards compatibility, but there is no need for this atm
		config_warn("File '%s' has a wrong database version (expected: %u, got: %u) on server %s", filepath, tkl_db_version, version, me.name); // Display warning familia
		close(fd); // Close 'em
		return -1; // And gtfo
	}

	R_SAFE(read_data(fd, &count, sizeof(count))); // Read how many X:Lines to expect

	for(i = 1; i <= count; i++) { // Iterate 'em all
		int type; // G:Line, Q:Line, etc; also refer to TKL_*
		unsigned short subtype; // Only used for spamfilters but set to 0 for errythang else anyways
		int parc = 0; // Amount of arguments (required by m_tkl())
		char *usermask = NULL; // User mask (targets for spamfilter, like cp)
		char *hostmask = NULL; // Host mask (action for spamfilter, like block)
		char *reason = NULL; // Ban reason (TKL time for spamfilters in case of G:Line action etc)
		char *setby = NULL; // Set by who
		char tklflag; // G, z, Z, f, etc
		char *tkltype = NULL; // Cuz m_tkl() needs a char* and not char for this
		TS expire_at, set_at; // Expiration and set-at timestamps (simply a typedef of 'long' afaik)
		char setTime[100], expTime[100], spamfTime[100]; // To convert TS timestamp shit to char arrays =]

		char *spamf_check = NULL; // Check for SPAMF/NOSPAMF ;]
		int spamf = 0; // "Boolean" to set if we got SPAMF
		unsigned short spamf_action; // Block, GZ:Line, etc; also refer to BAN_ACT_*
		char *spamf_tkl_reason = NULL; // Underscore-escaped string of why the spamfilter was set
		TS spamf_tkl_duration; // How long to set a ban for (if applicable)
		char *spamf_expr = NULL; // Actual expression/regex/etc
		MatchType matchtype; // Like simple/posix/regex, is simply an enum thingy ;]
		char *spamf_matchtype = "simple"; // Let's default to simple for spamfilters

		int doadd = 1; // Do we need to call m_tkl()?
		aTKline *tkl; // Iter8or for checking existing X:Lines to prevent dupes =]

		char *tkllayer[13] = { // Dem m_tkl() args =]
			me.name, // 0: Server name
			"+", // 1: Direction (always add in this case yo)
			NULL, // 2: Type, like G
			NULL, // 3: User mask (targets for spamfilter)
			NULL, // 4: Host mask (action for spamfilter)
			NULL, // 5: Set by who
			NULL, // 6: Expiration time
			NULL, // 7: Set-at time
			NULL, // 8: Reason (TKL time for spamfilters in case of G:Line action etc)
			NULL, // 9: Spamfilter only: TKL reason (w/ underscores and all etc)
			NULL, // 10: Spamfilter only: Match type (simple/posix/regex)
			NULL, // 11: Spamfilter only: Match string/regex etc
			NULL, // 12: Some functions rely on the post-last entry being NULL =]
		};

		// Now read that shit
		R_SAFE(read_data(fd, &type, sizeof(type)));
		R_SAFE(read_data(fd, &subtype, sizeof(subtype)));
		R_SAFE(read_str(fd, &usermask));
		R_SAFE(read_str(fd, &hostmask));
		R_SAFE(read_str(fd, &reason));
		R_SAFE(read_str(fd, &setby));
		R_SAFE(read_data(fd, &expire_at, sizeof(expire_at)));
		R_SAFE(read_data(fd, &set_at, sizeof(set_at)));
		R_SAFE(read_str(fd, &spamf_check));

		if(!strcmp(spamf_check, "SPAMF")) { // Oh but wait, there's more =]
			spamf = 1; // Flip "boolean"
			// Read m0awr
			R_SAFE(read_data(fd, &spamf_action, sizeof(spamf_action)));
			R_SAFE(read_str(fd, &spamf_tkl_reason));
			R_SAFE(read_data(fd, &spamf_tkl_duration, sizeof(spamf_tkl_duration)));
			R_SAFE(read_str(fd, &spamf_expr));
			R_SAFE(read_data(fd, &matchtype, sizeof(matchtype)));
		}

		tkltype = malloc(sizeof(char) * 2); // For tklflag + nullbyet yo
		tklflag = tkl_typetochar(type); // gg ez (turns an int to char)
		tkltype[0] = tklflag; // Set 'em
		tkltype[1] = '\0'; // Ayyy

		if(expire_at != 0 && expire_at <= TStime()) { // Check if the X:Line is not permanent and would expire immediately after setting
			// Send message about not re-adding shit
			if(tklflag == 'F') {
				ircd_log(LOG_ERROR, "[storetkl] Not re-adding spamfilter '%s' [%s] because it should be expired", spamf_expr, spamf_tkl_reason);
				sendto_realops("[storetkl] Not re-adding spamfilter '%s' [%s] because it should be expired", spamf_expr, spamf_tkl_reason); // Probably won't be seen ever, but just in case ;]
			}
			else {
				ircd_log(LOG_ERROR, "[storetkl] Not re-adding %c:Line '%s@%s' [%s] because it should be expired", tklflag, usermask, hostmask, reason);
				sendto_realops("[storetkl] Not re-adding %c:Line '%s@%s' [%s] because it should be expired", tklflag, usermask, hostmask, reason); // Probably won't be seen ever, but just in case ;]
			}
			rewrite++; // Increment countur lol
			free(tkltype); // Cuz we malloc'd em lol
			if(spamf_check) free(spamf_check); // read_str() does a MyMalloc, so let's free to prevent mem0ry issues
			if(usermask) free(usermask); // Ditto
			if(hostmask) free(hostmask); // Ditto
			if(reason) free(reason); // Ditto
			if(setby) free(setby); // Ditto
			continue; // Next one pls
		}

		ircsnprintf(setTime, sizeof(setTime), "%li", set_at); // Convert
		ircsnprintf(expTime, sizeof(expTime), "%li", expire_at); // 'em

		if(spamf && tklflag == 'f') // Cuz apparently 'f' means it was added through the conf or is built-in ('F' is ok tho)
			doadd = 0;

		// Build TKL args
		parc = 9; // Minimum of 9 args is required
		// All of these except [8] are the same for all (only odd one is spamfilter ofc)
		tkllayer[2] = tkltype;
		tkllayer[3] = usermask;
		tkllayer[4] = hostmask;
		tkllayer[5] = setby;
		tkllayer[6] = expTime;
		tkllayer[7] = setTime;
		tkllayer[8] = reason;

		if(spamf) { // If we got a spamfilter
			parc = 12; // Need m0ar args

			for(tkl = tklines[tkl_hash(tklflag)]; doadd && tkl; tkl = tkl->next) { // Check for existing spamfilters
				// We can assume it's the same spamfilter if all of the following match: spamfilter expression, targets, TKL reason, action, matchtype and TKL duration
				if(!strcmp(tkl->ptr.spamf->expr->str, spamf_expr) && !strcmp(tkl->usermask, usermask) && !strcmp(tkl->ptr.spamf->tkl_reason, spamf_tkl_reason) &&
					tkl->ptr.spamf->action == spamf_action && tkl->ptr.spamf->expr->type == matchtype && tkl->ptr.spamf->tkl_duration == spamf_tkl_duration) {
					doadd = 0; // So don't proceed with adding
					break; // And fuck off
				}
			}

			if(doadd) { // If above loop didn't find a valid entry, let's build the rest of the args =]
				ircsnprintf(spamfTime, sizeof(spamfTime), "%li", spamf_tkl_duration); // Convert TKL duration
				tkllayer[8] = spamfTime; // Replaces reason in other X:Lines
				tkllayer[9] = spamf_tkl_reason;

				if(matchtype == MATCH_PCRE_REGEX) // Premium enum yo
					spamf_matchtype = "regex"; // Set string
				else if(matchtype == MATCH_TRE_REGEX) // ayy
					spamf_matchtype = "posix"; // lmao

				tkllayer[10] = spamf_matchtype;
				tkllayer[11] = spamf_expr;
			}
		}
		else { // Not a spamfilter
			for(tkl = tklines[tkl_hash(tklflag)]; tkl; tkl = tkl->next) { // Still gotta check for dupes tho
				// Here we only need to have a match for a few fields =]
				if(!strcmp(tkl->usermask, usermask) && !strcmp(tkl->hostmask, hostmask) && !strcmp(tkl->reason, reason) && tkl->expire_at == expire_at) {
					doadd = 0;
					break;
				}
			}
		}

		if(doadd) { // Still need to add?
			m_tkl(&me, &me, parc, tkllayer); // Ayyyy
			num++; // Muh counter lel
		}

		free(tkltype); // Shit was malloc'd yo
		if(spamf_check) free(spamf_check); // read_str() does a MyMalloc, so let's free to prevent mem0ry issues
		if(usermask) free(usermask); // D
		if(hostmask) free(hostmask); // i
		if(reason) free(reason); // tt
		if(setby) free(setby); // o
	}

	close(fd); // Don't forget to close 'em

	if(num) {
		// Send message about re-adding shit
		ircd_log(LOG_ERROR, "[storetkl] Re-added %d X:Lines", num);
		sendto_realops("[storetkl] Re-added %d X:Lines", num); // Probably won't be seen ever, but just in case ;]
	}

	if(rewrite) {
		// Send message about rewriting DB file
		ircd_log(LOG_ERROR, "[storetkl] Rewriting DB file due to %d skipped/expired X:Line%s", rewrite, (rewrite > 1 ? "s" : ""));
		sendto_realops("[storetkl] Rewriting DB file due to %d skipped/expired X:Line%s", rewrite, (rewrite > 1 ? "s" : "")); // Probably won't be seen ever, but just in case ;]
		return writeDB(NULL, 0); // Pass error code through here =]
	}

	return 0; // We good (non-zero = error)
}

static inline int read_data(int fd, void *buf, size_t count) {
	if((size_t)read(fd, buf, count) < count)
		return -1;
	return 0;
}

static inline int write_data(int fd, void *buf, size_t count) {
	if((size_t)write(fd, buf, count) < count)
		return -1;
	return 0;
}

static int write_str(int fd, char *x) {
	size_t count = x ? strlen(x) : 0;

	if(write_data(fd, &count, sizeof count))
		return -1;

	if(count) {
		if(write_data(fd, x, sizeof(char) * count))
			return -1;
	}

	return 0;
}

static int read_str(int fd, char **x) {
	size_t count;

	if(read_data(fd, &count, sizeof count))
		return -1;

	if(!count) {
		*x = NULL;
		return 0;
	}

	*x = (char *)MyMalloc(sizeof(char) * count + 1);
	if(read_data(fd, *x, sizeof(char) * count)) {
		MyFree(*x);
		*x = NULL;
		return -1;
	}
	(*x)[count] = 0;

	return 0;
}
