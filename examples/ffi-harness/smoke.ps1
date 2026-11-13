# smoke.ps1 — assemble an FFI install layout and run the harness decision
# matrix against it. Works with the real winuxcore.dll + winuxcmd.exe or
# with the demo doubles (pass their paths instead); the matrix is the same.
#
#   pwsh smoke.ps1 -HarnessExe <ffi-harness.exe> -DllPath <winuxcore.dll> `
#       -SpawnExe <winuxcmd.exe> -TiersPath <tiers.tsv> `
#       -WorkDir <scratch dir> [-VersionLabel v0.0.0-demo]
#
# Exits non-zero on the first failed expectation; prints one PASS/FAIL
# line per case. All harness decisions are read from its stderr.
param(
    [Parameter(Mandatory = $true)][string]$HarnessExe,
    [Parameter(Mandatory = $true)][string]$DllPath,
    [Parameter(Mandatory = $true)][string]$SpawnExe,
    [Parameter(Mandatory = $true)][string]$TiersPath,
    [Parameter(Mandatory = $true)][string]$WorkDir,
    [string]$VersionLabel = "v0.0.0-smoke",
    [string]$SequenceApplets = ""
)

$ErrorActionPreference = "Stop"
$failures = 0

function Report([string]$name, [bool]$ok, [string]$detail) {
    if ($ok) { Write-Host "PASS $name" }
    else { Write-Host "FAIL $name :: $detail"; $script:failures += 1 }
}

# ---- layout -------------------------------------------------------------
$root = Join-Path $WorkDir "root"
$verDir = Join-Path $root "usr\bin\ffi\$VersionLabel"
New-Item -ItemType Directory -Force -Path $verDir | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $root "usr\bin") | Out-Null
Copy-Item $SpawnExe (Join-Path $root "usr\bin\winuxcmd.exe") -Force
Copy-Item $DllPath (Join-Path $verDir "winuxcore.dll") -Force
Copy-Item $TiersPath (Join-Path $verDir "tiers.tsv") -Force
$hash = (Get-FileHash (Join-Path $verDir "winuxcore.dll") -Algorithm SHA256).Hash.ToLower()
Set-Content -Path (Join-Path $verDir "winuxcore.sha256") -Value $hash -Encoding ascii -NoNewline
Set-Content -Path (Join-Path $root "usr\bin\ffi\current.txt") -Value $VersionLabel -Encoding ascii -NoNewline

$data = Join-Path $WorkDir "data"
New-Item -ItemType Directory -Force -Path $data | Out-Null
$payload = [byte[]](0..255) + [Text.Encoding]::ASCII.GetBytes("`r`nffi-smoke`n")
[IO.File]::WriteAllBytes((Join-Path $data "payload.bin"), $payload)
$payloadHash = (Get-FileHash (Join-Path $data "payload.bin") -Algorithm SHA256).Hash

function Same-AsPayload([string]$path) {
    return (Get-FileHash $path -Algorithm SHA256).Hash -eq $payloadHash
}

function Run-Harness([string]$switch, [string[]]$argv, [string]$tag) {
    $env:NIU_WINUXCMD_FFI = $switch
    $out = Join-Path $WorkDir "$tag.out"
    $err = Join-Path $WorkDir "$tag.err"
    $p = Start-Process -FilePath $HarnessExe -ArgumentList $argv `
        -RedirectStandardOutput $out -RedirectStandardError $err `
        -Wait -PassThru -NoNewWindow
    return @{ Code = $p.ExitCode; Out = $out; Err = (Get-Content $err -Raw) }
}

$rootArg = @($root)

# 1. switch off -> spawn, correct bytes
$r = Run-Harness "0" ($rootArg + @("cat", (Join-Path $data "payload.bin"))) "off"
$same = Same-AsPayload $r.Out
Report "switch-off-spawns" ($r.Code -eq 0 -and $same -and $r.Err -match "reason=switch-off") $r.Err

# 2. switch on -> ffi, bytes identical to the spawn run.
# Route assertions must be strict: the harness prints route=ffi BEFORE the
# call, so a call that faults in-process and is rescued by the spawn
# fallback still contains "route=ffi" in stderr. An FFI case only passes
# when the call really completed in-process: no ffi-call-failed line and
# no DLL boundary diagnostic may appear.
$r = Run-Harness "1" ($rootArg + @("cat", (Join-Path $data "payload.bin"))) "on"
$same = Same-AsPayload $r.Out
Report "switch-on-ffi-cat" ($r.Code -eq 0 -and $same -and $r.Err -match "route=ffi" -and $r.Err -notmatch "ffi-call-failed" -and $r.Err -notmatch "exception at DLL boundary") $r.Err

# 3. cp + rm in-process (same strict route assertions)
$r = Run-Harness "1" ($rootArg + @("cp", (Join-Path $data "payload.bin"), (Join-Path $data "copy.bin"))) "cp"
$cpOk = $r.Code -eq 0 -and (Test-Path (Join-Path $data "copy.bin")) -and $r.Err -match "route=ffi" -and $r.Err -notmatch "ffi-call-failed" -and $r.Err -notmatch "exception at DLL boundary"
Report "ffi-cp" $cpOk $r.Err
$r = Run-Harness "1" ($rootArg + @("rm", (Join-Path $data "copy.bin"))) "rm"
Report "ffi-rm" ($r.Code -eq 0 -and -not (Test-Path (Join-Path $data "copy.bin")) -and $r.Err -match "route=ffi" -and $r.Err -notmatch "ffi-call-failed" -and $r.Err -notmatch "exception at DLL boundary") $r.Err

# 3b. same-process multi-call sequence. Every case above runs ONE call per
# process; a fault that leaves the DLL's serialization lock held (the rm
# wedge class) is invisible to them — each new process starts with a fresh
# lock. The sequence runs 25 rotations (touch/cat/cp/rm/chmod by default)
# in ONE process with no spawn fallback and fails if any call falls back,
# returns an FFI-layer error, or if calls after an rm stop working.
$seqDir = Join-Path $WorkDir "seqdata"
New-Item -ItemType Directory -Force -Path $seqDir | Out-Null
$env:NIU_WINUXCMD_FFI = "1"
$seqArgs = @("sequence", $root, $seqDir, "25")
if ($SequenceApplets) { $seqArgs += $SequenceApplets }
$seqOut = Join-Path $WorkDir "seq.out"
$seqErr = Join-Path $WorkDir "seq.err"
$p = Start-Process -FilePath $HarnessExe -ArgumentList $seqArgs `
    -RedirectStandardOutput $seqOut -RedirectStandardError $seqErr `
    -Wait -PassThru -NoNewWindow
# Start-Process -PassThru with redirected streams can leave ExitCode
# unpopulated ($null) unless the process handle is touched and the exit
# is observed explicitly.
$null = $p.Handle
$p.WaitForExit()
$seqCode = $p.ExitCode
$seqText = Get-Content $seqOut -Raw
$seqErrText = Get-Content $seqErr -Raw
$seqExitOk = ($seqCode -eq 0)
$seqOutOk = ($null -ne $seqText) -and ($seqText -match "SEQUENCE OK")
$seqErrOk = [string]::IsNullOrEmpty($seqErrText) -or ($seqErrText -notmatch "exception at DLL boundary")
Report "sequence-multi-call" ($seqExitOk -and $seqOutOk -and $seqErrOk) "exit=$seqCode exitOk=$seqExitOk outOk=$seqOutOk errOk=$seqErrOk out: $seqText err: $seqErrText"

# 3c. stdio redirect binding (#1487 defect A). One process, ctx std
# handles that change between calls: the first call primes the DLL's
# cached std handles, later calls must follow the handles passed in ctx.
# The one-call-per-process cases above cannot see this — their cache is
# cold and already points at the redirect. Real-DLL matrix only: the
# demo doubles implement cat/cp/rm only, and sort/wc are real applets.
# The harness stdin is an empty file so the OLD dll reads EOF (wc prints
# 0) instead of blocking on a console.
if ($VersionLabel -ne "v0.0.0-demo") {
    $redirDir = Join-Path $WorkDir "redirdata"
    New-Item -ItemType Directory -Force -Path $redirDir | Out-Null
    $emptyStdin = Join-Path $WorkDir "empty.stdin"
    Set-Content -Path $emptyStdin -Value $null -Encoding ascii -NoNewline
    $redirOut = Join-Path $WorkDir "redir.out"
    $redirErr = Join-Path $WorkDir "redir.err"
    $p = Start-Process -FilePath $HarnessExe -ArgumentList @("redirect", $root, $redirDir) `
        -RedirectStandardOutput $redirOut -RedirectStandardError $redirErr `
        -RedirectStandardInput $emptyStdin `
        -Wait -PassThru -NoNewWindow
    $null = $p.Handle
    $p.WaitForExit()
    $redirText = Get-Content $redirOut -Raw
    Report "redirect-multi-call" ($p.ExitCode -eq 0 -and $redirText -match "REDIRECT OK") "exit=$($p.ExitCode) out: $redirText err: $(Get-Content $redirErr -Raw)"
}

# 3d. mid-dispatch cancellation (#1487 defect B). sort of a 3M-line file
# with a cancel callback firing 300ms in must report -1006 well before
# the uncancelled run finishes. Real-DLL matrix only (sort is a real
# applet, not a demo double).
if ($VersionLabel -ne "v0.0.0-demo") {
    $cancelDir = Join-Path $WorkDir "canceldata"
    New-Item -ItemType Directory -Force -Path $cancelDir | Out-Null
    $emptyStdin = Join-Path $WorkDir "empty.stdin"
    Set-Content -Path $emptyStdin -Value $null -Encoding ascii -NoNewline
    $cancelOut = Join-Path $WorkDir "cancel.out"
    $cancelErr = Join-Path $WorkDir "cancel.err"
    $p = Start-Process -FilePath $HarnessExe -ArgumentList @("cancel", $root, $cancelDir) `
        -RedirectStandardOutput $cancelOut -RedirectStandardError $cancelErr `
        -RedirectStandardInput $emptyStdin `
        -Wait -PassThru -NoNewWindow
    $null = $p.Handle
    $p.WaitForExit()
    $cancelText = Get-Content $cancelOut -Raw
    Report "cancel-mid-dispatch" ($p.ExitCode -eq 0 -and ($cancelText -match "CANCEL OK" -or $cancelText -match "CANCEL SKIP")) "exit=$($p.ExitCode) out: $cancelText err: $(Get-Content $cancelErr -Raw)"
}

# 4. tampered DLL -> hash-mismatch, spawn fallback still works
Add-Content -Path (Join-Path $verDir "winuxcore.dll") -Value "x" -Encoding ascii -NoNewline
$r = Run-Harness "1" ($rootArg + @("cat", (Join-Path $data "payload.bin"))) "tamper"
$same = Same-AsPayload $r.Out
Report "hash-tamper-falls-back" ($r.Code -eq 0 -and $same -and $r.Err -match "reason=hash-mismatch") $r.Err
Copy-Item $DllPath (Join-Path $verDir "winuxcore.dll") -Force

# 5. DLL missing -> dll-missing fallback
Remove-Item (Join-Path $verDir "winuxcore.dll")
$r = Run-Harness "1" ($rootArg + @("cat", (Join-Path $data "payload.bin"))) "missing"
$same = Same-AsPayload $r.Out
Report "dll-missing-falls-back" ($r.Code -eq 0 -and $same -and $r.Err -match "reason=dll-missing") $r.Err

# 6. current.txt points at a nonexistent version -> dll-missing fallback
Set-Content -Path (Join-Path $root "usr\bin\ffi\current.txt") -Value "v9.9.9-absent" -Encoding ascii -NoNewline
$r = Run-Harness "1" ($rootArg + @("cat", (Join-Path $data "payload.bin"))) "badptr"
$same = Same-AsPayload $r.Out
Report "dangling-pointer-falls-back" ($r.Code -eq 0 -and $same -and $r.Err -match "reason=dll-missing") $r.Err

if ($failures -gt 0) { Write-Host "SMOKE FAILED: $failures case(s)"; exit 1 }
Write-Host "SMOKE OK"
