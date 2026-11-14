<#
.SYNOPSIS
  Build the binary variants for the issue #1396 spawn-latency matrix.

.DESCRIPTION
  Produces four payload variants under -OutDir:

    unsigned      hardlinks of a locally built winuxcmd.exe, named ls/which/sort
                  (this is the wpm layout: one image, many names, no signature),
                  plus a copy of git.exe as the "different image" comparator.
    signed        the same layout, but the base image carries a valid, trusted
                  Authenticode signature from a locally created test CA.
    motw-unsigned unsigned payload whose files carry a Zone.Identifier ADS
                  (ZoneId=3) as if downloaded from the internet.
    motw-signed   signed payload with the same MOTW marks.

  The SignPath release certificate is NOT available in this environment (no
  signing token/secrets), so the "signed" leg uses a locally trusted self-signed
  code-signing certificate. That reproduces the property under test - a valid,
  trusted publisher chain on the image - but not SignPath's accumulated
  reputation. The report labels it accordingly; the SignPath leg is recorded as
  N/A rather than approximated silently.

  Hardlinks are used for ls/which/sort because that is what an installed
  WinuxCmd has: one inode, several names. Cold legs deliberately use byte
  copies (see spawn-bench.ps1) so the file identity is new.

.PARAMETER SourceExe
  winuxcmd.exe from the local build.

.PARAMETER GitExe
  git.exe to use for the git binary. Defaults to the installed niugit build.

.PARAMETER OutDir
  Payload root, default ..\raw\payload.

.PARAMETER Sign
  Sign the signed variant (creates/reuses a test code-signing certificate).

.PARAMETER SignTool
  Optional explicit path to signtool.exe.
#>
[CmdletBinding()]
param(
    [string]$SourceExe = '',
    [string]$GitExe = 'C:\Users\cmx\AppData\Local\Programs\Niubash\winuxcmd\usr\bin\git.exe',
    [string]$OutDir = '',
    [switch]$Sign,
    [string]$SignTool = ''
)

$ErrorActionPreference = 'Stop'
if (-not $OutDir) { $OutDir = Join-Path $PSScriptRoot '..\raw\payload' }
if (-not $SourceExe) {
    $candidate = Join-Path $PSScriptRoot '..\..\..\build-vs\winuxcmd.exe'
    $SourceExe = (Resolve-Path $candidate).Path
}
if (-not (Test-Path -LiteralPath $SourceExe)) { throw "missing -SourceExe $SourceExe" }
if (-not (Test-Path -LiteralPath $GitExe)) { throw "missing -GitExe $GitExe" }

$OutDir = [IO.Path]::GetFullPath($OutDir)
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$applets = @('ls', 'which', 'sort')

function New-HardlinkSet {
    param([string]$BaseImage, [string]$Dir)
    New-Item -ItemType Directory -Force -Path $Dir | Out-Null
    foreach ($a in $applets) {
        $dst = Join-Path $Dir ($a + '.exe')
        if (Test-Path -LiteralPath $dst) { Remove-Item -LiteralPath $dst -Force }
        New-Item -ItemType HardLink -Path $dst -Target $BaseImage | Out-Null
    }
    $git = Join-Path $Dir 'git.exe'
    if (Test-Path -LiteralPath $git) { Remove-Item -LiteralPath $git -Force }
    Copy-Item -LiteralPath $GitExe -Destination $git -Force
}

function Set-Motw {
    param([string]$Path)
    $body = @(
        '[ZoneTransfer]'
        'ZoneId=3'
        'HostUrl=https://github.com/unixwin/WinuxCmd/releases/download/motw/perf1396.zip'
        'ReferrerUrl=https://github.com/unixwin/WinuxCmd/releases'
    ) -join "`r`n"
    Set-Content -LiteralPath $Path -Stream 'Zone.Identifier' -Value $body -Encoding ASCII
}

# ------------------------------------------------------------------ unsigned --
$unsignedBase = Join-Path $OutDir 'winuxcmd-unsigned.exe'
Copy-Item -LiteralPath $SourceExe -Destination $unsignedBase -Force
New-HardlinkSet -BaseImage $unsignedBase -Dir (Join-Path $OutDir 'unsigned')

# -------------------------------------------------------------------- signed --
$signedBase = Join-Path $OutDir 'winuxcmd-signed.exe'
Copy-Item -LiteralPath $unsignedBase -Destination $signedBase -Force
$signInfo = 'not requested'

if ($Sign) {
    if (-not $SignTool) {
        $kits = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
        $SignTool = Get-ChildItem -Path $kits -Filter signtool.exe -Recurse -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match '\\x64\\' } |
            Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
    }
    if (-not $SignTool -or -not (Test-Path -LiteralPath $SignTool)) { throw 'signtool.exe not found; pass -SignTool' }

    $subject = 'CN=WinuxCmd Perf1396 Test Publisher, O=unixwin, C=CN'
    $cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq $subject } |
        Sort-Object NotAfter -Descending | Select-Object -First 1
    if (-not $cert) {
        $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $subject `
            -KeyUsage DigitalSignature -KeyExportPolicy Exportable `
            -NotAfter (Get-Date).AddYears(1) -CertStoreLocation Cert:\CurrentUser\My `
            -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3', '2.5.29.19={text}')
    }
    $cerDir = Join-Path $OutDir '..\certs'
    New-Item -ItemType Directory -Force -Path $cerDir | Out-Null
    $cerPath = Join-Path $cerDir 'perf1396-test.cer'
    Export-Certificate -Cert $cert -FilePath $cerPath -Force | Out-Null

    & $SignTool sign /sha1 $cert.Thumbprint /s My /fd sha256 /d 'WinuxCmd perf1396 test build' $signedBase | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "signtool failed with $LASTEXITCODE" }
    New-HardlinkSet -BaseImage $signedBase -Dir (Join-Path $OutDir 'signed')
    $sig = Get-AuthenticodeSignature $signedBase
    $signInfo = "signtool=$SignTool;thumbprint=$($cert.Thumbprint);status=$($sig.Status);cer=$cerPath"
    if ($sig.Status -ne 'Valid') {
        $signInfo += ';WARNING signature not trusted yet - run trust-test-cert.ps1 elevated before measuring'
    }
} else {
    Write-Warning 'no -Sign: the signed variant is just a copy of the unsigned image (do not measure it)'
    New-HardlinkSet -BaseImage $signedBase -Dir (Join-Path $OutDir 'signed')
}

# ---------------------------------------------------------------------- MOTW --
# MOTW variants are byte copies (not hardlinks): the ADS lives on the stream of
# that specific file, and a shared inode would also share the zone mark.
$names = @($applets | ForEach-Object { $_ + '.exe' }) + 'git.exe'
foreach ($pair in @(@{ src = 'unsigned'; dst = 'motw-unsigned' }, @{ src = 'signed'; dst = 'motw-signed' })) {
    $dir = Join-Path $OutDir $pair.dst
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    foreach ($name in $names) {
        $src = Join-Path (Join-Path $OutDir $pair.src) $name
        $dst = Join-Path $dir $name
        Copy-Item -LiteralPath $src -Destination $dst -Force
        # git keeps the mark too: it is the comparator the issue measured.
        Set-Motw -Path $dst
    }
}

# ------------------------------------------------------------------ manifest --
$rows = foreach ($variant in 'unsigned', 'signed', 'motw-unsigned', 'motw-signed') {
    foreach ($name in (@($applets | ForEach-Object { $_ + '.exe' }) + 'git.exe')) {
        $p = Join-Path (Join-Path $OutDir $variant) $name
        if (-not (Test-Path -LiteralPath $p)) { continue }
        $f = Get-Item -LiteralPath $p -Force
        $sig = Get-AuthenticodeSignature -LiteralPath $p
        $zone = Get-Content -LiteralPath ($p + ':Zone.Identifier') -ErrorAction SilentlyContinue
        [pscustomobject]@{
            variant   = $variant
            binary    = [IO.Path]::GetFileNameWithoutExtension($name)
            exe       = $p
            size      = $f.Length
            sha256    = (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash
            linkType  = $f.LinkType
            sigStatus = $sig.Status
            sigSubject = $sig.SignerCertificate.Subject
            motw      = ($zone -join ';') -replace '\s+', ' '
        }
    }
}
$csv = Join-Path $OutDir 'manifest.csv'
$rows | Export-Csv -NoTypeInformation -Encoding UTF8 -Path $csv
$rows | Format-Table variant, binary, size, linkType, sigStatus, motw -AutoSize | Out-String -Width 200 | Write-Host
Write-Host "manifest: $csv"
Write-Host "signing: $signInfo"
