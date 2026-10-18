// Virtual on-screen controls for touch devices (Android / iOS, or desktop
// touch screens). Pure layout + hit-testing; the platform layer draws them.
#pragma once
#include <cstdint>
#include <vector>

namespace chaotix {

struct TouchButton {
    uint16_t mask;       // PAD_* bits produced while held
    float cx, cy, r;     // centre and radius in output pixels
    const char* label;
    bool is_dpad;        // the directions: a d-pad, or the joystick's base
};

class TouchControls {
public:
    // Lays out controls for an output of w x h pixels. With stick set the
    // directions are a joystick rather than a d-pad (issue #30).
    void layout(int w, int h, bool stick = false);
    // Buttons pressed by a touch at (x, y) in output pixels.
    uint16_t hit(float x, float y) const;
    // A finger put down (down) or moved, by its id; returns what it presses.
    // A finger that lands on the joystick holds it until it is lifted, so it
    // keeps steering when it slides off the base, as a real stick would.
    uint16_t finger(uint64_t id, float x, float y, bool down);
    void lift(uint64_t id);
    const std::vector<TouchButton>& buttons() const { return buttons_; }
    bool stick() const { return stick_; }
    // The knob's offset from the base's centre, at most the base's radius.
    float knob_dx() const { return knob_dx_; }
    float knob_dy() const { return knob_dy_; }
    bool stick_held() const { return stick_held_; }

private:
    uint16_t stick_directions(float dx, float dy) const;

    std::vector<TouchButton> buttons_;
    bool stick_ = false;
    bool stick_held_ = false;   // a finger has the stick
    uint64_t stick_finger_ = 0;
    float knob_dx_ = 0, knob_dy_ = 0;
};

} // namespace chaotix
