@echo off
setlocal enabledelayedexpansion

:: WinuxCmd - CMD Environment Initializer
set SCRIPT_VERSION=0.16.0
set SCRIPT_DIR=%~dp0
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"

:: Check for --install flag
if /i "%~1"=="--install" goto :install_permanent
if /i "%~1"=="--uninstall" goto :uninstall_permanent
if /i "%~1"=="--help" goto :show_help

:: Find WinuxCmd Installation
set WINUXCMD_BIN=
if exist "%SCRIPT_DIR%\..\build-dev\usr\bin\winuxcmd.exe" (
    set WINUXCMD_BIN=%SCRIPT_DIR%\..\build-dev\usr\bin
) else if exist "%LOCALAPPDATA%\WinuxCmd\WinuxCmd-0.16.0-win-x64\usr\bin\winuxcmd.exe" (
    set WINUXCMD_BIN=%LOCALAPPDATA%\WinuxCmd\WinuxCmd-0.16.0-win-x64\usr\bin
) else if exist "%SCRIPT_DIR%\..\build-dev\winuxcmd.exe" (
    set WINUXCMD_BIN=%SCRIPT_DIR%\..\build-dev
) else if exist "%LOCALAPPDATA%\WinuxCmd\WinuxCmd-0.16.0-win-x64\winuxcmd.exe" (
    set WINUXCMD_BIN=%LOCALAPPDATA%\WinuxCmd\WinuxCmd-0.16.0-win-x64
) else (
    echo [ERROR] WinuxCmd not found.
    echo Please run from development directory or install WinuxCmd.
    exit /b 1
)

:: Normalize path
if "!WINUXCMD_BIN:~-1!"=="\" set "WINUXCMD_BIN=!WINUXCMD_BIN:~0,-1!"

echo.
echo [WinuxCmd] Initializing v%SCRIPT_VERSION%...
echo [OK] Found at: !WINUXCMD_BIN!
echo.

:: Add to PATH
set PATH=!WINUXCMD_BIN!;%PATH%
echo [OK] Added to PATH (temporary)
echo.

:: Create DOSKEY macros
echo Creating DOSKEY macros...
set MACRO_COUNT=0
for %%f in ("!WINUXCMD_BIN!\*.exe") do (
    set CMDNAME=%%~nf
    if /i "!CMDNAME!" neq "winuxcmd" (
        doskey !CMDNAME!="%%f" $*
        if !errorlevel! equ 0 (
            set /a MACRO_COUNT+=1
            echo   [+] !CMDNAME!
        ) else (
            echo   [!] Failed to create macro for !CMDNAME!
        )
    )
)
doskey winuxcmd="!WINUXCMD_BIN!\winuxcmd.exe" $*
echo   [+] winuxcmd
doskey echo="!WINUXCMD_BIN!\echo.exe" $*
echo   [+] GNU echo

echo.
echo [WinuxCmd] Activated in current session!
echo   %MACRO_COUNT% GNU commands ready.
echo.
echo For permanent activation (PATH only), run: %~nx0 --install
echo.
endlocal
exit /b 0

:: ========== Install to AutoRun (Permanent) ==========
:install_permanent
echo.
echo [WinuxCmd] Installing to AutoRun...
echo.
echo NOTE: Due to CMD limitations, AutoRun cannot create DOSKEY macros.
echo       Only PATH will be set automatically.
echo.

:: Re-initialize variables (since we're after endlocal)
set SCRIPT_DIR=%~dp0
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"

:: Find WinuxCmd again
set WINUXCMD_BIN=
if exist "%SCRIPT_DIR%\..\build-dev\usr\bin\winuxcmd.exe" (
    set WINUXCMD_BIN=%SCRIPT_DIR%\..\build-dev\usr\bin
) else if exist "%LOCALAPPDATA%\WinuxCmd\WinuxCmd-0.16.0-win-x64\usr\bin\winuxcmd.exe" (
    set WINUXCMD_BIN=%LOCALAPPDATA%\WinuxCmd\WinuxCmd-0.16.0-win-x64\usr\bin
) else if exist "%SCRIPT_DIR%\..\build-dev\winuxcmd.exe" (
    set WINUXCMD_BIN=%SCRIPT_DIR%\..\build-dev
) else if exist "%LOCALAPPDATA%\WinuxCmd\WinuxCmd-0.16.0-win-x64\winuxcmd.exe" (
    set WINUXCMD_BIN=%LOCALAPPDATA%\WinuxCmd\WinuxCmd-0.16.0-win-x64
)

if "%WINUXCMD_BIN%"=="" (
    echo [ERROR] WinuxCmd not found.
    exit /b 1
)

:: Normalize path
if "%WINUXCMD_BIN:~-1%"=="\" set "WINUXCMD_BIN=%WINUXCMD_BIN:~0,-1%"

:: Use full path without short names
set AUTORUN_SCRIPT=%USERPROFILE%\AppData\Local\Temp\winuxcmd_autorun.cmd

:: Write autorun script (only set PATH)
> "%AUTORUN_SCRIPT%" (
    echo @echo off
    echo set "PATH=%WINUXCMD_BIN%;%%PATH%%"
)

:: Add to registry
reg add "HKCU\Software\Microsoft\Command Processor" /v AutoRun /t REG_SZ /d "%AUTORUN_SCRIPT%" /f >nul 2>&1
if %errorlevel% equ 0 (
    echo [OK] WinuxCmd PATH added to AutoRun!
    echo.
    echo USAGE INSTRUCTIONS:
    echo   1. Close and reopen CMD
    echo   2. Commands will be available via PATH (e.g., ls, cat, grep)
    echo   3. For DOSKEY macros (tab completion), run: %~nx0
    echo.
    echo RECOMMENDED: Use temporary activation instead:
    echo   cd /d "%SCRIPT_DIR%"
    echo   %~nx0
    exit /b 0
) else (
    echo [ERROR] Failed to install to AutoRun.
    exit /b 1
)

exit /b 0
goto :eof

:: ========== Uninstall from AutoRun ==========
:uninstall_permanent
echo.
echo [WinuxCmd] Removing from AutoRun...
reg delete "HKCU\Software\Microsoft\Command Processor" /v AutoRun /f >nul 2>&1
if %errorlevel% equ 0 (
    echo [OK] Removed from AutoRun.
) else (
    echo [INFO] No AutoRun entry found.
)
if exist "%USERPROFILE%\AppData\Local\Temp\winuxcmd_autorun.cmd" (
    del "%USERPROFILE%\AppData\Local\Temp\winuxcmd_autorun.cmd" /f /q >nul 2>&1
    echo [OK] Deleted autorun script.
)
echo.
exit /b 0

:: ========== Show Help ==========
:show_help
echo.
echo WinuxCmd CMD Initializer v%SCRIPT_VERSION%
echo ========================================
echo.
echo DESCRIPTION:
echo   Activates WinuxCmd in current CMD session or installs/uninstalls
echo   permanent AutoRun (PATH only).
echo.
echo USAGE:
echo   %~nx0              - Activate in current session
echo   %~nx0 --install    - Install to AutoRun (permanent)
echo   %~nx0 --uninstall  - Remove from AutoRun
echo   %~nx0 --help       - Show this help
echo.
echo NOTES:
echo   - AutoRun only sets PATH; DOSKEY macros must be created manually
echo     by running %~nx0 without arguments in each CMD session.
echo   - Use temporary activation for full functionality.
echo.
exit /b 0
