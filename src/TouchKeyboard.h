#pragma once

#include <windows.h>
#include <shobjidl_core.h>
#include <wrl/client.h>

#include <condition_variable>
#include <mutex>
#include <thread>

// Brings up the Windows touch keyboard when a touchpad tap focuses a text field.
// Windows only does this by itself for real touch taps, and ours arrive as mouse clicks.
class TouchKeyboard
{
public:
    TouchKeyboard();
    ~TouchKeyboard();

    // Call after a tap-click. Focus moves asynchronously and UI Automation calls can block
    // on a busy app, so the check runs on a worker thread.
    void CheckFocusSoon();

    // Whether the touch keyboard (input pane) is on screen. Call from a COM-initialised thread.
    bool IsVisible();

private:
    void Worker();

    Microsoft::WRL::ComPtr<IFrameworkInputPane> pane_; // UI thread's instance
    std::mutex mutex_;
    std::condition_variable cv_;
    bool pending_ = false;
    bool stop_ = false;
    std::thread thread_;
};
