#include "App.h"
#include "InputSender.h"
#include "Messages.h"

#include <windowsx.h>

#include <algorithm>
#include <string>

namespace
{
    App* g_app = nullptr; // for the WinEvent callback, which has no context parameter

    constexpr UINT_PTR kKeyboardPollTimer = 1;
    constexpr UINT kKeyboardPollMs = 250;
    constexpr int kToggleHotkey = 1;
    constexpr UINT kTrayId = 1;
    constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    constexpr wchar_t kRunValue[] = L"VirtualTouchpad";

    enum MenuId : UINT
    {
        IdEnabled = 1,
        IdNaturalScrolling,
        IdShowBorder,
        IdStartWithWindows,
        IdEditSettings,
        IdReloadSettings,
        IdExit,
    };

    // RegisterPointerInputTargetEx (observe mode) isn't in every SDK's headers; resolve it at runtime.
    using RegisterTargetExFn = BOOL(WINAPI*)(HWND, POINTER_INPUT_TYPE, BOOL);
    using UnregisterTargetExFn = BOOL(WINAPI*)(HWND, POINTER_INPUT_TYPE);

    RegisterTargetExFn RegisterTargetEx()
    {
        static auto fn = reinterpret_cast<RegisterTargetExFn>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "RegisterPointerInputTargetEx"));
        return fn;
    }

    UnregisterTargetExFn UnregisterTargetEx()
    {
        static auto fn = reinterpret_cast<UnregisterTargetExFn>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "UnregisterPointerInputTargetEx"));
        return fn;
    }

    bool HasUIAccess()
    {
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
        DWORD uiAccess = 0, size = 0;
        const BOOL ok = GetTokenInformation(token, TokenUIAccess, &uiAccess, sizeof(uiAccess), &size);
        CloseHandle(token);
        return ok && uiAccess != 0;
    }

    // Draws a small touchpad glyph: a filled blue pad when on, a grey outline when off.
    HICON MakeTrayIcon(bool on)
    {
        const int s = GetSystemMetrics(SM_CXSMICON);
        HDC screen = GetDC(nullptr);
        HDC dc = CreateCompatibleDC(screen);
        HBITMAP color = CreateCompatibleBitmap(screen, s, s);
        HBITMAP mask = CreateBitmap(s, s, 1, 1, nullptr);
        ReleaseDC(nullptr, screen);

        const RECT full{ 0, 0, s, s };
        const int inset = std::max(1, s / 16);
        const int r = std::max(2, s / 4);
        const int penWidth = std::max(1, s / 12);

        auto drawPad = [&](COLORREF fill, COLORREF outline, bool filled) {
            HPEN pen = CreatePen(PS_INSIDEFRAME, penWidth, outline);
            HBRUSH brush = filled ? CreateSolidBrush(fill) : static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
            HGDIOBJ oldPen = SelectObject(dc, pen);
            HGDIOBJ oldBrush = SelectObject(dc, brush);
            RoundRect(dc, inset, inset + s / 8, s - inset, s - inset - s / 8, r, r);
            SelectObject(dc, oldBrush);
            SelectObject(dc, oldPen);
            DeleteObject(pen);
            if (filled) DeleteObject(brush);
        };

        // Colour plane: black where transparent (required by the AND/XOR icon format).
        HGDIOBJ old = SelectObject(dc, color);
        FillRect(dc, &full, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        if (on) drawPad(RGB(0, 120, 215), RGB(0, 120, 215), true);
        else drawPad(0, RGB(140, 140, 140), false);

        // Mask plane: white = transparent, black = opaque.
        SelectObject(dc, mask);
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

    void DisableTouchFeedback(HWND hwnd)
    {
        const FEEDBACK_TYPE types[] = {
            FEEDBACK_TOUCH_CONTACTVISUALIZATION, FEEDBACK_TOUCH_TAP, FEEDBACK_TOUCH_DOUBLETAP,
            FEEDBACK_TOUCH_PRESSANDHOLD, FEEDBACK_TOUCH_RIGHTTAP, FEEDBACK_GESTURE_PRESSANDTAP,
        };
        BOOL off = FALSE;
        for (FEEDBACK_TYPE t : types) SetWindowFeedbackSetting(hwnd, t, 0, sizeof(off), &off);
    }
}

App::App(HINSTANCE instance)
    : instance_(instance),
      settings_(Settings::Load()),
      engine_(settings_, host_)
{
    // Toggling re-registers the pointer target, so don't do it from inside a pointer message.
    host_.onToggle = [this] { PostMessageW(hwnd_, WM_APP_TOGGLE, 0, 0); };
    host_.onLeftClick = [this] {
        if (settings_.ShowKeyboardForTextFields) keyboard_.CheckFocusSoon();
    };
}

App::~App()
{
    if (iconOn_) DestroyIcon(iconOn_);
    if (iconOff_) DestroyIcon(iconOff_);
}

bool App::Create()
{
    RegisterOverlayClass(instance_);

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kHostWindowClass;
    if (!RegisterClassExW(&wc)) return false;

    // A 1x1, fully transparent, click-through window. Touch reaches it by redirection, not hit-testing.
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT,
                            kHostWindowClass, L"Virtual Touchpad", WS_POPUP,
                            0, 0, 1, 1, nullptr, nullptr, instance_, this);
    if (!hwnd_) return false;
    host_.hwnd = hwnd_;
    if (settings_.DebugLog) log_.Open();

    SetLayeredWindowAttributes(hwnd_, 0, 0, LWA_ALPHA);
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    DisableTouchFeedback(hwnd_);

    taskbarCreatedMsg_ = RegisterWindowMessageW(L"TaskbarCreated");
    for (UINT msg : { taskbarCreatedMsg_, UINT(WM_APP_TOGGLE), UINT(WM_APP_TRAY) })
        ChangeWindowMessageFilterEx(hwnd_, msg, MSGFLT_ALLOW, nullptr);

    RegisterHotKey(hwnd_, kToggleHotkey, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'T');

    // Cursor shape changes by other apps (see Raymond Chen, "How can I be notified when the cursor changes?").
    g_app = this;
    cursorHook_ = SetWinEventHook(EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE, nullptr, OnCursorEvent, 0, 0,
                                  WINEVENT_OUTOFCONTEXT);
    focusHook_ = SetWinEventHook(EVENT_OBJECT_FOCUS, EVENT_OBJECT_FOCUS, nullptr, OnFocusEvent, 0, 0,
                                 WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    // Raw mouse input comes only from physical mice and touchpads (not from touch, and not
    // from our own SendInput), so it tells us when to adopt the real cursor position.
    RAWINPUTDEVICE mouse{ 0x01, 0x02, RIDEV_INPUTSINK, hwnd_ };
    RegisterRawInputDevices(&mouse, 1, sizeof(mouse));

    SetTimer(hwnd_, kKeyboardPollTimer, kKeyboardPollMs, nullptr);

    iconOn_ = MakeTrayIcon(true);
    iconOff_ = MakeTrayIcon(false);
    AddTrayIcon();

    if (!HasUIAccess())
    {
        MessageBoxW(nullptr,
                    L"Virtual Touchpad needs the UIAccess privilege to take over touch input, "
                    L"and Windows only grants it to signed apps installed under Program Files.\n\n"
                    L"Run scripts\\install.ps1 from an elevated PowerShell to install it.",
                    L"Virtual Touchpad", MB_OK | MB_ICONWARNING);
    }

    SetEnabled(settings_.StartEnabled, false);
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
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        static_cast<App*>(cs->lpCreateParams)->hwnd_ = hwnd;
    }
    auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return app ? app->HandleMessage(msg, wParam, lParam) : DefWindowProcW(hwnd, msg, wParam, lParam);
}

namespace
{
    // Pointer messages (incl. non-client, wheel, routed), mouse messages, and legacy touch/gesture.
    bool IsInputMessage(UINT msg)
    {
        return (msg >= 0x0238 && msg <= 0x0253) || (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) ||
               (msg >= WM_NCMOUSEMOVE && msg <= WM_NCXBUTTONDBLCLK) || msg == WM_GESTURE || msg == WM_TOUCH ||
               msg == WM_SETCURSOR;
    }

    bool IsPointerMessage(UINT msg) { return msg >= 0x0238 && msg <= 0x0253; }

    const char* CursorName(HCURSOR c)
    {
        if (!c) return "none";
        static const struct { LPCWSTR id; const char* name; } kCursors[] = {
            { IDC_ARROW, "arrow" }, { IDC_IBEAM, "ibeam" }, { IDC_WAIT, "WAIT" },
            { IDC_APPSTARTING, "APPSTARTING" }, { IDC_HAND, "hand" }, { IDC_SIZEALL, "sizeall" },
        };
        for (const auto& k : kCursors)
            if (LoadCursorW(nullptr, k.id) == c) return k.name;
        return "other";
    }
}

void App::LogInputMessage(const char* stage, UINT msg, WPARAM wParam)
{
    CURSORINFO ci{ sizeof(ci) };
    GetCursorInfo(&ci);
    POINTER_INFO info{};
    const UINT32 id = GET_POINTERID_WPARAM(wParam);
    const bool pointer = IsPointerMessage(msg) && GetPointerInfo(id, &info);
    // cflags: 1 = CURSOR_SHOWING, 2 = CURSOR_SUPPRESSED (hidden because of touch/pen)
    log_.Write("%-7s msg=0x%04X pid=%u type=%d flags=0x%04X touch=%ld,%ld cursor=%ld,%ld cflags=%lu shape=%s cap=%d",
               stage, msg, pointer ? id : 0, pointer ? int(info.pointerType) : 0,
               pointer ? HIWORD(wParam) : 0, info.ptPixelLocation.x, info.ptPixelLocation.y,
               ci.ptScreenPos.x, ci.ptScreenPos.y, ci.flags, CursorName(ci.hCursor), capturing_ ? 1 : 0);
}

LRESULT App::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    const bool logged = log_.On() && IsInputMessage(msg);
    if (logged) LogInputMessage("recv", msg, wParam);

    switch (msg)
    {
    case WM_POINTERDOWN:
    case WM_POINTERUPDATE:
    case WM_POINTERUP:
    case WM_POINTERCAPTURECHANGED:
    case WM_POINTERENTER:
    case WM_POINTERLEAVE:
        // Handled touch must not reach DefWindowProc, which would promote it to mouse input.
        if (HandlePointer(msg, wParam)) return 0;
        break;

    case WM_POINTERACTIVATE:
        return PA_NOACTIVATE;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_INPUT:
        HandleRawInput(lParam);
        break; // DefWindowProc does the raw input cleanup

    case WM_TIMER:
        if (wParam >= Win32TouchpadHost::kTimerBase) engine_.OnTimer(int(wParam - Win32TouchpadHost::kTimerBase));
        else if (wParam == kKeyboardPollTimer) PollKeyboard();
        return 0;

    case WM_HOTKEY:
        if (wParam == kToggleHotkey)
        {
            GetCursorPos(&lastTouch_);
            SetEnabled(!enabled_, true);
        }
        return 0;

    case WM_APP_TOGGLE:
        SetEnabled(!enabled_, true);
        return 0;

    case WM_APP_TRAY:
        switch (LOWORD(lParam))
        {
        case NIN_SELECT:
        case NIN_KEYSELECT:
            GetCursorPos(&lastTouch_);
            SetEnabled(!enabled_, true);
            break;
        case WM_CONTEXTMENU:
            ShowTrayMenu(POINT{ GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam) });
            break;
        }
        return 0;

    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED:
        if (capturing_ && settings_.ShowBorder) border_.Show(settings_.BorderThickness);
        break;

    case WM_DESTROY:
        if (cursorHook_) UnhookWinEvent(cursorHook_);
        if (focusHook_) UnhookWinEvent(focusHook_);
        injector_.Destroy();
        engine_.Reset();
        ReleaseTouch();
        border_.Hide();
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
    if (logged) LogInputMessage("default", msg, wParam);
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

bool App::HandlePointer(UINT msg, WPARAM wParam)
{
    const UINT32 id = GET_POINTERID_WPARAM(wParam);
    POINTER_INFO info{};
    if (!GetPointerInfo(id, &info))
    {
        // The pointer is already gone; make sure we don't keep a stale contact.
        if (msg == WM_POINTERUP || msg == WM_POINTERCAPTURECHANGED) engine_.OnUp(id, true);
        return true;
    }
    if (info.pointerType != PT_TOUCH) return false;

    // While touch is redirected here, Windows shows *this thread's* cursor. We never get
    // WM_SETCURSOR for it, and an unset thread cursor is the busy (wait) cursor. Show the shape
    // the app under the pointer wants (I-beam over text, etc.) so the two never alternate.
    if (capturing_) SetCursor(appCursor_);

    const POINT p = info.ptPixelLocation;
    const bool canceled = (info.pointerFlags & POINTER_FLAG_CANCELED) != 0;
    lastTouch_ = p;
    lastTouchTick_ = GetTickCount64();

    // Native mode: hand the whole frame to the virtual touchpad.
    if (native_)
    {
        injector_.OnPointerMessage(id);
        // A touch-down hides the cursor until the touchpad stack starts moving it (~100 ms later);
        // mouse input at the current position shows it again straight away.
        if (msg == WM_POINTERDOWN)
        {
            POINT cursor{};
            GetCursorPos(&cursor);
            input::MoveCursorTo(cursor);
        }
    }

    switch (msg)
    {
    case WM_POINTERDOWN:
        engine_.SetDpi(DpiAt(p));
        engine_.OnDown(id, p.x, p.y);
        engine_.WakeCursor(); // touch-down hides the cursor; mouse input brings it back
        break;
    case WM_POINTERUPDATE:
        if (canceled) engine_.OnUp(id, true);
        else engine_.OnMove(id, p.x, p.y);
        break;
    case WM_POINTERUP:
        engine_.OnUp(id, canceled);
        break;
    case WM_POINTERCAPTURECHANGED:
        engine_.OnUp(id, true);
        break;
    }

    // Windows moves the real cursor to the finger; put it straight back at the touchpad pointer.
    POINT before{};
    if (log_.On()) GetCursorPos(&before);
    engine_.EnforceCursor();
    if (log_.On())
    {
        POINT after{};
        GetCursorPos(&after);
        if (after.x != before.x || after.y != before.y)
            log_.Write("snap    cursor was %ld,%ld (touch %ld,%ld), restored to %ld,%ld",
                       before.x, before.y, p.x, p.y, after.x, after.y);
    }
    return true;
}

void CALLBACK App::OnFocusEvent(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD)
{
    // Only focus changes caused by the touchpad (a touch within the last second) count as a tap.
    if (!g_app || !g_app->native_ || !g_app->settings_.ShowKeyboardForTextFields) return;
    if (GetTickCount64() - g_app->lastTouchTick_ > 1000) return;
    g_app->keyboard_.CheckFocusSoon();
}

void CALLBACK App::OnCursorEvent(HWINEVENTHOOK, DWORD, HWND hwnd, LONG idObject, LONG idChild, DWORD thread, DWORD)
{
    if (!g_app || hwnd != nullptr || idObject != OBJID_CURSOR || idChild != CHILDID_SELF) return;
    // The event arrives asynchronously, so the cursor showing now may already be ours again (the next
    // touch frame makes our thread the cursor owner). Events are attributed to a system thread, so the
    // only reliable test is: a shape different from our own thread's cursor must be the app's.
    CURSORINFO ci{ sizeof(ci) };
    if (GetCursorInfo(&ci) && ci.hCursor && ci.hCursor != GetCursor())
    {
        g_app->appCursor_ = ci.hCursor;
        g_app->log_.Write("cursorev app shape=%s (thread %lu)", CursorName(ci.hCursor), thread);
    }
}

void App::HandleRawInput(LPARAM lParam)
{
    RAWINPUT raw{};
    UINT size = sizeof(raw);
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) == UINT(-1))
        return;

    // hDevice is null for injected input, including our own.
    if (raw.header.dwType != RIM_TYPEMOUSE || raw.header.hDevice == nullptr || engine_.Touching()) return;
    POINT p{};
    GetCursorPos(&p);
    engine_.SyncCursor(p);
}

void App::PollKeyboard()
{
    const bool passthrough = enabled_ && settings_.TypeDirectlyOnKeyboard && keyboard_.IsVisible();
    if (passthrough == keyboardPassthrough_) return;
    keyboardPassthrough_ = passthrough;
    ApplyCapture();
}

// ------------------------------------------------------------------ on/off

void App::SetEnabled(bool enabled, bool announce)
{
    enabled_ = enabled;
    keyboardPassthrough_ = enabled && settings_.TypeDirectlyOnKeyboard && keyboard_.IsVisible();
    if (enabled) engine_.SyncCursor(host_.CursorPos());

    if (!ApplyCapture() && enabled)
    {
        const DWORD error = GetLastError();
        enabled_ = false;
        ApplyCapture();
        if (announce || HasUIAccess())
        {
            const std::wstring text = L"Couldn't take over touch input (error " + std::to_wstring(error) +
                L").\n\nThis needs the UIAccess privilege (install with scripts\\install.cmd), and only "
                L"one app at a time can capture touch.";
            MessageBoxW(nullptr, text.c_str(), L"Virtual Touchpad", MB_OK | MB_ICONWARNING);
        }
    }

    UpdateTrayIcon();
    if (announce && settings_.ShowOsd) osd_.Flash(enabled_ ? L"Touchpad on" : L"Touchpad off", lastTouch_);
}

// Captures touch when the touchpad is on and the touch keyboard isn't open; otherwise observes.
bool App::ApplyCapture()
{
    engine_.Reset();
    const bool capture = enabled_ && !keyboardPassthrough_;
    bool ok = true;
    DWORD error = ERROR_SUCCESS;
    if (capture && !CaptureTouch())
    {
        ok = false;
        error = GetLastError();
    }
    if (!capture || !ok) ObserveTouch();

    capturing_ = capture && ok;

    // Native mode: Windows' touchpad stack does everything; our engine only watches for the toggle.
    native_ = false;
    if (capturing_ && settings_.NativeTouchpad)
    {
        POINT primary{ 0, 0 };
        native_ = injector_.Ready() || injector_.Create(MonitorFromPoint(primary, MONITOR_DEFAULTTOPRIMARY));
    }
    else
    {
        injector_.Destroy();
    }
    engine_.SetObserveOnly(!capturing_ || native_);
    log_.Write("state   enabled=%d keyboardPassthrough=%d capture=%d ok=%d error=%lu registration=%d native=%d",
               enabled_, keyboardPassthrough_, capture, ok, error, int(registration_), native_);

    if (capturing_ && settings_.ShowBorder) border_.Show(settings_.BorderThickness);
    else border_.Hide();
    SetLastError(error); // callers report the capture failure, not anything that happened since
    return ok;
}

bool App::CaptureTouch()
{
    ReleaseTouch();
    if (!RegisterPointerInputTarget(hwnd_, PT_TOUCH)) return false;
    registration_ = Registration::Capture;
    return true;
}

void App::ObserveTouch()
{
    ReleaseTouch();
    // Observing lets apps keep receiving touch while we watch for the toggle gesture.
    // If it isn't available, the hotkey and tray icon still work.
    if (auto registerEx = RegisterTargetEx(); registerEx && registerEx(hwnd_, PT_TOUCH, TRUE))
        registration_ = Registration::Observe;
}

void App::ReleaseTouch()
{
    if (registration_ == Registration::Capture)
        UnregisterPointerInputTarget(hwnd_, PT_TOUCH);
    else if (registration_ == Registration::Observe)
        if (auto unregisterEx = UnregisterTargetEx()) unregisterEx(hwnd_, PT_TOUCH);
    registration_ = Registration::None;
}

// ------------------------------------------------------------------ tray

void App::AddTrayIcon()
{
    tray_ = NOTIFYICONDATAW{ sizeof(tray_) };
    tray_.hWnd = hwnd_;
    tray_.uID = kTrayId;
    tray_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    tray_.uCallbackMessage = WM_APP_TRAY;
    tray_.hIcon = enabled_ ? iconOn_ : iconOff_;
    wcscpy_s(tray_.szTip, enabled_ ? L"Virtual Touchpad: on" : L"Virtual Touchpad: off");
    Shell_NotifyIconW(NIM_ADD, &tray_);
    tray_.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &tray_);
}

void App::UpdateTrayIcon()
{
    tray_.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    tray_.hIcon = enabled_ ? iconOn_ : iconOff_;
    wcscpy_s(tray_.szTip, enabled_ ? L"Virtual Touchpad: on" : L"Virtual Touchpad: off");
    Shell_NotifyIconW(NIM_MODIFY, &tray_);
}

void App::ShowTrayMenu(POINT pt)
{
    HMENU menu = CreatePopupMenu();
    auto check = [](bool on) { return UINT(on ? MF_CHECKED : MF_UNCHECKED); };

    AppendMenuW(menu, MF_STRING | check(enabled_), IdEnabled, L"Touchpad enabled\tCtrl+Alt+T");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | check(settings_.NaturalScrolling), IdNaturalScrolling, L"Natural scrolling");
    AppendMenuW(menu, MF_STRING | check(settings_.ShowBorder), IdShowBorder, L"Show edge glow when on");
    AppendMenuW(menu, MF_STRING | check(StartsWithWindows()), IdStartWithWindows, L"Start with Windows");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IdEditSettings, L"Edit settings...");
    AppendMenuW(menu, MF_STRING, IdReloadSettings, L"Reload settings");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IdExit, L"Exit");

    SetForegroundWindow(hwnd_); // required for the menu to close when clicking elsewhere
    const UINT cmd = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, pt.x, pt.y, hwnd_, nullptr);
    PostMessageW(hwnd_, WM_NULL, 0, 0);
    DestroyMenu(menu);

    switch (cmd)
    {
    case IdEnabled:
        lastTouch_ = pt;
        SetEnabled(!enabled_, true);
        break;
    case IdNaturalScrolling:
        settings_.NaturalScrolling = !settings_.NaturalScrolling;
        settings_.Save();
        engine_.ApplySettings(settings_);
        break;
    case IdShowBorder:
        settings_.ShowBorder = !settings_.ShowBorder;
        settings_.Save();
        if (capturing_ && settings_.ShowBorder) border_.Show(settings_.BorderThickness);
        else border_.Hide();
        break;
    case IdStartWithWindows:
        SetStartWithWindows(!StartsWithWindows());
        break;
    case IdEditSettings:
        ShellExecuteW(nullptr, L"open", L"notepad.exe", Settings::FilePath().c_str(), nullptr, SW_SHOWNORMAL);
        break;
    case IdReloadSettings:
        ReloadSettings();
        break;
    case IdExit:
        DestroyWindow(hwnd_);
        break;
    }
}

void App::ReloadSettings()
{
    settings_ = Settings::Load();
    engine_.ApplySettings(settings_);
    if (settings_.DebugLog) log_.Open();
    else log_.Close();
    if (capturing_ && settings_.ShowBorder) border_.Show(settings_.BorderThickness);
    else border_.Hide();
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
