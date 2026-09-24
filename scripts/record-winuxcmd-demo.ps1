param(
    [string]$Output = "DOCS\media\winuxcmd-wpm-demo.mp4",
    [int]$Seconds = 35,
    [string]$WinuxCmdRoot = "C:\Users\caomengxuan\tools\winuxcmd",
    [string]$FfmpegBin = "C:\Users\caomengxuan\AppData\Local\Microsoft\WinGet\Packages\Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe\ffmpeg-8.1.2-full_build\bin",
    [switch]$NoLaunch
)

$ErrorActionPreference = "Stop"

function Resolve-Tool {
    param(
        [string]$PreferredPath,
        [string]$CommandName
    )

    if ($PreferredPath -and (Test-Path $PreferredPath)) {
        return (Resolve-Path $PreferredPath).Path
    }

    $cmd = Get-Command $CommandName -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }

    throw "Could not find $CommandName"
}

$ffmpeg = Resolve-Tool (Join-Path $FfmpegBin "ffmpeg.exe") "ffmpeg.exe"
$winuxcmd = Resolve-Tool (Join-Path $WinuxCmdRoot "winuxcmd.exe") "winuxcmd.exe"

$outputPath = Join-Path (Get-Location) $Output
$outputDir = Split-Path -Parent $outputPath
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

$demoScript = Join-Path $env:TEMP "winuxcmd-demo-$PID.ps1"
$demoBody = @"
`$ErrorActionPreference = "Continue"
`$env:PATH = "$WinuxCmdRoot;`$env:PATH"
function Step([string]`$Title, [string[]]`$ArgsList) {
    Write-Host ""
    Write-Host "`$ $Title" -ForegroundColor Cyan
    & "$winuxcmd" @ArgsList
    Start-Sleep -Milliseconds 900
}

Clear-Host
Write-Host "WinuxCmd 0.14 - native Windows commands + WPM" -ForegroundColor Green
Start-Sleep -Seconds 1

Step "winuxcmd --version" @("--version")
Step "winuxcmd ls -la" @("ls", "-la")
Step "winuxcmd grep -n WPM README.md" @("grep", "-n", "WPM", "README.md")
Step "winuxcmd wpm source list -v" @("wpm", "source", "list", "-v")
Step "winuxcmd wpm search json" @("wpm", "search", "json")
Step "winuxcmd wpm info jq" @("wpm", "info", "jq")

Write-Host ""
Write-Host "Demo complete. Press Enter to close." -ForegroundColor Green
Read-Host | Out-Null
"@

Set-Content -Path $demoScript -Value $demoBody -Encoding utf8

if (-not $NoLaunch) {
    $wt = Get-Command wt.exe -ErrorAction SilentlyContinue
    $args = @(
        "new-tab",
        "--title",
        "WinuxCmd WPM Demo",
        "powershell",
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        $demoScript
    )

    if ($wt) {
        Start-Process -FilePath $wt.Source -ArgumentList $args
    } else {
        Start-Process -FilePath "powershell.exe" -ArgumentList @(
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            $demoScript
        )
    }

    Start-Sleep -Seconds 2
}

Write-Host "Recording desktop for $Seconds seconds..."
Write-Host "Output: $outputPath"
Write-Host "Check the visible desktop before running this script; gdigrab records the screen."

& $ffmpeg `
    -y `
    -loglevel warning `
    -f gdigrab `
    -framerate 30 `
    -i desktop `
    -t $Seconds `
    -vf "scale=1280:-2" `
    -c:v libx264 `
    -preset veryfast `
    -pix_fmt yuv420p `
    $outputPath

Write-Host "Recorded demo: $outputPath"
