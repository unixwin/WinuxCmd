<#
.SYNOPSIS
  Trust the perf1396 test code-signing certificate on this machine.

.DESCRIPTION
  The "signed" leg needs the image's Authenticode signature to validate, i.e. the
  signing CA has to be a trusted root and the publisher has to sit in Trusted
  Publishers - which is what a real SignPath certificate gives you for free.
  Adding it here is a machine-wide change, so it is explicit and removable:

    -Action Install   local machine Root + TrustedPublisher
    -Action Remove    undo both
    -Action Show      what the local machine stores currently trust
    -Action SignPath  prints why the real SignPath leg is N/A in this environment

  Must run elevated (see run-elevated.ps1).
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Install', 'Remove', 'Show', 'SignPath')]
    [string]$Action,
    [string]$CerPath = ''
)

$ErrorActionPreference = 'Stop'
if (-not $CerPath) { $CerPath = Join-Path $PSScriptRoot '..\raw\certs\perf1396-test.cer' }

switch ($Action) {
    'Show' {
        Get-ChildItem Cert:\LocalMachine\Root, Cert:\LocalMachine\TrustedPublisher |
            Where-Object { $_.Subject -like '*WinuxCmd Perf1396*' } |
            Select-Object PSParentPath, Subject, Thumbprint, NotAfter | Format-List
    }
    'Install' {
        if (-not (Test-Path -LiteralPath $CerPath)) { throw "no certificate at $CerPath" }
        $c = Import-Certificate -FilePath $CerPath -CertStoreLocation Cert:\LocalMachine\Root
        $p = Import-Certificate -FilePath $CerPath -CertStoreLocation Cert:\LocalMachine\TrustedPublisher
        "Root: $($c.Thumbprint)  TrustedPublisher: $($p.Thumbprint)"
    }
    'Remove' {
        Get-ChildItem Cert:\LocalMachine\Root, Cert:\LocalMachine\TrustedPublisher |
            Where-Object { $_.Subject -like '*WinuxCmd Perf1396*' } |
            ForEach-Object {
                "removing $($_.Thumbprint) from $($_.PSParentPath)"
                Remove-Item -LiteralPath $_.PSPath -Force
            }
    }
    'SignPath' {
        @'
SignPath leg: N/A in this environment.
  * The release pipeline signs through SignPath with an API token held in GitHub
    secrets; no token, project slug, or signing policy is available locally.
  * SignPath test-signing also requires an organisation-level artifact
    configuration; downloading a signed artifact is the only supported way to
    obtain one, and releases here are built from CI only.
  * Therefore the signed legs use a locally trusted self-signed certificate as
    a structural proxy (valid chain + trusted publisher, zero reputation), and
    the report states that explicitly instead of presenting it as SignPath data.
'@
    }
}
