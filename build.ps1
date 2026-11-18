$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

if (-not (Get-Command g++ -ErrorAction SilentlyContinue)) {
    $CandidatePaths = @("C:\msys64\ucrt64\bin", "C:\msys64\mingw64\bin", "C:\mingw64\bin")
    foreach ($p in $CandidatePaths) {
        if (Test-Path "$p\g++.exe") {
            $env:PATH = "$p;$env:PATH"
            break
        }
    }
}
if (-not (Get-Command g++ -ErrorAction SilentlyContinue)) {
    Write-Error "g++ (MinGW-w64 x86_64 C++17) not found in PATH."
    exit 1
}

New-Item -ItemType Directory -Force "$PSScriptRoot\bin" | Out-Null

$CommonFlags = @("-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror")
$WinLibs = @(
    "-lws2_32", "-lole32", "-loleaut32", "-luuid", "-lwinhttp",
    "-lgdiplus", "-lgdi32", "-luser32", "-lshell32", "-lshlwapi",
    "-lbcrypt", "-lpsapi", "-lrpcrt4"
)

Write-Host "[1/3] Compiling schrodinger_clip.exe (Native Win32/COM STA Daemon + Observatory)..." -ForegroundColor Cyan
& g++ @CommonFlags "src\main.cpp" -o "bin\schrodinger_clip.exe" @WinLibs
if ($LASTEXITCODE -ne 0) { throw "Failed to build schrodinger_clip.exe" }

Write-Host "[2/3] Compiling e2e_verify.exe (Real OS Clipboard + COM IStream E2E Suite)..." -ForegroundColor Cyan
& g++ @CommonFlags "tests\e2e_verify.cpp" -o "bin\e2e_verify.exe" @WinLibs
if ($LASTEXITCODE -ne 0) { throw "Failed to build e2e_verify.exe" }

Write-Host "[3/3] Compiling edge_cases_test.exe (Adversarial Boundary & Error-Path Suite)..." -ForegroundColor Cyan
& g++ @CommonFlags "tests\edge_cases_test.cpp" -o "bin\edge_cases_test.exe" @WinLibs
if ($LASTEXITCODE -ne 0) { throw "Failed to build edge_cases_test.exe" }

Write-Host "`n[SUCCESS] All binaries built cleanly with 0 warnings (-Wall -Wextra -Werror)!" -ForegroundColor Green
