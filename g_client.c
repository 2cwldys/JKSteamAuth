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
 * g_client.c — Steam identity enforcement hooks (server game module).
 *
 * Drop these four functions into your g_client.c and call them at the
 * documented hook points.  All game API calls use the standard JKA MP
 * trap interface — no engine modifications required.
 *
 * Integration: paste these functions into your g_client.c alongside the
 * existing ClientUserinfoChanged, ClientConnect, ClientBegin, ClientDisconnect
 * functions — the file already includes g_local.h which provides all types.
 * If reviewing this file standalone, add: #include "g_local.h"
 *
 * CVars required (declare + register from g_xcvar.h):
 *   g_steamverify          — master switch (0 = disabled)
 *   g_steam_forcename      — when 1, forces in-game name to Steam persona
 *   g_steam_illegalclients — when 1, allows clients with no Steam ID through
 *
 * Session field required (add to clientSession_t in g_local.h):
 *   char steamID[21];
 *
 * NOTE: This implementation is trust-based.  The client reports its own
 * Steam ID; there is no cryptographic ticket validation.  Players with
 * modified binaries can send an arbitrary ID.  This is suitable for
 * accountability and ban tracking, not for anti-cheat enforcement.
 */

/* -------------------------------------------------------------------------
 * Steam_OnUserinfoChanged
 *
 * Hook: call from ClientUserinfoChanged after Info_Validate, before any
 * name-processing code.
 *
 * Reads cl_steamid and cl_steamname from the userinfo string.  If
 * g_steam_forcename is set, overwrites the "name" key in-place and calls
 * trap_SetUserinfo so the engine's copy is also updated.
 *
 * Returns qtrue if the client was kicked; the caller must return immediately.
 * ------------------------------------------------------------------------- */
qboolean Steam_OnUserinfoChanged(int clientNum, gentity_t *ent, gclient_t *client, char *userinfo)
{
	const char *steamIDStr;
	qboolean    hasSteamID = qfalse;

	if (!g_steamverify.integer || (ent->r.svFlags & SVF_BOT))
		return qfalse;

	steamIDStr = Info_ValueForKey(userinfo, "cl_steamid");
	if (steamIDStr && steamIDStr[0])
	{
		int i, slen = strlen(steamIDStr);
		hasSteamID = (slen >= 15 && slen <= 20);
		for (i = 0; hasSteamID && i < slen; i++)
			if (steamIDStr[i] < '0' || steamIDStr[i] > '9') hasSteamID = qfalse;
	}

	if (!hasSteamID && !g_steam_illegalclients.integer)
	{
		trap_DropClient(clientNum, "You must have Steam running to connect to this server.");
		return qtrue;
	}

	if (hasSteamID)
		Q_strncpyz(client->sess.steamID, steamIDStr, sizeof(client->sess.steamID));

	if (g_steam_forcename.integer)
	{
		const char *steamName = Info_ValueForKey(userinfo, "cl_steamname");
		if (steamName && steamName[0])
		{
			if (client->pers.connected == CON_CONNECTED &&
			    hasSteamID &&
			    strcmp(Info_ValueForKey(userinfo, "name"), steamName) != 0)
				trap_SendServerCommand(clientNum,
					"print \"^2[Steam] ^1Changing names is prohibited on this server.^7\n\"");
			Info_SetValueForKey(userinfo, "name", steamName);
			trap_SetUserinfo(clientNum, userinfo);
		}
	}

	return qfalse;
}

/* -------------------------------------------------------------------------
 * Steam_OnClientConnect
 *
 * Hook: call from ClientConnect after the IP ban check.
 * Propagate the return value: if non-NULL, return it from ClientConnect.
 * ------------------------------------------------------------------------- */
const char *Steam_OnClientConnect(int clientNum, qboolean firstTime, qboolean isBot,
                                  gentity_t *ent, const char *userinfo)
{
	const char *steamID;

	if (!g_steamverify.integer || !firstTime || (ent->r.svFlags & SVF_BOT) || isBot)
		return NULL;

	steamID = Info_ValueForKey(userinfo, "cl_steamid");
	if (steamID && steamID[0] && G_CheckSteamIDBan(steamID))
		return "Steam ID banned.";

	return NULL;
}

/* -------------------------------------------------------------------------
 * Steam_OnClientBegin
 *
 * Hook: call from ClientBegin after client->pers.connected = CON_CONNECTED.
 * Pass firstBegin as (client->sess.initClientBegin == qfalse), captured
 * before SendInitialCommands is called.
 * ------------------------------------------------------------------------- */
void Steam_OnClientBegin(int clientNum, qboolean firstBegin, gentity_t *ent, gclient_t *client)
{
	char        info[MAX_INFO_STRING];
	const char *steamID;

	if (!firstBegin || !g_steamverify.integer || (ent->r.svFlags & SVF_BOT))
		return;

	trap_GetUserinfo(clientNum, info, sizeof(info));
	steamID = Info_ValueForKey(info, "cl_steamid");

	if (steamID && steamID[0])
		trap_SendServerCommand(-1, va("print \"^2[Steam] ^5%s ^2(SteamID: ^5%s^2) connected.^7\n\"",
			client->pers.netname, steamID));
	else
		trap_SendServerCommand(-1, va("print \"^3[Steam] Unauthenticated user ^5%s ^3connected.^7\n\"",
			client->pers.netname));
}

/* -------------------------------------------------------------------------
 * Steam_OnClientDisconnect
 *
 * Hook: call from ClientDisconnect before any state teardown.
 * The CON_DISCONNECTED guard prevents a double-fire on reconnect.
 * ------------------------------------------------------------------------- */
void Steam_OnClientDisconnect(int clientNum, gentity_t *ent)
{
	if (!g_steamverify.integer || (ent->r.svFlags & SVF_BOT) ||
	    ent->client->pers.connected == CON_DISCONNECTED)
		return;

	if (ent->client->sess.steamID[0])
		trap_SendServerCommand(-1, va("print \"^2[Steam] ^5%s ^2(SteamID: ^5%s^2) disconnected.^7\n\"",
			ent->client->pers.netname, ent->client->sess.steamID));
	else
		trap_SendServerCommand(-1, va("print \"^3[Steam] Unauthenticated user ^5%s ^3disconnected.^7\n\"",
			ent->client->pers.netname));
}
