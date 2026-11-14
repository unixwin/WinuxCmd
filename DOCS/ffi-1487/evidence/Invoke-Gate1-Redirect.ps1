<#
.SYNOPSIS
  Reproduce niubash#727 gate ① (product-path equivalence) for
  unixwin/WinuxCmd#1487 defect A: FFI calls ignore shell redirections.

.DESCRIPTION
  Runs a small, deterministic case set twice through the product path:
    NIU_WINUXCMD_FFI=0  (spawn baseline)
    NIU_WINUXCMD_FFI=1  (FFI route)
  Every child's stdout/stderr is redirected to files; nothing is inherited
  by this script's console.  The 36 MB flood case is captured to a file and
  capped.

  The shipped tiers.tsv has no tier2 rows, so the product path claims 0
  commands.  Pass -ApplyTier2 to back up the installed tiers.tsv and
  temporarily promote only the case commands to tier2; the file is restored
  in a finally block and the restore is hash-verified.

  -Expect broken reproduces the 2026-11-01 defect (srt.out 0 B, wc 0,
  /dev/null ignored, ...).  -Expect fixed is the revival gate (all cases
  match spawn/GNU semantics).

.PARAMETER NiuExe
  Path to a 1.4.x niu.exe that still has the FFI route module.  niubash
  1.5.0 removed it (25f1966), so 1.5.0 cannot reproduce.

.EXAMPLE
  pwsh -NoProfile -File .\Invoke-Gate1-Redirect.ps1 -PlanOnly

.EXAMPLE
  pwsh -NoProfile -File .\Invoke-Gate1-Redirect.ps1 -ApplyTier2 -Expect broken -IncludeFlood
#>
[CmdletBinding()]
param(
    [string]$NiuExe = (Join-Path $env:LOCALAPPDATA 'Programs\Niubash\niu.exe'),
    [string]$OutDir,
    [ValidateSet('broken', 'fixed')]
    [string]$Expect = 'broken',
    [switch]$ApplyTier2,
    [switch]$IncludeFlood,
    [switch]$PlanOnly,
    # Harness self-test only: allow 0 FFI claims (e.g. fake niu). Real
    # evidence must never accept an unclaimed case as the FFI arm.
    [switch]$AllowUnclaimed,
    [int]$TimeoutSeconds = 180,
    [int]$FloodTimeoutSeconds = 300,
    [long]$FloodCapBytes = 64MB
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ExitEnv = 2
$ExitRestore = 3
$script:AllowUnclaimed = [bool]$AllowUnclaimed
$Stamp = (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssZ')
if (-not $OutDir) {
    $OutDir = Join-Path $PSScriptRoot (Join-Path '_runs' "gate1-$Stamp")
}
$OutDir = [IO.Path]::GetFullPath($OutDir)
$OutDirExisted = Test-Path -LiteralPath $OutDir
$CaseRoot = Join-Path $OutDir 'cases'
New-Item -ItemType Directory -Force -Path $OutDir, $CaseRoot | Out-Null

function Write-Info([string]$Message) { Write-Host "[gate1] $Message" }
function Stop-Setup([string]$Message) {
    Write-Host "[gate1] SETUP-FAIL: $Message"
    if ((Get-Variable -Name OutDirExisted -ErrorAction SilentlyContinue) -and
        -not $OutDirExisted -and (Test-Path -LiteralPath $OutDir)) {
        Remove-Item -LiteralPath $OutDir -Recurse -Force -ErrorAction SilentlyContinue
    }
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
        "module (25f1966); use a 1.4.x product build that still ships it.")
}
$FfiVersion = (Get-Content -LiteralPath $CurrentTxt -Raw).Trim()
if (-not $FfiVersion) { Stop-Setup "empty current.txt: $CurrentTxt" }
$FfiVersionDir = Join-Path $FfiRoot $FfiVersion
$Dll = Join-Path $FfiVersionDir 'winuxcore.dll'
$Tiers = Join-Path $FfiVersionDir 'tiers.tsv'
foreach ($required in @($Dll, $Tiers)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        Stop-Setup "missing FFI payload: $required"
    }
}

# --- environment snapshot ----------------------------------------------------
$EnvFile = Join-Path $OutDir 'environment.txt'
$VersionOut = Join-Path $OutDir 'niu-version.txt'
$DoctorOut = Join-Path $OutDir 'niu-doctor.txt'
$null = & $NiuExe --version 2>&1 | Out-File -LiteralPath $VersionOut -Encoding utf8
$oldFfi = $env:NIU_WINUXCMD_FFI
$oldLog = $env:NIU_WINUXCMD_FFI_LOG
try {
    $env:NIU_WINUXCMD_FFI = '1'
    $env:NIU_WINUXCMD_FFI_LOG = '1'
    $null = & $NiuExe doctor 2>&1 | Out-File -LiteralPath $DoctorOut -Encoding utf8
}
finally {
    $env:NIU_WINUXCMD_FFI = $oldFfi
    $env:NIU_WINUXCMD_FFI_LOG = $oldLog
}

@(
    "utc=$Stamp"
    "niu_exe=$NiuExe"
    "niu_version=$((Get-Content -LiteralPath $VersionOut -TotalCount 1))"
    "ffi_root=$FfiRoot"
    "ffi_version=$FfiVersion"
    "dll=$Dll"
    "dll_sha256=$((Get-FileHash -LiteralPath $Dll -Algorithm SHA256).Hash.ToLowerInvariant())"
    "tiers=$Tiers"
    "tiers_sha256=$((Get-FileHash -LiteralPath $Tiers -Algorithm SHA256).Hash.ToLowerInvariant())"
    "expect=$Expect"
    "apply_tier2=$([bool]$ApplyTier2)"
    "include_flood=$([bool]$IncludeFlood)"
    "powershell=$($PSVersionTable.PSVersion)"
    "python=$((& python --version 2>&1) -join ' ')"
) | Set-Content -LiteralPath $EnvFile -Encoding utf8

# --- fixtures ----------------------------------------------------------------
$Words = @('banana', 'apple', 'cherry', 'apple', 'date', 'banana', 'apple')
$WordsBytes = ($Words -join "`n") + "`n"

$Cases = @(
    [pscustomobject]@{
        Name = 'sort_redirect'
        Script = @'
sort < words.txt > srt.out 2> srt.err
echo "RC=$?"
echo "SIZE=$(wc -c < srt.out)"
'@
        Files = @{ 'words.txt' = $WordsBytes }
    }
    [pscustomobject]@{
        Name = 'sort_stdout'
        Script = @'
sort words.txt > o.txt
echo "RC=$?"
echo "SIZE=$(wc -c < o.txt)"
'@
        Files = @{ 'words.txt' = $WordsBytes }
    }
    [pscustomobject]@{
        Name = 'wc_stdin'
        Script = @'
wc -l < words.txt
echo "RC=$?"
'@
        Files = @{ 'words.txt' = $WordsBytes }
    }
    [pscustomobject]@{
        Name = 'tee_stdin'
        Script = @'
tee t.out < words.txt > /dev/null
echo "RC=$?"
echo "SIZE=$(wc -c < t.out)"
'@
        Files = @{ 'words.txt' = $WordsBytes }
    }
    [pscustomobject]@{
        Name = 'tr_stdin'
        Script = @'
tr a-z A-Z < words.txt
echo "RC=$?"
'@
        Files = @{ 'words.txt' = $WordsBytes }
    }
)

$Targets = @('sort', 'wc', 'tee', 'tr')

function Test-Tier2([string]$Command) {
    $line = Get-Content -LiteralPath $Tiers | Where-Object { $_ -match "^$([regex]::Escape($Command))\t" } | Select-Object -First 1
    if (-not $line) { return $false }
    return (($line -split "`t")[1] -eq 'tier2')
}

function Assert-Tier2Expanded {
    $still = @($Targets | Where-Object { -not (Test-Tier2 $_) })
    if ($still.Count -gt 0) {
        throw ("tier2 expansion did not apply to: $($still -join ', '). " +
            "The FFI route would claim 0 commands and the run would be meaningless.")
    }
}

$MissingTier2 = @($Targets | Where-Object { -not (Test-Tier2 $_) })
if ($MissingTier2.Count -gt 0 -and -not $ApplyTier2 -and -not $PlanOnly) {
    Stop-Setup ("shipped tiers.tsv has no tier2 for: $($MissingTier2 -join ', '). " +
        "The product path claims 0 commands. Re-run with -ApplyTier2 (backup/restore is automatic).")
}

if ($PlanOnly) {
    Write-Info "niu: $NiuExe"
    Write-Info "ffi: $FfiVersionDir"
    Write-Info "tier2 missing: $($MissingTier2 -join ', ')"
    Write-Info "tiers.tsv: $Tiers"
    Write-Info "cases: $($Cases.Name -join ', ')$(if ($IncludeFlood) { ', flood' } else { '' })"
    Write-Info "out: $OutDir"
    Write-Info "PLAN-ONLY: no applet executed"
    if (-not $OutDirExisted) { Remove-Item -LiteralPath $OutDir -Recurse -Force }
    exit 0
}

if ($PSVersionTable.PSVersion -lt [version]'7.4') {
    Stop-Setup "PowerShell 7.4+ required for Start-Process -Environment"
}

# --- big.txt (36,000,000 bytes: 3,000,000 lines x 12 bytes) -------------------
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

# --- tiers.tsv backup/expand/restore -----------------------------------------
$TierBackupDir = Join-Path $OutDir 'tier-backup'
$TierBackup = Join-Path $TierBackupDir 'tiers.tsv.orig'
$TierBackupHash = $null
$TierMutated = $false

function Expand-Tiers {
    New-Item -ItemType Directory -Force -Path $TierBackupDir | Out-Null
    Copy-Item -LiteralPath $Tiers -Destination $TierBackup -Force
    $script:TierBackupHash = (Get-FileHash -LiteralPath $TierBackup -Algorithm SHA256).Hash
    $text = [IO.File]::ReadAllText($Tiers)
    $lines = $text -split "`n"
    $out = foreach ($line in $lines) {
        $fields = $line -split "`t"
        if ($fields.Count -ge 2 -and $Targets -contains $fields[0]) {
            $fields[1] = 'tier2'
            ($fields -join "`t")
        }
        else { $line }
    }
    [IO.File]::WriteAllText($Tiers, ($out -join "`n"), [Text.UTF8Encoding]::new($false))
    $script:TierMutated = $true
    Assert-Tier2Expanded
    Write-Info "tier2 test expansion applied to: $($Targets -join ', ')"
}

function Restore-Tiers {
    if (-not $script:TierMutated) { return }
    Copy-Item -LiteralPath $TierBackup -Destination $Tiers -Force
    $now = (Get-FileHash -LiteralPath $Tiers -Algorithm SHA256).Hash
    if ($now -ne $script:TierBackupHash) {
        Write-Host "[gate1] RESTORE-FAIL: $Tiers hash $now != backup $($script:TierBackupHash)"
        exit $ExitRestore
    }
    $script:TierMutated = $false
    Write-Info "tiers.tsv restored (sha256 $now)"
}

# --- process runner ----------------------------------------------------------
$EmptyStdin = Join-Path $OutDir 'empty.stdin'
[IO.File]::WriteAllText($EmptyStdin, '')

function Invoke-NiuCase {
    param(
        [string]$Name,
        [string]$ScriptText,
        [hashtable]$Files,
        [bool]$Ffi,
        [int]$TimeoutSec,
        [string]$BigText = ''
    )
    $state = if ($Ffi) { 'ffi' } else { 'spawn' }
    $work = Join-Path (Join-Path $CaseRoot $Name) $state
    New-Item -ItemType Directory -Force -Path $work | Out-Null
    foreach ($entry in $Files.GetEnumerator()) {
        $dest = Join-Path $work $entry.Key
        [IO.File]::WriteAllText($dest, [string]$entry.Value, [Text.UTF8Encoding]::new($false))
    }
    if ($BigText) {
        Copy-Item -LiteralPath $BigText -Destination (Join-Path $work 'big.txt') -Force
    }
    $caseScript = Join-Path $work 'case.sh'
    [IO.File]::WriteAllText($caseScript, ($ScriptText -replace "`r`n", "`n"), [Text.UTF8Encoding]::new($false))

    $outFile = Join-Path $work 'stdout.bin'
    $errFile = Join-Path $work 'stderr.bin'
    $envMap = @{
        'NIU_WINUXCMD_FFI'        = $(if ($Ffi) { '1' } else { '0' })
        'NIU_WINUXCMD_FFI_LOG'    = '1'
        'NIU_WINUXCMD_FFI_EXCLUDE' = ''
        'BASH_ENV'                = ''
        'WINUXSH_ROOT'            = ''
        'WINUXCMD_PATH'           = ''
        'NIU_ROOT'                = ''
        'WINUXCMD_HOME'           = ''
        'HOME'                    = (Join-Path $OutDir 'home')
    }
    New-Item -ItemType Directory -Force -Path (Join-Path $OutDir 'home') | Out-Null

    $sw = [Diagnostics.Stopwatch]::StartNew()
    $p = Start-Process -FilePath $NiuExe -ArgumentList @('case.sh') `
        -WorkingDirectory $work -NoNewWindow -PassThru `
        -RedirectStandardInput $EmptyStdin `
        -RedirectStandardOutput $outFile `
        -RedirectStandardError $errFile `
        -Environment $envMap
    $timedOut = $false
    if (-not $p.WaitForExit($TimeoutSec * 1000)) {
        $timedOut = $true
        try { $p.Kill($true) } catch { }
        $null = $p.WaitForExit(5000)
    }
    $sw.Stop()
    $stdoutBytes = (Get-Item -LiteralPath $outFile).Length
    $stderrBytes = (Get-Item -LiteralPath $errFile).Length
    # Never materialize a multi-megabyte flood in a string; its size is the
    # evidence.  Everything else is small and is compared verbatim.
    if ($stdoutBytes -gt 4MB) {
        $stdout = ''
    }
    else {
        $stdout = Get-Content -LiteralPath $outFile -Raw -ErrorAction SilentlyContinue
        if ($null -eq $stdout) { $stdout = '' }
    }
    $stderr = Get-Content -LiteralPath $errFile -Raw -ErrorAction SilentlyContinue
    if ($null -eq $stderr) { $stderr = '' }
    $claims = ([regex]::Matches($stderr, 'route .* -> Ffi \(Claimed\)')).Count
    return [pscustomobject]@{
        Name        = $Name
        State       = $state
        Rc          = $(if ($timedOut) { -1 } else { $p.ExitCode })
        TimedOut    = $timedOut
        DurationSec = [Math]::Round($sw.Elapsed.TotalSeconds, 3)
        Stdout      = $stdout
        Stderr      = $stderr
        StdoutBytes = $stdoutBytes
        StderrBytes = $stderrBytes
        Claims      = $claims
        WorkDir     = $work
    }
}

function Get-LabelInt([string]$Text, [string]$Label) {
    $m = [regex]::Match($Text, "(?m)^$([regex]::Escape($Label))=(\d+)\s*$")
    if ($m.Success) { return [int]$m.Groups[1].Value }
    return $null
}

function Get-FirstInt([string]$Text) {
    $m = [regex]::Match($Text, '(?m)^\s*(\d+)\s*$')
    if ($m.Success) { return [int]$m.Groups[1].Value }
    return $null
}

# --- profiles ----------------------------------------------------------------
# Every number below is anchored in the 2026-11-01 artifacts (broken) or in
# the documented fix expectation (fixed).  sizes are bytes of the shell's
# redirect target, stdout_bytes is what escaped to the caller's stdout.
$Profiles = @{
    broken = @{
        sort_redirect = @{ size = 0; rc = 0 }
        sort_stdout   = @{ size = 0; stdout_bytes_min = 10 }
        wc_stdin      = @{ value = 0; rc = 0 }
        tee_stdin     = @{ size = 0; rc = 0 }
        tr_stdin      = @{ upper = $false; stdout_bytes_max = 8 }
        flood         = @{ stdout_bytes_min = 30000000 }
    }
    fixed = @{
        sort_redirect = @{ size = 44; rc = 0 }
        sort_stdout   = @{ size = 44; stdout_bytes_max = 20 }
        wc_stdin      = @{ value = 7; rc = 0 }
        tee_stdin     = @{ size = 44; rc = 0 }
        tr_stdin      = @{ upper = $true; stdout_bytes_min = 44 }
        flood         = @{ stdout_bytes_max = 1000 }
    }
}

$profile = $Profiles[$Expect]
$ledger = [Collections.Generic.List[string]]::new()
$ledger.Add("case`tstate`trc`tduration_s`tstdout_bytes`tstderr_bytes`tclaims`tsize`tvalue`tupper`tverdict`tnote")
$failures = 0

function Add-Verdict([string]$Case, [hashtable]$Measure, [hashtable]$Rule, $Run) {
    $notes = [Collections.Generic.List[string]]::new()
    $ok = $true
    $size = if ($Measure.ContainsKey('size')) { $Measure['size'] } else { '' }
    $value = if ($Measure.ContainsKey('value')) { $Measure['value'] } else { '' }
    $upper = if ($Measure.ContainsKey('upper')) { $Measure['upper'] } else { '' }
    if ($Rule.ContainsKey('rc') -and $Run.Rc -ne $Rule.rc) { $ok = $false; $notes.Add("rc=$($Run.Rc) want $($Rule.rc)") }
    if ($Rule.ContainsKey('size') -and $size -ne $Rule.size) { $ok = $false; $notes.Add("size=$size want $($Rule.size)") }
    if ($Rule.ContainsKey('value') -and $value -ne $Rule.value) { $ok = $false; $notes.Add("value=$value want $($Rule.value)") }
    if ($Rule.ContainsKey('upper') -and [bool]$upper -ne [bool]$Rule.upper) { $ok = $false; $notes.Add("upper=$upper want $($Rule.upper)") }
    if ($Rule.ContainsKey('stdout_bytes_min') -and [long]$Run.StdoutBytes -lt [long]$Rule.stdout_bytes_min) { $ok = $false; $notes.Add("stdout_bytes=$($Run.StdoutBytes) < $($Rule.stdout_bytes_min)") }
    if ($Rule.ContainsKey('stdout_bytes_max') -and [long]$Run.StdoutBytes -gt [long]$Rule.stdout_bytes_max) { $ok = $false; $notes.Add("stdout_bytes=$($Run.StdoutBytes) > $($Rule.stdout_bytes_max)") }
    if ($Run.State -eq 'ffi' -and $Run.Claims -lt 1) {
        if ($script:AllowUnclaimed) { $notes.Add('unclaimed (self-test)') }
        else { $ok = $false; $notes.Add('no Ffi claim: tier2/product path not active') }
    }
    $verdict = if ($ok) { 'PASS' } else { 'FAIL' }
    if (-not $ok) { $script:failures++ }
    $script:ledger.Add((
        "$Case`t$($Run.State)`t$($Run.Rc)`t$($Run.DurationSec)`t$($Run.StdoutBytes)`t$($Run.StderrBytes)`t$($Run.Claims)`t" +
        "$size`t$value`t$upper`t$verdict`t$($notes -join '; ')"
    ))
    if (-not $ok) { Write-Info "$Case [$($Run.State)] FAIL: $($notes -join '; ')" }
}

try {
    if ($ApplyTier2) { Expand-Tiers }

    foreach ($case in $Cases) {
        $ffiRun = Invoke-NiuCase -Name $case.Name -ScriptText $case.Script -Files $case.Files -Ffi $true -TimeoutSec $TimeoutSeconds

        # spawn baseline: same script, FFI=0 (proves the case itself is sound)
        $spawnRun = Invoke-NiuCase -Name $case.Name -ScriptText $case.Script -Files $case.Files -Ffi $false -TimeoutSec $TimeoutSeconds
        $spawnSize = Get-LabelInt $spawnRun.Stdout 'SIZE'
        $spawnValue = Get-FirstInt $spawnRun.Stdout
        $spawnUpper = $spawnRun.Stdout -match 'BANANA'
        $spawnOk = switch ($case.Name) {
            'sort_redirect' { $spawnSize -eq 44 }
            'sort_stdout'   { $spawnSize -eq 44 }
            'wc_stdin'      { $spawnValue -eq 7 }
            'tee_stdin'     { $spawnSize -eq 44 }
            'tr_stdin'      { $spawnUpper }
        }
        $ledger.Add("$($case.Name)`tspawn-baseline`t$($spawnRun.Rc)`t$($spawnRun.DurationSec)`t$($spawnRun.StdoutBytes)`t$($spawnRun.StderrBytes)`t$($spawnRun.Claims)`t$spawnSize`t$spawnValue`t$spawnUpper`t$(if ($spawnOk) { 'PASS' } else { 'FAIL' })`tspawn reference")
        if (-not $spawnOk) { $failures++ }

        $measure = @{
            size  = Get-LabelInt $ffiRun.Stdout 'SIZE'
            value = Get-FirstInt $ffiRun.Stdout
            upper = ($ffiRun.Stdout -match 'BANANA')
        }
        Add-Verdict -Case $case.Name -Measure $measure -Rule $profile[$case.Name] -Run $ffiRun
    }

    if ($IncludeFlood) {
        $big = Join-Path $OutDir 'big.txt'
        New-BigFixture $big
        $script = @'
sort big.txt > /dev/null
echo "RC=$?"
'@
        $ffiRun = Invoke-NiuCase -Name 'flood' -ScriptText $script -Files @{} -Ffi $true -TimeoutSec $FloodTimeoutSeconds -BigText $big
        $spawnRun = Invoke-NiuCase -Name 'flood' -ScriptText $script -Files @{} -Ffi $false -TimeoutSec $FloodTimeoutSeconds -BigText $big
        $spawnVerdict = if ($spawnRun.StdoutBytes -le 1000) { 'PASS' } else { 'FAIL' }
        $ledger.Add("flood`tspawn-baseline`t$($spawnRun.Rc)`t$($spawnRun.DurationSec)`t$($spawnRun.StdoutBytes)`t$($spawnRun.StderrBytes)`t$($spawnRun.Claims)`t`t`t`t$spawnVerdict`tspawn reference")
        $overflow = $ffiRun.StdoutBytes -gt $FloodCapBytes
        if ($overflow) {
            $ledger.Add("flood`tffi`t$($ffiRun.Rc)`t$($ffiRun.DurationSec)`t$($ffiRun.StdoutBytes)`t$($ffiRun.StderrBytes)`t$($ffiRun.Claims)`t`t`t`tFAIL`tflood-capped")
            $failures++
        }
        else { Add-Verdict -Case 'flood' -Measure @{} -Rule $profile['flood'] -Run $ffiRun }
    }
}
finally {
    Restore-Tiers
}

$ledger | Set-Content -LiteralPath (Join-Path $OutDir 'gate1-ledger.tsv') -Encoding utf8

$summary = [ordered]@{
    issue      = 'unixwin/WinuxCmd#1487'
    gate       = 'niubash#727 gate 1 (product-path redirect equivalence)'
    expect     = $Expect
    timestamp  = $Stamp
    niu_exe    = $NiuExe
    ffi_dir    = $FfiVersionDir
    dll_sha256 = (Get-FileHash -LiteralPath $Dll -Algorithm SHA256).Hash.ToLowerInvariant()
    cases      = $Cases.Name
    flood      = [bool]$IncludeFlood
    failures   = $failures
    verdict    = $(if ($failures -eq 0) { 'MATCH' } else { 'MISMATCH' })
}
$summary | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutDir 'gate1-summary.json') -Encoding utf8

Write-Info "evidence: $OutDir"
Write-Info "verdict: $($summary.verdict) ($failures failure(s), expect=$Expect)"
if ($failures -ne 0) { exit 1 }
exit 0
