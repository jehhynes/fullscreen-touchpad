#pragma once

#include "Settings.h"
#include "TouchpadHost.h"

#include <cstdint>
#include <unordered_map>

// Turns raw touch contacts into touchpad behaviour: relative cursor movement, taps,
// double-taps, tap-and-hold drags, long-press right click, two-finger scroll/pinch,
// and three/four-finger swipes.
class TouchpadEngine
{
public:
    enum Timer : int
    {
        TimerKinetic = 1, // kinetic scroll animation frame
        TimerHold,        // finger held still long enough for a long press / drag
        TimerClick,       // double-tap window expired: deliver the held-back single click
    };

    TouchpadEngine(const Settings& settings, TouchpadHost& host);

    void ApplySettings(const Settings& settings) { settings_ = settings; }
    void SetDpi(unsigned dpi) { scale_ = dpi > 0 ? dpi / 96.0 : 1.0; }

    // When true, only the toggle gesture is recognised and no input is synthesised.
    void SetObserveOnly(bool observe) { observeOnly_ = observe; }

    bool Touching() const { return !contacts_.empty(); }

    void OnDown(uint32_t id, int x, int y);
    void OnMove(uint32_t id, int x, int y);
    void OnUp(uint32_t id, bool canceled);
    void OnTimer(int id);

    // Drops all tracked contacts and releases any buttons or keys being held.
    void Reset();

    // The engine owns the pointer position, because Windows moves the real cursor to every
    // touch point. SyncCursor adopts a new position (e.g. after a real mouse moved it);
    // EnforceCursor puts the cursor back if touch has dragged it under the finger.
    void SyncCursor(POINT p);
    void EnforceCursor();

private:
    enum class Mode
    {
        None,       // gesture not yet recognised
        Pointer,    // one finger moving the cursor
        Drag,       // tap then touch-and-hold/move: left button held
        TwoPending, // two fingers down, waiting to tell scroll from pinch
        Scroll,
        Zoom,
        AltTab,     // three-finger horizontal swipe, Alt held while stepping through apps
        Done,       // gesture already fired; ignore the rest of this touch session
    };

    struct Contact
    {
        double startX, startY, x, y;
        int64_t lastMs;
    };

    void OnContactCountChanged(bool added);
    void HandleSingleFinger(const Contact& c, double dx, double dy, int64_t dt);
    void HandleTwoFingers(int64_t now);
    void HandleThreeFingers();
    void HandleFourFingers();
    void OnHold();
    void EndSession(bool canceled);

    void StartDrag();
    void FlushPendingClick();
    void CancelPendingClick();

    void EnsureCursor();
    POINT CursorPoint() const { return POINT{ sentX_, sentY_ }; }
    void MoveCursor(double dx, double dy, int64_t dt);
    void ScrollBy(double wx, double wy);
    void StartKinetic();
    void StopKinetic();
    void KineticFrame();
    void HoldCtrl();
    void ReleaseCtrl();
    void ReleaseAlt();
    void StartTimer(Timer id, unsigned ms) { host_.StartTimer(id, ms); }
    void StopTimer(Timer id) { host_.StopTimer(id); }

    void Centroid(double& cx, double& cy) const;
    double Spread() const;
    double Slop() const { return settings_.TapSlop * scale_; }
    int64_t Now() { return host_.NowMs(); }

    Settings settings_;
    TouchpadHost& host_;
    double scale_ = 1.0;
    bool observeOnly_ = false;

    std::unordered_map<uint32_t, Contact> contacts_;

    // Touch session (first finger down -> last finger up)
    bool inSession_ = false;
    int64_t sessionStartMs_ = 0;
    size_t maxContacts_ = 0;
    bool moved_ = false;
    Mode mode_ = Mode::None;
    uint32_t primaryId_ = 0;

    // Multi-finger baselines, reset whenever the number of fingers changes
    double baseCx_ = 0, baseCy_ = 0, baseDist_ = 0;
    double lastCx_ = 0, lastCy_ = 0, lastDist_ = 0;
    int64_t lastMultiMs_ = 0;

    // Pointer position (sub-pixel) and the pixel last sent to Windows
    bool haveCursor_ = false;
    double curX_ = 0, curY_ = 0;
    int sentX_ = 0, sentY_ = 0;

    // Tap sequences. A single tap's click is held back for DoubleTapWindowMs so that a
    // following tap can make it a double-click, or a following hold/move can make it a drag.
    bool clickPending_ = false;
    bool dragCandidate_ = false; // this session started while a click was pending

    // Scroll and zoom
    double wheelAccumX_ = 0, wheelAccumY_ = 0, zoomAccum_ = 0;
    double velX_ = 0, velY_ = 0;   // wheel units per ms
    int scrollAxis_ = 0;           // 0 = free, 1 = horizontal only, 2 = vertical only
    bool kineticActive_ = false;
    int64_t kineticLastMs_ = 0;

    // Modifier keys we are holding down
    bool altHeld_ = false, ctrlHeld_ = false;
    double altTabLastX_ = 0;
};
