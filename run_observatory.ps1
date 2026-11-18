param(
    [int]$Port = 17890,
    [switch]$NoHud
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

if (-not (Test-Path "$PSScriptRoot\bin\schrodinger_clip.exe")) {
    & "$PSScriptRoot\build.ps1"
}

$ArgsList = @("--port", "$Port", "--open-ui")
if ($NoHud) {
    $ArgsList += "--no-hud"
}

Write-Host "Launching SchrodingerClip Quantum Guardian & Observatory on http://127.0.0.1:$Port ..." -ForegroundColor Cyan
& "$PSScriptRoot\bin\schrodinger_clip.exe" @ArgsList
