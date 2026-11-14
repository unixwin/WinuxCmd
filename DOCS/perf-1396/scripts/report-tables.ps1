<#
.SYNOPSIS
  Render the issue #1396 result tables from legs.csv (both runs side by side).

.DESCRIPTION
  Produces raw/report-tables.md, which is embedded verbatim in
  DOCS/perf-1396/README.md and in the issue comment. Every cell is copied from
  legs.csv, so the document and the raw data cannot drift apart.

  The two runs are shown side by side on purpose: the host is shared with other
  build lanes, so a single run's median can carry contention. Where the two runs
  disagree by more than ~20% the cell is marked with a dagger and the lower of
  the two is used as the uncontended estimate.
#>
[CmdletBinding()]
param(
    [string]$LegsCsv = '',
    [string]$OutFile = ''
)

$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
if (-not $LegsCsv) { $LegsCsv = Join-Path $here '..\raw\legs.csv' }
if (-not $OutFile) { $OutFile = Join-Path $here '..\raw\report-tables.md' }

$legs = Import-Csv -LiteralPath $LegsCsv
$runA = 'rtp-off-quiet'
$runB = 'rtp-off'

function Get-Leg([string]$cond, [string]$variant, [string]$binary, [string]$state, [string]$prep = 'none', [string]$api = 'createprocess') {
    $legs | Where-Object {
        $_.condition -eq $cond -and $_.variant -eq $variant -and $_.binary -eq $binary -and
        $_.state -eq $state -and $_.prep -eq $prep -and $_.api -eq $api
    } | Select-Object -First 1
}

$out = [System.Collections.Generic.List[string]]::new()
function L([string]$s = '') { $out.Add($s) }

function Fmt($v) {
    if ($null -eq $v -or $v -eq '') { return 'n/a' }
    return ([double]$v).ToString('0.0')
}

function Mark($a, $b) {
    if ($null -eq $a -or $null -eq $b -or $a -eq '' -or $b -eq '') { return '' }
    $x = [double]$a; $y = [double]$b
    if ($x -le 0) { return '' }
    if ([math]::Abs($x - $y) / [math]::Max($x, $y) -gt 0.2) { return ' *' }
    return ''
}

L('### Warm spawn, CreateProcessW: median / P95 (ms)')
L('')
L('| variant | applet | run 1 median | run 1 P95 | run 2 median | run 2 P95 | notes |')
L('|---|---|---|---|---|---|---|')
foreach ($variant in 'installed', 'microsoft-signed', 'unsigned', 'signed', 'motw-unsigned', 'motw-signed') {
    $bins = if ($variant -eq 'microsoft-signed') { @('whoami') } else { @('ls', 'which', 'sort', 'git') }
    foreach ($b in $bins) {
        $a = Get-Leg $runA $variant $b 'warm'
        if (-not $a) { continue }
        $bb = Get-Leg $runB $variant $b 'warm'
        $note = switch ($variant) {
            'installed' { 'on PATH, inside the existing Defender exclusions' }
            'microsoft-signed' { 'OS binary, control' }
            'unsigned' { 'local build, no signature' }
            'signed' { 'local build + trusted test cert' }
            'motw-unsigned' { 'unsigned + Zone.Identifier' }
            'motw-signed' { 'signed + Zone.Identifier' }
        }
        L(('| {0} | {1} | {2} | {3} | {4} | {5} | {6}{7} |' -f $variant, $b, (Fmt $a.median_ms), (Fmt $a.p95_ms),
                (Fmt $bb.median_ms), (Fmt $bb.p95_ms), $note, (Mark $a.median_ms $bb.median_ms)))
    }
}

L('')
L('### Cold (fresh byte copy at a new path) and new content hash: median (ms)')
L('')
L('| variant | applet | cold run 1 | cold run 2 | newhash run 1 | newhash run 2 | cold - warm (run 1) |')
L('|---|---|---|---|---|---|---|')
foreach ($variant in 'unsigned', 'signed', 'motw-unsigned', 'motw-signed') {
    foreach ($b in 'ls', 'which', 'sort', 'git') {
        $warm = Get-Leg $runA $variant $b 'warm'
        $c1 = Get-Leg $runA $variant $b 'cold' 'recopy'
        $c2 = Get-Leg $runB $variant $b 'cold' 'recopy'
        $h1 = Get-Leg $runA $variant $b 'cold' 'newhash'
        $h2 = Get-Leg $runB $variant $b 'cold' 'newhash'
        if (-not $c1) { continue }
        $delta = if ($warm) { Fmt ([double]$c1.median_ms - [double]$warm.median_ms) } else { 'n/a' }
        L(('| {0} | {1} | {2} | {3} | {4} | {5} | {6} |' -f $variant, $b, (Fmt $c1.median_ms), (Fmt $c2.median_ms),
                (Fmt $h1.median_ms), (Fmt $h2.median_ms), $delta))
    }
}

L('')
L('### Explorer path (ShellExecuteEx), warm, 10 samples per leg: median / P95 (ms)')
L('')
L('| variant | applet | median | P95 | blocked |')
L('|---|---|---|---|---|')
foreach ($variant in 'unsigned', 'signed', 'motw-unsigned', 'motw-signed') {
    foreach ($b in 'ls', 'which', 'sort', 'git') {
        $s = Get-Leg $runA $variant $b 'warm' 'none' 'shellexecute'
        if (-not $s) { continue }
        L(('| {0} | {1} | {2} | {3} | {4} |' -f $variant, $b, (Fmt $s.median_ms), (Fmt $s.p95_ms), $s.blocked))
    }
}

$odPath = Join-Path $here '..\raw\ondemand-legs.csv'
if (Test-Path $odPath) {
    $od = Import-Csv -LiteralPath $odPath
    L('')
    L('### Defender engine scan cost per image (MpCmdRun)')
    L('')
    L('| variant | image | cached verdict, median (ms) | cached verdict, excluded path (ms) | cold verdict (new path+hash), median (ms) |')
    L('|---|---|---|---|---|')
    $cold = @{}
    $coldCsv = Join-Path $here '..\raw\coldscan.csv'
    if (Test-Path $coldCsv) {
        foreach ($g in ((Import-Csv $coldCsv) | Group-Object variant, binary)) {
            $ms = @($g.Group | ForEach-Object { [double]$_.ms } | Sort-Object)
            $cold[$g.Name] = $ms[[int]($ms.Count / 2)]
        }
    }
    foreach ($row in ($od | Where-Object { $_.condition -eq 'rtp-off' } | Sort-Object variant, binary)) {
        $excl = $od | Where-Object { $_.condition -eq 'rtp-off-excluded' -and $_.variant -eq $row.variant -and $_.binary -eq $row.binary }
        $key = "$($row.variant), $($row.binary)"
        L(('| {0} | {1} | {2} | {3} | {4} |' -f $row.variant, $row.binary, (Fmt $row.median_ms),
                (Fmt $excl.median_ms), (Fmt $cold[$key])))
    }
    $coldOnly = Join-Path $here '..\raw\coldscan.csv'
    if (Test-Path $coldOnly) {
        $ref = ((Import-Csv $coldOnly) | Where-Object { $_.variant -eq 'microsoft-signed' })
        if ($ref) {
            $ms = @($ref | ForEach-Object { [double]$_.ms } | Sort-Object)
            L(('| microsoft-signed | whoami | n/a | n/a | {0} |' -f (Fmt $ms[[int]($ms.Count / 2)])))
        }
    }
}

$out -join "`r`n" | Set-Content -Encoding UTF8 -LiteralPath $OutFile
Write-Host "wrote $OutFile ($($out.Count) lines)"
