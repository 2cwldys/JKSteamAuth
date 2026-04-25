/*
 * Steam Authentication Support for Jedi Academy Multiplayer
 * Copyright (C) 2025 2cwldys
 * GNU General Public License v2 (or later) — see <https://www.gnu.org/licenses/>.
 */

#ifdef XCVAR_DEF

XCVAR_DEF(g_steamverify,          "g_steamverify",          "0", CVAR_ARCHIVE | CVAR_SERVERINFO, 0, qfalse)
XCVAR_DEF(g_steam_forcename,      "g_steam_forcename",      "0", CVAR_ARCHIVE,                   0, qfalse)
XCVAR_DEF(g_steam_illegalclients, "g_steam_illegalclients", "0", CVAR_ARCHIVE,                   0, qfalse)

#else

vmCvar_t g_steamverify;
vmCvar_t g_steam_forcename;
vmCvar_t g_steam_illegalclients;

static void G_RegisterSteamCvars(void)
{
	trap_Cvar_Register(&g_steamverify,          "g_steamverify",          "0", CVAR_ARCHIVE | CVAR_SERVERINFO);
	trap_Cvar_Register(&g_steam_forcename,      "g_steam_forcename",      "0", CVAR_ARCHIVE);
	trap_Cvar_Register(&g_steam_illegalclients, "g_steam_illegalclients", "0", CVAR_ARCHIVE);
}

static void G_UpdateSteamCvars(void)
{
	trap_Cvar_Update(&g_steamverify);
	trap_Cvar_Update(&g_steam_forcename);
	trap_Cvar_Update(&g_steam_illegalclients);
}

#endif
