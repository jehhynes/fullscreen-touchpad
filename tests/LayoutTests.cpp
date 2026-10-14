#include "Layout.h"

#include <gtest/gtest.h>

namespace
{
    // Surface Pro 7: 2736x1824 at 200%, taskbar 96 px tall at the bottom.
    layout::Params Surface()
    {
        layout::Params p;
        p.monitor = RECT{ 0, 0, 2736, 1824 };
        p.work = RECT{ 0, 0, 2736, 1728 };
        p.scale = 2.0;
        return p;
    }

    bool InRegion(HRGN region, const RECT& window, POINT screen)
    {
        return PtInRegion(region, screen.x - window.left, screen.y - window.top) != FALSE;
    }
}

TEST(Layout, WindowExtendsAboveAndBelowTheScreenToHideTitleBarAndButtons)
{
    const RECT w = layout::WindowRect(Surface());
    EXPECT_EQ(w.left, 0);
    EXPECT_EQ(w.right, 2736);
    EXPECT_EQ(w.top, -80);           // 40 dip title offset at 200%
    EXPECT_EQ(w.bottom, 1728 + 700); // 350 dip below the work area
}

TEST(Layout, TouchAreaIsTheWorkAreaMinusEdgeStrips)
{
    const RECT t = layout::TouchRect(Surface());
    EXPECT_EQ(t.left, 2);
    EXPECT_EQ(t.top, 0);
    EXPECT_EQ(t.right, 2734);
    EXPECT_EQ(t.bottom, 1726);
}

TEST(Layout, PointerHoleIsASmallSquareAroundTheCursor)
{
    const RECT h = layout::PointerHole(Surface(), POINT{ 1000, 500 });
    EXPECT_EQ(h.left, 997);
    EXPECT_EQ(h.top, 497);
    EXPECT_EQ(h.right, 1004);
    EXPECT_EQ(h.bottom, 504);
}

TEST(Layout, NoHoleWhenThePointerIsOutsideTheTouchArea)
{
    const RECT h = layout::PointerHole(Surface(), POINT{ 1000, 1780 }); // over the taskbar
    EXPECT_EQ(h.right - h.left, 0);
}

TEST(Layout, HoleIsClippedAtTheTouchAreaEdge)
{
    const RECT h = layout::PointerHole(Surface(), POINT{ 3, 0 });
    EXPECT_EQ(h.left, 2);
    EXPECT_EQ(h.top, 0);
}

TEST(Layout, RegionCoversTheScreenExceptEdgesTaskbarAndPointer)
{
    const layout::Params p = Surface();
    const RECT window = layout::WindowRect(p);
    HRGN region = layout::CreateRegion(p, POINT{ 1000, 500 });

    EXPECT_TRUE(InRegion(region, window, POINT{ 1368, 900 }));   // middle of the screen
    EXPECT_TRUE(InRegion(region, window, POINT{ 10, 10 }));      // near the top-left corner
    EXPECT_FALSE(InRegion(region, window, POINT{ 1000, 500 }));  // under the pointer
    EXPECT_FALSE(InRegion(region, window, POINT{ 0, 900 }));     // left edge strip
    EXPECT_FALSE(InRegion(region, window, POINT{ 2735, 900 }));  // right edge strip
    EXPECT_FALSE(InRegion(region, window, POINT{ 1368, 1727 })); // bottom edge strip
    EXPECT_FALSE(InRegion(region, window, POINT{ 1368, 1780 })); // taskbar
    EXPECT_FALSE(InRegion(region, window, POINT{ 1368, -40 }));  // title bar, above the screen
    EXPECT_FALSE(InRegion(region, window, POINT{ 1368, 2000 })); // click buttons, below the screen
    DeleteObject(region);
}

TEST(Layout, TaskbarOnTheLeftIsLeftUncovered)
{
    layout::Params p = Surface();
    p.work = RECT{ 96, 0, 2736, 1824 };
    const RECT window = layout::WindowRect(p);
    HRGN region = layout::CreateRegion(p, POINT{ 1000, 500 });
    EXPECT_FALSE(InRegion(region, window, POINT{ 40, 900 }));  // taskbar
    EXPECT_TRUE(InRegion(region, window, POINT{ 200, 900 }));
    DeleteObject(region);
}

TEST(Layout, AutoHiddenTaskbarStillLeavesABottomEdgeStrip)
{
    layout::Params p = Surface();
    p.work = p.monitor; // auto-hide: the work area is the whole monitor
    const RECT window = layout::WindowRect(p);
    HRGN region = layout::CreateRegion(p, POINT{ 1000, 500 });
    EXPECT_FALSE(InRegion(region, window, POINT{ 1368, 1823 })); // swipe-up strip at the very bottom
    EXPECT_TRUE(InRegion(region, window, POINT{ 1368, 1700 }));
    DeleteObject(region);
}

TEST(Layout, OffsetsScaleWithDpi)
{
    layout::Params p = Surface();
    p.scale = 1.5;
    const RECT w = layout::WindowRect(p);
    EXPECT_EQ(w.top, -60);           // 40 dip
    EXPECT_EQ(w.bottom, 1728 + 525); // 350 dip
}

TEST(Layout, TouchKeyboardIsLeftUncoveredWhileShowing)
{
    layout::Params p = Surface();
    p.keyboard = RECT{ 0, 1100, 2736, 1728 }; // docked keyboard along the bottom of the work area
    const RECT window = layout::WindowRect(p);
    HRGN region = layout::CreateRegion(p, POINT{ 1000, 500 });
    EXPECT_FALSE(InRegion(region, window, POINT{ 1368, 1400 })); // a key
    EXPECT_TRUE(InRegion(region, window, POINT{ 1368, 900 }));   // above the keyboard is still touchpad
    DeleteObject(region);

    p.keyboard = RECT{}; // hidden again
    region = layout::CreateRegion(p, POINT{ 1000, 500 });
    EXPECT_TRUE(InRegion(region, window, POINT{ 1368, 1400 }));
    DeleteObject(region);
}

TEST(Layout, KeyboardCornerIsTheScreensCornerInsideTheEdgeStripsAndIsCutOut)
{
    const layout::Params p = Surface(); // 64 dip corner at 200% = 128 px
    const RECT c = layout::CornerRect(p);
    EXPECT_EQ(c.right, 2734);  // just inside the 2 px right edge strip
    EXPECT_EQ(c.bottom, 1822); // the screen's bottom, not the work area's: it covers the taskbar's corner
    EXPECT_EQ(c.right - c.left, 128);
    EXPECT_EQ(c.bottom - c.top, 128);
}

TEST(Layout, KeyboardCornerIsCutOutWhereItOverlapsTheTouchArea)
{
    layout::Params p = Surface();
    p.work.bottom = 1776; // collapsed tablet taskbar: the corner (1694..1822) overlaps the work area
    const RECT window = layout::WindowRect(p);
    HRGN region = layout::CreateRegion(p, POINT{ 1000, 500 });
    EXPECT_FALSE(InRegion(region, window, POINT{ 2700, 1750 })); // the touchpad's part of the corner is cut out
    EXPECT_TRUE(InRegion(region, window, POINT{ 2700, 1680 }));  // just above it
    EXPECT_TRUE(InRegion(region, window, POINT{ 2600, 1750 }));  // just left of it
    DeleteObject(region);
}

TEST(Layout, KeyboardCornerStaysPutWhenTheTaskbarCollapses)
{
    layout::Params p = Surface();
    const RECT expanded = layout::CornerRect(p);
    p.work.bottom = 1776; // collapsed tablet taskbar
    const RECT collapsed = layout::CornerRect(p);
    EXPECT_TRUE(EqualRect(&expanded, &collapsed));
}

TEST(Layout, KeyboardCornerCanBeTurnedOff)
{
    layout::Params p = Surface();
    p.cornerDip = 0;
    const RECT c = layout::CornerRect(p);
    EXPECT_EQ(c.right - c.left, 0);
    const RECT window = layout::WindowRect(p);
    HRGN region = layout::CreateRegion(p, POINT{ 1000, 500 });
    EXPECT_TRUE(InRegion(region, window, POINT{ 2700, 1700 }));
    DeleteObject(region);
}
