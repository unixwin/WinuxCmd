@echo off
:: ============================================================================
:: WinuxCmd - CMD Uninstaller
:: Copyright © 2026 [caomengxuan666]
:: ============================================================================
::
:: DESCRIPTION:
::   Removes WinuxCmd from CMD AutoRun and cleans up
::
:: ============================================================================

setlocal enabledelayedexpansion

echo.
echo [WinuxCmd] Uninstaller
echo ========================================
echo.

:: Remove from registry
reg query "HKCU\Software\Microsoft\Command Processor" /v AutoRun >nul 2>&1
if !errorlevel! equ 0 (
    reg delete "HKCU\Software\Microsoft\Command Processor" /v AutoRun /f >nul 2>&1
    echo [OK] Removed from CMD AutoRun
) else (
    echo [INFO] No AutoRun entry found
)

:: Find and delete autorun script
set "WINUXCMD_BASE=%LOCALAPPDATA%\WinuxCmd"
if exist "%WINUXCMD_BASE%" (
    for /f "tokens=*" %%f in ('dir "%WINUXCMD_BASE%\*autorun*.cmd" /s /b 2^>nul') do (
        del "%%f" /f /q >nul 2>&1
        echo [OK] Deleted: %%f
    )
)

:: Also check current directory
if exist "%~dp0%winuxcmd_autorun.cmd" (
    del "%~dp0%winuxcmd_autorun.cmd" /f /q >nul 2>&1
    echo [OK] Deleted local autorun script
)

echo.
echo [WinuxCmd] Uninstall complete!
echo.
echo To clean up DOSKEY macros in current session:
echo   doskey /reinstall
echo.
pause