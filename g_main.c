/*
 * Steam Authentication Support for Jedi Academy Multiplayer
 * Copyright (C) 2025 2cwldys
 * GNU General Public License v2 (or later) — see <https://www.gnu.org/licenses/>.
 */

#include "g_local.h"

// Bit flag for packing into a server features int (e.g. g_jediVmerc).
// Only needed if your mod packs multiple feature flags into one SERVERINFO CVar.
// If g_steamverify is CVAR_SERVERINFO (the default), this flag is unused.
#define SF_STEAMID (1 << 5)

void G_InitSteam(void)
{
	G_ProcessIPBans();
	G_ProcessSteamIDBans();
}

void G_Steam_SetServerFeatures(int *serverFeatures)
{
	if (g_steamverify.integer)
		*serverFeatures |= SF_STEAMID;
}
