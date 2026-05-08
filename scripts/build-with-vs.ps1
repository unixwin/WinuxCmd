<#
.SYNOPSIS
Build WinuxCmd under a Visual Studio developer environment.
.DESCRIPTION
Initializes the requested Visual Studio command prompt inside cmd.exe, then
configures and builds the selected CMake target in one shell session. The
default build directory is a fresh MSVC-specific folder to avoid reusing a
clang/MSVC-mismatched cache.
#>

[CmdletBinding()]
param(
    [string]$Root,
    [string]$BuildDir = "build-vs",
    [string]$Target = "winuxcmd-tests",
    [string]$Configuration = "Debug",
    [string]$VsEnvScript = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat",
    [string[]]$VsEnvArgs,
    [string]$Arch = "x64",
    [string]$Generator = "Ninja",
    [switch]$ConfigureOnly,
    [switch]$SkipConfigure
)

$ErrorActionPreference = "Stop"

function Resolve-Root {
    param([string]$Value)

    if ([string]::IsNullOrWhiteSpace($Value)) {
        return (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
    }

    return (Resolve-Path $Value).Path
}

function Quote-CmdArg {
    param([string]$Value)

    return '"' + ($Value -replace '"', '\"') + '"'
}

$rootPath = Resolve-Root -Value $Root
$buildPath = Join-Path $rootPath $BuildDir
$cmdExe = Join-Path $env:SystemRoot "System32\cmd.exe"

if (-not (Test-Path -LiteralPath $VsEnvScript -PathType Leaf)) {
    throw "Visual Studio environment script not found: $VsEnvScript"
}

if ($null -eq $VsEnvArgs) {
    $scriptName = Split-Path -Leaf $VsEnvScript
    if ($scriptName -ieq "VsDevCmd.bat") {
        $VsEnvArgs = @("-arch=$Arch")
    } elseif ($scriptName -ieq "vcvarsall.bat") {
        $VsEnvArgs = @($Arch)
    } else {
        $VsEnvArgs = @()
    }
}

$cmakeArgs = @(
    "-S", $rootPath,
    "-B", $buildPath,
    "-G", $Generator,
    "-DCMAKE_BUILD_TYPE=$Configuration"
)

$buildArgs = @(
    "--build", $buildPath,
    "--target", $Target
)

$steps = New-Object System.Collections.Generic.List[string]
if (-not $SkipConfigure) {
    $steps.Add(("cmake " + (($cmakeArgs | ForEach-Object { Quote-CmdArg $_ }) -join " ")))
}
if (-not $ConfigureOnly) {
    $steps.Add(("cmake " + (($buildArgs | ForEach-Object { Quote-CmdArg $_ }) -join " ")))
}

if ($steps.Count -eq 0) {
    Write-Host "Nothing to do. Use -ConfigureOnly, omit -SkipConfigure, or both."
    exit 0
}

$vsCall = "call " + (Quote-CmdArg $VsEnvScript)
if ($VsEnvArgs.Count -gt 0) {
    $vsCall += " " + (($VsEnvArgs | ForEach-Object { Quote-CmdArg $_ }) -join " ")
}
$command = $vsCall + " && " + ($steps -join " && ")

Write-Host "Root: $rootPath"
Write-Host "BuildDir: $buildPath"
Write-Host "Target: $Target"
Write-Host "VS env: $VsEnvScript $($VsEnvArgs -join ' ')"
Write-Host ""

& $cmdExe /d /s /c $command
exit $LASTEXITCODE
