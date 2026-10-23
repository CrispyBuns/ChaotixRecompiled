// Mega Drive tile replacement (issue #35): replaced tiles change only the
// picture, never what the game can observe.
#include "runtime/patches.h"
#include "runtime/tile_replace.h"
#include "runtime/vdp.h"
#include "test_framework.h"

#include <algorithm>
#include <array>
#include <memory>

using namespace chaotix;

namespace {

constexpr int kLine = 64;
constexpr uint32_t kSat = 0xE000;
constexpr uint32_t kRed = 0xFFFF0000u, kGreen = 0xFF00FF00u;

void word(Vdp& v, uint32_t at, uint16_t value) {
    v.vram[at] = uint8_t(value >> 8);
    v.vram[at + 1] = uint8_t(value);
}

// Tile 1 is solid colour 1 (red); colour 2 is green.
Vdp scene() {
    Vdp v;
    v.reset();
    v.reg[1] = 0x40;
    v.reg[2] = 0x30;  // plane A at C000
    v.reg[4] = 5;     // plane B at A000
    v.reg[5] = 0x70;  // sprites at E000
    v.reg[12] = 1;
    v.reg[16] = patches::kLevelPlaneSize;
    v.cram[1] = 0x000E;
    v.cram[2] = 0x00E0;
    std::fill(v.vram + 32, v.vram + 64, uint8_t(0x11));
    return v;
}

void sprite(Vdp& v, int index, int x, int next) {
    const uint32_t at = kSat + uint32_t(index) * 8;
    word(v, at, kLine + 128);
    word(v, at + 2, uint16_t(next));
    word(v, at + 4, 1);  // tile 1
    word(v, at + 6, uint16_t(x + 128));
}

std::array<uint32_t, Vdp::kMaxWidth> render(Vdp& v, int line = kLine) {
    std::array<uint32_t, Vdp::kMaxWidth> rgb{};
    uint8_t backdrop[Vdp::kMaxWidth]{};
    v.render_line(line, rgb.data(), backdrop, 0);
    return rgb;
}

// A frame starts at line 0, where the replacer looks at VRAM.
void new_frame(Vdp& v) { render(v, 0); }

std::array<uint8_t, 64> filled(uint8_t index) {
    std::array<uint8_t, 64> a;
    a.fill(index);
    return a;
}

} // namespace

TEST(tile_replace, a_replaced_sprite_tile_is_drawn_with_the_new_pixels) {
    auto v = scene();
    sprite(v, 0, 40, 0);
    CHECK_EQ(render(v)[40], kRed);

    v.tiles = std::make_shared<TileReplacer>();
    auto px = filled(2);
    px[kLine % 8 * 8 + 3] = 0;  // one transparent pixel on this row
    v.tiles->add(TileReplacer::hash(v.vram + 32), px);
    new_frame(v);
    const auto rgb = render(v);
    CHECK_EQ(rgb[40], kGreen);
    CHECK_EQ(rgb[43], 0xFF000000u);  // the backdrop shows through
    CHECK_EQ(rgb[47], kGreen);
}

TEST(tile_replace, plane_tiles_are_replaced_too) {
    auto v = scene();
    // Plane A's first entry at C000 + (line / 8) * 64 cells * 2 bytes: tile 1.
    const int width_cells = 64;
    word(v, 0xC000 + uint32_t((kLine / 8) * width_cells * 2), 1);
    CHECK_EQ(render(v)[0], kRed);
    v.tiles = std::make_shared<TileReplacer>();
    v.tiles->add(TileReplacer::hash(v.vram + 32), filled(2));
    new_frame(v);
    CHECK_EQ(render(v)[0], kGreen);
}

TEST(tile_replace, collisions_follow_the_original_tiles) {
    // Two sprites overlapping: the game sees a collision, even when the
    // replacement makes them transparent where they meet...
    auto v = scene();
    sprite(v, 0, 40, 1);
    sprite(v, 1, 44, 0);
    v.tiles = std::make_shared<TileReplacer>();
    v.tiles->add(TileReplacer::hash(v.vram + 32), filled(0));
    new_frame(v);
    v.sprite_collision = false;
    render(v);
    CHECK(v.sprite_collision);

    // ...and none when the originals do not meet, however the replacement looks.
    auto w = scene();
    std::fill(w.vram + 32, w.vram + 64, uint8_t(0));
    for (int r = 0; r < 8; ++r) w.vram[32 + r * 4] = 0x10;  // one pixel at the left of each row
    sprite(w, 0, 40, 1);
    sprite(w, 1, 44, 0);
    w.tiles = std::make_shared<TileReplacer>();
    w.tiles->add(TileReplacer::hash(w.vram + 32), filled(2));
    new_frame(w);
    w.sprite_collision = false;
    const auto rgb = render(w);
    CHECK_EQ(rgb[45], kGreen);   // drawn solid...
    CHECK(!w.sprite_collision);  // ...but the game saw no overlap
}

TEST(tile_replace, drawn_tiles_are_dumped_once_with_their_palette) {
    auto v = scene();
    sprite(v, 0, 40, 0);
    v.tiles = std::make_shared<TileReplacer>();
    v.tiles->dumping = true;
    new_frame(v);
    render(v);
    new_frame(v);  // the next frame collects what the last one drew
    CHECK_EQ(v.tiles->pending.size(), size_t(1));
    if (v.tiles->pending.empty()) return;
    const auto& d = v.tiles->pending[0];
    CHECK_EQ(d.hash, TileReplacer::hash(v.vram + 32));
    CHECK_EQ(int(d.indices[0]), 1);
    CHECK_EQ(d.palette[1], kRed);
    CHECK_EQ(d.palette[0], 0u);  // index 0 is transparent
    v.tiles->pending.clear();
    render(v);
    new_frame(v);
    CHECK(v.tiles->pending.empty());  // not again
}
