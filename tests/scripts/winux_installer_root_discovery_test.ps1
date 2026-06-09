$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$activateScript = Join-Path $repoRoot "scripts\winux-activate.ps1"
$winuxScript = Join-Path $repoRoot "scripts\winux.ps1"

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("winux-installer-discovery-" + [guid]::NewGuid().ToString("N"))
$localAppDataRoot = Join-Path $tempRoot "LocalAppData"
$installRoot = Join-Path $localAppDataRoot "Programs\WinuxCmd"
$wingetPackageRoot = Join-Path $localAppDataRoot "Microsoft\WinGet\Packages\caomengxuan666.WinuxCmd_Microsoft.Winget.Source_8wekyb3d8bbwe"
$wingetInstallRoot = Join-Path $wingetPackageRoot "WinuxCmd-9.99.9-win-x64"
$profilePath = Join-Path $tempRoot "profile.ps1"

$null = New-Item -ItemType Directory -Path $installRoot -Force
$null = New-Item -ItemType Directory -Path $wingetInstallRoot -Force

try {
    Copy-Item -LiteralPath $activateScript -Destination (Join-Path $installRoot "winux-activate.ps1")
    Copy-Item -LiteralPath $winuxScript -Destination (Join-Path $installRoot "winux.ps1")
    foreach ($name in @("winuxcmd.exe", "ls.exe", "cat.exe", "man.exe")) {
        Copy-Item -LiteralPath $env:ComSpec -Destination (Join-Path $installRoot $name)
    }

    Copy-Item -LiteralPath $winuxScript -Destination (Join-Path $wingetInstallRoot "winux.ps1")
    foreach ($name in @("winuxcmd.exe", "ls.exe", "cat.exe", "man.exe")) {
        Copy-Item -LiteralPath $env:ComSpec -Destination (Join-Path $wingetInstallRoot $name)
    }

    $env:WINUXCMD_PROFILE_PATH = $profilePath

    $installCommand = @"
`$env:LOCALAPPDATA = '$localAppDataRoot'
Set-Location '$installRoot'
& '$installRoot\winux-activate.ps1' -Install -ProfilesOnly -Quiet
"@
    powershell -NoProfile -ExecutionPolicy Bypass -Command $installCommand | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "winux-activate.ps1 installer-root install exited with $LASTEXITCODE"
    }

    $verifyCommand = @"
`$env:LOCALAPPDATA = '$localAppDataRoot'
. '$profilePath'
winux list 6>&1 | Out-String
"@
    $listOutput = powershell -NoProfile -ExecutionPolicy Bypass -Command $verifyCommand
    if ($LASTEXITCODE -ne 0) {
        throw "generated Winux wrapper failed from installer-style root"
    }

    $listText = ($listOutput | Out-String)
    if ($listText -notmatch 'Available WinuxCmd Commands:') {
        throw "expected winux list output via installer-style root"
    }
    if ($listText -notmatch 'man\s*\r?\n\s*-> man\.exe') {
        throw "expected installer-style root discovery to find man.exe"
    }

    $activateCommand = @"
`$env:LOCALAPPDATA = '$localAppDataRoot'
. '$profilePath'
winux activate | Out-String
"@
    $activateOutput = powershell -NoProfile -ExecutionPolicy Bypass -Command $activateCommand
    if ($LASTEXITCODE -ne 0) {
        throw "generated Winux wrapper failed to activate from installer-style root"
    }

    $activateText = ($activateOutput | Out-String)
    if ($activateText -notmatch [regex]::Escape($installRoot)) {
        throw "expected installer-style root to outrank newer WinGet versioned roots"
    }
}
finally {
    Remove-Item -LiteralPath Env:WINUXCMD_PROFILE_PATH -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
}
