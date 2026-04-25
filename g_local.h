/*
 * Steam Authentication Support for Jedi Academy Multiplayer
 * Copyright (C) 2025 2cwldys
 * GNU General Public License v2 (or later) — see <https://www.gnu.org/licenses/>.
 */

#ifndef STEAM_G_LOCAL_H
#define STEAM_G_LOCAL_H

// Add this field to clientSession_t in your mod's g_local.h:
//   char steamID[21];

// Ban engine (g_svcmds.c)
void        G_ProcessSteamIDBans(void);
qboolean    G_CheckSteamIDBan(const char *steamID);
void        AddSteamIDBan(const char *steamID);
void        RemoveSteamIDBan(const char *steamID);
int         G_SteamIDBanCount(void);
const char *G_SteamIDBanEntry(int i);

// Utility (g_cmds.c)
int G_ClientNumberFromSteamID(const char *steamIDStr);

// Client command handlers (g_cmds.c)
void Cmd_SBan_f(gentity_t *ent);
void Cmd_SUnban_f(gentity_t *ent);
void Cmd_SBanList_f(gentity_t *ent);

// Server console command handlers (g_svcmds.c)
void Svcmd_SteamBan_f(void);
void Svcmd_SteamUnban_f(void);
void Svcmd_SteamBanList_f(void);

// Hook functions (g_client.c)
qboolean    Steam_OnUserinfoChanged(int clientNum, gentity_t *ent, gclient_t *client, char *userinfo);
const char *Steam_OnClientConnect(int clientNum, qboolean firstTime, qboolean isBot, gentity_t *ent, const char *userinfo);
void        Steam_OnClientBegin(int clientNum, qboolean firstBegin, gentity_t *ent, gclient_t *client);
void        Steam_OnClientDisconnect(int clientNum, gentity_t *ent);

// g_main.c
void G_InitSteam(void);
void G_Steam_SetServerFeatures(int *serverFeatures);

// CVars
extern vmCvar_t g_steamverify;
extern vmCvar_t g_steam_forcename;
extern vmCvar_t g_steam_illegalclients;

#endif // STEAM_G_LOCAL_H
