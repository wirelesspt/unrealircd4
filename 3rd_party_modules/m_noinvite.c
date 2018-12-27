/* Copyright (C) All Rights Reserved
** Written by Gottem <support@gottem.nl>
** Website: https://gitgud.malvager.net/Wazakindjes/unrealircd_mods
** License: https://gitgud.malvager.net/Wazakindjes/unrealircd_mods/raw/master/LICENSE
*/

// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

#define BACKPORT (UNREAL_VERSION_GENERATION == 4 && UNREAL_VERSION_MAJOR == 0 && UNREAL_VERSION_MINOR <= 15)

#define UMODE_FLAG 'N' // No invite

// Commands to override
#define OVR_INVITE "INVITE"

// Quality fowod declarations
#if BACKPORT
	static int noinvite_override_invite(Cmdoverride *ovr, aClient *cptr, aClient *sptr, int parc, char *parv[]);
#else
	int noinvite_hook(aClient *sptr, aClient *target, aChannel *chptr, int *override);
#endif

// Muh globals
static ModuleInfo *noinviteMI = NULL; // Store ModuleInfo so we can use it to check for errors in MOD_LOAD
Umode *noinvite_umode = NULL; // Store umode handle
long noinvite_extumode = 0L; // Store bitwise value latur

#if BACKPORT
	Cmdoverride *noInvOVR; // Pointer to the override we're gonna add
#else
	Hook *noinviteHook = NULL;
#endif

// Dat dere module header
ModuleHeader MOD_HEADER(m_noinvite) = {
	"m_noinvite", // Module name
	"$Id: v1.02 2018/09/25 Gottem$", // Version
	"Adds umode +N to block invites", // Description
	"3.2-b8-1", // Modversion, not sure wat do
	NULL
};

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT(m_noinvite) {
	noinviteMI = modinfo;
	#if !BACKPORT
		noinviteHook = HookAdd(modinfo->handle, HOOKTYPE_PRE_INVITE, 0, noinvite_hook);
	#endif
	// Add a global umode (i.e. propagate to all servers), allow anyone to set/remove it on themselves
	noinvite_umode = UmodeAdd(modinfo->handle, UMODE_FLAG, UMODE_GLOBAL, 0, NULL, &noinvite_extumode);
	return MOD_SUCCESS; // Let MOD_LOAD handle errors
}

// Actually load the module here (also command overrides as they may not exist in MOD_INIT yet)
MOD_LOAD(m_noinvite) {
	#if BACKPORT
		noInvOVR = CmdoverrideAdd(noinviteMI->handle, OVR_INVITE, noinvite_override_invite); // Attempt to add command override
		if(ModuleGetError(noinviteMI->handle) != MODERR_NOERROR || !noinvite_umode || !noInvOVR)
	#else
		if(ModuleGetError(noinviteMI->handle) != MODERR_NOERROR || !noinvite_umode || !noinviteHook)
	#endif
	{
		// Display error string kek
		config_error("A critical error occurred when loading module %s: %s", MOD_HEADER(m_noinvite).name, ModuleGetErrorStr(noinviteMI->handle));
		return MOD_FAILED; // No good
	}

	return MOD_SUCCESS; // We good
}

// Called on unload/rehash obv
MOD_UNLOAD(m_noinvite) {
	return MOD_SUCCESS; // We good
}

// Now for the actual override
#if BACKPORT
	static int noinvite_override_invite(Cmdoverride *ovr, aClient *cptr, aClient *sptr, int parc, char *parv[]) {
		aClient *acptr;
		if(!IsOper(sptr) && !BadPtr(parv[1]) && (acptr = find_person(parv[1], NULL)) && (acptr->umodes & noinvite_extumode)) {
			if(!BadPtr(parv[2]) && find_channel(parv[2], NULL)) {
				sendto_one(sptr, ":%s NOTICE %s :%s has blocked all invites", me.name, parv[2], parv[1]);
				return 0;
			}
		}
		return CallCmdoverride(ovr, cptr, sptr, parc, parv); // Run original function yo
	}
#else
	int noinvite_hook(aClient *sptr, aClient *target, aChannel *chptr, int *override) {
		if(!IsULine(sptr) && !IsOper(sptr) && (target->umodes & noinvite_extumode)) {
			sendto_one(sptr, ":%s NOTICE %s :%s has blocked all invites", me.name, chptr->chname, target->name);
			return HOOK_DENY;
		}
		return HOOK_CONTINUE;
	}
#endif
