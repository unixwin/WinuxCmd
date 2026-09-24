#!/usr/bin/env pwsh
Write-Host "=== Pre-commit hook started ===" -ForegroundColor Cyan

$ver = (Get-Content PROJECT_VERSION -ErrorAction Stop).Trim()
Write-Host "Version from PROJECT_VERSION: $ver" -ForegroundColor Yellow

$files = @(
    "scripts\create_links.ps1",
    "scripts\winuxcmd_init.cmd"
)

foreach ($f in $files) {
    Write-Host "Checking $f..." -ForegroundColor Cyan
    
    if (-not (Test-Path $f)) {
        Write-Host "ERROR: $f not found!" -ForegroundColor Red
        continue
    }
    
    $c = Get-Content $f -Raw
    Write-Host "Original content length: $($c.Length)" -ForegroundColor Gray
    
    if ($c -match '\d+\.\d+\.\d+') {
        Write-Host "Found version: $($matches[0])" -ForegroundColor Yellow
    } else {
        Write-Host "WARNING: No version pattern found in $f" -ForegroundColor Red
    }
    
    $newc = $c -replace '\d+\.\d+\.\d+', $ver
    Set-Content $f -Value $newc -Encoding UTF8 -NoNewline
    Write-Host "Updated $f to version $ver" -ForegroundColor Green
    
    git add $f
    Write-Host "Added $f to git" -ForegroundColor Gray
}

Write-Host "=== Pre-commit hook completed ===" -ForegroundColor Cyan