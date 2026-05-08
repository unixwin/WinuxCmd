<#
.SYNOPSIS
Package the WinuxCmd skill into a standalone release archive.
.DESCRIPTION
Creates a zip archive rooted at winuxcmd/ so the skill can be attached to
GitHub releases or shipped independently.
#>

[CmdletBinding()]
param(
    [string]$Root,
    [string]$Version,
    [string]$OutputDir = "artifacts/skills",
    [switch]$Quiet
)

$ErrorActionPreference = "Stop"

function Resolve-Root {
    param([string]$Value)

    if ([string]::IsNullOrWhiteSpace($Value)) {
        return (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
    }

    return (Resolve-Path $Value).Path
}

function Get-Version {
    param([string]$RootPath, [string]$ExplicitVersion)

    if (-not [string]::IsNullOrWhiteSpace($ExplicitVersion)) {
        if ($ExplicitVersion -notmatch '^\d+\.\d+\.\d+$') {
            throw "Invalid version format: $ExplicitVersion"
        }
        return $ExplicitVersion
    }

    $versionPath = Join-Path $RootPath "PROJECT_VERSION"
    if (-not (Test-Path -LiteralPath $versionPath -PathType Leaf)) {
        throw "Cannot find PROJECT_VERSION under $RootPath"
    }

    $detected = (Get-Content -LiteralPath $versionPath -Raw).Trim()
    if ($detected -notmatch '^\d+\.\d+\.\d+$') {
        throw "Invalid version in PROJECT_VERSION: $detected"
    }

    return $detected
}

function Resolve-OutputDir {
    param([string]$RootPath, [string]$Value)

    if ([System.IO.Path]::IsPathRooted($Value)) {
        return $Value
    }

    return (Join-Path $RootPath $Value)
}

$rootPath = Resolve-Root -Value $Root
$skillsRoot = Join-Path $rootPath "skills"
$skillDir = Join-Path $skillsRoot "winuxcmd"
if (-not (Test-Path -LiteralPath $skillDir -PathType Container)) {
    throw "Skill directory not found: $skillDir"
}

$requiredFiles = @(
    (Join-Path $skillDir "SKILL.md"),
    (Join-Path (Join-Path $skillDir "agents") "openai.yaml")
)
foreach ($required in $requiredFiles) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Missing required skill file: $required"
    }
}

$versionValue = Get-Version -RootPath $rootPath -ExplicitVersion $Version
$targetDir = Resolve-OutputDir -RootPath $rootPath -Value $OutputDir
if (-not (Test-Path -LiteralPath $targetDir -PathType Container)) {
    New-Item -ItemType Directory -Path $targetDir -Force | Out-Null
}

$packageName = "WinuxCmd-skill-v$versionValue.zip"
$outputPath = Join-Path $targetDir $packageName

if (Test-Path -LiteralPath $outputPath) {
    Remove-Item -LiteralPath $outputPath -Force
}

Compress-Archive -Path $skillDir -DestinationPath $outputPath

if (-not $Quiet) {
    Write-Host "Packaged skill archive: $outputPath"
}

Write-Output $outputPath
