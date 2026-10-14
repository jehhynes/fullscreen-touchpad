#pragma once

#include <windows.h>

constexpr wchar_t kHostWindowClass[] = L"VirtualTouchpadHost";

constexpr UINT WM_APP_TRAY = WM_APP + 1;   // tray icon callback
constexpr UINT WM_APP_TOGGLE = WM_APP + 2; // toggle on/off (also sent by a second instance)
