#include "CornerGesture.h"

#include <algorithm>
#include <cmath>

void CornerGesture::OnReport(int64_t nowMs, int contactCount, const std::vector<TapDetector::Contact>& contacts)
{
    // Nothing touching at all: every finger has lifted.
    const bool anyTouching = std::any_of(contacts.begin(), contacts.end(), [](const auto& c) { return c.touching; });
    if (!anyTouching && contactCount == 0)
    {
        down_.clear();
        if (active_) EndTouch();
        return;
    }

    for (const auto& c : contacts)
    {
        if (c.touching) continue;
        down_.erase(c.id);
        if (active_ && c.id == id_) EndTouch();
    }
    Expire(nowMs);

    for (const auto& c : contacts)
    {
        if (!c.touching) continue;
        const bool isNew = down_.find(c.id) == down_.end();
        down_[c.id] = nowMs;
        if (active_ && c.id == id_)
        {
            lastMs_ = nowMs;
            if (std::abs(c.x - startX_) > params_.maxTravel || std::abs(c.y - startY_) > params_.maxTravel)
                still_ = false; // slid off: a swipe, not a press (but a hold, once started, lasts until the lift)
        }
        else if (!active_ && isNew && inCorner_(c.x, c.y))
        {
            active_ = true;
            id_ = c.id;
            startMs_ = lastMs_ = nowMs;
            startX_ = c.x;
            startY_ = c.y;
            still_ = true;
            holding_ = false;
        }
    }
}

void CornerGesture::Expire(int64_t nowMs)
{
    // A contact whose lift went unreported.
    std::erase_if(down_, [&](const auto& d) { return nowMs - d.second >= params_.idleMs; });
    if (active_ && nowMs - lastMs_ >= params_.idleMs) EndTouch();
}

CornerGesture::Event CornerGesture::Poll(int64_t nowMs)
{
    Expire(nowMs);
    if (pending_ != Event::None)
    {
        const Event e = pending_;
        pending_ = Event::None;
        return e;
    }
    if (active_ && still_ && !holding_ && nowMs - startMs_ >= params_.holdMs)
    {
        holding_ = true;
        return Event::HoldStart;
    }
    return Event::None;
}

void CornerGesture::EndTouch()
{
    active_ = false;
    if (holding_)
    {
        holding_ = false;
        lastTapMs_ = -1;
        pending_ = Event::HoldEnd;
        return;
    }
    if (!still_ || lastMs_ - startMs_ >= params_.holdMs)
    {
        lastTapMs_ = -1;
        return;
    }
    if (lastTapMs_ >= 0 && startMs_ - lastTapMs_ <= params_.doubleTapMs)
    {
        lastTapMs_ = -1;
        pending_ = Event::DoubleTap;
        return;
    }
    lastTapMs_ = lastMs_;
}
