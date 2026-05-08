<#
.SYNOPSIS
Install or remove user PowerShell profile hooks for WinuxCmd workspace auto-activation.
.DESCRIPTION
Writes a small profile block into the current user's PowerShell 7 and Windows
PowerShell profile locations so WinuxCmd auto-activates when a shell starts in
this repository. This is opt-in and touches only user profile files.
#>

[CmdletBinding()]
param(
    [string]$Root,
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

function Get-TargetProfilePaths {
    $docs = [Environment]::GetFolderPath("MyDocuments")
    $paths = @(
        (Join-Path $docs "PowerShell\profile.ps1"),
        (Join-Path $docs "WindowsPowerShell\profile.ps1")
    )
    return $paths | Sort-Object -Unique
}

function Ensure-ParentDirectory {
    param([string]$Path)

    $parent = Split-Path -Parent $Path
    if (-not [string]::IsNullOrWhiteSpace($parent) -and
        -not (Test-Path -LiteralPath $parent -PathType Container)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
}

function Backup-ProfileFile {
    param([string]$Path)

    if (Test-Path -LiteralPath $Path -PathType Leaf) {
        $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
        $backupPath = "$Path.bak.$stamp"
        Copy-Item -LiteralPath $Path -Destination $backupPath -Force
        return $backupPath
    }

    return $null
}

function Get-HookBlock {
    param([string]$RootPath)

    return @'
# BEGIN WinuxCmd workspace auto-activation
$repoRoot = '__ROOT__'
if ((Get-Location).Path.StartsWith($repoRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    . (Join-Path $repoRoot 'scripts\activate-workspace.ps1') -Root $repoRoot -Quiet
}
# END WinuxCmd workspace auto-activation
'@.Replace('__ROOT__', $RootPath)
}

function Update-ProfileFile {
    param(
        [string]$Path,
        [string]$HookBlock,
        [switch]$Remove
    )

    Ensure-ParentDirectory -Path $Path
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        New-Item -ItemType File -Path $Path -Force | Out-Null
    }

    $content = Get-Content -LiteralPath $Path -Raw
    $beginTag = "# BEGIN WinuxCmd workspace auto-activation"
    $endTag = "# END WinuxCmd workspace auto-activation"
    $pattern = [regex]::Escape($beginTag) + ".*?" + [regex]::Escape($endTag) + "\r?\n?"
    $updated = $content
    $changed = $false
    $backupPath = $null

    if ($Remove) {
        if ($content -match [regex]::Escape($beginTag)) {
            $backupPath = Backup-ProfileFile -Path $Path
            $updated = [regex]::Replace(
                $content,
                $pattern,
                "",
                [System.Text.RegularExpressions.RegexOptions]::Singleline
            )
            $changed = $true
        }
    } elseif ($content -match [regex]::Escape($beginTag)) {
        $backupPath = Backup-ProfileFile -Path $Path
        $updated = [regex]::Replace(
            $content,
            $pattern,
            $HookBlock,
            [System.Text.RegularExpressions.RegexOptions]::Singleline
        )
        $changed = $true
    } else {
        $backupPath = Backup-ProfileFile -Path $Path
        $updated = if ([string]::IsNullOrWhiteSpace($content)) {
            $HookBlock
        } else {
            $content.TrimEnd() + "`r`n`r`n" + $HookBlock
        }
        $changed = $true
    }

    if ($changed) {
        Set-Content -LiteralPath $Path -Value $updated -Encoding UTF8
    }

    return [pscustomobject]@{
        Path      = $Path
        Changed   = $changed
        Backup    = $backupPath
        HasHook   = (-not $Remove)
    }
}

$rootPath = Resolve-Root -Value $Root
$hookBlock = Get-HookBlock -RootPath $rootPath
$results = @()

foreach ($profilePath in Get-TargetProfilePaths) {
    $results += Update-ProfileFile -Path $profilePath -HookBlock $hookBlock -Remove:$Remove
}

if (-not $Quiet) {
    foreach ($result in $results) {
        if ($result.Changed) {
            if ($result.Backup) {
                Write-Host "Backed up $($result.Path) to $($result.Backup)"
            }
            if ($Remove) {
                Write-Host "Removed WinuxCmd workspace hook from $($result.Path)"
            } else {
                Write-Host "Installed WinuxCmd workspace hook in $($result.Path)"
            }
        } else {
            if ($Remove) {
                Write-Host "No WinuxCmd workspace hook found in $($result.Path)"
            } else {
                Write-Host "WinuxCmd workspace hook already present in $($result.Path)"
            }
        }
    }
}
