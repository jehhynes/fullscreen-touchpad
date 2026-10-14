#pragma once

#include <windows.h>

// An invisible button in the bottom-right corner of the screen, shown while the touchpad is on.
// A quick tap on it posts `tapMessage` to the app (which toggles the on-screen keyboard).
// The touchpad leaves that corner uncovered (see layout::CornerRect), so touches reach it.
class CornerButton
{
public:
    CornerButton(HWND notify, UINT tapMessage) : notify_(notify), tapMessage_(tapMessage) {}
    ~CornerButton();

    // Shows the button over `rect` (screen coordinates), or hides it if `rect` is empty.
    // Does nothing if that's already the case, unless `raise` (re-assert it's above the touchpad).
    void Place(const RECT& rect, bool raise = false);

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void OnPointer(UINT msg, WPARAM wParam);

    HWND notify_;
    UINT tapMessage_;
    HWND hwnd_ = nullptr;
    RECT placed_{};
    bool visible_ = false;

    // The touch in progress, to tell a tap from a press or a slide.
    UINT32 pointerId_ = 0;
    POINT downAt_{};
    ULONGLONG downTick_ = 0;
};
