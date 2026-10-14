#pragma once

#include <windows.h>
#include <objbase.h>

#include <condition_variable>
#include <mutex>
#include <thread>

// The on-screen touch keyboard, while the touchpad is on:
// - shows it when focus moves into an editable text field (Windows only does that by itself for
//   touch taps, and touchpad taps are mouse clicks);
// - toggles it on request (the corner tap), e.g. for Remote Desktop, where the remote PC's text
//   fields can't be detected.
// UI Automation must not run on the UI thread, so all of this runs on a worker thread.
class TouchKeyboard
{
public:
    TouchKeyboard();
    ~TouchKeyboard();

    // Call when focus changes; the check happens shortly afterwards on the worker thread.
    void CheckFocusSoon();

    // Shows the keyboard if it's hidden, hides it if it's showing.
    void ToggleSoon();

private:
    void Worker();

    std::mutex mutex_;
    std::condition_variable cv_;
    bool pending_ = false;
    bool toggle_ = false;
    bool stop_ = false;
    bool finished_ = false;
    std::thread thread_;
};
