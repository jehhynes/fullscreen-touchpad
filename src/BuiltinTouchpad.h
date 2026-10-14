#pragma once

#include "Layout.h"

#include <windows.h>

#include <climits>

// Opens and closes Windows' built-in virtual touchpad, and reshapes its window into an invisible
// full-screen touchpad (see Layout.h). Everything else, the pointer, taps, scrolling and gestures,
// is Windows' own.
class BuiltinTouchpad
{
public:
    struct Options
    {
        BYTE alpha = 1;         // 1/255: invisible, but still receives touch (0 would let touch through)
        int edgeGap = 2;
        int holeRadius = 4; // in 96-DPI pixels; scaled by the monitor DPI
    };

    explicit BuiltinTouchpad(Options options) : options_(options) {}

    // The touchpad's window, or null if it isn't open.
    static HWND FindTouchpadWindow();
    bool IsOpen() const { return FindTouchpadWindow() != nullptr; }

    // Opens it through Windows' "ms-virtualtouchpad:" URL; does nothing if it's already open.
    // Works without the taskbar, which hides its touchpad button when the keyboard is detached.
    static bool Open();

    // Closes it the way its own close button does. Returns false if it isn't open.
    static bool Close();

    // Reshapes the touchpad window if it's open and not reshaped yet. Returns true if it's in shape.
    bool Apply();

    // The window currently reshaped, if any.
    HWND Shaped() const { return shaped_; }

    // Moves the pointer hole to the current cursor position.
    void UpdatePointerHole();

    // Puts the touchpad window back as it was (size, region, opacity). Used when it can't be closed,
    // so the user is never left with an invisible window covering the screen.
    void Restore();

private:
    layout::Params LayoutFor(HWND hwnd) const;

    Options options_;
    HWND shaped_ = nullptr; // the window we last reshaped
    layout::Params params_{};
    POINT lastCursor_{ LONG_MIN, LONG_MIN };

    HWND original_ = nullptr;
    RECT originalRect_{};
    LONG_PTR originalExStyle_ = 0;
};
