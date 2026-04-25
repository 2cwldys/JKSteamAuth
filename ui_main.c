/*
 * Steam Authentication Support for Jedi Academy Multiplayer
 * Copyright (C) 2025 2cwldys
 * GNU General Public License v2 (or later) — see <https://www.gnu.org/licenses/>.
 */

#include "ui_local.h"

qboolean  UI_LoadSteamAPI(void);
qboolean  UI_UnloadSteamAPI(void);
void      UI_Steam_Init(void);
qboolean  UI_Steam_IsServerEnforcing(void);
void      UI_Steam_NameEnforcement(int realtime);
qhandle_t UI_Steam_GetServerIcon(int source, int serverIndex);

// Call once from UI_Init after other asset registration.
void UI_Steam_Setup(void)
{
	UI_LoadSteamAPI();
}

// Call every UI frame. Self-rate-limits to once per 5 seconds.
void UI_Steam_Frame(int realtime)
{
	UI_Steam_NameEnforcement(realtime);
}

// Call in server browser hostname column draw. Returns icon handle or 0.
qhandle_t UI_Steam_ServerIcon(int source, int serverIndex)
{
	return UI_Steam_GetServerIcon(source, serverIndex);
}
