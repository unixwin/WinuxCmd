$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$sourceScript = Join-Path $repoRoot "scripts\winux.ps1"

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("winux-command-discovery-" + [guid]::NewGuid().ToString("N"))
$null = New-Item -ItemType Directory -Path $tempRoot -Force

try {
    Copy-Item -LiteralPath $sourceScript -Destination (Join-Path $tempRoot "winux.ps1")

    foreach ($name in @("ls.exe", "cat.exe", "man.exe", "winuxcmd.exe")) {
        Copy-Item -LiteralPath $env:ComSpec -Destination (Join-Path $tempRoot $name)
    }

    $listOutput = powershell -NoProfile -ExecutionPolicy Bypass -Command @"
Set-Location '$tempRoot'
& '$tempRoot\winux.ps1' -Action list 6>&1
"@

    if ($LASTEXITCODE -ne 0) {
        throw "winux.ps1 list exited with $LASTEXITCODE"
    }

    $listText = ($listOutput | Out-String)

    if ($listText -notmatch 'ls\s*\r?\n\s*-> ls\.exe') {
        throw "expected ls.exe in command list"
    }
    if ($listText -notmatch 'cat\s*\r?\n\s*-> cat\.exe') {
        throw "expected cat.exe in command list"
    }
    if ($listText -notmatch 'man\s*\r?\n\s*-> man\.exe') {
        throw "expected man.exe in command list"
    }
    if ($listText -match 'winuxcmd\s*\r?\n\s*-> winuxcmd\.exe') {
        throw "winuxcmd.exe should not be listed as a command alias target"
    }
    if ($listText -notmatch 'Total: 3 commands') {
        throw "expected dynamic command total based on discovered executables"
    }

    powershell -NoProfile -ExecutionPolicy Bypass -Command @"
Set-Location '$tempRoot'
& '$tempRoot\winux.ps1' -Action activate | Out-Null
`$ls = Get-Alias ls -ErrorAction Stop
if (`$ls.Definition -ne (Join-Path '$tempRoot' 'ls.exe')) { exit 21 }
`$man = Get-Alias man -ErrorAction Stop
if (`$man.Definition -ne (Join-Path '$tempRoot' 'man.exe')) { exit 22 }
exit 0
"@ | Out-Null

    if ($LASTEXITCODE -ne 0) {
        throw "winux.ps1 activate alias verification failed with $LASTEXITCODE"
    }
}
finally {
    Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
}
