<#
One-time setup for Virtual Touchpad, then a first deploy.

Windows only grants UIAccess (needed to take over touch input) to executables that are signed with a
certificate the machine trusts and installed in Program Files. This script:
  - creates a local code-signing certificate (if there isn't one already) and trusts it machine-wide,
    keeping its private key non-exportable in the machine store so later updates can be signed
    (scripts\update.cmd) without touching the trust stores again,
  - builds the app if needed and deploys it (deploy.ps1),
  - adds a Start menu shortcut and (optionally) starts it with Windows.

Run from an elevated PowerShell (or double-click install.cmd):  .\scripts\install.ps1 [-StartWithWindows]
#>
param([switch] $StartWithWindows)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$subject = 'CN=VirtualTouchpad Local Code Signing'
$target = Join-Path $env:ProgramFiles 'VirtualTouchpad\VirtualTouchpad.exe'

$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this script from an elevated (Administrator) PowerShell.'
}

# 1. Signing certificate: reuse a trusted one with its key, otherwise replace any old ones.
$cert = Get-ChildItem Cert:\LocalMachine\My -CodeSigningCert | Where-Object { $_.Subject -eq $subject -and $_.HasPrivateKey } |
    Where-Object { Get-ChildItem Cert:\LocalMachine\Root | Where-Object Thumbprint -eq $_.Thumbprint } |
    Select-Object -First 1
if (-not $cert) {
    foreach ($store in 'Root', 'TrustedPublisher', 'My') {
        Get-ChildItem "Cert:\LocalMachine\$store" | Where-Object Subject -eq $subject | Remove-Item -Force
    }
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $subject `
        -CertStoreLocation Cert:\LocalMachine\My -KeyExportPolicy NonExportable `
        -KeyUsage DigitalSignature -NotAfter (Get-Date).AddYears(10)

    $cer = Join-Path $env:TEMP 'VirtualTouchpad.cer'
    Export-Certificate -Cert $cert -FilePath $cer | Out-Null
    Import-Certificate -FilePath $cer -CertStoreLocation Cert:\LocalMachine\Root | Out-Null
    Import-Certificate -FilePath $cer -CertStoreLocation Cert:\LocalMachine\TrustedPublisher | Out-Null
    Remove-Item $cer
}

# 2. Build if needed, then deploy
if (-not (Test-Path (Join-Path $root 'build\Release\VirtualTouchpad.exe'))) { & (Join-Path $PSScriptRoot 'build.ps1') }
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'deploy.ps1')
if ($LASTEXITCODE) { throw 'Deploy failed; see build\deploy.log' }

# 3. Shortcuts
$shell = New-Object -ComObject WScript.Shell
$lnk = $shell.CreateShortcut((Join-Path $env:ProgramData 'Microsoft\Windows\Start Menu\Programs\Virtual Touchpad.lnk'))
$lnk.TargetPath = $target
$lnk.Description = 'Full-screen invisible touchpad. Launch again to toggle.'
$lnk.Save()

if ($StartWithWindows) {
    Set-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name VirtualTouchpad -Value "`"$target`""
}

Write-Host "Installed to $target. Look for the touchpad icon in the system tray."
