#include "Overlays.h"

#include <shellscalingapi.h>

#include <algorithm>
#include <cmath>

namespace
{
    constexpr wchar_t kOverlayClass[] = L"VirtualTouchpadOverlay";
    constexpr COLORREF kAccent = RGB(0, 120, 215);
    constexpr COLORREF kColorKey = RGB(255, 0, 255);
    constexpr UINT_PTR kHideTimer = 1;
    constexpr UINT kOsdVisibleMs = 1100;

    constexpr DWORD kOverlayExStyle = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST |
                                      WS_EX_LAYERED | WS_EX_TRANSPARENT;

    HINSTANCE g_instance = nullptr;

    LRESULT CALLBACK OverlayProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    HWND CreateOverlay(const RECT& r, WNDPROC proc, void* param)
    {
        HWND hwnd = CreateWindowExW(kOverlayExStyle, kOverlayClass, L"", WS_POPUP,
                                    r.left, r.top, r.right - r.left, r.bottom - r.top,
                                    nullptr, nullptr, g_instance, param);
        if (hwnd && proc) SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(proc));
        return hwnd;
    }

    BOOL CALLBACK CollectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM param)
    {
        reinterpret_cast<std::vector<HMONITOR>*>(param)->push_back(monitor);
        return TRUE;
    }
}

void RegisterOverlayClass(HINSTANCE instance)
{
    g_instance = instance;
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = OverlayProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(kAccent);
    wc.lpszClassName = kOverlayClass;
    RegisterClassExW(&wc);
}

unsigned DpiAt(POINT pt)
{
    UINT dpiX = 96, dpiY = 96;
    HMONITOR monitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    if (FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) return 96;
    return dpiX;
}

// ------------------------------------------------------------------ ActiveBorder

void ActiveBorder::Show(double thicknessAt96Dpi)
{
    Hide();

    std::vector<HMONITOR> monitors;
    EnumDisplayMonitors(nullptr, nullptr, CollectMonitor, reinterpret_cast<LPARAM>(&monitors));

    for (HMONITOR monitor : monitors)
    {
        MONITORINFO mi{ sizeof(mi) };
        if (!GetMonitorInfoW(monitor, &mi)) continue;
        const RECT b = mi.rcMonitor;
        const unsigned dpi = DpiAt(POINT{ b.left + 1, b.top + 1 });
        const LONG t = std::max(1L, std::lround(thicknessAt96Dpi * dpi / 96.0));

        const RECT edges[] = {
            { b.left, b.top, b.right, b.top + t },
            { b.left, b.bottom - t, b.right, b.bottom },
            { b.left, b.top + t, b.left + t, b.bottom - t },
            { b.right - t, b.top + t, b.right, b.bottom - t },
        };
        for (const RECT& r : edges)
        {
            HWND strip = CreateOverlay(r, nullptr, nullptr);
            if (!strip) continue;
            SetLayeredWindowAttributes(strip, 0, 150, LWA_ALPHA);
            ShowWindow(strip, SW_SHOWNOACTIVATE);
            strips_.push_back(strip);
        }
    }
}

void ActiveBorder::Hide()
{
    for (HWND strip : strips_) DestroyWindow(strip);
    strips_.clear();
}

// ------------------------------------------------------------------ Osd

Osd::~Osd()
{
    if (hwnd_) DestroyWindow(hwnd_);
}

void Osd::Flash(const std::wstring& text, POINT anchor)
{
    text_ = text;

    HMONITOR monitor = MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(monitor, &mi);
    scale_ = DpiAt(anchor) / 96.0;

    const int w = int(240 * scale_), h = int(56 * scale_);
    const RECT& wa = mi.rcWork;
    const int x = wa.left + (wa.right - wa.left - w) / 2;
    const int y = wa.bottom - h - int(48 * scale_);

    if (!hwnd_)
    {
        hwnd_ = CreateOverlay(RECT{ x, y, x + w, y + h }, WndProc, nullptr);
        if (!hwnd_) return;
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        SetLayeredWindowAttributes(hwnd_, kColorKey, 235, LWA_COLORKEY | LWA_ALPHA);
    }

    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(hwnd_, nullptr, TRUE);
    SetTimer(hwnd_, kHideTimer, kOsdVisibleMs, nullptr);
}

LRESULT CALLBACK Osd::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto* self = reinterpret_cast<Osd*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg)
    {
    case WM_PAINT:
        if (self) self->Paint(hwnd);
        else ValidateRect(hwnd, nullptr);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_TIMER:
        if (wParam == kHideTimer)
        {
            KillTimer(hwnd, kHideTimer);
            ShowWindow(hwnd, SW_HIDE);
        }
        return 0;
    }
    return OverlayProc(hwnd, msg, wParam, lParam);
}

void Osd::Paint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);

    HBRUSH key = CreateSolidBrush(kColorKey);
    FillRect(dc, &rc, key);
    DeleteObject(key);

    HBRUSH bg = CreateSolidBrush(RGB(32, 32, 32));
    HGDIOBJ oldBrush = SelectObject(dc, bg);
    HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
    const int r = int(28 * scale_);
    RoundRect(dc, rc.left, rc.top, rc.right + 1, rc.bottom + 1, r, r);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(bg);

    HFONT font = CreateFontW(-int(18 * scale_), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    DrawTextW(dc, text_.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, oldFont);
    DeleteObject(font);

    EndPaint(hwnd, &ps);
}
