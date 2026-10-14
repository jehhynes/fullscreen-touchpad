#pragma once

#include "Overlays.h"
#include "Settings.h"
#include "TouchKeyboard.h"
#include "TouchpadEngine.h"
#include "Win32TouchpadHost.h"

#include <windows.h>
#include <shellapi.h>

// Owns the hidden host window that all touch input is redirected to, the tray icon,
// and the on/off state.
//
// When capturing, RegisterPointerInputTarget sends every touchscreen contact to this
// window (no matter where on screen it lands), so nothing needs to cover the screen.
// Otherwise the window only *observes* touch so it can spot the toggle gesture while
// apps receive touch normally. Both calls require the UIAccess privilege.
class App
{
public:
    explicit App(HINSTANCE instance);
    ~App();

    bool Create();
    int Run();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
    bool HandlePointer(UINT msg, WPARAM wParam);
    void HandleRawInput(LPARAM lParam);
    void PollKeyboard();

    void SetEnabled(bool enabled, bool announce);
    bool ApplyCapture();
    bool CaptureTouch();
    void ObserveTouch();
    void ReleaseTouch();

    void AddTrayIcon();
    void UpdateTrayIcon();
    void ShowTrayMenu(POINT pt);
    void ReloadSettings();
    bool StartsWithWindows() const;
    void SetStartWithWindows(bool on);

    HINSTANCE instance_;
    HWND hwnd_ = nullptr;
    Settings settings_;
    Win32TouchpadHost host_;
    TouchpadEngine engine_;
    TouchKeyboard keyboard_;
    ActiveBorder border_;
    Osd osd_;

    bool enabled_ = false;           // the user wants the touchpad on
    bool keyboardPassthrough_ = false; // touch keyboard is open, so touch goes straight to the screen
    bool capturing_ = false;
    enum class Registration { None, Capture, Observe } registration_ = Registration::None;
    POINT lastTouch_{};

    NOTIFYICONDATAW tray_{};
    HICON iconOn_ = nullptr;
    HICON iconOff_ = nullptr;
    UINT taskbarCreatedMsg_ = 0;
};
