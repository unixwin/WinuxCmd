# What does the engine cost on a *cold* verdict (new path, new content hash)?
#
# The cached-verdict on-demand scan is ~25 ms (see scan-ondemand.ps1), which only
# measures the CLI plus a cache hit. This script copies each image to a fresh
# directory and appends a pad byte so both the path and the content hash are new,
# then asks MpCmdRun to scan it. That is the cost a real-time monitor pays when
# its verdict cache does not cover the image - the situation right after an
# install, an upgrade, or any AV whose cache is invalidated per run.
$ErrorActionPreference = 'Stop'
$mp = Join-Path $env:ProgramFiles 'Windows Defender\MpCmdRun.exe'
$root = 'D:\repo\wc-av1396\DOCS\perf-1396\raw\payload'
$work = 'D:\repo\wc-av1396\DOCS\perf-1396\raw\coldscan'
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory -Force -Path $work | Out-Null
$out = 'D:\repo\wc-av1396\DOCS\perf-1396\raw\coldscan.csv'
$samples = 5
$rows = [System.Collections.Generic.List[object]]::new()

$targets = @(
    @{ variant = 'unsigned';      bin = 'ls';  src = Join-Path $root 'unsigned\ls.exe' },
    @{ variant = 'signed';        bin = 'ls';  src = Join-Path $root 'signed\ls.exe' },
    @{ variant = 'motw-unsigned'; bin = 'ls';  src = Join-Path $root 'motw-unsigned\ls.exe' },
    @{ variant = 'motw-signed';   bin = 'ls';  src = Join-Path $root 'motw-signed\ls.exe' },
    @{ variant = 'unsigned';      bin = 'git'; src = Join-Path $root 'unsigned\git.exe' },
    @{ variant = 'signed';        bin = 'git'; src = Join-Path $root 'signed\git.exe' },
    # Reference: an OS binary Defender already knows.
    @{ variant = 'microsoft-signed'; bin = 'whoami'; src = Join-Path $env:SystemRoot 'System32\whoami.exe' }
)

foreach ($t in $targets) {
    for ($i = 0; $i -lt $samples; $i++) {
        $dir = Join-Path $work ('{0}-{1}-{2}' -f $t.variant, $t.bin, $i)
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
        $dst = Join-Path $dir ([IO.Path]::GetFileName($t.src))
        $bytes = [IO.File]::ReadAllBytes($t.src)
        $padded = New-Object byte[] ($bytes.Length + 1)
        [Array]::Copy($bytes, $padded, $bytes.Length)
        $padded[$bytes.Length] = [byte](0x40 + $i + 1)
        [IO.File]::WriteAllBytes($dst, $padded)
        Start-Sleep -Milliseconds 200
        $sw = [Diagnostics.Stopwatch]::StartNew()
        $text = & $mp -Scan -ScanType 3 -File $dst -DisableRemediation 2>&1 | Out-String
        $sw.Stop()
        $rows.Add([pscustomobject]@{
            utc = (Get-Date).ToUniversalTime().ToString('o'); condition = 'coldscan'
            variant = $t.variant; binary = $t.bin; sample = $i
            ms = [math]::Round($sw.Elapsed.TotalMilliseconds, 1)
            verdict = if ($text -match 'no threats|Scan completed|find no threat') { 'clean' } else { 'other' }
            exe = $dst; raw = ($text -replace '\s+', ' ').Trim()
        })
        Remove-Item $dir -Recurse -Force -ErrorAction SilentlyContinue
    }
    $ms = @($rows | Where-Object { $_.variant -eq $t.variant -and $_.binary -eq $t.bin } |
            ForEach-Object { [double]$_.ms } | Sort-Object)
    "{0,-16} {1,-6} cold-scan n={2} median={3,7:N1} p95={4,7:N1} min={5,7:N1}" -f `
        $t.variant, $t.bin, $ms.Count, $ms[[int]($ms.Count / 2)], $ms[-1], $ms[0]
}
$rows | Export-Csv -NoTypeInformation -Encoding UTF8 -Path $out
Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
"rows -> $out"
