@echo off
setlocal

set SCRIPT_DIR=%~dp0

echo Installing Python dependencies...
pip install -r "%SCRIPT_DIR%requirements.txt"
if errorlevel 1 (
    echo.
    echo ERROR: pip install failed. Make sure Python and pip are installed.
    pause
    exit /b 1
)

echo.
echo Registering steam-assistant MCP server with Claude Code...
claude mcp add steam-assistant -- python "%SCRIPT_DIR%steam_assistant.py"
if errorlevel 1 (
    echo.
    echo ERROR: claude mcp add failed. Make sure Claude Code CLI is installed.
    pause
    exit /b 1
)

echo.
echo Done. Restart Claude Code and the steam-assistant MCP will be available.
pause
