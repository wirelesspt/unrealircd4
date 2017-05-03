// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

// Command strings
#define MSG_TEXTSHUN "TEXTSHUN"
#define MSG_TEXTSHUN_SHORT "TS"
#define MSG_TEXTSHUN_ALT "TLINE"

// Hewktypez
#define SCONNECT_HOOK HOOKTYPE_SERVER_CONNECT
#define PRCHANMSG_HOOK HOOKTYPE_PRE_CHANMSG
#define PRUSERMSG_HOOK HOOKTYPE_PRE_USERMSG

// Big hecks go here
typedef struct t_tline TLine;
struct t_tline {
	char *nickrgx;
	char *bodyrgx;
	time_t set;
	time_t expire;
	char *raisin;
	char *setby;
	TLine *next;
};

// Dem macros yo
#define DelCommand(x) if(x) CommandDel(x); x = NULL // For unregistering our command when unloading/rehashing
#define DelHook(x) if(x) HookDel(x); x = NULL
CMD_FUNC(m_textshun); // Register command function

#define IsMDErr(x, y, z) \
	do { \
		if(!(x)) { \
			config_error("A critical error occurred when registering ModData for %s: %s", MOD_HEADER(y).name, ModuleGetErrorStr((z)->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)

// Quality fowod declarations
static int dumpit(aClient *sptr, char **p);
void textshun_moddata_free(ModData *md);
void check_tlines(int gotexpire);
void add_tline(TLine *newtl);
void del_tline(TLine *muhtl);
TLine *get_tlines(void);
TLine *find_tline(char *nickrgx, char *bodyrgx);
TLine *match_tline(aClient *sptr, char *text);
int textshun_hook_serverconnect(aClient *sptr);
char *_check_premsg(aClient *sptr, char *text);
char *textshun_hook_prechanmsg(aClient *sptr, aChannel *chptr, char *text, int notice);
char *textshun_hook_preusermsg(aClient *sptr, aClient *to, char *text, int notice);

// Muh globals
ModDataInfo *textshunMDI; // To store the T:Lines with &me lol (hack so we don't have to use a .db file or some shit)
static ModuleInfo *textshunMI = NULL; // Store ModuleInfo so we can use it to check for errors in MOD_LOAD
Command *textshunCmd, *textshunCmdShort, *textshunCmdAlt; // Pointers to the commands we're gonna add
Hook *serverConnectHook, *preChanMsgHook, *preUserMsgHook; // Dem hewks lol
int TLC; // A counter for T:Lines so we can change the moddata back to NULL

// Help string in case someone does just /TEXTSHUN
static char *muhhalp[] = {
	/* Special characters:
	** STX = bold -- \x02
	** US = underlined -- \x1F
	*/
	"*** Help on /TEXTSHUN ***",
	"Enables opers to drop messages based on nick and body regexes (T:Lines).",
	"It only supports (PCRE) regexes because regular wildcards seem",
	"ineffective to me. ;] Also, you can't have spaces so you",
	"should simply use \\s. Also supports the aliases TS and TLINE.",
	"It's all case-insensitive by default. It also tells you if your",
	"regex is wrong (and what). The lines are network-wide.",
	"Servers, U:Lines and opers are exempt for obvious reasons.",
	"The nick regex is matched against both n!u@realhost and n!u@vhost masks.",
	" ",
	"Syntax:",
	"    /TEXTSHUN ADD/DEL nickrgx bodyrgx [expiration] reason",
	" ",
	"Examples:",
	"    /tline add guest.+ h[o0]+m[o0]+ 0 nope",
	"    /textshun add guest.+ h[o0]+m[o0]+ nope",
	"    /ts del guest.+ h[o0]+m[o0]+",
	"        Adds/deletes the same T:Line, with no expiration",
	"    /tline add guest.+ h[o0]+m[o0]+ 3600 ain't gonna happen",
	"    /tline add guest.+ h[o0]+m[o0]+ 1h ain't gonna happen",
	"        Add a T:Line that expires in an hour",
	"    /tline",
	"        Show all T:Lines",
	NULL
};

// Dat dere module header
ModuleHeader MOD_HEADER(m_textshun) = {
	"m_textshun", // Module name
	"$Id: v1.02 2017/03/11 Gottem$", // Version
	"Drop messages based on nick and body", // Description
	"3.2-b8-1", // Modversion, not sure wat do
	NULL
};

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT(m_textshun) {
	TLine *TLineList, *tEntry; // To initialise the TLC counter imo tbh fam
	// If command(s) already exist(s) for some reason, bail out
	if(CommandExists(MSG_TEXTSHUN)) {
		config_error("Command %s already exists", MSG_TEXTSHUN);
		return MOD_FAILED;
	}
	if(CommandExists(MSG_TEXTSHUN_SHORT)) {
		config_error("Command %s already exists", MSG_TEXTSHUN_SHORT);
		return MOD_FAILED;
	}
	if(CommandExists(MSG_TEXTSHUN_ALT)) {
		config_error("Command %s already exists", MSG_TEXTSHUN_ALT);
		return MOD_FAILED;
	}

	TLC = 0; // Start with 0 obv lmao
	if(!(textshunMDI = findmoddata_byname("textshun_list", MODDATATYPE_CLIENT))) { // Attempt to find active moddata (like in case of a rehash)
		ModDataInfo mreq; // No moddata, let's request that shit
		memset(&mreq, 0, sizeof(mreq)); // Set 'em lol
		mreq.type = MODDATATYPE_CLIENT; // Apply to users only (CLIENT actually includes servers but we'll disregard that =])
		mreq.name = "textshun_list"; // Name it
		mreq.free = textshun_moddata_free; // Function to free 'em
		mreq.serialize = NULL;
		mreq.unserialize = NULL;
		mreq.sync = 0;
		textshunMDI = ModDataAdd(modinfo->handle, mreq); // Add 'em yo
		IsMDErr(textshunMDI, m_textshun, modinfo);
	}
	else { // We did get moddata
		if((TLineList = get_tlines())) { // So load 'em
			for(tEntry = TLineList; tEntry; tEntry = tEntry->next) // and iter8 m8
				TLC++; // Ayyy premium countur
		}
	}

	// Add muh hooks with (mostly) high prio lol
	serverConnectHook = HookAdd(modinfo->handle, SCONNECT_HOOK, 0, textshun_hook_serverconnect);
	preChanMsgHook  = HookAddPChar(modinfo->handle, PRCHANMSG_HOOK, -100, textshun_hook_prechanmsg);
	preUserMsgHook = HookAddPChar(modinfo->handle, PRUSERMSG_HOOK, -100, textshun_hook_preusermsg);

	// Dem commands fam
	textshunCmd = CommandAdd(modinfo->handle, MSG_TEXTSHUN, m_textshun, MAXPARA, M_SERVER | M_USER);
	textshunCmdShort = CommandAdd(modinfo->handle, MSG_TEXTSHUN_SHORT, m_textshun, MAXPARA, M_SERVER | M_USER);
	textshunCmdAlt = CommandAdd(modinfo->handle, MSG_TEXTSHUN_ALT, m_textshun, MAXPARA, M_SERVER | M_USER);

	textshunMI = modinfo; // Store module info yo
	return MOD_SUCCESS; // Let MOD_LOAD handle module errors
}

// Actually load the module here (also command overrides as they may not exist in MOD_INIT yet)
MOD_LOAD(m_textshun) {
	// Did the module throw an error during initialisation, or is one of the h00k/command pointers null even?
	if(ModuleGetError(textshunMI->handle) != MODERR_NOERROR || !serverConnectHook || !preChanMsgHook || !preUserMsgHook || !textshunCmd || !textshunCmdShort || !textshunCmdAlt) {
		// Display error string kek
		config_error("A critical error occurred when loading module %s: %s", MOD_HEADER(m_textshun).name, ModuleGetErrorStr(textshunMI->handle));
		return MOD_FAILED; // No good
	}

	return MOD_SUCCESS; // We good
}

// Called on unload/rehash obv
MOD_UNLOAD(m_textshun) {
	TLine *TLineList;
	// Attempt to unregister hewks and custom commands
	DelHook(serverConnectHook);
	DelHook(preChanMsgHook);
	DelHook(preUserMsgHook);
	DelCommand(textshunCmd);
	DelCommand(textshunCmdShort);
	DelCommand(textshunCmdAlt);
	// Not clearing the moddata structs here so we can re-use them easily ;];]
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

// Probably never called but it's a required function
// The free shit here normally only happens when the client attached to the moddata quits (afaik), but that's us =]
void textshun_moddata_free(ModData *md) {
	if(md->ptr) { // r u insaiyan?
		TLine *tEntry = md->ptr; // Cast em
		if(tEntry->nickrgx) free(tEntry->nickrgx); // Gotta
		if(tEntry->bodyrgx) free(tEntry->bodyrgx); // free
		if(tEntry->raisin) free(tEntry->raisin); // 'em
		if(tEntry->setby) free(tEntry->setby); // all
		tEntry->set = 0; // Just in case lol
		tEntry->expire = 0L; // ditt0
		md->ptr = NULL; // d-d-ditt0
	}
}

// Check for expiring T:Lines
void check_tlines(int gotexpire) {
	TLine *TLineList, *head, *last, **tEntry;
	char gmt[256]; // For a pretty timestamp instead of UNIX time lol
	char *timeret; // Ditto
	TS setat; // For use with the pretty timestamp
	if(!(TLineList = get_tlines())) // Ayyy no T:Lines known
		return;

	tEntry = &TLineList; // Hecks so the ->next chain stays intact
	head = TLineList;
	while(*tEntry) { // Loop while we have entries obv
		if((*tEntry)->expire > 0 && TStime() > ((*tEntry)->set + (*tEntry)->expire)) { // Do we need to expire it?
			last = *tEntry; // Get the entry pointur
			*tEntry = last->next; // Set the iterat0r to the next one

			if(last == head) { // If it's the first entry, need to take special precautions ;]
				moddata_client((&me), textshunMDI).ptr = *tEntry; // Cuz shit rips if we don't do dis
				head = *tEntry; // Move head up
			}

			// Get pretty timestamp =]
			setat = last->set;
			timeret = asctime(gmtime((TS *)&setat));
			strlcpy(gmt, timeret, sizeof(gmt));
			iCstrip(gmt);

			// Send expiration notice to all _local_ opers lol (the "bogus" TLINE command below will take care of expiring it on other servers)
			sendto_snomask(SNO_TKL, "*** Expiring T:Line set by %s at %s GMT for nick /%s/ and body /%s/ [reason: %s]", last->setby, gmt, last->nickrgx, last->bodyrgx, last->raisin);
			if(!gotexpire)
				sendto_server(&me, 0, 0, ":%s TLINE EXPIRE", me.name); // check_tlines() is called by servers for every TLINE command, so no need for args here ;];]

			if(last->nickrgx) free(last->nickrgx); // Gotta
			if(last->bodyrgx) free(last->bodyrgx); // free
			if(last->raisin) free(last->raisin); // em
			if(last->setby) free(last->setby); // all
			free(last); // lol
			TLC--;
		}
		else {
			tEntry = &(*tEntry)->next; // No need for expiration, go to the next one
		}
	}
	if(TLC <= 0) // Cuz shit rips if we don't do dis
		moddata_client((&me), textshunMDI).ptr = NULL;
}

// Add new T:Line obv fam
void add_tline(TLine *newtl) {
	TLine *TLineList, *tEntry; // Head + iter8or imo tbh
	TLC++; // Always increment count
	if(!(TLineList = get_tlines())) { // If TLineList is NULL...
		TLineList = newtl; // ...simply have it point to the newly alloc8ed entry
		moddata_client((&me), textshunMDI).ptr = TLineList; // And st0re em
		return;
	}
	for(tEntry = TLineList; tEntry && tEntry->next; tEntry = tEntry->next) { } // Dirty shit to get teh last entray
	tEntry->next = newtl; // Append lol
}

// Delete em fam
void del_tline(TLine *muhtl) {
	TLine *TLineList, *last, **tEntry;
	if(!(TLineList = get_tlines())) // Ayyy no T:Lines known
		return;

	tEntry = &TLineList; // Hecks so the ->next chain stays intact
	if(*tEntry == muhtl) { // If it's the first entry, need to take special precautions ;]
		last = *tEntry; // Get the entry pointur
		*tEntry = last->next; // Set the iterat0r to the next one
		if(last->nickrgx) free(last->nickrgx); // Gotta
		if(last->bodyrgx) free(last->bodyrgx); // free
		if(last->raisin) free(last->raisin); // em
		if(last->setby) free(last->setby); // all
		free(last); // lol
		moddata_client((&me), textshunMDI).ptr = *tEntry; // Cuz shit rips if we don't do dis
		TLC--;
		return;
	}

	while(*tEntry) { // Loop while we have entries obv
		if(*tEntry == muhtl) { // Do we need to delete em?
			last = *tEntry; // Get the entry pointur
			*tEntry = last->next; // Set the iterat0r to the next one
			if(last->nickrgx) free(last->nickrgx); // Gotta
			if(last->bodyrgx) free(last->bodyrgx); // free
			if(last->raisin) free(last->raisin); // em
			if(last->setby) free(last->setby); // all
			free(last); // lol
			TLC--;
			break;
		}
		else {
			tEntry = &(*tEntry)->next; // No need, go to the next one
		}
	}
	if(TLC <= 0) // Cuz shit rips if we don't do dis
		moddata_client((&me), textshunMDI).ptr = NULL;
}

// Get (head of) the T:Line list
TLine *get_tlines(void) {
	TLine *TLineList = moddata_client((&me), textshunMDI).ptr; // Get mod data
	// Sanity check lol
	if(TLineList && TLineList->nickrgx)
		return TLineList;
	return NULL;
}

// Find a specific T:Line (based on nick and body regex lol)
TLine *find_tline(char *nickrgx, char *bodyrgx) {
	TLine *TLineList, *tEntry; // Head and iter8or fam
	if((TLineList = get_tlines())) { // Check if the list even has entries kek
		for(tEntry = TLineList; tEntry; tEntry = tEntry->next) { // Iter8 em
			// The regex match itself (aMatch *) is case-insensitive anyways, so let's do stricmp() here =]
			if(!stricmp(tEntry->nickrgx, nickrgx) && !stricmp(tEntry->bodyrgx, bodyrgx))
				return tEntry;
		}
	}
	return NULL; // Not found m8
}

// For matching a user and string to a T:Line
TLine *match_tline(aClient *sptr, char *text) {
	check_tlines(0); // Check for expiring T:Lines fam, also notify other servers if any
	char *mask = make_nick_user_host(sptr->name, sptr->user->username, sptr->user->realhost); // Get nick!user@host with the real hostnaem
	char *vmask = (sptr->user->virthost ? make_nick_user_host(sptr->name, sptr->user->username, sptr->user->virthost) : NULL); // Also virthost, if any
	aMatch *exprNick, *exprBody; // For creating the actual match struct pointer thingy
	int nickmatch, bodmatch; // Did we get een match?
	TLine *TLineList, *tEntry; // Head and iter8or fam

	if(!text || !mask) // r u insaiyan lol?
		return NULL;

	if((TLineList = get_tlines())) { // Check if the list even has entries kek
		for(tEntry = TLineList; tEntry; tEntry = tEntry->next) {
			nickmatch = bodmatch = 0;
			exprNick = unreal_create_match(MATCH_PCRE_REGEX, tEntry->nickrgx, NULL); // Create match struct for nikk regex
			exprBody = unreal_create_match(MATCH_PCRE_REGEX, tEntry->bodyrgx, NULL); // Also for body
			if(!exprNick || !exprBody) // If either failed, gtfo
				continue;

			if(vmask) // If virthost exists
				nickmatch = (unreal_match(exprNick, mask) || unreal_match(exprNick, vmask)); // Check if it either matches obv
			else // If it doesn't (no umode +x, no NickServ vhost, etc)
				nickmatch = unreal_match(exprNick, mask); // Matchem real host only
			bodmatch = unreal_match(exprBody, text);

			unreal_delete_match(exprNick); // Cleanup
			unreal_delete_match(exprBody); // lol
			if(nickmatch && bodmatch)
				return tEntry;
		}
	}
	return NULL; // rip
}

// Internal function called by the pre*msg hooks ;];]
char *_check_premsg(aClient *sptr, char *text) {
	TLine *tEntry; // Iter8or
	char *body = (char *)StripControlCodes(StripColors(text)); // Strip all markup shit (bold, italikk etc) and colours
	if(!IsServer(sptr) && !IsMe(sptr) && !IsULine(sptr) && !IsOper(sptr) && (tEntry = match_tline(sptr, body))) { // Servers, U:Lines and opers are exempt for obv raisins
		// If match, send notices to all other servers/opers =]
		sendto_snomask(SNO_TKL, "*** T:Line for nick /%s/ and body /%s/ matched by %s [body: %s]", tEntry->nickrgx, tEntry->bodyrgx, sptr->name, body);
		sendto_server(&me, 0, 0, ":%s SENDSNO G :*** T:Line for nick /%s/ and body /%s/ matched by %s [body: %s]", me.name, tEntry->nickrgx, tEntry->bodyrgx, sptr->name, body);
		return NULL; // And return NULL to discard the entire message (quality shun imo tbh)
	}
	return text; // Allowed, return original text
}

// Server connect hewk familia
int textshun_hook_serverconnect(aClient *sptr) {
	// Sync T:Lines fam
	TLine *TLineList, *tEntry; // Head and iter8or ;];]
	if((TLineList = get_tlines())) { // Gettem list
		for(tEntry = TLineList; tEntry; tEntry = tEntry->next) {
			if(!tEntry || !tEntry->nickrgx) // Sanity check imo ;]
				continue;
			// Syntax for servers is a bit different (namely the setby arg and the : before reason (makes the entire string after be considered one arg ;];])
			sendto_one(sptr, ":%s TLINE ADD %s %s %ld %ld %s :%s", me.name, tEntry->nickrgx, tEntry->bodyrgx, tEntry->set, tEntry->expire, tEntry->setby, tEntry->raisin);
		}
	}
	return HOOK_CONTINUE;
}

// Pre message hewks lol
char *textshun_hook_prechanmsg(aClient *sptr, aChannel *chptr, char *text, int notice) {
	return _check_premsg(sptr, text);
}

char *textshun_hook_preusermsg(aClient *sptr, aClient *to, char *text, int notice) {
	return _check_premsg(sptr, text);
}

// Function for /TLINE etc
CMD_FUNC(m_textshun) {
	/* Gets args: aClient *cptr, aClient *sptr, int parc, char[] parv
	**
	** cptr: Pointer to directly attached client -- if remote user this is the remote server instead
	** sptr: Pointer to user executing command -- you'll probably wanna use this fam
	** parc: Amount of arguments (also includes the command in the count)
	** parv: Contains the actual args, first one starts at parv[1]
	**
	** So "TEXTSHUN test" would result in parc = 2 and parv[1] = "test"
	** Also, parv[0] seems to always be NULL, so better not rely on it fam
	*/
	aMatch *exprNick, *exprBody; // For verifying the regexes
	char *regexerr, *regexerr_nick, *regexerr_body; // Error pointers (regexerr is static so better strdup em)
	TLine *TLineList, *newtl, *tEntry; // Quality struct pointers
	char *nickrgx, *bodyrgx, *exptmp, *setby; // Muh args
	char raisin[BUFSIZE]; // Reasons may or may not be pretty long
	char gmt[256], gmt2[256]; // For a pretty timestamp instead of UNIX time lol
	char *timeret; // Ditto
	char cur, prev, prev2; // For checking time strings
	long setat, expire; // After how many seconds the T:Line should expire
	TS expiry; // For use with the pretty timestamps
	int i, rindex, del, nickrgx_ok, bodyrgx_ok; // Iterat0rs and "booleans" =]

	// Gotta be at least a server, U:Line or oper with correct privs lol
	if((!IsServer(sptr) && !IsMe(sptr) && !IsULine(sptr) && !IsOper(sptr)) || !ValidatePermissionsForPath("textshun", sptr, NULL, NULL, NULL)) {
		sendto_one(sptr, err_str(ERR_NOPRIVILEGES), me.name, sptr->name); // Check ur privilege fam
		return -1; // Ain't gonna happen lol
	}

	check_tlines((IsServer(cptr) && !BadPtr(parv[1]) && !stricmp(parv[1], "expire"))); // Check for expiring T:Lines fam

	// If no args given (or we got /tline list)
	if(BadPtr(parv[1]) || !stricmp(parv[1], "list")) {
		if(IsServer(cptr)) // No need to list shit for servers =]
			return 0;
		if(!(TLineList = get_tlines())) // Attempt to get list
			sendnotice(sptr, "*** No T:Lines found");
		else {
			for(tEntry = TLineList; tEntry; tEntry = tEntry->next) {
				timeret = asctime(gmtime((TS *)&tEntry->set));
				strlcpy(gmt2, timeret, sizeof(gmt2));
				iCstrip(gmt2);
				if(tEntry->expire == 0) // Let's show "permanent" for permanent T:Lines, n0? =]
					sendnotice(sptr, "*** Permanent T:Line set by %s at %s GMT for nick /%s/ and body /%s/ [reason: %s]", tEntry->setby, gmt2, tEntry->nickrgx, tEntry->bodyrgx, tEntry->raisin);
				else {
					// Get pretty timestamp for expiring lines =]
					expiry = tEntry->set + tEntry->expire;
					timeret = asctime(gmtime((TS *)&expiry));
					strlcpy(gmt, timeret, sizeof(gmt));
					iCstrip(gmt);
					sendnotice(sptr, "*** T:Line set by %s at %s GMT for nick /%s/ and body /%s/, expiring at %s GMT [reason: %s]", tEntry->setby, gmt2, tEntry->nickrgx, tEntry->bodyrgx, gmt, tEntry->raisin);
				}
			}
		}
		return 0;
	}

	// Need at least 4 args lol
	if(parc <= 2 || !stricmp(parv[1], "help") || !stricmp(parv[1], "halp")) {
		return dumpit(sptr, muhhalp); // Return help string instead
	}
	del = (!stricmp(parv[1], "del") ? 1 : 0); // Are we deleting?
	if((!del && parc < 5) || (del && parc < 4)) { // Delete doesn't require the expire and reason fields
		sendto_one(sptr, err_str(ERR_NEEDMOREPARAMS), me.name, sptr->name, "TEXTSHUN"); // Need m0ar lol
		return -1;
	}
	if(stricmp(parv[1], "add") && stricmp(parv[1], "del")) { // If first arg is neither add nor del, fuck off
		sendnotice(sptr, "*** [textshun] First arg must be either ADD or DEL");
		return -1;
	}

	// Extra args required for servers (setby and setat fields, used during linking yo)
	if(IsServer(cptr) && parc <= 7)
		return -1; // Return silently

	// Initialise a bunch of shit
	memset(raisin, '\0', sizeof(raisin));
	exptmp = regexerr_nick = regexerr_body = NULL;
	expire = 0L;
	setat = TStime();
	rindex = nickrgx_ok = bodyrgx_ok = 0;
	nickrgx = parv[2];
	bodyrgx = parv[3];
	exptmp = parv[4];
	setby = sptr->name;

	exprNick = unreal_create_match(MATCH_PCRE_REGEX, nickrgx, &regexerr); // Attempt to create match struct
	if(!exprNick && regexerr && !IsServer(cptr)) { // Servers don't need to get a notice for invalid shit
		regexerr_nick = strdup(regexerr); // Must dup regexerr here ;]
		regexerr = NULL; // Nullify just to be shur
	}

	exprBody = unreal_create_match(MATCH_PCRE_REGEX, bodyrgx, &regexerr); // Attempt to create match struct
	if(!exprBody && regexerr && !IsServer(cptr)) { // Servers don't need to get a notice for invalid shit
		regexerr_body = strdup(regexerr);
		regexerr = NULL; // Nullify just to be shur
	}

	// We good?
	nickrgx_ok = (exprNick ? 1 : 0);
	bodyrgx_ok = (exprBody ? 1 : 0);
	if(exprNick) unreal_delete_match(exprNick); // Cleanup
	if(exprBody) unreal_delete_match(exprBody); // lol

	// Most shit is silent for servers
	if(IsServer(cptr)) {
		if(!nickrgx_ok || !bodyrgx_ok) // Both should be sane obv
			return -1; // Return silently

		tEntry = find_tline(nickrgx, bodyrgx); // Attempt to find existing T:Line
		if(!del && tEntry) // Adding a T:Line but it already exists
			return -1; // Return silently
		else if(del && !tEntry) // Deleting but doesn't exist
			return -1; // Return silently

		strlcpy(raisin, parv[7], sizeof(raisin)); // Copy the reason field imo tbh
		setat = atol(parv[4]); // Extra arg yo
		expire = atol(parv[5]); // Set expiration
		setby = parv[6]; // Extra arg yo
		if(setat <= 0) // Some error occured lol
			return -1; // Gtfo silently
	}

	// Command came from a user
	else {
		if(!nickrgx_ok || !bodyrgx_ok) { // Both need to be sane obv
			if(!nickrgx_ok) sendnotice(sptr, "*** [textshun] Invalid nick regex /%s/ [err: %s]", nickrgx, regexerr_nick); // Report regex error for nikk
			if(!bodyrgx_ok) sendnotice(sptr, "*** [textshun] Invalid body regex /%s/ [err: %s]", bodyrgx, regexerr_body); // For body too
			if(regexerr_nick) free(regexerr_nick); // Free if exists
			if(regexerr_body) free(regexerr_body); // Ditto
			return -1; // Lolnope
		}

		// Just in case ;]
		if(regexerr_nick) free(regexerr_nick);
		if(regexerr_body) free(regexerr_body);

		tEntry = find_tline(nickrgx, bodyrgx); // Attempt to find existing T:Line
		if(!del && tEntry) { // Adding a T:Line but it already exists
			sendnotice(sptr, "*** T:Line for nick /%s/ and body /%s/ already exists", nickrgx, bodyrgx);
			return -1; // Lolnope
		}
		else if(del && !tEntry) { // Deleting but doesn't exist
			sendnotice(sptr, "*** T:Line for nick /%s/ and body /%s/ doesn't exist", nickrgx, bodyrgx);
			return -1; // Lolnope
		}

		// If adding, check for expiration and reason fields
		if(!del) {
			// Let's check for a time string (3600, 1h, 2w3d, etc)
			for(i = 0; exptmp[i] != 0; i++) {
				cur = exptmp[i];
				if(!isdigit(cur)) { // No digit, check for the 'h' in '1h' etc
					prev = (i >= 1 ? exptmp[i - 1] : 0);
					prev2 = (i >= 2 ? exptmp[i - 2] : 0);

					if((prev && prev2 && isdigit(prev2) && prev == 'm' && cur == 'o') || (prev && isdigit(prev) && strchr("smhdw", cur))) // Check for allowed combos
						continue;

					exptmp = NULL; // Fuck off
					rindex = 4; // Reason index for parv[] is 4
					break; // Only one mismatch is enough
				}
			}

			if(exptmp) { // If the for() loop didn't enter the inner if(), expire field is sane
				expire = config_checkval(exptmp, CFG_TIME); // So get a long from the (possible) time string
				rindex = 5; // And set reason index for parv[] to 5
			}

			if(!rindex || BadPtr(parv[rindex]) || !strlen(parv[rindex])) { // If rindex is 0 it means the arg is missing
				sendnotice(sptr, "*** [textshun] The reason field is required");
				return -1; // No good fam
			}

			// Now start from rindex and copy dem remaining args
			for(i = rindex; parv[i] != NULL; i++) {
				if(i == rindex)
					strlcpy(raisin, parv[i], sizeof(raisin));
				else {
					strlcat(raisin, " ", sizeof(raisin));
					strlcat(raisin, parv[i], sizeof(raisin));
				}
			}
		}
	}

	// For both servers and users ;]
	if(!del) {
		// Allocate/initialise mem0ry for the new entry
		newtl = malloc(sizeof(TLine));
		newtl->nickrgx = strdup(nickrgx);
		newtl->bodyrgx = strdup(bodyrgx);
		newtl->set = setat;
		newtl->expire = expire;
		newtl->raisin = strdup(raisin);
		newtl->setby = strdup(setby);
		newtl->next = NULL;
		tEntry = newtl;
		add_tline(newtl); // Add em
	}

	// Propagate the T:Line to other servers if it came from a user only (when servers link they sync it themselves)
	if(!IsServer(sptr))
		sendto_server(&me, 0, 0, ":%s TLINE %s %s %s %ld %ld %s :%s", sptr->name, (del ? "DEL" : "ADD"), nickrgx, bodyrgx, tEntry->set, tEntry->expire, setby, tEntry->raisin); // Muh raw command
	else { // If it did come from a server, let's make a "set at" timestamp =]
		timeret = asctime(gmtime((TS *)&tEntry->set));
		strlcpy(gmt2, timeret, sizeof(gmt2));
		iCstrip(gmt2);
	}
	// Also send snomask notices to all local opers =]
	if(tEntry->expire == 0) { // Permanent lol
		if(IsServer(sptr)) // Show "set at" during sync phase ;]
			sendto_snomask(SNO_TKL, "*** Permanent T:Line %sed by %s at %s GMT for nick /%s/ and body /%s/ [reason: %s]", (del ? "delet" : "add"), setby, gmt2, nickrgx, bodyrgx, tEntry->raisin);
		else
			sendto_snomask(SNO_TKL, "*** Permanent T:Line %sed by %s for nick /%s/ and body /%s/ [reason: %s]", (del ? "delet" : "add"), setby, nickrgx, bodyrgx, tEntry->raisin);
	}
	else {
		// Make pretty expiration timestamp if not a permanent T:Line
		expiry = tEntry->set + tEntry->expire;
		timeret = asctime(gmtime((TS *)&expiry));
		strlcpy(gmt, timeret, sizeof(gmt));
		iCstrip(gmt);
		if(IsServer(sptr)) // Show "set at" during sync phase ;]
			sendto_snomask(SNO_TKL, "*** T:Line %sed by %s at %s GMT for nick /%s/ and body /%s/, expiring at %s GMT [reason: %s]", (del ? "delet" : "add"), setby, gmt2, nickrgx, bodyrgx, gmt, tEntry->raisin);
		else
			sendto_snomask(SNO_TKL, "*** T:Line %sed by %s for nick /%s/ and body /%s/, expiring at %s GMT [reason: %s]", (del ? "delet" : "add"), setby, nickrgx, bodyrgx, gmt, tEntry->raisin);
	}

	// Delete em famamlamlamlmal
	if(del)
		del_tline(tEntry);

	return 0; // All good
}