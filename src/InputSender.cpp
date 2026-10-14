#include "InputSender.h"

#include <algorithm>
#include <vector>

namespace
{
    INPUT Mouse(LONG dx, LONG dy, int data, DWORD flags)
    {
        INPUT in{};
        in.type = INPUT_MOUSE;
        in.mi.dx = dx;
        in.mi.dy = dy;
        in.mi.mouseData = static_cast<DWORD>(data);
        in.mi.dwFlags = flags;
        return in;
    }

    INPUT Key(WORD vk, bool up)
    {
        INPUT in{};
        in.type = INPUT_KEYBOARD;
        in.ki.wVk = vk;
        in.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
        if (vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN || vk == VK_LWIN)
            in.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        return in;
    }

    void Send(std::initializer_list<INPUT> inputs)
    {
        std::vector<INPUT> v(inputs);
        SendInput(static_cast<UINT>(v.size()), v.data(), sizeof(INPUT));
    }
}

namespace input
{
    void MoveCursorTo(int x, int y)
    {
        const int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
        const int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
        const int width = std::max(1, GetSystemMetrics(SM_CXVIRTUALSCREEN));
        const int height = std::max(1, GetSystemMetrics(SM_CYVIRTUALSCREEN));

        // Windows maps normalized -> pixel as (n * size) >> 16, so round up to land on the exact pixel.
        const long long nx = (static_cast<long long>(x - left) * 65536 + width - 1) / width;
        const long long ny = (static_cast<long long>(y - top) * 65536 + height - 1) / height;

        Send({ Mouse(static_cast<LONG>(std::clamp(nx, 0LL, 65535LL)),
                     static_cast<LONG>(std::clamp(ny, 0LL, 65535LL)), 0,
                     MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK) });
    }

    void LeftDown() { Send({ Mouse(0, 0, 0, MOUSEEVENTF_LEFTDOWN) }); }
    void LeftUp() { Send({ Mouse(0, 0, 0, MOUSEEVENTF_LEFTUP) }); }

    void LeftClick() { Send({ Mouse(0, 0, 0, MOUSEEVENTF_LEFTDOWN), Mouse(0, 0, 0, MOUSEEVENTF_LEFTUP) }); }
    void RightClick() { Send({ Mouse(0, 0, 0, MOUSEEVENTF_RIGHTDOWN), Mouse(0, 0, 0, MOUSEEVENTF_RIGHTUP) }); }
    void MiddleClick() { Send({ Mouse(0, 0, 0, MOUSEEVENTF_MIDDLEDOWN), Mouse(0, 0, 0, MOUSEEVENTF_MIDDLEUP) }); }

    void Wheel(int delta)
    {
        if (delta != 0) Send({ Mouse(0, 0, delta, MOUSEEVENTF_WHEEL) });
    }

    void HWheel(int delta)
    {
        if (delta != 0) Send({ Mouse(0, 0, delta, MOUSEEVENTF_HWHEEL) });
    }

    void KeyDown(WORD vk) { Send({ Key(vk, false) }); }
    void KeyUp(WORD vk) { Send({ Key(vk, true) }); }

    void Chord(std::initializer_list<WORD> keys)
    {
        std::vector<INPUT> v;
        for (WORD k : keys) v.push_back(Key(k, false));
        for (auto it = std::rbegin(keys); it != std::rend(keys); ++it) v.push_back(Key(*it, true));
        SendInput(static_cast<UINT>(v.size()), v.data(), sizeof(INPUT));
    }
}
