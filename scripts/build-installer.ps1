<#
.SYNOPSIS
Build a staged WinuxCmd install tree and compile the Inno Setup installer.
#>

[CmdletBinding()]
param(
    [string]$Root,
    [string]$BuildDir = "build-vs-installer",
    [string]$Configuration = "Release",
    [string]$Generator = "Ninja",
    [string]$Arch = "x64",
    [string]$VsEnvScript = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat",
    [string[]]$VsEnvArgs,
    [string]$StageDir,
    [string]$OutputDir,
    [string]$IsccPath,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

function Resolve-Root {
    param([string]$Value)

    if ([string]::IsNullOrWhiteSpace($Value)) {
        return (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
    }

    return (Resolve-Path $Value).Path
}

function Get-ProjectVersion {
    param([string]$ProjectRoot)

    return (Get-Content (Join-Path $ProjectRoot "PROJECT_VERSION") -Raw).Trim()
}

function Resolve-IsccPath {
    param([string]$Candidate)

    if ($Candidate) {
        if (-not (Test-Path -LiteralPath $Candidate)) {
            throw "ISCC.exe not found: $Candidate"
        }
        return (Resolve-Path $Candidate).Path
    }

    $fromPath = Get-Command iscc.exe -ErrorAction SilentlyContinue
    if ($fromPath) {
        return $fromPath.Source
    }

    foreach ($path in @(
        "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
        "C:\Program Files\Inno Setup 6\ISCC.exe"
    )) {
        if (Test-Path -LiteralPath $path) {
            return $path
        }
    }

    throw "ISCC.exe not found. Install Inno Setup 6 or pass -IsccPath."
}

$rootPath = Resolve-Root -Value $Root
$buildPath = Join-Path $rootPath $BuildDir
$stagePath = if ($StageDir) { $StageDir } else { Join-Path $buildPath "stage" }
$outputPath = if ($OutputDir) { $OutputDir } else { Join-Path $rootPath "dist" }
$resolvedStagePath = [System.IO.Path]::GetFullPath($stagePath)
$resolvedOutputDir = [System.IO.Path]::GetFullPath($outputPath)
$projectVersion = Get-ProjectVersion -ProjectRoot $rootPath
$issPath = Join-Path $rootPath "packaging\winuxcmd.iss"
$buildScript = Join-Path $rootPath "scripts\build-with-vs.ps1"

if (-not (Test-Path -LiteralPath $issPath)) {
    throw "Installer script not found: $issPath"
}

if (-not $SkipBuild) {
    if (-not (Test-Path -LiteralPath $VsEnvScript)) {
        throw "Visual Studio environment script not found: $VsEnvScript"
    }

    if (-not (Test-Path -LiteralPath $buildScript)) {
        throw "Build helper not found: $buildScript"
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
}

New-Item -ItemType Directory -Path $resolvedOutputDir -Force | Out-Null
if (Test-Path -LiteralPath $resolvedStagePath) {
    Remove-Item -LiteralPath $resolvedStagePath -Recurse -Force
}
New-Item -ItemType Directory -Path $resolvedStagePath -Force | Out-Null

Write-Host "Root: $rootPath"
Write-Host "BuildDir: $buildPath"
Write-Host "StageDir: $resolvedStagePath"
Write-Host "OutputDir: $resolvedOutputDir"
Write-Host "Version: $projectVersion"
Write-Host "Arch: $Arch"
Write-Host ""

if (-not $SkipBuild) {
    $buildScriptArgs = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", $buildScript,
        "-Root", $rootPath,
        "-BuildDir", $BuildDir,
        "-Target", "winuxcmd",
        "-Configuration", $Configuration,
        "-VsEnvScript", $VsEnvScript,
        "-Arch", $Arch,
        "-Generator", $Generator
    )

    if ($VsEnvArgs.Count -gt 0) {
        $buildScriptArgs += @("-VsEnvArgs")
        $buildScriptArgs += $VsEnvArgs
    }

    & powershell @buildScriptArgs
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

& cmake --install $buildPath --prefix $resolvedStagePath
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$resolvedIsccPath = Resolve-IsccPath -Candidate $IsccPath
$isccArgs = @(
    "/DAppVersion=$projectVersion",
    "/DArchSlug=$Arch",
    "/DStageDir=$resolvedStagePath",
    "/DOutputDir=$resolvedOutputDir",
    $issPath
)

Write-Host "ISCC: $resolvedIsccPath"
& $resolvedIsccPath @isccArgs
exit $LASTEXITCODE
