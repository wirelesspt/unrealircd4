// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

#define MSG_CLONES "CLONES"

#define IsParam(x) (parc > (x) && !BadPtr(parv[(x)]))
#define IsNotParam(x) (parc <= (x) || BadPtr(parv[(x)]))

static int dumpit(aClient *sptr, char **p);
CMD_FUNC(m_clones);

static ModuleInfo *clonesMI = NULL;
Command *CmdClones;

ModuleHeader MOD_HEADER(m_clones) = {
	"m_clones",
	"$Id: v1.0 2017/01/19 Gottem$",
	"Adds a command /CLONES to list all users having the same IP address matching the given options",
	"3.2-b8-1",
	NULL
};

MOD_INIT(m_clones) {
	if(CommandExists(MSG_CLONES)) {
		config_error("Command %s already exists", MSG_CLONES);
		return MOD_FAILED;
	}

	CmdClones = CommandAdd(modinfo->handle, MSG_CLONES, m_clones, 3, M_USER);
	clonesMI = modinfo;
	return MOD_SUCCESS;
}

MOD_LOAD(m_clones) {
	if(ModuleGetError(clonesMI->handle) != MODERR_NOERROR || !CmdClones) {
		config_error("Error adding command %s: %s", MSG_CLONES, ModuleGetErrorStr(clonesMI->handle));
		return MOD_FAILED;
	}

	return MOD_SUCCESS;
}

MOD_UNLOAD(m_clones) {
	return MOD_SUCCESS;
}

// Dump a NULL-terminated array of strings to user sptr using the numeric rplnum, and then return 0 (taken from DarkFire IRCd)
static int dumpit(aClient *sptr, char **p) {
	if(IsServer(sptr)) // Bail out early and silently if it's a server =]
		return 0;

	for(; *p != NULL; p++)
		sendto_one(sptr, ":%s %03d %s :%s", me.name, RPL_TEXT, sptr->name, *p);

	sptr->local->since += 8; // Needs to read it for at least 8 seconds
	return 0;
}

static char *clones_halp[] = {
	"*** \002Help on /clones\002 ***",
	"Gives you a list of clones based on the specified options.",
	"Clones are listed by a nickname or by a minimal number of",
	"concurrent sessions connecting from the local or the given",
	"server.",
	" ",
	"Syntax:",
	"CLONES <\037min-num-of-sessions|nickname\037> [\037server\037]",
	" ",
	"Examples:",
	"  /clones 2",
	"    Lists local clones having two or more sessions",
	"  /clones Loser",
	"    Lists local clones of Loser",
	"  /clones 3 hub.test.com",
	"    Lists all clones with at least 3 sessions, which are",
	"    connecting from hub.test.com",
	NULL
};

CMD_FUNC(m_clones) {
	aClient *acptr, *acptr2;
	int i, j;
	u_int min, count, found = 0;

	if(!IsPerson(sptr))
		return -1;

	if(!IsOper(sptr)) {
		sendto_one(sptr, err_str(ERR_NOPRIVILEGES), me.name, sptr->name);
		return 0;
	}

	if(!IsParam(1))
		return dumpit(sptr, clones_halp);

	if(IsParam(2)) {
		if(hunt_server(cptr, sptr, ":%s CLONES %s %s", 2, parc, parv) != HUNTED_ISME)
			return 0;
	}

	if(isdigit(*parv[1])) {
		if((min = atoi(parv[1])) < 2) {
			sendto_one(sptr, ":%s NOTICE %s :*** Invalid minimal clone count number (%s)", me.name, MSG_CLONES, parv[1]);
			return 0;
		}

		list_for_each_entry(acptr, &client_list, client_node) {
			if(!IsPerson(acptr) || !acptr->local)
				continue;

			count = 0;

			list_for_each_entry(acptr2, &client_list, client_node) {
				if(!IsPerson(acptr2) || acptr == acptr2 || !acptr2->local)
					continue;

				if(!strcmp(acptr->ip, acptr2->ip))
					count++;
			}

			if(count >= min) {
				found++;
				sendto_one(sptr, ":%s NOTICE %s :%s (%s!%s@%s)", me.name, sptr->name, acptr->ip, acptr->name, acptr->user->username, acptr->user->realhost);
			}
		}
	}
	else {
		if(!(acptr = find_person(parv[1], NULL))) {
			sendto_one(sptr, err_str(ERR_NOSUCHNICK), me.name, MSG_CLONES, parv[1]);
			return 0;
		}

		if(!MyConnect(acptr)) {
			sendto_one(sptr, ":%s NOTICE %s :*** %s is not my client", me.name, MSG_CLONES, acptr->name);
			return 0;
		}

		found = 0;

		list_for_each_entry(acptr2, &client_list, client_node) {
			if(!IsPerson(acptr2) || acptr == acptr2 || !acptr2->local)
				continue;

			if(!strcmp(acptr->ip, acptr2->ip)) {
				found++;
				sendto_one(sptr, ":%s NOTICE %s :%s (%s!%s@%s)", me.name, sptr->name, acptr2->ip, acptr2->name, acptr2->user->username, acptr2->user->realhost);
			}
		}
	}

	sendto_one(sptr, ":%s NOTICE %s :%d clone%s found", me.name, sptr->name, found, (!found || found > 1) ? "s" : "");
	return 0;
}
