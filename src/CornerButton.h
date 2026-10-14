#pragma once

#include <windows.h>

// An invisible window over the bottom-right corner of the screen, shown while the touchpad is on.
// It only absorbs touches, so they don't reach the app underneath; the app recognises the corner
// gestures (hold, double tap) from raw touchscreen input, which arrives while the finger is still
// down (see CornerGesture). The touchpad leaves that corner uncovered (see layout::CornerRect).
class CornerButton
{
public:
    CornerButton() = default;
    ~CornerButton();
    CornerButton(const CornerButton&) = delete;
    CornerButton& operator=(const CornerButton&) = delete;

    // Shows the window over `rect` (screen coordinates), or hides it if `rect` is empty.
    // Does nothing if that's already the case, unless `raise` (re-assert it's above the touchpad).
    void Place(const RECT& rect, bool raise = false);
    bool Visible() const { return visible_; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND hwnd_ = nullptr;
    RECT placed_{};
    bool visible_ = false;
};
