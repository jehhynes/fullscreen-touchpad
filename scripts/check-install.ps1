# Reports whether the installed app is the current build, correctly signed, and running.
# Exits 0 only if all three are true.
$root = Split-Path -Parent $PSScriptRoot
$installDir = Join-Path $env:ProgramFiles 'VirtualTouchpad'
$target = Join-Path $installDir 'VirtualTouchpad.exe'
$built = Join-Path $root 'build\Release\VirtualTouchpad.exe'
$ok = $true

if (-not (Test-Path $target)) { Write-Host 'NOT INSTALLED'; exit 1 }

$recorded = Get-Content (Join-Path $installDir 'source.sha256') -ErrorAction SilentlyContinue
$current = (Get-FileHash $built -Algorithm SHA256).Hash
if ($recorded -eq $current) { Write-Host 'Installed build: current' }
else { Write-Host 'Installed build: OUT OF DATE (run scripts\update.cmd)'; $ok = $false }

$sig = Get-AuthenticodeSignature $target
Write-Host "Signature: $($sig.Status)"
if ($sig.Status -ne 'Valid') { $ok = $false }

$proc = Get-Process VirtualTouchpad -ErrorAction SilentlyContinue | Where-Object Path -eq $target
if ($proc) { Write-Host "Running: yes (pid $($proc.Id), started $($proc.StartTime))" }
else { Write-Host 'Running: NO'; $ok = $false }

if ($ok) { exit 0 } else { exit 1 }
