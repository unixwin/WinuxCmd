# Builds winuxcmd.exe with MSVC Profile-Guided Optimization.
#
#   1. Release build with /GENPROFILE (instrumented)
#   2. Run the training workloads (scripts/pgo-train.sh, ported from
#      uutils/coreutils util/build-pgo.sh) plus the differential corpus;
#      each instrumented process drops a .pgc into VCPROFILE_PATH
#   3. Release build with /USEPROFILE
#
# Usage: scripts/build-pgo.ps1 [-WorkDir build-pgo] [-Configuration Release]
# The optimized binary lands in <WorkDir>\usr\bin\winuxcmd.exe.

[CmdletBinding()]
param(
    [string]$WorkDir = "build-pgo",
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
# build-with-vs.ps1 joins the build dir against the repo root itself, so it
# needs the relative name.
$TrainRel = "$WorkDir-train"
$TrainDir = Join-Path $Root $TrainRel

function Invoke-VsBuild {
    param([string]$Dir, [string[]]$Defines, [switch]$ConfigureOnly)
    $args2 = @("-BuildDir", $Dir, "-Target", "winuxcmd", "-Configuration", $Configuration)
    if ($Defines) { $args2 += @("-CMakeExtraArgs", ($Defines -join " ")) }
    if ($ConfigureOnly) { $args2 += "-ConfigureOnly" }
    & powershell.exe -NoProfile -ExecutionPolicy Bypass `
        -File (Join-Path $Root "scripts\build-with-vs.ps1") @args2
    if ($LASTEXITCODE -ne 0) { throw "build failed in $Dir" }
}

Write-Host "=== Step 1: instrumented build (/GENPROFILE) ===" -ForegroundColor Cyan
Invoke-VsBuild -Dir $TrainRel -Defines "-DWINUXCMD_PGO_INSTRUMENT=ON"

$TrainBin = Join-Path $TrainDir "usr\bin\winuxcmd.exe"
if (-not (Test-Path $TrainBin)) { throw "instrumented binary missing: $TrainBin" }

Write-Host "=== Step 2: training workloads ===" -ForegroundColor Cyan
$ProfileDir = Join-Path $TrainDir "pgo"
New-Item -ItemType Directory -Force -Path $ProfileDir | Out-Null
# Every instrumented command spawn (180 hardlinks) writes its own .pgc here.
$env:VCPROFILE_PATH = $ProfileDir
# The instrumented binary needs pgort140.dll (VC PGO runtime), which lives in
# the compiler's host directory — not on PATH outside a VS prompt.
$PgRt = Get-ChildItem "C:\Program Files\Microsoft Visual Studio" -Recurse -Filter pgort140.dll -ErrorAction SilentlyContinue |
    Select-Object -First 1 -ExpandProperty FullName
if ($PgRt) { $env:PATH = "$(Split-Path $PgRt);$env:PATH" }
& bash (Join-Path $Root "scripts\pgo-train.sh") (Join-Path $TrainDir "usr\bin")
if ($LASTEXITCODE -ne 0) { throw "training workloads failed" }
Remove-Item Env:\VCPROFILE_PATH

# Keep only fresh profile data, and fail loudly when training collected
# nothing (upstream's "profile covers <500 functions" guard, MSVC edition).
Get-ChildItem $ProfileDir -Filter *.pgc | Where-Object { $_.Length -eq 0 } | Remove-Item
$Pgc = Get-ChildItem $ProfileDir -Filter *.pgc
if (-not $Pgc) { throw "no .pgc profile data collected; the training workloads did not run" }
Write-Host ("collected {0} .pgc file(s), {1:N1} MB total" -f $Pgc.Count, (($Pgc | Measure-Object Length -Sum).Sum / 1MB))
# /USEPROFILE resolves winuxcmd.pgd relative to the profile directory.
Copy-Item (Join-Path $TrainDir "winuxcmd.pgd") $ProfileDir -Force

Write-Host "=== Step 3: optimized build (/USEPROFILE) ===" -ForegroundColor Cyan
# Configure the final build dir, drop the pgd/pgc next to the (future) exe,
# then link: plain /USEPROFILE reads <output>.pgd from the link directory.
Invoke-VsBuild -Dir $WorkDir -Defines "-DWINUXCMD_PGO_USE=ON" -ConfigureOnly
Copy-Item (Join-Path $ProfileDir "*.pgc") $WorkDir -Force
Copy-Item (Join-Path $ProfileDir "winuxcmd.pgd") $WorkDir -Force
Invoke-VsBuild -Dir $WorkDir

Write-Host ""
Write-Host "Optimized binary: $WorkDir\usr\bin\winuxcmd.exe" -ForegroundColor Green
