<#
.SYNOPSIS
Activate WinuxCmd for the current PowerShell session only.
.DESCRIPTION
Creates .winuxcmd/bin hardlinks if needed and prepends that workspace-local
directory to PATH in the current process. It does not edit profile files,
registry PATH, user PATH, or machine PATH.
#>

[CmdletBinding()]
param(
    [string]$Root,
    [string]$WinuxCmdPath,
    [switch]$Force,
    [switch]$Quiet
)

$ErrorActionPreference = "Stop"

$rootPath = if ([string]::IsNullOrWhiteSpace($Root)) {
    (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
} else {
    (Resolve-Path $Root).Path
}

if ([string]::IsNullOrWhiteSpace($WinuxCmdPath)) {
    & (Join-Path $PSScriptRoot "setup-workspace-bin.ps1") -Root $rootPath -Force:$Force -Quiet:$Quiet
} else {
    & (Join-Path $PSScriptRoot "setup-workspace-bin.ps1") -Root $rootPath -WinuxCmdPath $WinuxCmdPath -Force:$Force -Quiet:$Quiet
}

$binPath = Join-Path $rootPath ".winuxcmd\bin"
$pathParts = $env:PATH -split ";" | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
if ($pathParts -notcontains $binPath) {
    $env:PATH = "$binPath;$env:PATH"
}
$env:WINUXCMD_HOME = $binPath

$aliasNames = @(
    "ls",
    "cat",
    "cp",
    "mv",
    "rm",
    "man",
    "pwd",
    "echo",
    "sort",
    "sleep",
    "tee",
    "clear",
    "kill"
)

foreach ($aliasName in $aliasNames) {
    if (Get-Command $aliasName -CommandType Alias -ErrorAction SilentlyContinue) {
        Remove-Item -Path "Alias:$aliasName" -Force -ErrorAction SilentlyContinue
    }
}

if (-not $Quiet) {
    Write-Host ""
    Write-Host "WinuxCmd workspace activation is active for this PowerShell session."
    Write-Host "Common PowerShell alias collisions were cleared for this session."
    Write-Host "Use explicit .exe command names when you want to be unambiguous, for example:"
    Write-Host "  man.exe ls"
    Write-Host "  grep.exe -n TODO README.md"
    Write-Host "  winuxcmd.exe help sort"
}
