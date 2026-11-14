<#
.SYNOPSIS
  Measure what the Defender engine costs to scan one image (issue #1396).

.DESCRIPTION
  This host cannot turn real-time protection on (see docs), so the per-spawn
  on-access cost cannot be observed directly. What can be observed is the
  engine's own file-scan cost, via the supported command line tool:

      MpCmdRun.exe -Scan -ScanType 3 -File <image>

  That number is the part a real-time monitor would add to every spawn if it
  did not have a cached verdict; it is reported as a proxy, never as an
  on-access measurement. Scanning is repeated to show the engine's own file
  cache behaviour, and can be repeated with the directory excluded (Defender
  then returns immediately, which is what the exclusion mitigation buys).

.PARAMETER Variants
  Payload variants under -PayloadRoot.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Condition,
    [string[]]$Variants = @('unsigned', 'signed', 'motw-unsigned', 'motw-signed'),
    [string[]]$Binaries = @('ls', 'git'),
    [string]$PayloadRoot = '',
    [int]$Samples = 5,
    [string]$OutCsv = ''
)

$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
if (-not $PayloadRoot) { $PayloadRoot = Join-Path $here '..\raw\payload' }
if (-not $OutCsv) { $OutCsv = Join-Path $here '..\raw\ondemand.csv' }
$mp = Join-Path $env:ProgramFiles 'Windows Defender\MpCmdRun.exe'
if (-not (Test-Path -LiteralPath $mp)) { throw "MpCmdRun.exe not found at $mp" }
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutCsv) | Out-Null

$rows = [System.Collections.Generic.List[object]]::new()
foreach ($variant in $Variants) {
    foreach ($b in $Binaries) {
        $exe = Join-Path (Join-Path $PayloadRoot $variant) ($b + '.exe')
        if (-not (Test-Path -LiteralPath $exe)) { Write-Warning "skip missing $exe"; continue }
        for ($i = 0; $i -lt $Samples; $i++) {
            $sw = [Diagnostics.Stopwatch]::StartNew()
            $out = & $mp -Scan -ScanType 3 -File $exe -DisableRemediation 2>&1 | Out-String
            $sw.Stop()
            $verdict = if ($out -match 'no threats|未发现威胁|no malware') { 'clean' }
                       elseif ($out -match 'threat|威胁') { 'threat-or-message' }
                       else { 'unknown' }
            $rows.Add([pscustomobject]@{
                utc       = (Get-Date).ToUniversalTime().ToString('o')
                condition = $Condition
                variant   = $variant
                binary    = $b
                sample    = $i
                ms        = [math]::Round($sw.Elapsed.TotalMilliseconds, 1)
                verdict   = $verdict
                exe       = $exe
                raw       = ($out -replace '\s+', ' ').Trim()
            })
        }
        $ms = @($rows | Where-Object { $_.variant -eq $variant -and $_.binary -eq $b } |
                ForEach-Object { [double]$_.ms } | Sort-Object)
        Write-Host ("{0,-14} {1,-4} n={2} median={3,8:N1} min={4,8:N1} max={5,8:N1}" -f `
                $variant, $b, $ms.Count, $ms[[int]($ms.Count / 2)], $ms[0], $ms[$ms.Count - 1])
    }
}
if (Test-Path -LiteralPath $OutCsv) {
    $rows | Export-Csv -NoTypeInformation -Encoding UTF8 -Append -Path $OutCsv
} else {
    $rows | Export-Csv -NoTypeInformation -Encoding UTF8 -Path $OutCsv
}
Write-Host "rows appended to $OutCsv"
