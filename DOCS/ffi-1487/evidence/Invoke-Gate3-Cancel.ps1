<#
.SYNOPSIS
  Reproduce niubash#727 gate ③ for unixwin/WinuxCmd#1487 defect B:
  a long FFI dispatch does not poll the host cancellation callback.

.DESCRIPTION
  Drives a real interactive niu through ConPTY (pywinpty), starts
  `sort big.txt` (3,000,000 lines / 36,000,000 B), injects a genuine ^C
  (0x03) after -CancelAfter seconds, and measures how long the shell takes
  to answer a queued probe.  -Expect broken reproduces the 2026-11-01
  result (+9.01 s after ^C, c_early=false); -Expect fixed is the revival
  gate (< 2 s, cancellation reported).

  The optional -IncludeFlood phase reruns the original
  `sort big.txt > /dev/null` disaster: the redirect is ignored, ~36 MB is
  written to the console, and ^C does not restore the prompt for ~180 s.
  The ConPTY stream is counted and only its tail is persisted.

.PARAMETER NiuExe
  Path to a 1.4.x niu.exe.  niubash 1.5.0 removed the FFI route module
  (25f1966), so 1.5.0 cannot reproduce gate ③.

.EXAMPLE
  pwsh -NoProfile -File .\Invoke-Gate3-Cancel.ps1 -PlanOnly

.EXAMPLE
  pwsh -NoProfile -File .\Invoke-Gate3-Cancel.ps1 -Expect broken -IncludeFlood
#>
[CmdletBinding()]
param(
    [string]$NiuExe = (Join-Path $env:LOCALAPPDATA 'Programs\Niubash\niu.exe'),
    [string]$OutDir,
    [ValidateSet('broken', 'fixed')]
    [string]$Expect = 'broken',
    [switch]$IncludeFlood,
    [switch]$PlanOnly,
    [double]$CancelAfter = 3.0,
    [int]$TimeoutSeconds = 300,
    [int]$FloodTimeoutSeconds = 360
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$ExitEnv = 2
$Stamp = (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssZ')
if (-not $OutDir) {
    $OutDir = Join-Path $PSScriptRoot (Join-Path '_runs' "gate3-$Stamp")
}
$OutDir = [IO.Path]::GetFullPath($OutDir)

function Write-Info([string]$Message) { Write-Host "[gate3] $Message" }
function Stop-Setup([string]$Message) {
    Write-Host "[gate3] SETUP-FAIL: $Message"
    exit $ExitEnv
}

if (-not (Test-Path -LiteralPath $NiuExe -PathType Leaf)) {
    Stop-Setup "niu.exe not found: $NiuExe (pass -NiuExe)"
}
$NiuExe = (Resolve-Path -LiteralPath $NiuExe).Path
$NiuRoot = Split-Path -Parent $NiuExe
$FfiRoot = Join-Path $NiuRoot 'winuxcmd\usr\bin\ffi'
$CurrentTxt = Join-Path $FfiRoot 'current.txt'
if (-not (Test-Path -LiteralPath $CurrentTxt)) {
    Stop-Setup ("no FFI tree at $FfiRoot. niubash 1.5.0 removed the FFI route " +
        "module (25f1966); use a 1.4.x build that still ships it.")
}
$FfiVersion = (Get-Content -LiteralPath $CurrentTxt -Raw).Trim()
$FfiVersionDir = Join-Path $FfiRoot $FfiVersion
$Dll = Join-Path $FfiVersionDir 'winuxcore.dll'
if (-not (Test-Path -LiteralPath $Dll -PathType Leaf)) {
    Stop-Setup "missing FFI payload: $Dll"
}

$Python = (Get-Command python -ErrorAction SilentlyContinue)
if (-not $Python) { Stop-Setup "python not found on PATH" }
$PyWinpty = & $Python.Source -c "import winpty; print(winpty.__version__)" 2>&1
if ($LASTEXITCODE -ne 0) {
    Stop-Setup "pywinpty is required (pip install pywinpty==3.0.5): $PyWinpty"
}

if ($PlanOnly) {
    Write-Info "niu: $NiuExe"
    Write-Info "ffi: $FfiVersionDir"
    Write-Info "pywinpty: $PyWinpty"
    Write-Info "cancel-after: $CancelAfter s; flood: $([bool]$IncludeFlood)"
    Write-Info "out: $OutDir"
    Write-Info "PLAN-ONLY: no applet executed"
    exit 0
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$Work = Join-Path $OutDir 'work'
New-Item -ItemType Directory -Force -Path $Work | Out-Null
$Big = Join-Path $Work 'big.txt'

function New-BigFixture([string]$Path) {
    if ((Test-Path -LiteralPath $Path) -and ((Get-Item -LiteralPath $Path).Length -eq 36000000)) {
        return
    }
    Write-Info "generating big.txt (36,000,000 B)..."
    $rng = [Random]::new(1487)
    $utf8 = [Text.UTF8Encoding]::new($false)
    $sw = [IO.StreamWriter]::new($Path, $false, $utf8, 1MB)
    try {
        for ($i = 0; $i -lt 3000000; $i++) {
            $n = $rng.Next(0, 1000000000)
            $c = [char](97 + $rng.Next(0, 8))
            $sw.Write($n.ToString('D9') + '-' + $c + "`n")
        }
    }
    finally { $sw.Dispose() }
    $len = (Get-Item -LiteralPath $Path).Length
    if ($len -ne 36000000) { throw "big.txt is $len B, expected 36000000" }
}
New-BigFixture $Big

@(
    "utc=$Stamp"
    "niu_exe=$NiuExe"
    "niu_version=$((& $NiuExe --version 2>&1 | Select-Object -First 1))"
    "ffi_version=$FfiVersion"
    "dll_sha256=$((Get-FileHash -LiteralPath $Dll -Algorithm SHA256).Hash.ToLowerInvariant())"
    "pywinpty=$PyWinpty"
    "expect=$Expect"
    "include_flood=$([bool]$IncludeFlood)"
    "big_sha256=$((Get-FileHash -LiteralPath $Big -Algorithm SHA256).Hash.ToLowerInvariant())"
) | Set-Content -LiteralPath (Join-Path $OutDir 'environment.txt') -Encoding utf8

$Helper = Join-Path $PSScriptRoot 'conpty_ctrl_c.py'
$HelperOut = Join-Path $OutDir 'helper-stdout.txt'
$HelperErr = Join-Path $OutDir 'helper-stderr.txt'
$argv = @(
    $Helper,
    '--niu', $NiuExe,
    '--work', $Work,
    '--out', $OutDir,
    '--big', $Big,
    '--cancel-after', $CancelAfter.ToString([Globalization.CultureInfo]::InvariantCulture),
    '--timeout', $TimeoutSeconds.ToString([Globalization.CultureInfo]::InvariantCulture),
    '--expect', $Expect
)
if ($IncludeFlood) {
    $argv += @('--flood', '--flood-timeout', $FloodTimeoutSeconds.ToString([Globalization.CultureInfo]::InvariantCulture))
}

Write-Info "running ConPTY driver (console stream captured to $OutDir)..."
$quoted = $argv | ForEach-Object {
    if ($_ -match '\s') { '"' + ($_ -replace '"', '\"') + '"' } else { $_ }
}
$proc = Start-Process -FilePath $Python.Source -ArgumentList ($quoted -join ' ') -NoNewWindow -PassThru `
    -RedirectStandardOutput $HelperOut -RedirectStandardError $HelperErr
$proc.WaitForExit()
$driverRc = $proc.ExitCode

$SummaryPath = Join-Path $OutDir 'gate3-summary.json'
if (-not (Test-Path -LiteralPath $SummaryPath)) {
    Get-Content -LiteralPath $HelperErr -ErrorAction SilentlyContinue | Write-Host
    Write-Info "driver produced no summary (rc=$driverRc)"
    exit $ExitEnv
}
$summary = Get-Content -LiteralPath $SummaryPath -Raw | ConvertFrom-Json

$reasons = [Collections.Generic.List[string]]::new()
$ok = $true
function Require([bool]$Condition, [string]$Reason) {
    if (-not $Condition) {
        $script:ok = $false
        $script:reasons.Add($Reason)
    }
}

if ($Expect -eq 'broken') {
    # The case must still be running when ^C lands; the 2026-11-01 baseline
    # was 12.61 s, but absolute speed is machine-dependent.
    Require ([double]$summary.b_dur -ge 5.0) "baseline dur=$($summary.b_dur) s (< 5 s; ^C would land after completion)"
    Require ($summary.b_rc -eq 0) "baseline rc=$($summary.b_rc)"
    Require ($summary.c_early -eq $false) "c_early=$($summary.c_early) (expected false)"
    Require ($null -ne $summary.c_token_lat -and [double]$summary.c_token_lat -ge 5.0) "cancel latency=$($summary.c_token_lat) s (expected >= 5)"
    Require ($summary.wc_lines -eq '3000000') "wc lines=$($summary.wc_lines)"
    Require ($summary.cmp1 -eq 0 -and $summary.cmp2 -eq 0) "cmp B/C=$($summary.cmp1) B/D=$($summary.cmp2)"
    if ($IncludeFlood) {
        Require ($null -ne $summary.flood) "flood phase missing"
        if ($summary.flood) {
            Require ([double]$summary.flood.cancel_to_prompt_s -ge 60.0) "flood ^C-to-prompt=$($summary.flood.cancel_to_prompt_s) s (expected ~180)"
            Require ([long]$summary.flood.cancel_bytes -ge 30000000) "flood cancel bytes=$($summary.flood.cancel_bytes)"
        }
    }
}
else {
    Require ($summary.b_rc -eq 0) "baseline rc=$($summary.b_rc)"
    Require ($summary.c_early -eq $true) "c_early=$($summary.c_early) (expected true)"
    Require ($summary.c_rc -eq 130) "c_rc=$($summary.c_rc) (expected 130, host Cancelled)"
    Require ($null -ne $summary.c_token_lat -and [double]$summary.c_token_lat -lt 2.0) "cancel latency=$($summary.c_token_lat) s (expected < 2)"
    Require ($summary.wc_lines -eq '3000000') "wc lines=$($summary.wc_lines)"
    Require ($summary.cmp1 -eq 0 -and $summary.cmp2 -eq 0) "cmp B/C=$($summary.cmp1) B/D=$($summary.cmp2)"
    Require ($summary.exit_alive -eq $false) "process still alive after exit=$($summary.exit_alive)"
    if ($IncludeFlood) {
        Require ($null -ne $summary.flood) "flood phase missing"
        if ($summary.flood) {
            Require ([double]$summary.flood.cancel_to_prompt_s -lt 5.0) "flood ^C-to-prompt=$($summary.flood.cancel_to_prompt_s) s (expected < 5)"
            Require ([long]$summary.flood.cancel_bytes -lt 1000000) "flood cancel bytes=$($summary.flood.cancel_bytes) (expected < 1 MB)"
        }
    }
}

$verdict = [ordered]@{
    issue     = 'unixwin/WinuxCmd#1487'
    gate      = 'niubash#727 gate 3 (product-path Ctrl-C)'
    expect    = $Expect
    verdict   = $(if ($ok) { 'MATCH' } else { 'MISMATCH' })
    reasons   = $reasons
    driver_rc = $driverRc
    summary   = $summary
}
$verdict | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutDir 'gate3-verdict.json') -Encoding utf8

Write-Info "evidence: $OutDir"
Write-Info ("baseline={0:N2}s cancel_latency={1} c_early={2} wc={3} cmp={4}/{5}" -f `
    [double]$summary.b_dur, $summary.c_token_lat, $summary.c_early, `
    $summary.wc_lines, $summary.cmp1, $summary.cmp2)
if ($IncludeFlood -and $summary.flood) {
    Write-Info ("flood: baseline={0:N2}s ^C-to-prompt={1:N2}s bytes={2}" -f `
        [double]$summary.flood.baseline_s, [double]$summary.flood.cancel_to_prompt_s, `
        [long]$summary.flood.cancel_bytes)
}
Write-Info "verdict: $($verdict.verdict) (expect=$Expect, driver rc=$driverRc)"
if (-not $ok -or $driverRc -ne 0) {
    $reasons | ForEach-Object { Write-Info "  - $_" }
    exit 1
}
exit 0
