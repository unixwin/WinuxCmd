#!/usr/bin/env pwsh

# WinuxCmd cache refresh script for CI/CD
# Used to trigger server cache updates after releases
# Fails silently to not break the CI/CD pipeline

param(
    [string]$ServerUrl = "https://dl.caomengxuan666.com",
    [string]$CacheRefreshToken
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Continue"  # Changed to Continue to prevent pipeline failure

# Detect CI/CD environment
$isCI = $env:CI -eq "true" -or $env:GITHUB_ACTIONS -eq "true"

if ($isCI) {
    Write-Host "::group::🔄 Refreshing WinuxCmd Server Cache"
}

Write-Host "Target Server: $ServerUrl" -ForegroundColor Cyan

# Check required parameters
if (-not $CacheRefreshToken -and -not $env:CACHE_REFRESH_TOKEN) {
    $warningMsg = "⚠️ Warning: CACHE_REFRESH_TOKEN not provided, skipping cache refresh"
    if ($isCI) {
        Write-Host "::warning::$warningMsg"
        # Set output for skipped state
        if ($env:GITHUB_OUTPUT) {
            "cache-refresh-result=skipped" >> $env:GITHUB_OUTPUT
            "cache-message=Token not provided" >> $env:GITHUB_OUTPUT
        }
    }
    Write-Host $warningMsg -ForegroundColor Yellow
    if ($isCI) {
        Write-Host "::endgroup::"
    }
    exit 0  # Exit with success to not break pipeline
}

# Use token from parameter if provided, otherwise from environment variable
$token = if ($CacheRefreshToken) { $CacheRefreshToken } else { $env:CACHE_REFRESH_TOKEN }

# Execute cache refresh request
$apiEndpoint = "${ServerUrl}/api/cache/refresh"
Write-Host "📡 Sending request to ${apiEndpoint}..." -ForegroundColor Yellow

try {
    $headers = @{
        "Authorization" = "Bearer $token"
        "Content-Type" = "application/json"
    }

    $response = Invoke-RestMethod -Uri $apiEndpoint `
        -Method Post `
        -Headers $headers `
        -ErrorAction Stop

    $successMsg = "✅ Cache refresh successful! $($response.message)"
    if ($isCI) {
        Write-Host "::notice::$successMsg"
        # Set output variables for notification
        if ($env:GITHUB_OUTPUT) {
            "cache-refresh-result=success" >> $env:GITHUB_OUTPUT
            "cache-message=$($response.message)" >> $env:GITHUB_OUTPUT
            if ($response.timestamp) {
                "cache-timestamp=$($response.timestamp)" >> $env:GITHUB_OUTPUT
            }
        }
    }
    Write-Host $successMsg -ForegroundColor Green

    exit 0
}
catch {
    # Always log the error but don't fail the pipeline
    $statusCode = if ($_.Exception.Response) { $_.Exception.Response.StatusCode.value__ } else { "Unknown" }
    $errorMsg = "⚠️ Cache refresh failed (HTTP $statusCode): $($_.Exception.Message)"

    if ($isCI) {
        Write-Host "::warning::$errorMsg"
        # Set output variables for notification even on failure
        if ($env:GITHUB_OUTPUT) {
            "cache-refresh-result=failure" >> $env:GITHUB_OUTPUT
            "cache-error=$errorMsg" >> $env:GITHUB_OUTPUT
        }
    }
    Write-Host $errorMsg -ForegroundColor Yellow

    # Try to get response body for debugging
    try {
        if ($_.Exception.Response -and $_.Exception.Response.GetResponseStream()) {
            $reader = New-Object System.IO.StreamReader($_.Exception.Response.GetResponseStream())
            $reader.BaseStream.Position = 0
            $responseBody = $reader.ReadToEnd()
            if ($responseBody -and $responseBody.Trim()) {
                Write-Host "Response: $responseBody" -ForegroundColor Gray
                if ($isCI -and $env:GITHUB_OUTPUT) {
                    "cache-response=$responseBody" >> $env:GITHUB_OUTPUT
                }
            }
            $reader.Close()
        }
    }
    catch {
        # Ignore if we can't read the response
    }

    # Always exit with success to not break the pipeline
    exit 0
}
finally {
    if ($isCI) {
        Write-Host "::endgroup::"
    }
}