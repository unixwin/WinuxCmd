@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
set "WINUX_PS1=%SCRIPT_DIR%\winux.ps1"

if not exist "%WINUX_PS1%" (
    echo winux.ps1 not found
    exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%WINUX_PS1%" -Action %*
exit /b %ERRORLEVEL%
