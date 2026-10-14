#include "Osd.h"

#include <shellscalingapi.h>

namespace
{
    constexpr wchar_t kClass[] = L"VirtualTouchpadOsd";
    constexpr COLORREF kColorKey = RGB(255, 0, 255);
    constexpr UINT_PTR kHideTimer = 1;
}

Osd::~Osd()
{
    if (hwnd_) DestroyWindow(hwnd_);
}

void Osd::Flash(const std::wstring& text)
{
    text_ = text;
    HMONITOR monitor = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(monitor, &mi);
    UINT dpiX = 96, dpiY = 96;
    GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    scale_ = dpiX / 96.0;

    const int w = int(380 * scale_), h = int(56 * scale_);
    const RECT& wa = mi.rcWork;
    const int x = wa.left + (wa.right - wa.left - w) / 2, y = wa.bottom - h - int(48 * scale_);

    if (!hwnd_)
    {
        HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc = WndProc;
        wc.hInstance = instance;
        wc.lpszClassName = kClass;
        RegisterClassExW(&wc);
        hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT,
                                kClass, L"", WS_POPUP, x, y, w, h, nullptr, nullptr, instance, this);
        if (!hwnd_) return;
        SetLayeredWindowAttributes(hwnd_, kColorKey, 235, LWA_COLORKEY | LWA_ALPHA);
    }
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(hwnd_, nullptr, TRUE);
    SetTimer(hwnd_, kHideTimer, 1100, nullptr);
}

LRESULT CALLBACK Osd::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_NCCREATE)
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
    auto* self = reinterpret_cast<Osd*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg)
    {
    case WM_PAINT:
        if (self) self->Paint();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_TIMER:
        KillTimer(hwnd, kHideTimer);
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void Osd::Paint()
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd_, &ps);
    RECT rc;
    GetClientRect(hwnd_, &rc);

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
    EndPaint(hwnd_, &ps);
}
