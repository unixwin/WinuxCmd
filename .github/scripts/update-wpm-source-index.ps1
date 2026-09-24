<#
.SYNOPSIS
    Update the official WPM source index for a WinuxCmd release.
#>

param(
    [Parameter(Mandatory = $true)]
    [string]$IndexPath,

    [Parameter(Mandatory = $true)]
    [string]$Version,

    [Parameter(Mandatory = $true)]
    [string]$X64ZipPath,

    [Parameter(Mandatory = $true)]
    [string]$Arm64ZipPath,

    [Parameter(Mandatory = $false)]
    [string]$Repository = "unixwin/WinuxCmd",

    [Parameter(Mandatory = $false)]
    [string]$ReleaseTag = "v$Version",

    [Parameter(Mandatory = $false)]
    [string]$UpdatedDate = (Get-Date -Format "yyyy-MM-dd")
)

$ErrorActionPreference = "Stop"

if ($Version -notmatch '^\d+\.\d+\.\d+$') {
    throw "Invalid version '$Version'. Expected semantic version like 0.14.3."
}

if ($UpdatedDate -notmatch '^\d{4}-\d{2}-\d{2}$') {
    throw "Invalid updated date '$UpdatedDate'. Expected yyyy-MM-dd."
}

$resolvedIndex = Resolve-Path -LiteralPath $IndexPath -ErrorAction Stop
$resolvedX64 = Resolve-Path -LiteralPath $X64ZipPath -ErrorAction Stop
$resolvedArm64 = Resolve-Path -LiteralPath $Arm64ZipPath -ErrorAction Stop

$index = Get-Content -LiteralPath $resolvedIndex -Raw | ConvertFrom-Json
$package = $index.packages | Where-Object { $_.name -eq "winuxcmd" } | Select-Object -First 1
if ($null -eq $package) {
    throw "Package 'winuxcmd' was not found in $resolvedIndex."
}

function Update-Artifact {
    param(
        [Parameter(Mandatory = $true)]
        [pscustomobject]$Artifacts,

        [Parameter(Mandatory = $true)]
        [string]$Arch,

        [Parameter(Mandatory = $true)]
        [string]$ArchivePath,

        [Parameter(Mandatory = $true)]
        [string]$AssetName
    )

    $property = $Artifacts.PSObject.Properties[$Arch]
    if ($null -eq $property) {
        throw "Artifact '$Arch' was not found for package 'winuxcmd'."
    }

    $artifact = $property.Value
    $artifact.type = "zip"
    $artifact.sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $ArchivePath).Hash.ToLowerInvariant()
    $artifact.urls = @("https://github.com/$Repository/releases/download/$ReleaseTag/$AssetName")
    $artifact.files = @(
        [pscustomobject]@{
            from = "winuxcmd.exe"
            to = "winuxcmd.exe"
        }
    )
}

$index.version = "official-" + $UpdatedDate.Replace("-", ".")
$index.updated = $UpdatedDate
$package.version = $Version

Update-Artifact `
    -Artifacts $package.artifacts `
    -Arch "windows-x64" `
    -ArchivePath $resolvedX64 `
    -AssetName "WinuxCmd-$Version-win-x64.zip"

Update-Artifact `
    -Artifacts $package.artifacts `
    -Arch "windows-arm64" `
    -ArchivePath $resolvedArm64 `
    -AssetName "WinuxCmd-$Version-win-arm64.zip"

$json = $index | ConvertTo-Json -Depth 100
Set-Content -LiteralPath $resolvedIndex -Value $json -Encoding utf8NoBOM

Write-Host "Updated winuxcmd WPM index metadata to $Version."
