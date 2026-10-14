# Virtual Touchpad

Turns your whole touchscreen into an invisible touchpad. While it's on, moving a finger anywhere
on the screen moves the mouse pointer relatively, like a laptop touchpad, and nothing is drawn
over your content. A real mouse and pen keep working normally.

## How it works

It reuses **Windows' built-in virtual touchpad** and reshapes its window. Windows' own touchpad
then does everything: pointer movement, taps, scrolling and gestures, with your touchpad settings
and no cursor flicker. The app:

- opens the built-in touchpad through Windows' `ms-virtualtouchpad:` URL and closes it with its own
  close command, so it works even when the taskbar is hidden (e.g. with the keyboard detached);
- stretches its window over the whole screen and makes it invisible (opacity 1/255);
- pushes its title bar above the screen and its click buttons below it;
- limits where it receives touch with a window region: the work area, minus thin strips at the
  left, right and bottom edges so Windows' edge swipes still work, and minus a small hole under the
  mouse pointer that follows it, so clicks, hover and scrolling at the pointer reach the app under it;
- watches the touchscreen through Raw Input (read-only, no special privileges) for the toggle tap.

## Using it

| Action | Result |
| --- | --- |
| **Four-finger tap** | Turn the touchpad on or off |
| Ctrl+Alt+T, or tap the app's tray icon | Same |
| Everything else | Windows' touchpad gestures: move, tap, two-finger scroll and right-click, and so on |

Setup:

- **Set Windows' own four-finger tap to "Nothing"**, so it doesn't also fire when you toggle:
  Settings → Bluetooth & devices → Touchpad → Three- and four-finger gestures.
- Pointer speed and tap behaviour come from Settings → Bluetooth & devices → Touchpad.
- While the app runs, the built-in touchpad always opens full screen and invisible, however it's
  opened. Without the app it's Windows' normal touchpad.

## Build, test, install

Requires Visual Studio 2026 Build Tools with the **Desktop development with C++** workload.

```powershell
.\scripts\build.ps1                       # build (no developer prompt needed)
.\build\Release\touchpad_tests.exe        # unit tests (layout and tap detection)
.\scripts\install.ps1 -StartWithWindows   # per-user install, no admin rights
```

After a rebuild, `.\scripts\deploy.ps1` installs and restarts the new build, and
`.\scripts\check-install.ps1` confirms it's current and running. `.\scripts\uninstall.ps1` removes it.

## Settings

Tray icon → *Edit settings…* opens `%APPDATA%\VirtualTouchpad\settings.ini`; restart the app after
editing. Options: `StartEnabled`, `ToggleFingers`, `ToggleMaxMs`, `Opacity`, `EdgeGap`, `PointerHole`,
`ShowOsd`, and `LogTouch` (diagnostics: writes each touch and toggle to
`%LOCALAPPDATA%\VirtualTouchpad\touch.log`).

Note that the four-finger tap is also seen by whatever is under your fingers: turning the touchpad on
with it taps the app underneath.
