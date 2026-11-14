# Diagnose why real-time protection will not turn on (elevated).
$ErrorActionPreference = 'Continue'
'--- policies ---'
Get-ItemProperty 'HKLM:\SOFTWARE\Policies\Microsoft\Windows Defender' -ErrorAction SilentlyContinue |
    Select-Object DisableAntiSpyware, DisableAntiVirus, DisableRealtimeMonitoring | Format-List
Get-ChildItem 'HKLM:\SOFTWARE\Policies\Microsoft\Windows Defender' -ErrorAction SilentlyContinue |
    ForEach-Object { $_.PSPath; Get-ItemProperty $_.PSPath | Format-List }

'--- realtime key ---'
Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows Defender\Real-Time Protection' -ErrorAction SilentlyContinue |
    Format-List *
'--- status ---'
Get-MpComputerStatus | Select-Object AMRunningMode, AMServiceEnabled, AntivirusEnabled,
    RealTimeProtectionEnabled, IsTamperProtected, TamperProtectionSource, AMServiceVersion,
    NISEnabled, DefinitionStatus | Format-List
'--- service ---'
Get-Service WinDefend, WdNisSvc, Sense, SecurityHealthService | Select-Object Name, Status, StartType | Format-Table -AutoSize
'--- disable attempts ---'
Set-MpPreference -DisableRealtimeMonitoring $false -ErrorAction Continue
'after Set-MpPreference: ' + (Get-MpComputerStatus).RealTimeProtectionEnabled
# Older engines flip the setting through this value object.
$k = 'HKLM:\SOFTWARE\Microsoft\Windows Defender\Real-Time Protection'
Set-ItemProperty -Path $k -Name DisableRealtimeMonitoring -Value 0 -Type DWord -ErrorAction Continue
'realtime reg value now: ' + (Get-ItemProperty $k).DisableRealtimeMonitoring
Restart-Service WinDefend -Force -ErrorAction Continue
Start-Sleep -Seconds 5
'after service restart: ' + (Get-MpComputerStatus).RealTimeProtectionEnabled
Get-MpComputerStatus | Select-Object AMRunningMode, RealTimeProtectionEnabled, BehaviorMonitorEnabled | Format-List
