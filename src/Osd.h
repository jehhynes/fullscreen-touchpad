#pragma once

#include <windows.h>

#include <string>

// A short-lived, click-through "Touchpad on/off" label near the bottom of the primary monitor.
// Needed because the touchpad itself is invisible.
class Osd
{
public:
    ~Osd();
    void Flash(const std::wstring& text);

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void Paint();

    HWND hwnd_ = nullptr;
    std::wstring text_;
    double scale_ = 1.0;
};
