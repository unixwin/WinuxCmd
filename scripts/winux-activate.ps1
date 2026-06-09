<#
.SYNOPSIS
WinuxCmd Profile Initializer - One-time setup
.DESCRIPTION
Adds winux wrapper to PowerShell profile without PATH dependency
Run this ONCE after installing WinuxCmd.
#>

param(
    [switch]$Install,
    [switch]$Uninstall,
    [switch]$ProfilesOnly,
    [switch]$SkipProfileUpdate,
    [switch]$Quiet
)

$ErrorActionPreference = "Stop"

function Write-Color {
    param($Color, $Text)
    if ($Quiet) {
        return
    }
    $Markers = @{
        Green  = "[OK]"
        Yellow = "[INFO]"
        Red    = "[ERROR]"
        Blue   = "[NOTE]"
        Cyan   = "[INFO]"
    }
    Write-Host "$($Markers[$Color]) $Text"
}

function Get-ProfileInstallPath {
    if (-not [string]::IsNullOrWhiteSpace($env:WINUXCMD_PROFILE_PATH)) {
        return $env:WINUXCMD_PROFILE_PATH
    }

    return $PROFILE.CurrentUserAllHosts
}

function Get-ProfileInstallPaths {
    if (-not [string]::IsNullOrWhiteSpace($env:WINUXCMD_PROFILE_PATH)) {
        return @($env:WINUXCMD_PROFILE_PATH)
    }

    $docs = [Environment]::GetFolderPath("MyDocuments")
    return @(
        (Join-Path $docs "WindowsPowerShell\profile.ps1"),
        (Join-Path $docs "WindowsPowerShell\Microsoft.PowerShell_profile.ps1"),
        (Join-Path $docs "PowerShell\profile.ps1"),
        (Join-Path $docs "PowerShell\Microsoft.PowerShell_profile.ps1")
    ) | Sort-Object -Unique
}

function Get-WinuxInstallRoots {
    $roots = New-Object System.Collections.Generic.List[string]

    if (-not [string]::IsNullOrWhiteSpace($env:WINUXCMD_INSTALL_ROOTS)) {
        foreach ($root in ($env:WINUXCMD_INSTALL_ROOTS -split ';')) {
            if (-not [string]::IsNullOrWhiteSpace($root) -and (Test-Path $root)) {
                $roots.Add((Resolve-Path $root).Path)
            }
        }
    }

    $classicRoot = Join-Path $env:LOCALAPPDATA "WinuxCmd"
    if (Test-Path $classicRoot) {
        $roots.Add((Resolve-Path $classicRoot).Path)
    }

    foreach ($root in @(
        (Join-Path $env:LOCALAPPDATA "Programs\WinuxCmd"),
        (Join-Path $env:ProgramFiles "WinuxCmd"),
        (Join-Path ${env:ProgramFiles(x86)} "WinuxCmd")
    )) {
        if (-not [string]::IsNullOrWhiteSpace($root) -and (Test-Path $root)) {
            $roots.Add((Resolve-Path $root).Path)
        }
    }

    $wingetPackagesRoot = Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Packages"
    if (Test-Path $wingetPackagesRoot) {
        foreach ($pkgRoot in Get-ChildItem -Path $wingetPackagesRoot -Directory -Filter "*WinuxCmd*" -ErrorAction SilentlyContinue) {
            $roots.Add($pkgRoot.FullName)
        }
    }

    return $roots | Select-Object -Unique
}

function Get-WinuxVersionDirectories {
    $versionDirs = New-Object System.Collections.Generic.List[object]

    foreach ($root in Get-WinuxInstallRoots) {
        $rootName = Split-Path $root -Leaf
        if ((Test-Path (Join-Path $root "winuxcmd.exe")) -or (Test-Path (Join-Path $root "bin\winuxcmd.exe"))) {
            $item = Get-Item $root
            $item | Add-Member -NotePropertyName WinuxPriority -NotePropertyValue 3 -Force
            $versionDirs.Add($item)
            continue
        }

        if ($rootName -like "WinuxCmd-*") {
            $item = Get-Item $root
            $item | Add-Member -NotePropertyName WinuxPriority -NotePropertyValue 2 -Force
            $versionDirs.Add($item)
            continue
        }

        foreach ($dir in Get-ChildItem -Path $root -Directory -Filter "WinuxCmd-*" -ErrorAction SilentlyContinue) {
            $dir | Add-Member -NotePropertyName WinuxPriority -NotePropertyValue 1 -Force
            $versionDirs.Add($dir)
        }
    }

    return $versionDirs |
        Sort-Object -Property @{
            Expression = { $_.WinuxPriority }
            Descending = $true
        }, @{
            Expression = {
                if ($_.Name -match 'WinuxCmd-(\d+\.\d+\.\d+)') {
                    [Version]$Matches[1]
                } else {
                    [Version]"0.0.0"
                }
            }
            Descending = $true
        }
}

function Remove-LegacyProfileBlocks {
    param([string]$Content)

    $options = [System.Text.RegularExpressions.RegexOptions]::Singleline -bor
               [System.Text.RegularExpressions.RegexOptions]::Multiline
    $patterns = @(
        '^# >>> WinuxCmd integration >>>\r?\n.*?^# <<< WinuxCmd integration <<<\r?\n?',
        '^# Enumerate supported install roots\. WINUXCMD_INSTALL_ROOTS is mainly for\r?\n.*?^Register-EngineEvent -SourceIdentifier PowerShell\.Exiting -Action \{.*?\} \| Out-Null\r?\n?',
        '^# Enumerate supported install roots\. WINUXCMD_INSTALL_ROOTS is mainly for\r?\n.*?^# Find winuxcmd\.exe and set alias for it\.\r?\n?',
        '^# Find winuxcmd\.exe and set alias for it\.\r?\n?',
        '# WinuxCmd wrapper.*?(?=^# Find winuxcmd\.exe|\Z)',
        '^# set alias\s*\r?\nUpdate-WinuxCmdAlias\s*\r?\n\r?\n',
        '^function Update-WinuxCmdAlias\s*\{.*?^\}',
        '^function global:winux\s*\{.*?^\}',
        '^Set-Alias -Name winuxcmd -Value [^\r\n]+\r?\n?',
        '^# =+[\r\n]+# WinuxCmd Integration.*?# =+[\r\n]+# End WinuxCmd Integration[\r\n]*',
        '^Register-EngineEvent -SourceIdentifier PowerShell\.Exiting -Action \{.*?\} \| Out-Null\r?\n?'
    )

    foreach ($pattern in $patterns) {
        $Content = [regex]::Replace($Content, $pattern, '', $options)
    }

    return $Content
}

function Get-WinuxBinDir {
    if (Test-Path ".\winuxcmd.exe") {
        return (Get-Location).Path
    }

    if ($PSScriptRoot -and (Test-Path (Join-Path $PSScriptRoot "winuxcmd.exe"))) {
        return $PSScriptRoot
    }

    if ($env:WINUXCMD_HOME -and (Test-Path $env:WINUXCMD_HOME)) {
        $winuxExe = Join-Path $env:WINUXCMD_HOME "winuxcmd.exe"
        if (Test-Path $winuxExe) {
            return $env:WINUXCMD_HOME
        }
    }

    $versionDirs = Get-WinuxVersionDirectories
    if ($versionDirs.Count -gt 0) {
        foreach ($versionDir in $versionDirs) {
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

    $winuxPath = Get-Command winuxcmd.exe -ErrorAction SilentlyContinue
    if ($winuxPath) {
        return Split-Path $winuxPath.Source
    }

    Write-Color "Red" "WinuxCmd not found. Please install WinuxCmd first."
    return $null
}

function Install-WinuxToProfile {
    param([string]$BinDir)

    $winuxPs1Path = Join-Path $BinDir "winux.ps1"
    $winuxCmdPath = Join-Path $BinDir "winuxcmd.exe"

    if (-not (Test-Path $winuxCmdPath)) {
        Write-Color "Red" "Error: winuxcmd.exe not found at: $winuxCmdPath"
        return $false
    }

    $winuxFunction = @'
# >>> WinuxCmd integration >>>
# Enumerate supported install roots. WINUXCMD_INSTALL_ROOTS is mainly for
# tests and explicit overrides, then we probe classic and WinGet roots.
function Get-WinuxInstallRoots {
    $roots = New-Object System.Collections.Generic.List[string]

    if (-not [string]::IsNullOrWhiteSpace($env:WINUXCMD_INSTALL_ROOTS)) {
        foreach ($root in ($env:WINUXCMD_INSTALL_ROOTS -split ';')) {
            if (-not [string]::IsNullOrWhiteSpace($root) -and (Test-Path $root)) {
                $roots.Add((Resolve-Path $root).Path)
            }
        }
    }

    $classicRoot = Join-Path $env:LOCALAPPDATA 'WinuxCmd'
    if (Test-Path $classicRoot) {
        $roots.Add((Resolve-Path $classicRoot).Path)
    }

    foreach ($root in @(
        (Join-Path $env:LOCALAPPDATA 'Programs\WinuxCmd'),
        (Join-Path $env:ProgramFiles 'WinuxCmd'),
        (Join-Path ${env:ProgramFiles(x86)} 'WinuxCmd')
    )) {
        if (-not [string]::IsNullOrWhiteSpace($root) -and (Test-Path $root)) {
            $roots.Add((Resolve-Path $root).Path)
        }
    }

    $wingetPackagesRoot = Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages'
    if (Test-Path $wingetPackagesRoot) {
        foreach ($pkgRoot in Get-ChildItem -Path $wingetPackagesRoot -Directory -Filter '*WinuxCmd*' -ErrorAction SilentlyContinue) {
            $roots.Add($pkgRoot.FullName)
        }
    }

    return $roots | Select-Object -Unique
}

function Get-WinuxVersionDirectories {
    $versionDirs = New-Object System.Collections.Generic.List[object]

    foreach ($root in Get-WinuxInstallRoots) {
        $rootName = Split-Path $root -Leaf
        if ((Test-Path (Join-Path $root 'winuxcmd.exe')) -or (Test-Path (Join-Path $root 'bin\winuxcmd.exe'))) {
            $item = Get-Item $root
            $item | Add-Member -NotePropertyName WinuxPriority -NotePropertyValue 3 -Force
            $versionDirs.Add($item)
            continue
        }

        if ($rootName -like 'WinuxCmd-*') {
            $item = Get-Item $root
            $item | Add-Member -NotePropertyName WinuxPriority -NotePropertyValue 2 -Force
            $versionDirs.Add($item)
            continue
        }

        foreach ($dir in Get-ChildItem -Path $root -Directory -Filter 'WinuxCmd-*' -ErrorAction SilentlyContinue) {
            $dir | Add-Member -NotePropertyName WinuxPriority -NotePropertyValue 1 -Force
            $versionDirs.Add($dir)
        }
    }

    return $versionDirs | Sort-Object -Property @{
        Expression = { $_.WinuxPriority }
        Descending = $true
    }, @{
        Expression = {
            if ($_.Name -match 'WinuxCmd-(\d+\.\d+\.\d+)') {
                [Version]$Matches[1]
            } else {
                [Version]'0.0.0'
            }
        }
        Descending = $true
    }
}

# Find winuxcmd.exe and set alias for it.
function Update-WinuxCmdAlias {
    $latestExe = $null

    foreach ($dir in Get-WinuxVersionDirectories) {
        $exePath = Join-Path $dir.FullName 'bin\winuxcmd.exe'
        if (-not (Test-Path $exePath)) {
            $exePath = Join-Path $dir.FullName 'winuxcmd.exe'
        }

        if (Test-Path $exePath) {
            $latestExe = $exePath
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
        $allVersions = @()
        foreach ($dir in Get-WinuxVersionDirectories) {
            $versionString = 'installed'
            $version = [Version]'0.0.0'
            if ($dir.Name -match 'WinuxCmd-(\d+\.\d+\.\d+)') {
                try {
                    $version = [Version]$Matches[1]
                    $versionString = $Matches[1]
                } catch {
                    $version = [Version]'0.0.0'
                    $versionString = 'installed'
                }
            }

            $allVersions += [PSCustomObject]@{
                Directory = $dir
                Version = $version
                FullName = $dir.FullName
                VersionString = $versionString
                WinuxPriority = $dir.WinuxPriority
            }
        }

        if ($allVersions.Count -eq 0) {
            return $null
        }

        $sorted = $allVersions | Sort-Object -Property @{
            Expression = { $_.WinuxPriority }
            Descending = $true
        }, @{
            Expression = { $_.Version }
            Descending = $true
        }
        $latestDir = $sorted[0].FullName

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

    $winuxPaths = Find-LatestWinuxCmd
    if (-not $winuxPaths) {
        Write-Host "WinuxCmd not found. Please install WinuxCmd first."
        Write-Host "Expected locations:"
        Write-Host "  $env:LOCALAPPDATA\WinuxCmd"
        Write-Host "  $env:LOCALAPPDATA\Programs\WinuxCmd"
        Write-Host "  $env:ProgramFiles\WinuxCmd"
        Write-Host "  $env:LOCALAPPDATA\Microsoft\WinGet\Packages\*\WinuxCmd-*"
        return
    }

    $winuxPs1Path = $winuxPaths.WinuxPs1
    $winuxCmdPath = $winuxPaths.WinuxCmdExe
    $winuxVersion = $winuxPaths.Version

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

    $Command = $Arguments[0]
    $RemainingArgs = if ($Arguments.Count -gt 1) { $Arguments[1..($Arguments.Count - 1)] } else { @() }
    $managementCommands = @("activate", "deactivate", "status", "list", "help", "version")

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

    if (Test-Path $winuxCmdPath) {
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
# <<< WinuxCmd integration <<<
'@

    foreach ($profilePath in Get-ProfileInstallPaths) {
        $profileDir = Split-Path $profilePath -Parent
        if (-not (Test-Path $profileDir)) {
            New-Item -ItemType Directory -Path $profileDir -Force | Out-Null
        }

        $currentContent = ""
        if (Test-Path $profilePath) {
            $currentContent = Get-Content $profilePath -Raw
            $currentContent = $currentContent.Trim()
        }

        $currentContent = Remove-LegacyProfileBlocks -Content $currentContent
        $currentContent = $currentContent -replace '\n\n\n+', "`n`n"
        $currentContent = $currentContent.Trim()

        $newContent = if ([string]::IsNullOrWhiteSpace($currentContent)) {
            $winuxFunction.Trim()
        } else {
            $currentContent + "`n`n" + $winuxFunction.Trim()
        }
        Set-Content -Path $profilePath -Value $newContent -Encoding UTF8 -Force
    }

    return $true
}

function Update-UserPathEntry {
    param(
        [string]$BinDir,
        [bool]$Present
    )

    $userPath = [System.Environment]::GetEnvironmentVariable("PATH", "User")
    $parts = @()
    if (-not [string]::IsNullOrWhiteSpace($userPath)) {
        $parts = $userPath -split ";" | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
    }

    $filtered = foreach ($part in $parts) {
        if ($part -ne $BinDir) {
            $part
        }
    }

    $updated = if ($Present) { @($BinDir) + @($filtered) } else { @($filtered) }
    [System.Environment]::SetEnvironmentVariable("PATH", ($updated -join ";"), "User")
}

function Remove-WinuxFromProfiles {
    foreach ($profilePath in Get-ProfileInstallPaths) {
        if (-not (Test-Path $profilePath)) {
            continue
        }

        $content = Get-Content $profilePath -Raw
        $content = Remove-LegacyProfileBlocks -Content $content
        $lines = $content -split "`r?`n" | Where-Object {
            $_ -notmatch '# WinuxCmd Tab Completion' -and
            $_ -notmatch 'winuxcmd-completions\.ps1'
        }
        $newContent = ($lines -join "`r`n").Trim()
        Set-Content -LiteralPath $profilePath -Value $newContent -Encoding UTF8
        Write-Color "Green" "Cleaned profile: $profilePath"
    }
}

Write-Color "Cyan" "WinuxCmd Profile Initializer"
Write-Color "Cyan" "==========================="
if (-not $Quiet) {
    Write-Host ""
}

if ($Install) {
    $updateProfiles = -not $SkipProfileUpdate
    $updatePath = -not $ProfilesOnly

    Write-Color "Yellow" "Permanent installation: configuring WinuxCmd integration..."
    if (-not $Quiet) {
        Write-Host ""
    }

    $binDir = Get-WinuxBinDir
    if (-not $binDir) {
        Write-Color "Red" "WinuxCmd installation not found."
        exit 1
    }
    Write-Color "Cyan" "Found WinuxCmd at: $binDir"

    if ($updatePath) {
        Update-UserPathEntry -BinDir $binDir -Present $true
        Write-Color "Green" "Added '$binDir' to user PATH registry."
        if (-not $Quiet) {
            Write-Host "       (Effective in new sessions / after re-login)"
        }

        if ($env:PATH -notlike "*$binDir*") {
            $env:PATH = "$binDir;$env:PATH"
        }
    }

    if ($updateProfiles) {
        if (Install-WinuxToProfile -BinDir $binDir) {
            foreach ($profilePath in Get-ProfileInstallPaths) {
                Write-Color "Green" "Installed Winux PowerShell wrapper in: $profilePath"
            }
        } else {
            Write-Color "Red" "Failed to install Winux PowerShell wrapper."
            exit 1
        }
    }

    if (-not $Quiet) {
        Write-Host ""
    }
    Write-Color "Green" "Installation complete!"
    if (-not $Quiet) {
        if ($updateProfiles) {
            Write-Host "  - Open a new PowerShell window and run: winux activate"
        }
        if ($updatePath) {
            Write-Host "  - GNU command executables are also available from PATH"
        }
    }
    exit 0
}

if ($Uninstall) {
    $removeProfiles = -not $SkipProfileUpdate
    $removePath = -not $ProfilesOnly

    Write-Color "Yellow" "Removing permanent installation..."
    $binDir = Get-WinuxBinDir
    if ($removePath -and $binDir) {
        Update-UserPathEntry -BinDir $binDir -Present $false
        if ($env:PATH -like "*$binDir*") {
            $env:PATH = (($env:PATH -split ';' | Where-Object { $_ -ne $binDir }) -join ';')
        }
        Write-Color "Green" "Removed from user PATH registry."
    }

    if ($removeProfiles) {
        Remove-WinuxFromProfiles
    }

    Write-Color "Green" "Uninstall complete. Open a new shell to take effect."
    exit 0
}

$binDir = Get-WinuxBinDir
if (-not $binDir) {
    Write-Color "Red" "Failed to find WinuxCmd installation"
    Write-Host "Please install WinuxCmd first."
    exit 1
}

Write-Color "Cyan" "Found WinuxCmd at: $binDir"

Write-Host ""
Write-Host "This will add the 'winux' command to your PowerShell profile."
Write-Host "Profile locations:"
foreach ($profilePath in Get-ProfileInstallPaths) {
    Write-Host "  $profilePath"
}
Write-Host ""

$confirm = Read-Host "Continue? (Y/N)"
if ($confirm -notmatch '^[Yy]') {
    Write-Color "Yellow" "Cancelled."
    exit 0
}

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
    Write-Host "3. Optional: winux activate (enables bare ls/rm/cat/man in PowerShell)"
    Write-Host ""
    Write-Color "Cyan" "Usage after restart:"
    Write-Host "  > winux                     # Show help and version info"
    Write-Host "  > winux ls -la              # Use GNU ls directly"
    Write-Host "  > winux activate            # Override common PowerShell aliases"
    Write-Host "  > winuxcmd --help           # Direct alias to winuxcmd.exe"
    Write-Host ""
    Write-Host "Note: Future WinuxCmd updates will be automatically detected!"
} else {
    Write-Color "Red" "Failed to add WinuxCmd to profile"
    exit 1
}
