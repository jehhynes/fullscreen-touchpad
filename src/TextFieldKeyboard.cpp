#include "TextFieldKeyboard.h"

#include <shobjidl_core.h>
#include <UIAutomation.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace
{
    // Undocumented but long-standing way to show the touch keyboard from a desktop app; it toggles,
    // so only call it when the keyboard is hidden.
    MIDL_INTERFACE("37c994e7-432b-4834-a2f7-dce1f13b834b")
    ITipInvocation : public IUnknown
    {
        virtual HRESULT STDMETHODCALLTYPE Toggle(HWND wnd) = 0;
    };
    constexpr CLSID CLSID_UIHostNoLaunch = { 0x4CE576FA, 0x83DC, 0x4F88, { 0x95, 0x1C, 0x9D, 0x07, 0x82, 0xB4, 0xE3, 0x76 } };

    bool KeyboardVisible(IFrameworkInputPane* pane)
    {
        RECT rc{};
        return pane && SUCCEEDED(pane->Location(&rc)) && rc.right > rc.left && rc.bottom > rc.top;
    }

    // With a hardware keyboard attached (not in slate mode), leave the touch keyboard alone.
    bool HardwareKeyboardAttached() { return GetSystemMetrics(SM_CONVERTIBLESLATEMODE) != 0; }

    bool FocusIsEditable(IUIAutomation* uia)
    {
        ComPtr<IUIAutomationElement> el;
        if (FAILED(uia->GetFocusedElement(&el)) || !el) return false;

        CONTROLTYPEID type = 0;
        if (FAILED(el->get_CurrentControlType(&type))) return false;
        if (type != UIA_EditControlTypeId && type != UIA_DocumentControlTypeId) return false;

        ComPtr<IUIAutomationValuePattern> value;
        if (SUCCEEDED(el->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&value))) && value)
        {
            BOOL readOnly = FALSE;
            if (SUCCEEDED(value->get_CurrentIsReadOnly(&readOnly))) return !readOnly;
        }
        if (type == UIA_EditControlTypeId) return true;

        // Documents: editors support TextEdit; a web page's own read-only document does not.
        ComPtr<IUnknown> textEdit;
        return SUCCEEDED(el->GetCurrentPattern(UIA_TextEditPatternId, &textEdit)) && textEdit;
    }
}

TextFieldKeyboard::TextFieldKeyboard() : thread_([this] { Worker(); }) {}

TextFieldKeyboard::~TextFieldKeyboard()
{
    std::unique_lock<std::mutex> lock(mutex_);
    stop_ = true;
    cv_.notify_all();
    // Don't hang the app's exit on a UI Automation call into a busy app.
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

void TextFieldKeyboard::CheckFocusSoon()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_ = true;
    }
    cv_.notify_all();
}

void TextFieldKeyboard::Worker()
{
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    {
        ComPtr<IUIAutomation> uia;
        CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia));
        ComPtr<IFrameworkInputPane> pane;
        CoCreateInstance(CLSID_FrameworkInputPane, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pane));

        for (;;)
        {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [&] { return stop_ || pending_; });
                if (stop_) break;
                pending_ = false;
            }
            Sleep(100); // let focus settle (a click can move it more than once)
            if (!uia || HardwareKeyboardAttached() || KeyboardVisible(pane.Get()) || !FocusIsEditable(uia.Get()))
                continue;

            ComPtr<ITipInvocation> tip;
            if (SUCCEEDED(CoCreateInstance(CLSID_UIHostNoLaunch, nullptr, CLSCTX_INPROC_HANDLER | CLSCTX_LOCAL_SERVER,
                                           IID_PPV_ARGS(&tip))))
                tip->Toggle(GetDesktopWindow());
        }
    }
    if (SUCCEEDED(com)) CoUninitialize();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        finished_ = true;
    }
    cv_.notify_all();
}
