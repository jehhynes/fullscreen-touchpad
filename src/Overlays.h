#pragma once

#include <windows.h>

#include <string>
#include <vector>

// Thin coloured strips along every monitor's edges, shown while the touchpad is active.
// The strips are click-through and never take focus.
class ActiveBorder
{
public:
    ~ActiveBorder() { Hide(); }
    void Show(double thicknessAt96Dpi);
    void Hide();

private:
    std::vector<HWND> strips_;
};

// Short-lived "Touchpad on/off" label near the bottom of the screen.
class Osd
{
public:
    ~Osd();
    void Flash(const std::wstring& text, POINT anchor);

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void Paint(HWND hwnd);

    HWND hwnd_ = nullptr;
    std::wstring text_;
    double scale_ = 1.0;
};

// Registers the window class shared by all overlays. Call once at startup.
void RegisterOverlayClass(HINSTANCE instance);

// DPI of the monitor nearest to a point.
unsigned DpiAt(POINT pt);
