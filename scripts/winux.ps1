<#
.SYNOPSIS
WinuxCmd - GNU Coreutils for Windows
.DESCRIPTION
Simple activation script that adds WinuxCmd to PATH and handles alias conflicts
#>

param(
    [ValidateSet("activate", "deactivate", "status", "list")]
    [string]$Action = "activate"
)

$ScriptDir = $PSScriptRoot

# PowerShell aliases that conflict with WinuxCmd commands
$ConflictedAliases = @(
    "cat", "clear", "diff", "group", "kill", "ls", 
    "mv", "ps", "pwd", "rm", "rmdir", "sleep", "sort", "tee", "type", "man"
)

# Aliases intentionally left to PowerShell in the default activation flow
# because overriding them is still surprising in many PowerShell sessions.
$AllScopeAliases = @("echo", "cp", "where")

# ========== Functions ==========

function Get-AvailableCommands {
    $commands = [ordered]@{}
    $ignoredNames = @("winuxcmd")

    foreach ($exe in Get-ChildItem -LiteralPath $ScriptDir -Filter "*.exe" -File -ErrorAction SilentlyContinue) {
        $commandName = $exe.BaseName
        if ($commandName -in $ignoredNames) {
            continue
        }

        $commands[$commandName] = $exe.Name
    }

    return $commands
}

function Save-ConflictedAliases {
    $global:Winux_SavedAliases = @{}

    foreach ($aliasName in $ConflictedAliases) {
        $alias = Get-Alias -Name $aliasName -ErrorAction SilentlyContinue
        if ($alias) {
            $global:Winux_SavedAliases[$aliasName] = @{
                Definition = $alias.Definition
                Options    = $alias.Options -join ","
            }
        }
    }
}

function Set-WinuxAliases {
    $commandMap = Get-AvailableCommands

    foreach ($aliasName in $ConflictedAliases) {
        if (-not $commandMap.Contains($aliasName)) { continue }

        $commandPath = Join-Path $ScriptDir $commandMap[$aliasName]
        if (-not (Test-Path $commandPath)) { continue }

        $alias = Get-Alias -Name $aliasName -ErrorAction SilentlyContinue
        $options = if ($alias) {
            [System.Management.Automation.ScopedItemOptions]$alias.Options
        } else {
            [System.Management.Automation.ScopedItemOptions]::None
        }

        Set-Alias -Name $aliasName -Value $commandPath -Scope Global -Option $options -Force
    }
}

function Restore-ConflictedAliases {
    if (-not $global:Winux_SavedAliases) { return }

    foreach ($aliasName in $global:Winux_SavedAliases.Keys) {
        $saved = $global:Winux_SavedAliases[$aliasName]
        
        try {
            $options = [System.Management.Automation.ScopedItemOptions]$saved.Options
            Set-Alias -Name $aliasName -Value $saved.Definition -Scope Global -Option $options -Force
        }
        catch {
            # If restore fails, ignore it
        }
    }

    Remove-Variable Winux_SavedAliases -Scope Global -ErrorAction SilentlyContinue
}

foreach ($aliasName in $global:Winux_SavedAliases.Keys) {
    $saved = $global:Winux_SavedAliases[$aliasName]
    $options = [System.Management.Automation.ScopedItemOptions]$saved.Options
    Set-Alias -Name $aliasName -Value $saved.Definition -Scope Global -Option $options -Force
}

Remove-Variable Winux_SavedAliases -Scope Global -ErrorAction SilentlyContinue


function Show-CommandList {
    $commandMap = Get-AvailableCommands

    Write-Host "Available WinuxCmd Commands:" -ForegroundColor Cyan
    Write-Host "============================" -ForegroundColor Cyan

    foreach ($cmd in $commandMap.Keys | Sort-Object) {
        Write-Host "  $cmd" -ForegroundColor Yellow -NoNewline
        Write-Host " -> $($commandMap[$cmd])"
    }

    Write-Host ""
    Write-Host "Total: $($commandMap.Count) commands" -ForegroundColor Yellow
    Write-Host "To use these commands, run: .\winux.ps1 activate" -ForegroundColor Green
}

function Show-Status {
    $commandMap = Get-AvailableCommands

    Write-Host "WinuxCmd Status:" -ForegroundColor Cyan
    Write-Host "================" -ForegroundColor Cyan

    # Check if WinuxCmd is in PATH
    $winuxInPath = $env:PATH -split ';' | Where-Object { $_ -eq $ScriptDir }
    
    if ($winuxInPath) {
        Write-Host "Status: ACTIVE" -ForegroundColor Green
        Write-Host "Directory: $ScriptDir" -ForegroundColor Gray
        Write-Host "Commands: $($commandMap.Count) available" -ForegroundColor Yellow
    }
    else {
        Write-Host "Status: INACTIVE" -ForegroundColor Gray
        Write-Host "Run '.\winux.ps1 activate' to enable" -ForegroundColor Gray
    }

    Write-Host ""
    Write-Host "Run '.\winux.ps1 list' to see all commands" -ForegroundColor Gray
}

function Invoke-Activate {
    $commandMap = Get-AvailableCommands

    Write-Host "Activating WinuxCmd..." -ForegroundColor Green

    Save-ConflictedAliases
    Set-WinuxAliases

    # Add WinuxCmd bin directory to PATH
    if ($env:PATH -notlike "$ScriptDir*") {
        $env:PATH = "$ScriptDir;$env:PATH"
    }

    Write-Host "Activation complete!" -ForegroundColor Green
    Write-Host ""
    Write-Host "Total: $($commandMap.Count) commands" -ForegroundColor Yellow
    Write-Host "Directory: $ScriptDir" -ForegroundColor Gray
    Write-Host ""

    # Show note for aliases intentionally left to PowerShell in the default flow.
    if ($AllScopeAliases.Count -gt 0) {
        Write-Host "Note: The default activation flow leaves these PowerShell aliases unchanged:" -ForegroundColor Yellow
        Write-Host "  $($AllScopeAliases -join ', ')" -ForegroundColor Magenta
        Write-Host "  Use the WinuxCmd executable name when needed, for example:" -ForegroundColor Cyan
        Write-Host "  echo.exe, cp.exe, where.exe" -ForegroundColor Green
        Write-Host ""
    }

    Write-Host "Run 'winux status' for details" -ForegroundColor Cyan
    Write-Host "Run 'winux list' to see all commands" -ForegroundColor Cyan
}

function Invoke-Deactivate {
    Write-Host "Deactivating WinuxCmd..." -ForegroundColor Green

    Restore-ConflictedAliases

    Write-Host "Deactivation complete! All original aliases restored." -ForegroundColor Green
}

# ========== Main Logic ==========

switch ($Action) {
    "activate" { Invoke-Activate }
    "deactivate" { Invoke-Deactivate }
    "status" { Show-Status }
    "list" { Show-CommandList }
    default { Invoke-Activate }
}

# Cleanup on exit
Register-EngineEvent -SourceIdentifier PowerShell.Exiting -Action {
    if ($global:Winux_SavedAliases) {
        Restore-ConflictedAliases
    }
} | Out-Null
