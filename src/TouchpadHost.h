#pragma once

#include <windows.h>

#include <cstdint>
#include <initializer_list>

// Everything the gesture engine needs from the outside world: time, timers, the screen,
// and somewhere to send mouse/keyboard input. The app implements it with Win32 calls;
// tests implement it with a fake clock and a recorder.
class TouchpadHost
{
public:
    virtual ~TouchpadHost() = default;

    // Environment
    virtual int64_t NowMs() = 0;
    virtual POINT CursorPos() = 0;
    virtual RECT MonitorRectAt(POINT p) = 0;          // bounds of the monitor nearest p
    virtual void StartTimer(int id, unsigned ms) = 0; // repeating: OnTimer(id) every ms until stopped
    virtual void StopTimer(int id) = 0;

    // Output. Pointer actions carry their position because Windows moves the real cursor
    // to every touch point; the move and the action must be delivered together.
    virtual void MoveCursorTo(POINT at) = 0;
    virtual void LeftDown(POINT at) = 0;
    virtual void LeftUp(POINT at) = 0;
    virtual void LeftClick(POINT at) = 0;
    virtual void RightClick(POINT at) = 0;
    virtual void MiddleClick(POINT at) = 0;
    virtual void Wheel(POINT at, int delta) = 0;
    virtual void HWheel(POINT at, int delta) = 0;
    virtual void KeyDown(WORD vk) = 0;
    virtual void KeyUp(WORD vk) = 0;
    virtual void Chord(std::initializer_list<WORD> keys) = 0; // press in order, release in reverse

    // Notifications
    virtual void ToggleRequested() = 0; // four-finger tap
    virtual void LeftClicked() = 0;     // a tap produced a left click
};
