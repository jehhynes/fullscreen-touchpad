# Reports whether the installed app is the current build and is running. Exits 0 only if both are true.
$root = Split-Path -Parent $PSScriptRoot
$installDir = Join-Path $env:LOCALAPPDATA 'Programs\VirtualTouchpad'
$target = Join-Path $installDir 'VirtualTouchpad.exe'
$built = Join-Path $root 'build\Release\VirtualTouchpad.exe'
$ok = $true

if (-not (Test-Path $target)) { Write-Host 'NOT INSTALLED'; exit 1 }

$recorded = Get-Content (Join-Path $installDir 'source.sha256') -ErrorAction SilentlyContinue
if (-not (Test-Path $built)) { Write-Host "No build at $built"; exit 1 }
if ($recorded -eq (Get-FileHash $built -Algorithm SHA256).Hash) { Write-Host 'Installed build: current' }
else { Write-Host 'Installed build: OUT OF DATE (run scripts\deploy.ps1)'; $ok = $false }

$proc = Get-Process VirtualTouchpad -ErrorAction SilentlyContinue | Where-Object Path -eq $target | Select-Object -First 1
if ($proc) { Write-Host "Running: yes (pid $($proc.Id), started $($proc.StartTime))" }
else { Write-Host 'Running: NO'; $ok = $false }

$other = Get-Process VirtualTouchpad -ErrorAction SilentlyContinue | Where-Object { $_.Path -ne $target }
if ($other) { Write-Host "WARNING: another copy is running: $($other.Path -join ', ')"; $ok = $false }

if ($ok) { exit 0 } else { exit 1 }
