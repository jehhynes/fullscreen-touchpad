#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

// Recognises the toggle gesture, a quick multi-finger tap, from raw touchscreen reports.
// A touch "session" lasts from the first report until no reports arrive for idleMs (all fingers
// lifted). It counts as the toggle tap if it reached `fingers` contacts, was short, and no
// contact moved far (so multi-finger swipes don't toggle).
class TapDetector
{
public:
    struct Params
    {
        int fingers = 4;
        int maxDurationMs = 700;
        int idleMs = 150;
        double maxTravel = 0.03; // fraction of the screen's width/height
    };

    struct Contact
    {
        uint32_t id;
        double x, y; // 0..1 across the screen
    };

    explicit TapDetector(Params params = {}) : params_(params) {}

    // One raw report: the contact count it carries (0 if it doesn't carry one) and its contacts.
    void OnReport(int64_t nowMs, int contactCount, const std::vector<Contact>& contacts);

    // Call periodically. Returns true once when a finished session was the toggle tap.
    bool Poll(int64_t nowMs);

    bool InSession() const { return active_; }

    // The most recently finished session, for diagnostics.
    struct Summary
    {
        int maxFingers = 0;
        int64_t durationMs = 0;
        bool moved = false;
        bool toggled = false;
    };
    const Summary& LastSession() const { return last_; }

private:
    struct Track
    {
        double startX, startY;
    };

    Params params_;
    bool active_ = false;
    int64_t startMs_ = 0, lastMs_ = 0;
    int maxCount_ = 0;
    bool moved_ = false;
    std::unordered_map<uint32_t, Track> tracks_;
    Summary last_;
};
