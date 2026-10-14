#pragma once

#include <windows.h>

// Where to put Windows' built-in touchpad window so it becomes an invisible full-screen touchpad.
//
// The window is made taller than the screen and shifted up, so its title bar (with the close
// button) sits above the screen and its two click buttons sit below it. Its window region then
// limits the touchable part to the work area, minus thin strips at the left, right and bottom
// edges (so Windows' edge swipes still reach the shell), minus the on-screen touch keyboard while
// it's showing, and minus a small hole under the mouse pointer (so clicks, hover and scrolling at
// the pointer reach the app underneath).
namespace layout
{
    struct Params
    {
        RECT monitor{};        // monitor bounds, physical pixels
        RECT work{};           // monitor work area (excludes the taskbar)
        double scale = 1.0;    // monitor DPI / 96
        int titleOffsetDip = 40;   // how far the title bar is pushed above the screen
        int extraBottomDip = 350;  // how far the click buttons are pushed below the work area
        int edgeGap = 2;           // uncovered strip at the left, right and bottom edges (pixels)
        int holeRadius = 3;        // half-size of the uncovered square under the pointer (pixels)
        RECT keyboard{};           // on-screen touch keyboard, left uncovered so it can be typed on (empty if hidden)
        int cornerDip = 64;        // size of the keyboard-toggle corner at the screen's bottom right (0 = none)
    };

    // Window rectangle, in screen coordinates.
    RECT WindowRect(const Params& p);

    // The touchable rectangle, in screen coordinates (before cutting out the pointer hole).
    RECT TouchRect(const Params& p);

    // The pointer hole, in screen coordinates. Empty if the pointer is outside the touchable area.
    RECT PointerHole(const Params& p, POINT cursor);

    // The keyboard-toggle corner (bottom right of the monitor, inside the edge strips, over the
    // taskbar's corner if the taskbar is there), in screen coordinates. It's cut out of the
    // touchpad so a tap there reaches the app's own corner button.
    RECT CornerRect(const Params& p);

    // Builds the window region (in window coordinates) for SetWindowRgn. Caller owns the HRGN.
    HRGN CreateRegion(const Params& p, POINT cursor);
}
