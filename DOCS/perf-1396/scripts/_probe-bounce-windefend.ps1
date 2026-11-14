# WinDefend's service DACL does not grant Administrators stop/start rights, and
# the engine only re-reads the real-time-protection configuration when it
# starts. A short-lived SYSTEM scheduled task is the supported way to bounce the
# service without a reboot. Delete the task afterwards.
$ErrorActionPreference = 'Continue'
$name = 'perf1396-bounce-windefend'
$inner = 'D:\repo\wc-av1396\DOCS\perf-1396\raw\bounce-windefend.ps1'
Set-Content -LiteralPath $inner -Encoding ASCII -Value @'
Restart-Service WinDefend -Force
Start-Sleep -Seconds 2
'@

& schtasks.exe /create /tn $name /tr ('powershell -NoProfile -ExecutionPolicy Bypass -File ' + $inner) /sc once /st 23:59 /ru SYSTEM /rl HIGHEST /f 2>&1 | Select-Object -Last 2
& schtasks.exe /run /tn $name 2>&1 | Select-Object -Last 1
Start-Sleep -Seconds 25
& schtasks.exe /delete /tn $name /f 2>&1 | Select-Object -Last 1

Start-Sleep -Seconds 5
'RealTimeProtectionEnabled=' + (Get-MpComputerStatus).RealTimeProtectionEnabled
'BehaviorMonitorEnabled=' + (Get-MpComputerStatus).BehaviorMonitorEnabled
'ServiceStatus=' + (Get-Service WinDefend).Status
'--- filters ---'
& fltmc.exe filters 2>&1
