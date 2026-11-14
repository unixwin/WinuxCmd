<#
.SYNOPSIS
  Save / restore / change the Defender state used as the control variable of
  the issue #1396 experiment. Must run elevated.

.DESCRIPTION
  The interesting independent variables on a Defender host are:
    * real-time protection on/off (the AV-on vs AV-off contrast),
    * Defender path exclusions (the "exclusion" mitigation),
    * cloud/MAPS settings (this host has them off by policy; recorded, not changed).

  Because changing them mutates the machine, -Save writes the original state to
  JSON and -Restore puts it back exactly. Nothing here is left enabled after the
  experiment.

.PARAMETER Action
  Save | Show | On | Off | AddExclusion | RemoveExclusion | Restore

.PARAMETER Path
  Payload directory for AddExclusion/RemoveExclusion.

.PARAMETER StateFile
  Where the original state is stored (default ..\raw\defender-original.json).
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Save', 'Show', 'On', 'Off', 'AddExclusion', 'RemoveExclusion', 'Restore')]
    [string]$Action,
    [string]$Path = '',
    [string]$StateFile = ''
)

$ErrorActionPreference = 'Stop'
if (-not $StateFile) { $StateFile = Join-Path $PSScriptRoot '..\raw\defender-original.json' }

$id = [Security.Principal.WindowsIdentity]::GetCurrent()
$isAdmin = ([Security.Principal.WindowsPrincipal]$id).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
"IsAdmin=$isAdmin"

function Get-State {
    $s = Get-MpComputerStatus
    $p = Get-MpPreference
    [pscustomobject]@{
        capturedUtc                  = (Get-Date).ToUniversalTime().ToString('o')
        realTimeProtectionEnabled    = $s.RealTimeProtectionEnabled
        behaviorMonitorEnabled       = $s.BehaviorMonitorEnabled
        ioavProtectionEnabled        = $s.IoavProtectionEnabled
        onAccessProtectionEnabled    = $s.OnAccessProtectionEnabled
        disableRealtimeMonitoring    = $p.DisableRealtimeMonitoring
        exclusionPath                = @($p.ExclusionPath)
        exclusionProcess             = @($p.ExclusionProcess)
        mapsReporting                = $p.MAPSReporting
        submitSamplesConsent         = $p.SubmitSamplesConsent
        cloudBlockLevel              = $p.CloudBlockLevel
        signatureVersion             = $s.AntivirusSignatureVersion
        engineVersion                = $s.AMEngineVersion
        registryDisableRtp           = (Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows Defender\Real-Time Protection' -ErrorAction SilentlyContinue).DisableRealtimeMonitoring
        # Group Policy can pin real-time protection off; on this host it does
        # (HKLM\SOFTWARE\Policies\...\Real-Time Protection\DisableRealtimeMonitoring
        # = 1), and Set-MpPreference alone then cannot turn scanning on.
        policyDisableRtp             = (Get-ItemProperty 'HKLM:\SOFTWARE\Policies\Microsoft\Windows Defender\Real-Time Protection' -ErrorAction SilentlyContinue).DisableRealtimeMonitoring
        policyDisableAntiSpyware     = (Get-ItemProperty 'HKLM:\SOFTWARE\Policies\Microsoft\Windows Defender' -ErrorAction SilentlyContinue).DisableAntiSpyware
    }
}

switch ($Action) {
    'Show' {
        Get-State | Format-List
    }
    'Save' {
        if (Test-Path -LiteralPath $StateFile) {
            Write-Host "state file already exists, not overwriting: $StateFile"
        } else {
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $StateFile) | Out-Null
            Get-State | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 -LiteralPath $StateFile
            Write-Host "saved original state to $StateFile"
        }
        Get-State | Format-List
    }
    'On' {
        Set-MpPreference -DisableRealtimeMonitoring $false
        # The policy pin (see Get-State) overrides Set-MpPreference; lift it so
        # the engine can actually scan, then restart the service so the policy
        # change is picked up. Verified below instead of assumed.
        $pk = 'HKLM:\SOFTWARE\Policies\Microsoft\Windows Defender\Real-Time Protection'
        if ((Get-ItemProperty $pk -ErrorAction SilentlyContinue).DisableRealtimeMonitoring -ne $null) {
            Set-ItemProperty -Path $pk -Name DisableRealtimeMonitoring -Value 0 -Type DWord
        }
        $k = 'HKLM:\SOFTWARE\Microsoft\Windows Defender\Real-Time Protection'
        # That key is owned by TrustedInstaller; the Defender API (Set-MpPreference)
        # is the supported writer, so a direct write is best-effort only.
        try { Set-ItemProperty -Path $k -Name DisableRealtimeMonitoring -Value 0 -Type DWord -ErrorAction Stop }
        catch { "note: direct registry write refused ($($_.Exception.Message -replace '\s+', ' ')); relying on Set-MpPreference" }
        Restart-Service WinDefend -Force -ErrorAction SilentlyContinue
        Start-Sleep -Seconds 8
        $s = Get-MpComputerStatus
        "RealTimeProtectionEnabled=$($s.RealTimeProtectionEnabled)"
        "BehaviorMonitorEnabled=$($s.BehaviorMonitorEnabled)"
        "AMRunningMode=$($s.AMRunningMode)"
        if (-not $s.RealTimeProtectionEnabled) { throw 'real-time protection did not turn on' }
    }
    'Off' {
        Set-MpPreference -DisableRealtimeMonitoring $true
        Start-Sleep -Seconds 3
        "RealTimeProtectionEnabled=$((Get-MpComputerStatus).RealTimeProtectionEnabled)"
    }
    'AddExclusion' {
        if (-not $Path) { throw 'AddExclusion needs -Path' }
        Add-MpPreference -ExclusionPath $Path
        "ExclusionPath=$(((Get-MpPreference).ExclusionPath | Sort-Object) -join '; ')"
    }
    'RemoveExclusion' {
        if (-not $Path) { throw 'RemoveExclusion needs -Path' }
        Remove-MpPreference -ExclusionPath $Path
        "ExclusionPath=$(((Get-MpPreference).ExclusionPath | Sort-Object) -join '; ')"
    }
    'Restore' {
        if (-not (Test-Path -LiteralPath $StateFile)) { throw "no saved state at $StateFile" }
        $orig = Get-Content -LiteralPath $StateFile -Raw | ConvertFrom-Json
        $now = Get-State
        # Exclusions: add back what was removed, drop what we added.
        foreach ($p in $orig.exclusionPath) {
            if ($p -and $now.exclusionPath -notcontains $p) { Add-MpPreference -ExclusionPath $p }
        }
        foreach ($p in $now.exclusionPath) {
            if ($p -and $orig.exclusionPath -notcontains $p) { Remove-MpPreference -ExclusionPath $p }
        }
        if ($orig.realTimeProtectionEnabled) {
            Set-MpPreference -DisableRealtimeMonitoring $false
        } else {
            Set-MpPreference -DisableRealtimeMonitoring $true
            $k = 'HKLM:\SOFTWARE\Microsoft\Windows Defender\Real-Time Protection'
            Set-ItemProperty -Path $k -Name DisableRealtimeMonitoring -Value 1 -Type DWord -ErrorAction SilentlyContinue
            $pk = 'HKLM:\SOFTWARE\Policies\Microsoft\Windows Defender\Real-Time Protection'
            if ($null -ne $orig.policyDisableRtp) {
                Set-ItemProperty -Path $pk -Name DisableRealtimeMonitoring -Value ([int]$orig.policyDisableRtp) -Type DWord -ErrorAction SilentlyContinue
            }
            Restart-Service WinDefend -Force -ErrorAction SilentlyContinue
        }
        Start-Sleep -Seconds 3
        '--- restored ---'
        Get-State | Format-List
    }
}
