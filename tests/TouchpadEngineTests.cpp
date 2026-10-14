#include "TouchpadEngine.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace
{
    std::string Pt(POINT p) { return std::to_string(p.x) + "," + std::to_string(p.y); }

    // Records everything the engine does, with a fake clock and fake timers.
    class FakeHost : public TouchpadHost
    {
    public:
        int64_t now = 1000;
        POINT cursor{ 500, 500 };
        RECT monitor{ 0, 0, 1920, 1080 };
        std::vector<std::string> events; // clicks, buttons, wheel and keys (not plain moves)
        int toggles = 0;
        int leftClickedNotifications = 0;
        TouchpadEngine* engine = nullptr;

        int64_t NowMs() override { return now; }
        POINT CursorPos() override { return cursor; }
        RECT MonitorRectAt(POINT) override { return monitor; }
        void StartTimer(int id, unsigned ms) override { timers_[id] = Timer{ now + ms, ms }; }
        void StopTimer(int id) override { timers_.erase(id); }

        void MoveCursorTo(POINT at) override { cursor = at; }
        void LeftDown(POINT at) override { Record("down", at); }
        void LeftUp(POINT at) override { Record("up", at); }
        void LeftClick(POINT at) override { Record("click", at); }
        void RightClick(POINT at) override { Record("rclick", at); }
        void MiddleClick(POINT at) override { Record("mclick", at); }
        void Wheel(POINT at, int delta) override { cursor = at; events.push_back("wheel " + std::to_string(delta)); }
        void HWheel(POINT at, int delta) override { cursor = at; events.push_back("hwheel " + std::to_string(delta)); }
        void KeyDown(WORD vk) override { events.push_back("keydown " + std::to_string(vk)); }
        void KeyUp(WORD vk) override { events.push_back("keyup " + std::to_string(vk)); }
        void Chord(std::initializer_list<WORD> keys) override
        {
            std::string s = "chord ";
            for (WORD k : keys) s += (s.size() > 6 ? "+" : "") + std::to_string(k);
            events.push_back(s);
        }
        void ToggleRequested() override { ++toggles; }
        void LeftClicked() override { ++leftClickedNotifications; }

        // Moves the clock forward, firing timers (which repeat, like Win32 SetTimer) on the way.
        void Advance(int64_t ms)
        {
            const int64_t target = now + ms;
            for (;;)
            {
                auto next = timers_.end();
                for (auto it = timers_.begin(); it != timers_.end(); ++it)
                    if (it->second.due <= target && (next == timers_.end() || it->second.due < next->second.due))
                        next = it;
                if (next == timers_.end()) break;
                now = next->second.due;
                const int id = next->first;
                next->second.due += next->second.period;
                engine->OnTimer(id);
            }
            now = target;
        }

        int SumOf(const std::string& prefix) const
        {
            int total = 0;
            for (const auto& e : events)
                if (e.rfind(prefix + " ", 0) == 0) total += std::stoi(e.substr(prefix.size() + 1));
            return total;
        }

        int Count(const std::string& prefix) const
        {
            int n = 0;
            for (const auto& e : events)
                if (e.rfind(prefix, 0) == 0) ++n;
            return n;
        }

    private:
        struct Timer { int64_t due; unsigned period; };
        std::map<int, Timer> timers_;

        void Record(const char* what, POINT at)
        {
            cursor = at;
            events.push_back(std::string(what) + " " + Pt(at));
        }
    };

    using Events = std::vector<std::string>;

    class EngineTest : public ::testing::Test
    {
    protected:
        FakeHost host;
        Settings settings; // defaults: TapMaxMs 220, DoubleTapWindowMs 250, LongPressMs 600, TapSlop 12
        std::unique_ptr<TouchpadEngine> engine;

        void SetUp() override { Recreate(); }

        void Recreate()
        {
            engine = std::make_unique<TouchpadEngine>(settings, host);
            host.engine = engine.get();
            engine->SetDpi(96);
        }

        void Down(uint32_t id, int x, int y) { engine->OnDown(id, x, y); }
        void Move(uint32_t id, int x, int y) { engine->OnMove(id, x, y); }
        void Up(uint32_t id) { engine->OnUp(id, false); }

        // One finger touching down and lifting `holdMs` later without moving.
        void Tap(int x = 100, int y = 100, int holdMs = 50)
        {
            Down(1, x, y);
            host.Advance(holdMs);
            Up(1);
        }

        // Moves one finger in steps 8 ms apart.
        void Swipe(uint32_t id, int x0, int y0, int x1, int y1, int steps = 10)
        {
            for (int i = 1; i <= steps; ++i)
            {
                host.Advance(8);
                Move(id, x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps);
            }
        }

        // Moves several fingers together by (dx, dy), each starting from its own point.
        void SwipeFingers(const std::vector<POINT>& starts, int dx, int dy, int steps = 10)
        {
            for (int i = 1; i <= steps; ++i)
            {
                host.Advance(8);
                for (size_t f = 0; f < starts.size(); ++f)
                    Move(uint32_t(f + 1), starts[f].x + dx * i / steps, starts[f].y + dy * i / steps);
            }
        }

        // Pinch: two fingers on a horizontal line moving apart (positive) or together (negative).
        void Pinch(int left, int right, int y, int spreadEach, int steps = 10)
        {
            for (int i = 1; i <= steps; ++i)
            {
                host.Advance(8);
                Move(1, left - spreadEach * i / steps, y);
                Move(2, right + spreadEach * i / steps, y);
            }
        }
    };
}

// ------------------------------------------------------------------ taps and clicks

TEST_F(EngineTest, SingleTapClicksOnceTheDoubleTapWindowExpires)
{
    Tap();
    EXPECT_TRUE(host.events.empty()) << "click is held back while waiting for a possible second tap";

    host.Advance(int64_t(settings.DoubleTapWindowMs) - 1);
    EXPECT_TRUE(host.events.empty());

    host.Advance(1);
    EXPECT_EQ(host.events, Events({ "click 500,500" }));
    EXPECT_EQ(host.leftClickedNotifications, 1);
}

TEST_F(EngineTest, DoubleTapDoubleClicks)
{
    Tap();
    host.Advance(100);
    Tap();
    EXPECT_EQ(host.events, Events({ "click 500,500", "click 500,500" }));

    host.Advance(1000);
    EXPECT_EQ(host.events.size(), 2u) << "no extra click once the window passes";
}

TEST_F(EngineTest, TapsFartherApartThanTheWindowAreTwoSingleClicks)
{
    Tap();
    host.Advance(400);
    Tap();
    host.Advance(400);
    EXPECT_EQ(host.events, Events({ "click 500,500", "click 500,500" }));
}

TEST_F(EngineTest, ClicksLandAtThePointerNotUnderTheFinger)
{
    engine->SyncCursor(POINT{ 800, 600 });
    Tap(100, 100);
    host.Advance(300);
    EXPECT_EQ(host.events, Events({ "click 800,600" }));
}

TEST_F(EngineTest, TapThatLastsTooLongIsNotAClick)
{
    Tap(100, 100, int(settings.TapMaxMs) + 50);
    host.Advance(1000);
    EXPECT_TRUE(host.events.empty());
}

TEST_F(EngineTest, TapToClickCanBeTurnedOff)
{
    settings.TapToClick = false;
    Recreate();
    Tap();
    host.Advance(1000);
    EXPECT_TRUE(host.events.empty());
}

// ------------------------------------------------------------------ drag

TEST_F(EngineTest, TapThenHoldPressesTheButtonWithoutClickingFirst)
{
    Tap();
    host.Advance(100);
    Down(1, 100, 100);
    host.Advance(int(settings.TapMaxMs) + 10);
    EXPECT_EQ(host.events, Events({ "down 500,500" }));

    Swipe(1, 100, 100, 200, 100);
    EXPECT_GT(host.cursor.x, 500) << "holding the button, the finger drags the pointer";

    Up(1);
    EXPECT_EQ(host.events.back(), "up " + Pt(host.cursor));
    EXPECT_EQ(host.Count("click"), 0) << "the first tap must not also click (it would look like a double-click)";
}

TEST_F(EngineTest, TapThenTouchAndMoveDragsImmediately)
{
    Tap();
    host.Advance(100);
    Down(1, 100, 100);
    Swipe(1, 100, 100, 100, 250);

    ASSERT_FALSE(host.events.empty());
    EXPECT_EQ(host.events.front(), "down 500,500");
    EXPECT_GT(host.cursor.y, 500);

    Up(1);
    EXPECT_EQ(host.events.back(), "up " + Pt(host.cursor));
    EXPECT_EQ(host.Count("click"), 0);
}

TEST_F(EngineTest, DragHeldStillThenReleasedIsAPressAndRelease)
{
    Tap();
    host.Advance(100);
    Down(1, 100, 100);
    host.Advance(1000);
    Up(1);
    EXPECT_EQ(host.events, Events({ "down 500,500", "up 500,500" }));
}

// ------------------------------------------------------------------ right / middle click

TEST_F(EngineTest, LongPressRightClicks)
{
    Down(1, 100, 100);
    host.Advance(int(settings.LongPressMs) - 1);
    EXPECT_TRUE(host.events.empty());

    host.Advance(1);
    EXPECT_EQ(host.events, Events({ "rclick 500,500" }));

    host.Advance(1000);
    Up(1);
    host.Advance(1000);
    EXPECT_EQ(host.events, Events({ "rclick 500,500" })) << "lifting after a long press does nothing more";
}

TEST_F(EngineTest, MovingCancelsTheLongPress)
{
    Down(1, 100, 100);
    Swipe(1, 100, 100, 200, 100);
    host.Advance(1000);
    Up(1);
    host.Advance(1000);
    EXPECT_EQ(host.Count("rclick"), 0);
    EXPECT_EQ(host.Count("click"), 0);
}

TEST_F(EngineTest, LongPressRightClickCanBeTurnedOff)
{
    settings.LongPressRightClick = false;
    Recreate();
    Down(1, 100, 100);
    host.Advance(2000);
    Up(1);
    host.Advance(1000);
    EXPECT_TRUE(host.events.empty());
}

TEST_F(EngineTest, TwoFingerTapRightClicks)
{
    Down(1, 100, 100);
    Down(2, 160, 100);
    host.Advance(60);
    Up(1);
    Up(2);
    EXPECT_EQ(host.events, Events({ "rclick 500,500" }));
}

TEST_F(EngineTest, ThreeFingerTapMiddleClicks)
{
    Down(1, 100, 100);
    Down(2, 160, 100);
    Down(3, 220, 100);
    host.Advance(60);
    Up(1);
    Up(2);
    Up(3);
    EXPECT_EQ(host.events, Events({ "mclick 500,500" }));
}

TEST_F(EngineTest, TapFollowedByTwoFingersDeliversTheClickFirst)
{
    Tap();
    host.Advance(100);
    Down(1, 100, 100);
    Down(2, 160, 100);
    EXPECT_EQ(host.events, Events({ "click 500,500" }));
}

// ------------------------------------------------------------------ pointer

TEST_F(EngineTest, OneFingerMovesThePointerRelativelyNotToTheFinger)
{
    Down(1, 1500, 900);
    Swipe(1, 1500, 900, 1600, 900);
    Up(1);

    EXPECT_GT(host.cursor.x, 600) << "pointer moved right by roughly the finger travel, with acceleration";
    EXPECT_LT(host.cursor.x, 1000);
    EXPECT_EQ(host.cursor.y, 500);
    host.Advance(1000);
    EXPECT_TRUE(host.events.empty()) << "a move is not a tap";
}

TEST_F(EngineTest, PointerStaysOnTheMonitor)
{
    Down(1, 100, 100);
    Swipe(1, 100, 100, 1900, 100, 5);
    Swipe(1, 1900, 100, 100, 100, 1);
    Swipe(1, 100, 100, 1900, 100, 5);
    EXPECT_LE(host.cursor.x, 1919);
    EXPECT_GE(host.cursor.x, 0);
}

TEST_F(EngineTest, EnforceCursorUndoesWindowsMovingTheCursorToTheFinger)
{
    engine->SyncCursor(POINT{ 800, 600 });
    Down(1, 100, 100);
    host.cursor = POINT{ 100, 100 }; // what Windows does on touch
    engine->EnforceCursor();
    EXPECT_EQ(Pt(host.cursor), "800,600");
}

// ------------------------------------------------------------------ scroll and zoom

TEST_F(EngineTest, TwoFingersUpScrollsDownWithNaturalScrolling)
{
    Down(1, 500, 600);
    Down(2, 600, 600);
    SwipeFingers({ { 500, 600 }, { 600, 600 } }, 0, -150);
    Up(1);
    Up(2);

    EXPECT_LT(host.SumOf("wheel"), 0) << "negative wheel scrolls the content down";
    EXPECT_EQ(host.SumOf("hwheel"), 0) << "a mostly vertical scroll is locked to the vertical axis";
    EXPECT_EQ(host.Count("rclick"), 0) << "a two-finger scroll is not a two-finger tap";
}

TEST_F(EngineTest, ClassicScrollingReversesDirection)
{
    settings.NaturalScrolling = false;
    settings.KineticScrolling = false;
    Recreate();
    Down(1, 500, 600);
    Down(2, 600, 600);
    SwipeFingers({ { 500, 600 }, { 600, 600 } }, 0, -150);
    Up(1);
    Up(2);
    EXPECT_GT(host.SumOf("wheel"), 0);
}

TEST_F(EngineTest, FlickKeepsScrollingAfterLift)
{
    Down(1, 500, 600);
    Down(2, 600, 600);
    SwipeFingers({ { 500, 600 }, { 600, 600 } }, 0, -150);
    Up(1);
    Up(2);
    const int atLift = host.SumOf("wheel");

    host.Advance(1000);
    EXPECT_LT(host.SumOf("wheel"), atLift) << "kinetic scrolling continues in the same direction";
}

TEST_F(EngineTest, TouchingStopsKineticScrolling)
{
    Down(1, 500, 600);
    Down(2, 600, 600);
    SwipeFingers({ { 500, 600 }, { 600, 600 } }, 0, -150);
    Up(1);
    Up(2);
    host.Advance(50);
    Down(1, 500, 500);
    const size_t count = host.events.size();
    host.Advance(100);
    EXPECT_EQ(host.events.size(), count);
}

TEST_F(EngineTest, PinchOutZoomsInWithCtrlWheel)
{
    Down(1, 500, 500);
    Down(2, 600, 500);
    Pinch(500, 600, 500, 200);
    Up(1);
    Up(2);

    ASSERT_GE(host.events.size(), 3u);
    EXPECT_EQ(host.events.front(), "keydown 17");
    EXPECT_GT(host.SumOf("wheel"), 0);
    EXPECT_EQ(host.SumOf("wheel") % WHEEL_DELTA, 0) << "zoom uses whole wheel notches";
    EXPECT_EQ(host.events.back(), "keyup 17");
}

TEST_F(EngineTest, PinchInZoomsOut)
{
    Down(1, 300, 500);
    Down(2, 800, 500);
    Pinch(300, 800, 500, -200);
    Up(1);
    Up(2);
    EXPECT_LT(host.SumOf("wheel"), 0);
    EXPECT_EQ(host.events.back(), "keyup 17");
}

// ------------------------------------------------------------------ swipes and toggle

TEST_F(EngineTest, ThreeFingerSwipeUpOpensTaskView)
{
    Down(1, 400, 600);
    Down(2, 500, 600);
    Down(3, 600, 600);
    SwipeFingers({ { 400, 600 }, { 500, 600 }, { 600, 600 } }, 0, -200);
    Up(1);
    Up(2);
    Up(3);
    EXPECT_EQ(host.events, Events({ "chord 91+9" })); // Win+Tab
}

TEST_F(EngineTest, ThreeFingerSwipeDownShowsDesktop)
{
    Down(1, 400, 300);
    Down(2, 500, 300);
    Down(3, 600, 300);
    SwipeFingers({ { 400, 300 }, { 500, 300 }, { 600, 300 } }, 0, 200);
    Up(1);
    Up(2);
    Up(3);
    EXPECT_EQ(host.events, Events({ "chord 91+68" })); // Win+D
}

TEST_F(EngineTest, ThreeFingerSwipeSidewaysHoldsAltUntilLift)
{
    Down(1, 400, 600);
    Down(2, 500, 600);
    Down(3, 600, 600);
    SwipeFingers({ { 400, 600 }, { 500, 600 }, { 600, 600 } }, 400, 0);
    EXPECT_EQ(host.events.front(), "keydown 18");
    EXPECT_GE(host.Count("chord 9"), 2) << "keeps stepping through apps while swiping";
    EXPECT_EQ(host.Count("keyup 18"), 0);

    Up(1);
    Up(2);
    Up(3);
    EXPECT_EQ(host.events.back(), "keyup 18");
}

TEST_F(EngineTest, FourFingerSwipeLeftGoesToTheNextDesktop)
{
    Down(1, 900, 500);
    Down(2, 1000, 500);
    Down(3, 1100, 500);
    Down(4, 1200, 500);
    SwipeFingers({ { 900, 500 }, { 1000, 500 }, { 1100, 500 }, { 1200, 500 } }, -200, 0);
    Up(1);
    Up(2);
    Up(3);
    Up(4);
    EXPECT_EQ(host.events, Events({ "chord 17+91+39" })); // Ctrl+Win+Right
}

TEST_F(EngineTest, FourFingerTapRequestsToggle)
{
    for (uint32_t id = 1; id <= 4; ++id) Down(id, 100 * int(id), 100);
    host.Advance(80);
    for (uint32_t id = 1; id <= 4; ++id) Up(id);
    EXPECT_EQ(host.toggles, 1);
    EXPECT_TRUE(host.events.empty());
}

TEST_F(EngineTest, ObserveOnlyRecognisesToggleButSendsNothing)
{
    engine->SetObserveOnly(true);

    Tap();
    host.Advance(1000);
    Down(1, 100, 100);
    Swipe(1, 100, 100, 400, 100);
    host.Advance(1000);
    Up(1);
    EXPECT_TRUE(host.events.empty());
    EXPECT_EQ(Pt(host.cursor), "500,500");

    for (uint32_t id = 1; id <= 4; ++id) Down(id, 100 * int(id), 100);
    host.Advance(80);
    for (uint32_t id = 1; id <= 4; ++id) Up(id);
    EXPECT_EQ(host.toggles, 1);
}

TEST_F(EngineTest, ResetReleasesHeldButtonsAndKeys)
{
    Tap();
    host.Advance(100);
    Down(1, 100, 100);
    Swipe(1, 100, 100, 200, 100); // dragging: button down
    engine->Reset();
    EXPECT_EQ(host.events.back().rfind("up ", 0), 0u);
}
