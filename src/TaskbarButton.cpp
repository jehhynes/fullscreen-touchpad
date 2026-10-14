#include "TaskbarButton.h"

using Microsoft::WRL::ComPtr;

TaskbarButton::TaskbarButton(HWND notify, UINT resultMessage)
    : notify_(notify), resultMessage_(resultMessage), thread_([this] { Worker(); })
{
}

TaskbarButton::~TaskbarButton()
{
    std::unique_lock<std::mutex> lock(mutex_);
    stop_ = true;
    cv_.notify_all();
    // A UI Automation call into a hung Explorer can block for a long time; don't hang the app's exit
    // on it. The process ends right after this anyway.
    if (cv_.wait_for(lock, std::chrono::seconds(1), [&] { return finished_; }))
    {
        lock.unlock();
        thread_.join();
    }
    else
    {
        lock.unlock();
        thread_.detach();
    }
}

void TaskbarButton::Press()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++requested_;
    }
    cv_.notify_all();
}

void TaskbarButton::Worker()
{
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    for (;;)
    {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [&] { return stop_ || done_ < requested_; });
            if (stop_) break;
        }
        int step = 0;
        const HRESULT hr = DoPress(step);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++done_;
        }
        PostMessageW(notify_, resultMessage_, WPARAM(step), LPARAM(hr));
    }
    button_.Reset();
    uia_.Reset();
    if (SUCCEEDED(com)) CoUninitialize();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        finished_ = true;
    }
    cv_.notify_all();
}

HRESULT TaskbarButton::DoPress(int& step)
{
    // The cached button goes stale if Explorer restarts; then find it again.
    step = 0;
    if (button_ && SUCCEEDED(button_->Invoke())) return S_OK;
    const HRESULT found = Find(step);
    if (FAILED(found)) return found;
    step = kStepInvoke;
    const HRESULT hr = button_->Invoke();
    if (SUCCEEDED(hr)) step = 0;
    return hr;
}

HRESULT TaskbarButton::Find(int& step)
{
    button_.Reset();
    step = kStepCreate;
    HRESULT hr = S_OK;
    if (!uia_ &&
        FAILED(hr = CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia_))))
        return hr;

    step = kStepTaskbar;
    HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
    ComPtr<IUIAutomationElement> trayElement;
    if (!tray) return E_HANDLE;
    if (FAILED(hr = uia_->ElementFromHandle(tray, &trayElement))) return hr;

    // The system tray's "Virtual Touchpad" button (system icons have the id "SystemTrayIcon").
    VARIANT id{};
    id.vt = VT_BSTR;
    id.bstrVal = SysAllocString(L"SystemTrayIcon");
    VARIANT name{};
    name.vt = VT_BSTR;
    name.bstrVal = SysAllocString(L"Virtual Touchpad");
    ComPtr<IUIAutomationCondition> byId, byName, both;
    uia_->CreatePropertyCondition(UIA_AutomationIdPropertyId, id, &byId);
    uia_->CreatePropertyCondition(UIA_NamePropertyId, name, &byName);
    VariantClear(&id);
    VariantClear(&name);
    if (!byId || !byName) return E_FAIL;
    if (FAILED(hr = uia_->CreateAndCondition(byId.Get(), byName.Get(), &both))) return hr;

    step = kStepFind;
    ComPtr<IUIAutomationElement> button;
    if (FAILED(hr = trayElement->FindFirst(TreeScope_Descendants, both.Get(), &button))) return hr;
    if (!button) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);

    step = kStepPattern;
    if (FAILED(hr = button->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&button_))))
    {
        button_.Reset();
        return hr;
    }
    return button_ ? S_OK : E_NOINTERFACE;
}
