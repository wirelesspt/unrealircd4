/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gitgud.malvager.net/Wazakindjes/unrealircd_mods
** License: https://gitgud.malvager.net/Wazakindjes/unrealircd_mods/raw/master/LICENSE
*/

// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

// Hooktype to use
#define MYHEWK HOOKTYPE_PRE_USERMSG

// Muh macros
#define IsMDErr(x, y, z) \
	do { \
		if(!(x)) { \
			config_error("A critical error occurred when registering ModData for %s: %s", MOD_HEADER(y).name, ModuleGetErrorStr((z)->handle)); \
			return MOD_FAILED; \
		} \
	} while(0)

#define BACKPORT_CONNECTTIMEOUT (UNREAL_VERSION_GENERATION == 4 && UNREAL_VERSION_MAJOR == 0 && UNREAL_VERSION_MINOR <= 15)
#define BACKPORT (UNREAL_VERSION_GENERATION == 4 && UNREAL_VERSION_MAJOR == 0 && UNREAL_VERSION_MINOR < 12)

// Big hecks go here
typedef struct t_ctcpOverride cOverride;
struct t_ctcpOverride {
	cOverride *next, *prev;
	char *uid;
	time_t when;
};

// Quality fowod declarations
char *allowctcp_opers_preusermsg(aClient *sptr, aClient *to, char *text, int notice);
void override_md_free(ModData *md);
void check_stale_overrides(aClient *sptr);
cOverride *match_override(aClient *sptr, char *uid);
void add_override(aClient *sptr, aClient *to);
void delete_override(aClient *sptr, cOverride *covr);
void free_override(cOverride *covr);
static int IsACTCP(char *s);

#if BACKPORT
	long find_user_mode(char flag);
	int has_user_mode(aClient *acptr, char mode);
#endif

// Muh globals
ModDataInfo *COvrMDI; // To store some shit with the user's aClient pointur ;]
static ModuleInfo *allowctcp_opersMI = NULL; // Store ModuleInfo so we can use it to check for errors in MOD_LOAD
Hook *preUMsgHook = NULL;

// Dat dere module header
ModuleHeader MOD_HEADER(m_allowctcp_opers) = {
	"m_allowctcp_opers", // Module name
	"$Id: v1.11 2017/11/11 Gottem$", // Version
	"Allows opers to override someone's umode +T (noctcp)", // Description
	"3.2-b8-1", // Modversion, not sure wat do
	NULL
};

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT(m_allowctcp_opers) {
	preUMsgHook = HookAddPChar(modinfo->handle, MYHEWK, -1, allowctcp_opers_preusermsg); // Add a hook with higher priority than noctcp_preusermsg()

	// Request moddata for storing the actual overrides
	ModDataInfo mreq;
	memset(&mreq, 0, sizeof(mreq));
	mreq.type = MODDATATYPE_CLIENT; // Apply to users only (CLIENT actually includes servers but we'll disregard that here =])
	mreq.name = "allowctcp_opers"; // Name it
	mreq.free = override_md_free; // Function to free 'em
	COvrMDI = ModDataAdd(modinfo->handle, mreq);
	IsMDErr(COvrMDI, m_allowctcp_opers, modinfo);

	allowctcp_opersMI = modinfo; // Store module info yo
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
	return MOD_SUCCESS; // We good
}

// Actual hewk function m8
char *allowctcp_opers_preusermsg(aClient *sptr, aClient *to, char *text, int notice) {
	/* Arguments depend on hook type used obv
	**
	** sptr: Pointer to user executing command
	** to: Same sort of pointer to the target client
	** text: Actual text obv
	** notice: Was it a notice?
	*/
	cOverride *covr = NULL; // To store the matching entry for een bit
	if(MyClient(sptr) && IsACTCP(text) && IsOper(sptr)) { // If an oper is sending a CTCP
		if(has_user_mode(sptr, 'T') && !IsOper(to)) { // If oper has +T themselves and target is not an oper (override is not necessary in that case ;])
			check_stale_overrides(sptr); // Ayyy lmao
			add_override(sptr, to); // Add override for the reply yo
		}
		if(has_user_mode(to, 'T')) { // Target has +T, so bypass that shit
			sendto_one(to, ":%s %s %s :%s", sptr->name, (notice ? "NOTICE" : "PRIVMSG"), to->name, text); // Dirty hack goes here lol
			return NULL; // Don't process other hooks (i.e. usermodes/noctcp)
		}
		return text; // No further action required
	}

	if(has_user_mode(to, 'T') && IsACTCP(text) && IsOper(to)) { // If an oper is receiving a CTCP reply but has +T also
		if(MyClient(sptr) && !MyClient(to)) { // Target is on different server, so bypass that shit
			sendto_one(to, ":%s %s %s :%s", sptr->name, (notice ? "NOTICE" : "PRIVMSG"), to->name, text); // Dirty hack goes here lol
			return NULL; // Don't process other hooks (i.e. usermodes/noctcp)
		}

		if(MyClient(to)) { // If the target is on our server
			check_stale_overrides(to); // Flush stale overrides first
			if(IsOper(sptr) || (covr = match_override(to, sptr->id))) { // Checkem override
				delete_override(to, covr); // Clear entry first lol
				if(MyClient(sptr)) //{ // Only need to bypass here if it's local2local client
					sendto_one(to, ":%s %s %s :%s", sptr->name, (notice ? "NOTICE" : "PRIVMSG"), to->name, text); // Ayyy we good
			}
			else { // No override found, unsolicited CTCP
				if(!notice)
					sendto_one(sptr, err_str(ERR_NOCTCP), me.name, sptr->name, to->name);
				return NULL; // Drop 'em
			}
		}
	}

	return text; // Process normally
}

void override_md_free(ModData *md) {
	if(md->ptr) { // r u insaiyan?
		cOverride *covrList, *covr, *next; // Sum iter8ors lol
		covrList = md->ptr; // Get pointur to head of teh list
		for(covr = covrList; covr; covr = next) { // Let's do all entries yo
			next = covr->next; // Get next entry in advance lol
			free_override(covr); // Free 'em imo
		}
		md->ptr = NULL; // Shit rip's if we don't kek
	}
}

void check_stale_overrides(aClient *sptr) {
	int ping; // Expiration time is the same as ping timeouts ;]
	cOverride *covrList, *covr, *next; // Sum iter8ors lol
	if(!sptr || !sptr->local) // r u insaiyan?
		return;

	if((covrList = moddata_client(sptr, COvrMDI).ptr)) { // One of the for loops MIGHT have cleared the entire list ;]
		#if BACKPORT_CONNECTTIMEOUT
			ping = (sptr->local->class ? sptr->local->class->pingfreq : CONNECTTIMEOUT);
		#else
			ping = (sptr->local->class ? sptr->local->class->pingfreq : iConf.handshake_timeout);
		#endif
		for(covr = covrList; covr; covr = next) { // Iterate em lol
			next = covr->next; // Next one imo
			if(ping > (TStime() - sptr->local->lasttime)) // Still good
				continue;

			// Doubly linked lists ftw yo
			if(covr->prev) // Is anything but the FIRST entry
				covr->prev->next = covr->next; // Previous entry should skip over dis one
			else { // Is the first entry
				moddata_client(sptr, COvrMDI).ptr = covr->next; // So make the moddata thingy point to the second one
				covrList = covr->next; // Really just for the if below (outside the lewp) =]
			}

			if(covr->next) // If anything but the LAST entry
				covr->next->prev = covr->prev; // Next entry should skip over dis one

			next = covr->next; // Next one imo
			free_override(covr); // Free 'em lol
		}

		if(!covrList) // We empty nao?
			moddata_client(sptr, COvrMDI).ptr = NULL; // Cuz inb4ripperoni
	}
}

cOverride *match_override(aClient *sptr, char *uid) {
	cOverride *covrList, *covr; // Some iter80rs lol
	if(!sptr || !uid || !uid[0]) // Sanity checks
		return NULL; // Lolnope

	if((covrList = moddata_client(sptr, COvrMDI).ptr)) { // Something st0red?
		for(covr = covrList; covr; covr = covr->next) { // Iter8 em
			if(!strcmp(covr->uid, uid)) // Checkem UID
				return covr; // Gottem
		}
	}
	return NULL; // Loln0pe
}

void add_override(aClient *sptr, aClient *to) {
	cOverride *covrList, *last, *cur; // Sum iterators famalam
	cOverride *covr; // New entry etc
	if(!sptr || !to || !to->id) // Sanity checks
		return; // kbai

	covr = (cOverride *)MyMallocEx(sizeof(cOverride)); // Alloc8 new entry pls
	covr->uid = strdup(to->id); // Set 'em UID
	covr->next = NULL; // Inb4rip
	covr->prev = NULL; // ditt0
	covr->when = TStime(); // Set timestamp lol
	if(!(covrList = moddata_client(sptr, COvrMDI).ptr)) { // One of the for loops MIGHT have cleared the entire list ;]
		moddata_client(sptr, COvrMDI).ptr = covr; // Necessary to properly st0re that shit
		return; // We good
	}

	// Dirty shit to get the last entry lol
	for(cur = covrList; cur; cur = cur->next)
		last = cur; // cur will end up pointing to NULL, so let's use the entry just before ;];]

	covr->prev = last; // The new entry's prev should point to the actual last entry
	last->next = covr; // And the last entry's next should be the new one obv =]
}

void delete_override(aClient *sptr, cOverride *covr) {
	cOverride *covrList, *last; // Sum iter8ors lol
	if(!covr || !covr->uid) // r u insaiyan?
		return;

	if(sptr && (covrList = moddata_client(sptr, COvrMDI).ptr)) { // One of the for loops MIGHT have cleared the entire list ;]
		for(last = covrList; last; last = last->next) { // Iterate em lol
			if(last == covr) { // We gottem match?
				// Doubly linked lists ftw yo
				if(last->prev) // Is anything but the FIRST entry
					last->prev->next = last->next; // Previous entry should skip over dis one
				else { // Is the first entry
					moddata_client(sptr, COvrMDI).ptr = last->next; // So make the moddata thingy point to the second one
					covrList = last->next; // Really just for the if below =]
				}

				if(last->next) // If anything but the LAST entry
					last->next->prev = last->prev; // Next entry should skip over dis one

				free_override(last); // Free 'em lol
				break; // Gtfo imo tbh famlammlmflma
			}
		}

		if(!covrList) // We empty nao?
			moddata_client(sptr, COvrMDI).ptr = NULL; // Cuz inb4ripperoni
	}
}

void free_override(cOverride *covr) {
	if(!covr) // LOLNOPE
		return;
	if(covr->uid) // Sanity cheqq lol
		free(covr->uid); // Free
	free(covr); // 'em
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
