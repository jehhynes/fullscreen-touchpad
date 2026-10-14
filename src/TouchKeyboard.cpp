#include "TouchKeyboard.h"

#include <shellapi.h>
#include <UIAutomation.h>

using Microsoft::WRL::ComPtr;

namespace
{
    // Undocumented, but the long-standing way to show the touch keyboard (TabTip) from a desktop app.
    MIDL_INTERFACE("37c994e7-432b-4834-a2f7-dce1f13b834b")
    ITipInvocation : public IUnknown
    {
        virtual HRESULT STDMETHODCALLTYPE Toggle(HWND wnd) = 0;
    };

    constexpr CLSID CLSID_UIHostNoLaunch = { 0x4CE576FA, 0x83DC, 0x4F88, { 0x95, 0x1C, 0x9D, 0x07, 0x82, 0xB4, 0xE3, 0x76 } };

    bool PaneVisible(IFrameworkInputPane* pane)
    {
        RECT rc{};
        return pane && SUCCEEDED(pane->Location(&rc)) && rc.right > rc.left && rc.bottom > rc.top;
    }

    ComPtr<IFrameworkInputPane> CreatePane()
    {
        ComPtr<IFrameworkInputPane> pane;
        CoCreateInstance(CLSID_FrameworkInputPane, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pane));
        return pane;
    }

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

        // Documents: editors (Word, rich text, contenteditable) support TextEdit; a web page's
        // own read-only document does not.
        ComPtr<IUnknown> textEdit;
        return SUCCEEDED(el->GetCurrentPattern(UIA_TextEditPatternId, &textEdit)) && textEdit;
    }

    void LaunchTabTip()
    {
        wchar_t path[MAX_PATH];
        if (ExpandEnvironmentStringsW(L"%CommonProgramW6432%\\microsoft shared\\ink\\TabTip.exe", path, MAX_PATH))
            ShellExecuteW(nullptr, L"open", path, nullptr, nullptr, SW_SHOWNOACTIVATE);
    }

    void ShowKeyboard()
    {
        ComPtr<ITipInvocation> tip;
        HRESULT hr = CoCreateInstance(CLSID_UIHostNoLaunch, nullptr, CLSCTX_INPROC_HANDLER | CLSCTX_LOCAL_SERVER,
                                      IID_PPV_ARGS(&tip));
        if (FAILED(hr))
        {
            // The keyboard host isn't running yet; start it and try again.
            LaunchTabTip();
            Sleep(500);
            hr = CoCreateInstance(CLSID_UIHostNoLaunch, nullptr, CLSCTX_INPROC_HANDLER | CLSCTX_LOCAL_SERVER,
                                  IID_PPV_ARGS(&tip));
        }
        if (SUCCEEDED(hr)) tip->Toggle(GetDesktopWindow());
    }
}

TouchKeyboard::TouchKeyboard()
    : thread_([this] { Worker(); })
{
}

TouchKeyboard::~TouchKeyboard()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cv_.notify_one();
    thread_.join();
}

void TouchKeyboard::CheckFocusSoon()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_ = true;
    }
    cv_.notify_one();
}

bool TouchKeyboard::IsVisible()
{
    if (!pane_) pane_ = CreatePane();
    return PaneVisible(pane_.Get());
}

void TouchKeyboard::Worker()
{
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return;
    {
        ComPtr<IUIAutomation> uia;
        CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia));
        ComPtr<IFrameworkInputPane> pane = CreatePane();

        for (;;)
        {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return pending_ || stop_; });
                if (stop_) break;
                pending_ = false;
            }
            if (!uia) continue;

            // The clicked app moves focus asynchronously; give it a few chances.
            for (int attempt = 0; attempt < 3; ++attempt)
            {
                Sleep(120);
                if (FocusIsEditable(uia.Get()))
                {
                    if (!PaneVisible(pane.Get())) ShowKeyboard();
                    break;
                }
            }
        }
    }
    CoUninitialize();
}
