# Builds build\Release\VirtualTouchpad.exe. Works from any PowerShell; no developer prompt needed.
param([ValidateSet('Release', 'Debug')] [string] $Configuration = 'Release')

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    if (-not (Test-Path $cmake)) { throw 'CMake not found. Install Visual Studio Build Tools with the C++ workload.' }
}

Push-Location $root
try {
    & $cmake --preset vs
    if ($LASTEXITCODE) { throw "CMake configure failed ($LASTEXITCODE)" }
    & $cmake --build --preset ($Configuration.ToLower())
    if ($LASTEXITCODE) { throw "Build failed ($LASTEXITCODE)" }
    Write-Host "Built $root\build\$Configuration\VirtualTouchpad.exe"
}
finally {
    Pop-Location
}
