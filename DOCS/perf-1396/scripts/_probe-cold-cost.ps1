# Measures the cost of each cold-preparation primitive, once each, so the
# orchestrator can choose one that is honest and affordable.
# Runs elevated (see run-elevated.ps1).
$ErrorActionPreference = 'Stop'
if (-not ('Perf1396.SpawnBench' -as [type])) {
    Add-Type -Path (Join-Path $PSScriptRoot 'SpawnBench.cs')
}
$id = [Security.Principal.WindowsIdentity]::GetCurrent()
"IsAdmin=" + ([Security.Principal.WindowsPrincipal]$id).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
"Privileges=" + [Perf1396.SpawnBench]::PrepareColdSupport()

$t = [Perf1396.SpawnBench]::TrimFileCache()
"TrimFileCache #1 = {0:N1} ms" -f $t
$t = [Perf1396.SpawnBench]::TrimFileCache()
"TrimFileCache #2 = {0:N1} ms" -f $t

$p = [Perf1396.SpawnBench]::PurgeStandby()
"PurgeStandby #1 = {0:N1} ms" -f $p
