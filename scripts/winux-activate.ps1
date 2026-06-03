<#
.SYNOPSIS
WinuxCmd Profile Initializer - One-time setup
.DESCRIPTION
Adds winux wrapper to PowerShell profile without PATH dependency
Run this ONCE after installing WinuxCmd.
#>

param(
    [switch]$Install,
    [switch]$Uninstall
)

$ErrorActionPreference = "Stop"

function Write-Color {
    param($Color, $Text)
    # Simple text-based markers for older terminals
    $Markers = @{
        Green  = "[OK]"
        Yellow = "[INFO]"
        Red    = "[ERROR]"
        Blue   = "[NOTE]"
        Cyan   = "[INFO]"
    }
    Write-Host "$($Markers[$Color]) $Text"
}

# ========== Find WinuxCmd installation ==========
function Get-WinuxBinDir {
    # Priority 1: Check current directory (Scoop installation scenario)
    if (Test-Path ".\winuxcmd.exe") {
        return (Get-Location).Path
    }
    
    # Priority 2: Check script directory
    if ($PSScriptRoot -and (Test-Path (Join-Path $PSScriptRoot "winuxcmd.exe"))) {
        return $PSScriptRoot
    }
    
    # Priority 3: Check environment variable
    if ($env:WINUXCMD_HOME -and (Test-Path $env:WINUXCMD_HOME)) {
        $winuxExe = Join-Path $env:WINUXCMD_HOME "winuxcmd.exe"
        if (Test-Path $winuxExe) {
            return $env:WINUXCMD_HOME
        }
    }
    
    # Priority 4: Check $env:LOCALAPPDATA\WinuxCmd (traditional installation)
    $baseDir = "$env:LOCALAPPDATA\WinuxCmd"
    if (Test-Path $baseDir) {
        $versionDir = Get-ChildItem -Path $baseDir -Directory -Filter "WinuxCmd-*" |
                      Select-Object -First 1
        if ($versionDir) {
            $binDir = Join-Path $versionDir.FullName "bin"
            if (-not (Test-Path $binDir)) {
                $exeFile = Get-ChildItem -Path $versionDir.FullName -Filter "winuxcmd.exe" -Recurse -File |
                           Select-Object -First 1
                if ($exeFile) {
                    $binDir = $exeFile.DirectoryName
                }
            }
            if (Test-Path $binDir) {
                return $binDir
            }
        }
    }
    
    # Priority 5: Check PATH for winuxcmd.exe
    $winuxPath = Get-Command winuxcmd.exe -ErrorAction SilentlyContinue
    if ($winuxPath) {
        return Split-Path $winuxPath.Source
    }
    
    Write-Color "Red" "WinuxCmd not found. Please install WinuxCmd first."
    return $null
}

# ========== Install to Profile ==========
function Install-WinuxToProfile {
    param([string]$BinDir)

    $winuxPs1Path = Join-Path $BinDir "winux.ps1"
    $winuxCmdPath = Join-Path $BinDir "winuxcmd.exe"

    # Verify winuxcmd.exe exists
    if (-not (Test-Path $winuxCmdPath)) {
        Write-Color "Red" "Error: winuxcmd.exe not found at: $winuxCmdPath"
        return $false
    }

    # Dynamically find the latest winux version
    $winuxFunction = @'
# Find winuxcmd.exe and set alias for it.
function Update-WinuxCmdAlias {
    $baseDir = "$env:LOCALAPPDATA\WinuxCmd"
    if (-not (Test-Path $baseDir)) {
        return
    }

    $dirs = Get-ChildItem -Path $baseDir -Directory -Filter "WinuxCmd-*" -ErrorAction SilentlyContinue
    $latestExe = $null

    foreach ($dir in $dirs) {
        if ($dir.Name -match 'WinuxCmd-(\d+\.\d+\.\d+)-win-(x64|arm64)') {
            $exePath = Join-Path $dir.FullName "bin\winuxcmd.exe"
            if (Test-Path $exePath) {
                $latestExe = $exePath
                # continue search for potential latest version.
            }
        }
    }

    if ($latestExe) {
        Set-Alias -Name winuxcmd -Value $latestExe -Scope Global -Force -ErrorAction SilentlyContinue
    }
}

# WinuxCmd wrapper (dynamically finds latest version)
function global:winux {
    param(
        [Parameter(Position=0, ValueFromRemainingArguments=$true)]
        [string[]]$Arguments
    )

    function Find-LatestWinuxCmd {
        $baseDir = "$env:LOCALAPPDATA\WinuxCmd"
        if (-not (Test-Path $baseDir)) {
            return $null
        }

        # Fetch all version then sort.
        $allVersions = @()
        $dirs = Get-ChildItem -Path $baseDir -Directory -Filter "WinuxCmd-*" -ErrorAction SilentlyContinue

        foreach ($dir in $dirs) {
            if ($dir.Name -match 'WinuxCmd-(\d+\.\d+\.\d+)-win-(x64|arm64)') {
                try {
                    $version = [Version]$Matches[1]
                    $allVersions += [PSCustomObject]@{
                        Directory = $dir
                        Version = $version
                        FullName = $dir.FullName
                        VersionString = $Matches[1]
                    }
                }
                catch {
                    # skip parser error
                    continue
                }
            }
        }

        if ($allVersions.Count -eq 0) {
            return $null
        }

        # sort from by version
        $sorted = $allVersions | Sort-Object Version -Descending
        $latestDir = $sorted[0].FullName

        # find new and exe.
        $binDir = Join-Path $latestDir "bin"
        $winuxCmdPath = Join-Path $binDir "winuxcmd.exe"
        $winuxPs1Path = Join-Path $binDir "winux.ps1"

        if (-not (Test-Path $winuxCmdPath)) {
            $exeFile = Get-ChildItem -Path $latestDir -Filter "winuxcmd.exe" -Recurse -File -ErrorAction SilentlyContinue |
                       Select-Object -First 1
            if ($exeFile) {
                $binDir = $exeFile.DirectoryName
                $winuxCmdPath = $exeFile.FullName
                $winuxPs1Path = Join-Path $binDir "winux.ps1"
            } else {
                return $null
            }
        }

        return @{
            BinDir = $binDir
            WinuxCmdExe = $winuxCmdPath
            WinuxPs1 = $winuxPs1Path
            Version = $sorted[0].VersionString
        }
    }

    # Get WinuxCmd Path
    $winuxPaths = Find-LatestWinuxCmd
    if (-not $winuxPaths) {
        Write-Host "WinuxCmd not found. Please install WinuxCmd first."
        Write-Host "Expected location: $env:LOCALAPPDATA\WinuxCmd"
        return
    }

    $winuxPs1Path = $winuxPaths.WinuxPs1
    $winuxCmdPath = $winuxPaths.WinuxCmdExe
    $winuxVersion = $winuxPaths.Version

    # Handle empty arguments (just 'winux' command)
    if ($Arguments.Count -eq 0) {
        Write-Host "WinuxCmd v$winuxVersion - GNU Coreutils for Windows"
        Write-Host "==================================================="
        Write-Host ""
        Write-Host "Installation:"
        Write-Host "  Location: $($winuxPaths.BinDir)"
        Write-Host ""
        Write-Host "Management Commands:"
        Write-Host "  winux activate          - Enable GNU commands"
        Write-Host "  winux deactivate        - Restore original commands"
        Write-Host "  winux status            - Check activation status"
        Write-Host "  winux list              - List available commands"
        Write-Host "  winux version           - Show version"
        Write-Host "  winux help              - Show this help"
        Write-Host ""
        Write-Host "GNU Commands (direct):"
        Write-Host "  winux ls -la            - List files"
        Write-Host "  winux cp source dest    - Copy files"
        Write-Host "  winux mv source dest    - Move files"
        Write-Host "  winux rm file           - Remove file"
        Write-Host "  winux cat file          - Show file content"
        Write-Host "  winux mkdir dir         - Create directory"
        Write-Host ""
        Write-Host "Direct Access:"
        Write-Host "  winuxcmd --help         - Show winuxcmd help"
        return
    }

    # Get the first argument as the command
    $Command = $Arguments[0]
    $RemainingArgs = if ($Arguments.Count -gt 1) { $Arguments[1..($Arguments.Count - 1)] } else { @() }

    # Management commands that go to winux.ps1
    $managementCommands = @("activate", "deactivate", "status", "list", "help", "version")

    # Check if this is a management command
    if ($Command -in $managementCommands) {
        switch ($Command) {
            "activate" {
                if (Test-Path $winuxPs1Path) {
                    & $winuxPs1Path -Action activate @RemainingArgs
                } else {
                    Write-Host "winux.ps1 not found. Cannot activate."
                    Write-Host "Copy winux.ps1 to: $winuxPs1Path"
                }
            }
            "deactivate" {
                if (Test-Path $winuxPs1Path) {
                    & $winuxPs1Path -Action deactivate @RemainingArgs
                } else {
                    Write-Host "winux.ps1 not found. Cannot deactivate."
                }
            }
            "status" {
                if (Test-Path $winuxPs1Path) {
                    & $winuxPs1Path -Action status @RemainingArgs
                } else {
                    Write-Host "winux.ps1 not found. Status unknown."
                }
            }
            "list" {
                if (Test-Path $winuxPs1Path) {
                    & $winuxPs1Path -Action list @RemainingArgs
                } else {
                    Write-Host "winux.ps1 not found. Cannot list commands."
                    Write-Host "Direct GNU commands available via winuxcmd.exe"
                }
            }
            "help" {
                Write-Host "WinuxCmd v$winuxVersion - GNU Coreutils for Windows"
                Write-Host "==================================================="
                Write-Host ""
                Write-Host "Installation:"
                Write-Host "  Location: $($winuxPaths.BinDir)"
                Write-Host ""
                Write-Host "Management Commands:"
                Write-Host "  winux activate          - Enable GNU commands"
                Write-Host "  winux deactivate        - Restore original commands"
                Write-Host "  winux status            - Check activation status"
                Write-Host "  winux list              - List available commands"
                Write-Host "  winux version           - Show version"
                Write-Host "  winux help              - Show this help"
                Write-Host ""
                Write-Host "GNU Commands (direct):"
                Write-Host "  winux ls -la            - List files"
                Write-Host "  winux cp source dest    - Copy files"
                Write-Host "  winux mv source dest    - Move files"
                Write-Host "  winux rm file           - Remove file"
                Write-Host "  winux cat file          - Show file content"
                Write-Host "  winux mkdir dir         - Create directory"
                Write-Host ""
                Write-Host "Direct Access:"
                Write-Host "  winuxcmd --help         - Show winuxcmd help"
            }
            "version" {
                if (Test-Path $winuxCmdPath) {
                    & $winuxCmdPath --version
                } else {
                    Write-Host "winuxcmd.exe not found"
                }
            }
        }
        return
    }

    # Special case: --help and --version flags (when used as first argument)
    if ($Command -eq "--help" -or $Command -eq "-h") {
        if (Test-Path $winuxCmdPath) {
            & $winuxCmdPath --help
        }
        return
    }

    if ($Command -eq "--version" -or $Command -eq "-v") {
        if (Test-Path $winuxCmdPath) {
            & $winuxCmdPath --version
        }
        return
    }

    # All other commands: pass through to winuxcmd.exe
    if (Test-Path $winuxCmdPath) {
        # Build argument list properly - include the command and all remaining args
        & $winuxCmdPath @Arguments
    } else {
        Write-Host "winuxcmd.exe not found at: $winuxCmdPath"
    }
}
Register-EngineEvent -SourceIdentifier PowerShell.Exiting -Action {
    $baseDir = "$env:LOCALAPPDATA\WinuxCmd"
    if (Test-Path $baseDir) {
        $configFile = Join-Path $baseDir "last_alias.json"
        $currentAlias = (Get-Alias winuxcmd -ErrorAction SilentlyContinue).Definition

        if ($currentAlias -and (Test-Path $currentAlias)) {
            @{
                LastPath = $currentAlias
                LastUpdate = (Get-Date).ToString("o")
            } | ConvertTo-Json | Set-Content $configFile -Encoding UTF8
        }
    }
} | Out-Null
'@

    $profilePath = $PROFILE.CurrentUserAllHosts

    # Ensure profile directory exists
    $profileDir = Split-Path $profilePath -Parent
    if (-not (Test-Path $profileDir)) {
        New-Item -ItemType Directory -Path $profileDir -Force | Out-Null
    }

    # read current profile
    $currentContent = ""
    if (Test-Path $profilePath) {
        $currentContent = Get-Content $profilePath -Raw
        $currentContent = $currentContent.Trim()
    }

    # remove old configuration.
    $patterns = @(
        '(?s)# WinuxCmd wrapper.*?(?=^# Find winuxcmd\.exe|\Z)',
        '(?s)^# set alias\s*\r?\nUpdate-WinuxCmdAlias\s*\r?\n\r?\n',
        '(?s)^function Update-WinuxCmdAlias\s*\{.*?^\}',
        '(?s)^function global:winux\s*\{.*?^\}',
        '^Set-Alias -Name winuxcmd -Value [^\r\n]+\r?\n?',
        '(?s)^# =+[\r\n]+# WinuxCmd Integration.*?# =+[\r\n]+# End WinuxCmd Integration[\r\n]+',
        '(?s)^Register-EngineEvent -SourceIdentifier PowerShell\.Exiting -Action \{.*?\} \| Out-Null'
    )

    foreach ($pattern in $patterns) {
        $currentContent = $currentContent -replace $pattern, ''
    }

    # remove extra space
    $currentContent = $currentContent -replace '\n\n\n+', "`n`n"
    $currentContent = $currentContent.Trim()

    # add new dynamical function
    $newContent = $currentContent + "`n`n" + $winuxFunction
    Set-Content -Path $profilePath -Value $newContent -Encoding UTF8 -Force

    return $true
}

# ========== Main Script ==========
Write-Color "Cyan" "WinuxCmd Profile Initializer"
Write-Color "Cyan" "==========================="
Write-Host ""

# ── New: --install flag for one-shot permanent setup ──────────────────────────
if ($Install) {
    Write-Color "Yellow" "Permanent installation: PATH + Tab completion..."
    Write-Host ""

    $binDir = Get-WinuxBinDir
    if (-not $binDir) {
        Write-Color "Red" "WinuxCmd installation not found."
        exit 1
    }
    Write-Color "Cyan" "Found WinuxCmd at: $binDir"

    # 1. Add bin dir to HKCU\Environment\PATH (no admin required)
    $userPath = [System.Environment]::GetEnvironmentVariable("PATH", "User")
    if ($userPath -notlike "*$binDir*") {
        $newPath = if ($userPath) { "$binDir;$userPath" } else { $binDir }
        [System.Environment]::SetEnvironmentVariable("PATH", $newPath, "User")
        Write-Color "Green" "Added '$binDir' to user PATH registry."
        Write-Host "       (Effective in new sessions / after re-login)"
    } else {
        Write-Color "Green" "Already in user PATH."
    }
    # Also add to current session
    $env:PATH = "$binDir;$env:PATH"

    # 2. Dot-source completion script from $PROFILE
    $completionScript = Join-Path $binDir "winuxcmd-completions.ps1"
    if (-not (Test-Path $completionScript)) {
        # Try script directory
        $completionScript = Join-Path (Split-Path $MyInvocation.MyCommand.Path) "winuxcmd-completions.ps1"
    }
    if (Test-Path $completionScript) {
        $marker  = "# WinuxCmd Tab Completion"
        $profPath = $PROFILE.CurrentUserCurrentHost
        $profDir  = Split-Path $profPath
        if ($profDir -and -not (Test-Path $profDir)) {
            New-Item $profDir -ItemType Directory -Force | Out-Null
        }
        if (-not (Test-Path $profPath)) {
            New-Item $profPath -ItemType File -Force | Out-Null
        }
        $content = Get-Content $profPath -Raw -ErrorAction SilentlyContinue
        if ($content -notlike "*$marker*") {
            Add-Content $profPath "`r`n$marker`r`. '$completionScript'`r`n"
            Write-Color "Green" "Tab completion added to: $profPath"
        } else {
            Write-Color "Green" "Tab completion already in profile."
        }
        # Load immediately in this session
        . $completionScript
        Write-Color "Green" "Tab completion active in this session NOW."
    } else {
        Write-Color "Yellow" "winuxcmd-completions.ps1 not found - skipping Tab completion setup."
        Write-Host "       Copy winuxcmd-completions.ps1 next to winuxcmd.exe and re-run."
    }

    Write-Host ""
    Write-Color "Green" "Installation complete!"
    Write-Host "  - Open a new PowerShell window: ls, grep, tree etc. are on PATH"
    Write-Host "  - Tab completes commands and their options with descriptions"
    Write-Host "  - No need to run this script again"
    exit 0
}

if ($Uninstall) {
    Write-Color "Yellow" "Removing permanent installation..."
    $binDir = Get-WinuxBinDir
    if ($binDir) {
        $userPath = [System.Environment]::GetEnvironmentVariable("PATH", "User")
        if ($userPath -like "*$binDir*") {
            $newPath = ($userPath -split ";" | Where-Object { $_ -ne $binDir }) -join ";"
            [System.Environment]::SetEnvironmentVariable("PATH", $newPath, "User")
            Write-Color "Green" "Removed from user PATH registry."
        }
    }
    # Remove completion line from profile
    foreach ($prof in @($PROFILE.CurrentUserCurrentHost, $PROFILE.CurrentUserAllHosts)) {
        if (-not $prof -or -not (Test-Path $prof)) { continue }
        $lines = Get-Content $prof | Where-Object {
            $_ -notmatch '# WinuxCmd Tab Completion' -and
            $_ -notmatch "winuxcmd-completions\.ps1"
        }
        Set-Content $prof $lines -Encoding UTF8
        Write-Color "Green" "Cleaned profile: $prof"
    }
    Write-Color "Green" "Uninstall complete. Open a new shell to take effect."
    exit 0
}

# ── Legacy flow (no flags) ────────────────────────────────────────────────────

# Find WinuxCmd (For Verify Installation)
$binDir = Get-WinuxBinDir
if (-not $binDir) {
    Write-Color "Red" "Failed to find WinuxCmd installation"
    Write-Host "Please install WinuxCmd first."
    exit 1
}

Write-Color "Cyan" "Found WinuxCmd at: $binDir"

# Get profile path for display
$profilePath = $PROFILE.CurrentUserAllHosts

# Ask for confirmation
Write-Host ""
Write-Host "This will add the 'winux' command to your PowerShell profile."
Write-Host "Profile location: $profilePath"
Write-Host ""

$confirm = Read-Host "Continue? (Y/N)"
if ($confirm -notmatch '^[Yy]') {
    Write-Color "Yellow" "Cancelled."
    exit 0
}

# Install to profile.
if (Install-WinuxToProfile -BinDir $binDir) {
    Write-Color "Green" "WinuxCmd added to PowerShell profile"
    Write-Host ""
    Write-Color "Cyan" "Features:"
    Write-Host "  - Automatically finds latest WinuxCmd version"
    Write-Host "  - No need to re-run after updates"
    Write-Host "  - Works with multiple installed versions"
    Write-Host ""
    Write-Color "Cyan" "Next steps:"
    Write-Host "1. RESTART PowerShell or run: . `$PROFILE.CurrentUserAllHosts"
    Write-Host "2. Test with: winux"
    Write-Host "3. If you have winux.ps1, copy it to WinuxCmd bin directory"
    Write-Host "4. Optional: winux activate (if winux.ps1 exists)"
    Write-Host ""
    Write-Color "Cyan" "Usage after restart:"
    Write-Host "  > winux                     # Show help and version info"
    Write-Host "  > winux ls -la              # Use GNU ls directly"
    Write-Host "  > winux activate            # Activate (if winux.ps1 exists)"
    Write-Host "  > winuxcmd --help           # Direct alias toe winuxcmd.exe"
    Write-Host ""
    Write-Host "Note: Future WinuxCmd updates will be automatically detected!"
} else {
    Write-Color "Red" "Failed to add WinuxCmd to profile"
    exit 1
}
