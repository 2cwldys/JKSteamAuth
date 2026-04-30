/*
 * Steam Authentication Support for Jedi Academy Multiplayer
 * Copyright (C) 2025 2cwldys
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 *
 * ---- Steamworks SDK ----
 * This file optionally loads steam_api.dll (Windows) or steam_api.so (Linux)
 * at runtime to fetch the live Steam identity of the local player.
 * steam_api is the property of Valve Corporation and is governed by the
 * Steamworks SDK license agreement (https://partner.steamgames.com/).
 * It is NOT required — when absent the code falls back to loginusers.vdf.
 * No Steamworks SDK headers or libraries are distributed with this file.
 */

/*
 * ui_steamload.c — Steam identity fetch + UI helpers for the client module.
 *
 * Fetches the local player's Steam64 ID and persona name, then exposes them
 * as CVAR_USERINFO CVars so the engine automatically includes them in the
 * userinfo string sent to every server on connect and on CVar change.
 *
 * Two-stage identity fetch:
 *   1. Registry + loginusers.vdf  — fast, no DLL, works even if Steam is not
 *      currently running (uses the last-logged-in account).
 *   2. Live steam_api.dll query   — overrides stage 1 with the account that
 *      is actually signed in right now.  Falls back silently if the DLL is
 *      absent or if SteamAPI_Init fails.
 *
 * Integration points (see ui_main.c reference for paste locations):
 *   UI_LoadSteamAPI()              — call once from UI_Init
 *   UI_Steam_NameEnforcement()     — call every UI frame (self-rate-limits)
 *   UI_Steam_GetServerIcon()       — call in server browser row draw
 */

#include "ui_local.h"

/* =========================================================================
 * Platform-independent section
 * All functions below compile on both Windows and Linux.
 * ========================================================================= */

static qhandle_t s_steamIcon;

#define SF_STEAMID (1 << 5)

/* -------------------------------------------------------------------------
 * UI_Steam_Init
 * Register the server browser icon shader.  Called automatically by
 * UI_LoadSteamAPI — no need to call this directly.
 * ------------------------------------------------------------------------- */
void UI_Steam_Init(void)
{
	s_steamIcon = trap_R_RegisterShaderNoMip("gfx/menus/steamlogo");
}

/* -------------------------------------------------------------------------
 * UI_Steam_IsServerEnforcing
 * Returns qtrue when the connected server has g_steamverify set.
 * g_steamverify must be registered server-side with CVAR_SERVERINFO so it
 * appears in the serverinfo configstring (see g_xcvar.h).
 * ------------------------------------------------------------------------- */
qboolean UI_Steam_IsServerEnforcing(void)
{
	char info[MAX_INFO_STRING];
	int  serverFeatures;

	trap_GetConfigString(CS_SERVERINFO, info, sizeof(info));

	serverFeatures = atoi(Info_ValueForKey(info, "g_jediVmerc"));
	if (serverFeatures & SF_STEAMID)
		return qtrue;

	return (atoi(Info_ValueForKey(info, "g_steamverify")) != 0);
}

/* -------------------------------------------------------------------------
 * UI_Steam_NameEnforcement
 * While connected to a server with g_steamverify active, overwrite the
 * player's `name` CVar with their Steam persona name every 5 seconds to
 * prevent manual renames.  Safe to call every UI frame.
 * ------------------------------------------------------------------------- */
void UI_Steam_NameEnforcement(int realtime)
{
	static int nextCheck = 0;
	char steamName[128];
	char currentName[128];

	if (realtime < nextCheck)
		return;
	nextCheck = realtime + 5000;

	if (!UI_Steam_IsServerEnforcing())
		return;

	trap_Cvar_VariableStringBuffer("cl_steamname", steamName, sizeof(steamName));
	if (!steamName[0])
		return;

	trap_Cvar_VariableStringBuffer("name", currentName, sizeof(currentName));
	if (strcmp(currentName, steamName) != 0)
		trap_Cvar_Set("name", steamName);
}

/* -------------------------------------------------------------------------
 * UI_Steam_GetServerIcon
 * Returns the Steam logo shader handle if the server at (source, serverIndex)
 * in the LAN server list has g_steamverify set; returns 0 otherwise.
 *
 * Usage in server browser row draw:
 *   qhandle_t icon = UI_Steam_GetServerIcon(ui_netSource.integer, serverIndex);
 *   if (icon) DrawPicture(x, y, w, h, icon);
 *
 * `source` is typically ui_netSource.integer (LAN/internet/favorites).
 * `serverIndex` is the raw display index into the LAN server list.
 * ------------------------------------------------------------------------- */
qhandle_t UI_Steam_GetServerIcon(int source, int serverIndex)
{
	char info[MAX_INFO_STRING];
	int  serverFeatures;

	trap_LAN_GetServerInfo(source, serverIndex, info, sizeof(info));

	serverFeatures = atoi(Info_ValueForKey(info, "g_jediVmerc"));
	if (serverFeatures & SF_STEAMID)
		return s_steamIcon;

	if (atoi(Info_ValueForKey(info, "g_steamverify")))
		return s_steamIcon;

	return 0;
}

/* =========================================================================
 * Windows platform section
 * ========================================================================= */

#ifdef _WIN32
#include <windows.h>

static HINSTANCE steamLib;

typedef void     (__cdecl *STEAM_FN)(void);
typedef qboolean (__cdecl *STEAM_FN_BOOL)(void);

static STEAM_FN_BOOL SteamAPI_Init;
static STEAM_FN      SteamAPI_Shutdown;

static vmCvar_t cl_steamid_cvar;
static vmCvar_t cl_steamname_cvar;

/* -------------------------------------------------------------------------
 * ReadPersonaName
 * Parse <SteamPath>/config/loginusers.vdf for the PersonaName that belongs
 * to steamID64.  Writes an empty string on any failure.
 * ------------------------------------------------------------------------- */
static void ReadPersonaName(unsigned __int64 steamID64, char *outName, int outSize)
{
	HKEY     hKey;
	DWORD    len;
	char     steamPath[MAX_OSPATH];
	char     vdfPath[MAX_OSPATH];
	char     idStr[21];
	FILE    *f;
	char     line[512];
	int      depth = 0;
	qboolean inUserBlock = qfalse;

	outName[0] = '\0';

	if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Valve\\Steam",
	                  0, KEY_READ, &hKey) != ERROR_SUCCESS)
		return;

	len = sizeof(steamPath);
	if (RegQueryValueExA(hKey, "SteamPath", NULL, NULL,
	                     (LPBYTE)steamPath, &len) != ERROR_SUCCESS)
		steamPath[0] = '\0';
	RegCloseKey(hKey);

	if (!steamPath[0]) return;

	Com_sprintf(idStr,   sizeof(idStr),   "%I64u",     steamID64);
	Com_sprintf(vdfPath, sizeof(vdfPath), "%s/config/loginusers.vdf", steamPath);

	f = fopen(vdfPath, "r");
	if (!f) return;

	while (fgets(line, sizeof(line), f))
	{
		char *p = line;
		{
			int ll = strlen(line);
			while (ll > 0 && (line[ll-1] == '\r' || line[ll-1] == '\n')) line[--ll] = '\0';
		}
		while (*p == ' ' || *p == '\t') p++;

		if (*p == '{') { depth++; continue; }
		if (*p == '}')
		{
			if (inUserBlock && depth == 2) inUserBlock = qfalse;
			depth--;
			continue;
		}

		if (!inUserBlock && depth == 1 && strstr(p, idStr))
		{
			inUserBlock = qtrue;
			continue;
		}

		if (inUserBlock && depth == 2 && strstr(p, "PersonaName"))
		{
			char *q = strchr(p, '"');
			if (q) q = strchr(q + 1, '"');
			if (q) q = strchr(q + 1, '"');
			if (q)
			{
				char *start = q + 1;
				char *end   = strchr(start, '"');
				if (end)
				{
					int n = (int)(end - start);
					if (n >= outSize) n = outSize - 1;
					strncpy(outName, start, n);
					outName[n] = '\0';
				}
			}
			break;
		}
	}
	fclose(f);
}

/* -------------------------------------------------------------------------
 * UI_FetchSteamIdentity  (Windows)
 * Stage 1: read accountID from the registry ActiveProcess key, derive the
 * Steam64 ID, then parse loginusers.vdf for the persona name.
 * Registers cl_steamid / cl_steamname as CVAR_USERINFO so changes propagate
 * to the server automatically.
 * ------------------------------------------------------------------------- */
static void UI_FetchSteamIdentity(void)
{
	HKEY             hKey;
	DWORD            accountID = 0;
	DWORD            len;
	unsigned __int64 steamID64;
	char             personaName[128];
	char             steamIDStr[21];

	trap_Cvar_Register(&cl_steamid_cvar,   "cl_steamid",   "", CVAR_USERINFO);
	trap_Cvar_Register(&cl_steamname_cvar, "cl_steamname", "", CVAR_USERINFO);

	personaName[0] = '\0';

	if (RegOpenKeyExA(HKEY_CURRENT_USER,
	                  "Software\\Valve\\Steam\\ActiveProcess",
	                  0, KEY_READ, &hKey) != ERROR_SUCCESS)
	{
		Com_Printf("^3Steam: registry key not found -- is Steam installed?\n");
		return;
	}

	len = sizeof(accountID);
	RegQueryValueExA(hKey, "ActiveUser", NULL, NULL, (LPBYTE)&accountID, &len);
	RegCloseKey(hKey);

	if (accountID == 0)
	{
		Com_Printf("^3Steam: not logged in (ActiveUser = 0).\n");
		return;
	}

	steamID64 = 76561197960265728ULL + (unsigned __int64)accountID;
	ReadPersonaName(steamID64, personaName, sizeof(personaName));

	Com_sprintf(steamIDStr, sizeof(steamIDStr), "%I64u", steamID64);
	trap_Cvar_Set("cl_steamid", steamIDStr);

	if (personaName[0])
		trap_Cvar_Set("cl_steamname", personaName);

	Com_Printf("Steam: registry identity: %s (%s)\n",
		personaName[0] ? personaName : "(no name)", steamIDStr);
}

/* -------------------------------------------------------------------------
 * UI_FetchLiveSteamIdentity  (Windows)
 * Stage 2: query the already-loaded steam_api.dll for the live account.
 * Tries both the plain exports and versioned fallbacks for newer SDK builds.
 * Overwrites the stage-1 values on success; leaves them untouched on failure.
 * ------------------------------------------------------------------------- */
static void UI_FetchLiveSteamIdentity(void)
{
	typedef void*            (__cdecl *pfnGetIface_t)(void);
	typedef unsigned __int64 (__cdecl *pfnGetSteamID64_t)(void *pUser);
	typedef const char*      (__cdecl *pfnGetPersonaName_t)(void *pFriends);

	pfnGetIface_t       fnSteamUser, fnSteamFriends;
	pfnGetSteamID64_t   fnGetID;
	pfnGetPersonaName_t fnGetName;
	void               *pUser, *pFriends;
	unsigned __int64    steamID64;
	const char         *name;
	char                idStr[21];

	fnSteamUser    = (pfnGetIface_t)      GetProcAddress(steamLib, "SteamUser");
	fnSteamFriends = (pfnGetIface_t)      GetProcAddress(steamLib, "SteamFriends");
	fnGetID        = (pfnGetSteamID64_t)  GetProcAddress(steamLib, "SteamAPI_ISteamUser_GetSteamID");
	fnGetName      = (pfnGetPersonaName_t)GetProcAddress(steamLib, "SteamAPI_ISteamFriends_GetPersonaName");

	/* Newer SDK builds replaced plain "SteamUser"/"SteamFriends" with versioned exports. */
	if (!fnSteamUser)
	{
		static const char * const s_userExports[] = {
			"SteamAPI_SteamUser_v023", "SteamAPI_SteamUser_v022", "SteamAPI_SteamUser_v021", NULL
		};
		int ei;
		for (ei = 0; s_userExports[ei]; ei++)
		{
			fnSteamUser = (pfnGetIface_t)GetProcAddress(steamLib, s_userExports[ei]);
			if (fnSteamUser) { Com_Printf("Steam: using %s\n", s_userExports[ei]); break; }
		}
	}
	if (!fnSteamFriends)
	{
		static const char * const s_friendsExports[] = {
			"SteamAPI_SteamFriends_v017", "SteamAPI_SteamFriends_v016", "SteamAPI_SteamFriends_v015", NULL
		};
		int ei;
		for (ei = 0; s_friendsExports[ei]; ei++)
		{
			fnSteamFriends = (pfnGetIface_t)GetProcAddress(steamLib, s_friendsExports[ei]);
			if (fnSteamFriends) { Com_Printf("Steam: using %s\n", s_friendsExports[ei]); break; }
		}
	}

	if (!fnSteamUser || !fnSteamFriends || !fnGetID || !fnGetName)
	{
		Com_Printf("^3Steam: live API exports not found -- keeping registry/vdf identity.\n");
		return;
	}

	pUser    = fnSteamUser();
	pFriends = fnSteamFriends();

	if (!pUser || !pFriends)
	{
		Com_Printf("^3Steam: SteamUser/SteamFriends returned NULL -- keeping registry/vdf identity.\n");
		return;
	}

	steamID64 = fnGetID(pUser);
	if (!steamID64)
	{
		Com_Printf("^3Steam: live GetSteamID returned 0 -- keeping registry/vdf identity.\n");
		return;
	}

	name = fnGetName(pFriends);

	Com_sprintf(idStr, sizeof(idStr), "%I64u", steamID64);
	trap_Cvar_Set("cl_steamid", idStr);

	if (name && name[0])
		trap_Cvar_Set("cl_steamname", name);

	Com_Printf("^2Steam live identity: %s (%s)\n",
		(name && name[0]) ? name : "(no name)", idStr);
}

/* -------------------------------------------------------------------------
 * UI_LoadSteamAPI  (Windows)
 * Call once from UI_Init.  Registers the browser icon shader, fetches Steam
 * identity, then immediately shuts down and frees the DLL.
 * Returns qtrue if the live identity was successfully fetched.
 * ------------------------------------------------------------------------- */
qboolean UI_LoadSteamAPI(void)
{
	qboolean success = qfalse;
	char     basePath[MAX_OSPATH];
	char     dllPath[MAX_OSPATH];

	UI_Steam_Init();

	/* Stage 1: fast registry + vdf fallback (no DLL required). */
	UI_FetchSteamIdentity();

	/* Stage 2: try to load steam_api.dll for the live account. */
	trap_Cvar_VariableStringBuffer("fs_basepath", basePath, sizeof(basePath));
	Com_sprintf(dllPath, sizeof(dllPath), "%s\\steam_api.dll", basePath);
	steamLib = LoadLibraryA(dllPath);

	if (!steamLib)
	{
		Com_Printf("^3Steam: steam_api.dll not found at %s -- using registry/vdf identity.\n", dllPath);
		return qfalse;
	}

	SteamAPI_Init     = (STEAM_FN_BOOL)GetProcAddress(steamLib, "SteamAPI_Init");
	SteamAPI_Shutdown = (STEAM_FN)     GetProcAddress(steamLib, "SteamAPI_Shutdown");

	if (SteamAPI_Init && SteamAPI_Shutdown && SteamAPI_Init())
	{
		UI_FetchLiveSteamIdentity();
		SteamAPI_Shutdown();
		success = qtrue;
	}
	else
	{
		Com_Printf("^3Steam: SteamAPI_Init failed -- using registry/vdf identity.\n");
	}

	FreeLibrary(steamLib);
	steamLib          = NULL;
	SteamAPI_Init     = NULL;
	SteamAPI_Shutdown = NULL;

	return success;
}

qboolean UI_UnloadSteamAPI(void) { return qtrue; }

#else /* Linux */

#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>

static vmCvar_t cl_steamid_cvar;
static vmCvar_t cl_steamname_cvar;

/* -------------------------------------------------------------------------
 * UI_FetchSteamIdentity  (Linux)
 * Reads loginusers.vdf from standard Steam install paths.
 * The MostRecent=1 field identifies the last-logged-in account.
 * ------------------------------------------------------------------------- */
static void UI_FetchSteamIdentity(void)
{
	const char *home = getenv("HOME");
	const char *candidates[] = {
		"%s/.steam/steam/config/loginusers.vdf",
		"%s/.local/share/Steam/config/loginusers.vdf",
		NULL
	};
	char     vdfPath[MAX_OSPATH];
	FILE    *f = NULL;
	char     line[512];
	int      depth = 0;
	int      i;

	unsigned long long currentID   = 0;
	unsigned long long foundID     = 0;
	char               currentName[128];
	char               foundName[128];
	qboolean           isMostRecent = qfalse;
	char               steamIDStr[21];

	trap_Cvar_Register(&cl_steamid_cvar,   "cl_steamid",   "", CVAR_USERINFO);
	trap_Cvar_Register(&cl_steamname_cvar, "cl_steamname", "", CVAR_USERINFO);

	currentName[0] = '\0';
	foundName[0]   = '\0';

	if (!home) { Com_Printf("^3Steam: $HOME not set.\n"); return; }

	for (i = 0; candidates[i]; i++)
	{
		Com_sprintf(vdfPath, sizeof(vdfPath), candidates[i], home);
		f = fopen(vdfPath, "r");
		if (f) break;
	}

	if (!f) { Com_Printf("^3Steam: loginusers.vdf not found.\n"); return; }

	while (fgets(line, sizeof(line), f))
	{
		int   ll = (int)strlen(line);
		char *p;
		while (ll > 0 && (line[ll-1] == '\r' || line[ll-1] == '\n')) line[--ll] = '\0';
		p = line;
		while (*p == ' ' || *p == '\t') p++;

		if (*p == '{')
		{
			depth++;
			if (depth == 2) { currentName[0] = '\0'; isMostRecent = qfalse; }
			continue;
		}
		if (*p == '}')
		{
			if (depth == 2 && isMostRecent)
			{
				foundID = currentID;
				Q_strncpyz(foundName, currentName, sizeof(foundName));
				break;
			}
			if (depth == 2) currentID = 0;
			depth--;
			continue;
		}
		if (depth == 1 && *p == '"')
		{
			char *start = p + 1, *end = strchr(start, '"');
			if (end)
			{
				char buf[21];
				int  n = (int)(end - start);
				if (n > 0 && n < (int)sizeof(buf))
				{
					strncpy(buf, start, n);
					buf[n] = '\0';
					currentID = (unsigned long long)atoll(buf);
				}
			}
		}
		if (depth == 2)
		{
			if (strstr(p, "PersonaName"))
			{
				char *q = strchr(p, '"');
				if (q) q = strchr(q + 1, '"');
				if (q) q = strchr(q + 1, '"');
				if (q)
				{
					char *s = q + 1, *e = strchr(s, '"');
					if (e)
					{
						int n = (int)(e - s);
						if (n >= (int)sizeof(currentName)) n = (int)sizeof(currentName) - 1;
						strncpy(currentName, s, n);
						currentName[n] = '\0';
					}
				}
			}
			if (strstr(p, "MostRecent") && strstr(p, "\"1\""))
				isMostRecent = qtrue;
		}
	}
	fclose(f);

	if (!foundID) { Com_Printf("^3Steam: no MostRecent user in loginusers.vdf.\n"); return; }

	Com_sprintf(steamIDStr, sizeof(steamIDStr), "%llu", foundID);
	trap_Cvar_Set("cl_steamid", steamIDStr);
	if (foundName[0]) trap_Cvar_Set("cl_steamname", foundName);

	Com_Printf("Steam: vdf identity: %s (%s)\n",
		foundName[0] ? foundName : "(no name)", steamIDStr);
}

/* -------------------------------------------------------------------------
 * UI_LoadSteamAPI  (Linux)
 * Call once from UI_Init.  Registers the browser icon shader, fetches Steam
 * identity via loginusers.vdf, then tries steam_api.so for the live account.
 * Returns qtrue if the live identity was successfully fetched.
 * ------------------------------------------------------------------------- */
qboolean UI_LoadSteamAPI(void)
{
	typedef qboolean        (*pfnInit_t)(void);
	typedef void            (*pfnShutdown_t)(void);
	typedef void*           (*pfnGetIface_t)(void);
	typedef unsigned long long (*pfnGetSteamID64_t)(void *pUser);
	typedef const char*     (*pfnGetPersonaName_t)(void *pFriends);

	void               *lib = NULL;
	pfnInit_t           fnInit;
	pfnShutdown_t       fnShutdown;
	pfnGetIface_t       fnSteamUser, fnSteamFriends;
	pfnGetSteamID64_t   fnGetID;
	pfnGetPersonaName_t fnGetName;
	void               *pUser, *pFriends;
	unsigned long long  steamID64;
	const char         *name;
	char                idStr[21];
	char                basePath[MAX_OSPATH];
	char                libPath[MAX_OSPATH];
	qboolean            success = qfalse;

	UI_Steam_Init();
	UI_FetchSteamIdentity();

	trap_Cvar_VariableStringBuffer("fs_basepath", basePath, sizeof(basePath));
	if (basePath[0])
	{
		Com_sprintf(libPath, sizeof(libPath), "%s/steam_api.so", basePath);
		lib = dlopen(libPath, RTLD_LAZY | RTLD_LOCAL);
	}
	if (!lib) lib = dlopen("steam_api.so", RTLD_LAZY | RTLD_LOCAL);
	if (!lib)
	{
		Com_Printf("^3Steam: steam_api.so not found -- using vdf identity.\n");
		return qfalse;
	}

	fnInit         = (pfnInit_t)          dlsym(lib, "SteamAPI_Init");
	fnShutdown     = (pfnShutdown_t)      dlsym(lib, "SteamAPI_Shutdown");
	fnSteamUser    = (pfnGetIface_t)      dlsym(lib, "SteamUser");
	fnSteamFriends = (pfnGetIface_t)      dlsym(lib, "SteamFriends");
	fnGetID        = (pfnGetSteamID64_t)  dlsym(lib, "SteamAPI_ISteamUser_GetSteamID");
	fnGetName      = (pfnGetPersonaName_t)dlsym(lib, "SteamAPI_ISteamFriends_GetPersonaName");

	if (!fnInit || !fnShutdown || !fnSteamUser || !fnSteamFriends || !fnGetID || !fnGetName)
	{
		Com_Printf("^3Steam: required exports missing from steam_api.so -- using vdf identity.\n");
		dlclose(lib);
		return qfalse;
	}

	if (!fnInit())
	{
		Com_Printf("^3Steam: SteamAPI_Init failed -- using vdf identity.\n");
		dlclose(lib);
		return qfalse;
	}

	pUser = fnSteamUser(); pFriends = fnSteamFriends();
	if (pUser && pFriends)
	{
		steamID64 = fnGetID(pUser);
		name      = fnGetName(pFriends);
		if (steamID64)
		{
			Com_sprintf(idStr, sizeof(idStr), "%llu", steamID64);
			trap_Cvar_Set("cl_steamid", idStr);
			if (name && name[0]) { trap_Cvar_Set("cl_steamname", name); }
			Com_Printf("^2Steam live identity: %s (%s)\n",
				(name && name[0]) ? name : "(no name)", idStr);
			success = qtrue;
		}
		else Com_Printf("^3Steam: live GetSteamID returned 0 -- using vdf identity.\n");
	}
	else Com_Printf("^3Steam: SteamUser/SteamFriends returned NULL -- using vdf identity.\n");

	fnShutdown();
	dlclose(lib);
	return success;
}

qboolean UI_UnloadSteamAPI(void) { return qtrue; }

#endif /* !_WIN32 */
