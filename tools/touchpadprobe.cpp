// Touchpad probe: checks whether a synthetic precision touchpad (CreateSyntheticPointerDevice2 with
// PT_TOUCHPAD, Windows 11) works on this machine: does a one-finger swipe move the cursor, and does a
// tap click? The tap is aimed at the probe's own window, so nothing else gets clicked.
//
// Exit code: 0 = cursor moved and tap clicked, 2 = partial, 1 = API unavailable / failed.

#include <windows.h>

#include <cstdio>
#include <cstdlib>

namespace
{
    // From learn.microsoft.com/windows/win32/input-precisiontouchpad/createsyntheticpointerdevice2
    // (not yet in the installed SDK headers).
    enum : ULONG { SDCO_NONE = 0x0, SDCO_PHYSICAL_SIZE = 0x1, SDCO_TOUCHPAD_GESTURE_ONLY = 0x2 };

    struct SyntheticDeviceCreationParams
    {
        POINTER_INPUT_TYPE pointerType;
        ULONG maxCount;
        POINTER_FEEDBACK_MODE feedbackMode;
        HMONITOR hMonitor;
        ULONG deviceWidth;  // himetric
        ULONG deviceHeight; // himetric
        ULONG options;
    };

    using CreateDevice2Fn = HSYNTHETICPOINTERDEVICE(WINAPI*)(SyntheticDeviceCreationParams*);
    constexpr POINTER_INPUT_TYPE kTouchpad = POINTER_INPUT_TYPE(5); // PT_TOUCHPAD

    int g_clicks = 0;

    LRESULT CALLBACK ProbeProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_LBUTTONDOWN) ++g_clicks;
        if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    void Pump(DWORD ms)
    {
        const ULONGLONG end = GetTickCount64() + ms;
        do
        {
            MSG m;
            while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&m);
            Sleep(1);
        } while (GetTickCount64() < end);
    }

    bool InjectFrame(HSYNTHETICPOINTERDEVICE device, POINT himetric, POINTER_FLAGS flags)
    {
        POINTER_TYPE_INFO info{};
        info.type = kTouchpad;
        info.touchInfo.pointerInfo.pointerType = kTouchpad;
        info.touchInfo.pointerInfo.pointerId = 0;
        // Precision touchpads treat contacts without "confidence" as palms and ignore them.
        info.touchInfo.pointerInfo.pointerFlags = flags | POINTER_FLAG_CONFIDENCE | POINTER_FLAG_PRIMARY;
        info.touchInfo.pointerInfo.ptHimetricLocation = himetric;
        if (InjectSyntheticPointerInput(device, &info, 1)) return true;
        std::printf("InjectSyntheticPointerInput failed: %lu (flags 0x%x)\n", GetLastError(), unsigned(flags));
        return false;
    }

    // One finger down at `start`, moving by `step` for `frames` frames 8 ms apart, then up.
    bool Stroke(HSYNTHETICPOINTERDEVICE device, POINT start, POINT step, int frames)
    {
        const POINTER_FLAGS down = POINTER_FLAG_DOWN | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT;
        const POINTER_FLAGS update = POINTER_FLAG_UPDATE | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT;
        POINT p = start;
        if (!InjectFrame(device, p, down)) return false;
        for (int i = 0; i < frames; ++i)
        {
            Pump(8);
            p.x += step.x;
            p.y += step.y;
            if (!InjectFrame(device, p, update)) return false;
        }
        Pump(8);
        return InjectFrame(device, p, POINTER_FLAG_UP);
    }
}

int main()
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    auto create = reinterpret_cast<CreateDevice2Fn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "CreateSyntheticPointerDevice2"));
    if (!create)
    {
        std::printf("CreateSyntheticPointerDevice2 not available\n");
        return 1;
    }

    SyntheticDeviceCreationParams params{};
    params.pointerType = kTouchpad;
    params.maxCount = 5;
    params.feedbackMode = POINTER_FEEDBACK_NONE;
    params.hMonitor = nullptr;
    params.deviceWidth = 10000; // 100 mm
    params.deviceHeight = 6000; // 60 mm
    params.options = SDCO_PHYSICAL_SIZE;
    HSYNTHETICPOINTERDEVICE device = create(&params);
    if (!device)
    {
        std::printf("CreateSyntheticPointerDevice2 failed: %lu\n", GetLastError());
        return 1;
    }
    std::printf("synthetic touchpad created\n");

    // 1. Swipe: 40 mm to the right.
    POINT before{}, after{};
    GetCursorPos(&before);
    const bool swiped = Stroke(device, POINT{ 2000, 3000 }, POINT{ 160, 0 }, 25);
    Pump(150);
    GetCursorPos(&after);
    CURSORINFO ci{ sizeof(ci) };
    GetCursorInfo(&ci);
    std::printf("swipe: cursor %ld,%ld -> %ld,%ld (moved %ld px), cursor flags %lu (1 = visible)\n", before.x,
                before.y, after.x, after.y, after.x - before.x, ci.flags);

    // 2. Tap on our own window: put the cursor in it, then a short stationary contact.
    const int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    WNDCLASSW wc{};
    wc.lpfnWndProc = ProbeProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH));
    wc.lpszClassName = L"VirtualTouchpadTouchpadProbe";
    RegisterClassW(&wc);
    HWND window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"probe",
                                  WS_POPUP | WS_VISIBLE, sw / 2 - 200, sh / 2 - 120, 400, 240, nullptr, nullptr,
                                  wc.hInstance, nullptr);
    Pump(200);
    SetCursorPos(sw / 2, sh / 2);
    Pump(100);
    const bool tapped = Stroke(device, POINT{ 5000, 3000 }, POINT{ 0, 0 }, 4);
    Pump(600); // touchpad tap-to-click is reported after a short delay
    std::printf("tap: probe window received %d left click(s)\n", g_clicks);

    DestroyWindow(window);
    DestroySyntheticPointerDevice(device);

    const bool moved = after.x - before.x > 20;
    if (!swiped || !tapped) return 1;
    return moved && g_clicks > 0 ? 0 : 2;
}
