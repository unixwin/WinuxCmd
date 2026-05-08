<#
.SYNOPSIS
Create a workspace-local WinuxCmd command directory.
.DESCRIPTION
Generates command-name .exe hardlinks inside .winuxcmd/bin without changing
the user or machine PATH. This is intended for AI agents and per-repository
shell sessions that need explicit command.exe access without global install
side effects.
#>

[CmdletBinding()]
param(
    [string]$Root,
    [string]$WinuxCmdPath,
    [string]$BinDir,
    [switch]$Force,
    [switch]$Remove,
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

function Find-WinuxCmdExe {
    param([string]$RootPath, [string]$ExplicitPath)

    if (-not [string]::IsNullOrWhiteSpace($ExplicitPath)) {
        $resolved = (Resolve-Path $ExplicitPath).Path
        if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
            throw "winuxcmd.exe not found: $ExplicitPath"
        }
        return $resolved
    }

    $candidates = @(
        (Join-Path $RootPath "build-dev\winuxcmd.exe"),
        (Join-Path $RootPath "build-release\winuxcmd.exe"),
        (Join-Path $RootPath "build\winuxcmd.exe"),
        (Join-Path $RootPath "winuxcmd.exe")
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path $candidate).Path
        }
    }

    $found = Get-ChildItem -LiteralPath $RootPath -Recurse -Filter winuxcmd.exe -File -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -notmatch "\\.winuxcmd\\" } |
        Select-Object -First 1
    if ($found) {
        return $found.FullName
    }

    throw "winuxcmd.exe was not found. Build the project first or pass -WinuxCmdPath."
}

function Get-CommandNames {
    param([string]$RootPath)

    $commandsDir = Join-Path $RootPath "src\commands"
    if (-not (Test-Path -LiteralPath $commandsDir -PathType Container)) {
        throw "Cannot find src\commands under $RootPath"
    }

    $names = Get-ChildItem -LiteralPath $commandsDir -Filter "*.cpp" -File |
        ForEach-Object { $_.BaseName } |
        Sort-Object -Unique

    return @("winuxcmd") + $names
}

function Remove-ExistingFile {
    param([string]$Path)

    if (Test-Path -LiteralPath $Path) {
        Remove-Item -LiteralPath $Path -Force
    }
}

function New-CommandHardLink {
    param([string]$Path, [string]$Target, [switch]$Overwrite)

    if (Test-Path -LiteralPath $Path) {
        if (-not $Overwrite) {
            return "skipped"
        }
        Remove-ExistingFile -Path $Path
    }

    New-Item -ItemType HardLink -Path $Path -Target $Target | Out-Null
    return "created"
}

$rootPath = Resolve-Root -Value $Root
$binPath = if ([string]::IsNullOrWhiteSpace($BinDir)) {
    Join-Path $rootPath ".winuxcmd\bin"
} else {
    $BinDir
}

if ($Remove) {
    if (Test-Path -LiteralPath $binPath) {
        Remove-Item -LiteralPath $binPath -Recurse -Force
    }
    if (-not $Quiet) {
        Write-Host "Removed workspace WinuxCmd bin: $binPath"
    }
    exit 0
}

$winuxExe = Find-WinuxCmdExe -RootPath $rootPath -ExplicitPath $WinuxCmdPath
$commands = Get-CommandNames -RootPath $rootPath

New-Item -ItemType Directory -Path $binPath -Force | Out-Null

$created = 0
$skipped = 0
$failed = 0

foreach ($cmd in $commands) {
    $linkPath = Join-Path $binPath "$cmd.exe"
    try {
        $result = New-CommandHardLink -Path $linkPath -Target $winuxExe -Overwrite:$Force
        if ($result -eq "created") {
            $created++
        } else {
            $skipped++
        }
    } catch {
        $failed++
        Write-Warning "Failed to create $cmd.exe: $($_.Exception.Message)"
    }
}

if ($failed -gt 0) {
    throw "Failed to create $failed command hardlink(s). Hardlinks require an NTFS-like filesystem."
}

if (-not $Quiet) {
    Write-Host "Workspace WinuxCmd bin: $binPath"
    Write-Host "Target winuxcmd.exe: $winuxExe"
    Write-Host "Created: $created, skipped: $skipped"
    Write-Host "Add this directory to the current shell PATH only:"
    Write-Host "  $binPath"
}
