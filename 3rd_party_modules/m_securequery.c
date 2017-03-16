// One include for all cross-platform compatibility thangs
#include "unrealircd.h"

#define DelHook(x) if (x) HookDel(x); x = NULL

ModuleHeader MOD_HEADER(m_securequery) = {
	"m_securequery",
	"$Id: v1.0 2017/01/17 Gottem$",
	"Adds umode +Z to prevent you from messaging non-SSL users in either direction",
	"3.2-b8-1",
	NULL
};

int umode_allow_secure(aClient *sptr, int what);
char *securequery_checkmsg(aClient *sptr, aClient *to, char *text, int notice);

static long UMODE_SECUREQUERY = 0;
static Umode *UmodeSecureQuery = NULL;
Hook *CheckMsg;

MOD_INIT(m_securequery) {
	UmodeSecureQuery = UmodeAdd(modinfo->handle, 'Z', UMODE_GLOBAL, 0, umode_allow_secure, &UMODE_SECUREQUERY);
	if(!UmodeSecureQuery) {
		config_error("m_securequery: Could not add usermode 'Z': %s", ModuleGetErrorStr(modinfo->handle));
		return MOD_FAILED;
	}

	CheckMsg = HookAddPChar(modinfo->handle, HOOKTYPE_PRE_USERMSG, 0, securequery_checkmsg);
	//ModuleSetOptions(modinfo->handle, MOD_OPT_PERM); // May break shit, so commented out
	return MOD_SUCCESS;
}

MOD_LOAD(m_securequery) {
	return MOD_SUCCESS;
}

MOD_UNLOAD(m_securequery) {
	DelHook(CheckMsg);
	UmodeDel(UmodeSecureQuery);
	return MOD_SUCCESS;
}

int umode_allow_secure(aClient *sptr, int what) {
	if((MyClient(sptr)) && !(sptr->umodes & UMODE_SECURE)) {
		sendnotice(sptr,"You must be connected using SSL to set usermode +Z.");
		return 0;
	}

	return 1;
}

char *securequery_checkmsg(aClient *sptr, aClient *acptr, char *text, int notice) {
	if((acptr->umodes & UMODE_SECUREQUERY) && !IsULine(sptr) && !IsServer(sptr) && !(sptr->umodes & UMODE_SECURE)) {
		sendnotice(sptr, "Message to '%s' not delivered: User does not accept private messages or notices from non-SSL users. Please login to the server securely using SSL or TLS.", acptr->name);
		return NULL;
	}
	else if((sptr->umodes & UMODE_SECUREQUERY) && !IsULine(acptr) && !IsServer(acptr) && !(acptr->umodes & UMODE_SECURE)) {
		sendnotice(sptr, "Message to '%s' not delivered: For your security, your modes do not allow you to send private messages or notices to non-SSL users.", acptr->name);
		return NULL;
	}

	return text;
}
