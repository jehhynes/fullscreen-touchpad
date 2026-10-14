#include "App.h"
#include "Messages.h"

#include <windows.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    // Launching a second copy toggles the running one, so a desktop/taskbar shortcut works as a switch.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\VirtualTouchpad.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (HWND existing = FindWindowW(kHostWindowClass, nullptr))
            PostMessageW(existing, WM_APP_TOGGLE, 0, 0);
        return 0;
    }

    // COM is used for the touch keyboard (input pane visibility, showing it).
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    int result = 1;
    {
        App app(instance);
        if (app.Create()) result = app.Run();
    }

    if (SUCCEEDED(com)) CoUninitialize();
    if (mutex) CloseHandle(mutex);
    return result;
}
