# Is the Real-Time Protection registry value (not just the policy) still pinned?
$ErrorActionPreference = 'Continue'
$k = 'HKLM:\SOFTWARE\Microsoft\Windows Defender\Real-Time Protection'
"value before: " + (Get-ItemProperty $k -ErrorAction SilentlyContinue).DisableRealtimeMonitoring
Set-MpPreference -DisableRealtimeMonitoring $false
Start-Sleep -Seconds 2
"value after Set-MpPreference: " + (Get-ItemProperty $k -ErrorAction SilentlyContinue).DisableRealtimeMonitoring
Get-MpPreference | Select-Object DisableRealtimeMonitoring | Format-List
(Get-MpComputerStatus).RealTimeProtectionEnabled

'--- ACL of the key ---'
$acl = Get-Acl $k
$acl.Access | Select-Object IdentityReference, RegistryRights, AccessControlType | Format-Table -AutoSize
'owner: ' + $acl.Owner

'--- try MpCmdRun -RemoveDefinitions? no: try service control ---'
& sc.exe stop WinDefend 2>&1 | Select-Object -First 3
Start-Sleep -Seconds 3
"value while stopped: " + (Get-ItemProperty $k -ErrorAction SilentlyContinue).DisableRealtimeMonitoring
& sc.exe start WinDefend 2>&1 | Select-Object -First 3
Start-Sleep -Seconds 6
"value after start: " + (Get-ItemProperty $k -ErrorAction SilentlyContinue).DisableRealtimeMonitoring
(Get-MpComputerStatus).RealTimeProtectionEnabled
