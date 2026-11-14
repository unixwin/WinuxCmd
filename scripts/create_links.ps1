<#
.SYNOPSIS
    WinuxCmd Command Link Generator compatibility wrapper.
.DESCRIPTION
    Delegates command link management to the internal WPM command:
    winuxcmd.exe wpm links rebuild/remove.
.PARAMETER Force
    Overwrite existing command executables when WPM can do so safely.
.PARAMETER UseSymbolicLinks
    Deprecated. WPM manages hardlinks only.
.PARAMETER Remove
    Remove generated command hardlinks. winuxcmd.exe is kept.
.EXAMPLE
    .\create_links.ps1 -Force
    Rebuild hardlinks through WPM.
.EXAMPLE
    .\create_links.ps1 -Remove
    Remove hardlinks through WPM.
#>

[CmdletBinding()]
param(
    [switch]$Force,
    [switch]$UseSymbolicLinks,
    [switch]$Remove
)

$ErrorActionPreference = "Stop"

function Write-ErrorLine {
    param([string]$Text)
    Write-Host "[ERROR] $Text"
}

function Get-WinuxCmdPath {
    $candidate = Join-Path (Get-Location) "winuxcmd.exe"
    if (-not (Test-Path -LiteralPath $candidate)) {
        Write-ErrorLine "winuxcmd.exe not found in current directory."
        Write-Host "[INFO] Current directory: $(Get-Location)"
        Write-Host "[INFO] Run this wrapper from the WinuxCmd bin directory."
        exit 1
    }
    return $candidate
}

if ($UseSymbolicLinks) {
    Write-ErrorLine "-UseSymbolicLinks is no longer supported here."
    Write-Host "[INFO] WPM owns command-link management and creates hardlinks only."
    Write-Host "[INFO] For one-off symlinks, use: winuxcmd.exe ln -s SOURCE LINK"
    exit 2
}

$winuxCmdPath = Get-WinuxCmdPath
# `Get-Location` returns a PathInfo, which has no `.Name`/`.Parent` members -
# reading them yields $null, so the old test was always false and $root stayed
# the bin directory itself. wpm then created usr\bin *inside* usr\bin and the
# 172 entry links landed one level too deep (the archive's documented first-run
# step was broken from 3e1090e1 on; the installer was unaffected because ISS
# passes {app} explicitly). Work on real path strings.
$binDir = (Get-Location).ProviderPath
$root = if ((Split-Path -Leaf $binDir) -eq "bin" -and
            (Split-Path -Leaf (Split-Path -Parent $binDir)) -eq "usr") {
    Split-Path -Parent (Split-Path -Parent $binDir)
} else {
    $binDir
}

if ($Remove) {
    $wpmArgs = @("wpm", "links", "remove", "--root", "$root")
}
else {
    $wpmArgs = @("wpm", "links", "rebuild", "--root", "$root")
    if ($Force) {
        $wpmArgs += "--force"
    }
}

& $winuxCmdPath @wpmArgs
exit $LASTEXITCODE
