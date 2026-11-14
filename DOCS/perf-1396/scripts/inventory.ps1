<#
.SYNOPSIS
  Environment + binary inventory for the AV-on-spawn study (issue #1396).

.DESCRIPTION
  Records machine facts (OS build, CPU, security products, Defender state,
  MOTW/attachment-zone support, admin status) and per-binary facts
  (path, size, link count, Authenticode status, SHA256, MOTW Zone.Identifier)
  for the binaries participating in the spawn-latency matrix.

  Everything is written as a CSV of key/value/note rows plus a flat text log,
  so the raw evidence can be attached to the issue comment.

.PARAMETER OutDir
  Directory for inventory.csv / inventory.txt. Defaults to ..\raw next to this script.
#>
[CmdletBinding()]
param(
    [string]$OutDir = ''
)

$ErrorActionPreference = 'Stop'
# $PSScriptRoot is not populated while parameter defaults are evaluated.
if (-not $OutDir) { $OutDir = Join-Path $PSScriptRoot '..\raw' }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$rows = [System.Collections.Generic.List[object]]::new()
function Add-Row([string]$Category, [string]$Name, $Value, [string]$Note = '') {
    $rows.Add([pscustomobject]@{
        category = $Category
        name     = $Name
        value    = if ($null -eq $Value) { '' } else { [string]$Value }
        note     = $Note
    })
}

# ---------------------------------------------------------------- OS / CPU --
$os = Get-CimInstance Win32_OperatingSystem
Add-Row os Caption        $os.Caption
Add-Row os Version        $os.Version
Add-Row os BuildNumber    $os.BuildNumber
Add-Row os Architecture   $os.OSArchitecture
Add-Row os UBR            (Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion').UBR
$cpu = Get-CimInstance Win32_Processor | Select-Object -First 1
Add-Row cpu Name          $cpu.Name
Add-Row cpu Cores         $cpu.NumberOfCores
Add-Row cpu Logical       $cpu.NumberOfLogicalProcessors
Add-Row cpu MaxClockMHz   $cpu.MaxClockSpeed
$cs = Get-CimInstance Win32_ComputerSystem
Add-Row machine Manufacturer $cs.Manufacturer
Add-Row machine Model        $cs.Model
Add-Row machine RAM_GB       ([math]::Round($cs.TotalPhysicalMemory / 1GB, 1))
Add-Row machine IsVirtualMachine ($cs.Model -match 'Virtual|VMware|VirtualBox|KVM|HVM')
Add-Row machine Admin        ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
Add-Row machine User         ([Security.Principal.WindowsIdentity]::GetCurrent().Name)
Add-Row machine PowerShell   $PSVersionTable.PSVersion.ToString()
Add-Row machine DateUtc      (Get-Date).ToUniversalTime().ToString('o')

# ------------------------------------------------------------- SecurityCenter --
try {
    $avs = Get-CimInstance -Namespace root/SecurityCenter2 -ClassName AntiVirusProduct
    $i = 0
    foreach ($av in $avs) {
        Add-Row security ("AV{0}.displayName" -f $i)  $av.displayName
        Add-Row security ("AV{0}.productState" -f $i) ('0x{0:X6}' -f $av.productState)
        Add-Row security ("AV{0}.path" -f $i)         $av.pathToSignedProductExe
        $i++
    }
    if (-not $avs) { Add-Row security AV 'none registered' }
} catch {
    Add-Row security AV ('SecurityCenter2 query failed: ' + $_.Exception.Message)
}

# Signature versions of every registered AV package (helps identify 360/Huorong).
try {
    foreach ($av in $avs) {
        $name = $av.displayName -replace '[^\w]', '_'
        $sig = Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows Defender\Signature Updates" -ErrorAction SilentlyContinue
        if ($sig -and $name -match 'Defender') {
            Add-Row security "$name.AMEngineVersion" $sig.AMEngineVersion
            Add-Row security "$name.AVSignatureVersion" $sig.AVSignatureVersion
            Add-Row security "$name.SignatureUpdate" $sig.AntivirusSignatureLastUpdated
        }
    }
} catch { }

# ---------------------------------------------------------------- Defender --
$mp = Get-Command Get-MpComputerStatus -ErrorAction SilentlyContinue
if ($mp) {
    try {
        $s = Get-MpComputerStatus
        foreach ($p in 'AMServiceEnabled','AntivirusEnabled','RealTimeProtectionEnabled',
                       'BehaviorMonitorEnabled','IoavProtectionEnabled','OnAccessProtectionEnabled',
                       'IsTamperProtected','NISEnabled','AMEngineVersion','AMProductVersion',
                       'AntivirusSignatureVersion','AntivirusSignatureLastUpdated') {
            Add-Row defender $p $s.$p
        }
        $prefs = Get-MpPreference
        Add-Row defender ExclusionPath        (($prefs.ExclusionPath        | Sort-Object) -join '; ')
        Add-Row defender ExclusionProcess     (($prefs.ExclusionProcess     | Sort-Object) -join '; ')
        Add-Row defender ExclusionExtension   (($prefs.ExclusionExtension   | Sort-Object) -join '; ')
        Add-Row defender DisableRealtimeMonitoring $prefs.DisableRealtimeMonitoring
        Add-Row defender MAPSReporting        $prefs.MAPSReporting
        Add-Row defender SubmitSamplesConsent $prefs.SubmitSamplesConsent
        Add-Row defender CloudBlockLevel      $prefs.CloudBlockLevel
        Add-Row defender EnableControlledFolderAccess $prefs.EnableControlledFolderAccess
    } catch {
        Add-Row defender state ('Get-MpComputerStatus failed: ' + $_.Exception.Message)
    }
} else {
    Add-Row defender state 'Defender PowerShell module not present'
}

# Third-party AV drivers actually loaded in the kernel (catches AVs that do not
# register with SecurityCenter2, and shows minifilter ordering).
try {
    $flt = & fltmc filters 2>$null
    Add-Row security Minifilters (($flt | Select-Object -Skip 2 | ForEach-Object { ($_ -split '\s+')[0] }) -join ',')
} catch { Add-Row security Minifilters 'fltmc unavailable' }

# ------------------------------------------------------------------ MOTW --
Add-Row motw AttachmentManagerSupported (Test-Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Policies\Attachments')
Add-Row motw SaveZoneInformation        (Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Policies\Attachments' -ErrorAction SilentlyContinue).SaveZoneInformation
Add-Row motw SmartScreenShell           (Test-Path 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer\SmartScreenEnabled')
Add-Row motw SmartScreenValue           (Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer' -ErrorAction SilentlyContinue).SmartScreenEnabled

# --------------------------------------------------------------- Binaries --
$targets = @(
    'C:\Users\cmx\AppData\Local\Programs\Niubash\winuxcmd\usr\bin\ls.exe',
    'C:\Users\cmx\AppData\Local\Programs\Niubash\winuxcmd\usr\bin\which.exe',
    'C:\Users\cmx\AppData\Local\Programs\Niubash\winuxcmd\usr\bin\git.exe',
    'C:\Users\cmx\AppData\Local\Programs\Niubash\winuxcmd\usr\bin\sort.exe',
    'C:\Users\cmx\AppData\Local\Programs\Niubash\winuxcmd\usr\bin\winuxcmd.exe'
)
foreach ($t in $targets) {
    if (-not (Test-Path $t)) { Add-Row binary $t 'MISSING'; continue }
    $f = Get-Item $t -Force
    Add-Row binary $t ('size=' + $f.Length)
    Add-Row binary $t ('linkType=' + $f.LinkType + ' target=' + $f.Target)
    Add-Row binary $t ('lastWriteUtc=' + $f.LastWriteTimeUtc.ToString('o'))
    try { Add-Row binary $t ('hardlinkCount=' + (Get-CimInstance Win32_LogicalFileSecuritySetting -ErrorAction SilentlyContinue | Out-Null)) } catch { }
    $sig = Get-AuthenticodeSignature $t
    Add-Row binary $t ('sigStatus=' + $sig.Status)
    Add-Row binary $t ('sigSubject=' + $sig.SignerCertificate.Subject)
    Add-Row binary $t ('sigThumbprint=' + $sig.SignerCertificate.Thumbprint)
    Add-Row binary $t ('sigTimeStampSubject=' + $sig.TimeStamperCertificate.Subject)
    Add-Row binary $t ('sha256=' + (Get-FileHash $t -Algorithm SHA256).Hash)
    $zone = Get-Content -LiteralPath ($t + ':Zone.Identifier') -ErrorAction SilentlyContinue
    Add-Row binary $t ('motw=' + (($zone -join '|') -replace '\s+', ' '))
}

# Named hardlink count via fsutil so link topology is auditable.
foreach ($t in $targets) {
    if (-not (Test-Path $t)) { continue }
    $hl = (& fsutil hardlink list $t 2>$null) -join ' | '
    Add-Row binary $t ('hardlinkList=' + $hl)
}

$csv = Join-Path $OutDir 'inventory.csv'
$rows | Export-Csv -NoTypeInformation -Encoding UTF8 -Path $csv
$txt = Join-Path $OutDir 'inventory.txt'
$rows | Format-Table -AutoSize -Wrap | Out-String -Width 300 | Set-Content -Encoding UTF8 $txt
Write-Host "wrote $csv and $txt ($($rows.Count) rows)"
