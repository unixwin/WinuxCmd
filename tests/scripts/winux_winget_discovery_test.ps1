$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$activateScript = Join-Path $repoRoot "scripts\winux-activate.ps1"
$winuxScript = Join-Path $repoRoot "scripts\winux.ps1"

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("winux-winget-discovery-" + [guid]::NewGuid().ToString("N"))
$packageRoot = Join-Path $tempRoot "caomengxuan666.WinuxCmd_Microsoft.Winget.Source_8wekyb3d8bbwe"
$installRoot = Join-Path $packageRoot "WinuxCmd-0.12.0-win-x64"
$profilePath = Join-Path $tempRoot "profile.ps1"

$null = New-Item -ItemType Directory -Path $installRoot -Force

try {
    Copy-Item -LiteralPath $activateScript -Destination (Join-Path $installRoot "winux-activate.ps1")
    Copy-Item -LiteralPath $winuxScript -Destination (Join-Path $installRoot "winux.ps1")
    foreach ($name in @("winuxcmd.exe", "ls.exe", "cat.exe", "man.exe")) {
        Copy-Item -LiteralPath $env:ComSpec -Destination (Join-Path $installRoot $name)
    }

    $env:WINUXCMD_INSTALL_ROOTS = $packageRoot
    $env:WINUXCMD_PROFILE_PATH = $profilePath

    $installCommand = @"
Set-Location '$installRoot'
& '$installRoot\winux-activate.ps1' -Install -ProfilesOnly -Quiet
"@
    powershell -NoProfile -ExecutionPolicy Bypass -Command $installCommand | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "winux-activate.ps1 install exited with $LASTEXITCODE"
    }

    $verifyCommand = @"
`$env:WINUXCMD_INSTALL_ROOTS = '$packageRoot'
. '$profilePath'
winux list 6>&1 | Out-String
"@
    $listOutput = powershell -NoProfile -ExecutionPolicy Bypass -Command $verifyCommand
    if ($LASTEXITCODE -ne 0) {
        throw "generated Winux wrapper failed to run from WinGet-style install root"
    }

    $listText = ($listOutput | Out-String)
    if ($listText -notmatch 'Available WinuxCmd Commands:') {
        throw "expected winux list output via generated wrapper"
    }
    if ($listText -notmatch 'ls\s*\r?\n\s*-> ls\.exe') {
        throw "expected WinGet-style install root discovery to find ls.exe"
    }
}
finally {
    Remove-Item -LiteralPath Env:WINUXCMD_INSTALL_ROOTS -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath Env:WINUXCMD_PROFILE_PATH -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
}
