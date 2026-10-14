#include "CornerGesture.h"

#include <gtest/gtest.h>

#include <vector>

namespace
{
    using Event = CornerGesture::Event;

    // The corner is x, y > 0.9.
    CornerGesture Make()
    {
        return CornerGesture({}, [](double x, double y) { return x > 0.9 && y > 0.9; });
    }

    // One finger at (x, y) for durationMs, reporting every 10 ms, then lifted; polls every 50 ms
    // throughout and until the gesture has seen the lift. Returns the events, in order.
    std::vector<Event> Press(CornerGesture& g, int64_t& now, int durationMs, double x = 0.95, double y = 0.95,
                             double slideX = 0)
    {
        std::vector<Event> events;
        auto poll = [&] {
            const Event e = g.Poll(now);
            if (e != Event::None) events.push_back(e);
        };
        const int64_t start = now;
        for (; now - start <= durationMs; now += 10)
        {
            const double t = durationMs ? double(now - start) / durationMs : 1;
            g.OnReport(now, 1, { { 1, x + slideX * t, y } });
            if ((now - start) % 50 == 0) poll();
        }
        for (int i = 0; i < 6; ++i, now += 50) poll();
        return events;
    }
}

TEST(CornerGesture, HoldStartsWhileTheFingerIsDownAndEndsWhenItLifts)
{
    CornerGesture g = Make();
    int64_t now = 1000;
    g.OnReport(now, 1, { { 1, 0.95, 0.95 } });
    for (int i = 0; i < 25; ++i) // 250 ms: not yet a hold
    {
        now += 10;
        g.OnReport(now, 1, { { 1, 0.95, 0.95 } });
        EXPECT_EQ(g.Poll(now), Event::None);
    }
    Event started = Event::None;
    for (int i = 0; i < 10 && started == Event::None; ++i)
    {
        now += 10;
        g.OnReport(now, 1, { { 1, 0.95, 0.95 } });
        started = g.Poll(now);
    }
    EXPECT_EQ(started, Event::HoldStart); // before the finger lifts
    EXPECT_TRUE(g.Holding());

    EXPECT_EQ(g.Poll(now + 200), Event::HoldEnd);
    EXPECT_FALSE(g.Holding());
}

TEST(CornerGesture, LongHoldGivesExactlyOneStartAndOneEnd)
{
    CornerGesture g = Make();
    int64_t now = 1000;
    EXPECT_EQ(Press(g, now, 3000), (std::vector<Event>{ Event::HoldStart, Event::HoldEnd }));
}

TEST(CornerGesture, DoubleTap)
{
    CornerGesture g = Make();
    int64_t now = 1000;
    EXPECT_TRUE(Press(g, now, 80).empty());
    now += 50;
    EXPECT_EQ(Press(g, now, 80), (std::vector<Event>{ Event::DoubleTap }));
}

TEST(CornerGesture, SingleTapDoesNothing)
{
    CornerGesture g = Make();
    int64_t now = 1000;
    EXPECT_TRUE(Press(g, now, 80).empty());
}

TEST(CornerGesture, TapsTooFarApartAreNotADoubleTap)
{
    CornerGesture g = Make();
    int64_t now = 1000;
    Press(g, now, 80);
    now += 1000;
    EXPECT_TRUE(Press(g, now, 80).empty());
}

TEST(CornerGesture, ThreeTapsAreOneDoubleTap)
{
    CornerGesture g = Make();
    int64_t now = 1000;
    Press(g, now, 80);
    EXPECT_EQ(Press(g, now, 80), (std::vector<Event>{ Event::DoubleTap }));
    EXPECT_TRUE(Press(g, now, 80).empty());
}

TEST(CornerGesture, HoldThenTapIsNotADoubleTap)
{
    CornerGesture g = Make();
    int64_t now = 1000;
    Press(g, now, 1000);
    EXPECT_TRUE(Press(g, now, 80).empty());
}

TEST(CornerGesture, TouchesOutsideTheCornerAreIgnored)
{
    CornerGesture g = Make();
    int64_t now = 1000;
    EXPECT_TRUE(Press(g, now, 1000, 0.5, 0.5).empty());
    Press(g, now, 80, 0.5, 0.5);
    EXPECT_TRUE(Press(g, now, 80, 0.5, 0.5).empty());
}

TEST(CornerGesture, SlidingOffIsNotAHold)
{
    CornerGesture g = Make();
    int64_t now = 1000;
    EXPECT_TRUE(Press(g, now, 1000, 0.95, 0.95, -0.3).empty());
}

TEST(CornerGesture, TwoFingersAreNotAHold)
{
    CornerGesture g = Make();
    int64_t now = 1000;
    for (int i = 0; i < 100; ++i, now += 10)
    {
        g.OnReport(now, 2, { { 1, 0.95, 0.95 }, { 2, 0.5, 0.5 } });
        EXPECT_EQ(g.Poll(now), Event::None);
    }
    EXPECT_EQ(g.Poll(now + 200), Event::None);
}

TEST(CornerGesture, QuickDoubleTapWithLiftReportsIsSeenAsTwoTaps)
{
    // The digitizer reports the lift (no finger touching); the second tap comes only 60 ms later,
    // well inside the idle timeout, and a poll may not happen in between.
    CornerGesture g = Make();
    int64_t now = 1000;
    std::vector<Event> events;
    auto tap = [&] {
        for (int i = 0; i < 8; ++i, now += 10) g.OnReport(now, 1, { { 1, 0.95, 0.95 } });
        g.OnReport(now, 0, {});
    };
    tap();
    now += 60;
    tap();
    for (int i = 0; i < 6; ++i, now += 50)
        if (const Event e = g.Poll(now); e != Event::None) events.push_back(e);
    EXPECT_EQ(events, (std::vector<Event>{ Event::DoubleTap }));
}

TEST(CornerGesture, AHoldEndsPromptlyWhenTheLiftIsReported)
{
    CornerGesture g = Make();
    int64_t now = 1000;
    for (int i = 0; i < 40; ++i, now += 10)
    {
        g.OnReport(now, 1, { { 1, 0.95, 0.95 } });
        g.Poll(now);
    }
    ASSERT_TRUE(g.Holding());
    g.OnReport(now, 0, {});
    EXPECT_EQ(g.Poll(now + 40), Event::HoldEnd); // well before the 150 ms idle timeout
}
