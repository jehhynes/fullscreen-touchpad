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

    HRGN CreateRegion(const Params& p, POINT cursor)
    {
        const RECT window = WindowRect(p);
        const RECT touch = TouchRect(p);
        HRGN region = CreateRectRgn(touch.left - window.left, touch.top - window.top, touch.right - window.left,
                                    touch.bottom - window.top);

        const RECT hole = PointerHole(p, cursor);
        if (hole.right > hole.left && hole.bottom > hole.top)
        {
            HRGN holeRegion = CreateRectRgn(hole.left - window.left, hole.top - window.top, hole.right - window.left,
                                            hole.bottom - window.top);
            CombineRgn(region, region, holeRegion, RGN_DIFF);
            DeleteObject(holeRegion);
        }
        return region;
    }
}
