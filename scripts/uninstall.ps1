# Removes the per-user install, its Start menu shortcut and autostart entry. Settings are kept.
# Ask the running copy to exit cleanly (it closes the built-in touchpad it reshaped), then make sure.
Add-Type -Namespace VT -Name W -MemberDefinition '[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string c, string t); [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);'
$appWindow = [VT.W]::FindWindow('VirtualTouchpadApp', [NullString]::Value)
if ($appWindow -ne [IntPtr]::Zero) { [void][VT.W]::PostMessage($appWindow, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) } # WM_CLOSE
Get-Process VirtualTouchpad -ErrorAction SilentlyContinue | ForEach-Object {
    if (-not $_.WaitForExit(3000)) { Stop-Process -Id $_.Id -Force }
}
Remove-Item (Join-Path $env:LOCALAPPDATA 'Programs\VirtualTouchpad') -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Virtual Touchpad.lnk') -Force -ErrorAction SilentlyContinue
Remove-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name VirtualTouchpad -ErrorAction SilentlyContinue
Write-Host 'Virtual Touchpad removed.'
