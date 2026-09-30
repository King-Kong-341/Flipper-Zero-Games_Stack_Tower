# Builds Stack Tower with ufbt and copies the .fap to the repository root.
# Usage (PowerShell):  .\scripts\build.ps1
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")
python -m ufbt
if ($LASTEXITCODE -ne 0) { throw "Build failed" }
Copy-Item "dist\stack_tower.fap" "stack_tower.fap" -Force
Write-Host "OK - stack_tower.fap is ready (copy it to SD Card/apps/Games/)."
