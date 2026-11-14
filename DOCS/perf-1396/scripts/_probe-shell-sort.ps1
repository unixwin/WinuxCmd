# Why does the Explorer-path spawn of payload sort.exe hang while ls/which/git
# do not? Start the spawn, then look at what the OS actually created.
$ErrorActionPreference = 'Continue'
if (-not ('Perf1396.SpawnBench' -as [type])) {
    Add-Type -Path (Join-Path $PSScriptRoot 'SpawnBench.cs')
}
$exe = 'D:\repo\wc-av1396\DOCS\perf-1396\raw\payload\unsigned\sort.exe'
$job = Start-Job -ScriptBlock {
    param($exe)
    Add-Type -Path 'D:\repo\wc-av1396\DOCS\perf-1396\scripts\SpawnBench.cs'
    [Perf1396.SpawnBench]::SpawnShellExecute($exe, '--version', 0, $true, 20000)
} -ArgumentList $exe
Start-Sleep -Seconds 3
'--- processes named sort/winuxcmd ---'
Get-CimInstance Win32_Process -Filter "Name='sort.exe' or Name='winuxcmd.exe'" |
    Select-Object ProcessId, ParentProcessId, Name, ExecutablePath, CommandLine | Format-List
'--- everyone who has sort.exe open ---'
Get-Process sort -ErrorAction SilentlyContinue | Select-Object Id, Path | Format-Table -AutoSize
Stop-Job $job -ErrorAction SilentlyContinue
Remove-Job $job -Force -ErrorAction SilentlyContinue
