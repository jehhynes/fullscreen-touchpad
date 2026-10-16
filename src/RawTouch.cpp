#include "RawTouch.h"

#include <hidsdi.h>
#include <hidpi.h>

#include <algorithm>

namespace
{
    constexpr USAGE kDigitizerPage = 0x0D;
    constexpr USAGE kTouchScreen = 0x04;
    constexpr USAGE kFinger = 0x22;
    constexpr USAGE kTipSwitch = 0x42;
    constexpr USAGE kContactId = 0x51;
    constexpr USAGE kContactCount = 0x54;
    constexpr USAGE kGenericDesktopPage = 0x01;
    constexpr USAGE kX = 0x30;
    constexpr USAGE kY = 0x31;

    PHIDP_PREPARSED_DATA Ppd(std::vector<BYTE>& data) { return reinterpret_cast<PHIDP_PREPARSED_DATA>(data.data()); }

    bool ValueRange(PHIDP_PREPARSED_DATA ppd, USHORT collection, USAGE usage, LONG& min, LONG& max)
    {
        // A usage can have several value caps (e.g. one per report); ask for all of them.
        HIDP_VALUE_CAPS caps[16]{};
        USHORT count = ARRAYSIZE(caps);
        if (HidP_GetSpecificValueCaps(HidP_Input, kGenericDesktopPage, collection, usage, caps, &count, ppd) !=
                HIDP_STATUS_SUCCESS || count == 0)
            return false;
        min = caps[0].LogicalMin;
        max = caps[0].LogicalMax;
        return max > min;
    }
}

bool RawTouch::Register(HWND hwnd)
{
    RAWINPUTDEVICE rid{ kDigitizerPage, kTouchScreen, RIDEV_INPUTSINK, hwnd };
    return RegisterRawInputDevices(&rid, 1, sizeof(rid)) != FALSE;
}

RawTouch::Device* RawTouch::DeviceFor(HANDLE handle)
{
    auto it = devices_.find(handle);
    if (it != devices_.end()) return it->second.preparsed.empty() ? nullptr : &it->second;

    Device& d = devices_[handle];
    UINT size = 0;
    GetRawInputDeviceInfoW(handle, RIDI_PREPARSEDDATA, nullptr, &size);
    if (size == 0) return nullptr;
    d.preparsed.resize(size);
    if (GetRawInputDeviceInfoW(handle, RIDI_PREPARSEDDATA, d.preparsed.data(), &size) == UINT(-1))
    {
        d.preparsed.clear();
        return nullptr;
    }

    HIDP_CAPS caps{};
    HidP_GetCaps(Ppd(d.preparsed), &caps);
    std::vector<HIDP_LINK_COLLECTION_NODE> nodes(caps.NumberLinkCollectionNodes);
    ULONG nodeCount = ULONG(nodes.size());
    if (nodeCount && HidP_GetLinkCollectionNodes(nodes.data(), &nodeCount, Ppd(d.preparsed)) == HIDP_STATUS_SUCCESS)
    {
        for (ULONG i = 0; i < nodeCount; ++i)
            if (nodes[i].LinkUsagePage == kDigitizerPage && nodes[i].LinkUsage == kFinger)
                d.fingerCollections.push_back(USHORT(i));
    }
    if (!d.fingerCollections.empty())
    {
        d.hasRange = ValueRange(Ppd(d.preparsed), d.fingerCollections[0], kX, d.minX, d.maxX) &&
                     ValueRange(Ppd(d.preparsed), d.fingerCollections[0], kY, d.minY, d.maxY);
    }
    return &d;
}

bool RawTouch::Read(LPARAM lParam, int& contactCount, std::vector<TapDetector::Contact>& contacts)
{
    contactCount = 0;
    contacts.clear();

    UINT size = 0;
    GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
    buffer_.resize(size);
    if (size == 0 ||
        GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, buffer_.data(), &size,
                        sizeof(RAWINPUTHEADER)) != size)
        return false;

    const auto* raw = reinterpret_cast<const RAWINPUT*>(buffer_.data());
    if (raw->header.dwType != RIM_TYPEHID) return false;
    Device* device = DeviceFor(raw->header.hDevice);
    if (!device) return false;
    PHIDP_PREPARSED_DATA ppd = Ppd(device->preparsed);

    for (DWORD r = 0; r < raw->data.hid.dwCount; ++r)
    {
        auto* report = reinterpret_cast<PCHAR>(const_cast<BYTE*>(raw->data.hid.bRawData) + r * raw->data.hid.dwSizeHid);
        const ULONG length = raw->data.hid.dwSizeHid;

        // The frame's contact count includes contacts being lifted in it. In hybrid mode a frame spans
        // several reports and only the first carries the count. Slots past the frame's last contact
        // are unused, so a slot that isn't touching is a lift only while the frame has contacts left.
        ULONG count = 0;
        if (HidP_GetUsageValue(HidP_Input, kDigitizerPage, 0, kContactCount, &count, ppd, report, length) ==
                HIDP_STATUS_SUCCESS &&
            count > 0)
        {
            contactCount = std::max(contactCount, int(count));
            device->remaining = count;
            device->hasCount = true;
        }

        int touching = 0;
        for (USHORT collection : device->fingerCollections)
        {
            const bool inFrame = device->remaining > 0;
            if (inFrame) --device->remaining;
            // Slots past the frame's contacts keep stale data, even a tip switch still on from a
            // contact lifted long ago, so they're ignored whenever the digitizer gives a count.
            if (device->hasCount && !inFrame) continue;
            USAGE usages[16];
            ULONG usageCount = ARRAYSIZE(usages);
            if (HidP_GetUsages(HidP_Input, kDigitizerPage, collection, usages, &usageCount, ppd, report, length) !=
                HIDP_STATUS_SUCCESS)
                continue;
            const bool tip = std::find(usages, usages + usageCount, kTipSwitch) != usages + usageCount;
            if (!tip && !inFrame) continue; // an unused slot
            if (tip) ++touching;

            // Without a known position range, count fingers only.
            ULONG id = collection, x = 0, y = 0;
            HidP_GetUsageValue(HidP_Input, kDigitizerPage, collection, kContactId, &id, ppd, report, length);
            if (!device->hasRange ||
                HidP_GetUsageValue(HidP_Input, kGenericDesktopPage, collection, kX, &x, ppd, report, length) !=
                    HIDP_STATUS_SUCCESS ||
                HidP_GetUsageValue(HidP_Input, kGenericDesktopPage, collection, kY, &y, ppd, report, length) !=
                    HIDP_STATUS_SUCCESS)
                continue;
            contacts.push_back(TapDetector::Contact{
                uint32_t(id), double(LONG(x) - device->minX) / double(device->maxX - device->minX),
                double(LONG(y) - device->minY) / double(device->maxY - device->minY), tip });
        }
        contactCount = std::max(contactCount, touching);
    }
    return true;
}
