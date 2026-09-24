#!/usr/bin/env pwsh
Write-Host "=== Post-commit hook started ===" -ForegroundColor Cyan

$msg = git log -1 --pretty=%B
Write-Host "Commit message: $msg" -ForegroundColor Yellow

if ($msg -match "^release: v?(\d+\.\d+\.\d+)") {
    $ver = $matches[1]
    Write-Host "Release detected! Version: $ver" -ForegroundColor Green
    
    git tag "v$ver"
    Write-Host "Created tag v$ver" -ForegroundColor Green
    
    git push origin "v$ver"
    Write-Host "Pushed tag v$ver" -ForegroundColor Green
} else {
    Write-Host "Not a release commit" -ForegroundColor Gray
}

Write-Host "=== Post-commit hook completed ===" -ForegroundColor Cyan