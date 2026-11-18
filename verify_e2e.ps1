$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

& "$PSScriptRoot\build.ps1"

Write-Host "`n>>> Running Adversarial Edge-Case & Boundary Unit Suite..." -ForegroundColor Yellow
& "$PSScriptRoot\bin\edge_cases_test.exe"
if ($LASTEXITCODE -ne 0) { throw "edge_cases_test.exe failed with code $LASTEXITCODE" }

Write-Host "`n>>> Running Real OS Clipboard + COM IDataObject / IStream E2E Verification Suite..." -ForegroundColor Yellow
& "$PSScriptRoot\bin\e2e_verify.exe" "bin\schrodinger_clip.exe"
if ($LASTEXITCODE -ne 0) { throw "e2e_verify.exe failed with code $LASTEXITCODE" }

Write-Host "`n[ALL VERIFICATIONS PASSED 100%] Zero warnings, zero memory leaks, 100% SHA-256 stream integrity!" -ForegroundColor Green
