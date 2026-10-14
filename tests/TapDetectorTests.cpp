#include "TapDetector.h"

#include <gtest/gtest.h>

namespace
{
    using Contacts = std::vector<TapDetector::Contact>;

    Contacts Fingers(int n, double dx = 0, double dy = 0)
    {
        Contacts c;
        for (int i = 0; i < n; ++i) c.push_back({ uint32_t(i), 0.3 + 0.1 * i + dx, 0.5 + dy });
        return c;
    }

    // Reports every 8 ms for `durationMs`, then polls until the session ends. Returns whether it toggled.
    bool Touch(TapDetector& d, int64_t& now, int fingers, int durationMs, double travel = 0)
    {
        const int frames = std::max(1, durationMs / 8);
        for (int i = 0; i <= frames; ++i)
        {
            const double t = travel * i / frames;
            d.OnReport(now, fingers, Fingers(fingers, t, 0));
            now += 8;
        }
        bool toggled = false;
        for (int i = 0; i < 40; ++i)
        {
            toggled = d.Poll(now) || toggled;
            now += 10;
        }
        return toggled;
    }
}

TEST(TapDetector, FourFingerTapToggles)
{
    TapDetector d;
    int64_t now = 1000;
    EXPECT_TRUE(Touch(d, now, 4, 150));
}

TEST(TapDetector, FiresOnlyOncePerTap)
{
    TapDetector d;
    int64_t now = 1000;
    for (int i = 0; i < 3; ++i) d.OnReport(now += 8, 4, Fingers(4));
    int fired = 0;
    for (int i = 0; i < 100; ++i) fired += d.Poll(now += 10);
    EXPECT_EQ(fired, 1);
}

TEST(TapDetector, WaitsForAllFingersToLift)
{
    TapDetector d;
    int64_t now = 1000;
    d.OnReport(now, 4, Fingers(4));
    EXPECT_FALSE(d.Poll(now + 100)); // still within the idle window
    EXPECT_TRUE(d.InSession());
    EXPECT_TRUE(d.Poll(now + 200));
}

TEST(TapDetector, FewerFingersDoNotToggle)
{
    TapDetector d;
    int64_t now = 1000;
    EXPECT_FALSE(Touch(d, now, 1, 100));
    EXPECT_FALSE(Touch(d, now, 2, 100));
    EXPECT_FALSE(Touch(d, now, 3, 100));
}

TEST(TapDetector, FiveFingersAlsoToggle)
{
    TapDetector d;
    int64_t now = 1000;
    EXPECT_TRUE(Touch(d, now, 5, 150));
}

TEST(TapDetector, SlowTouchDoesNotToggle)
{
    TapDetector d;
    int64_t now = 1000;
    EXPECT_FALSE(Touch(d, now, 4, 1200));
}

TEST(TapDetector, FourFingerSwipeDoesNotToggle)
{
    TapDetector d;
    int64_t now = 1000;
    EXPECT_FALSE(Touch(d, now, 4, 300, 0.2));
}

TEST(TapDetector, SmallJitterStillCountsAsATap)
{
    TapDetector d;
    int64_t now = 1000;
    EXPECT_TRUE(Touch(d, now, 4, 200, 0.01));
}

TEST(TapDetector, CountsFingersWhenOnlyTheFirstReportCarriesTheCount)
{
    // Hybrid-mode digitizers split a frame across reports; only the first carries the contact count.
    TapDetector d;
    int64_t now = 1000;
    for (int i = 0; i < 10; ++i)
    {
        d.OnReport(now, 4, Fingers(2));
        d.OnReport(now, 0, Contacts{ { 2, 0.5, 0.5 }, { 3, 0.6, 0.5 } });
        now += 8;
    }
    bool toggled = false;
    for (int i = 0; i < 40; ++i) toggled = d.Poll(now += 10) || toggled;
    EXPECT_TRUE(toggled);
}

TEST(TapDetector, ConfigurableFingerCount)
{
    TapDetector::Params p;
    p.fingers = 3;
    TapDetector d(p);
    int64_t now = 1000;
    EXPECT_TRUE(Touch(d, now, 3, 150));
}

TEST(TapDetector, VerticalSwipeDoesNotToggle)
{
    TapDetector d;
    int64_t now = 1000;
    for (int i = 0; i <= 30; ++i) d.OnReport(now += 8, 4, Fingers(4, 0, 0.2 * i / 30));
    bool toggled = false;
    for (int i = 0; i < 40; ++i) toggled = d.Poll(now += 10) || toggled;
    EXPECT_FALSE(toggled);
    EXPECT_TRUE(d.LastSession().moved);
}

TEST(TapDetector, DurationLimitIsInclusive)
{
    TapDetector::Params p;
    TapDetector d(p);
    int64_t now = 1000;
    d.OnReport(now, 4, Fingers(4));
    d.OnReport(now + p.maxDurationMs, 4, Fingers(4));
    EXPECT_TRUE(d.Poll(now + p.maxDurationMs + p.idleMs));

    d.OnReport(now += 5000, 4, Fingers(4));
    d.OnReport(now + p.maxDurationMs + 1, 4, Fingers(4));
    EXPECT_FALSE(d.Poll(now + p.maxDurationMs + 1 + p.idleMs));
}

TEST(TapDetector, CountOnlyReportsWithoutPositionsStillToggle)
{
    // When the digitizer's coordinate range can't be read, reports carry a finger count but no positions.
    TapDetector d;
    int64_t now = 1000;
    for (int i = 0; i < 10; ++i) d.OnReport(now += 8, 4, {});
    bool toggled = false;
    for (int i = 0; i < 40; ++i) toggled = d.Poll(now += 10) || toggled;
    EXPECT_TRUE(toggled);
}

TEST(TapDetector, SummaryDescribesTheLastTouch)
{
    TapDetector d;
    int64_t now = 1000;
    Touch(d, now, 2, 300);
    EXPECT_EQ(d.LastSession().maxFingers, 2);
    EXPECT_FALSE(d.LastSession().toggled);
}
