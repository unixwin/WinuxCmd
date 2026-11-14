<#
.SYNOPSIS
  Aggregate the issue #1396 raw samples into leg statistics and markdown tables.

.DESCRIPTION
  samples.csv holds one row per spawn; this script computes n / median / P95 /
  min / max / mean / stdev per leg, writes legs.csv, and renders the markdown
  tables used in the issue comment and in DOCS/perf-1396/README.md.

  P95 uses the nearest-rank definition on the sorted sample set (the value at
  ceil(0.95*n)), the conservative choice for a 50-sample leg. Rows whose spawn
  was blocked (timed_out=True) are counted separately and excluded from the
  latency statistics: folding an 8 s prompt into a median would invent a number.
#>
[CmdletBinding()]
param(
    [string]$SamplesCsv = '',
    [string]$OnDemandCsv = '',
    [string]$OutDir = ''
)

$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
if (-not $SamplesCsv) { $SamplesCsv = Join-Path $here '..\raw\samples.csv' }
if (-not $OnDemandCsv) { $OnDemandCsv = Join-Path $here '..\raw\ondemand.csv' }
if (-not $OutDir) { $OutDir = Join-Path $here '..\raw' }

function Get-Stats {
    param($Values)
    $v = @($Values | ForEach-Object { [double]$_ } | Sort-Object)
    $n = $v.Count
    if ($n -eq 0) { return $null }
    $median = if ($n % 2) { $v[[int](($n - 1) / 2)] } else { ($v[$n / 2 - 1] + $v[$n / 2]) / 2 }
    $p95 = $v[[math]::Min($n - 1, [int][math]::Ceiling(0.95 * $n) - 1)]
    $mean = ($v | Measure-Object -Average).Average
    $sd = 0
    if ($n -gt 1) {
        $sum = 0
        foreach ($x in $v) { $sum += ($x - $mean) * ($x - $mean) }
        $sd = [math]::Sqrt($sum / ($n - 1))
    }
    [pscustomobject]@{
        n = $n; median = [math]::Round($median, 2); p95 = [math]::Round($p95, 2)
        min = [math]::Round($v[0], 2); max = [math]::Round($v[$n - 1], 2)
        mean = [math]::Round($mean, 2); stdev = [math]::Round($sd, 2)
    }
}

$samples = Import-Csv -LiteralPath $SamplesCsv
$legs = foreach ($g in ($samples | Group-Object condition, variant, binary, api, state, prep)) {
    $ok = @($g.Group | Where-Object { $_.timed_out -ne 'True' -and $_.started -eq 'True' })
    $s = Get-Stats -Values ($ok | ForEach-Object { $_.ms })
    if (-not $s) {
        $s = [pscustomobject]@{ n = 0; median = ''; p95 = ''; min = ''; max = ''; mean = ''; stdev = '' }
    }
    [pscustomobject]@{
        condition = $g.Group[0].condition
        variant   = $g.Group[0].variant
        binary    = $g.Group[0].binary
        api       = $g.Group[0].api
        state     = $g.Group[0].state
        prep      = $g.Group[0].prep
        n         = $s.n
        median_ms = $s.median
        p95_ms    = $s.p95
        min_ms    = $s.min
        max_ms    = $s.max
        mean_ms   = $s.mean
        stdev_ms  = $s.stdev
        blocked   = @($g.Group | Where-Object { $_.timed_out -eq 'True' }).Count
        notes     = $g.Group[0].notes
    }
}
$legsCsv = Join-Path $OutDir 'legs.csv'
$legs | Sort-Object condition, variant, state, api, binary | Export-Csv -NoTypeInformation -Encoding UTF8 -Path $legsCsv
Write-Host "legs: $legsCsv ($($legs.Count) legs)"

$md = [System.Collections.Generic.List[string]]::new()
function Add-Table {
    param([string]$Title, [object[]]$Rows)
    if (-not $Rows) { return }
    $cols = @('variant', 'binary', 'n', 'median_ms', 'p95_ms', 'min_ms', 'max_ms')
    $md.Add('')
    $md.Add("#### $Title")
    $md.Add('')
    $md.Add('| ' + ($cols -join ' | ') + ' |')
    $md.Add('|' + (($cols | ForEach-Object { '---' }) -join '|') + '|')
    foreach ($r in $Rows) {
        $md.Add('| ' + (($cols | ForEach-Object { $r.$_ }) -join ' | ') + ' |')
    }
}

foreach ($cond in ($legs | Select-Object -ExpandProperty condition -Unique | Sort-Object)) {
    $md.Add('')
    $md.Add("## Condition: $cond")
    Add-Table -Title 'Warm, CreateProcessW (ms)' -Rows (
        $legs | Where-Object { $_.condition -eq $cond -and $_.state -eq 'warm' -and $_.api -eq 'createprocess' -and $_.prep -eq 'none' } |
            Sort-Object variant, binary)
    Add-Table -Title 'Cold: fresh copy at a new path, CreateProcessW (ms)' -Rows (
        $legs | Where-Object { $_.condition -eq $cond -and $_.prep -eq 'recopy' } | Sort-Object variant, binary)
    Add-Table -Title 'Cold + new content hash, CreateProcessW (ms)' -Rows (
        $legs | Where-Object { $_.condition -eq $cond -and $_.prep -eq 'newhash' } | Sort-Object variant, binary)
    Add-Table -Title 'Warm, ShellExecuteEx (Explorer path, ms)' -Rows (
        $legs | Where-Object { $_.condition -eq $cond -and $_.api -eq 'shellexecute' } | Sort-Object variant, binary)
}

# Deltas that answer the question directly.
$md.Add('')
$md.Add('## Deltas')
$md.Add('')
$md.Add('| condition | binary | unsigned warm | signed warm | signed - unsigned | cold (copy) | cold - warm | newhash | newhash - warm |')
$md.Add('|---|---|---|---|---|---|---|---|---|')
foreach ($cond in ($legs | Select-Object -ExpandProperty condition -Unique | Sort-Object)) {
    foreach ($b in 'ls', 'which', 'sort', 'git') {
        $u = $legs | Where-Object { $_.condition -eq $cond -and $_.binary -eq $b -and $_.state -eq 'warm' -and $_.prep -eq 'none' -and $_.api -eq 'createprocess' -and $_.variant -eq 'unsigned' }
        if (-not $u) { continue }
        $s = $legs | Where-Object { $_.condition -eq $cond -and $_.binary -eq $b -and $_.state -eq 'warm' -and $_.prep -eq 'none' -and $_.api -eq 'createprocess' -and $_.variant -eq 'signed' }
        $c = $legs | Where-Object { $_.condition -eq $cond -and $_.binary -eq $b -and $_.prep -eq 'recopy' -and $_.variant -eq 'unsigned' }
        $h = $legs | Where-Object { $_.condition -eq $cond -and $_.binary -eq $b -and $_.prep -eq 'newhash' -and $_.variant -eq 'unsigned' }
        function D($a, $b) { if ($null -eq $a -or $null -eq $b -or $a -eq '' -or $b -eq '') { '' } else { [math]::Round([double]$a - [double]$b, 2) } }
        $md.Add(('| {0} | {1} | {2} | {3} | {4} | {5} | {6} | {7} | {8} |' -f $cond, $b, $u.median_ms,
                $(if ($s) { $s.median_ms } else { '' }), (D $s.median_ms $u.median_ms),
                $(if ($c) { $c.median_ms } else { '' }), (D $c.median_ms $u.median_ms),
                $(if ($h) { $h.median_ms } else { '' }), (D $h.median_ms $u.median_ms)))
    }
}

if (Test-Path -LiteralPath $OnDemandCsv) {
    $od = Import-Csv -LiteralPath $OnDemandCsv
    $odLegs = foreach ($g in ($od | Group-Object condition, variant, binary)) {
        $s = Get-Stats -Values ($g.Group | ForEach-Object { $_.ms })
        [pscustomobject]@{
            condition = $g.Group[0].condition
            variant   = $g.Group[0].variant
            binary    = $g.Group[0].binary
            n = $s.n; median_ms = $s.median; p95_ms = $s.p95
            min_ms = $s.min; max_ms = $s.max
            verdict = ($g.Group | Select-Object -First 1).verdict
        }
    }
    $odLegs | Sort-Object condition, variant, binary | Export-Csv -NoTypeInformation -Encoding UTF8 `
        -Path (Join-Path $OutDir 'ondemand-legs.csv')
    foreach ($cond in ($odLegs | Select-Object -ExpandProperty condition -Unique | Sort-Object)) {
        Add-Table -Title "On-demand MpCmdRun scan, cached verdict ($cond, ms)" -Rows (
            $odLegs | Where-Object { $_.condition -eq $cond } | Sort-Object variant, binary)
    }
}

$mdPath = Join-Path $OutDir 'summary.md'
$md | Set-Content -Encoding UTF8 -LiteralPath $mdPath
Write-Host "summary: $mdPath"
