// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

#define UMODE_FLAG 'N' // No invite

// Commands to override
#define OVR_INVITE "INVITE"

// Muh macros
#define DelOverride(cmd, ovr) if(ovr && CommandExists(cmd)) CmdoverrideDel(ovr); ovr = NULL; // Unregister override

// Quality fowod declarations
static int noinvite_override_invite(Cmdoverride *ovr, aClient *cptr, aClient *sptr, int parc, char *parv[]);

// Muh globals
static ModuleInfo *noinviteMI = NULL; // Store ModuleInfo so we can use it to check for errors in MOD_LOAD
Umode *noinvite_umode = NULL; // Store umode handle
long noinvite_extumode = 0L; // Store bitwise value latur
Cmdoverride *noInvOVR; // Pointer to the override we're gonna add

// Dat dere module header
ModuleHeader MOD_HEADER(m_noinvite) = {
	"m_noinvite", // Module name
	"$Id: v1.0 2017/03/11 Gottem$", // Version
	"Adds umode +N to block invites", // Description
	"3.2-b8-1", // Modversion, not sure wat do
	NULL
};

// Initialisation routine (register hooks, commands and modes or create structs etc)
MOD_INIT(m_noinvite) {
	noinviteMI = modinfo;
	// Add a global umode (i.e. propagate to all servers), allow anyone to set/remove it on themselves
	noinvite_umode = UmodeAdd(modinfo->handle, UMODE_FLAG, UMODE_GLOBAL, 0, NULL, &noinvite_extumode);
	return MOD_SUCCESS; // Let MOD_LOAD handle errors
}

// Actually load the module here (also command overrides as they may not exist in MOD_INIT yet)
MOD_LOAD(m_noinvite) {
	noInvOVR = CmdoverrideAdd(noinviteMI->handle, OVR_INVITE, noinvite_override_invite); // Attempt to add command override

	// Did the module throw an error during initialisation?
	if(ModuleGetError(noinviteMI->handle) != MODERR_NOERROR || !noinvite_umode || !noInvOVR) {
		// Display error string kek
		config_error("A critical error occurred when loading module %s: %s", MOD_HEADER(m_noinvite).name, ModuleGetErrorStr(noinviteMI->handle));
		return MOD_FAILED; // No good
	}

	return MOD_SUCCESS; // We good
}

// Called on unload/rehash obv
MOD_UNLOAD(m_noinvite) {
	// Attempt to free overrides
	DelOverride(OVR_INVITE, noInvOVR);
	return MOD_SUCCESS; // We good
}

// Now for the actual override
static int noinvite_override_invite(Cmdoverride *ovr, aClient *cptr, aClient *sptr, int parc, char *parv[]) {
	aClient *acptr;
	if(!BadPtr(parv[1]) && (acptr = find_person(parv[1], NULL)) && (acptr->umodes & noinvite_extumode)) {
		if(!BadPtr(parv[2]) && find_channel(parv[2], NULL))
			sendto_one(sptr, ":%s NOTICE %s :%s has blocked all invites", me.name, parv[2], parv[1]);
		return 0;
	}
	return CallCmdoverride(ovr, cptr, sptr, parc, parv); // Run original function yo
}
