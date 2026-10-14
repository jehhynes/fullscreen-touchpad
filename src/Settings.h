#pragma once

#include <string>

// User-tunable settings, persisted as key=value lines in %APPDATA%\VirtualTouchpad\settings.ini.
// Distances are pixels at 100% display scaling and are scaled by the monitor DPI at runtime.
struct Settings
{
    // Pointer movement
    double Sensitivity = 1.2;       // cursor px per finger px at slow speeds
    double Acceleration = 0.6;      // extra gain per (finger px/ms) of speed
    double MaxAccelSpeed = 3.0;     // finger speed (px/ms) where acceleration stops increasing

    // Taps
    bool TapToClick = true;
    double TapMaxMs = 220;          // max duration of a one-finger tap
    double MultiTapMaxMs = 350;     // max duration of a 2/3/4-finger tap
    double TapSlop = 12;            // max movement for a touch to still count as a tap
    double DoubleTapWindowMs = 300; // tap, then touch-and-move within this window, starts a drag

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

    static std::wstring FilePath();
    static Settings Load();
    void Save() const;
};
