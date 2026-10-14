#pragma once

#include "BuiltinTouchpad.h"
#include "Osd.h"
#include "RawTouch.h"
#include "Settings.h"
#include "TapDetector.h"
#include "TaskbarButton.h"

#include <windows.h>
#include <shellapi.h>

#include <memory>

constexpr wchar_t kAppWindowClass[] = L"VirtualTouchpadApp";
constexpr UINT WM_APP_TRAY = WM_APP + 1;         // tray icon callback
constexpr UINT WM_APP_TOGGLE = WM_APP + 2;       // toggle (also posted by a second instance)
constexpr UINT WM_APP_PRESS_RESULT = WM_APP + 3;  // outcome of pressing the taskbar touchpad button

// Turns Windows' built-in virtual touchpad into an invisible full-screen touchpad and toggles it
// with a four-finger tap (read from raw touchscreen input), Ctrl+Alt+T, or the tray icon.
class App
{
public:
    explicit App(HINSTANCE instance);
    ~App();

    bool Create();
    int Run();

    // Puts the touchpad window back to normal (used by the crash handler).
    void RestoreTouchpad() { touchpad_.Restore(); }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static void CALLBACK OnWinEvent(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void Toggle();
    void LogTouch(const TapDetector::Summary& s);
    void LogLine(const std::string& line); // diagnostics log, when LogTouch is on
    void AddTrayIcon();
    void UpdateTrayIcon();
    void ShowTrayMenu(POINT pt);
    bool StartsWithWindows() const;
    void SetStartWithWindows(bool on);

    HINSTANCE instance_;
    HWND hwnd_ = nullptr;
    Settings settings_;
    BuiltinTouchpad touchpad_;
    std::unique_ptr<TaskbarButton> button_; // opens/closes the touchpad, on its own thread
    RawTouch rawTouch_;
    TapDetector detector_;
    Osd osd_;

    HWINEVENTHOOK showHideHook_ = nullptr;
    HWINEVENTHOOK cloakHook_ = nullptr;
    HWINEVENTHOOK locationHook_ = nullptr;

    NOTIFYICONDATAW tray_{};
    HICON iconOn_ = nullptr;
    HICON iconOff_ = nullptr;
    UINT taskbarCreatedMsg_ = 0;
    bool trayShowsOn_ = false;
    ULONGLONG lastToggleTick_ = 0;
    bool closing_ = false;        // exiting: waiting for the touchpad to close
    ULONGLONG closeDeadline_ = 0; // after this, restore the touchpad window and exit anyway
};
