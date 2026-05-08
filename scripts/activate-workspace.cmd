@echo off
set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "WINUX_ROOT=%%~fI"
set "WINUX_LOCAL_BIN=%WINUX_ROOT%\.winuxcmd\bin"

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%setup-workspace-bin.ps1" -Root "%WINUX_ROOT%" %*
if errorlevel 1 exit /b %errorlevel%

set "PATH=%WINUX_LOCAL_BIN%;%PATH%"
set "WINUXCMD_HOME=%WINUX_LOCAL_BIN%"

echo.
echo WinuxCmd workspace activation is active for this CMD session.
echo Local bin: %WINUX_LOCAL_BIN%
echo Use explicit .exe command names when a shell alias may conflict.
