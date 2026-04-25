"""
Steam Authentication MCP Server for Jedi Academy Modding
Copyright (C) 2025 2cwldys — GNU GPL v2 or later

Exposes the steamintegration source files as resources and provides
analysis tools that identify exactly where to add Steam hooks in an
existing JKA mod codebase.  Runs fully offline over stdio.

Add to Claude Code:
  claude mcp add steam-assistant -- python /path/to/steam_assistant.py
"""

import os
import re
from mcp.server.fastmcp import FastMCP

mcp = FastMCP("JKA Steam Integration Assistant")

STEAM_DIR = os.path.dirname(os.path.abspath(__file__))

SOURCE_FILES = {
    "g_client.c":     "Server hook functions: Steam_OnUserinfoChanged, Steam_OnClientConnect, Steam_OnClientBegin, Steam_OnClientDisconnect",
    "g_cmds.c":       "Client-side admin commands (sban/sunban/sbanlist) and G_ClientNumberFromSteamID",
    "g_local.h":      "All function declarations and CVar externs to merge into your g_local.h",
    "g_main.c":       "G_InitSteam (ban list load) and G_Steam_SetServerFeatures (feature bit)",
    "g_svcmds.c":     "Server console ban engine with banSteamID.dat persistence",
    "g_xcvar.h":      "CVar table entries for g_steamverify, g_steam_forcename, g_steam_illegalclients",
    "ui_main.c":      "UI hook points: UI_Steam_Setup, UI_Steam_Frame, UI_Steam_ServerIcon",
    "ui_steamload.c": "Full Steam identity fetch + server browser icon + name enforcement",
    "readme.txt":     "Integration overview and ordered step list",
}

# Maps file basename patterns to their hook analysis rules.
# Each rule: (search_pattern, found_message, not_found_message, code_to_add)
HOOK_RULES = {
    "g_main.c": [
        (
            r"G_ProcessIPBans\s*\(",
            "Found G_ProcessIPBans() — add G_ProcessSteamIDBans() on the next line.",
            "G_ProcessIPBans() not found — add G_ProcessSteamIDBans() inside G_InitGame after IP ban loading.",
            "G_ProcessSteamIDBans();"
        ),
        (
            r"G_InitGame\s*\(",
            "Found G_InitGame — make sure G_InitSteam() or G_ProcessSteamIDBans() is called inside it.",
            "G_InitGame not found in this file — check you are looking at the right file.",
            None
        ),
        (
            r"trap_Cvar_Update",
            "Found existing trap_Cvar_Update calls — add the three Steam CVar updates alongside them.",
            "No trap_Cvar_Update found — add the three Steam CVar updates in G_RunFrame or G_UpdateCvars.",
            "trap_Cvar_Update(&g_steamverify);\ntrap_Cvar_Update(&g_steam_forcename);\ntrap_Cvar_Update(&g_steam_illegalclients);"
        ),
    ],
    "g_client.c": [
        (
            r"ClientUserinfoChanged\s*\(",
            "Found ClientUserinfoChanged — call Steam_OnUserinfoChanged() near the top of this function.",
            "ClientUserinfoChanged not found — locate where userinfo is parsed and add the Steam hook there.",
            "Steam_OnUserinfoChanged(clientNum, ent, client, userinfo);"
        ),
        (
            r"ClientConnect\s*\(",
            "Found ClientConnect — call Steam_OnClientConnect() and return its result if non-NULL.",
            "ClientConnect not found — locate where clients are validated on connect.",
            'const char *steamErr = Steam_OnClientConnect(clientNum, firstTime, isBot, ent, userinfo);\nif (steamErr) return steamErr;'
        ),
        (
            r"ClientBegin\s*\(",
            "Found ClientBegin — call Steam_OnClientBegin() inside it.",
            "ClientBegin not found — locate where clients first enter the game.",
            "Steam_OnClientBegin(clientNum, firstBegin, ent, client);"
        ),
        (
            r"ClientDisconnect\s*\(",
            "Found ClientDisconnect — call Steam_OnClientDisconnect() inside it.",
            "ClientDisconnect not found — locate where clients leave the game.",
            "Steam_OnClientDisconnect(clientNum, ent);"
        ),
    ],
    "g_cmds.c": [
        (
            r"G_Say\s*\(|Cmd_Say_f\s*\(",
            "Found chat/command handling — add Cmd_SBan_f, Cmd_SUnban_f, Cmd_SBanList_f as admin commands here.",
            "No command dispatch found — locate where ClientCommand dispatches named commands.",
            None
        ),
        (
            r"\"kick\"|\"clientkick\"",
            "Found admin command list — add \"sban\", \"sunban\", \"sbanlist\" entries alongside these.",
            None,
            None
        ),
    ],
    "g_svcmds.c": [
        (
            r"ConsoleCommand\s*\(",
            "Found ConsoleCommand — add Svcmd_SteamBan_f, Svcmd_SteamUnban_f, Svcmd_SteamBanList_f dispatch here.",
            "ConsoleCommand not found — locate where server console commands are dispatched.",
            'if (!Q_stricmp(cmd, "steamban"))   { Svcmd_SteamBan_f();    return qtrue; }\n'
            'if (!Q_stricmp(cmd, "steamunban")) { Svcmd_SteamUnban_f(); return qtrue; }\n'
            'if (!Q_stricmp(cmd, "steambanlist")) { Svcmd_SteamBanList_f(); return qtrue; }'
        ),
        (
            r"G_ProcessIPBans\s*\(",
            "Also found G_ProcessIPBans here — call G_ProcessSteamIDBans() alongside it.",
            None,
            None
        ),
    ],
    "g_local.h": [
        (
            r"clientSession_t",
            "Found clientSession_t — add `char steamID[21];` as a field inside the struct.",
            "clientSession_t not found — locate your session struct and add `char steamID[21];`.",
            "char steamID[21];"
        ),
        (
            r"G_ProcessIPBans",
            "Found G_ProcessIPBans declaration — add G_ProcessSteamIDBans and related externs alongside it.",
            "No IP ban declarations found — add the Steam function externs from g_local.h (steamintegration).",
            None
        ),
    ],
    "g_xcvar.h": [
        (
            r"XCVAR_DEF",
            "Found XCVAR_DEF macro usage — add the three Steam XCVAR_DEF entries from g_xcvar.h (steamintegration).",
            "No XCVAR_DEF found — use the manual trap_Cvar_Register block from g_xcvar.h (steamintegration) in G_InitGame.",
            None
        ),
    ],
    "ui_main.c": [
        (
            r"UI_Init\s*\(",
            "Found UI_Init — call UI_Steam_Setup() inside it after other asset registration.",
            "UI_Init not found — locate your UI initialisation function.",
            "UI_Steam_Setup();"
        ),
        (
            r"trap_R_RegisterShaderNoMip",
            "Found shader registration — UI_LoadSteamAPI (called by UI_Steam_Setup) registers gfx/menus/steamlogo automatically.",
            None,
            None
        ),
        (
            r"realtime|uiDC\.realTime|trap_Milliseconds",
            "Found realtime reference — call UI_Steam_Frame(realtime) in the same update function.",
            "No realtime reference found — pass trap_Milliseconds() to UI_Steam_Frame().",
            "UI_Steam_Frame(realtime);"
        ),
        (
            r"ui_netSource|LAN_GetServerInfo|server.*browser|serverIndex",
            "Found server browser code — call UI_Steam_ServerIcon(source, serverIndex) in the hostname column draw.",
            "No server browser draw found in this file — locate where server rows are rendered.",
            "qhandle_t steamIcon = UI_Steam_ServerIcon(source, serverIndex);\nif (steamIcon) trap_R_DrawStretchPic(x, y, 16, 16, 0, 0, 1, 1, steamIcon);"
        ),
    ],
}


def _read_file(filename: str) -> str:
    path = os.path.join(STEAM_DIR, filename)
    if not os.path.isfile(path):
        return f"File not found: {filename}"
    with open(path, "r", encoding="utf-8") as f:
        return f.read()


@mcp.resource("steam://files/{filename}")
def steam_file(filename: str) -> str:
    """Return the content of a steamintegration source file."""
    if filename not in SOURCE_FILES:
        return f"Unknown file '{filename}'. Call list_files() to see available files."
    return _read_file(filename)


@mcp.tool()
def list_files() -> str:
    """List all steamintegration source files with a description of each."""
    lines = ["Available steamintegration files:\n"]
    for name, desc in SOURCE_FILES.items():
        lines.append(f"  {name}\n    {desc}\n")
    return "\n".join(lines)


@mcp.tool()
def read_steam_file(filename: str) -> str:
    """
    Return the full content of a steamintegration source file.

    Args:
        filename: One of the files listed by list_files().
    """
    if filename not in SOURCE_FILES:
        return f"Unknown file '{filename}'. Call list_files() to see available files."
    return _read_file(filename)


@mcp.tool()
def analyze_mod_file(filename: str, content: str) -> str:
    """
    Analyze one of your existing mod source files and report exactly where
    to add Steam integration hooks, with the code to insert at each point.

    Args:
        filename: The basename of your file (e.g. "g_main.c", "g_client.c").
        content:  The full text of that file.
    """
    basename = os.path.basename(filename).lower()

    rules = None
    for key in HOOK_RULES:
        if basename == key:
            rules = HOOK_RULES[key]
            break

    if rules is None:
        supported = ", ".join(HOOK_RULES.keys())
        return (
            f"No analysis rules for '{basename}'.\n"
            f"Supported files: {supported}\n\n"
            "For other files, read the relevant steamintegration source file "
            "with read_steam_file() and merge manually."
        )

    results = []
    for pattern, found_msg, not_found_msg, code in rules:
        match = re.search(pattern, content, re.IGNORECASE)
        if match:
            line_num = content[: match.start()].count("\n") + 1
            msg = f"[line ~{line_num}] {found_msg}"
            if code:
                msg += f"\n  Add:\n    {code.replace(chr(10), chr(10) + '    ')}"
        else:
            if not_found_msg:
                msg = f"[NOT FOUND] {not_found_msg}"
                if code:
                    msg += f"\n  Add:\n    {code.replace(chr(10), chr(10) + '    ')}"
            else:
                continue
        results.append(msg)

    if not results:
        return f"No integration points identified for '{basename}'."

    header = f"Analysis of {basename}:\n" + ("─" * 60) + "\n"
    return header + "\n\n".join(results)


@mcp.tool()
def get_integration_checklist() -> str:
    """Return the full ordered integration checklist for adding Steam auth to a JKA mod."""
    return """
JKA Steam Authentication — Integration Checklist
=================================================

SERVER SIDE
-----------
1. g_local.h
   • Add `char steamID[21];` to clientSession_t.
   • Merge all declarations from steamintegration/g_local.h.

2. g_xcvar.h / CVar registration
   • If using XCVAR_DEF: add the three entries from steamintegration/g_xcvar.h.
   • Otherwise: call G_RegisterSteamCvars() from G_InitGame.
   • g_steamverify MUST be CVAR_SERVERINFO for the browser icon to work.

3. g_main.c
   • Call G_ProcessSteamIDBans() after G_ProcessIPBans() in G_InitGame.
   • Call trap_Cvar_Update for the three Steam CVars in G_RunFrame / G_UpdateCvars.
   • Optionally call G_Steam_SetServerFeatures(&serverFeatures) if packing
     feature flags into g_jediVmerc.

4. g_client.c
   • Paste Steam_OnUserinfoChanged / Connect / Begin / Disconnect from
     steamintegration/g_client.c and call each at the matching hook point.

5. g_cmds.c
   • Paste G_ClientNumberFromSteamID and Cmd_SBan_f / Unban / BanList.
   • Wire the three commands as admin-only in ClientCommand dispatch.

6. g_svcmds.c
   • Paste the ban engine and Svcmd_SteamBan_f / Unban / BanList.
   • Wire in ConsoleCommand: "steamban", "steamunban", "steambanlist".

CLIENT / UI SIDE
----------------
7. ui_steamload.c
   • Add this file to your UI module build (CMakeLists, Makefile, or .vcproj).

8. ui_main.c
   • Call UI_Steam_Setup() inside UI_Init after other asset registration.
   • Call UI_Steam_Frame(realtime) in your UI update/frame function.
   • Call UI_Steam_ServerIcon(source, serverIndex) in server browser row draw
     and render the returned handle if non-zero.

9. Assets
   • Add gfx/menus/steamlogo.tga to your mod's pk3.

Use analyze_mod_file(filename, content) to get line-specific guidance for
each of your existing source files.
""".strip()
