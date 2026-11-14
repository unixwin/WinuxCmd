param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Rest)
$id = [Security.Principal.WindowsIdentity]::GetCurrent()
"IsAdmin=" + ([Security.Principal.WindowsPrincipal]$id).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
"WhoAmI=" + $id.Name
"Defender=" + (Get-MpComputerStatus).RealTimeProtectionEnabled
"Exclusions=" + ((Get-MpPreference).ExclusionPath -join ';')
