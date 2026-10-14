# Copies the current build to %LOCALAPPDATA%\Programs\VirtualTouchpad and (re)starts it.
# No admin rights needed. Exits non-zero on failure.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$built = Join-Path $root 'build\Release\VirtualTouchpad.exe'
$installDir = Join-Path $env:LOCALAPPDATA 'Programs\VirtualTouchpad'
$target = Join-Path $installDir 'VirtualTouchpad.exe'

if (-not (Test-Path $built)) { throw "No build at $built. Run scripts\build.ps1 first." }

# Ask the running copy to exit cleanly (it closes the built-in touchpad it reshaped), then make sure.
Add-Type -Namespace VT -Name W -MemberDefinition '[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string c, string t); [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);'
$appWindow = [VT.W]::FindWindow('VirtualTouchpadApp', [NullString]::Value)
if ($appWindow -ne [IntPtr]::Zero) { [void][VT.W]::PostMessage($appWindow, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) } # WM_CLOSE
Get-Process VirtualTouchpad -ErrorAction SilentlyContinue | ForEach-Object {
    if (-not $_.WaitForExit(3000)) { Stop-Process -Id $_.Id -Force }
}

New-Item -ItemType Directory -Force $installDir | Out-Null
Copy-Item $built $target -Force
(Get-FileHash $built -Algorithm SHA256).Hash | Set-Content (Join-Path $installDir 'source.sha256') -Encoding ascii

Start-Process $target
Write-Host "Deployed and started $target"
