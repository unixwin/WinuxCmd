<#
.SYNOPSIS
    WinuxCmd Command Link Generator
.DESCRIPTION
    Creates hardlinks or symbolic links for all WinuxCmd commands
    from winuxcmd.exe in the current directory.
.PARAMETER Force
    Overwrite existing command executables without prompting.
.PARAMETER UseSymbolicLinks
    Use symbolic links instead of hardlinks (useful for non-NTFS filesystems).
.PARAMETER Remove
    Remove all created links (winuxcmd.exe is kept).
.EXAMPLE
    .\create_links.ps1
    Create hardlinks for all commands (requires NTFS).
.EXAMPLE
    .\create_links.ps1 -UseSymbolicLinks
    Create symbolic links (works on all Windows filesystems).
.EXAMPLE
    .\create_links.ps1 -Force
    Create hardlinks, overwriting existing files without prompting.
.EXAMPLE
    .\create_links.ps1 -Remove
    Remove all command links.
#>

[CmdletBinding()]
param(
    [switch]$Force,
    [switch]$UseSymbolicLinks,
    [switch]$Remove
)

$ErrorActionPreference = "Stop"
$Script:Version = "0.7.2"

# Escape special characters for PowerShell path handling
function Escape-WildcardChars {
    param([string]$Path)
    $specialChars = @('[', ']', '*', '?')
    foreach ($char in $specialChars) {
        $Path = $Path.Replace($char, "`$char")
    }
    return $Path
}

# Available commands list (auto-generated from src/commands/*.cpp)
$Script:Commands = @(
    "arch", "b2sum", "base32", "base64", "basename", "basenc", "cal", "cat",
    "chgrp", "chmod", "chown", "cksum", "clear", "cmp", "col", "column", "comm", "cp", "cpio", "csplit",
    "cut", "cygpath", "d2u", "date", "dd", "df", "diff", "diff3", "dirname",
    "dir", "dircolors", "dos2unix", "du", "echo", "env", "expand", "expr", "factor", "false", "file",
    "find", "fmt", "fold", "free", "getconf", "grep", "groups", "head", "hexdump",
    "hmac256", "hostid", "hostname", "id", "infocmp", "install", "join", "jq",
    "kill", "less", "link", "ln", "locale", "logname", "ls", "lsof", "man", "md5sum",
    "mkdir", "mktemp", "more", "mpicalc", "mv", "nice", "nl", "nohup",
    "nproc", "numfmt", "od", "paste", "patch", "pathchk", "pinky", "pr",
    "printenv", "printf", "ps", "ptx", "pwd", "readlink", "realpath", "reset",
    "rev", "rm", "rmdir", "sdiff", "sed", "seq", "sha1sum", "sha224sum",
    "sha256sum", "sha384sum", "sha512sum", "shred", "shuf", "sleep", "sort",
    "split", "stat", "stdbuf", "strings", "stty", "sum", "sync", "tac", "tail", "tee", "test",
    "[", "tic", "timeout", "toe", "top", "touch", "tput", "tr", "tree", "true",
    "truncate", "tsort", "tty", "tzset", "u2d", "uname", "unexpand", "uniq",
    "unix2dos", "unlink", "uptime", "users", "vdir", "watch", "wc", "which", "who",
    "whoami", "xargs", "xxd", "yes"
)

function Write-ColorOutput {
    param(
        [Parameter(Mandatory = $true)]
        [ValidateSet("Green", "Yellow", "Red", "Cyan", "Blue", "Gray")]
        [string]$Color,
        
        [Parameter(Mandatory = $true)]
        [string]$Text
    )
    
    # Simple text-based markers for older terminals
    $Markers = @{
        Green  = "[OK]"
        Yellow = "[INFO]"
        Red    = "[ERROR]"
        Cyan   = "[INFO]"
        Blue   = "[NOTE]"
        Gray   = "      "
    }
    
    Write-Host "$($Markers[$Color]) $Text"
}

function Get-WinuxCmdPath {
    $currentDir = Get-Location
    $WinuxCmdPath = Join-Path $currentDir "winuxcmd.exe"
    
    if (-not (Test-Path $WinuxCmdPath)) {
        Write-ColorOutput "Red" "Error: winuxcmd.exe not found in current directory."
        Write-ColorOutput "Yellow" "Current directory: $currentDir"
        Write-ColorOutput "Gray" "Please run this script from the WinuxCmd bin directory."
        exit 1
    }
    
    return $WinuxCmdPath
}

function Get-FileSystemType {
    try {
        $drive = (Get-Location).Drive.Root
        $volume = Get-WmiObject -Class Win32_Volume -Filter "DriveLetter='$drive'" -ErrorAction SilentlyContinue
        
        if ($volume) {
            return $volume.FileSystem
        }
    }
    catch {
        # Fall back to assuming NTFS if WMI fails
        return "Unknown"
    }
    
    return "Unknown"
}

function Remove-CommandLinks {
    param(
        [string]$WinuxCmdPath
    )
    
    $removedCount = 0
    $skippedCount = 0
    $errorsCount = 0
    
    Write-ColorOutput "Cyan" "Removing WinuxCmd command links..."
    Write-Host ""
    
    foreach ($cmd in $Script:Commands) {
        $cmdPath = Join-Path (Get-Location) "$cmd.exe"

        if (-not (Test-Path -LiteralPath $cmdPath)) {
            continue
        }
        
        try {
            # Use winuxcmd rm to remove the file
            $process = Start-Process -FilePath $WinuxCmdPath -ArgumentList @("rm", "-f", $cmdPath) -Wait -NoNewWindow -PassThru
            if ($process.ExitCode -eq 0) {
                Write-ColorOutput "Green" "  [Removed] $cmd.exe"
                $removedCount++
            }
        }
        catch {
            Write-ColorOutput "Red" "  [Error] Failed to remove $cmd.exe: $($_.Exception.Message)"
            $errorsCount++
        }
    }
    
    Write-Host ""
    Write-ColorOutput "Cyan" "Summary:"
    Write-ColorOutput "Green" "  Removed: $removedCount"
    if ($skippedCount -gt 0) {
        Write-ColorOutput "Yellow" "  Skipped: $skippedCount"
    }
    if ($errorsCount -gt 0) {
        Write-ColorOutput "Red" "  Errors: $errorsCount"
    }
    Write-ColorOutput "Gray" "  winuxcmd.exe is preserved"
}

function New-CommandLinks {
    param(
        [string]$WinuxCmdPath,
        [bool]$UseSymbolic
    )
    
    $createdCount = 0
    $skippedCount = 0
    $errorsCount = 0
    $fsType = Get-FileSystemType
    
    $linkType = if ($UseSymbolic) { "symbolic link" } else { "hardlink" }
    
    Write-ColorOutput "Cyan" "WinuxCmd Command Link Generator v$Script:Version"
    Write-ColorOutput "Cyan" "========================================="
    Write-Host ""
    Write-ColorOutput "Blue" "Configuration:"
    Write-ColorOutput "Gray" "  Target: $WinuxCmdPath"
    Write-ColorOutput "Gray" "  Link Type: $linkType"
    Write-ColorOutput "Gray" "  Filesystem: $fsType"
    Write-Host ""
    
    if (-not $UseSymbolic -and $fsType -ne "NTFS" -and $fsType -ne "Unknown") {
        Write-ColorOutput "Yellow" "Warning: Hardlinks require NTFS filesystem."
        Write-ColorOutput "Yellow" "         Current filesystem: $fsType"
        Write-ColorOutput "Yellow" "         Consider using -UseSymbolicLinks flag."
        Write-Host ""
        
        if (-not $Force) {
            $continue = Read-Host "Continue anyway? (Y/N)"
            if ($continue -notmatch '^[Yy]') {
                Write-ColorOutput "Yellow" "Cancelled."
                exit 0
            }
        }
    }
    
    # Check for existing files
    $existingFiles = @()
    foreach ($cmd in $Script:Commands) {
        $cmdPath = Join-Path (Get-Location) "$cmd.exe"
        if (Test-Path -LiteralPath $cmdPath) {
            $existingFiles += $cmd
        }
    }
    
    if ($existingFiles.Count -gt 0 -and -not $Force) {
        Write-ColorOutput "Yellow" "Warning: The following files already exist:"
        foreach ($file in $existingFiles) {
            Write-ColorOutput "Yellow" "  - $file.exe"
        }
        Write-Host ""
        
        $overwrite = Read-Host "Overwrite existing files? (Y/N)"
        if ($overwrite -notmatch '^[Yy]') {
            Write-ColorOutput "Yellow" "Cancelled."
            exit 0
        }
    }
    
    Write-ColorOutput "Cyan" "Creating links (batch mode)..."
    Write-Host ""
    
    # Build all target paths for batch creation
    $allTargets = @()
    foreach ($cmd in $Script:Commands) {
        $cmdPath = Join-Path (Get-Location) "$cmd.exe"
        $allTargets += $cmdPath
    }
    
    try {
        # Build ln command arguments for batch creation
        $lnArgs = @("ln")
        if ($UseSymbolic) {
            $lnArgs += @("-s")
        }
        if ($Force) {
            $lnArgs += @("-f")
        }
        $lnArgs += @($WinuxCmdPath)  # Source
        $lnArgs += $allTargets       # All targets
        
        # Create all links in a single process call
        $process = Start-Process -FilePath $WinuxCmdPath -ArgumentList $lnArgs -Wait -NoNewWindow -PassThru
        
        if ($process.ExitCode -eq 0) {
            # All links created successfully
            Write-ColorOutput "Green" "  [Batch Created] $($Script:Commands.Count) command links"
            $createdCount = $Script:Commands.Count
        }
        else {
            # Batch creation failed, show error
            Write-ColorOutput "Red" "  [Batch Failed] winuxcmd ln returned exit code $($process.ExitCode)"
            $errorsCount = $Script:Commands.Count
        }
    }
    catch {
        $errorMsg = $_.Exception.Message
        Write-ColorOutput "Red" "  [Batch Failed] $errorMsg"
        $errorsCount = $Script:Commands.Count
    }
    
    Write-Host ""
    Write-ColorOutput "Cyan" "Summary:"
    Write-ColorOutput "Green" "  Created: $createdCount"
    if ($errorsCount -gt 0) {
        Write-ColorOutput "Red" "  Errors: $errorsCount"
    }
    Write-Host ""
    
    if ($createdCount -gt 0) {
        Write-ColorOutput "Green" "Success! WinuxCmd commands are now available."
        Write-Host ""
        Write-ColorOutput "Blue" "Usage:"
        Write-ColorOutput "Gray" "  $ ls -la           # Use ls command"
        Write-ColorOutput "Gray" "  $ cat file.txt     # Use cat command"
        Write-ColorOutput "Gray" "  $ grep pattern file  # Use grep command"
        Write-Host ""
        
        if ($UseSymbolic) {
            Write-ColorOutput "Yellow" "Note: Symbolic links were used. These may require elevated permissions"
            Write-ColorOutput "Yellow" "      on some systems. If you encounter permission issues, try running"
            Write-ColorOutput "Yellow" "      PowerShell as Administrator."
        }
    }
    else {
        Write-ColorOutput "Red" "Failed to create any command links."
        exit 1
    }
}

# Main script logic
$WinuxCmdPath = Get-WinuxCmdPath

if ($Remove) {
    Remove-CommandLinks -WinuxCmdPath $WinuxCmdPath
}
else {
    New-CommandLinks -WinuxCmdPath $WinuxCmdPath -UseSymbolic $UseSymbolicLinks
}

