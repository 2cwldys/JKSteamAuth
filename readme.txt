Steam Authentication Support for Jedi Academy Multiplayer
=========================================================
Copyright (C) 2025 2cwldys
License: GNU General Public License v2 (or, at your option, v3)
         See <https://www.gnu.org/licenses/>

Overview
--------
Trust-based Steam identity for JKA MP servers.  Players send their Steam64 ID
and persona name as CVAR_USERINFO CVars (cl_steamid, cl_steamname); the server
optionally enforces authentication, bans by Steam ID, and forces in-game names
to match Steam personas.

No engine modifications are required.  Works with any standard JKA MP mod.

Files
-----
ui_steamload.c   Client (UI) module — fetches Steam identity at launch via
                 registry/loginusers.vdf (stage 1) and live steam_api.dll
                 (stage 2).  Exposes cl_steamid and cl_steamname as
                 CVAR_USERINFO so the engine includes them in every connect.
                 Also provides UI_Steam_IsServerEnforcing() and
                 UI_Steam_NameEnforcement() for persona-name lockdown.
                 Windows and Linux paths included.

g_client.c       Four hook functions for the server game module:
                   Steam_OnUserinfoChanged  — enforce auth, store steamID,
                                             optionally force name
                   Steam_OnClientConnect    — check Steam ID ban list
                   Steam_OnClientBegin      — announce connection
                   Steam_OnClientDisconnect — announce disconnection

g_cmds.c         G_ClientNumberFromSteamID utility + client-facing admin
                 command handlers (Cmd_SBan_f, Cmd_SUnban_f, Cmd_SBanList_f).

g_svcmds.c       Complete ban engine (in-memory list, banSteamID.dat
                 persistence) + server console command handlers
                 (Svcmd_SteamBan_f, Svcmd_SteamUnban_f, Svcmd_SteamBanList_f).

g_local.h        Function declarations and externs to merge into your g_local.h.

g_xcvar.h        CVar table entries (XCVAR_DEF or manual trap_Cvar_Register).

g_main.c         G_InitSteam and G_Steam_SetServerFeatures.

ui_main.c        UI hook wrappers: UI_Steam_Setup, UI_Steam_Frame, UI_Steam_ServerIcon.

steam_assistant.py  Offline MCP server — connect via Claude Code for guided integration.

Integration steps
-----------------
Server side:
1. Add `char steamID[21];` to clientSession_t in your g_local.h.
2. Merge declarations from g_local.h (this package) into your g_local.h.
3. Add the three CVar entries from g_xcvar.h (XCVAR_DEF or manual).
4. Call G_ProcessSteamIDBans() after G_ProcessIPBans() in G_InitGame.
5. Paste Steam_On* functions from g_client.c; call at each hook point.
6. Paste G_ClientNumberFromSteamID + Cmd_S* from g_cmds.c; wire commands.
7. Paste the ban engine + Svcmd_* from g_svcmds.c; wire console commands.

Client/UI side:
8. Add ui_steamload.c to your UI module build.
9. Call UI_Steam_Setup() from UI_Init.
10. Call UI_Steam_Frame(realtime) from your UI update function.
11. Call UI_Steam_ServerIcon(source, index) in server browser row draw.
12. Add gfx/menus/steamlogo.tga to your mod's pk3.

AI-assisted integration (offline)
----------------------------------
  pip install -r requirements.txt
  claude mcp add steam-assistant -- python /path/to/steam_assistant.py

Then in Claude Code:
  "Analyze my g_main.c and tell me where to add Steam hooks"
  "Show me the g_client.c integration file"
  "Give me the full integration checklist"

Steam account requirement
------------------------
Players do NOT need to own Jedi Academy on Steam to use this system.  They
only need a Steam account — free accounts are sufficient.  The system reads
the Steam64 ID and persona name of whichever account is logged into the Steam
client on the player's machine, regardless of what games that account owns.

This means:
  - Retail/CD copy players can use it as long as Steam is installed and
    they are logged into any Steam account.
  - Free-to-play or limited Steam accounts are accepted.
  - The system identifies the player, it does not verify game ownership.

Servers that set g_steam_illegalclients 0 (the default) will kick players
who have no Steam client running or no account logged in.  Setting
g_steam_illegalclients 1 allows those players through while still tracking
the Steam IDs of players who do supply one.

Trust model
-----------
Clients self-report their Steam ID.  Players with modified binaries can send
an arbitrary ID.  This system is suitable for accountability and ban tracking;
it is not a substitute for cryptographic ticket validation.
