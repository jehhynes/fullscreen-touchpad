#include "TouchpadEngine.h"

#include <algorithm>
#include <cmath>

namespace
{
    double Distance(double x1, double y1, double x2, double y2)
    {
        return std::hypot(x2 - x1, y2 - y1);
    }
}

TouchpadEngine::TouchpadEngine(const Settings& settings, TouchpadHost& host)
    : settings_(settings), host_(host)
{
}

// ------------------------------------------------------------------ contact events

void TouchpadEngine::OnDown(uint32_t id, int x, int y)
{
    StopKinetic();
    const int64_t now = Now();

    if (contacts_.empty())
    {
        inSession_ = true;
        sessionStartMs_ = now;
        maxContacts_ = 0;
        moved_ = false;
        mode_ = Mode::None;
        primaryId_ = id;
        dragCandidate_ = !observeOnly_ && clickPending_;
        StopTimer(TimerClick); // the pending click now waits on what this touch turns into

        if (!observeOnly_)
        {
            // Holding still turns a tap-then-touch into a drag, or a lone touch into a right click.
            const double holdMs = dragCandidate_ ? settings_.TapMaxMs : settings_.LongPressMs;
            if (dragCandidate_ || settings_.LongPressRightClick) StartTimer(TimerHold, unsigned(holdMs));
        }
    }

    contacts_[id] = Contact{ double(x), double(y), double(x), double(y), now };
    maxContacts_ = std::max(maxContacts_, contacts_.size());
    OnContactCountChanged(true);
}

void TouchpadEngine::OnMove(uint32_t id, int x, int y)
{
    auto it = contacts_.find(id);
    if (it == contacts_.end()) return;
    Contact& c = it->second;

    const int64_t now = Now();
    const double dx = x - c.x, dy = y - c.y;
    const int64_t dt = now - c.lastMs;
    if (dx == 0 && dy == 0) return;

    c.x = x;
    c.y = y;
    c.lastMs = now;
    if (!moved_ && Distance(c.startX, c.startY, x, y) > Slop())
    {
        moved_ = true;
        StopTimer(TimerHold);
    }

    if (observeOnly_) return;

    const size_t n = contacts_.size();
    if (mode_ == Mode::Drag || (maxContacts_ == 1 && n == 1))
    {
        if (id == primaryId_) HandleSingleFinger(c, dx, dy, dt);
    }
    else if (n == 2) HandleTwoFingers(now);
    else if (n == 3) HandleThreeFingers();
    else if (n == 4) HandleFourFingers();
}

void TouchpadEngine::OnUp(uint32_t id, bool canceled)
{
    if (contacts_.erase(id) == 0) return;

    if (!contacts_.empty())
    {
        OnContactCountChanged(false);
        return;
    }
    EndSession(canceled);
}

void TouchpadEngine::OnTimer(int id)
{
    switch (id)
    {
    case TimerKinetic:
        KineticFrame();
        break;
    case TimerHold:
        StopTimer(TimerHold);
        OnHold();
        break;
    case TimerClick:
        StopTimer(TimerClick);
        FlushPendingClick();
        break;
    }
}

void TouchpadEngine::Reset()
{
    contacts_.clear();
    if (inSession_) EndSession(true);
    CancelPendingClick();
    StopTimer(TimerHold);
    StopKinetic();
}

// ------------------------------------------------------------------ gesture handling

void TouchpadEngine::OnContactCountChanged(bool added)
{
    if (observeOnly_) return;

    if (contacts_.size() > 1)
    {
        // Multi-finger gestures never become holds, and a tap waiting to see if it was the
        // first of a double-tap is just a click.
        StopTimer(TimerHold);
        FlushPendingClick();
        dragCandidate_ = false;
    }

    if (mode_ == Mode::Scroll && !added) StartKinetic();

    if (added)
    {
        if (mode_ == Mode::Pointer || mode_ == Mode::TwoPending || mode_ == Mode::Scroll || mode_ == Mode::Zoom)
            mode_ = Mode::None;
    }
    else if (mode_ == Mode::Scroll || mode_ == Mode::Zoom)
    {
        // Lifting fingers ends a scroll/pinch; don't let the remaining finger move the cursor.
        mode_ = Mode::Done;
    }

    if (mode_ != Mode::Zoom) ReleaseCtrl();

    double cx, cy;
    Centroid(cx, cy);
    baseCx_ = lastCx_ = cx;
    baseCy_ = lastCy_ = cy;
    baseDist_ = lastDist_ = Spread();
    lastMultiMs_ = Now();
    if (contacts_.size() == 2 && mode_ == Mode::None) mode_ = Mode::TwoPending;
}

void TouchpadEngine::HandleSingleFinger(const Contact& c, double dx, double dy, int64_t dt)
{
    if (mode_ == Mode::None)
    {
        const double fromStart = Distance(c.startX, c.startY, c.x, c.y);
        if (dragCandidate_)
        {
            // Tap, then touch-and-move: drag.
            if (fromStart <= Slop()) return;
            StartDrag();
        }
        else
        {
            // Small dead zone so taps don't nudge the cursor.
            if (fromStart <= 3 * scale_) return;
            EnsureCursor();
            mode_ = Mode::Pointer;
        }
    }

    if (mode_ == Mode::Pointer || mode_ == Mode::Drag) MoveCursor(dx, dy, dt);
}

void TouchpadEngine::OnHold()
{
    if (observeOnly_ || !inSession_ || moved_ || maxContacts_ != 1 || contacts_.size() != 1) return;
    if (mode_ != Mode::None && mode_ != Mode::Pointer) return;

    if (dragCandidate_)
    {
        // Tap, then touch-and-hold: press the button; moving now drags, lifting releases.
        StartDrag();
    }
    else if (settings_.LongPressRightClick)
    {
        EnsureCursor();
        host_.RightClick(CursorPoint());
        mode_ = Mode::Done;
    }
}

void TouchpadEngine::StartDrag()
{
    // The held-back click from the first tap becomes the press of this drag.
    StopTimer(TimerClick);
    clickPending_ = false;
    EnsureCursor();
    host_.LeftDown(CursorPoint());
    mode_ = Mode::Drag;
}

void TouchpadEngine::HandleTwoFingers(int64_t now)
{
    double cx, cy;
    Centroid(cx, cy);
    const double dist = Spread();

    if (mode_ == Mode::TwoPending)
    {
        const double dc = Distance(baseCx_, baseCy_, cx, cy);
        const double dd = std::abs(dist - baseDist_);
        if (std::max(dc, dd) <= Slop()) return;

        if (settings_.PinchZoom && dd > dc * 1.2)
        {
            mode_ = Mode::Zoom;
            zoomAccum_ = 0;
            HoldCtrl();
        }
        else
        {
            mode_ = Mode::Scroll;
            wheelAccumX_ = wheelAccumY_ = 0;
            velX_ = velY_ = 0;
            const double ax = std::abs(cx - baseCx_), ay = std::abs(cy - baseCy_);
            scrollAxis_ = ay > 2 * ax ? 2 : (ax > 2 * ay ? 1 : 0);
        }
    }
    else if (mode_ == Mode::Scroll)
    {
        double dx = (cx - lastCx_) / scale_, dy = (cy - lastCy_) / scale_;
        if (scrollAxis_ == 1) dy = 0;
        if (scrollAxis_ == 2) dx = 0;

        // Positive WHEEL scrolls up, positive HWHEEL scrolls right.
        const double sign = settings_.NaturalScrolling ? 1 : -1;
        const double wy = sign * dy * settings_.ScrollSpeed;
        const double wx = -sign * dx * settings_.ScrollSpeed;
        ScrollBy(wx, wy);

        const int64_t dt = now - lastMultiMs_;
        if (dt > 0)
        {
            velX_ = 0.6 * velX_ + 0.4 * (wx / dt);
            velY_ = 0.6 * velY_ + 0.4 * (wy / dt);
        }
    }
    else if (mode_ == Mode::Zoom && lastDist_ > 1 && dist > 1)
    {
        // Ctrl+wheel in whole notches, which every app understands.
        zoomAccum_ += std::log(dist / lastDist_) * settings_.ZoomSpeed;
        while (std::abs(zoomAccum_) >= WHEEL_DELTA)
        {
            const int notch = zoomAccum_ > 0 ? WHEEL_DELTA : -WHEEL_DELTA;
            host_.Wheel(CursorPoint(), notch);
            zoomAccum_ -= notch;
        }
    }

    lastCx_ = cx;
    lastCy_ = cy;
    lastDist_ = dist;
    lastMultiMs_ = now;
}

void TouchpadEngine::HandleThreeFingers()
{
    double cx, cy;
    Centroid(cx, cy);

    if (mode_ == Mode::AltTab)
    {
        const double step = settings_.AltTabStep * scale_;
        while (cx - altTabLastX_ > step) { host_.Chord({ VK_TAB }); altTabLastX_ += step; }
        while (altTabLastX_ - cx > step) { host_.Chord({ VK_SHIFT, VK_TAB }); altTabLastX_ -= step; }
        return;
    }
    if (mode_ != Mode::None) return;

    const double dx = cx - baseCx_, dy = cy - baseCy_;
    if (std::max(std::abs(dx), std::abs(dy)) < settings_.SwipeThreshold * scale_) return;

    if (std::abs(dx) > std::abs(dy))
    {
        // Like the Windows touchpad: hold Alt and step through open apps while swiping.
        host_.KeyDown(VK_MENU);
        altHeld_ = true;
        if (dx > 0) host_.Chord({ VK_TAB });
        else host_.Chord({ VK_SHIFT, VK_TAB });
        altTabLastX_ = cx;
        mode_ = Mode::AltTab;
    }
    else
    {
        if (dy < 0) host_.Chord({ VK_LWIN, VK_TAB }); // Task View
        else host_.Chord({ VK_LWIN, 'D' });           // Show desktop
        mode_ = Mode::Done;
    }
}

void TouchpadEngine::HandleFourFingers()
{
    if (mode_ != Mode::None) return;

    double cx, cy;
    Centroid(cx, cy);
    const double dx = cx - baseCx_, dy = cy - baseCy_;
    if (std::max(std::abs(dx), std::abs(dy)) < settings_.SwipeThreshold * scale_) return;

    if (std::abs(dx) > std::abs(dy))
    {
        // Swiping left reveals the virtual desktop to the right, as on a touchpad.
        host_.Chord({ VK_CONTROL, VK_LWIN, static_cast<WORD>(dx < 0 ? VK_RIGHT : VK_LEFT) });
    }
    else if (dy < 0) host_.Chord({ VK_LWIN, VK_TAB });
    else host_.Chord({ VK_LWIN, 'D' });
    mode_ = Mode::Done;
}

void TouchpadEngine::EndSession(bool canceled)
{
    inSession_ = false;
    StopTimer(TimerHold);
    const int64_t duration = Now() - sessionStartMs_;

    const bool tapCandidate = !canceled && !moved_ &&
        (mode_ == Mode::None || mode_ == Mode::Pointer || mode_ == Mode::TwoPending);

    if (tapCandidate && maxContacts_ == 4 && duration <= settings_.MultiTapMaxMs + 100)
    {
        host_.ToggleRequested();
    }
    else if (tapCandidate && !observeOnly_ && settings_.TapToClick)
    {
        EnsureCursor();
        if (maxContacts_ == 1 && duration <= settings_.TapMaxMs)
        {
            if (clickPending_)
            {
                // Second tap of a double-tap: deliver both clicks now.
                clickPending_ = false;
                host_.LeftClick(CursorPoint());
                host_.LeftClick(CursorPoint());
                host_.LeftClicked();
            }
            else
            {
                // Hold the click back briefly in case this is the start of a double-tap or drag.
                clickPending_ = true;
                StartTimer(TimerClick, unsigned(settings_.DoubleTapWindowMs));
            }
        }
        else if (maxContacts_ == 2 && duration <= settings_.MultiTapMaxMs) host_.RightClick(CursorPoint());
        else if (maxContacts_ == 3 && duration <= settings_.MultiTapMaxMs) host_.MiddleClick(CursorPoint());
    }

    // A click still held back from the previous tap was a plain click after all
    // (e.g. this touch was too long to be a tap, or a long-press right click).
    if (!(clickPending_ && tapCandidate && maxContacts_ == 1)) FlushPendingClick();

    if (mode_ == Mode::Drag) host_.LeftUp(CursorPoint());
    ReleaseCtrl();
    ReleaseAlt();

    dragCandidate_ = false;
    mode_ = Mode::None;
}

void TouchpadEngine::FlushPendingClick()
{
    if (!clickPending_) return;
    StopTimer(TimerClick);
    clickPending_ = false;
    if (observeOnly_) return;
    EnsureCursor();
    host_.LeftClick(CursorPoint());
    host_.LeftClicked();
}

void TouchpadEngine::CancelPendingClick()
{
    StopTimer(TimerClick);
    clickPending_ = false;
}

// ------------------------------------------------------------------ cursor

void TouchpadEngine::SyncCursor(POINT p)
{
    haveCursor_ = true;
    curX_ = sentX_ = p.x;
    curY_ = sentY_ = p.y;
}

void TouchpadEngine::EnsureCursor()
{
    if (!haveCursor_) SyncCursor(host_.CursorPos());
}

void TouchpadEngine::EnforceCursor()
{
    if (!haveCursor_ || observeOnly_) return;
    const POINT actual = host_.CursorPos();
    if (actual.x != sentX_ || actual.y != sentY_) host_.MoveCursorTo(CursorPoint());
}

void TouchpadEngine::MoveCursor(double dx, double dy, int64_t dt)
{
    const double speed = std::hypot(dx, dy) / scale_ / double(std::max<int64_t>(1, dt));
    const double gain = settings_.Sensitivity *
        (1 + settings_.Acceleration * std::min(speed, settings_.MaxAccelSpeed));

    double x = curX_ + dx * gain;
    double y = curY_ + dy * gain;

    // Keep the pointer on a real monitor, the way Windows clamps a mouse, so that moving
    // back doesn't first have to "undo" travel into gaps between differently sized screens.
    const RECT m = host_.MonitorRectAt(POINT{ LONG(std::lround(x)), LONG(std::lround(y)) });
    x = std::clamp(x, double(m.left), double(m.right - 1));
    y = std::clamp(y, double(m.top), double(m.bottom - 1));
    curX_ = x;
    curY_ = y;

    const int px = int(std::lround(curX_)), py = int(std::lround(curY_));
    if (px == sentX_ && py == sentY_) return;
    sentX_ = px;
    sentY_ = py;
    host_.MoveCursorTo(CursorPoint());
}

// ------------------------------------------------------------------ scrolling

void TouchpadEngine::ScrollBy(double wx, double wy)
{
    wheelAccumX_ += wx;
    wheelAccumY_ += wy;
    const int ix = int(wheelAccumX_), iy = int(wheelAccumY_);
    wheelAccumX_ -= ix;
    wheelAccumY_ -= iy;
    EnsureCursor();
    if (iy != 0) host_.Wheel(CursorPoint(), iy);
    if (ix != 0) host_.HWheel(CursorPoint(), ix);
}

void TouchpadEngine::StartKinetic()
{
    if (!settings_.KineticScrolling || Now() - lastMultiMs_ > 60) return;
    if (std::abs(velX_) < 0.3 && std::abs(velY_) < 0.3) return;
    kineticActive_ = true;
    kineticLastMs_ = Now();
    StartTimer(TimerKinetic, 15);
}

void TouchpadEngine::StopKinetic()
{
    velX_ = velY_ = 0;
    if (!kineticActive_) return;
    kineticActive_ = false;
    StopTimer(TimerKinetic);
}

void TouchpadEngine::KineticFrame()
{
    if (!kineticActive_) return;

    const int64_t now = Now();
    const int64_t dt = std::max<int64_t>(1, now - kineticLastMs_);
    kineticLastMs_ = now;

    ScrollBy(velX_ * dt, velY_ * dt);
    const double decay = std::pow(settings_.KineticFriction, dt / 16.0);
    velX_ *= decay;
    velY_ *= decay;
    if (std::abs(velX_) < 0.05 && std::abs(velY_) < 0.05) StopKinetic();
}

// ------------------------------------------------------------------ helpers

void TouchpadEngine::HoldCtrl()
{
    if (ctrlHeld_) return;
    host_.KeyDown(VK_CONTROL);
    ctrlHeld_ = true;
}

void TouchpadEngine::ReleaseCtrl()
{
    if (!ctrlHeld_) return;
    host_.KeyUp(VK_CONTROL);
    ctrlHeld_ = false;
}

void TouchpadEngine::ReleaseAlt()
{
    if (!altHeld_) return;
    host_.KeyUp(VK_MENU);
    altHeld_ = false;
}

void TouchpadEngine::Centroid(double& cx, double& cy) const
{
    cx = cy = 0;
    if (contacts_.empty()) return;
    for (const auto& entry : contacts_)
    {
        cx += entry.second.x;
        cy += entry.second.y;
    }
    cx /= double(contacts_.size());
    cy /= double(contacts_.size());
}

// Average distance of the contacts from their centroid (the "size" of a pinch).
double TouchpadEngine::Spread() const
{
    if (contacts_.size() < 2) return 0;
    double cx, cy;
    Centroid(cx, cy);
    double total = 0;
    for (const auto& entry : contacts_) total += Distance(cx, cy, entry.second.x, entry.second.y);
    return total / double(contacts_.size());
}
