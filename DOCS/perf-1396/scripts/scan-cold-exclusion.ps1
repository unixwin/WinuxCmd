# Does a Defender path exclusion actually remove the cold scan?
#
# Run three phases, each on a fresh copy (new path, new content hash):
#   no-exclusion    path outside every exclusion
#   excluded        same kind of path, inside an exclusion added for this test
# The exclusion is added and removed by the caller (defender-state.ps1), because
# it needs an elevated token.
$ErrorActionPreference = 'Stop'
$mp = Join-Path $env:ProgramFiles 'Windows Defender\MpCmdRun.exe'
$src = 'D:\repo\wc-av1396\DOCS\perf-1396\raw\payload\unsigned\ls.exe'
$out = Join-Path $PSScriptRoot '..\raw\coldscan-exclusion.csv'
$samples = 3
$phase = $args[0]
$work = if ($args.Count -gt 1) { $args[1] } else { 'D:\repo\wc-av1396\DOCS\perf-1396\raw\coldscan-plain' }

$bytes = [IO.File]::ReadAllBytes($src)
$rows = [System.Collections.Generic.List[object]]::new()
for ($i = 0; $i -lt $samples; $i++) {
    $dir = Join-Path $work ('s{0}' -f $i)
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $dst = Join-Path $dir 'ls.exe'
    $padded = New-Object byte[] ($bytes.Length + 1)
    [Array]::Copy($bytes, $padded, $bytes.Length)
    $padded[$bytes.Length] = [byte](0x50 + $i)
    [IO.File]::WriteAllBytes($dst, $padded)
    Start-Sleep -Milliseconds 200
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $text = & $mp -Scan -ScanType 3 -File $dst -DisableRemediation 2>&1 | Out-String
    $sw.Stop()
    $rows.Add([pscustomobject]@{
        utc = (Get-Date).ToUniversalTime().ToString('o'); phase = $phase
        sample = $i; ms = [math]::Round($sw.Elapsed.TotalMilliseconds, 1)
        exe = $dst; raw = ($text -replace '\s+', ' ').Trim()
    })
    Remove-Item $dir -Recurse -Force -ErrorAction SilentlyContinue
}
if (Test-Path $out) { $rows | Export-Csv -NoTypeInformation -Encoding UTF8 -Append -Path $out }
else { $rows | Export-Csv -NoTypeInformation -Encoding UTF8 -Path $out }
$ms = @($rows | ForEach-Object { [double]$_.ms } | Sort-Object)
"{0,-14} n={1} median={2:N1} min={3:N1} max={4:N1}" -f $phase, $ms.Count, $ms[[int]($ms.Count / 2)], $ms[0], $ms[-1]
Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
