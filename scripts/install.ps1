# Installs Virtual Touchpad for the current user: builds if needed, deploys to
# %LOCALAPPDATA%\Programs\VirtualTouchpad, adds a Start menu shortcut and optionally starts it with Windows.
# No admin rights needed.   .\scripts\install.ps1 [-StartWithWindows]
param([switch] $StartWithWindows)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$target = Join-Path $env:LOCALAPPDATA 'Programs\VirtualTouchpad\VirtualTouchpad.exe'

if (-not (Test-Path (Join-Path $root 'build\Release\VirtualTouchpad.exe'))) { & (Join-Path $PSScriptRoot 'build.ps1') }
& (Join-Path $PSScriptRoot 'deploy.ps1')

$shell = New-Object -ComObject WScript.Shell
$lnk = $shell.CreateShortcut((Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Virtual Touchpad.lnk'))
$lnk.TargetPath = $target
$lnk.Description = 'Invisible full-screen touchpad. Launch again to toggle.'
$lnk.Save()

if ($StartWithWindows) {
    Set-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name VirtualTouchpad -Value "`"$target`""
}
Write-Host "Installed. Four-finger tap (or Ctrl+Alt+T, or the tray icon) toggles the touchpad."
