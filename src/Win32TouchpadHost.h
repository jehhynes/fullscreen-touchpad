#pragma once

#include "TouchpadHost.h"

#include <functional>

// The real TouchpadHost: SendInput for output, window timers, and the actual screen.
class Win32TouchpadHost : public TouchpadHost
{
public:
    static constexpr UINT_PTR kTimerBase = 100; // engine timer id N -> window timer kTimerBase + N

    HWND hwnd = nullptr;                // receives WM_TIMER for engine timers
    std::function<void()> onToggle;
    std::function<void()> onLeftClick;

    int64_t NowMs() override;
    POINT CursorPos() override;
    RECT MonitorRectAt(POINT p) override;
    void StartTimer(int id, unsigned ms) override;
    void StopTimer(int id) override;

    void MoveCursorTo(POINT at) override;
    void LeftDown(POINT at) override;
    void LeftUp(POINT at) override;
    void LeftClick(POINT at) override;
    void RightClick(POINT at) override;
    void MiddleClick(POINT at) override;
    void Wheel(POINT at, int delta) override;
    void HWheel(POINT at, int delta) override;
    void KeyDown(WORD vk) override;
    void KeyUp(WORD vk) override;
    void Chord(std::initializer_list<WORD> keys) override;

    void ToggleRequested() override { if (onToggle) onToggle(); }
    void LeftClicked() override { if (onLeftClick) onLeftClick(); }
};
