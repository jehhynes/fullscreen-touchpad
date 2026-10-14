#pragma once

#include <windows.h>
#include <objbase.h>
#include <UIAutomation.h>
#include <wrl/client.h>

#include <condition_variable>
#include <mutex>
#include <thread>

// Presses the taskbar's "Virtual Touchpad" button, which toggles Windows' built-in touchpad.
//
// UI Automation client calls must not run on the app's UI thread (they can deadlock with the
// app's own windows; it hung the app on exit), so presses run on a dedicated worker thread.
class TaskbarButton
{
public:
    // After each press, `resultMessage` is posted to `notify` with wParam = the step that failed
    // (0 = success; see kStep*) and lParam = the HRESULT.
    enum Step : int { kStepCreate = 1, kStepTaskbar, kStepFind, kStepPattern, kStepInvoke };
    TaskbarButton(HWND notify, UINT resultMessage);
    ~TaskbarButton();

    // Queues a press and returns immediately.
    void Press();

private:
    void Worker();
    HRESULT DoPress(int& step);
    HRESULT Find(int& step);

    HWND notify_;
    UINT resultMessage_;

    std::mutex mutex_;
    std::condition_variable cv_;
    unsigned requested_ = 0, done_ = 0;
    bool stop_ = false;
    bool finished_ = false; // the worker has left its loop
    std::thread thread_;

    // Worker thread only. Finding the button in the taskbar's UI Automation tree is slow, so keep it.
    Microsoft::WRL::ComPtr<IUIAutomation> uia_;
    Microsoft::WRL::ComPtr<IUIAutomationInvokePattern> button_;
};
