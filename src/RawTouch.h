#pragma once

#include "TapDetector.h"

#include <windows.h>

#include <map>
#include <vector>

// Reads the touchscreen through Raw Input (digitizer usage page 0x0D, touch screen usage 0x04).
// With RIDEV_INPUTSINK the app sees every touch, whichever window the touch goes to, without
// taking it away from that window, and without any special privileges.
class RawTouch
{
public:
    bool Register(HWND hwnd);

    // Parses a WM_INPUT message. Returns false if it isn't a touchscreen report. `contacts` includes
    // contacts being lifted (touching = false), so a lift is seen the moment it happens.
    bool Read(LPARAM lParam, int& contactCount, std::vector<TapDetector::Contact>& contacts);

private:
    struct Device
    {
        std::vector<BYTE> preparsed;
        std::vector<USHORT> fingerCollections; // link collections with usage Finger (0x0D/0x22)
        LONG minX = 0, maxX = 1, minY = 0, maxY = 1;
        bool hasRange = false; // X/Y logical ranges known, so positions can be normalised
        ULONG remaining = 0;   // contacts of the current frame not read yet (a hybrid-mode frame spans reports)
        bool hasCount = false; // reports carry a contact count, so slots past it are unused
    };

    Device* DeviceFor(HANDLE handle);

    std::map<HANDLE, Device> devices_;
    std::vector<BYTE> buffer_;
};
