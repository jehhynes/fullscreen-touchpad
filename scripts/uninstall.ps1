# Removes Virtual Touchpad, its shortcuts, autostart entry and trusted certificate.
# Run from an elevated PowerShell. Settings in %APPDATA%\VirtualTouchpad are kept.
$ErrorActionPreference = 'Stop'
$subject = 'CN=VirtualTouchpad Local Code Signing'

$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this script from an elevated (Administrator) PowerShell.'
}

Get-Process VirtualTouchpad -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 300

Remove-Item (Join-Path $env:ProgramFiles 'VirtualTouchpad') -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $env:ProgramData 'Microsoft\Windows\Start Menu\Programs\Virtual Touchpad.lnk') -Force -ErrorAction SilentlyContinue
Remove-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name VirtualTouchpad -ErrorAction SilentlyContinue

foreach ($store in 'Root', 'TrustedPublisher', 'My') {
    Get-ChildItem "Cert:\LocalMachine\$store" | Where-Object Subject -eq $subject | Remove-Item -Force
}
Write-Host 'Virtual Touchpad removed.'
