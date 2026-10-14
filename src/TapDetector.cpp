#include "TapDetector.h"

#include <algorithm>
#include <cmath>

void TapDetector::OnReport(int64_t nowMs, int contactCount, const std::vector<Contact>& contacts)
{
    if (!active_)
    {
        active_ = true;
        startMs_ = nowMs;
        maxCount_ = 0;
        moved_ = false;
        tracks_.clear();
    }
    lastMs_ = nowMs;

    // Some digitizers spread one frame over several reports and only put the count in the first.
    maxCount_ = std::max({ maxCount_, contactCount, int(contacts.size()) });

    for (const Contact& c : contacts)
    {
        auto it = tracks_.find(c.id);
        if (it == tracks_.end())
        {
            tracks_.emplace(c.id, Track{ c.x, c.y });
            continue;
        }
        if (std::abs(c.x - it->second.startX) > params_.maxTravel ||
            std::abs(c.y - it->second.startY) > params_.maxTravel)
            moved_ = true;
    }
}

bool TapDetector::Poll(int64_t nowMs)
{
    if (!active_ || nowMs - lastMs_ < params_.idleMs) return false;
    active_ = false;
    last_.maxFingers = maxCount_;
    last_.durationMs = lastMs_ - startMs_;
    last_.moved = moved_;
    last_.toggled = maxCount_ >= params_.fingers && last_.durationMs <= params_.maxDurationMs && !moved_;
    return last_.toggled;
}
