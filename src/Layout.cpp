#include "Layout.h"

#include <algorithm>
#include <cmath>

namespace layout
{
    namespace
    {
        int Scaled(int dip, double scale) { return int(std::lround(dip * scale)); }

        bool Contains(const RECT& r, POINT pt)
        {
            return pt.x >= r.left && pt.x < r.right && pt.y >= r.top && pt.y < r.bottom;
        }
    }

    RECT WindowRect(const Params& p)
    {
        return RECT{ p.monitor.left, p.monitor.top - Scaled(p.titleOffsetDip, p.scale), p.monitor.right,
                     p.work.bottom + Scaled(p.extraBottomDip, p.scale) };
    }

    RECT TouchRect(const Params& p)
    {
        return RECT{ p.work.left + p.edgeGap, p.work.top, p.work.right - p.edgeGap, p.work.bottom - p.edgeGap };
    }

    RECT PointerHole(const Params& p, POINT cursor)
    {
        const RECT touch = TouchRect(p);
        if (!Contains(touch, cursor)) return RECT{};
        const int r = p.holeRadius;
        return RECT{ std::max(touch.left, cursor.x - r), std::max(touch.top, cursor.y - r),
                     std::min(touch.right, cursor.x + r + 1), std::min(touch.bottom, cursor.y + r + 1) };
    }

    RECT CornerRect(const Params& p)
    {
        if (p.cornerDip <= 0) return RECT{};
        // The screen's own corner (not the work area's), so a finger can find it against the bezel
        // whatever state the taskbar is in. Full-screen apps such as Remote Desktop draw over the
        // taskbar's strip, so the work area's corner would float invisibly above the real one.
        const LONG right = p.monitor.right - p.edgeGap, bottom = p.monitor.bottom - p.edgeGap;
        const LONG size = Scaled(p.cornerDip, p.scale);
        return RECT{ std::max(p.monitor.left, right - size), std::max(p.monitor.top, bottom - size), right, bottom };
    }

    POINT TouchToScreen(double x, double y, const RECT& monitor, int orientation)
    {
        // The panel reports in its native orientation. DMDO_90 means the display is turned 90 degrees
        // clockwise, so its native left edge is now at the top and its native top edge on the right.
        double sx = x, sy = y;
        switch (orientation)
        {
        case DMDO_90: sx = 1 - y; sy = x; break;
        case DMDO_180: sx = 1 - x; sy = 1 - y; break;
        case DMDO_270: sx = y; sy = 1 - x; break;
        }
        const LONG w = monitor.right - monitor.left, h = monitor.bottom - monitor.top;
        return POINT{ monitor.left + std::lround(sx * (w - 1)), monitor.top + std::lround(sy * (h - 1)) };
    }

    HRGN CreateRegion(const Params& p, POINT cursor)
    {
        const RECT window = WindowRect(p);
        const RECT touch = TouchRect(p);
        HRGN region = CreateRectRgn(touch.left - window.left, touch.top - window.top, touch.right - window.left,
                                    touch.bottom - window.top);

        auto cutOut = [&](const RECT& r) {
            if (r.right <= r.left || r.bottom <= r.top) return;
            HRGN cut = CreateRectRgn(r.left - window.left, r.top - window.top, r.right - window.left,
                                     r.bottom - window.top);
            CombineRgn(region, region, cut, RGN_DIFF);
            DeleteObject(cut);
        };
        cutOut(p.keyboard);
        cutOut(CornerRect(p));
        cutOut(PointerHole(p, cursor));
        return region;
    }
}
