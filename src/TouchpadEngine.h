#pragma once

#include "Settings.h"

#include <cstdint>
#include <functional>
#include <unordered_map>

// Turns raw touch contacts into touchpad behaviour: relative cursor movement, taps,
// tap-and-drag, two-finger scroll/pinch, and three/four-finger swipes.
class TouchpadEngine
{
public:
    struct Callbacks
    {
        std::function<void()> toggleRequested;          // four-finger tap recognised
        std::function<void(bool)> setAnimationTimer;    // start/stop periodic OnAnimationTick calls
    };

    TouchpadEngine(const Settings& settings, Callbacks callbacks);

    void ApplySettings(const Settings& settings) { settings_ = settings; }
    void SetDpi(unsigned dpi) { scale_ = dpi > 0 ? dpi / 96.0 : 1.0; }

    // When true, only the toggle gesture is recognised and no input is synthesised.
    void SetObserveOnly(bool observe) { observeOnly_ = observe; }

    void OnDown(uint32_t id, int x, int y);
    void OnMove(uint32_t id, int x, int y);
    void OnUp(uint32_t id, bool canceled);
    void OnAnimationTick();

    // Drops all tracked contacts and releases any buttons or keys being held.
    void Reset();

private:
    enum class Mode
    {
        None,       // gesture not yet recognised
        Pointer,    // one finger moving the cursor
        Drag,       // tap then touch-and-move: left button held
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
    void EndSession(bool canceled);

    void BeginCursorTracking();
    void MoveCursor(double dx, double dy, int64_t dt);
    void ScrollBy(double wx, double wy);
    void StartKinetic();
    void StopKinetic();
    void HoldCtrl();
    void ReleaseCtrl();
    void ReleaseAlt();

    void Centroid(double& cx, double& cy) const;
    double Spread() const;
    double Slop() const { return settings_.TapSlop * scale_; }
    static int64_t Now();

    Settings settings_;
    Callbacks callbacks_;
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

    // Cursor
    double curX_ = 0, curY_ = 0;
    int sentX_ = 0, sentY_ = 0;

    // Tap-and-drag
    int64_t lastTapMs_ = INT64_MIN / 2;
    bool dragCandidate_ = false;

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
