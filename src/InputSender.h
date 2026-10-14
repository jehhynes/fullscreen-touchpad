#pragma once

#include <windows.h>
#include <initializer_list>

// Thin wrappers around SendInput for synthesizing mouse and keyboard events.
//
// Windows moves the real cursor to wherever the screen is touched, so every pointer
// action carries the position it applies at. The move and the action go out in a single
// SendInput batch, so nothing can land in between.
namespace input
{
    void MoveCursorTo(POINT at);

    void LeftDown(POINT at);
    void LeftUp(POINT at);
    void LeftClick(POINT at);
    void RightClick(POINT at);
    void MiddleClick(POINT at);

    void Wheel(POINT at, int delta);
    void HWheel(POINT at, int delta);

    void KeyDown(WORD vk);
    void KeyUp(WORD vk);

    // Presses the keys in order, then releases them in reverse order.
    void Chord(std::initializer_list<WORD> keys);
}
