#include "App.h"

#include <windowsx.h>
#include <shlobj.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
    App* g_app = nullptr; // for the WinEvent callback, which has no context parameter

    constexpr UINT_PTR kTapTimer = 1;   // ends touch sessions for tap detection
    constexpr UINT_PTR kShapeTimer = 2; // keeps the touchpad in shape and the tray icon current (backstop for events)
    constexpr UINT_PTR kExitTimer = 3;  // waits for the touchpad to close before exiting
    constexpr UINT_PTR kKeyboardTimer = 4; // follows the on-screen touch keyboard

    // If the app crashes, put the touchpad window back so the user isn't left with an invisible one.
    LONG WINAPI OnCrash(EXCEPTION_POINTERS*)
    {
        if (g_app) g_app->RestoreTouchpad();
        return EXCEPTION_CONTINUE_SEARCH;
    }
    constexpr int kToggleHotkey = 1;
    constexpr UINT kTrayId = 1;
    constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    constexpr wchar_t kRunValue[] = L"VirtualTouchpad";

    enum MenuId : UINT { IdOn = 1, IdStartWithWindows, IdEditSettings, IdExit };

    TapDetector::Params DetectorParams(const Settings& s)
    {
        TapDetector::Params p;
        p.fingers = std::max(2, int(s.ToggleFingers));
        p.maxDurationMs = int(s.ToggleMaxMs);
        return p;
    }

    BuiltinTouchpad::Options TouchpadOptions(const Settings& s)
    {
        BuiltinTouchpad::Options o;
        o.alpha = BYTE(std::clamp(int(s.Opacity), 1, 255));
        o.edgeGap = std::max(0, int(s.EdgeGap));
        o.holeRadius = std::max(1, int(s.PointerHole));
        o.cornerDip = int(s.KeyboardCorner);
        return o;
    }

    // A small touchpad glyph: a filled blue pad when on, a grey outline when off.
    HICON MakeTrayIcon(bool on)
    {
        const int s = GetSystemMetrics(SM_CXSMICON);
        HDC screen = GetDC(nullptr);
        HDC dc = CreateCompatibleDC(screen);
        HBITMAP color = CreateCompatibleBitmap(screen, s, s);
        HBITMAP mask = CreateBitmap(s, s, 1, 1, nullptr);
        ReleaseDC(nullptr, screen);

        const RECT full{ 0, 0, s, s };
        const int inset = std::max(1, s / 16), r = std::max(2, s / 4), pen = std::max(1, s / 12);
        auto drawPad = [&](COLORREF fill, COLORREF outline, bool filled) {
            HPEN p = CreatePen(PS_INSIDEFRAME, pen, outline);
            HBRUSH b = filled ? CreateSolidBrush(fill) : static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
            HGDIOBJ oldPen = SelectObject(dc, p), oldBrush = SelectObject(dc, b);
            RoundRect(dc, inset, inset + s / 8, s - inset, s - inset - s / 8, r, r);
            SelectObject(dc, oldBrush);
            SelectObject(dc, oldPen);
            DeleteObject(p);
            if (filled) DeleteObject(b);
        };

        HGDIOBJ old = SelectObject(dc, color); // colour plane: black where transparent
        FillRect(dc, &full, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        if (on) drawPad(RGB(0, 120, 215), RGB(0, 120, 215), true);
        else drawPad(0, RGB(140, 140, 140), false);
        SelectObject(dc, mask); // mask plane: white = transparent
        FillRect(dc, &full, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        drawPad(RGB(0, 0, 0), RGB(0, 0, 0), on);
        SelectObject(dc, old);

        ICONINFO ii{ TRUE, 0, 0, mask, color };
        HICON icon = CreateIconIndirect(&ii);
        DeleteObject(color);
        DeleteObject(mask);
        DeleteDC(dc);
        return icon;
    }
}

App::App(HINSTANCE instance)
    : instance_(instance),
      settings_(Settings::Load()),
      touchpad_(TouchpadOptions(settings_)),
      detector_(DetectorParams(settings_))
{
    g_app = this;
}

App::~App()
{
    g_app = nullptr;
    if (iconOn_) DestroyIcon(iconOn_);
    if (iconOff_) DestroyIcon(iconOff_);
}

bool App::Create()
{
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance_;
    wc.lpszClassName = kAppWindowClass;
    if (!RegisterClassExW(&wc)) return false;

    // Never shown; a top-level window so the tray menu can take the foreground.
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, kAppWindowClass, L"Virtual Touchpad", WS_POPUP, 0, 0, 0, 0, nullptr,
                            nullptr, instance_, this);
    if (!hwnd_) return false;
    SetUnhandledExceptionFilter(OnCrash);
    corner_ = std::make_unique<CornerButton>();

    taskbarCreatedMsg_ = RegisterWindowMessageW(L"TaskbarCreated");
    RegisterHotKey(hwnd_, kToggleHotkey, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'T');
    const bool rawOk = rawTouch_.Register(hwnd_);
    if (settings_.LogTouch)
        LogLine("start: raw input registered=" + std::to_string(rawOk ? 1 : 0) + " error=" +
                std::to_string(rawOk ? 0 : GetLastError()));

    // Reshape the touchpad whenever it opens; move the pointer hole whenever the cursor moves.
    // The touchpad is an app frame window: it appears by being uncloaked rather than shown.
    showHideHook_ = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_HIDE, nullptr, OnWinEvent, 0, 0,
                                    WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    cloakHook_ = SetWinEventHook(EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED, nullptr, OnWinEvent, 0, 0,
                                 WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    locationHook_ = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, nullptr, OnWinEvent, 0,
                                    0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    // Focus moving into a text field while the touchpad is on: show the touch keyboard.
    if (settings_.ShowKeyboardForTextFields)
        focusHook_ = SetWinEventHook(EVENT_OBJECT_FOCUS, EVENT_OBJECT_FOCUS, nullptr, OnWinEvent, 0, 0,
                                     WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    iconOn_ = MakeTrayIcon(true);
    iconOff_ = MakeTrayIcon(false);
    AddTrayIcon();

    if (touchpad_.IsOpen()) touchpad_.Apply();
    else if (settings_.StartEnabled) BuiltinTouchpad::Open();

    SetTimer(hwnd_, kTapTimer, 50, nullptr);
    SetTimer(hwnd_, kShapeTimer, 250, nullptr);
    SetTimer(hwnd_, kKeyboardTimer, 100, nullptr);
    return true;
}

int App::Run()
{
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return int(msg.wParam);
}

LRESULT CALLBACK App::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_NCCREATE)
    {
        auto* app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        app->hwnd_ = hwnd;
    }
    auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return app ? app->HandleMessage(msg, wParam, lParam) : DefWindowProcW(hwnd, msg, wParam, lParam);
}

void CALLBACK App::OnWinEvent(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG, DWORD, DWORD)
{
    if (!g_app) return;
    if (event == EVENT_OBJECT_FOCUS)
    {
        if (g_app->touchpad_.Shaped()) g_app->touchKeyboard_.CheckFocusSoon();
        return;
    }
    if (event == EVENT_OBJECT_LOCATIONCHANGE)
    {
        if (idObject == OBJID_CURSOR)
        {
            g_app->touchpad_.UpdatePointerHole();
            g_app->PlaceCorner(false);
        }
        else if (idObject == OBJID_WINDOW && hwnd && hwnd == g_app->touchpad_.Shaped())
            g_app->touchpad_.Apply(); // put it back if it moved or resized itself
        return;
    }
    if (idObject != OBJID_WINDOW || !hwnd) return;
    wchar_t cls[64];
    if (!GetClassNameW(hwnd, cls, ARRAYSIZE(cls)) || wcscmp(cls, L"ApplicationFrameWindow") != 0) return;

    if (event == EVENT_OBJECT_SHOW || event == EVENT_OBJECT_UNCLOAKED) g_app->touchpad_.Apply();
    g_app->UpdateTrayIcon();
}

LRESULT App::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_INPUT:
    {
        int count = 0;
        std::vector<TapDetector::Contact> contacts;
        ++rawMessages_;
        if (rawTouch_.Read(lParam, count, contacts))
        {
            ++rawParsed_;
            detector_.OnReport(int64_t(GetTickCount64()), count, contacts);
            cornerGesture_.OnReport(int64_t(GetTickCount64()), count, contacts);
        }
        break; // DefWindowProc does the raw input cleanup
    }

    case WM_TIMER:
        if (wParam == kTapTimer)
        {
            const bool wasInSession = detector_.InSession();
            const bool tapped = detector_.Poll(int64_t(GetTickCount64()));
            if (wasInSession && !detector_.InSession() && settings_.LogTouch) LogTouch(detector_.LastSession());
            if (tapped) Toggle();
            OnCornerGesture(cornerGesture_.Poll(int64_t(GetTickCount64())));
        }
        else if (wParam == kShapeTimer)
        {
            if (!closing_) touchpad_.Apply(); // no-op when it's closed or already in shape
            PlaceCorner(true);
            UpdateTrayIcon();
            if (settings_.LogTouch && rawMessages_ != rawLogged_)
            {
                LogLine("raw input: messages=" + std::to_string(rawMessages_) + " parsed=" + std::to_string(rawParsed_));
                rawLogged_ = rawMessages_;
            }
        }
        else if (wParam == kKeyboardTimer)
        {
            UpdateKeyboardRect();
        }
        else if (wParam == kExitTimer && (!touchpad_.IsOpen() || GetTickCount64() > closeDeadline_))
        {
            KillTimer(hwnd_, kExitTimer);
            DestroyWindow(hwnd_);
        }
        return 0;

    case WM_HOTKEY:
        if (wParam == kToggleHotkey) Toggle();
        return 0;

    case WM_APP_TOGGLE:
        Toggle();
        return 0;

    case WM_APP_TRAY:
        switch (LOWORD(lParam))
        {
        case NIN_SELECT:
        case NIN_KEYSELECT:
            Toggle();
            break;
        case WM_CONTEXTMENU:
            ShowTrayMenu(POINT{ GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam) });
            break;
        }
        return 0;

    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE: // work area changes, e.g. taskbar moved or resized
        touchpad_.Apply();
        break;

    case WM_CLOSE:
        // Don't leave an invisible touchpad behind whose pointer hole no longer follows the pointer:
        // close it first, and wait (pumping messages) until it's gone.
        if (!closing_ && touchpad_.IsOpen())
        {
            closing_ = true;
            closeDeadline_ = GetTickCount64() + 2500;
            BuiltinTouchpad::Close();
            SetTimer(hwnd_, kExitTimer, 100, nullptr);
            return 0;
        }
        DestroyWindow(hwnd_);
        return 0;

    case WM_DESTROY:
        SendHoldKey(false);
        // If the touchpad is somehow still open, at least put its window back to normal.
        if (touchpad_.IsOpen()) touchpad_.Restore();
        if (focusHook_) UnhookWinEvent(focusHook_);
        if (showHideHook_) UnhookWinEvent(showHideHook_);
        if (cloakHook_) UnhookWinEvent(cloakHook_);
        if (locationHook_) UnhookWinEvent(locationHook_);
        UnregisterHotKey(hwnd_, kToggleHotkey);
        Shell_NotifyIconW(NIM_DELETE, &tray_);
        PostQuitMessage(0);
        return 0;

    default:
        if (msg == taskbarCreatedMsg_ && msg != 0)
        {
            AddTrayIcon(); // Explorer restarted
            return 0;
        }
        break;
    }
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

void App::Toggle()
{
    // Opening and closing are asynchronous (about half a second); a second press while one is still
    // in progress would undo it.
    const ULONGLONG now = GetTickCount64();
    if (now - lastToggleTick_ < 900) return;
    lastToggleTick_ = now;

    const bool wasOn = touchpad_.IsOpen();
    const bool ok = wasOn ? BuiltinTouchpad::Close() : BuiltinTouchpad::Open();
    if (settings_.LogTouch)
        LogLine(std::string(wasOn ? "toggle: closing" : "toggle: opening") + " ok=" + (ok ? "1" : "0") +
                " error=" + std::to_string(ok ? 0 : GetLastError()));
    if (settings_.ShowOsd) osd_.Flash(!ok ? L"Couldn't toggle the touchpad" : wasOn ? L"Touchpad off" : L"Touchpad on");
}

void App::PlaceCorner(bool raise)
{
    if (!corner_) return;
    // Empty while exiting, and while the touchpad is off unless the corner works then too.
    RECT corner = closing_ ? RECT{} : touchpad_.CornerRect(settings_.CornerWhenOff);
    // When the mouse pointer is in the corner, get out of the way so clicks reach the app underneath.
    // But not when a finger put it there: touching the corner moves the (hidden) pointer to the
    // finger before the touch arrives, and stepping aside then would make the tap miss the button.
    CURSORINFO cursor{ sizeof(cursor) };
    const bool cursorInside = GetCursorInfo(&cursor) && (cursor.flags & CURSOR_SHOWING) &&
                              !(cursor.flags & CURSOR_SUPPRESSED) && PtInRect(&corner, cursor.ptScreenPos);
    if (cursorInside) corner = RECT{};
    const bool wasVisible = corner_->Visible();
    corner_->Place(corner, raise);
    if (settings_.LogTouch && corner_->Visible() != wasVisible)
        LogLine(corner_->Visible() ? "corner: shown at " + std::to_string(corner.left) + "," + std::to_string(corner.top) +
                                         "-" + std::to_string(corner.right) + "," + std::to_string(corner.bottom)
                                   : std::string("corner: hidden") + (cursorInside ? " (pointer inside)" : ""));
}

bool App::InCorner(double x, double y) const
{
    if (!corner_ || !corner_->Visible()) return false; // not in use now, or the pointer is in the corner
    const RECT corner = touchpad_.CornerRect(settings_.CornerWhenOff);
    HMONITOR monitor = MonitorFromRect(&corner, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (!GetMonitorInfoW(monitor, &mi) || !EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &mode))
        return false;
    const POINT pt = layout::TouchToScreen(x, y, mi.rcMonitor, int(mode.dmDisplayOrientation));
    // Under the on-screen keyboard, the touch belongs to the keyboard.
    return PtInRect(&corner, pt) && !PtInRect(&keyboard_, pt);
}

void App::OnCornerGesture(CornerGesture::Event event)
{
    switch (event)
    {
    case CornerGesture::Event::HoldStart:
        if (settings_.LogTouch) LogLine("corner: hold");
        SendHoldKey(true);
        break;
    case CornerGesture::Event::HoldEnd:
        if (settings_.LogTouch) LogLine("corner: released");
        SendHoldKey(false);
        break;
    case CornerGesture::Event::DoubleTap:
        if (settings_.LogTouch) LogLine("corner: double tap, toggling keyboard");
        touchKeyboard_.ToggleSoon();
        break;
    case CornerGesture::Event::None:
        break;
    }
}

void App::SendHoldKey(bool down)
{
    const WORD vk = WORD(settings_.HoldKey);
    if (vk == 0 || down == holdKeyDown_) return;
    holdKeyDown_ = down;
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.wScan = WORD(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(input));
}

void App::UpdateKeyboardRect()
{
    if (!inputPane_ &&
        FAILED(CoCreateInstance(CLSID_FrameworkInputPane, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&inputPane_))))
        return;
    RECT keyboard{};
    if (FAILED(inputPane_->Location(&keyboard)) || keyboard.right <= keyboard.left || keyboard.bottom <= keyboard.top)
        keyboard = RECT{};
    touchpad_.SetKeyboardRect(keyboard);
    keyboard_ = keyboard;

    if (settings_.LogTouch && !EqualRect(&keyboard, &loggedKeyboard_))
    {
        LogLine("keyboard " + std::to_string(keyboard.left) + "," + std::to_string(keyboard.top) + "-" +
                std::to_string(keyboard.right) + "," + std::to_string(keyboard.bottom));
        loggedKeyboard_ = keyboard;
    }
}

void App::LogTouch(const TapDetector::Summary& s)
{
    char at[48] = "?";
    if (s.startX >= 0) sprintf_s(at, "%.3f,%.3f", s.startX, s.startY);
    POINT cursor{};
    GetCursorPos(&cursor);
    LogLine("touch fingers=" + std::to_string(s.maxFingers) + " duration=" + std::to_string(s.durationMs) +
            "ms moved=" + std::to_string(s.moved ? 1 : 0) + " toggled=" + std::to_string(s.toggled ? 1 : 0) +
            " at=" + at + " cursor=" + std::to_string(cursor.x) + "," + std::to_string(cursor.y) +
            " corner=" + (corner_ && corner_->Visible() ? "shown" : "hidden"));
}

void App::LogLine(const std::string& line)
{
    wchar_t* localAppData = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData))) return;
    const std::wstring dir = std::wstring(localAppData) + L"\\VirtualTouchpad";
    CoTaskMemFree(localAppData);
    CreateDirectoryW(dir.c_str(), nullptr);

    FILE* f = nullptr;
    if (_wfopen_s(&f, (dir + L"\\touch.log").c_str(), L"a") != 0 || !f) return;
    fprintf(f, "%llu %s\n", GetTickCount64(), line.c_str());
    fclose(f);
}

// ------------------------------------------------------------------ tray

void App::AddTrayIcon()
{
    tray_ = NOTIFYICONDATAW{ sizeof(tray_) };
    tray_.hWnd = hwnd_;
    tray_.uID = kTrayId;
    tray_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    tray_.uCallbackMessage = WM_APP_TRAY;
    trayShowsOn_ = touchpad_.IsOpen();
    tray_.hIcon = trayShowsOn_ ? iconOn_ : iconOff_;
    wcscpy_s(tray_.szTip, L"Full-screen touchpad (three-finger tap or Ctrl+Alt+T)");
    Shell_NotifyIconW(NIM_ADD, &tray_);
    tray_.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &tray_);
}

void App::UpdateTrayIcon()
{
    const bool on = touchpad_.IsOpen();
    if (on == trayShowsOn_) return;
    trayShowsOn_ = on;
    tray_.uFlags = NIF_ICON;
    tray_.hIcon = on ? iconOn_ : iconOff_;
    Shell_NotifyIconW(NIM_MODIFY, &tray_);
}

void App::ShowTrayMenu(POINT pt)
{
    HMENU menu = CreatePopupMenu();
    auto check = [](bool on) { return UINT(on ? MF_CHECKED : MF_UNCHECKED); };
    AppendMenuW(menu, MF_STRING | check(touchpad_.IsOpen()), IdOn, L"Touchpad on\tCtrl+Alt+T");
    AppendMenuW(menu, MF_STRING | check(StartsWithWindows()), IdStartWithWindows, L"Start with Windows");
    AppendMenuW(menu, MF_STRING, IdEditSettings, L"Edit settings...");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IdExit, L"Exit");

    SetForegroundWindow(hwnd_); // so the menu closes when clicking elsewhere
    const UINT cmd = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, pt.x, pt.y, hwnd_, nullptr);
    PostMessageW(hwnd_, WM_NULL, 0, 0);
    DestroyMenu(menu);

    switch (cmd)
    {
    case IdOn: Toggle(); break;
    case IdStartWithWindows: SetStartWithWindows(!StartsWithWindows()); break;
    case IdEditSettings:
        ShellExecuteW(nullptr, L"open", L"notepad.exe", Settings::FilePath().c_str(), nullptr, SW_SHOWNORMAL);
        break;
    case IdExit: PostMessageW(hwnd_, WM_CLOSE, 0, 0); break;
    }
}

bool App::StartsWithWindows() const
{
    DWORD size = 0;
    return RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ, nullptr, nullptr, &size) == ERROR_SUCCESS;
}

void App::SetStartWithWindows(bool on)
{
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
    if (on)
    {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        const std::wstring value = L"\"" + std::wstring(path) + L"\"";
        RegSetValueExW(key, kRunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                       DWORD((value.size() + 1) * sizeof(wchar_t)));
    }
    else
    {
        RegDeleteValueW(key, kRunValue);
    }
    RegCloseKey(key);
}
