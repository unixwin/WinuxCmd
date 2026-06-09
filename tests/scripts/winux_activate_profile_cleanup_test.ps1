$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$sourceScript = Join-Path $repoRoot "scripts\winux-activate.ps1"

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("winux-activate-profile-test-" + [guid]::NewGuid().ToString("N"))
$null = New-Item -ItemType Directory -Path $tempRoot -Force

try {
    $installRoot = Join-Path $tempRoot "install"
    $null = New-Item -ItemType Directory -Path $installRoot -Force

    Copy-Item -LiteralPath $sourceScript -Destination (Join-Path $installRoot "winux-activate.ps1")
    Copy-Item -LiteralPath $env:ComSpec -Destination (Join-Path $installRoot "winuxcmd.exe")

    $profilePath = Join-Path $tempRoot "profile.ps1"
    @'
function global:winux {
    param(
        [Parameter(Position=0,ValueFromRemainingArguments=$true)]
        [string[]]$Arguments
    )
    $winuxPs1 = Join-Path $env:USERPROFILE\scoop\apps\winuxcmd\current 'winux.ps1'
    if (Test-Path $winuxPs1) {
        & $winuxPs1 -Action activate
    } else {
        Write-Host 'winux.ps1 not found'
    }
}
'@ | Set-Content -LiteralPath $profilePath -Encoding UTF8

    $env:WINUXCMD_PROFILE_PATH = $profilePath
    $command = @"
Set-Location '$installRoot'
'Y' | & '$installRoot\winux-activate.ps1'
"@

    powershell -NoProfile -ExecutionPolicy Bypass -Command $command | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "winux-activate.ps1 exited with $LASTEXITCODE"
    }

    $profileContent = Get-Content -LiteralPath $profilePath -Raw
    if ($profileContent -match 'scoop\\apps\\winuxcmd\\current') {
        throw "stale Scoop wrapper was not removed"
    }
    if (($profileContent | Select-String -Pattern 'function global:winux' -AllMatches).Matches.Count -ne 1) {
        throw "expected exactly one winux wrapper after rewrite"
    }
    if ($profileContent -notmatch '\$env:LOCALAPPDATA\\WinuxCmd') {
        throw "expected dynamic LOCALAPPDATA-based wrapper"
    }
}
finally {
    Remove-Item -LiteralPath Env:WINUXCMD_PROFILE_PATH -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
}
