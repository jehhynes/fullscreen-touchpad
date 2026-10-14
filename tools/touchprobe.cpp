// Touch probe: injects synthetic touch strokes and measures whether the real cursor jumps to the
// touch point (the flicker bug), without needing a person at the touchscreen.
//
// It shows its own small window under the injected strokes, so if touch is NOT captured by the
// touchpad app, the strokes land harmlessly on the probe instead of on another app.
//
// Usage: touchprobe [strokes=3]
// Exit code: 0 = cursor never seen at the touch point, 2 = it was (flicker reproduced), 1 = error.

#include <windows.h>
#include <mmsystem.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace
{
    int g_probeWindowTouches = 0;

    LRESULT CALLBACK ProbeProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_POINTERDOWN:
        case WM_POINTERUPDATE:
        case WM_POINTERUP:
            ++g_probeWindowTouches;
            return 0;
        case WM_POINTERACTIVATE:
            return PA_NOACTIVATE;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect(dc, &rc, static_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH)));
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(255, 255, 255));
            DrawTextW(dc, L"Virtual Touchpad touch probe", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            EndPaint(hwnd, &ps);
            return 0;
        }
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    const char* ShapeName(HCURSOR c)
    {
        if (!c) return "none";
        if (c == LoadCursorW(nullptr, IDC_ARROW)) return "arrow";
        if (c == LoadCursorW(nullptr, IDC_IBEAM)) return "ibeam";
        if (c == LoadCursorW(nullptr, IDC_WAIT)) return "WAIT";
        if (c == LoadCursorW(nullptr, IDC_APPSTARTING)) return "APPSTARTING";
        return "other";
    }

    struct Stats
    {
        int samples = 0;
        int atTouch = 0;        // cursor within 3 px of the finger
        int visibleAtTouch = 0; // ...and drawn (not hidden/suppressed)
        int hiddenSamples = 0;  // cursor suppressed or not showing
        int visibilityChanges = 0;
        int shapeChanges = 0;
        int positionJumps = 0;  // moves of more than 40 px between consecutive samples
        bool havePrev = false;
        CURSORINFO prev{};
        int printed = 0;
    };

    double NowMs()
    {
        LARGE_INTEGER f, c;
        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&c);
        return double(c.QuadPart) * 1000.0 / double(f.QuadPart);
    }

    bool Visible(const CURSORINFO& ci) { return (ci.flags & CURSOR_SHOWING) && !(ci.flags & 0x2 /* CURSOR_SUPPRESSED */); }

    void PumpAndSample(DWORD ms, POINT touch, Stats& stats)
    {
        static const double t0 = NowMs();
        const ULONGLONG end = GetTickCount64() + ms;
        do
        {
            MSG m;
            while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&m);
                DispatchMessageW(&m);
            }
            CURSORINFO ci{ sizeof(ci) };
            GetCursorInfo(&ci);
            ++stats.samples;
            if (!Visible(ci)) ++stats.hiddenSamples;
            if (std::abs(ci.ptScreenPos.x - touch.x) <= 3 && std::abs(ci.ptScreenPos.y - touch.y) <= 3)
            {
                ++stats.atTouch;
                if (Visible(ci)) ++stats.visibleAtTouch;
            }
            if (stats.havePrev)
            {
                const bool vis = Visible(ci) != Visible(stats.prev);
                const bool shape = ci.hCursor != stats.prev.hCursor;
                const bool jump = std::abs(ci.ptScreenPos.x - stats.prev.ptScreenPos.x) +
                                  std::abs(ci.ptScreenPos.y - stats.prev.ptScreenPos.y) > 40;
                stats.visibilityChanges += vis;
                stats.shapeChanges += shape;
                stats.positionJumps += jump;
                if ((vis || shape || jump) && stats.printed < 60)
                {
                    ++stats.printed;
                    std::printf("  %8.1f ms  pos %ld,%ld  %s  shape=%s  (touch %ld,%ld)\n", NowMs() - t0,
                                ci.ptScreenPos.x, ci.ptScreenPos.y, Visible(ci) ? "visible" : "HIDDEN",
                                ShapeName(ci.hCursor), touch.x, touch.y);
                }
            }
            stats.prev = ci;
            stats.havePrev = true;
            Sleep(1);
        } while (GetTickCount64() < end);
    }

    bool Inject(POINTER_TOUCH_INFO& contact, POINT p, POINTER_FLAGS flags)
    {
        contact.pointerInfo.ptPixelLocation = p;
        contact.pointerInfo.pointerFlags = flags;
        contact.rcContact = RECT{ p.x - 2, p.y - 2, p.x + 2, p.y + 2 };
        if (InjectTouchInput(1, &contact)) return true;
        std::printf("InjectTouchInput failed: %lu\n", GetLastError());
        return false;
    }
}

// Grabs a strip of the screen as 32-bit pixels.
static std::vector<DWORD> Capture(int x, int y, int w, int h)
{
    std::vector<DWORD> pixels(size_t(w) * h);
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader = BITMAPINFOHEADER{ sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB };
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, w, h, screen, x, y, SRCCOPY);
    std::memcpy(pixels.data(), bits, pixels.size() * sizeof(DWORD));
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return pixels;
}

static double ChangedFraction(const std::vector<DWORD>& a, const std::vector<DWORD>& b)
{
    size_t changed = 0;
    for (size_t i = 0; i < a.size(); ++i) changed += (a[i] & 0xFFFFFF) != (b[i] & 0xFFFFFF);
    return a.empty() ? 0 : double(changed) / double(a.size());
}

// "edge" mode: injects a swipe up from the bottom edge of the primary monitor and reports whether the
// bottom of the screen changed (the collapsed touch taskbar expanding), i.e. whether Windows treats
// injected touch as an edge gesture. Optional second arg: how far below the edge the swipe starts.
static int EdgeSwipeTest(int startBelowEdge)
{
    if (!InitializeTouchInjection(1, TOUCH_FEEDBACK_NONE))
    {
        std::printf("InitializeTouchInjection failed: %lu\n", GetLastError());
        return 1;
    }
    const int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    const int stripH = sh / 8;
    const auto before = Capture(0, sh - stripH, sw, stripH);
    POINTER_TOUCH_INFO contact{};
    contact.pointerInfo.pointerType = PT_TOUCH;
    contact.touchMask = TOUCH_MASK_CONTACTAREA | TOUCH_MASK_ORIENTATION | TOUCH_MASK_PRESSURE;
    contact.orientation = 90;
    contact.pressure = 32000;

    Stats ignored;
    POINT p{ sw / 2, sh - 1 - startBelowEdge };
    if (!Inject(contact, p, POINTER_FLAG_DOWN | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT)) return 1;
    for (int i = 1; i <= 20; ++i)
    {
        PumpAndSample(10, p, ignored);
        p.y -= 12;
        if (!Inject(contact, p, POINTER_FLAG_UPDATE | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT)) return 1;
    }
    if (!Inject(contact, p, POINTER_FLAG_UP)) return 1;

    PumpAndSample(700, p, ignored);
    const double changed = ChangedFraction(before, Capture(0, sh - stripH, sw, stripH));
    std::printf("bottom strip changed: %.1f%% -> %s\n", changed * 100,
                changed > 0.05 ? "EDGE GESTURE TRIGGERED by injected touch" : "no visible edge gesture");
    return changed > 0.05 ? 0 : 2;
}

// Saves a screen region as a 24-bit BMP, with the logical cursor drawn in (BitBlt omits it).
static void SaveScreenshot(const wchar_t* path, RECT r)
{
    const int w = r.right - r.left, h = r.bottom - r.top;
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader = BITMAPINFOHEADER{ sizeof(BITMAPINFOHEADER), w, h, 1, 24, BI_RGB };
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, w, h, screen, r.left, r.top, SRCCOPY | CAPTUREBLT);
    CURSORINFO ci{ sizeof(ci) };
    if (GetCursorInfo(&ci) && ci.hCursor && (ci.flags & CURSOR_SHOWING))
    {
        ICONINFO ii{};
        GetIconInfo(ci.hCursor, &ii);
        DrawIconEx(mem, ci.ptScreenPos.x - r.left - int(ii.xHotspot), ci.ptScreenPos.y - r.top - int(ii.yHotspot),
                   ci.hCursor, 0, 0, 0, nullptr, DI_NORMAL);
        if (ii.hbmMask) DeleteObject(ii.hbmMask);
        if (ii.hbmColor) DeleteObject(ii.hbmColor);
    }
    GdiFlush();
    const DWORD stride = DWORD((w * 3 + 3) & ~3), size = stride * DWORD(h);
    BITMAPFILEHEADER fh{ 0x4D42, DWORD(sizeof(fh) + sizeof(BITMAPINFOHEADER) + size), 0, 0,
                         DWORD(sizeof(fh) + sizeof(BITMAPINFOHEADER)) };
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"wb") == 0 && f)
    {
        fwrite(&fh, sizeof(fh), 1, f);
        fwrite(&bi.bmiHeader, sizeof(BITMAPINFOHEADER), 1, f);
        fwrite(bits, 1, size, f);
        fclose(f);
    }
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
}

// "hold" mode: injects a touch WITH default touch feedback, moves it, then holds it still, taking
// screenshots before the touch, while moving, and while held, into the directory given as argv[2].
static int HoldTest(const wchar_t* dir)
{
    if (!InitializeTouchInjection(1, TOUCH_FEEDBACK_DEFAULT))
    {
        std::printf("InitializeTouchInjection failed: %lu\n", GetLastError());
        return 1;
    }
    const RECT full{ GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                     GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
                     GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN) };
    auto shot = [&](const wchar_t* name) {
        wchar_t path[MAX_PATH];
        swprintf_s(path, L"%s\\%s.bmp", dir, name);
        SaveScreenshot(path, full);
    };

    POINTER_TOUCH_INFO contact{};
    contact.pointerInfo.pointerType = PT_TOUCH;
    contact.touchMask = TOUCH_MASK_CONTACTAREA | TOUCH_MASK_ORIENTATION | TOUCH_MASK_PRESSURE;
    contact.orientation = 90;
    contact.pressure = 32000;
    const POINTER_FLAGS kUpdate = POINTER_FLAG_UPDATE | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT;

    Stats stats;
    const int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    POINT p{ sw / 2 - 150, sh / 2 };
    shot(L"1-before");
    if (!Inject(contact, p, POINTER_FLAG_DOWN | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT)) return 1;
    for (int i = 0; i < 20; ++i)
    {
        PumpAndSample(16, p, stats);
        p.x += 15;
        if (!Inject(contact, p, kUpdate)) return 1;
        if (i == 15) shot(L"2-moving");
    }
    for (int i = 0; i < 60; ++i) // hold still ~1 s, still sending updates as a real digitizer does
    {
        PumpAndSample(16, p, stats);
        if (!Inject(contact, p, kUpdate)) return 1;
        if (i == 45) shot(L"3-holding");
    }
    if (!Inject(contact, p, POINTER_FLAG_UP)) return 1;
    PumpAndSample(300, p, stats);
    shot(L"4-after");
    std::printf("touch held at %ld,%ld; cursor visible-at-touch samples %d of %d\n", p.x, p.y,
                stats.visibleAtTouch, stats.samples);
    return 0;
}

int main(int argc, char** argv)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    timeBeginPeriod(1); // 1 ms sampling
    if (argc > 2 && std::strcmp(argv[1], "hold") == 0)
    {
        wchar_t dir[MAX_PATH];
        MultiByteToWideChar(CP_ACP, 0, argv[2], -1, dir, MAX_PATH);
        return HoldTest(dir);
    }
    if (argc > 1 && std::strcmp(argv[1], "edge") == 0) return EdgeSwipeTest(argc > 2 ? std::atoi(argv[2]) : 0);
    const int strokes = argc > 1 ? std::atoi(argv[1]) : 3;

    // Probe window in the middle of the primary monitor.
    const int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    const RECT area{ sw / 2 - 300, sh / 2 - 150, sw / 2 + 300, sh / 2 + 150 };

    WNDCLASSW wc{};
    wc.lpfnWndProc = ProbeProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"VirtualTouchpadProbe";
    RegisterClassW(&wc);
    HWND probe = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"probe",
                                 WS_POPUP | WS_VISIBLE, area.left, area.top, area.right - area.left,
                                 area.bottom - area.top, nullptr, nullptr, wc.hInstance, nullptr);
    if (!probe) return 1;
    Stats warmup;
    PumpAndSample(200, POINT{ -1, -1 }, warmup);

    if (!InitializeTouchInjection(1, TOUCH_FEEDBACK_NONE))
    {
        std::printf("InitializeTouchInjection failed: %lu\n", GetLastError());
        return 1;
    }

    POINT start{};
    GetCursorPos(&start);

    POINTER_TOUCH_INFO contact{};
    contact.pointerInfo.pointerType = PT_TOUCH;
    contact.pointerInfo.pointerId = 0;
    contact.touchFlags = TOUCH_FLAG_NONE;
    contact.touchMask = TOUCH_MASK_CONTACTAREA | TOUCH_MASK_ORIENTATION | TOUCH_MASK_PRESSURE;
    contact.orientation = 90;
    contact.pressure = 32000;

    const POINTER_FLAGS kDown = POINTER_FLAG_DOWN | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT;
    const POINTER_FLAGS kUpdate = POINTER_FLAG_UPDATE | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT;

    Stats stats;
    for (int s = 0; s < strokes; ++s)
    {
        // Horizontal stroke across the probe window, alternating direction.
        const int y = area.top + 60 + 60 * (s % 3);
        const int x0 = s % 2 ? area.right - 80 : area.left + 80;
        const int x1 = s % 2 ? area.left + 80 : area.right - 80;
        POINT p{ x0, y };
        if (!Inject(contact, p, kDown)) return 1;
        PumpAndSample(16, p, stats);
        for (int i = 1; i <= 25; ++i)
        {
            p.x = x0 + (x1 - x0) * i / 25;
            if (!Inject(contact, p, kUpdate)) return 1;
            PumpAndSample(16, p, stats);
        }
        if (!Inject(contact, p, POINTER_FLAG_UP)) return 1;
        PumpAndSample(150, p, stats);
    }

    POINT end{};
    GetCursorPos(&end);
    DestroyWindow(probe);

    std::printf("cursor start %ld,%ld  end %ld,%ld\n", start.x, start.y, end.x, end.y);
    std::printf("probe window received touch messages: %d %s\n", g_probeWindowTouches,
                g_probeWindowTouches ? "(touch was NOT captured by the touchpad app)" : "(touch was captured)");
    std::printf("samples %d, cursor at touch point %d, of which visible %d\n", stats.samples, stats.atTouch,
                stats.visibleAtTouch);
    std::printf("hidden samples %d, visibility changes %d, shape changes %d, position jumps %d\n",
                stats.hiddenSamples, stats.visibilityChanges, stats.shapeChanges, stats.positionJumps);

    if (g_probeWindowTouches) return 1;
    return stats.atTouch ? 2 : 0;
}
