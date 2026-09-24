#!/usr/bin/env pwsh
Write-Host "Installing git hooks..." -ForegroundColor Cyan

$hooks = @("pre-commit", "post-commit")
$root = (Get-Location).Path

foreach ($h in $hooks) {
    $hookPath = ".git/hooks/$h"
    $content = "#!/bin/sh`npwsh -File `"$root/scripts/hooks/$h.ps1`""
    Set-Content $hookPath -Value $content -Encoding ASCII
    Write-Host "Installed $h hook" -ForegroundColor Green
}

Write-Host "Done!" -ForegroundColor Green