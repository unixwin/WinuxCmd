<#
.SYNOPSIS
Compare the installed Microsoft Coreutils command surface against WinuxCmd.

.DESCRIPTION
Builds a compatibility matrix from two command directories by:
- enumerating command executables
- running shared commands with --help
- extracting option-like tokens from help output
- writing JSON and Markdown summaries
#>

[CmdletBinding()]
param(
    [string]$WinuxDir = "",
    [string]$MicrosoftDir = "",
    [string]$OutputJson = "",
    [string]$OutputMarkdown = ""
)

$ErrorActionPreference = "Stop"

function Resolve-DefaultWinuxDir {
    $candidates = @(
        (Join-Path $PSScriptRoot "..\.winuxcmd\bin"),
        (Join-Path $PSScriptRoot "..\build-vs-installer\stage"),
        (Join-Path $PSScriptRoot "..\build-vs-installer\stage\bin")
    )

    foreach ($candidate in $candidates) {
        $full = [System.IO.Path]::GetFullPath($candidate)
        if (Test-Path -LiteralPath $full) {
            return $full
        }
    }

    throw "Could not find a Winux command directory. Pass -WinuxDir explicitly."
}

function Resolve-DefaultMicrosoftDir {
    $registryEntry = Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*" -ErrorAction SilentlyContinue |
        Where-Object { $_.DisplayName -like "Coreutils for Windows*" } |
        Select-Object -First 1

    if ($registryEntry -and $registryEntry.InstallLocation) {
        $binDir = Join-Path $registryEntry.InstallLocation "bin"
        if (Test-Path -LiteralPath $binDir) {
            return $binDir
        }
    }

    $fallbacks = @(
        "C:\Users\cmx\repo\_winget_coreutils_install\bin",
        (Join-Path $env:ProgramFiles "coreutils\bin"),
        (Join-Path $env:LOCALAPPDATA "Programs\coreutils\bin")
    )

    foreach ($candidate in $fallbacks) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and (Test-Path -LiteralPath $candidate)) {
            return $candidate
        }
    }

    throw "Could not find a Microsoft Coreutils command directory. Pass -MicrosoftDir explicitly."
}

function Get-CommandMap {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Directory,
        [string[]]$IgnoredNames = @()
    )

    $map = @{}

    Get-ChildItem -LiteralPath $Directory -File -Filter "*.exe" -ErrorAction SilentlyContinue |
        ForEach-Object {
            $name = $_.BaseName
            if ($name -in $IgnoredNames) {
                return
            }

            $map[$name] = $_.FullName
        }

    return $map
}

function Get-HelpText {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Executable
    )

    $output = & $Executable "--help" 2>&1 | Out-String
    $exitCode = $LASTEXITCODE

    return [PSCustomObject]@{
        ExitCode = $exitCode
        Text = $output
    }
}

function Get-OptionTokens {
    param(
        [AllowNull()]
        [AllowEmptyString()]
        [string]$HelpText
    )

    if ([string]::IsNullOrEmpty($HelpText)) {
        return @()
    }

    $pattern = '(?<![A-Za-z0-9_])(--[A-Za-z0-9][A-Za-z0-9-]*|-[A-Za-z0-9?])(?=(?:[ =][A-Z<\[])?(?:\s|,|;|$))'
    $tokens = [regex]::Matches($HelpText, $pattern) |
        ForEach-Object { $_.Groups[1].Value } |
        Sort-Object -Unique

    return @($tokens)
}

function Join-TokenList {
    param([string[]]$Tokens)

    if (-not $Tokens -or $Tokens.Count -eq 0) {
        return "-"
    }

    return ($Tokens -join ", ")
}

function Get-SharedCommandRows {
    param(
        [hashtable]$WinuxMap,
        [hashtable]$MicrosoftMap
    )

    $sharedNames = @($WinuxMap.Keys | Where-Object { $MicrosoftMap.ContainsKey($_) } | Sort-Object)
    $rows = New-Object System.Collections.Generic.List[object]

    foreach ($name in $sharedNames) {
        $winuxHelp = Get-HelpText -Executable $WinuxMap[$name]
        $microsoftHelp = Get-HelpText -Executable $MicrosoftMap[$name]

        $winuxOptions = Get-OptionTokens -HelpText $winuxHelp.Text
        $microsoftOptions = Get-OptionTokens -HelpText $microsoftHelp.Text

        $sharedOptions = @($winuxOptions | Where-Object { $_ -in $microsoftOptions } | Sort-Object -Unique)
        $winuxOnly = @($winuxOptions | Where-Object { $_ -notin $microsoftOptions } | Sort-Object -Unique)
        $microsoftOnly = @($microsoftOptions | Where-Object { $_ -notin $winuxOptions } | Sort-Object -Unique)

        $classification = if ($winuxOnly.Count -eq 0 -and $microsoftOnly.Count -eq 0) {
            "same"
        } elseif ($microsoftOnly.Count -eq 0) {
            "winux-superset"
        } elseif ($winuxOnly.Count -eq 0) {
            "microsoft-superset"
        } else {
            "different"
        }

        $rows.Add([PSCustomObject]@{
            Command = $name
            Classification = $classification
            WinuxHelpExitCode = $winuxHelp.ExitCode
            MicrosoftHelpExitCode = $microsoftHelp.ExitCode
            WinuxOptionCount = $winuxOptions.Count
            MicrosoftOptionCount = $microsoftOptions.Count
            SharedOptionCount = $sharedOptions.Count
            WinuxOnlyCount = $winuxOnly.Count
            MicrosoftOnlyCount = $microsoftOnly.Count
            WinuxOnly = $winuxOnly
            MicrosoftOnly = $microsoftOnly
        })
    }

    return $rows.ToArray()
}

function Write-MarkdownReport {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [pscustomobject]$Report
    )

    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add("# WinuxCmd vs Microsoft Coreutils Compatibility Matrix")
    $lines.Add("")
    $lines.Add("Generated from live command directories and \`--help\` output.")
    $lines.Add("")
    $lines.Add("Generated at: $($Report.GeneratedAt)")
    $lines.Add("Winux directory: $($Report.WinuxDir)")
    $lines.Add("Microsoft directory: $($Report.MicrosoftDir)")
    $lines.Add("")
    $lines.Add("## Summary")
    $lines.Add("")
    $lines.Add("| Metric | Value |")
    $lines.Add("| --- | ---: |")
    $lines.Add("| Winux commands | $($Report.Summary.WinuxCommandCount) |")
    $lines.Add("| Microsoft commands | $($Report.Summary.MicrosoftCommandCount) |")
    $lines.Add("| Shared commands | $($Report.Summary.SharedCommandCount) |")
    $lines.Add("| Winux-only commands | $($Report.Summary.WinuxOnlyCommandCount) |")
    $lines.Add("| Microsoft-only commands | $($Report.Summary.MicrosoftOnlyCommandCount) |")
    $lines.Add("| Shared commands with identical option tokens | $($Report.Summary.SameOptionSurfaceCount) |")
    $lines.Add("| Shared commands where Winux has extra option tokens only | $($Report.Summary.WinuxSupersetCount) |")
    $lines.Add("| Shared commands where Microsoft has extra option tokens only | $($Report.Summary.MicrosoftSupersetCount) |")
    $lines.Add("| Shared commands with mixed option-token differences | $($Report.Summary.DifferentCount) |")
    $lines.Add("")
    $lines.Add("## Winux-only Commands")
    $lines.Add("")
    $lines.Add((Join-TokenList -Tokens $Report.WinuxOnlyCommands))
    $lines.Add("")
    $lines.Add("## Microsoft-only Commands")
    $lines.Add("")
    $lines.Add((Join-TokenList -Tokens $Report.MicrosoftOnlyCommands))
    $lines.Add("")
    $lines.Add("## Shared Command Summary")
    $lines.Add("")
    $lines.Add("| Command | Class | Winux opts | Microsoft opts | Winux-only | Microsoft-only |")
    $lines.Add("| --- | --- | ---: | ---: | ---: | ---: |")

    foreach ($row in $Report.SharedCommands) {
        $lines.Add("| $($row.Command) | $($row.Classification) | $($row.WinuxOptionCount) | $($row.MicrosoftOptionCount) | $($row.WinuxOnlyCount) | $($row.MicrosoftOnlyCount) |")
    }

    $lines.Add("")
    $lines.Add("## Shared Command Details")
    $lines.Add("")

    foreach ($row in $Report.SharedCommands) {
        $lines.Add("### $($row.Command)")
        $lines.Add("")
        $lines.Add("Classification: $($row.Classification)")
        $lines.Add("Winux help exit code: $($row.WinuxHelpExitCode)")
        $lines.Add("Microsoft help exit code: $($row.MicrosoftHelpExitCode)")
        $lines.Add("Winux-only tokens: $(Join-TokenList -Tokens $row.WinuxOnly)")
        $lines.Add("Microsoft-only tokens: $(Join-TokenList -Tokens $row.MicrosoftOnly)")
        $lines.Add("")
    }

    $parent = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }

    Set-Content -LiteralPath $Path -Value ($lines -join "`r`n") -Encoding UTF8
}

try {
    $resolvedWinuxDir = if ([string]::IsNullOrWhiteSpace($WinuxDir)) {
        Resolve-DefaultWinuxDir
    } else {
        (Resolve-Path -LiteralPath $WinuxDir).Path
    }

    $resolvedMicrosoftDir = if ([string]::IsNullOrWhiteSpace($MicrosoftDir)) {
        Resolve-DefaultMicrosoftDir
    } else {
        (Resolve-Path -LiteralPath $MicrosoftDir).Path
    }

    $resolvedJson = if ([string]::IsNullOrWhiteSpace($OutputJson)) {
        [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\DOCS\generated\microsoft_coreutils_matrix.json"))
    } else {
        [System.IO.Path]::GetFullPath($OutputJson)
    }

    $resolvedMarkdown = if ([string]::IsNullOrWhiteSpace($OutputMarkdown)) {
        [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\DOCS\generated\microsoft_coreutils_matrix.md"))
    } else {
        [System.IO.Path]::GetFullPath($OutputMarkdown)
    }

    $winuxMap = Get-CommandMap -Directory $resolvedWinuxDir -IgnoredNames @("winuxcmd", "unins000")
    $microsoftMap = Get-CommandMap -Directory $resolvedMicrosoftDir -IgnoredNames @("coreutils", "unins000")

    $winuxOnlyCommands = @($winuxMap.Keys | Where-Object { -not $microsoftMap.ContainsKey($_) } | Sort-Object)
    $microsoftOnlyCommands = @($microsoftMap.Keys | Where-Object { -not $winuxMap.ContainsKey($_) } | Sort-Object)
    $sharedRows = @(Get-SharedCommandRows -WinuxMap $winuxMap -MicrosoftMap $microsoftMap)

    $report = [PSCustomObject]@{
        GeneratedAt = (Get-Date).ToString("o")
        WinuxDir = $resolvedWinuxDir
        MicrosoftDir = $resolvedMicrosoftDir
        Summary = [PSCustomObject]@{
            WinuxCommandCount = $winuxMap.Count
            MicrosoftCommandCount = $microsoftMap.Count
            SharedCommandCount = $sharedRows.Count
            WinuxOnlyCommandCount = $winuxOnlyCommands.Count
            MicrosoftOnlyCommandCount = $microsoftOnlyCommands.Count
            SameOptionSurfaceCount = @($sharedRows | Where-Object { $_.Classification -eq "same" }).Count
            WinuxSupersetCount = @($sharedRows | Where-Object { $_.Classification -eq "winux-superset" }).Count
            MicrosoftSupersetCount = @($sharedRows | Where-Object { $_.Classification -eq "microsoft-superset" }).Count
            DifferentCount = @($sharedRows | Where-Object { $_.Classification -eq "different" }).Count
        }
        WinuxOnlyCommands = $winuxOnlyCommands
        MicrosoftOnlyCommands = $microsoftOnlyCommands
        SharedCommands = $sharedRows
    }

    $jsonParent = Split-Path -Parent $resolvedJson
    if (-not (Test-Path -LiteralPath $jsonParent)) {
        New-Item -ItemType Directory -Path $jsonParent -Force | Out-Null
    }

    $report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $resolvedJson -Encoding UTF8
    Write-MarkdownReport -Path $resolvedMarkdown -Report $report

    Write-Host "JSON: $resolvedJson"
    Write-Host "Markdown: $resolvedMarkdown"
}
catch {
    Write-Error ("compare-coreutils-compat.ps1 failed at line {0}: {1}" -f $_.InvocationInfo.ScriptLineNumber, $_.Exception.Message)
    throw
}
