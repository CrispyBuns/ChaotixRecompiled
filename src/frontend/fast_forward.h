// Fast-forward (issue #45): whether it is on, and how many emulated frames a
// pass round the main loop runs. Pure, so the policy is testable without SDL.
//
// Fast-forward only ever runs more frames of the same game: the emulated
// input of each frame is what it would have been anyway, so the game stays
// deterministic and a recording made while fast-forwarding replays exactly.
#pragma once
#include <algorithm>
#include <cstdint>

namespace chaotix {

// HOLD: on while the button is held. TOGGLE: each press turns it on or off.
enum class FastForwardMode { Hold, Toggle };
// 2X / 4X: that many emulated frames for every frame of normal pacing, so the
// game runs at exactly twice or four times its speed. MAX: as fast as the
// machine allows, up to 8 frames per presented picture, unthrottled.
enum class FastForwardSpeed { X2, X4, Max };

inline const char* fast_forward_mode_name(FastForwardMode m) {
    return m == FastForwardMode::Toggle ? "Toggle" : "Hold";
}
inline const char* fast_forward_speed_name(FastForwardSpeed s) {
    return s == FastForwardSpeed::X2 ? "2x" : s == FastForwardSpeed::X4 ? "4x" : "Max";
}
// Emulated frames per normally paced frame; 0 for MAX, which has no fixed rate.
inline int fast_forward_multiplier(FastForwardSpeed s) {
    return s == FastForwardSpeed::X2 ? 2 : s == FastForwardSpeed::X4 ? 4 : 0;
}

// The on/off state. `held` is whether any fast-forward input (key, gamepad
// button, touch button) is down this pass; `locked` is set while fast-forward
// is not allowed at all (a TIME ATTACK run, the menu open), which also
// forgets a toggle so it does not come back on by itself afterwards.
struct FastForwardState {
    bool toggled = false;
    bool was_held = false;

    bool update(bool held, FastForwardMode mode, bool locked) {
        const bool pressed = held && !was_held;
        was_held = held;
        if (locked) {
            toggled = false;
            return false;
        }
        if (mode == FastForwardMode::Hold) {
            toggled = false;
            return held;
        }
        if (pressed) toggled = !toggled;
        return toggled;
    }
};

// What one pass round the main loop runs.
struct PassPlan {
    int frames = 0;            // emulated frames to run
    int ticks = 0;             // frame periods of the accumulator they use up
    bool reset_accumulator = false;  // MAX: whatever time is left is dropped
    bool unthrottled = false;  // MAX: do not wait for the next frame period
};

// Without fast-forward, one frame per elapsed frame period, at most
// `max_catch_up` of them (a pass that fell behind catches up a little rather
// than all at once). 2X / 4X run that many frames per period, with the same
// pacing and catch-up. MAX runs 8 frames and does not wait.
inline PassPlan plan_pass(double accumulator, double frame_ticks, bool fast_forward,
                          FastForwardSpeed speed, int max_catch_up = 3) {
    PassPlan p;
    if (fast_forward && speed == FastForwardSpeed::Max) {
        p.frames = 8;
        p.reset_accumulator = true;
        p.unthrottled = true;
        return p;
    }
    int ticks = 0;
    if (frame_ticks > 0)
        ticks = int(std::min<double>(max_catch_up, std::max(0.0, accumulator / frame_ticks)));
    p.ticks = ticks;
    p.frames = ticks * (fast_forward ? fast_forward_multiplier(speed) : 1);
    return p;
}

} // namespace chaotix
