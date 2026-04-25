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
 */

/*
 * g_svcmds.c — Steam ID ban engine + server console commands.
 *
 * Integration: paste into your g_svcmds.c which already includes g_local.h.
 * If reviewing standalone, add: #include "g_local.h"
 *
 * All public functions are self-contained.  Drop this block into your
 * g_svcmds.c and wire the three Svcmd_* handlers into ConsoleCommand()
 * (see wiring guide at the bottom of this file).
 *
 * Ban data is persisted to <fs_game>/banSteamID.dat in the same space-
 * delimited format used by the stock banIP.dat.
 *
 * Capacity: MAX_STEAM_ID_BANS entries (default 512).
 */

#define MAX_STEAM_ID_BANS   512
#define STEAMID_BAN_FILE    "banSteamID.dat"
#define STEAMID_BAN_VERSION "bansteamid_v1 "

static char steamIDBans[MAX_STEAM_ID_BANS][21];
static int  numSteamIDBans;

/* Returns qtrue if steamID is present in the ban list. */
qboolean G_CheckSteamIDBan(const char *steamID)
{
	int i;
	if (!steamID || !steamID[0]) return qfalse;
	for (i = 0; i < numSteamIDBans; i++)
		if (!strcmp(steamIDBans[i], steamID)) return qtrue;
	return qfalse;
}

static void UpdateSteamIDBans(void)
{
	fileHandle_t f;
	char         entry[22];
	int          i;

	trap_FS_FOpenFile(STEAMID_BAN_FILE, &f, FS_WRITE);
	if (!f) { G_Printf("SteamID ban: cannot write %s\n", STEAMID_BAN_FILE); return; }

	trap_FS_Write(STEAMID_BAN_VERSION, strlen(STEAMID_BAN_VERSION), f);
	for (i = 0; i < numSteamIDBans; i++)
	{
		if (!steamIDBans[i][0]) continue;
		Com_sprintf(entry, sizeof(entry), "%s ", steamIDBans[i]);
		trap_FS_Write(entry, strlen(entry), f);
	}
	trap_FS_FCloseFile(f);
}

/* Call once on map load (after G_ProcessIPBans) to read banSteamID.dat. */
void G_ProcessSteamIDBans(void)
{
	fileHandle_t f;
	/* Sized for MAX_STEAM_ID_BANS entries * 22 chars + header. */
	static char  buf[MAX_STEAM_ID_BANS * 22 + 32];
	char        *s, *t;
	int          len;

	numSteamIDBans = 0;
	memset(steamIDBans, 0, sizeof(steamIDBans));

	len = trap_FS_FOpenFile(STEAMID_BAN_FILE, &f, FS_READ);
	if (!f) return;
	if (len <= 0 || len >= (int)sizeof(buf)) { trap_FS_FCloseFile(f); return; }

	trap_FS_Read(buf, len, f);
	buf[len] = '\0';
	trap_FS_FCloseFile(f);

	s = Q_stristr(buf, STEAMID_BAN_VERSION) ? strchr(buf, ' ') : buf;
	if (!s) return;

	for (t = s; *t; )
	{
		s = strchr(t, ' ');
		if (!s) break;
		while (*s == ' ') *s++ = '\0';
		if (*t && numSteamIDBans < MAX_STEAM_ID_BANS)
			Q_strncpyz(steamIDBans[numSteamIDBans++], t, 21);
		t = s;
	}

	if (numSteamIDBans) G_Printf("Steam ID bans loaded: %d\n", numSteamIDBans);
}

void AddSteamIDBan(const char *steamID)
{
	if (G_CheckSteamIDBan(steamID))
	{
		G_Printf("Steam ID %s is already banned.\n", steamID);
		return;
	}
	if (numSteamIDBans >= MAX_STEAM_ID_BANS)
	{
		G_Printf("Steam ID ban list is full (%d entries).\n", MAX_STEAM_ID_BANS);
		return;
	}
	Q_strncpyz(steamIDBans[numSteamIDBans++], steamID, 21);
	UpdateSteamIDBans();
}

void RemoveSteamIDBan(const char *steamID)
{
	int i;
	for (i = 0; i < numSteamIDBans; i++)
	{
		if (!strcmp(steamIDBans[i], steamID))
		{
			for (; i < numSteamIDBans - 1; i++)
				Q_strncpyz(steamIDBans[i], steamIDBans[i + 1], 21);
			steamIDBans[--numSteamIDBans][0] = '\0';
			UpdateSteamIDBans();
			G_Printf("Steam ID %s unbanned.\n", steamID);
			return;
		}
	}
	G_Printf("Steam ID %s not found in ban list.\n", steamID);
}

int         G_SteamIDBanCount(void)  { return numSteamIDBans; }
const char *G_SteamIDBanEntry(int i) { return (i >= 0 && i < numSteamIDBans) ? steamIDBans[i] : NULL; }

static qboolean IsSteamID64(const char *str)
{
	int i, len = (int)strlen(str);
	if (len < 15 || len > 20) return qfalse;
	for (i = 0; i < len; i++)
		if (str[i] < '0' || str[i] > '9') return qfalse;
	return qtrue;
}

/* Server console: sban <clientid|name|steamid64> */
void Svcmd_SteamBan_f(void)
{
	char       str[MAX_TOKEN_CHARS];
	int        pids[MAX_CLIENTS];
	char       err[MAX_STRING_CHARS];
	int        playerid;
	const char *steamID;

	if (trap_Argc() < 2) { G_Printf("Usage: sban <clientid|name|steamid64>\n"); return; }
	trap_Argv(1, str, sizeof(str));

	if (IsSteamID64(str))
	{
		AddSteamIDBan(str);
		playerid = G_ClientNumberFromSteamID(str);
		if (playerid >= 0) trap_DropClient(playerid, "Steam ID banned.");
		return;
	}

	if (ClientNumbersFromString(str, pids) != 1)
	{
		G_MatchOnePlayer(pids, err, sizeof(err));
		G_Printf("%s\n", err);
		return;
	}
	playerid = pids[0];
	if (!g_entities[playerid].inuse || !g_entities[playerid].client)
	{
		G_Printf("Client slot %d is not in use.\n", playerid);
		return;
	}
	steamID = g_entities[playerid].client->sess.steamID;
	if (!steamID[0])
	{
		G_Printf("Client has no Steam ID -- use 'ban' to ban by IP instead.\n");
		return;
	}
	AddSteamIDBan(steamID);
	G_Printf("Steam ID banned: %s (%s)\n", g_entities[playerid].client->pers.netname, steamID);
	trap_DropClient(playerid, "Steam ID banned.");
}

/* Server console: unsban <steamid64> */
void Svcmd_SteamUnban_f(void)
{
	char str[MAX_TOKEN_CHARS];
	if (trap_Argc() < 2) { G_Printf("Usage: unsban <steamid64>\n"); return; }
	trap_Argv(1, str, sizeof(str));
	RemoveSteamIDBan(str);
}

/* Server console: sbanlist */
void Svcmd_SteamBanList_f(void)
{
	int i, count = G_SteamIDBanCount();
	G_Printf("Steam ID ban list (%d):\n", count);
	for (i = 0; i < count; i++)
		G_Printf("  %d: %s\n", i, G_SteamIDBanEntry(i));
	if (!count) G_Printf("  (none)\n");
}

/*
 * Wiring — add to ConsoleCommand() in g_svcmds.c:
 *
 *   } else if (Q_stricmp(cmd, "sban")     == 0) { Svcmd_SteamBan_f();
 *   } else if (Q_stricmp(cmd, "unsban")   == 0) { Svcmd_SteamUnban_f();
 *   } else if (Q_stricmp(cmd, "sbanlist") == 0) { Svcmd_SteamBanList_f();
 */
