/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gitgud.malvager.net/Wazakindjes/unrealircd_mods
** License: https://gitgud.malvager.net/Wazakindjes/unrealircd_mods/raw/master/LICENSE
*/

#include "unrealircd.h"

#define MSG_GETLEGITUSERS "GETLEGITUSERS" // Actual command name

CMD_FUNC(m_getlegitusers); // Register that shit

ModuleInfo *ModGetLegitUsers; // Store module info so we can check for errors later
Command	*CmdGetLegitUsers; // Store command pointer

// Quality module header yo
ModuleHeader MOD_HEADER(m_getlegitusers) = {
	"m_getlegitusers",
	"$Id: v1.0 2016/09/11 Gottem$",
	"Command /getlegitusers to show user/bot count across the network",
	"3.2-b8-1",
	NULL
};

// Module initialisation
MOD_INIT(m_getlegitusers) {
	// If command already exists for some reason, bail out
	if(CommandExists(MSG_GETLEGITUSERS)) {
		config_error("Command %s already exists", MSG_GETLEGITUSERS);
		return MOD_FAILED;
	}

	// Add command, has no params, usable by users only
	CmdGetLegitUsers = CommandAdd(modinfo->handle, MSG_GETLEGITUSERS, m_getlegitusers, 1, M_USER);
	ModGetLegitUsers = modinfo; // Store module info yo
	return MOD_SUCCESS; // Let MOD_LOAD handle errors
}

MOD_LOAD(m_getlegitusers) {
	if(ModuleGetError(ModGetLegitUsers->handle) != MODERR_NOERROR || !CmdGetLegitUsers) {
		// Display error string kek
		config_error("A critical error occurred when loading module %s: %s", MOD_HEADER(m_getlegitusers).name, ModuleGetErrorStr(ModGetLegitUsers->handle));
		return MOD_FAILED; // No good
	}

	return MOD_SUCCESS; // We good fam
}

// Called on mod unload
MOD_UNLOAD(m_getlegitusers) {
	return MOD_SUCCESS;
}

CMD_FUNC(m_getlegitusers) {
	long total, legit, bots; // Just some counters lol
	aClient *tmpcptr; // Client pointer for the iteration of the client list

	if(!IsPerson(sptr) || !IsOper(sptr)) { // Is the thing executing the command even a user and opered up?
		sendto_one(sptr, err_str(ERR_NOPRIVILEGES), me.name, MSG_GETLEGITUSERS); // Lolnope
		return -1;
	}

	bots = total = legit = 0; // Initialise dem counters

	// Iterate over all known clients
	list_for_each_entry(tmpcptr, &client_list, client_node) {
		if(tmpcptr->user) { // Sanity check
			total++; // Increment total count since this IS a user
			if(tmpcptr->user->joined > 0) // But are they joined to more than one chan ?
				legit++; // Increment legitimate user count
			else {
				if(strncasecmp(tmpcptr->user->server, "services", 8) == 0) // Does the user's local server name start with "services"?
					bots++; // Surely this must be a bot then
				else
					// Seems to be an unknown user, send notice to executing oper
					sendnotice(sptr, "*** [%s] Found unknown user %s!%s@%s",
						MSG_GETLEGITUSERS, tmpcptr->name, tmpcptr->user->username, tmpcptr->user->realhost);
			}
		}
	}

	// Server notice to the executing oper
	sendnotice(sptr, "*** [%s] %ld clients are on at least one channel and %ld are not present on any channel. The other %ld are services agents.",
		MSG_GETLEGITUSERS, legit, total-(legit+bots), bots);

	return 0; // All good
}
