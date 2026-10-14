#pragma once

#include <windows.h>

#include <array>
#include <cstdint>

// Feeds captured touchscreen contacts to a synthetic precision touchpad
// (CreateSyntheticPointerDevice2 with PT_TOUCHPAD, Windows 11). Windows' own touchpad stack then
// moves the pointer, clicks, scrolls and recognises gestures, using the user's touchpad settings,
// exactly as for a physical touchpad.
class TouchpadInjector
{
public:
    ~TouchpadInjector() { Destroy(); }

    // Creates the virtual touchpad sized like the monitor (physical size). False if unsupported.
    bool Create(HMONITOR monitor);
    void Destroy();
    bool Ready() const { return device_ != nullptr; }

    // Forwards the whole pointer frame that `pointerId` belongs to. Call for every touch
    // WM_POINTER* message; frames already forwarded are skipped.
    void OnPointerMessage(UINT32 pointerId);

    // Lifts every contact currently down on the virtual touchpad.
    void LiftAll();

private:
    static constexpr int kMaxContacts = 5; // PT_TOUCHPAD limit

    int SlotFor(UINT32 touchId, bool allocate);
    POINT ToHimetric(POINT px) const;
    bool Inject(POINTER_TYPE_INFO* contacts, UINT32 count);

    HSYNTHETICPOINTERDEVICE device_ = nullptr;
    RECT monitorRect_{};
    LONG widthHimetric_ = 0, heightHimetric_ = 0;

    UINT32 lastFrameId_ = UINT32(-1);
    std::array<UINT32, kMaxContacts> slotTouchId_{}; // touch pointer id per touchpad contact slot
    std::array<bool, kMaxContacts> slotUsed_{};
    std::array<POINT, kMaxContacts> slotLast_{};      // last himetric position, for LiftAll
};
