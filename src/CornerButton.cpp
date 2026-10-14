#include "CornerButton.h"

#include <cstdlib>

namespace
{
    constexpr wchar_t kClass[] = L"VirtualTouchpadCorner";
    constexpr ULONGLONG kMaxTapMs = 600;
    constexpr LONG kMaxTapTravel = 40; // pixels

    void DisableTouchFeedback(HWND hwnd)
    {
        const FEEDBACK_TYPE types[] = { FEEDBACK_TOUCH_CONTACTVISUALIZATION, FEEDBACK_TOUCH_TAP,
                                        FEEDBACK_TOUCH_DOUBLETAP, FEEDBACK_TOUCH_PRESSANDHOLD,
                                        FEEDBACK_TOUCH_RIGHTTAP, FEEDBACK_GESTURE_PRESSANDTAP };
        BOOL off = FALSE;
        for (FEEDBACK_TYPE t : types) SetWindowFeedbackSetting(hwnd, t, 0, sizeof(off), &off);
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
                                nullptr, instance, this);
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
    if (msg == WM_NCCREATE)
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
    auto* self = reinterpret_cast<CornerButton*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg)
    {
    case WM_POINTERDOWN:
    case WM_POINTERUP:
    case WM_POINTERUPDATE:
    case WM_POINTERCAPTURECHANGED:
        if (self) self->OnPointer(msg, wParam);
        return 0; // handled: don't let it turn into mouse input
    case WM_POINTERACTIVATE:
        return PA_NOACTIVATE;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void CornerButton::OnPointer(UINT msg, WPARAM wParam)
{
    const UINT32 id = GET_POINTERID_WPARAM(wParam);
    POINTER_INFO info{};
    if (!GetPointerInfo(id, &info)) return;

    if (msg == WM_POINTERDOWN)
    {
        pointerId_ = id;
        downAt_ = info.ptPixelLocation;
        downTick_ = GetTickCount64();
        return;
    }
    if (msg == WM_POINTERUP && id == pointerId_ && downTick_ != 0)
    {
        const bool quick = GetTickCount64() - downTick_ <= kMaxTapMs;
        const bool still = std::labs(info.ptPixelLocation.x - downAt_.x) <= kMaxTapTravel &&
                           std::labs(info.ptPixelLocation.y - downAt_.y) <= kMaxTapTravel;
        if (quick && still) PostMessageW(notify_, tapMessage_, 0, 0);
    }
    if (msg == WM_POINTERUP || msg == WM_POINTERCAPTURECHANGED) downTick_ = 0;
}
