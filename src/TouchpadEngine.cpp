#include "TouchpadEngine.h"
#include "InputSender.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace
{
    double Distance(double x1, double y1, double x2, double y2)
    {
        return std::hypot(x2 - x1, y2 - y1);
    }
}

TouchpadEngine::TouchpadEngine(const Settings& settings, Callbacks callbacks)
    : settings_(settings), callbacks_(std::move(callbacks))
{
}

int64_t TouchpadEngine::Now()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
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
        dragCandidate_ = !observeOnly_ && now - lastTapMs_ <= settings_.DoubleTapWindowMs;
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
    if (Distance(c.startX, c.startY, x, y) > Slop()) moved_ = true;

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

void TouchpadEngine::Reset()
{
    contacts_.clear();
    if (inSession_) EndSession(true);
    StopKinetic();
}

// ------------------------------------------------------------------ gesture handling

void TouchpadEngine::OnContactCountChanged(bool added)
{
    if (observeOnly_) return;

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
            // Second touch shortly after a tap: becomes a drag once it clearly moves.
            if (fromStart <= Slop()) return;
            BeginCursorTracking();
            input::LeftDown();
            mode_ = Mode::Drag;
        }
        else
        {
            // Small dead zone so taps don't nudge the cursor.
            if (fromStart <= 3 * scale_) return;
            BeginCursorTracking();
            mode_ = Mode::Pointer;
        }
    }

    if (mode_ == Mode::Pointer || mode_ == Mode::Drag) MoveCursor(dx, dy, dt);
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
            input::Wheel(notch);
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
        while (cx - altTabLastX_ > step) { input::Chord({ VK_TAB }); altTabLastX_ += step; }
        while (altTabLastX_ - cx > step) { input::Chord({ VK_SHIFT, VK_TAB }); altTabLastX_ -= step; }
        return;
    }
    if (mode_ != Mode::None) return;

    const double dx = cx - baseCx_, dy = cy - baseCy_;
    if (std::max(std::abs(dx), std::abs(dy)) < settings_.SwipeThreshold * scale_) return;

    if (std::abs(dx) > std::abs(dy))
    {
        // Like the Windows touchpad: hold Alt and step through open apps while swiping.
        input::KeyDown(VK_MENU);
        altHeld_ = true;
        if (dx > 0) input::Chord({ VK_TAB });
        else input::Chord({ VK_SHIFT, VK_TAB });
        altTabLastX_ = cx;
        mode_ = Mode::AltTab;
    }
    else
    {
        if (dy < 0) input::Chord({ VK_LWIN, VK_TAB }); // Task View
        else input::Chord({ VK_LWIN, 'D' });           // Show desktop
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
        input::Chord({ VK_CONTROL, VK_LWIN, static_cast<WORD>(dx < 0 ? VK_RIGHT : VK_LEFT) });
    }
    else if (dy < 0) input::Chord({ VK_LWIN, VK_TAB });
    else input::Chord({ VK_LWIN, 'D' });
    mode_ = Mode::Done;
}

void TouchpadEngine::EndSession(bool canceled)
{
    inSession_ = false;
    const int64_t now = Now();
    const int64_t duration = now - sessionStartMs_;
    bool tapped = false;

    const bool tapCandidate = !canceled && !moved_ &&
        (mode_ == Mode::None || mode_ == Mode::Pointer || mode_ == Mode::TwoPending);

    if (tapCandidate && maxContacts_ == 4 && duration <= settings_.MultiTapMaxMs + 100)
    {
        if (callbacks_.toggleRequested) callbacks_.toggleRequested();
    }
    else if (tapCandidate && !observeOnly_ && settings_.TapToClick)
    {
        if (maxContacts_ == 1 && duration <= settings_.TapMaxMs)
        {
            input::LeftClick();
            tapped = true;
        }
        else if (maxContacts_ == 2 && duration <= settings_.MultiTapMaxMs) input::RightClick();
        else if (maxContacts_ == 3 && duration <= settings_.MultiTapMaxMs) input::MiddleClick();
    }

    if (mode_ == Mode::Drag) input::LeftUp();
    ReleaseCtrl();
    ReleaseAlt();

    lastTapMs_ = tapped ? now : INT64_MIN / 2;
    dragCandidate_ = false;
    mode_ = Mode::None;
}

// ------------------------------------------------------------------ cursor

void TouchpadEngine::BeginCursorTracking()
{
    POINT p{};
    GetCursorPos(&p);
    curX_ = sentX_ = p.x;
    curY_ = sentY_ = p.y;
}

void TouchpadEngine::MoveCursor(double dx, double dy, int64_t dt)
{
    // If something else moved the cursor (a real mouse, or Windows clamping us at a
    // screen edge), carry on from where it actually is.
    POINT actual{};
    if (GetCursorPos(&actual) &&
        Distance(actual.x, actual.y, sentX_, sentY_) > std::max(40 * scale_, 4 * (std::abs(dx) + std::abs(dy))))
    {
        curX_ = actual.x;
        curY_ = actual.y;
    }

    const double speed = std::hypot(dx, dy) / scale_ / double(std::max<int64_t>(1, dt));
    const double gain = settings_.Sensitivity *
        (1 + settings_.Acceleration * std::min(speed, settings_.MaxAccelSpeed));

    const int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int right = left + GetSystemMetrics(SM_CXVIRTUALSCREEN) - 1;
    const int bottom = top + GetSystemMetrics(SM_CYVIRTUALSCREEN) - 1;

    curX_ = std::clamp(curX_ + dx * gain, double(left), double(right));
    curY_ = std::clamp(curY_ + dy * gain, double(top), double(bottom));

    const int x = int(std::lround(curX_)), y = int(std::lround(curY_));
    if (x == sentX_ && y == sentY_) return;
    sentX_ = x;
    sentY_ = y;
    input::MoveCursorTo(x, y);
}

// ------------------------------------------------------------------ scrolling

void TouchpadEngine::ScrollBy(double wx, double wy)
{
    wheelAccumX_ += wx;
    wheelAccumY_ += wy;
    const int ix = int(wheelAccumX_), iy = int(wheelAccumY_);
    wheelAccumX_ -= ix;
    wheelAccumY_ -= iy;
    input::Wheel(iy);
    input::HWheel(ix);
}

void TouchpadEngine::StartKinetic()
{
    if (!settings_.KineticScrolling || Now() - lastMultiMs_ > 60) return;
    if (std::abs(velX_) < 0.3 && std::abs(velY_) < 0.3) return;
    kineticActive_ = true;
    kineticLastMs_ = Now();
    if (callbacks_.setAnimationTimer) callbacks_.setAnimationTimer(true);
}

void TouchpadEngine::StopKinetic()
{
    velX_ = velY_ = 0;
    if (!kineticActive_) return;
    kineticActive_ = false;
    if (callbacks_.setAnimationTimer) callbacks_.setAnimationTimer(false);
}

void TouchpadEngine::OnAnimationTick()
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
    input::KeyDown(VK_CONTROL);
    ctrlHeld_ = true;
}

void TouchpadEngine::ReleaseCtrl()
{
    if (!ctrlHeld_) return;
    input::KeyUp(VK_CONTROL);
    ctrlHeld_ = false;
}

void TouchpadEngine::ReleaseAlt()
{
    if (!altHeld_) return;
    input::KeyUp(VK_MENU);
    altHeld_ = false;
}

void TouchpadEngine::Centroid(double& cx, double& cy) const
{
    cx = cy = 0;
    if (contacts_.empty()) return;
    for (const auto& [id, c] : contacts_)
    {
        cx += c.x;
        cy += c.y;
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
    for (const auto& [id, c] : contacts_) total += Distance(cx, cy, c.x, c.y);
    return total / double(contacts_.size());
}
