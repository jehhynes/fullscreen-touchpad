#include "Win32TouchpadHost.h"
#include "InputSender.h"

#include <chrono>

int64_t Win32TouchpadHost::NowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

POINT Win32TouchpadHost::CursorPos()
{
    POINT p{};
    GetCursorPos(&p);
    return p;
}

RECT Win32TouchpadHost::MonitorRectAt(POINT p)
{
    MONITORINFO mi{ sizeof(mi) };
    if (GetMonitorInfoW(MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST), &mi)) return mi.rcMonitor;
    const int left = GetSystemMetrics(SM_XVIRTUALSCREEN), top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    return RECT{ left, top, left + GetSystemMetrics(SM_CXVIRTUALSCREEN), top + GetSystemMetrics(SM_CYVIRTUALSCREEN) };
}

void Win32TouchpadHost::StartTimer(int id, unsigned ms) { SetTimer(hwnd, kTimerBase + id, ms, nullptr); }
void Win32TouchpadHost::StopTimer(int id) { KillTimer(hwnd, kTimerBase + id); }

void Win32TouchpadHost::MoveCursorTo(POINT at) { input::MoveCursorTo(at); }
void Win32TouchpadHost::LeftDown(POINT at) { input::LeftDown(at); }
void Win32TouchpadHost::LeftUp(POINT at) { input::LeftUp(at); }
void Win32TouchpadHost::LeftClick(POINT at) { input::LeftClick(at); }
void Win32TouchpadHost::RightClick(POINT at) { input::RightClick(at); }
void Win32TouchpadHost::MiddleClick(POINT at) { input::MiddleClick(at); }
void Win32TouchpadHost::Wheel(POINT at, int delta) { input::Wheel(at, delta); }
void Win32TouchpadHost::HWheel(POINT at, int delta) { input::HWheel(at, delta); }
void Win32TouchpadHost::KeyDown(WORD vk) { input::KeyDown(vk); }
void Win32TouchpadHost::KeyUp(WORD vk) { input::KeyUp(vk); }
void Win32TouchpadHost::Chord(std::initializer_list<WORD> keys) { input::Chord(keys); }
