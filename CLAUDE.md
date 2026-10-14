# Virtual Touchpad

Full-screen invisible touchpad for Windows touchscreens. C++20 / Win32, CMake (Visual Studio 2026
generator), GoogleTest. See README.md for the design and gestures.

## Commands

- Build: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build.ps1`
- Test: `build\Release\touchpad_tests.exe` (or `ctest --preset release`)
- Deploy the current build: `scripts\update.cmd` (asks the user for admin rights; waits; exit code 0 = success,
  log in `build\deploy.log`)
- Check what's installed: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\check-install.ps1`
  (exit 0 = installed build is current, signed, and running)

## Definition of done

**A change is not complete until it is installed.** Before reporting any code change as done:

1. Build with no warnings.
2. Run the unit tests; all must pass.
3. Run `scripts\update.cmd` to deploy.
4. Run `scripts\check-install.ps1` and confirm it reports current, `Valid` signature, and running.

Only then report completion, saying what was verified. If deploying fails or is blocked, say so plainly
rather than reporting the task complete.

`scripts\install.cmd` (one-time setup) creates and trusts the signing certificate in the machine's root
store, which is a system security change. The user runs that one, not Claude. If `update.cmd` says no
signing certificate exists, ask the user to run `scripts\install.cmd`.

## Notes

- The gesture engine (`src/TouchpadEngine.*`) only talks to the world through `TouchpadHost`; keep it
  that way so it stays unit-testable. Add a test in `tests/` for new gesture behaviour.
- UIAccess only works for the signed copy in Program Files, so touch behaviour can only be checked on the
  installed app, by the user, on the touchscreen.
