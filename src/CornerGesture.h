#pragma once

#include "TapDetector.h"

#include <cstdint>
#include <functional>
#include <vector>

// Recognises one-finger gestures on the corner button from raw touchscreen reports: press and
// hold (for push-to-talk) and double tap. Raw input arrives while the finger is still down, so a
// hold can start without waiting for the finger to lift; Windows sometimes delivers a touch to the
// corner window itself only after the lift.
class CornerGesture
{
public:
    struct Params
    {
        int holdMs = 300;      // a touch this long, without moving, becomes a hold
        int doubleTapMs = 400; // longest gap between the two taps of a double tap
        int idleMs = 150;      // no reports for this long: the finger has lifted
        int liftMs = 30;       // after a report with no finger touching, this long without more: lifted
        double maxTravel = 0.03; // fraction of the screen's width/height
    };

    enum class Event { None, HoldStart, HoldEnd, DoubleTap };

    // inCorner says whether a position (0..1 across the touchscreen) is on the corner button.
    CornerGesture(Params params, std::function<bool(double x, double y)> inCorner)
        : params_(params), inCorner_(std::move(inCorner)) {}

    void OnReport(int64_t nowMs, int contactCount, const std::vector<TapDetector::Contact>& contacts);

    // Call periodically (every 50 ms or so). Returns at most one event per call.
    Event Poll(int64_t nowMs);

    bool Holding() const { return holding_; }

private:
    void EndTouch(); // the finger lifted: decide what the touch was

    Event pending_ = Event::None; // decided when a touch ended, returned by the next Poll
    bool lastEmpty_ = false;      // the latest report had no finger touching
    Params params_;
    std::function<bool(double, double)> inCorner_;

    bool active_ = false;
    int64_t startMs_ = 0, lastMs_ = 0;
    bool candidate_ = false; // one finger that landed on the corner and hasn't moved away
    bool holding_ = false;
    uint32_t firstId_ = 0;
    double startX_ = 0, startY_ = 0;
    int64_t lastTapMs_ = -1; // when the previous corner tap ended, if it could start a double tap
};
