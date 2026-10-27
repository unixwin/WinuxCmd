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
    [string]$VersionLabel = "v0.0.0-smoke"
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
$same = [Linq.Enumerable]::SequenceEqual([IO.File]::ReadAllBytes($r.Out), $payload)
Report "switch-off-spawns" ($r.Code -eq 0 -and $same -and $r.Err -match "reason=switch-off") $r.Err

# 2. switch on -> ffi, bytes identical to the spawn run
$r = Run-Harness "1" ($rootArg + @("cat", (Join-Path $data "payload.bin"))) "on"
$same = [Linq.Enumerable]::SequenceEqual([IO.File]::ReadAllBytes($r.Out), $payload)
Report "switch-on-ffi-cat" ($r.Code -eq 0 -and $same -and $r.Err -match "route=ffi") $r.Err

# 3. cp + rm in-process
$r = Run-Harness "1" ($rootArg + @("cp", (Join-Path $data "payload.bin"), (Join-Path $data "copy.bin"))) "cp"
$cpOk = $r.Code -eq 0 -and (Test-Path (Join-Path $data "copy.bin")) -and $r.Err -match "route=ffi"
Report "ffi-cp" $cpOk $r.Err
$r = Run-Harness "1" ($rootArg + @("rm", (Join-Path $data "copy.bin"))) "rm"
Report "ffi-rm" ($r.Code -eq 0 -and -not (Test-Path (Join-Path $data "copy.bin")) -and $r.Err -match "route=ffi") $r.Err

# 4. tampered DLL -> hash-mismatch, spawn fallback still works
Add-Content -Path (Join-Path $verDir "winuxcore.dll") -Value "x" -Encoding ascii -NoNewline
$r = Run-Harness "1" ($rootArg + @("cat", (Join-Path $data "payload.bin"))) "tamper"
$same = [Linq.Enumerable]::SequenceEqual([IO.File]::ReadAllBytes($r.Out), $payload)
Report "hash-tamper-falls-back" ($r.Code -eq 0 -and $same -and $r.Err -match "reason=hash-mismatch") $r.Err
Copy-Item $DllPath (Join-Path $verDir "winuxcore.dll") -Force

# 5. DLL missing -> dll-missing fallback
Remove-Item (Join-Path $verDir "winuxcore.dll")
$r = Run-Harness "1" ($rootArg + @("cat", (Join-Path $data "payload.bin"))) "missing"
$same = [Linq.Enumerable]::SequenceEqual([IO.File]::ReadAllBytes($r.Out), $payload)
Report "dll-missing-falls-back" ($r.Code -eq 0 -and $same -and $r.Err -match "reason=dll-missing") $r.Err

# 6. current.txt points at a nonexistent version -> dll-missing fallback
Set-Content -Path (Join-Path $root "usr\bin\ffi\current.txt") -Value "v9.9.9-absent" -Encoding ascii -NoNewline
$r = Run-Harness "1" ($rootArg + @("cat", (Join-Path $data "payload.bin"))) "badptr"
$same = [Linq.Enumerable]::SequenceEqual([IO.File]::ReadAllBytes($r.Out), $payload)
Report "dangling-pointer-falls-back" ($r.Code -eq 0 -and $same -and $r.Err -match "reason=dll-missing") $r.Err

if ($failures -gt 0) { Write-Host "SMOKE FAILED: $failures case(s)"; exit 1 }
Write-Host "SMOKE OK"
