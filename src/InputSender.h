#pragma once

#include <windows.h>
#include <initializer_list>

// Thin wrappers around SendInput for synthesizing mouse and keyboard events.
namespace input
{
    void MoveCursorTo(int x, int y);

    void LeftDown();
    void LeftUp();
    void LeftClick();
    void RightClick();
    void MiddleClick();

    void Wheel(int delta);
    void HWheel(int delta);

    void KeyDown(WORD vk);
    void KeyUp(WORD vk);

    // Presses the keys in order, then releases them in reverse order.
    void Chord(std::initializer_list<WORD> keys);
}
