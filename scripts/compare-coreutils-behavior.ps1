<#
.SYNOPSIS
Compare WinuxCmd and Microsoft Coreutils behavior for curated shared commands.

.DESCRIPTION
Creates an isolated sandbox per test case, runs the same command against the
WinuxCmd executable and the Microsoft Coreutils executable, captures stdout,
stderr, exit code, and a post-run filesystem snapshot, then writes JSON and
Markdown reports.
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

function Resolve-OutputPath {
    param(
        [string]$Path,
        [string]$DefaultRelativePath
    )

    if ([string]::IsNullOrWhiteSpace($Path)) {
        return [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot $DefaultRelativePath))
    }

    return [System.IO.Path]::GetFullPath($Path)
}

function New-SandboxTree {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Root
    )

    New-Item -ItemType Directory -Path $Root -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $Root "alpha") -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $Root "beta") -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $Root "copies") -Force | Out-Null

    Set-Content -LiteralPath (Join-Path $Root "sample.txt") -Value @(
        "alpha"
        "beta"
        "alpha beta"
        "gamma"
    ) -Encoding ASCII

    Set-Content -LiteralPath (Join-Path $Root "sortable.txt") -Value @(
        "pear"
        "apple"
        "banana"
    ) -Encoding ASCII

    Set-Content -LiteralPath (Join-Path $Root "alpha\one.txt") -Value "one`n" -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $Root "beta\two.txt") -Value "two`n" -Encoding ASCII
}

function Invoke-NativeCommand {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Executable,
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments,
        [Parameter(Mandatory = $true)]
        [string]$WorkingDirectory,
        [AllowNull()]
        [string]$StdinText
    )

    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = $Executable
    $startInfo.WorkingDirectory = $WorkingDirectory
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardInput = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.CreateNoWindow = $true

    foreach ($argument in $Arguments) {
        [void]$startInfo.ArgumentList.Add($argument)
    }

    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $startInfo
    [void]$process.Start()

    if ($null -ne $StdinText) {
        $process.StandardInput.Write($StdinText)
    }
    $process.StandardInput.Close()

    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    $process.WaitForExit()

    return [PSCustomObject]@{
        ExitCode = $process.ExitCode
        Stdout = $stdout
        Stderr = $stderr
    }
}

function Normalize-Text {
    param([AllowNull()][string]$Text)

    if ($null -eq $Text) {
        return ""
    }

    return (($Text -replace "`r`n", "`n") -replace "`r", "`n").TrimEnd("`n")
}

function Get-TreeSnapshot {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Root
    )

    $entries = Get-ChildItem -LiteralPath $Root -Recurse -Force |
        Sort-Object FullName |
        ForEach-Object {
            $relative = [System.IO.Path]::GetRelativePath($Root, $_.FullName).Replace("\", "/")
            if ($_.PSIsContainer) {
                return "dir:$relative"
            }

            $content = Get-Content -LiteralPath $_.FullName -Raw -Encoding UTF8 -ErrorAction SilentlyContinue
            $normalizedContent = Normalize-Text -Text $content
            return "file:${relative}:$normalizedContent"
        }

    return @($entries)
}

function Join-ArgumentsDisplay {
    param([string[]]$Items)

    if (-not $Items -or $Items.Count -eq 0) {
        return ""
    }

    return ($Items -join " ")
}

function New-TestCase {
    param(
        [string]$Name,
        [string]$Command,
        [string[]]$Arguments,
        [string]$StdinText = $null
    )

    return [PSCustomObject]@{
        Name = $Name
        Command = $Command
        Arguments = $Arguments
        StdinText = $StdinText
    }
}

function Get-TestCases {
    return @(
        (New-TestCase -Name "cat-file" -Command "cat" -Arguments @("sample.txt")),
        (New-TestCase -Name "cp-file" -Command "cp" -Arguments @("sample.txt", "copies/copied.txt")),
        (New-TestCase -Name "ls-basic" -Command "ls" -Arguments @("-1")),
        (New-TestCase -Name "head-two-lines" -Command "head" -Arguments @("-n", "2", "sample.txt")),
        (New-TestCase -Name "tail-two-lines" -Command "tail" -Arguments @("-n", "2", "sample.txt")),
        (New-TestCase -Name "sort-file" -Command "sort" -Arguments @("sortable.txt")),
        (New-TestCase -Name "grep-line-number" -Command "grep" -Arguments @("-n", "alpha", "sample.txt")),
        (New-TestCase -Name "find-top-files" -Command "find" -Arguments @(".", "-maxdepth", "1", "-type", "f", "-print")),
        (New-TestCase -Name "xargs-basic" -Command "xargs" -Arguments @("-n", "1", "echo") -StdinText "alpha beta`ngamma`n")
    )
}

function Invoke-TestCase {
    param(
        [Parameter(Mandatory = $true)]
        [pscustomobject]$Case,
        [Parameter(Mandatory = $true)]
        [string]$Executable,
        [Parameter(Mandatory = $true)]
        [string]$FlavorName
    )

    $sandboxRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("coreutils-compare-" + [System.Guid]::NewGuid().ToString("N"))
    try {
        New-SandboxTree -Root $sandboxRoot
        $processResult = Invoke-NativeCommand -Executable $Executable -Arguments $Case.Arguments -WorkingDirectory $sandboxRoot -StdinText $Case.StdinText
        $snapshot = Get-TreeSnapshot -Root $sandboxRoot

        return [PSCustomObject]@{
            Flavor = $FlavorName
            Executable = $Executable
            ExitCode = $processResult.ExitCode
            Stdout = Normalize-Text -Text $processResult.Stdout
            Stderr = Normalize-Text -Text $processResult.Stderr
            Snapshot = $snapshot
        }
    }
    finally {
        if (Test-Path -LiteralPath $sandboxRoot) {
            Remove-Item -LiteralPath $sandboxRoot -Recurse -Force
        }
    }
}

function Compare-TestCase {
    param(
        [Parameter(Mandatory = $true)]
        [pscustomobject]$Case,
        [Parameter(Mandatory = $true)]
        [string]$WinuxExecutable,
        [Parameter(Mandatory = $true)]
        [string]$MicrosoftExecutable
    )

    $winuxResult = Invoke-TestCase -Case $Case -Executable $WinuxExecutable -FlavorName "winux"
    $microsoftResult = Invoke-TestCase -Case $Case -Executable $MicrosoftExecutable -FlavorName "microsoft"

    $sameStdout = $winuxResult.Stdout -ceq $microsoftResult.Stdout
    $sameStderr = $winuxResult.Stderr -ceq $microsoftResult.Stderr
    $sameExitCode = $winuxResult.ExitCode -eq $microsoftResult.ExitCode
    $sameSnapshot = (@($winuxResult.Snapshot) -join "`n") -ceq (@($microsoftResult.Snapshot) -join "`n")

    $classification = if ($sameStdout -and $sameStderr -and $sameExitCode -and $sameSnapshot) {
        "same"
    } else {
        "different"
    }

    return [PSCustomObject]@{
        Name = $Case.Name
        Command = $Case.Command
        Arguments = $Case.Arguments
        StdinText = $Case.StdinText
        Classification = $classification
        Matches = [PSCustomObject]@{
            Stdout = $sameStdout
            Stderr = $sameStderr
            ExitCode = $sameExitCode
            Snapshot = $sameSnapshot
        }
        Winux = $winuxResult
        Microsoft = $microsoftResult
    }
}

function Write-MarkdownReport {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [pscustomobject]$Report
    )

    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add("# WinuxCmd vs Microsoft Coreutils Behavior Matrix")
    $lines.Add("")
    $lines.Add("Generated from curated behavior probes over shared commands.")
    $lines.Add("")
    $lines.Add("Generated at: $($Report.GeneratedAt)")
    $lines.Add("Winux directory: $($Report.WinuxDir)")
    $lines.Add("Microsoft directory: $($Report.MicrosoftDir)")
    $lines.Add("")
    $lines.Add("## Summary")
    $lines.Add("")
    $lines.Add("| Metric | Value |")
    $lines.Add("| --- | ---: |")
    $lines.Add("| Probe count | $($Report.Summary.TotalCases) |")
    $lines.Add("| Matching probes | $($Report.Summary.SameCount) |")
    $lines.Add("| Differing probes | $($Report.Summary.DifferentCount) |")
    $lines.Add("")
    $lines.Add("## Probe Summary")
    $lines.Add("")
    $lines.Add("| Probe | Command | Result | Stdout | Stderr | Exit | Filesystem |")
    $lines.Add("| --- | --- | --- | --- | --- | --- | --- |")

    foreach ($row in $Report.Cases) {
        $lines.Add("| $($row.Name) | $($row.Command) | $($row.Classification) | $($row.Matches.Stdout) | $($row.Matches.Stderr) | $($row.Matches.ExitCode) | $($row.Matches.Snapshot) |")
    }

    $lines.Add("")
    $lines.Add("## Probe Details")
    $lines.Add("")

    foreach ($row in $Report.Cases) {
        $lines.Add("### $($row.Name)")
        $lines.Add("")
        $argumentDisplay = Join-ArgumentsDisplay -Items $row.Arguments
        $commandDisplay = if ([string]::IsNullOrEmpty($argumentDisplay)) {
            $row.Command
        } else {
            "$($row.Command) $argumentDisplay"
        }
        $lines.Add(('Command: ``{0}``' -f $commandDisplay))
        if (-not [string]::IsNullOrEmpty($row.StdinText)) {
            $stdinDisplay = $row.StdinText.Replace("`n", "\n")
            $lines.Add(('stdin: ``{0}``' -f $stdinDisplay))
        }
        $lines.Add("Result: $($row.Classification)")
        $lines.Add("Matches: stdout=$($row.Matches.Stdout), stderr=$($row.Matches.Stderr), exit=$($row.Matches.ExitCode), filesystem=$($row.Matches.Snapshot)")
        $lines.Add("")
        $lines.Add("Winux exit: $($row.Winux.ExitCode)")
        $lines.Add("Winux stdout:")
        $lines.Add('```text')
        $lines.Add($row.Winux.Stdout)
        $lines.Add('```')
        $lines.Add("Winux stderr:")
        $lines.Add('```text')
        $lines.Add($row.Winux.Stderr)
        $lines.Add('```')
        $lines.Add("Microsoft exit: $($row.Microsoft.ExitCode)")
        $lines.Add("Microsoft stdout:")
        $lines.Add('```text')
        $lines.Add($row.Microsoft.Stdout)
        $lines.Add('```')
        $lines.Add("Microsoft stderr:")
        $lines.Add('```text')
        $lines.Add($row.Microsoft.Stderr)
        $lines.Add('```')
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

    $resolvedJson = Resolve-OutputPath -Path $OutputJson -DefaultRelativePath "..\DOCS\generated\microsoft_coreutils_behavior_matrix.json"
    $resolvedMarkdown = Resolve-OutputPath -Path $OutputMarkdown -DefaultRelativePath "..\DOCS\generated\microsoft_coreutils_behavior_matrix.md"

    $cases = Get-TestCases
    $results = New-Object System.Collections.Generic.List[object]

    foreach ($case in $cases) {
        $winuxExecutable = Join-Path $resolvedWinuxDir ($case.Command + ".exe")
        $microsoftExecutable = Join-Path $resolvedMicrosoftDir ($case.Command + ".exe")

        if (-not (Test-Path -LiteralPath $winuxExecutable)) {
            throw "Winux executable not found: $winuxExecutable"
        }

        if (-not (Test-Path -LiteralPath $microsoftExecutable)) {
            throw "Microsoft executable not found: $microsoftExecutable"
        }

        $results.Add((Compare-TestCase -Case $case -WinuxExecutable $winuxExecutable -MicrosoftExecutable $microsoftExecutable))
    }

    $report = [PSCustomObject]@{
        GeneratedAt = (Get-Date).ToString("o")
        WinuxDir = $resolvedWinuxDir
        MicrosoftDir = $resolvedMicrosoftDir
        Summary = [PSCustomObject]@{
            TotalCases = $results.Count
            SameCount = @($results | Where-Object { $_.Classification -eq "same" }).Count
            DifferentCount = @($results | Where-Object { $_.Classification -eq "different" }).Count
        }
        Cases = $results.ToArray()
    }

    $jsonParent = Split-Path -Parent $resolvedJson
    if (-not (Test-Path -LiteralPath $jsonParent)) {
        New-Item -ItemType Directory -Path $jsonParent -Force | Out-Null
    }

    $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $resolvedJson -Encoding UTF8
    Write-MarkdownReport -Path $resolvedMarkdown -Report $report

    Write-Host "JSON: $resolvedJson"
    Write-Host "Markdown: $resolvedMarkdown"
}
catch {
    Write-Error ("compare-coreutils-behavior.ps1 failed at line {0}: {1}" -f $_.InvocationInfo.ScriptLineNumber, $_.Exception.Message)
    throw
}
