/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gitgud.malvager.net/Wazakindjes/unrealircd_mods
** License: https://gitgud.malvager.net/Wazakindjes/unrealircd_mods/raw/master/LICENSE
*/

#include "unrealircd.h"

// Some macros lel
#define IsParam(x) (parc > (x) && !BadPtr(parv[(x)]))
#define IsNotParam(x) (parc <= (x) || BadPtr(parv[(x)]))

// Muh fowod declaration
aTKline *_my_tkl_del_line(aTKline *tkl); // Function to remove *lines from current server

static ModuleInfo *rmtklMI = NULL; // Store ModuleInfo so we can use it to check for errors in MOD_LOAD
Command *CmdRmtkl = NULL; // Store pointer so we can check if we got an error

CMD_FUNC(m_rmtkl); // Register command function

ModuleHeader MOD_HEADER(m_rmtkl) = {
	"m_rmtkl",
	"$Id: v1.23 2017/11/26 Gottem$",
	"Adds /rmtkl command to easily remove *:Lines in bulk",
	"3.2-b8-1",
	NULL
};

MOD_INIT(m_rmtkl) {
	// If command already exists for some reason, bail out
	if(CommandExists("RMTKL")) {
		config_error("Command RMTKL already exists");
		return MOD_FAILED;
	}

	CmdRmtkl = CommandAdd(modinfo->handle, "RMTKL", m_rmtkl, 4, M_USER); // Register command with the IRCd
	rmtklMI = modinfo; // Store module info yo
	return MOD_SUCCESS;
}

MOD_LOAD(m_rmtkl) {
	// When linking dynamically we can get a more meaningful error message
#ifndef STATIC_LINKING
	if(ModuleGetError(rmtklMI->handle) != MODERR_NOERROR || !CmdRmtkl)
#else
	if(!CmdRmtkl)
#endif
	{
#ifndef STATIC_LINKING
		config_error("Error adding command RMTKL: %s", ModuleGetErrorStr(rmtklMI->handle));
#else
		config_error("Error adding command RMTKL");
#endif
		return MOD_FAILED;
	}

	return MOD_SUCCESS;
}


MOD_UNLOAD(m_rmtkl) {
	return MOD_SUCCESS;
}

/*
 * =================================================================
 * dumpit:
 *     Dump a NULL-terminated array of strings to user sptr using
 *     the numeric rplnum, and then return 0.
 *     (Taken from DarkFire IRCd)
 * =================================================================
 */

static int dumpit(aClient *sptr, char **p) {
	if(IsServer(sptr)) // Bail out early and silently if it's a server =]
		return 0;

	for(; *p != NULL; p++)
		sendto_one(sptr, ":%s %03d %s :%s", me.name, RPL_TEXT, sptr->name, *p);

	/* let user take 8 seconds to read it! */
	sptr->local->since += 8;
	return 0;
}

/* Help for /rmtkl command */

static char *rmtkl_help[] = {
	"*** \002Help on /rmtkl\002 *** ",
	"COMMAND - Removes all TKLs matching the given conditions from the",
	"local server or the IRC Network depending on it's a global ban or not.",
	"With this command you can remove any type of TKLs.",
	"Syntax:",
	"    \002/rmtkl\002 \037type\037 \037user@host\037 [\037comment\037] [\037-skipperm\037]",
	"The \037type\037 field may contain any number of the following characters:",
	"    K, z, G, Z, q, Q, F and *",
	"    (asterisk includes every types but q, Q and F).",
	"The \037user@host\037 field is a wildcard mask to match an user@host which",
        "    a ban was set on.",
	"The \037comment\037 field is also wildcard mask that you can match the",
	"    text of the reason for a ban.",
	"Examples:",
	"    - \002/rmtkl * *\002",
	"        [remove all TKLs but q and Q lines and spamfilters]",
	"    - \002/rmtkl GZ *@*.mx\002 * -skipperm",
	"        [remove all Mexican G/Z:Lines while skipping over permanent ones]",
	"    - \002/rmtkl * * *Zombie*\002",
	"        [remove all non-nick bans having \037Zombie\037 in their reasons]",
	"*** \002End of help\002 ***",
	NULL
};

// =================================================================
// Array of TKL types
// =================================================================

typedef struct {
	int	type;
	char flag;
	char *txt;
	char *pflag;
} TKLType;

TKLType tkl_types[] = {
	{ TKL_KILL, 'K', "K:Line", "tkl:kline:remove" },
	{ TKL_ZAP, 'z',	"Z:Line", "tkl:zline:local:remove" },
	{ TKL_KILL | TKL_GLOBAL, 'G', "G:Line", "tkl:gline:remove" },
	{ TKL_ZAP | TKL_GLOBAL, 'Z', "Global Z:Line", "tkl:zline:remove" },
	{ TKL_SHUN | TKL_GLOBAL, 's', "Shun", "tkl:shun:remove" },
	{ TKL_NICK, 'q', "Q:Line", "tkl:qline:local:remove" },
	{ TKL_NICK | TKL_GLOBAL, 'Q', "Global Q:Line", "tkl:qline:remove" },
	{ TKL_SPAMF | TKL_GLOBAL, 'F', "Global Spamfilter", "tkl:spamfilter:remove" },
	{ 0, 0, "Unknown *:Line", 0 },
};

static TKLType *find_TKLType_by_type(int type) {
	TKLType *t;

	for(t = tkl_types; t->type; t++)
		if(t->type == type)
			break;

	return t;
}

static TKLType *find_TKLType_by_flag(char flag) {
	TKLType *t;

	for(t = tkl_types; t->type; t++)
		if(t->flag == flag)
			break;

	return t;
}

// I nicked this from src/modules/m_tkl.c =]
aTKline *_my_tkl_del_line(aTKline *tkl) {
	aTKline *p, *q;
	int index = tkl_hash(tkl_typetochar(tkl->type));

	for(p = tklines[index]; p; p = p->next) {
		if(p == tkl) {
			q = p->next;
			MyFree(p->hostmask);
			MyFree(p->reason);
			MyFree(p->setby);
			if(p->type & TKL_SPAMF && p->ptr.spamf) {
				unreal_delete_match(p->ptr.spamf->expr);
				if(p->ptr.spamf->tkl_reason)
					MyFree(p->ptr.spamf->tkl_reason);
				MyFree(p->ptr.spamf);
			}
			DelListItem(p, tklines[index]);
			MyFree(p);
			return q;
		}
	}
	return NULL;
}

/*
 * =================================================================
 * m_rmtkl -- Remove all matching TKLs from the network
 *     parv[1] = ban types
 *     parv[2] = userhost mask
 *     parv[3] = comment mask (optional)
 * =================================================================
 */

CMD_FUNC(m_rmtkl) {
	/* Gets args: aClient *cptr, aClient *sptr, int parc, char *parv[]
	**
	** cptr: Pointer to directly attached client -- if remote user this is the remote server instead
	** sptr: Pointer to user executing command
	** parc: Amount of arguments (also includes the command in the count)
	** parv: Contains the actual args, first one starts at parv[1]
	**
	** So "RMTKL test" would result in parc = 2 and parv[1] = "test"
	** Also, parv[0] seems to always be NULL, so better not rely on it fam
	*/
	aTKline *tk, *next = NULL;
	TKLType *tkltype;
	char *types, *uhmask, *cmask, *p, *rawnick;
	char gmt[256], flag;
	int tklindex, skipperm = 0;
	TS curtime = TStime();

	// Is the client sending RMTKL even a ULine or oper?
	if(!IsULine(sptr) && !IsPerson(sptr) && !IsOper(sptr)) {
		sendto_one(sptr, err_str(ERR_NOPRIVILEGES), me.name, sptr->name);
		return -1;
	}

	if(IsNotParam(1))
		return dumpit(sptr, rmtkl_help);

	if(IsNotParam(2)) {
		/*
		 * In this case we don't send the entire help text to
		 * the client.
		 */
		sendto_one(sptr, ":%s NOTICE %s :Not enough parameters. Type /%s for help.", me.name, sptr->name, "RMTKL");
		return 0;
	}

	types = parv[1]; // First argument is what types to remove
	uhmask = parv[2]; // Second argument is user@host mask (or IP w/ zlines)
	cmask = IsParam(3) ? parv[3] : NULL; // Comment mask is optional

	if(IsParam(4)) {
		if(!match("-skipperm*", parv[4]))
			skipperm = 1;
	}

	/* I don't add q, Q and F here. They are different. */
	if(strchr(types, '*'))
		types = "KzGZs";

	// If client is not a uline, let's check oper privileges
	if(!IsULine(sptr)) {
		for(p = types; *p; p++) {
			tkltype = find_TKLType_by_flag(*p);
			if(!tkltype->type)
				continue;
			if(((tkltype->type & TKL_GLOBAL) && !IsOper(sptr)) || !ValidatePermissionsForPath(tkltype->pflag, sptr, NULL, NULL, NULL)) {
				sendto_one(sptr, err_str(ERR_NOPRIVILEGES), me.name, sptr->name);
				return -1;
			}
		}
	}

	// Loop over all supported TKL types
	for(tkltype = tkl_types; tkltype->type; tkltype++) {
		flag = tkltype->flag;
		tklindex = tkl_hash(flag);

		// Does the first arg even include this type?
		if(!strchr(types, flag))
			continue;

		// Loop over all TKL entries
		for(tk = tklines[tklindex]; tk; tk = next) {
			next = tk->next;

			// Reiterate if the entry's type doesn't match this type
			if(tk->type != tkltype->type)
				continue;

			// Is it a (global) Q:Line?
			if(tk->type & TKL_NICK) {
				/*
				 * If it's a services hold (ie. NickServ is holding
				 * a nick), it's better not to touch it
				 */
				if(*tk->usermask == 'H')
					continue;

				// Also reiterate if the user@host mask doesn't match the entry
				if(match(uhmask, tk->hostmask))
					continue;
			}

			// Or ein spamfiltur?
			else if(tk->type & TKL_SPAMF) {
				if(match(uhmask, tk->reason))
					continue;
			}

			else
				// Not a Q:Line, but does the passed user@host mask (or IP) match the entry?
				if(match(uhmask, make_user_host(tk->usermask, tk->hostmask)))
					continue;

			// If we have a comment mask and it doesn't match the entry, continue
			if(cmask && _match(cmask, tk->reason))
				continue;

			// If permanent *:line and -skipperm is set, reiter8 m8
			if(skipperm && tk->expire_at == 0)
				continue;

			// Convert "set at" timestamp to human readable time
			if(tk->set_at) {
				strlcpy(gmt, asctime(gmtime((TS *)&tk->set_at)), sizeof(gmt));
				if(gmt)
					iCstrip(gmt);
			}

			// (Global) Q:Line have a slightly different format in snomasks
			if(tk->type & TKL_NICK) {
				sendto_snomask(SNO_TKL, "%s removed %s %s (set at %s " "- reason: %s)", sptr->name, tkltype->txt, tk->hostmask, (gmt ? gmt : "<unknown>"), tk->reason);
				ircd_log(LOG_TKL, "%s removed %s %s (set at %s - reason: %s)", sptr->name, tkltype->txt, tk->hostmask, (gmt ? gmt : "<unknown>"), tk->reason);
			}

			// Also spamfilters =]
			else if(tk->type & TKL_SPAMF) {
				// Skip default spamfilters and ones added through configs
				if(flag == 'f') // Cuz apparently 'f' means it was added through the conf or is built-in ('F' is ok tho)
					continue;

				sendto_snomask(SNO_TKL, "%s removed %s [%s] %s (set at %s " "- reason: %s)", sptr->name, tkltype->txt, banact_valtostring(tk->ptr.spamf->action), tk->reason, (gmt ? gmt : "<unknown>"), tk->ptr.spamf->tkl_reason);

				ircd_log(LOG_TKL, "%s removed %s [%s] %s (set at %s " "- reason: %s)", sptr->name, tkltype->txt, banact_valtostring(tk->ptr.spamf->action), tk->reason, (gmt ? gmt : "<unknown>"), tk->ptr.spamf->tkl_reason);

			}

			// All other *:Lines have the same format
			else {
				sendto_snomask(SNO_TKL, "%s removed %s %s@%s (set at %s - reason: %s)", sptr->name, tkltype->txt, tk->usermask, tk->hostmask, (gmt ? gmt : "<unknown>"), tk->reason);
				ircd_log(LOG_TKL, "%s removed %s %s@%s (set at %s - reason: %s)", sptr->name, tkltype->txt, tk->usermask, tk->hostmask, (gmt ? gmt : "<unknown>"), tk->reason);
			}

			RunHook5(HOOKTYPE_TKL_DEL, cptr, sptr, tk, NULL, NULL); // Run hooks lol

			// If this is a global *:Line, let's pass it on to other servers, shall we? =]
			if(tk->type & TKL_GLOBAL) {
				rawnick = make_nick_user_host(sptr->name, sptr->user->username, GetHost(sptr)); // Unreal requires a "by" argument (like, who undid it)

				// Muh spamfilter format
				if(tk->type & TKL_SPAMF)
					sendto_server(&me, 0, 0, ":%s TKL - %c %s %c %s %li %li :%s", me.name, flag, spamfilter_target_inttostring(tk->subtype), banact_valtochar(tk->ptr.spamf->action), rawnick, (tk->expire_at != 0) ? (tk->expire_at - curtime) : 0, curtime - tk->set_at, tk->reason);

				else
					sendto_server(&me, 0, 0, ":%s TKL - %c %s %s %s", me.name, flag, tk->usermask, tk->hostmask, rawnick);
			}

			// And if it's a shun, remove it from this server
			if(tk->type & TKL_SHUN)
				tkl_check_local_remove_shun(tk);

			_my_tkl_del_line(tk); // Delete *:Line from this server
		}
	}

	return 0;
}
