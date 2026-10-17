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
    # The size field was never updated by this script, so every release carried
    # whatever the first entry happened to say (2,842,372 for a 4 MB archive).
    $archiveSize = (Get-Item -LiteralPath $ArchivePath).Length
    if ($null -eq $artifact.PSObject.Properties['size']) {
        $artifact | Add-Member -NotePropertyName size -NotePropertyValue $archiveSize
    }
    else {
        $artifact.size = $archiveSize
    }
    $artifact.urls = @("https://github.com/$Repository/releases/download/$ReleaseTag/$AssetName")
    $artifact.files = @(
        [pscustomobject]@{
            from = "winuxcmd.exe"
            to = "winuxcmd.exe"
        }
        [pscustomobject]@{
            from = "winuxcmd-shim.exe"
            to = "winuxcmd-shim.exe"
        }
    )

    Assert-ArchiveCarries -ArchivePath $ArchivePath -AssetName $AssetName -Files $artifact.files
}

# A mapping for a file the archive does not carry makes `wpm install` fail with
# "extracted path not found", and the opposite mistake - shipping a file nobody
# maps - silently does nothing (that is how the package shim stayed out of a
# release after the code for it had already landed, unixwin/WinuxCmd#1207).
# Check the artifact against the mapping here, where both are known.
function Assert-ArchiveCarries {
    param(
        [Parameter(Mandatory = $true)][string]$ArchivePath,
        [Parameter(Mandatory = $true)][string]$AssetName,
        [Parameter(Mandatory = $true)][object]$Files
    )

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [System.IO.Compression.ZipFile]::OpenRead($ArchivePath)
    try {
        $names = @($archive.Entries | ForEach-Object { $_.FullName })
        foreach ($mapping in $Files) {
            $leaf = ($mapping.from -split '[\\/]')[-1]
            $found = $names | Where-Object { $_ -like "*/$leaf" -or $_ -eq $leaf } | Select-Object -First 1
            if ($null -eq $found) {
                throw "$AssetName does not contain '$($mapping.from)' (looked for '$leaf'); the index would map a file the archive does not ship."
            }
        }
    }
    finally {
        $archive.Dispose()
    }
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
