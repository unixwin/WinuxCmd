# Deeper diagnosis: why does Defender refuse to re-enable real-time protection?
$ErrorActionPreference = 'Continue'
'--- policy tree ---'
Get-ChildItem 'HKLM:\SOFTWARE\Policies\Microsoft\Windows Defender' -Recurse -ErrorAction SilentlyContinue |
    ForEach-Object {
        $k = $_
        $props = Get-ItemProperty $k.PSPath -ErrorAction SilentlyContinue
        $names = ($props.PSObject.Properties | Where-Object { $_.Name -notlike 'PS*' } |
            ForEach-Object { "$($_.Name)=$($_.Value)" }) -join ', '
        "{0} :: {1}" -f $k.PSPath.Replace('Microsoft.PowerShell.Core\Registry::HKEY_LOCAL_MACHINE', 'HKLM'), $names
    }
'--- effective preference ---'
Get-MpPreference | Select-Object DisableRealtimeMonitoring, DisableBehaviorMonitoring,
    DisableIOAVProtection, DisableOnAccessProtection, DisableScriptScanning, DisableArchiveScanning | Format-List
'--- status ---'
Get-MpComputerStatus | Select-Object RealTimeProtectionEnabled, BehaviorMonitorEnabled,
    AMRunningMode, AMServiceEnabled, AntivirusEnabled, IsTamperProtected, TamperProtectionSource | Format-List
'--- defender operational log (last 15) ---'
Get-WinEvent -LogName 'Microsoft-Windows-Windows Defender/Operational' -MaxEvents 15 -ErrorAction SilentlyContinue |
    Select-Object TimeCreated, Id, @{ n = 'msg'; e = { ($_.Message -split "`n")[0] } } | Format-Table -AutoSize -Wrap
'--- sc qc ---'
& sc.exe qc WinDefend
'--- try enable via MpCmdRun ---'
& "$env:ProgramFiles\Windows Defender\MpCmdRun.exe" -? 2>&1 | Select-Object -First 5
