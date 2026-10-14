#include "App.h"

#include <windows.h>
#include <objbase.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    // A second launch toggles the running instance, so a shortcut works as an on/off switch.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\VirtualTouchpad.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (HWND existing = FindWindowW(kAppWindowClass, nullptr)) PostMessageW(existing, WM_APP_TOGGLE, 0, 0);
        return 0;
    }

    // COM for UI Automation (pressing the taskbar's touchpad button).
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
