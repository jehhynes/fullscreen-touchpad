#include "CornerGesture.h"

#include <algorithm>
#include <cmath>

void CornerGesture::OnReport(int64_t nowMs, int contactCount, const std::vector<TapDetector::Contact>& contacts)
{
    // A finger landing after a report with none touching is a new touch, even if it came quickly
    // (the second tap of a double tap).
    if (active_ && lastEmpty_ && !contacts.empty()) EndTouch();
    lastEmpty_ = contacts.empty();
    if (!active_)
    {
        if (contacts.empty()) return;
        active_ = true;
        startMs_ = nowMs;
        holding_ = false;
        candidate_ = inCorner_(contacts[0].x, contacts[0].y);
        firstId_ = contacts[0].id;
        startX_ = contacts[0].x;
        startY_ = contacts[0].y;
    }
    lastMs_ = nowMs;
    if (!candidate_ || holding_) return; // once holding, keep holding until the finger lifts

    if (std::max(contactCount, int(contacts.size())) > 1)
    {
        candidate_ = false; // a multi-finger touch isn't a corner gesture
        return;
    }
    for (const auto& c : contacts)
        if (c.id == firstId_ &&
            (std::abs(c.x - startX_) > params_.maxTravel || std::abs(c.y - startY_) > params_.maxTravel))
            candidate_ = false; // slid off: a swipe, not a press
}

CornerGesture::Event CornerGesture::Poll(int64_t nowMs)
{
    if (active_ && (nowMs - lastMs_ >= params_.idleMs || (lastEmpty_ && nowMs - lastMs_ >= params_.liftMs)))
        EndTouch();
    if (pending_ != Event::None)
    {
        const Event e = pending_;
        pending_ = Event::None;
        return e;
    }
    if (active_ && candidate_ && !holding_ && !lastEmpty_ && nowMs - startMs_ >= params_.holdMs)
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
    if (!candidate_ || lastMs_ - startMs_ >= params_.holdMs)
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
