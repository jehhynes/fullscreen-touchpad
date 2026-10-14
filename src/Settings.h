#pragma once

#include <string>

// User-tunable settings, persisted as key=value lines in %APPDATA%\VirtualTouchpad\settings.ini.
// Distances are pixels at 100% display scaling and are scaled by the monitor DPI at runtime.
struct Settings
{
    // Feed touches to a virtual precision touchpad so Windows' own touchpad stack does pointer
    // movement, clicks, scrolling and gestures (Windows 11). When false, or unavailable, the app's
    // own gesture engine and the settings below are used instead.
    bool NativeTouchpad = true;

    // Pointer movement
    double Sensitivity = 1.2;       // cursor px per finger px at slow speeds
    double Acceleration = 0.6;      // extra gain per (finger px/ms) of speed
    double MaxAccelSpeed = 3.0;     // finger speed (px/ms) where acceleration stops increasing

    // Taps
    bool TapToClick = true;
    double TapMaxMs = 220;          // max duration of a one-finger tap
    double MultiTapMaxMs = 350;     // max duration of a 2/3/4-finger tap
    double TapSlop = 12;            // max movement for a touch to still count as a tap
    double DoubleTapWindowMs = 250; // a tap's click waits this long for a second tap (double-click)
                                    // or a touch-and-hold/move (drag)
    bool LongPressRightClick = true;
    double LongPressMs = 600;       // hold one finger still this long to right click

    // Touch keyboard
    bool ShowKeyboardForTextFields = true; // open the touch keyboard when a tap focuses a text field
    bool TypeDirectlyOnKeyboard = true;    // while it's open, touch goes straight to the screen

    // Scrolling and zoom
    double ScrollSpeed = 2.0;       // wheel units per finger px (120 = one notch)
    bool NaturalScrolling = true;   // content follows fingers, like the Windows touchpad default
    bool KineticScrolling = true;
    double KineticFriction = 0.92;  // velocity multiplier per 16 ms
    bool PinchZoom = true;
    double ZoomSpeed = 900;         // wheel units per ln(pinch distance ratio)

    // Three/four-finger swipes
    double SwipeThreshold = 70;     // movement before a swipe triggers
    double AltTabStep = 140;        // movement per extra app while swiping three fingers sideways

    // Toggling and feedback
    bool StartEnabled = true;
    bool ShowBorder = true;         // thin glow around the screen edges while active
    double BorderThickness = 3;
    bool ShowOsd = true;            // brief "Touchpad on/off" label when toggling

    // Diagnostics
    bool DebugLog = false;          // log pointer/mouse messages to %LOCALAPPDATA%\VirtualTouchpad\debug.log

    static std::wstring FilePath();
    static Settings Load();
    void Save() const;
};
