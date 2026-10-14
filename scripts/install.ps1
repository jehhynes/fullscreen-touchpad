<#
Installs Virtual Touchpad so Windows will grant it UIAccess, which it needs to take over touch input.

UIAccess is only honoured for executables that are
  1. signed with a certificate the machine trusts, and
  2. installed in a secure location (Program Files).

This script:
  - builds the app if needed,
  - creates a fresh self-signed code-signing certificate, signs the exe with it,
    trusts the *public* certificate machine-wide, then deletes the private key so
    nothing else can ever be signed with it,
  - copies the exe to "C:\Program Files\VirtualTouchpad",
  - adds a Start menu shortcut and (optionally) starts it with Windows.

Run from an elevated PowerShell:  .\scripts\install.ps1 [-StartWithWindows]
#>
param([switch] $StartWithWindows)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$subject = 'CN=VirtualTouchpad Local Code Signing'
$installDir = Join-Path $env:ProgramFiles 'VirtualTouchpad'
$exeName = 'VirtualTouchpad.exe'
$target = Join-Path $installDir $exeName

$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this script from an elevated (Administrator) PowerShell.'
}

# 1. Build
$built = Join-Path $root "build\Release\$exeName"
if (-not (Test-Path $built)) { & (Join-Path $PSScriptRoot 'build.ps1') }

# 2. Stop a running copy so the exe can be replaced
Get-Process VirtualTouchpad -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 300

# 3. Copy to Program Files
New-Item -ItemType Directory -Force $installDir | Out-Null
Copy-Item $built $target -Force

# 4. Sign with a single-use certificate
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

$sig = Set-AuthenticodeSignature -FilePath $target -Certificate $cert -HashAlgorithm SHA256
Remove-Item "Cert:\LocalMachine\My\$($cert.Thumbprint)" -DeleteKey
if ($sig.Status -ne 'Valid') { throw "Signing failed: $($sig.Status) $($sig.StatusMessage)" }

# 5. Shortcuts
$shell = New-Object -ComObject WScript.Shell
$lnk = $shell.CreateShortcut((Join-Path $env:ProgramData 'Microsoft\Windows\Start Menu\Programs\Virtual Touchpad.lnk'))
$lnk.TargetPath = $target
$lnk.Description = 'Full-screen invisible touchpad. Launch again to toggle.'
$lnk.Save()

if ($StartWithWindows) {
    $run = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
    Set-ItemProperty $run -Name VirtualTouchpad -Value "`"$target`""
}

# 6. Launch un-elevated (via Explorer) so it runs as the normal user
Start-Process explorer.exe -ArgumentList "`"$target`""
Write-Host "Installed to $target and started. Look for the touchpad icon in the system tray."
