#include "CornerButton.h"

namespace
{
    constexpr wchar_t kClass[] = L"VirtualTouchpadCorner";

    void DisableTouchFeedback(HWND hwnd)
    {
        const FEEDBACK_TYPE types[] = { FEEDBACK_TOUCH_CONTACTVISUALIZATION, FEEDBACK_TOUCH_TAP,
                                        FEEDBACK_TOUCH_DOUBLETAP, FEEDBACK_TOUCH_PRESSANDHOLD,
                                        FEEDBACK_TOUCH_RIGHTTAP, FEEDBACK_GESTURE_PRESSANDTAP };
        BOOL off = FALSE;
        for (FEEDBACK_TYPE t : types) SetWindowFeedbackSetting(hwnd, t, 0, sizeof(off), &off);
    }

    // Whether the mouse message being handled was made from touch or pen input.
    bool FromTouchOrPen()
    {
        constexpr LPARAM kSignatureMask = 0xFFFFFF00, kSignature = 0xFF515700;
        return (GetMessageExtraInfo() & kSignatureMask) == kSignature;
    }
}

CornerButton::~CornerButton()
{
    if (hwnd_) DestroyWindow(hwnd_);
}

void CornerButton::Place(const RECT& rect, bool raise)
{
    const bool show = rect.right > rect.left && rect.bottom > rect.top;
    if (show && visible_ && !raise && EqualRect(&rect, &placed_)) return;
    if (!show)
    {
        if (hwnd_ && visible_) ShowWindow(hwnd_, SW_HIDE);
        visible_ = false;
        return;
    }

    if (!hwnd_)
    {
        HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc = WndProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        wc.lpszClassName = kClass;
        RegisterClassExW(&wc);
        // Opacity 1/255: invisible, but still receives touch (0 would let touch through).
        hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_LAYERED, kClass, L"",
                                WS_POPUP, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, nullptr,
                                nullptr, instance, nullptr);
        if (!hwnd_) return;
        SetLayeredWindowAttributes(hwnd_, 0, 1, LWA_ALPHA);
        DisableTouchFeedback(hwnd_);
    }

    // Keep it above the touchpad window (both are always-on-top).
    SetWindowPos(hwnd_, HWND_TOPMOST, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    placed_ = rect;
    visible_ = true;
}

LRESULT CALLBACK CornerButton::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_POINTERDOWN:
    case WM_POINTERUP:
    case WM_POINTERUPDATE:
        return 0; // handled: don't let it turn into mouse input
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
        // Windows sometimes delivers a touch here only as the mouse click it turned it into
        // (a right click after a long press). Swallow those too.
        if (FromTouchOrPen()) return 0;
        break;
    case WM_POINTERACTIVATE:
        return PA_NOACTIVATE;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
