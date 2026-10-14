# Virtual Touchpad

Invisible full-screen touchpad for Windows touchscreens. C++20 / Win32, CMake (Visual Studio 2026
generator), GoogleTest. It reshapes Windows' built-in virtual touchpad window rather than
handling touch itself; see README.md.

## Commands

- Build: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build.ps1`
- Test: `build\Release\touchpad_tests.exe`
- Deploy the current build: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\deploy.ps1`
  (per-user, no admin; exits the running copy cleanly, copies, restarts)
- Check what's installed: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\check-install.ps1`
  (exit 0 = installed build is current and running)

## Definition of done

**A change is not complete until it is installed.** Before reporting any code change as done:

1. Build with no warnings.
2. Run the unit tests; all must pass.
3. Deploy with `scripts\deploy.ps1`.
4. Run `scripts\check-install.ps1` and confirm it reports current and running.

Only then report completion, saying what was verified and what still needs the user's eyes or fingers.

## Lessons learned (don't repeat)

- Capturing touch ourselves (RegisterPointerInputTarget, a full-screen overlay, SendInput or synthetic
  touchpad injection) always made the cursor flicker and jump to the finger. Windows' built-in touchpad
  doesn't, which is why this app reuses it.
- Screenshots (BitBlt) show neither the cursor nor this layered touchpad window, so they can't confirm
  what the user sees. Don't claim visual results from them.
- Never leave the user without a way out: they may have no keyboard. Every mode must be switchable off
  by touch (the taskbar stays uncovered, and the four-finger tap toggles).
- No UI Automation calls on the UI thread (they deadlock); `TaskbarButton` runs them on its own thread.
- The built-in touchpad closes by being cloaked, not hidden: "open" means visible and not cloaked.
- PowerShell traps when testing: `$null` passed to a string parameter becomes `""` (use `[NullString]::Value`),
  and `if ([IntPtr]::Zero)` is true (compare with `-ne [IntPtr]::Zero`).
- `LogTouch=true` writes touches and toggle outcomes to `%LOCALAPPDATA%\VirtualTouchpad\touch.log`; use it to
  diagnose with the user's real touches instead of guessing.

## Notes

- Keep pure logic (`Layout`, `TapDetector`) free of side effects and unit tested.
- Don't launch `VirtualTouchpad.exe` to "test" it: a second launch toggles the running instance.
