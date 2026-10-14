#include "TouchpadInjector.h"

#include <algorithm>
#include <vector>

namespace
{
    // From learn.microsoft.com/windows/win32/input-precisiontouchpad/createsyntheticpointerdevice2
    // (not yet in the installed SDK headers).
    constexpr ULONG SDCO_PHYSICAL_SIZE = 0x1;
    constexpr POINTER_INPUT_TYPE kTouchpad = POINTER_INPUT_TYPE(5); // PT_TOUCHPAD

    struct SyntheticDeviceCreationParams
    {
        POINTER_INPUT_TYPE pointerType;
        ULONG maxCount;
        POINTER_FEEDBACK_MODE feedbackMode;
        HMONITOR hMonitor;
        ULONG deviceWidth;  // himetric (0.01 mm)
        ULONG deviceHeight; // himetric
        ULONG options;
    };

    using CreateDevice2Fn = HSYNTHETICPOINTERDEVICE(WINAPI*)(SyntheticDeviceCreationParams*);

    constexpr POINTER_FLAGS kPhaseFlags = POINTER_FLAG_DOWN | POINTER_FLAG_UPDATE | POINTER_FLAG_UP |
                                          POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT;
}

bool TouchpadInjector::Create(HMONITOR monitor)
{
    Destroy();

    auto create = reinterpret_cast<CreateDevice2Fn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "CreateSyntheticPointerDevice2"));
    if (!create) return false;

    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(monitor, &mi)) return false;
    monitorRect_ = mi.rcMonitor;

    // Physical size of the display, so finger travel maps 1:1 to touchpad travel.
    HDC dc = CreateDCW(L"DISPLAY", mi.szDevice, nullptr, nullptr);
    const int widthMm = dc ? GetDeviceCaps(dc, HORZSIZE) : 0;
    const int heightMm = dc ? GetDeviceCaps(dc, VERTSIZE) : 0;
    if (dc) DeleteDC(dc);
    widthHimetric_ = std::max(1000, widthMm * 100);
    heightHimetric_ = std::max(1000, heightMm * 100);

    SyntheticDeviceCreationParams params{};
    params.pointerType = kTouchpad;
    params.maxCount = kMaxContacts;
    params.feedbackMode = POINTER_FEEDBACK_NONE;
    params.hMonitor = nullptr; // required for touchpads
    params.deviceWidth = ULONG(widthHimetric_);
    params.deviceHeight = ULONG(heightHimetric_);
    params.options = SDCO_PHYSICAL_SIZE;
    device_ = create(&params);
    return device_ != nullptr;
}

void TouchpadInjector::Destroy()
{
    if (!device_) return;
    LiftAll();
    DestroySyntheticPointerDevice(device_);
    device_ = nullptr;
}

POINT TouchpadInjector::ToHimetric(POINT px) const
{
    const LONG w = std::max(1L, monitorRect_.right - monitorRect_.left);
    const LONG h = std::max(1L, monitorRect_.bottom - monitorRect_.top);
    const LONG x = std::clamp(px.x - monitorRect_.left, 0L, w - 1);
    const LONG y = std::clamp(px.y - monitorRect_.top, 0L, h - 1);
    return POINT{ LONG(LONGLONG(x) * widthHimetric_ / w), LONG(LONGLONG(y) * heightHimetric_ / h) };
}

int TouchpadInjector::SlotFor(UINT32 touchId, bool allocate)
{
    for (int i = 0; i < kMaxContacts; ++i)
        if (slotUsed_[i] && slotTouchId_[i] == touchId) return i;
    if (!allocate) return -1;
    for (int i = 0; i < kMaxContacts; ++i)
    {
        if (slotUsed_[i]) continue;
        slotUsed_[i] = true;
        slotTouchId_[i] = touchId;
        return i;
    }
    return -1; // more than five fingers: ignore the extras
}

bool TouchpadInjector::Inject(POINTER_TYPE_INFO* contacts, UINT32 count)
{
    return count == 0 || InjectSyntheticPointerInput(device_, contacts, count);
}

void TouchpadInjector::OnPointerMessage(UINT32 pointerId)
{
    if (!device_) return;

    UINT32 count = 0;
    if (!GetPointerFrameInfo(pointerId, &count, nullptr) || count == 0) return;
    std::vector<POINTER_INFO> frame(count);
    if (!GetPointerFrameInfo(pointerId, &count, frame.data())) return;
    if (frame[0].frameId == lastFrameId_) return; // one message per contact, but one injection per frame
    lastFrameId_ = frame[0].frameId;

    POINTER_TYPE_INFO contacts[kMaxContacts]{};
    UINT32 n = 0;
    for (const POINTER_INFO& touch : frame)
    {
        const bool lifting = (touch.pointerFlags & (POINTER_FLAG_UP | POINTER_FLAG_CANCELED)) != 0;
        const int slot = SlotFor(touch.pointerId, !lifting);
        if (slot < 0) continue;

        POINTER_FLAGS flags = touch.pointerFlags & kPhaseFlags;
        if (touch.pointerFlags & POINTER_FLAG_CANCELED) flags = POINTER_FLAG_UP;
        if (lifting) slotUsed_[slot] = false;

        const POINT pos = lifting ? slotLast_[slot] : ToHimetric(touch.ptPixelLocation);
        slotLast_[slot] = pos;

        POINTER_TYPE_INFO& c = contacts[n++];
        c.type = kTouchpad;
        c.touchInfo.pointerInfo.pointerType = kTouchpad;
        c.touchInfo.pointerInfo.pointerId = UINT32(slot);
        c.touchInfo.pointerInfo.ptHimetricLocation = pos;
        // Contacts without "confidence" are treated as palms and ignored.
        c.touchInfo.pointerInfo.pointerFlags = flags | POINTER_FLAG_CONFIDENCE |
                                               (slot == 0 ? POINTER_FLAG_PRIMARY : POINTER_FLAG_NONE);
    }
    Inject(contacts, n);
}

void TouchpadInjector::LiftAll()
{
    if (!device_) return;
    POINTER_TYPE_INFO contacts[kMaxContacts]{};
    UINT32 n = 0;
    for (int i = 0; i < kMaxContacts; ++i)
    {
        if (!slotUsed_[i]) continue;
        slotUsed_[i] = false;
        POINTER_TYPE_INFO& c = contacts[n++];
        c.type = kTouchpad;
        c.touchInfo.pointerInfo.pointerType = kTouchpad;
        c.touchInfo.pointerInfo.pointerId = UINT32(i);
        c.touchInfo.pointerInfo.ptHimetricLocation = slotLast_[i];
        c.touchInfo.pointerInfo.pointerFlags = POINTER_FLAG_UP | POINTER_FLAG_CONFIDENCE;
    }
    Inject(contacts, n);
    lastFrameId_ = UINT32(-1);
}
