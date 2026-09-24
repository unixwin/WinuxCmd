#!/usr/bin/env pwsh

# Notification script for cache refresh results
# Sends notification and always succeeds

param(
    [string]$Result,
    [string]$Message,
    [string]$Error,
    [string]$Version,
    [string]$TagName
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Continue"

$isCI = $env:CI -eq "true" -or $env:GITHUB_ACTIONS -eq "true"

if ($isCI) {
    Write-Host "::group::📢 Cache Refresh Notification"
}

Write-Host "📊 Cache Refresh Result Summary" -ForegroundColor Cyan
Write-Host "=============================="

if ($Result -eq "success") {
    Write-Host "✅ Status: SUCCESS" -ForegroundColor Green
    if ($Message) {
        Write-Host "📝 Message: $Message" -ForegroundColor White
    }
} elseif ($Result -eq "failure") {
    Write-Host "⚠️ Status: FAILED (non-blocking)" -ForegroundColor Yellow
    if ($Error) {
        Write-Host "❌ Error: $Error" -ForegroundColor Red
    }
} else {
    Write-Host "⏭️ Status: SKIPPED" -ForegroundColor Gray
    Write-Host "No cache refresh token provided" -ForegroundColor Gray
}

if ($Version) {
    Write-Host "🏷️ Version: $Version" -ForegroundColor Cyan
}
if ($TagName) {
    Write-Host "🔖 Tag: $TagName" -ForegroundColor Cyan
}

# Here you can add actual notification logic
# For example, send to Slack, Discord, email, etc.
Write-Host ""
Write-Host "💡 Notification endpoints can be added here:" -ForegroundColor Magenta
Write-Host "  - Slack webhook" -ForegroundColor Gray
Write-Host "  - Discord webhook" -ForegroundColor Gray
Write-Host "  - Email notification" -ForegroundColor Gray
Write-Host "  - Custom webhook" -ForegroundColor Gray

# Example: If you want to send to a webhook
# $webhookBody = @{
#     version = $Version
#     tag = $TagName
#     cache_result = $Result
#     message = $Message
#     error = $Error
#     timestamp = Get-Date -Format "yyyy-MM-ddTHH:mm:ssZ"
# } | ConvertTo-Json

# try {
#     Invoke-RestMethod -Uri "https://your-webhook-url" -Method Post -Body $webhookBody -ContentType "application/json"
#     Write-Host "📤 Notification sent successfully" -ForegroundColor Green
# } catch {
#     Write-Host "⚠️ Failed to send notification (non-blocking): $($_.Exception.Message)" -ForegroundColor Yellow
# }

if ($isCI) {
    Write-Host "::endgroup::"
}

# Always exit successfully
exit 0