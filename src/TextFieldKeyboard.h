#pragma once

#include <windows.h>
#include <objbase.h>

#include <condition_variable>
#include <mutex>
#include <thread>

// Shows the on-screen touch keyboard when focus moves into an editable text field while the
// touchpad is on. Windows only does that by itself for touch taps, and touchpad taps are mouse
// clicks. The check uses UI Automation, which must not run on the UI thread, so it runs on a
// worker thread.
class TextFieldKeyboard
{
public:
    TextFieldKeyboard();
    ~TextFieldKeyboard();

    // Call when focus changes; the check happens shortly afterwards on the worker thread.
    void CheckFocusSoon();

private:
    void Worker();

    std::mutex mutex_;
    std::condition_variable cv_;
    bool pending_ = false;
    bool stop_ = false;
    bool finished_ = false;
    std::thread thread_;
};
