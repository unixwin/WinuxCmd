# Elevation helper.
#
# The machine is configured with ConsentPromptBehaviorAdmin=0 ("elevate without
# prompting") and the interactive user is a member of Administrators, so
# `Start-Process -Verb RunAs` raises the token without showing a consent UI.
# This helper runs a child PowerShell script with that elevated token and waits
# for it; the caller keeps its normal (filtered) token for everything else.
#
# -Verb implies ShellExecute, which cannot be combined with stdout redirection,
# so the child is wrapped in a generated script that tees its own output into a
# log file the parent then prints. Child stdout itself is not captured.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Script,
    [string[]]$ScriptArgs = @()
)

$ErrorActionPreference = 'Stop'
# PowerShelly callers pass one array element per argument, but a shell on the
# other side may hand over "-Action,Install" as a single token; normalise both.
$ScriptArgs = @($ScriptArgs | ForEach-Object { $_ -split ',' } | Where-Object { $_ -ne '' })
$stamp = Join-Path $env:TEMP ('elevated-' + [guid]::NewGuid().ToString('N'))
$log = $stamp + '.log'
$wrapper = $stamp + '.ps1'

# Parameter names must stay unquoted to be parsed as parameters; values are
# single-quoted so paths with spaces survive.
$quotedArgs = ($ScriptArgs | ForEach-Object {
        if ($_ -match '^-[A-Za-z][\w-]*$') { $_ } else { "'" + ($_ -replace "'", "''") + "'" }
    }) -join ' '
$wrapperBody = @"
`$ErrorActionPreference = 'Continue'
try {
    Invoke-Expression "& '$($Script -replace "'", "''")' $quotedArgs" 2>&1 | Tee-Object -FilePath '$log'
} catch {
    (`$_ | Out-String) | Tee-Object -FilePath '$log' -Append
    exit 1
}
"@
Set-Content -LiteralPath $wrapper -Value $wrapperBody -Encoding UTF8

$ps = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$p = Start-Process -FilePath $ps `
    -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $wrapper) `
    -Verb RunAs -PassThru -Wait -WindowStyle Hidden
Get-Content -LiteralPath $log -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $log, $wrapper -ErrorAction SilentlyContinue
exit $p.ExitCode
