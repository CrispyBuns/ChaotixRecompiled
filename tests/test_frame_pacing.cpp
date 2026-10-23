#include "platform/frame_pacing.h"
#include "frontend/fast_forward.h"
#include "test_framework.h"
#include <cmath>

namespace {
constexpr uint64_t frequency = 10000000; // Windows-style 100 ns counter ticks.
constexpr double frame_ticks = double(frequency) * (3420.0 * 262.0) / 53693175.0;

struct Clock {
    uint64_t now = 0, last = 0;
    double accumulator = 0;
    unsigned waits = 0;
    bool early_once = false;
    uint64_t overshoot = 0;
    void pace(bool unthrottled = false) {
        chaotix::pace_frame(accumulator, last, frame_ticks, frequency, unthrottled,
            [&] { return now; }, [&](uint64_t ns) {
                ++waits;
                // Allow a single early wakeup, then advance by whole ticks.
                uint64_t ticks = uint64_t(std::ceil(double(ns) * frequency / 1e9));
                if (early_once) { ticks /= 2; early_once = false; }
                now += ticks + overshoot;
            });
    }
};
}

TEST(frame_pacing, waits_for_a_game_frame) {
    Clock c;
    c.now = 10000; // Frontend work finished long before the next frame.
    c.pace();
    CHECK(c.accumulator >= frame_ticks);
    CHECK(c.accumulator < frame_ticks + 1);
    CHECK_EQ(c.waits, 1u);
    CHECK_EQ(c.last, c.now);
}

TEST(frame_pacing, early_wakeup_still_reaches_deadline) {
    Clock c;
    c.early_once = true;
    c.pace();
    CHECK(c.accumulator >= frame_ticks);
    CHECK_EQ(c.waits, 2u);
}

TEST(frame_pacing, fractional_deadlines_do_not_drift) {
    Clock c;
    for (int i = 0; i < 6000; ++i) {
        c.now += 13000; // 1.3 ms of simulation / render work.
        c.pace();
        c.accumulator -= frame_ticks;
    }
    CHECK(std::abs(double(c.now) - 6000 * frame_ticks) < 1.01);
    CHECK(c.accumulator >= 0 && c.accumulator < 1.01);
}

TEST(frame_pacing, stalls_remain_bounded_including_during_wait) {
    Clock c;
    c.now += frequency * 10;
    c.pace();
    CHECK(c.accumulator == frame_ticks * 8);
    CHECK_EQ(c.waits, 0u);
    c.accumulator = 0;
    c.overshoot = frequency * 10;
    c.pace();
    CHECK(c.accumulator == frame_ticks * 8);
    CHECK_EQ(c.waits, 1u);
}

TEST(frame_pacing, unthrottled_runs_do_not_wait) {
    Clock c;
    c.pace(true); // Fast-forward and automatic tests share this bypass.
    CHECK_EQ(c.waits, 0u);
    CHECK(c.accumulator == 0);
    c.accumulator = -frame_ticks * 64; // Existing automatic-test batching.
    c.pace(true);
    CHECK_EQ(c.waits, 0u);
    c.accumulator = 0; // Fast-forward resets its accumulated time each pass.
    c.now += 13000;
    c.pace();
    CHECK(c.accumulator >= frame_ticks);
    CHECK_EQ(c.waits, 1u);
}

// Issue #45: how many frames a pass runs, with and without fast-forward.

TEST(fast_forward, normal_pacing_runs_one_frame_per_period) {
    using namespace chaotix;
    const PassPlan none = plan_pass(frame_ticks * 0.5, frame_ticks, false, FastForwardSpeed::Max);
    CHECK_EQ(none.frames, 0);
    const PassPlan one = plan_pass(frame_ticks * 1.2, frame_ticks, false, FastForwardSpeed::X4);
    CHECK_EQ(one.frames, 1);
    CHECK_EQ(one.ticks, 1);
    CHECK(!one.unthrottled && !one.reset_accumulator);
    // A pass that fell behind catches up at most three frames.
    const PassPlan behind = plan_pass(frame_ticks * 7.5, frame_ticks, false, FastForwardSpeed::X2);
    CHECK_EQ(behind.frames, 3);
    CHECK_EQ(behind.ticks, 3);
}

TEST(fast_forward, two_and_four_times_keep_the_pacing) {
    using namespace chaotix;
    for (int ticks = 1; ticks <= 3; ++ticks) {
        const double acc = frame_ticks * (ticks + 0.25);
        const PassPlan x2 = plan_pass(acc, frame_ticks, true, FastForwardSpeed::X2);
        CHECK_EQ(x2.frames, 2 * ticks);
        CHECK_EQ(x2.ticks, ticks);
        CHECK(!x2.unthrottled && !x2.reset_accumulator);
        const PassPlan x4 = plan_pass(acc, frame_ticks, true, FastForwardSpeed::X4);
        CHECK_EQ(x4.frames, 4 * ticks);
        CHECK_EQ(x4.ticks, ticks);
        CHECK(!x4.unthrottled);
    }
    // Nothing due yet: nothing run, whatever the speed.
    CHECK_EQ(plan_pass(frame_ticks * 0.9, frame_ticks, true, FastForwardSpeed::X4).frames, 0);
    CHECK_EQ(fast_forward_multiplier(FastForwardSpeed::X2), 2);
    CHECK_EQ(fast_forward_multiplier(FastForwardSpeed::X4), 4);
    CHECK_EQ(fast_forward_multiplier(FastForwardSpeed::Max), 0);
}

TEST(fast_forward, max_is_what_fast_forward_always_did) {
    using namespace chaotix;
    // Eight frames a pass, without waiting, and the time left over dropped.
    for (double acc : {0.0, frame_ticks * 0.5, frame_ticks * 5}) {
        const PassPlan p = plan_pass(acc, frame_ticks, true, FastForwardSpeed::Max);
        CHECK_EQ(p.frames, 8);
        CHECK(p.unthrottled);
        CHECK(p.reset_accumulator);
    }
    // Paced through pace_frame as before: unthrottled skips the wait.
    Clock c;
    c.pace(plan_pass(0, frame_ticks, true, FastForwardSpeed::Max).unthrottled);
    CHECK_EQ(c.waits, 0u);
}

TEST(fast_forward, hold_and_toggle) {
    using namespace chaotix;
    FastForwardState s;
    CHECK(!s.update(false, FastForwardMode::Hold, false));
    CHECK(s.update(true, FastForwardMode::Hold, false));
    CHECK(s.update(true, FastForwardMode::Hold, false));
    CHECK(!s.update(false, FastForwardMode::Hold, false));

    FastForwardState t;
    CHECK(t.update(true, FastForwardMode::Toggle, false));    // pressed: on
    CHECK(t.update(true, FastForwardMode::Toggle, false));    // still held: still on
    CHECK(t.update(false, FastForwardMode::Toggle, false));   // let go: stays on
    CHECK(!t.update(true, FastForwardMode::Toggle, false));   // pressed again: off
    CHECK(!t.update(false, FastForwardMode::Toggle, false));
}

TEST(fast_forward, locked_during_time_attack) {
    using namespace chaotix;
    // Held through a time attack run, it does nothing in either mode.
    for (FastForwardMode mode : {FastForwardMode::Hold, FastForwardMode::Toggle}) {
        FastForwardState s;
        CHECK(!s.update(true, mode, true));
        CHECK(!s.update(false, mode, true));
        CHECK(!s.update(true, mode, true));
    }
    // A toggle left on does not survive the run, nor come back after it.
    FastForwardState t;
    CHECK(t.update(true, FastForwardMode::Toggle, false));
    CHECK(!t.update(false, FastForwardMode::Toggle, true));
    CHECK(!t.update(false, FastForwardMode::Toggle, false));
    // A button still held as the run ends is not a fresh press.
    FastForwardState u;
    CHECK(!u.update(true, FastForwardMode::Toggle, true));
    CHECK(!u.update(true, FastForwardMode::Toggle, false));
    CHECK(u.update(false, FastForwardMode::Toggle, false) == false);
    CHECK(u.update(true, FastForwardMode::Toggle, false));
    // While locked, nothing is run faster than normal pacing.
    const bool active = false;  // what update returns while locked
    CHECK_EQ(plan_pass(frame_ticks * 1.1, frame_ticks, active, FastForwardSpeed::Max).frames, 1);
}
