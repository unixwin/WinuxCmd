# Is the Explorer-path hang tied to the file NAME "sort.exe" (a shell/App Paths
# resolution problem) or to the payload content?
$ErrorActionPreference = 'Continue'
if (-not ('Perf1396.SpawnBench' -as [type])) {
    Add-Type -Path (Join-Path $PSScriptRoot 'SpawnBench.cs')
}
$dir = 'D:\repo\wc-av1396\DOCS\perf-1396\raw\payload\unsigned'
Copy-Item "$dir\sort.exe" 'D:\repo\wc-av1396\DOCS\perf-1396\raw\payload\unsigned\sortx.exe' -Force
foreach ($n in 'ls.exe', 'sortx.exe', 'sort.exe') {
    $exe = Join-Path $dir $n
    $s = [Perf1396.SpawnBench]::SpawnShellExecute($exe, '--version', 0, $true, 6000)
    "{0,-12} started={1} timedout={2} ms={3:N0} err={4}" -f $n, $s.Started, $s.TimedOut, $s.Ms, $s.Error
}
Remove-Item 'D:\repo\wc-av1396\DOCS\perf-1396\raw\payload\unsigned\sortx.exe' -Force -ErrorAction SilentlyContinue
