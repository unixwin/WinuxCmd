# Is real-time scanning actually active, regardless of what the status flag says?
# Writes the standard EICAR test string (harmless, industry-standard AV probe)
# into a non-excluded directory and reports whether it survives.
$ErrorActionPreference = 'Continue'
'--- features ---'
Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows Defender\Features' -ErrorAction SilentlyContinue |
    Select-Object TamperProtection, PassiveMode, RealTimeProtocol | Format-List

$dir = 'D:\repo\wc-av1396\DOCS\perf-1396\raw\eicar'
New-Item -ItemType Directory -Force -Path $dir | Out-Null
$file = Join-Path $dir 'eicar.com'
$eicar = 'X5O!P%@AP[4\PZX54(P^)7CC)7}$EICAR-STANDARD-ANTIVIRUS-TEST-FILE!$H+H*'
try {
    Set-Content -LiteralPath $file -Value $eicar -Encoding ASCII -ErrorAction Stop
    'write: succeeded'
} catch {
    'write: blocked -> ' + $_.Exception.Message
}
Start-Sleep -Seconds 2
"exists after 2s: " + (Test-Path -LiteralPath $file)
if (Test-Path $file) { "size: " + (Get-Item $file).Length }
'--- recent detections ---'
Get-MpThreatDetection -ErrorAction SilentlyContinue | Sort-Object InitialDetectionTime -Descending |
    Select-Object -First 3 InitialDetectionTime, ThreatID, Resources | Format-List
'--- status flag ---'
(Get-MpComputerStatus) | Select-Object RealTimeProtectionEnabled, AntivirusEnabled | Format-List
Remove-Item -LiteralPath $file -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $dir -Recurse -Force -ErrorAction SilentlyContinue
