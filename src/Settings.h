#pragma once

#include <string>

// User settings, stored as key=value lines in %APPDATA%\VirtualTouchpad\settings.ini.
struct Settings
{
    bool StartEnabled = false;   // open the touchpad when the app starts
    double ToggleFingers = 3;    // fingers for the on/off tap
    double ToggleMaxMs = 700;    // longest touch that still counts as that tap
    double Opacity = 1;          // touchpad opacity, 1..255 (1 = invisible; 0 would stop it receiving touch)
    double EdgeGap = 2;          // uncovered pixels at the left, right and bottom edges, for edge swipes
    double PointerHole = 4;      // half-size of the uncovered square under the pointer, in pixels at 100% scaling
    bool ShowOsd = true;         // brief "Touchpad on/off" label when toggling
    bool ShowKeyboardForTextFields = true; // open the touch keyboard when a text field gets focus (no hardware keyboard)
    double KeyboardCorner = 64;  // size of the bottom-right corner that toggles the keyboard when tapped (0 = off)
    bool LogTouch = false;       // diagnostics: log each touch to %LOCALAPPDATA%\VirtualTouchpad\touch.log

    static std::wstring FilePath();
    static Settings Load();
    void Save() const;
};
