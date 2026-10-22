// Menu backdrops (issue #8): pictures of the game's zones, at their times of
// day, for the menu to show behind itself instead of a plain colour.
//
// Nothing of the game is shipped for this. A backdrop is rendered by the game
// itself, on a second Machine that is never the one being played: it boots,
// takes the stage the way TIME ATTACK starts one, runs until the level is on
// screen and keeps one frame. The characters and the HUD are left out of the
// picture by giving that machine's sprite recorder empty replacements for
// their graphics (runtime/sprite_replace.h), which changes only what the
// machine shows. The frontend caches the result as a PNG in the user's data
// folder, so this happens once per ROM (docs/MENU_ART.md).
#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace chaotix {

struct Rom;

struct BackdropSpec {
    uint16_t place = 0, level = 1, attime = 0;  // as stage_select::Request has them
    const char* key = "";    // file name stem in the cache
    // Rendered wide (kBackdropWideExtra). Off where the start of the level
    // shows what lies beyond its edge in the margins, and in the lobby, which
    // the game keeps 4:3.
    bool wide = true;
};

// The zones the menu cycles through, each at a time of day that suits it.
const std::vector<BackdropSpec>& backdrop_specs();

struct BackdropImage {
    int width = 0, height = 0;
    std::vector<uint32_t> pixels;  // XRGB8888, row-major
};

// Width added on each side of the 320-pixel picture, so a backdrop is about
// 16:9 like most windows (the level is rendered wide, as WIDESCREEN does).
constexpr int kBackdropWideExtra = 40;
// Frames run after the stage is taken before the picture is kept: the title
// card has gone and the level has settled.
constexpr int kBackdropSettleFrames = 480;

// Renders the backdrop on a machine of its own (interpreted, so it may run on
// any thread while the recompiled machine plays). False (and `why`) if the
// stage never started. `cancel` is polled once a frame.
bool render_backdrop(const Rom& rom, const BackdropSpec& spec, BackdropImage& out, std::string* why,
                     const std::atomic<bool>* cancel = nullptr);

// A dark, saturated colour from the picture, for the translucent overlay that
// keeps the menu readable over it: the average colour, darkened (ARGB).
uint32_t backdrop_tint(const BackdropImage& image);
// The zone's most common vivid colour, brightened, for the menu's frames and
// markers (ARGB). The theme's blue when the picture has nothing vivid.
uint32_t backdrop_accent(const BackdropImage& image);

} // namespace chaotix
