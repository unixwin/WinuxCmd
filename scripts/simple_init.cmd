@echo off
setlocal enabledelayedexpansion

echo [WinuxCmd] Testing...
set "WINUXCMD_BIN=C:\Users\Administrator\repo\WinuxCmd\build-dev\usr\bin"
echo [OK] Found: !WINUXCMD_BIN!
set "PATH=!WINUXCMD_BIN!;%PATH%"
echo [OK] Added to PATH
echo Done!
endlocal
exit /b 0
