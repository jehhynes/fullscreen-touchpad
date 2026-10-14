#include "BuiltinTouchpad.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <shellscalingapi.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace
{
    bool IsExplorerWindow(HWND hwnd)
    {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!process) return false;
        wchar_t path[MAX_PATH];
        DWORD size = MAX_PATH;
        const bool ok = QueryFullProcessImageNameW(process, 0, path, &size) != FALSE;
        CloseHandle(process);
        if (!ok) return false;
        const std::wstring p(path, size);
        return p.size() >= 13 && _wcsicmp(p.c_str() + p.size() - 13, L"\\explorer.exe") == 0;
    }

    BOOL CALLBACK FindProc(HWND hwnd, LPARAM param)
    {
        wchar_t cls[64];
        if (!IsWindowVisible(hwnd) || !GetClassNameW(hwnd, cls, ARRAYSIZE(cls)) ||
            wcscmp(cls, L"ApplicationFrameWindow") != 0)
            return TRUE;
        // The touchpad is Explorer's always-on-top, never-activated frame window.
        const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        if ((ex & (WS_EX_TOPMOST | WS_EX_NOACTIVATE)) != (WS_EX_TOPMOST | WS_EX_NOACTIVATE)) return TRUE;
        // App frame windows can be "visible" yet cloaked (hidden by DWM).
        DWORD cloaked = 0;
        if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) return TRUE;
        if (!IsExplorerWindow(hwnd)) return TRUE;
        *reinterpret_cast<HWND*>(param) = hwnd;
        return FALSE;
    }

    bool SameLayout(const layout::Params& a, const layout::Params& b)
    {
        return EqualRect(&a.monitor, &b.monitor) && EqualRect(&a.work, &b.work) && a.scale == b.scale &&
               a.edgeGap == b.edgeGap && a.holeRadius == b.holeRadius && EqualRect(&a.keyboard, &b.keyboard);
    }
}

HWND BuiltinTouchpad::FindTouchpadWindow()
{
    HWND found = nullptr;
    EnumWindows(FindProc, reinterpret_cast<LPARAM>(&found));
    return found;
}

bool BuiltinTouchpad::Open()
{
    SHELLEXECUTEINFOW sei{ sizeof(sei) };
    sei.fMask = SEE_MASK_ASYNCOK | SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"open";
    sei.lpFile = L"ms-virtualtouchpad:";
    sei.nShow = SW_SHOWNOACTIVATE;
    return ShellExecuteExW(&sei) != FALSE;
}

bool BuiltinTouchpad::Close()
{
    HWND hwnd = FindTouchpadWindow();
    return hwnd && PostMessageW(hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
}

layout::Params BuiltinTouchpad::LayoutFor(HWND) const
{
    // The touchscreen is the primary monitor.
    HMONITOR monitor = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(monitor, &mi);
    UINT dpiX = 96, dpiY = 96;
    GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);

    layout::Params p;
    p.monitor = mi.rcMonitor;
    p.work = mi.rcWork;
    p.scale = dpiX / 96.0;
    p.edgeGap = options_.edgeGap;
    p.holeRadius = std::max(1, int(std::lround(options_.holeRadius * p.scale)));
    p.keyboard = keyboard_;
    return p;
}

bool BuiltinTouchpad::Apply()
{
    HWND hwnd = FindTouchpadWindow();
    if (!hwnd)
    {
        shaped_ = nullptr;
        return false;
    }

    const layout::Params params = LayoutFor(hwnd);
    const RECT target = layout::WindowRect(params);
    RECT current{};
    GetWindowRect(hwnd, &current);
    if (hwnd == shaped_ && EqualRect(&current, &target) && SameLayout(params, params_)) return true;

    if (hwnd != shaped_)
    {
        // Remember how the touchpad looked, to put it back if closing it fails.
        original_ = hwnd;
        originalRect_ = current;
        originalExStyle_ = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    }
    params_ = params;

    // The touchpad vetoes resizing in WM_WINDOWPOSCHANGING, so skip that message.
    SetWindowPos(hwnd, nullptr, target.left, target.top, target.right - target.left, target.bottom - target.top,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSENDCHANGING);

    const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (!(ex & WS_EX_LAYERED)) SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex | WS_EX_LAYERED);
    SetLayeredWindowAttributes(hwnd, 0, options_.alpha, LWA_ALPHA);

    shaped_ = hwnd;
    lastCursor_ = POINT{ LONG_MIN, LONG_MIN }; // rebuild the region
    UpdatePointerHole();
    return true;
}

void BuiltinTouchpad::Restore()
{
    HWND hwnd = original_;
    shaped_ = nullptr;
    original_ = nullptr;
    if (!hwnd || !IsWindow(hwnd)) return;

    SetWindowRgn(hwnd, nullptr, TRUE);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, originalExStyle_);
    SetWindowPos(hwnd, nullptr, originalRect_.left, originalRect_.top, originalRect_.right - originalRect_.left,
                 originalRect_.bottom - originalRect_.top,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSENDCHANGING | SWP_FRAMECHANGED);
}

void BuiltinTouchpad::SetKeyboardRect(const RECT& keyboard)
{
    if (EqualRect(&keyboard, &keyboard_)) return;
    keyboard_ = keyboard;
    if (!shaped_) return;
    params_.keyboard = keyboard_;
    lastCursor_ = POINT{ LONG_MIN, LONG_MIN }; // rebuild the region now
    UpdatePointerHole();
}

void BuiltinTouchpad::UpdatePointerHole()
{
    if (!shaped_) return;
    if (!IsWindow(shaped_))
    {
        shaped_ = nullptr;
        return;
    }
    POINT cursor{};
    GetCursorPos(&cursor);
    if (cursor.x == lastCursor_.x && cursor.y == lastCursor_.y) return;
    lastCursor_ = cursor;

    HRGN region = layout::CreateRegion(params_, cursor);
    if (!SetWindowRgn(shaped_, region, FALSE)) DeleteObject(region); // on success the window owns it
}
