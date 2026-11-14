# Look for MDM/provisioning policy sources that pin Defender real-time protection.
$ErrorActionPreference = 'Continue'
$roots = @(
    'HKLM:\SOFTWARE\Microsoft\PolicyManager\current\device\Defender',
    'HKLM:\SOFTWARE\Microsoft\PolicyManager\providers\default\default\Defender',
    'HKLM:\SOFTWARE\Microsoft\Windows Defender\PolicyManager'
)
foreach ($r in $roots) {
    if (Test-Path $r) { "== $r"; Get-ItemProperty $r | Format-List } else { "== $r (absent)" }
}
'--- any AllowRealtimeMonitoring under PolicyManager ---'
Get-ChildItem 'HKLM:\SOFTWARE\Microsoft\PolicyManager' -Recurse -ErrorAction SilentlyContinue |
    ForEach-Object {
        $p = Get-ItemProperty $_.PSPath -ErrorAction SilentlyContinue
        $p.PSObject.Properties | Where-Object { $_.Name -match 'Realtime|RealTime|AllowRealtime' } |
            ForEach-Object { "$($_.Name)=$($_.Value)  @ $($_.PSPath)" }
    }
'--- provisioning packages ---'
Get-CimInstance -Namespace root/cimv2/mdm/dmmap -ClassName MDM_DevDetail_Ext01 -ErrorAction SilentlyContinue |
    Select-Object DeviceHardwareData | Out-Null
& "$env:SystemRoot\System32\provtool.exe" /? 2>&1 | Select-Object -First 3
'--- gpupdate ---'
& gpupdate.exe /force 2>&1 | Select-Object -Last 5
(Get-MpComputerStatus).RealTimeProtectionEnabled
