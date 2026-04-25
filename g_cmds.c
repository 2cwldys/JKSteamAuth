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
 * g_cmds.c — Steam client command handlers (server game module).
 *
 * G_ClientNumberFromSteamID — utility, call from anywhere.
 * Cmd_SBan_f / Cmd_SUnban_f / Cmd_SBanList_f — wire into your ClientCommand
 * dispatch as admin-only commands named "sban", "unsban", "sbanlist".
 *
 * Integration: paste into your g_cmds.c which already includes g_local.h.
 * If reviewing standalone, add: #include "g_local.h"
 *
 * Depends on: g_svcmds.c (AddSteamIDBan, RemoveSteamIDBan,
 *             G_SteamIDBanCount, G_SteamIDBanEntry)
 */

/* -------------------------------------------------------------------------
 * G_ClientNumberFromSteamID
 * Returns the client slot of the connected player whose sess.steamID matches
 * steamIDStr, or -1 if not found.
 * ------------------------------------------------------------------------- */
int G_ClientNumberFromSteamID(const char *steamIDStr)
{
	int i;
	for (i = 0; i < level.maxclients; i++)
	{
		if (g_entities[i].inuse && g_entities[i].client &&
		    g_entities[i].client->pers.connected == CON_CONNECTED &&
		    g_entities[i].client->sess.steamID[0] &&
		    !strcmp(g_entities[i].client->sess.steamID, steamIDStr))
		{
			return i;
		}
	}
	return -1;
}

/* -------------------------------------------------------------------------
 * Cmd_SBan_f — "sban <clientid|name|steamid64>"
 * Wire into ClientCommand as an admin-only command.
 * Accepts a client number, partial name, or a raw Steam64 ID.
 * If a Steam64 ID is passed for an online player, kicks them immediately.
 * ------------------------------------------------------------------------- */
void Cmd_SBan_f(gentity_t *ent)
{
	char       arg[MAX_TOKEN_CHARS];
	int        pids[MAX_CLIENTS];
	int        targetNum;
	const char *steamID;

	trap_Argv(1, arg, sizeof(arg));
	if (!arg[0])
	{
		trap_SendServerCommand(ent - g_entities,
			"print \"Usage: sban <clientid|name|steamid64>\n\"");
		return;
	}

	/* Raw Steam64 ID: ban it, then kick if the player is online. */
	{
		int      slen = strlen(arg), k;
		qboolean isSteamID = (slen >= 15 && slen <= 20);
		for (k = 0; isSteamID && k < slen; k++)
			if (arg[k] < '0' || arg[k] > '9') isSteamID = qfalse;

		if (isSteamID)
		{
			AddSteamIDBan(arg);
			targetNum = G_ClientNumberFromSteamID(arg);
			if (targetNum >= 0) trap_DropClient(targetNum, "Steam ID banned.");
			trap_SendServerCommand(ent - g_entities,
				va("print \"^2Steam ID %s banned.\n\"", arg));
			return;
		}
	}

	if (ClientNumbersFromString(arg, pids) != 1)
	{
		trap_SendServerCommand(ent - g_entities,
			"print \"^3Could not find a unique client match.\n\"");
		return;
	}
	targetNum = pids[0];
	if (!g_entities[targetNum].inuse || !g_entities[targetNum].client) return;

	steamID = g_entities[targetNum].client->sess.steamID;
	if (!steamID[0])
	{
		trap_SendServerCommand(ent - g_entities,
			"print \"^3Client has no Steam ID -- use 'ban' to ban by IP instead.\n\"");
		return;
	}

	AddSteamIDBan(steamID);
	trap_SendServerCommand(ent - g_entities,
		va("print \"^2Steam banned: ^5%s ^2(%s)\n\"",
			g_entities[targetNum].client->pers.netname, steamID));
	trap_DropClient(targetNum, "Steam ID banned.");
}

/* -------------------------------------------------------------------------
 * Cmd_SUnban_f — "unsban <steamid64>"
 * ------------------------------------------------------------------------- */
void Cmd_SUnban_f(gentity_t *ent)
{
	char arg[MAX_TOKEN_CHARS];
	trap_Argv(1, arg, sizeof(arg));
	if (!arg[0])
	{
		trap_SendServerCommand(ent - g_entities, "print \"Usage: unsban <steamid64>\n\"");
		return;
	}
	RemoveSteamIDBan(arg);
	trap_SendServerCommand(ent - g_entities,
		va("print \"^2Steam ID %s removed from ban list (if present).\n\"", arg));
}

/* -------------------------------------------------------------------------
 * Cmd_SBanList_f — "sbanlist"
 * ------------------------------------------------------------------------- */
void Cmd_SBanList_f(gentity_t *ent)
{
	int  i, count = G_SteamIDBanCount();
	char buf[2048];

	Com_sprintf(buf, sizeof(buf), "^3Steam ID Ban List (%d):\n", count);
	for (i = 0; i < count && strlen(buf) < sizeof(buf) - 32; i++)
		Q_strcat(buf, sizeof(buf), va("  %d: %s\n", i, G_SteamIDBanEntry(i)));
	if (!count) Q_strcat(buf, sizeof(buf), "  (none)\n");

	trap_SendServerCommand(ent - g_entities, va("print \"%s\"", buf));
}
