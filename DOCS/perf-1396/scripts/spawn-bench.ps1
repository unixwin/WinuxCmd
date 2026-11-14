<#
.SYNOPSIS
  Measure spawn latency of one leg of the issue #1396 matrix.

.DESCRIPTION
  One "leg" is a (binary, variant, AV condition, cold/warm, spawn API) tuple.
  The script spawns the image -Samples times through the native helper in
  SpawnBench.cs, records every sample in -OutCsv, and prints one summary line.

  Two notions of "cold" are supported, because they answer different questions:

    -State warm    repeated spawn of one stable path. This is "the user types
                   ls again in the same terminal" and is the state in which the
                   reported 0.9 s pathology persists.

    -State cold    each sample spawns a *fresh byte copy* of the image in a new
                   directory (same file name, so the multi-call dispatcher still
                   resolves the applet). New path + new file identity means any
                   per-file AV verdict cache is cold, which is the state right
                   after an install or an upgrade.

    -PrepMode newhash
                   like cold, but each copy also gets a trailing pad byte, so
                   the content hash is new as well. This separates "AV caches by
                   path/file id" from "AV caches by content hash".

    -PrepMode purge
                   strong cold: trim the system file cache and purge the
                   standby list before each sample (needs elevation, costs
                   seconds per sample on a 64 GB host) - only used for a small
                   probe.

.PARAMETER Exe
  Full path of the image to spawn.

.PARAMETER Args
  Arguments passed to the image (kept identical across legs).

.PARAMETER State
  warm | cold.

.PARAMETER Api
  createprocess (shell/terminal path) | shellexecute (Explorer path).

.PARAMETER PrepMode
  none | trim | purge | recopy | newhash. Defaults from -State.

.PARAMETER Warmup
  Discarded spawns before sampling (default 3).
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [string]$Arguments = '--version',
    [Parameter(Mandatory = $true)][string]$Binary,
    [Parameter(Mandatory = $true)][string]$Variant,
    [Parameter(Mandatory = $true)][string]$Condition,
    [ValidateSet('warm', 'cold')][string]$State = 'warm',
    [ValidateSet('createprocess', 'shellexecute')][string]$Api = 'createprocess',
    [ValidateSet('none', 'trim', 'purge', 'recopy', 'newhash')][string]$PrepMode = '',
    [int]$Samples = 50,
    [int]$Warmup = 3,
    [int]$TimeoutMs = 15000,
    [string]$OutCsv = '',
    [string]$ColdRoot = '',
    [string]$Notes = '',
    [switch]$RequireElevated
)

$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot

if (-not $PrepMode) { $PrepMode = if ($State -eq 'cold') { 'recopy' } else { 'none' } }
if (-not $OutCsv) { $OutCsv = Join-Path $here '..\raw\samples.csv' }
if (-not $ColdRoot) { $ColdRoot = Join-Path $here '..\raw\coldwork' }
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutCsv) | Out-Null

if (-not ('Perf1396.SpawnBench' -as [type])) {
    Add-Type -Path (Join-Path $here 'SpawnBench.cs')
}

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if ($RequireElevated -and -not $isAdmin) { throw 'this leg needs an elevated token; run through run-elevated.ps1' }

$privLog = [Perf1396.SpawnBench]::PrepareColdSupport()
if ($PrepMode -in 'trim', 'purge' -and $privLog -notmatch 'SeIncreaseQuotaPrivilege=True') {
    throw "cold prep privileges unavailable ($privLog); refusing to label paged-in numbers as cold"
}

if (-not (Test-Path -LiteralPath $Exe)) { throw "missing image: $Exe" }
$prepId = @{ none = 0; trim = 1; purge = 2; recopy = 0; newhash = 0 }[$PrepMode]
$usesCopy = $PrepMode -in 'recopy', 'newhash'

function Invoke-Spawn([string]$Path) {
    if ($Api -eq 'createprocess') {
        [Perf1396.SpawnBench]::SpawnCreateProcess($Path, $Arguments, $prepId, $TimeoutMs)
    } else {
        [Perf1396.SpawnBench]::SpawnShellExecute($Path, $Arguments, $prepId, $true, $TimeoutMs)
    }
}

# Warmup spawns are never recorded: they populate Defender's scan cache and the
# loader's prefetch entry exactly like real usage does. For copy legs the warmup
# runs on the stable image so it cannot pre-warm the fresh copies.
for ($i = 0; $i -lt $Warmup; $i++) {
    [void](Invoke-Spawn $Exe)
}

$legDir = Join-Path $ColdRoot ("{0}-{1}-{2}-{3}" -f $Condition, $Variant, $Binary, $Api)
if ($usesCopy) {
    if (Test-Path -LiteralPath $legDir) { Remove-Item -LiteralPath $legDir -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $legDir | Out-Null
}

$rows = [System.Collections.Generic.List[object]]::new()
$failures = 0
$timeouts = 0
$srcBytes = [IO.File]::ReadAllBytes($Exe)
$padByte = [byte]0x41

for ($i = 0; $i -lt $Samples; $i++) {
    $target = $Exe
    $copyMs = 0
    if ($usesCopy) {
        $dir = Join-Path $legDir ('s{0:D4}' -f $i)
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
        $target = Join-Path $dir ([IO.Path]::GetFileName($Exe))
        $swCopy = [Diagnostics.Stopwatch]::StartNew()
        if ($PrepMode -eq 'newhash') {
            $bytes = New-Object byte[] ($srcBytes.Length + 1)
            [Array]::Copy($srcBytes, $bytes, $srcBytes.Length)
            $bytes[$srcBytes.Length] = [byte]($padByte + ($i % 64))
            [IO.File]::WriteAllBytes($target, $bytes)
        } else {
            [IO.File]::WriteAllBytes($target, $srcBytes)
        }
        $swCopy.Stop()
        $copyMs = [math]::Round($swCopy.Elapsed.TotalMilliseconds, 3)
    }

    $sw = [Diagnostics.Stopwatch]::StartNew()
    $s = Invoke-Spawn $target
    $sw.Stop()
    if (-not $s.Started) { $failures++ }
    if ($s.TimedOut) { $timeouts++ }

    $rows.Add([pscustomobject]@{
        utc       = (Get-Date).ToUniversalTime().ToString('o')
        condition = $Condition
        variant   = $Variant
        binary    = $Binary
        api       = $Api
        state     = $State
        prep      = $PrepMode
        sample    = $i
        ms        = [math]::Round($s.Ms, 3)
        prep_ms   = if ($usesCopy) { $copyMs } else { [math]::Round($s.PrepMs, 3) }
        wall_ms   = [math]::Round($sw.Elapsed.TotalMilliseconds, 3)
        exit_code = $s.ExitCode
        started   = $s.Started
        timed_out = $s.TimedOut
        error     = $s.Error
        exe       = $target
        args      = $Arguments
        notes     = $Notes
    })
}

if ($usesCopy) {
    # Copies are 4.6 MB each; keeping 50 per leg per condition is pointless.
    Remove-Item -LiteralPath $legDir -Recurse -Force -ErrorAction SilentlyContinue
}

if (Test-Path -LiteralPath $OutCsv) {
    $rows | Export-Csv -NoTypeInformation -Encoding UTF8 -Append -Path $OutCsv
} else {
    $rows | Export-Csv -NoTypeInformation -Encoding UTF8 -Path $OutCsv
}

$ms = $rows | Where-Object { $_.timed_out -ne $true -and $_.started } | ForEach-Object { [double]$_.ms }
$sorted = @($ms | Sort-Object)
$n = $sorted.Count
if ($n -eq 0) {
    Write-Warning "no successful samples for $Condition/$Variant/$Binary/$Api"
    return
}
$median = if ($n % 2) { $sorted[[int](($n - 1) / 2)] } else { ($sorted[$n / 2 - 1] + $sorted[$n / 2]) / 2 }
$p95 = $sorted[[math]::Min($n - 1, [int][math]::Ceiling(0.95 * $n) - 1)]
$prepSorted = @($rows | ForEach-Object { [double]$_.prep_ms } | Sort-Object)
$prepMed = if ($prepSorted.Count) { $prepSorted[[int]($prepSorted.Count / 2)] } else { 0 }

Write-Host ("{0,-18} {1,-14} {2,-6} {3,-13} {4,-5} n={5,-3} median={6,8:N2} p95={7,8:N2} min={8,7:N2} max={9,8:N2} prep_med={10,7:N1} fail={11} timeout={12}" -f `
        $Condition, $Variant, $Binary, $Api, $State, $n, $median, $p95, $sorted[0], $sorted[$n - 1], $prepMed, $failures, $timeouts)
