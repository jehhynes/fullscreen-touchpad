#pragma once

#include "BuiltinTouchpad.h"
#include "CornerButton.h"
#include "Osd.h"
#include "RawTouch.h"
#include "Settings.h"
#include "TapDetector.h"
#include "TouchKeyboard.h"

#include <windows.h>
#include <shellapi.h>
#include <shobjidl_core.h>
#include <wrl/client.h>

#include <memory>

constexpr wchar_t kAppWindowClass[] = L"VirtualTouchpadApp";
constexpr UINT WM_APP_TRAY = WM_APP + 1;         // tray icon callback
constexpr UINT WM_APP_TOGGLE = WM_APP + 2;       // toggle (also posted by a second instance)
constexpr UINT WM_APP_CORNER_TAP = WM_APP + 3;   // the keyboard corner was tapped

// Turns Windows' built-in virtual touchpad into an invisible full-screen touchpad and toggles it
// with a three-finger tap (read from raw touchscreen input), Ctrl+Alt+T, or the tray icon.
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
    RawTouch rawTouch_;
    TapDetector detector_;
    Osd osd_;

    // Where the on-screen touch keyboard is, so the touchpad can leave it uncovered.
    void UpdateKeyboardRect();
    Microsoft::WRL::ComPtr<IFrameworkInputPane> inputPane_;
    RECT loggedKeyboard_{};

    TouchKeyboard touchKeyboard_;
    HWINEVENTHOOK focusHook_ = nullptr;

    // Tap the bottom-right corner to show/hide the keyboard (needed in Remote Desktop).
    void PlaceCorner(bool raise);
    std::unique_ptr<CornerButton> corner_;

    HWINEVENTHOOK showHideHook_ = nullptr;
    HWINEVENTHOOK cloakHook_ = nullptr;
    HWINEVENTHOOK locationHook_ = nullptr;

    NOTIFYICONDATAW tray_{};
    HICON iconOn_ = nullptr;
    HICON iconOff_ = nullptr;
    UINT taskbarCreatedMsg_ = 0;
    bool trayShowsOn_ = false;
    ULONGLONG lastToggleTick_ = 0;
    unsigned rawMessages_ = 0, rawParsed_ = 0, rawLogged_ = 0; // diagnostics
    bool closing_ = false;        // exiting: waiting for the touchpad to close
    ULONGLONG closeDeadline_ = 0; // after this, restore the touchpad window and exit anyway
};
