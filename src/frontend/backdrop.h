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
    // The zone, as MENU BACKGROUND names it: its `[Video] MenuBackground`
    // value and the menu's label (issue #37).
    const char* zone = "";
    const char* name = "";
};

// The zones the menu cycles through, each at a time of day that suits it, in
// the order they are made and shown.
const std::vector<BackdropSpec>& backdrop_specs();
// Indices into backdrop_specs() in the order MENU BACKGROUND offers them: the
// game's own, the lobby and the island first, then the five attractions.
const std::vector<int>& backdrop_menu_order();
// The index of the spec whose `zone` is this (case ignored), or -1.
int backdrop_find_zone(const std::string& zone);

struct BackdropImage {
    int width = 0, height = 0;
    std::vector<uint32_t> pixels;  // XRGB8888, row-major
    // 32X sprite draws of the kept frame left out of it (the characters, the
    // HUD, the tether), and all of its draws: for tests and logs.
    int hidden_draws = 0, sprite_draws = 0;
};

// Width added on each side of the 320-pixel picture, so a backdrop is about
// 16:9 like most windows (the level is rendered wide, as WIDESCREEN does).
constexpr int kBackdropWideExtra = 40;
// Frames run after the stage is taken before the picture is kept: the title
// card has gone and the level has settled.
constexpr int kBackdropSettleFrames = 480;

// How a caller steers a backdrop being rendered on another thread. Both flags
// are read before every emulated frame of the backdrop machine.
struct BackdropControl {
    std::atomic<bool> cancel{false};  // give up (the program is closing)
    // Hold still between frames: the game is being played and must not share
    // the CPU with this (slow machines have no core to spare, issue #31).
    std::atomic<bool> pause{false};
    std::atomic<uint64_t> frames{0};  // emulated so far, for tests and logs
};

// Renders the backdrop on a machine of its own (interpreted, so it may run on
// any thread while the recompiled machine plays). False (and `why`) if the
// stage never started or it was cancelled.
bool render_backdrop(const Rom& rom, const BackdropSpec& spec, BackdropImage& out, std::string* why,
                     BackdropControl* control = nullptr);

// A dark, saturated colour from the picture, for the translucent overlay that
// keeps the menu readable over it: the average colour, darkened (ARGB).
uint32_t backdrop_tint(const BackdropImage& image);
// The zone's most common vivid colour, brightened, for the menu's frames and
// markers (ARGB). The theme's blue when the picture has nothing vivid.
uint32_t backdrop_accent(const BackdropImage& image);

} // namespace chaotix
