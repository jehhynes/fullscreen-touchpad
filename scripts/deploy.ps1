<#
Signs the current build with the existing local signing certificate, copies it to Program Files and
restarts it. Does not touch the certificate trust stores: run install.ps1 once first to set those up.

Run elevated (scripts\update.cmd does that). Writes a log to build\deploy.log and exits non-zero on failure.
#>
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$log = Join-Path $root 'build\deploy.log'
Start-Transcript -Path $log -Force | Out-Null

try {
    $subject = 'CN=VirtualTouchpad Local Code Signing'
    $installDir = Join-Path $env:ProgramFiles 'VirtualTouchpad'
    $target = Join-Path $installDir 'VirtualTouchpad.exe'
    $built = Join-Path $root 'build\Release\VirtualTouchpad.exe'

    $principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Must run elevated.' }
    if (-not (Test-Path $built)) { throw "No build at $built. Run scripts\build.ps1 first." }

    $cert = Get-ChildItem Cert:\LocalMachine\My -CodeSigningCert | Where-Object { $_.Subject -eq $subject -and $_.HasPrivateKey } |
        Sort-Object NotAfter -Descending | Select-Object -First 1
    if (-not $cert) { throw 'No signing certificate found. Run scripts\install.cmd once to set it up.' }
    if (-not (Get-ChildItem Cert:\LocalMachine\Root | Where-Object Thumbprint -eq $cert.Thumbprint)) {
        throw 'The signing certificate is not trusted. Run scripts\install.cmd once to set it up.'
    }

    Get-Process VirtualTouchpad -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 500

    New-Item -ItemType Directory -Force $installDir | Out-Null
    Copy-Item $built $target -Force
    $sig = Set-AuthenticodeSignature -FilePath $target -Certificate $cert -HashAlgorithm SHA256
    if ($sig.Status -ne 'Valid') { throw "Signing failed: $($sig.Status) $($sig.StatusMessage)" }

    # Record which build is installed so scripts\check-install.ps1 can compare (signing changes the file's hash).
    (Get-FileHash $built -Algorithm SHA256).Hash | Set-Content (Join-Path $installDir 'source.sha256') -Encoding ascii

    # Start un-elevated, as the signed-in user.
    Start-Process explorer.exe -ArgumentList "`"$target`""
    Write-Host "Deployed and started $target"
}
catch {
    Write-Host "DEPLOY FAILED: $_"
    Stop-Transcript | Out-Null
    exit 1
}
Stop-Transcript | Out-Null
exit 0
