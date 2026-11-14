<#
.SYNOPSIS
  Run the whole issue #1396 spawn-latency matrix for one AV condition.

.DESCRIPTION
  Legs, for every payload variant and every applet:
      warm      repeated spawn of a stable path (the "user types ls again" case)
      cold      spawn of a fresh byte copy at a new path (post-install/upgrade)
      newhash   cold + a trailing pad byte, so the content hash is new too
      shell     the Explorer spawn API (where SmartScreen/MOTW would act)

  Plus:
      installed the binaries the user actually has on PATH, measured in place,
                which is the configuration the bug report describes.

  Every leg appends to one samples.csv, so the analysis script can rebuild all
  tables from raw observations.

.PARAMETER Condition
  Free-form label recorded with every row, e.g. rtp-off, rtp-on, rtp-on-excluded.

.PARAMETER Samples
  Samples per leg (default 50).

.PARAMETER PayloadRoot
  Directory produced by prepare-binaries.ps1.

.PARAMETER Variants
  Which payload variants to run.

.PARAMETER Phases
  warm, cold, newhash, shell, installed.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Condition,
    [int]$Samples = 50,
    [string]$PayloadRoot = '',
    [string[]]$Variants = @('unsigned', 'signed', 'motw-unsigned', 'motw-signed'),
    [string[]]$Phases = @('installed', 'warm', 'cold', 'newhash', 'shell'),
    [string]$OutCsv = '',
    [int]$ColdSamples = 50,
    [int]$ShellSamples = 10,
    [int]$ShellTimeoutMs = 8000
)

$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
# A shell on the other side of this call may deliver "-Phases a,b,c" as a single
# element; normalise so both comma-joined and repeated flags work.
$Phases = @($Phases | ForEach-Object { $_ -split ',' } | Where-Object { $_ -ne '' })
$Variants = @($Variants | ForEach-Object { $_ -split ',' } | Where-Object { $_ -ne '' })
if (-not $PayloadRoot) { $PayloadRoot = Join-Path $here '..\raw\payload' }
if (-not $OutCsv) { $OutCsv = Join-Path $here '..\raw\samples.csv' }
$bench = Join-Path $here 'spawn-bench.ps1'
$applets = @('ls', 'which', 'sort', 'git')
$installedBin = 'C:\Users\cmx\AppData\Local\Programs\Niubash\winuxcmd\usr\bin'

function Invoke-Leg {
    param([string]$Exe, [string]$Binary, [string]$Variant, [string]$State,
          [string]$Api = 'createprocess', [string]$PrepMode = '', [int]$N = 50, [string]$Notes = '',
          [int]$TimeoutMs = 15000)
    $a = @{
        Exe = $Exe; Binary = $Binary; Variant = $Variant; Condition = $Condition
        State = $State; Api = $Api; Samples = $N; OutCsv = $OutCsv; Notes = $Notes
        TimeoutMs = $TimeoutMs
    }
    if ($PrepMode) { $a.PrepMode = $PrepMode }
    & $bench @a
}

Write-Host "=== condition=$Condition samples=$Samples ==="
$loadBefore = (Get-CimInstance Win32_Processor | Measure-Object -Property LoadPercentage -Average).Average
Write-Host ("host load before matrix: {0}%" -f $loadBefore)

if ($Phases -contains 'installed') {
    if (Test-Path -LiteralPath $installedBin) {
        foreach ($b in $applets) {
            $exe = Join-Path $installedBin ($b + '.exe')
            if (-not (Test-Path -LiteralPath $exe)) { continue }
            Invoke-Leg -Exe $exe -Binary $b -Variant 'installed' -State 'warm' `
                -Notes 'on PATH, inside existing Defender exclusions; this is the configuration from the bug report'
        }
    } else {
        Write-Warning "installed bin dir not found: $installedBin"
    }
}

if ($Phases -contains 'control') {
    # Microsoft-signed system binary as a reference: it is what a spawn costs
    # with no involvement from our payload, so it shows the floor and whether
    # the host was noisy during the run.
    $whoami = Join-Path $env:SystemRoot 'System32\whoami.exe'
    Invoke-Leg -Exe $whoami -Binary 'whoami' -Variant 'microsoft-signed' -State 'warm' `
        -Notes 'control: OS-signed binary on the system volume, args=--version is invalid for whoami'
}

foreach ($variant in $Variants) {
    foreach ($b in $applets) {
        $exe = Join-Path (Join-Path $PayloadRoot $variant) ($b + '.exe')
        if (-not (Test-Path -LiteralPath $exe)) { Write-Warning "skip missing $exe"; continue }
        if ($Phases -contains 'warm') { Invoke-Leg -Exe $exe -Binary $b -Variant $variant -State 'warm' }
        if ($Phases -contains 'cold') { Invoke-Leg -Exe $exe -Binary $b -Variant $variant -State 'cold' -N $ColdSamples }
        if ($Phases -contains 'newhash') { Invoke-Leg -Exe $exe -Binary $b -Variant $variant -State 'cold' -PrepMode 'newhash' -N $ColdSamples }
        if ($Phases -contains 'shell') {
            # A zone-marked image on the Explorer path can open the SmartScreen
            # window and not return, so every shell leg is bounded and sampled a
            # few times; a timeout is reported as "blocked", not as a latency.
            $note = if ($variant -like 'motw-*') { 'Explorer path; MOTW image' } else { 'Explorer path, no MOTW' }
            Invoke-Leg -Exe $exe -Binary $b -Variant $variant -State 'warm' -Api 'shellexecute' `
                -N $ShellSamples -TimeoutMs 8000 -Notes $note
        }
    }
}

$loadAfter = (Get-CimInstance Win32_Processor | Measure-Object -Property LoadPercentage -Average).Average
Write-Host ("host load after matrix: {0}%" -f $loadAfter)
Write-Host "rows appended to $OutCsv"
