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
        startX_ = startY_ = -1;
        tracks_.clear();
        down_.clear();
    }
    lastMs_ = nowMs;

    for (const Contact& c : contacts)
        if (!c.touching) down_.erase(c.id);
    // A contact whose lift went unreported.
    std::erase_if(down_, [&](const auto& d) { return nowMs - d.second >= params_.idleMs; });

    for (const Contact& c : contacts)
    {
        if (!c.touching) continue;
        down_[c.id] = nowMs;
        auto it = tracks_.find(c.id);
        if (it == tracks_.end())
        {
            if (tracks_.empty()) startX_ = c.x, startY_ = c.y;
            tracks_.emplace(c.id, Track{ c.x, c.y });
            continue;
        }
        if (std::abs(c.x - it->second.startX) > params_.maxTravel ||
            std::abs(c.y - it->second.startY) > params_.maxTravel)
            moved_ = true;
    }

    // Some digitizers spread one frame over several reports and only put the count in the first, so
    // contacts are counted across reports. Without positions, only the digitizer's count is known.
    maxCount_ = std::max(maxCount_, contacts.empty() ? contactCount : int(down_.size()));
}

bool TapDetector::Poll(int64_t nowMs)
{
    if (!active_ || nowMs - lastMs_ < params_.idleMs) return false;
    active_ = false;
    last_.maxFingers = maxCount_;
    last_.durationMs = lastMs_ - startMs_;
    last_.moved = moved_;
    last_.startX = startX_;
    last_.startY = startY_;
    last_.toggled = maxCount_ >= params_.fingers && last_.durationMs <= params_.maxDurationMs && !moved_;
    return last_.toggled;
}
