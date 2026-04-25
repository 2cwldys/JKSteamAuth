#!/bin/sh
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "Installing Python dependencies..."
pip install -r "$SCRIPT_DIR/requirements.txt"

echo ""
echo "Registering steam-assistant MCP server with Claude Code..."
claude mcp add steam-assistant -- python "$SCRIPT_DIR/steam_assistant.py"

echo ""
echo "Done. Restart Claude Code and the steam-assistant MCP will be available."
