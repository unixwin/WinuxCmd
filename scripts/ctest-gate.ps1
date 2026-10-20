<#
.SYNOPSIS
Run ctest in parallel, then re-run only the failed tests serially once.

.DESCRIPTION
Parallel ctest runs on Windows intermittently fail with 0xc0000409
(fail-fast) or environment-sensitive assertions under load while the same
tests pass serially (issue #1260). This wrapper runs the suite in
parallel; on failure it re-runs only the failed tests (ctest
--rerun-failed) serially. If the serial pass is green the gate succeeds
and the recovered tests are annotated; a test that also fails serially
still fails the gate, so real regressions stay red.
#>

[CmdletBinding()]
param(
    [string]$BuildDir = ".",
    [string]$Configuration = "Release",
    [int]$Jobs = 0,
    [int]$TimeoutSeconds = 120,
    [int]$SerialRetries = 1,
    [string]$ExtraArgs
)

$ErrorActionPreference = "Stop"

function Write-Gate {
    param([string]$Message)
    Write-Host "[ctest-gate] $Message"
}

function Write-GateAnnotation {
    param([string]$Level, [string]$Message)
    if ($env:GITHUB_ACTIONS -eq "true") {
        Write-Host "::$Level::$Message"
    } else {
        Write-Gate $Message
    }
}

if ($Jobs -le 0) {
    if ($env:CTEST_PARALLEL_LEVEL) {
        $Jobs = [int]$env:CTEST_PARALLEL_LEVEL
    } elseif ($env:NUMBER_OF_PROCESSORS) {
        $Jobs = [int]$env:NUMBER_OF_PROCESSORS
    } else {
        $Jobs = [Environment]::ProcessorCount
    }
}
if ($Jobs -lt 1) { $Jobs = 1 }

$ctestArgs = @(
    "--output-on-failure",
    "--no-tests=error",
    "--timeout", "$TimeoutSeconds",
    "-C", $Configuration
)
if (-not [string]::IsNullOrWhiteSpace($ExtraArgs)) {
    $ctestArgs += ($ExtraArgs -split '\s+')
}

Push-Location $BuildDir
try {
    Write-Gate "parallel run: ctest $($ctestArgs -join ' ') -j $Jobs"
    & ctest @ctestArgs -j $Jobs
    $code = $LASTEXITCODE
    if ($code -eq 0) {
        Write-Gate "all tests passed"
        exit 0
    }

    # LastTestsFailed.log lists one "<index>:<test-name>" per failed test.
    $failedLog = Join-Path "Testing" "Temporary\LastTestsFailed.log"
    $failedTests = @()
    if (Test-Path $failedLog) {
        $failedTests = @(Get-Content $failedLog |
            ForEach-Object { $_ -replace '^\d+:', '' } |
            Where-Object { $_ })
    }

    Write-Gate "parallel run failed (exit $code); $($failedTests.Count) test(s) failed:"
    foreach ($t in $failedTests) { Write-Host "  $t" }

    if ($failedTests.Count -eq 0) {
        Write-Gate "no failed-test list recorded; cannot isolate a serial retry"
        exit $code
    }

    for ($attempt = 1; $attempt -le $SerialRetries; $attempt++) {
        Write-Gate "serial retry $attempt/$SerialRetries of $($failedTests.Count) failed test(s)"
        & ctest @ctestArgs --rerun-failed -j 1
        if ($LASTEXITCODE -eq 0) {
            Write-GateAnnotation "warning" `
                "$($failedTests.Count) test(s) recovered on serial retry (parallel-run flake, #1260): $($failedTests -join ', ')"
            exit 0
        }
    }

    Write-GateAnnotation "error" `
        "serial retry still failing after $SerialRetries attempt(s); treating as real regression"
    exit 1
} finally {
    Pop-Location
}
