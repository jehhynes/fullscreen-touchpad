# Virtual Touchpad

Turns your whole touchscreen into an invisible touchpad, like the Windows virtual touchpad but full
screen and with nothing drawn over your content. While it's on, touching the screen moves the mouse
pointer relatively instead of tapping what's under your finger. A real mouse and pen keep working
normally.

## How it works

`RegisterPointerInputTarget` redirects **every** touchscreen contact to a hidden 1×1 window, no
matter where on screen it lands, so no overlay covers the screen. The gesture engine turns contacts
into mouse/keyboard events with `SendInput`. When the touchpad is off, the app switches to
*observe* mode (`RegisterPointerInputTargetEx(..., fObserve = TRUE)`) so apps receive touch
normally while it watches for the toggle gesture.

Redirecting touch requires the **UIAccess** privilege. Windows only grants it to signed executables
installed in Program Files, which is why the app needs an install step.

## Gestures

| Gesture | Action |
| --- | --- |
| **4-finger tap** | **Toggle the touchpad on/off** (works in both states) |
| 1-finger move | Move pointer (with acceleration) |
| Tap | Left click (after a short wait for a possible second tap) |
| Double tap | Double-click |
| Tap, then touch and hold or move | Drag (left button held until you lift) |
| Touch and hold still | Right click |
| 2-finger tap | Right click |
| 2-finger drag | Scroll (with kinetic coasting) |
| 2-finger pinch | Zoom (Ctrl+wheel) |
| 3-finger tap | Middle click |
| 3-finger swipe left/right | Switch apps (Alt+Tab, keep swiping to step) |
| 3/4-finger swipe up | Task View |
| 3/4-finger swipe down | Show desktop |
| 4-finger swipe left/right | Switch virtual desktop |

Tapping into a text field opens the touch keyboard. While it's open, touch goes straight to the
screen so you can type on it, and the touchpad comes back when the keyboard closes.

You can also toggle with **Ctrl+Alt+T**, by clicking the tray icon, or by launching the app again
(e.g. from a pinned taskbar shortcut). While the touchpad is on, a thin blue glow runs along the
screen edges. You can turn it off from the tray menu.

## Build

Requirements: Visual Studio 2026 Build Tools with the **Desktop development with C++** workload
(includes MSVC, the Windows SDK and CMake).

```powershell
winget install --id Microsoft.VisualStudio.BuildTools --override "--passive --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
.\scripts\build.ps1
```

Or use the CMake presets directly: `cmake --preset vs` then `cmake --build --preset release`.

## Tests

The gesture engine talks to the outside world only through `TouchpadHost`, so the unit tests in
`tests/` drive it with simulated touches and a fake clock and check the exact clicks, drags,
scrolls and keys it produces. Run them with `ctest --preset release` (GoogleTest is fetched
automatically) or run `build\Release\touchpad_tests.exe`.

## Install

Double-click `scripts\install.cmd`, or run `scripts\install.cmd -StartWithWindows` to also start
it at sign-in. It asks for admin rights and runs `install.ps1` with the execution policy bypassed
for that run only, so you don't need to change your system policy. From an elevated PowerShell you
can also run `.\scripts\install.ps1 -StartWithWindows` directly.

This one-time setup creates a local code-signing certificate and trusts it machine-wide. Its
private key stays in the machine store, non-exportable and usable only by administrators. Setup
then signs the app, installs it to `C:\Program Files\VirtualTouchpad`, adds a Start menu shortcut,
and starts it.

After rebuilding, run `scripts\update.cmd` to sign and deploy the new build with the existing
certificate. It doesn't change the trust stores. `scripts\check-install.ps1` reports whether the
installed copy is the current build, validly signed, and running. `scripts\uninstall.cmd` removes
everything, including the certificate.

## Settings

Tray icon → *Edit settings…* opens `%APPDATA%\VirtualTouchpad\settings.ini`; choose *Reload
settings* after saving. Tunables include pointer `Sensitivity`/`Acceleration`, `ScrollSpeed`,
`NaturalScrolling`, tap timings, and swipe thresholds. Distances are in pixels at 100% scaling.
