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
# Note: echo, cp, where are excluded because they have AllScope option and cannot be removed
$ConflictedAliases = @(
    "cat", "clear", "diff", "group", "kill", "ls", 
    "mv", "ps", "pwd", "rm", "rmdir", "sleep", "sort", "tee", "type", "man"
)

# Aliases with AllScope that cannot be removed - users must use .exe extension
$AllScopeAliases = @("echo", "cp", "where")

$CommandMap = @{
    "ls"        = "ls.exe"
    "cat"       = "cat.exe"
    "cp"        = "cp.exe"
    "mv"        = "mv.exe"
    "rm"        = "rm.exe"
    "mkdir"     = "mkdir.exe"
    "rmdir"     = "rmdir.exe"
    "touch"     = "touch.exe"
    "echo"      = "echo.exe"
    "head"      = "head.exe"
    "tail"      = "tail.exe"
    "find"      = "find.exe"
    "grep"      = "grep.exe"
    "sort"      = "sort.exe"
    "uniq"      = "uniq.exe"
    "cut"       = "cut.exe"
    "wc"        = "wc.exe"
    "which"     = "which.exe"
    "env"       = "env.exe"
    "sed"       = "sed.exe"
    "chmod"     = "chmod.exe"
    "date"      = "date.exe"
    "diff"      = "diff.exe"
    "ln"        = "ln.exe"
    "ps"        = "ps.exe"
    "pwd"       = "pwd.exe"
    "tee"       = "tee.exe"
    "xargs"     = "xargs.exe"
    "file"      = "file.exe"
    "realpath"  = "realpath.exe"
    "df"        = "df.exe"
    "du"        = "du.exe"
    "kill"      = "kill.exe"
    "tree"      = "tree.exe"
    "lsof"      = "lsof.exe"
    # New commands added in v0.5.3
    "base64"    = "base64.exe"
    "tr"        = "tr.exe"
    "less"      = "less.exe"
    "watch"     = "watch.exe"
    "jq"        = "jq.exe"
    "md5sum"    = "md5sum.exe"
    "sha256sum" = "sha256sum.exe"
    "basename"  = "basename.exe"
    "dirname"   = "dirname.exe"
    "free"      = "free.exe"
    "column"    = "column.exe"
    "seq"       = "seq.exe"
    "stat"      = "stat.exe"
    # New commands added in v0.7.0 - Hash tools
    "sha1sum"   = "sha1sum.exe"
    "sha224sum" = "sha224sum.exe"
    "sha384sum" = "sha384sum.exe"
    "sha512sum" = "sha512sum.exe"
    "b2sum"     = "b2sum.exe"
    # New commands added in v0.7.0 - Text processing
    "paste"     = "paste.exe"
    "join"      = "join.exe"
    "comm"      = "comm.exe"
    "split"     = "split.exe"
    "csplit"    = "csplit.exe"
    "cmp"       = "cmp.exe"
    "nl"        = "nl.exe"
    "fold"      = "fold.exe"
    "fmt"       = "fmt.exe"
    # New commands added in v0.7.0 - Text conversion
    "expand"    = "expand.exe"
    "unexpand"  = "unexpand.exe"
    "tac"       = "tac.exe"
    # New commands added in v0.7.0 - System information
    "hostname"  = "hostname.exe"
    "whoami"    = "whoami.exe"
    "arch"      = "arch.exe"
    "uname"     = "uname.exe"
    "id"        = "id.exe"
    "who"       = "who.exe"
    "users"     = "users.exe"
    "groups"    = "groups.exe"
    # New commands added in v0.7.0 - File operations
    "truncate"  = "truncate.exe"
    "mktemp"    = "mktemp.exe"
    "install"   = "install.exe"
    "readlink"  = "readlink.exe"
    "cksum"     = "cksum.exe"
    "sum"       = "sum.exe"
    # New commands added in v0.7.0 - Other tools
    "sleep"     = "sleep.exe"
    "timeout"   = "timeout.exe"
    "uptime"    = "uptime.exe"
    "shuf"      = "shuf.exe"
    "pr"        = "pr.exe"
    "yes"       = "yes.exe"
    "ptx"       = "ptx.exe"
    # New commands added in v0.7.0 - Basic utilities
    "clear"     = "clear.exe"
    "true"      = "true.exe"
    "false"     = "false.exe"
    "tty"       = "tty.exe"
    "sync"      = "sync.exe"
    "reset"     = "reset.exe"
    "logname"   = "logname.exe"
    "printenv"  = "printenv.exe"
    # New commands added in v0.7.0 - Text processing
    "rev"       = "rev.exe"
    "d2u"       = "d2u.exe"
    "u2d"       = "u2d.exe"
    "dos2unix"  = "dos2unix.exe"
    "unix2dos"  = "unix2dos.exe"
    "base32"    = "base32.exe"
    "basenc"    = "basenc.exe"
    "cygpath"   = "cygpath.exe"
    "pathchk"   = "pathchk.exe"
    # New commands added in v0.7.0 - Programming tools
    "printf"    = "printf.exe"
    "expr"      = "expr.exe"
    "test"      = "test.exe"
    "["         = "[.exe"
    # New commands added in v0.7.0 - Binary tools
    "od"        = "od.exe"
    "xxd"       = "xxd.exe"
    "dd"        = "dd.exe"
    "shred"     = "shred.exe"
    # New commands added in v0.7.0 - System utilities
    "numfmt"    = "numfmt.exe"
    "hmac256"   = "hmac256.exe"
    "nice"      = "nice.exe"
    "nohup"     = "nohup.exe"
    "stdbuf"    = "stdbuf.exe"
    # New commands added in v0.7.0 - Development tools
    "patch"     = "patch.exe"
    "diff3"     = "diff3.exe"
    "sdiff"     = "sdiff.exe"
    # New commands added in v0.7.0 - Calendar and sorting
    "cal"       = "cal.exe"
    "tsort"     = "tsort.exe"
    # New commands added in v0.7.0 - Terminal tools
    "tput"      = "tput.exe"
    "infocmp"   = "infocmp.exe"
    "tic"       = "tic.exe"
    "toe"       = "toe.exe"
    "top"       = "top.exe"
    # New commands added in v0.7.0 - System information
    "hostid"    = "hostid.exe"
    "locale"    = "locale.exe"
    "tzset"     = "tzset.exe"
    "pinky"     = "pinky.exe"
    "mpicalc"   = "mpicalc.exe"
    # New commands added in v0.7.0 - Archive tools
    "cpio"      = "cpio.exe"
    # New commands added in v0.7.0 - System utilities
    "nproc"     = "nproc.exe"
    "getconf"   = "getconf.exe"
    "link"      = "link.exe"
    "unlink"    = "unlink.exe"
    "factor"    = "factor.exe"
    # New commands added in v0.9.0 - Coreutils
    "man"       = "man.exe"
    "chown"     = "chown.exe"
}

# ========== Functions ==========

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

function Remove-ConflictedAliases {
    foreach ($aliasName in $ConflictedAliases) {
        Remove-Item -Path "Alias:\$aliasName" -Force -ErrorAction SilentlyContinue
    }
}

function Restore-ConflictedAliases {
    if (-not $global:Winux_SavedAliases) { return }

    foreach ($aliasName in $global:Winux_SavedAliases.Keys) {
        $saved = $global:Winux_SavedAliases[$aliasName]
        
        try {
            Set-Alias -Name $aliasName -Value $saved.Definition -Scope Global -Force
        }
        catch {
            # If restore fails, ignore it
        }
    }

    Remove-Variable Winux_SavedAliases -Scope Global -ErrorAction SilentlyContinue
}

foreach ($aliasName in $global:Winux_SavedAliases.Keys) {
    $saved = $global:Winux_SavedAliases[$aliasName]
    Set-Alias -Name $aliasName -Value $saved.Definition -Scope Global -Force
}

Remove-Variable Winux_SavedAliases -Scope Global -ErrorAction SilentlyContinue


function Show-CommandList {
    Write-Host "Available WinuxCmd Commands:" -ForegroundColor Cyan
    Write-Host "============================" -ForegroundColor Cyan

    foreach ($cmd in $CommandMap.Keys | Sort-Object) {
        Write-Host "  $cmd" -ForegroundColor Yellow -NoNewline
        Write-Host " -> $($CommandMap[$cmd])"
    }

    Write-Host ""
    Write-Host "Total: $($CommandMap.Count) commands" -ForegroundColor Yellow
    Write-Host "To use these commands, run: .\winux.ps1 activate" -ForegroundColor Green
}

function Show-Status {
    Write-Host "WinuxCmd Status:" -ForegroundColor Cyan
    Write-Host "================" -ForegroundColor Cyan

    # Check if WinuxCmd is in PATH
    $winuxInPath = $env:PATH -split ';' | Where-Object { $_ -eq $ScriptDir }
    
    if ($winuxInPath) {
        Write-Host "Status: ACTIVE" -ForegroundColor Green
        Write-Host "Directory: $ScriptDir" -ForegroundColor Gray
        Write-Host "Commands: $($CommandMap.Count) available" -ForegroundColor Yellow
    }
    else {
        Write-Host "Status: INACTIVE" -ForegroundColor Gray
        Write-Host "Run '.\winux.ps1 activate' to enable" -ForegroundColor Gray
    }

    Write-Host ""
    Write-Host "Run '.\winux.ps1 list' to see all commands" -ForegroundColor Gray
}

function Invoke-Activate {
    Write-Host "Activating WinuxCmd..." -ForegroundColor Green

    Save-ConflictedAliases
    Remove-ConflictedAliases

    # Add WinuxCmd bin directory to PATH
    if ($env:PATH -notlike "$ScriptDir*") {
        $env:PATH = "$ScriptDir;$env:PATH"
    }

    Write-Host "Activation complete!" -ForegroundColor Green
    Write-Host ""
    Write-Host "Total: $($CommandMap.Count) commands" -ForegroundColor Yellow
    Write-Host "Directory: $ScriptDir" -ForegroundColor Gray
    Write-Host ""

    # Show warning for AllScope aliases
    if ($AllScopeAliases.Count -gt 0) {
        Write-Host "Note: The following commands have AllScope option and cannot be overridden:" -ForegroundColor Yellow
        Write-Host "  $($AllScopeAliases -join ', ')" -ForegroundColor Magenta
        Write-Host "  Use `.exe` extension to run WinuxCmd version, e.g.:" -ForegroundColor Cyan
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
