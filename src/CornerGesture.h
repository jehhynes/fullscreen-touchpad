#pragma once

#include "TapDetector.h"

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

// Recognises one-finger gestures on the corner button from raw touchscreen reports: press and
// hold (for push-to-talk) and double tap. Raw input arrives while the finger is still down, so a
// hold can start without waiting for the finger to lift; Windows sometimes delivers a touch to the
// corner window itself only after the lift.
//
// It follows the finger that landed in the corner by its contact id and ignores other contacts, so
// part of the hand resting on the screen while reaching for the corner doesn't spoil the gesture.
class CornerGesture
{
public:
    struct Params
    {
        int holdMs = 300;      // a touch this long, without moving, becomes a hold
        int doubleTapMs = 400; // longest gap between the two taps of a double tap
        int idleMs = 150;      // a contact not reported for this long has lifted
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
    void EndTouch(); // the corner finger lifted: decide what the touch was
    void Expire(int64_t nowMs);

    Params params_;
    std::function<bool(double, double)> inCorner_;
    Event pending_ = Event::None; // decided when a touch ended, returned by the next Poll

    // Every contact down now, by id, and when it was last reported: only a contact that's new can
    // start a corner touch (not one that slid into the corner).
    std::unordered_map<uint32_t, int64_t> down_;

    bool active_ = false; // following a finger that landed in the corner
    uint32_t id_ = 0;
    int64_t startMs_ = 0, lastMs_ = 0;
    double startX_ = 0, startY_ = 0;
    bool still_ = false;  // hasn't moved away: can still be a tap or a hold
    bool holding_ = false;
    int64_t lastTapMs_ = -1; // when the previous corner tap ended, if it could start a double tap
};
