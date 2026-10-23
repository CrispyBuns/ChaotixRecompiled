// Mega Drive text replaced by tile sequence: a pattern of tiles found in a
// plane is drawn from the modder's image, only there, scrolling with the
// plane; the longest pattern wins; wider images cover their neighbours.
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
constexpr int kCells = 64;  // patches::kLevelPlaneSize: 64x32 cells

void word(Vdp& v, uint32_t at, uint16_t value) {
    v.vram[at] = uint8_t(value >> 8);
    v.vram[at + 1] = uint8_t(value);
}

uint32_t rgb(uint16_t c) { return Vdp::cram_to_rgb(c) | 0xFF000000u; }
const uint16_t kRedC = 0x000E, kGreenC = 0x00E0, kBlueC = 0x0E00, kWhiteC = 0x0EEE;

// Tile 1 is solid colour 1 (red), tile 2 solid colour 2 (green); colour 3 is
// blue. Plane A's row at kLine shows tiles 1 2 1 from cell 0, and 1 2 1 again
// from cell 5 with the 2 flipped horizontally.
Vdp scene() {
    Vdp v;
    v.reset();
    v.reg[1] = 0x40;
    v.reg[2] = 0x30;   // plane A at C000
    v.reg[4] = 5;      // plane B at A000
    v.reg[5] = 0x70;   // sprites at E000
    v.reg[12] = 1;
    v.reg[13] = 0x3C;  // H-scroll table at F000
    v.reg[16] = patches::kLevelPlaneSize;
    v.cram[1] = kRedC;
    v.cram[2] = kGreenC;
    v.cram[3] = kBlueC;
    v.cram[16 + 3] = kWhiteC;
    std::fill(v.vram + 32, v.vram + 64, uint8_t(0x11));
    std::fill(v.vram + 64, v.vram + 96, uint8_t(0x22));
    const uint32_t row = 0xC000 + uint32_t((kLine / 8) * kCells * 2);
    word(v, row + 0, 1);
    word(v, row + 2, 2);
    word(v, row + 4, 1);
    word(v, row + 10, 1);
    word(v, row + 12, 2 | 0x0800);
    word(v, row + 14, 1);
    return v;
}

std::array<uint32_t, Vdp::kMaxWidth> render(Vdp& v, int line = kLine) {
    std::array<uint32_t, Vdp::kMaxWidth> out{};
    uint8_t backdrop[Vdp::kMaxWidth]{};
    v.render_line(line, out.data(), backdrop, 0);
    return out;
}

void new_frame(Vdp& v) { render(v, 0); }

uint64_t tile_hash(const Vdp& v, int tile) { return TileReplacer::hash(v.vram + tile * 32); }

TextReplacer::Pattern pattern(std::vector<uint64_t> hashes, int width, uint8_t index) {
    TextReplacer::Pattern p;
    p.rows = 1;
    p.cols = int(hashes.size());
    p.hashes = std::move(hashes);
    p.flips.assign(p.hashes.size(), 0);
    p.width = width;
    p.height = 8;
    p.indices.assign(size_t(width * 8), index);
    return p;
}

void add(Vdp& v, TextReplacer::Pattern p) {
    if (!v.tiles) v.tiles = std::make_shared<TileReplacer>();
    std::string error;
    CHECK(v.tiles->text.add(std::move(p), &error));
}

} // namespace

TEST(text_replace, a_run_of_tiles_is_drawn_from_the_image_and_only_there) {
    auto v = scene();
    const auto before = render(v);
    add(v, pattern({tile_hash(v, 1), tile_hash(v, 2)}, 16, 3));
    new_frame(v);
    const auto after = render(v);
    CHECK_EQ(after[0], rgb(kBlueC));
    CHECK_EQ(after[15], rgb(kBlueC));
    CHECK_EQ(after[16], rgb(kRedC));   // the third tile is not in the pattern
    // The run from cell 5 has its 2 flipped: another sequence.
    for (int x = 16; x < 320; ++x) CHECK_EQ(after[size_t(x)], before[size_t(x)]);
}

TEST(text_replace, flipped_tiles_are_matched_by_their_flip) {
    auto v = scene();
    auto p = pattern({tile_hash(v, 1), tile_hash(v, 2)}, 16, 3);
    p.flips[1] = 1;
    add(v, p);
    new_frame(v);
    const auto after = render(v);
    CHECK_EQ(after[0], rgb(kRedC));
    CHECK_EQ(after[40], rgb(kBlueC));
    CHECK_EQ(after[55], rgb(kBlueC));
    CHECK_EQ(after[56], rgb(kRedC));
}

TEST(text_replace, the_image_scrolls_with_the_plane) {
    auto v = scene();
    word(v, 0xF000, 4);  // plane A 4 pixels to the right
    add(v, pattern({tile_hash(v, 1), tile_hash(v, 2)}, 16, 3));
    new_frame(v);
    const auto after = render(v);
    CHECK_EQ(after[3], rgb(0));  // the plane's last cell, blank: the backdrop
    CHECK_EQ(after[4], rgb(kBlueC));
    CHECK_EQ(after[19], rgb(kBlueC));
    CHECK_EQ(after[20], rgb(kRedC));
}

TEST(text_replace, the_longest_pattern_wins) {
    auto v = scene();
    add(v, pattern({tile_hash(v, 1), tile_hash(v, 2)}, 16, 3));
    add(v, pattern({tile_hash(v, 1), tile_hash(v, 2), tile_hash(v, 1)}, 24, 2));
    new_frame(v);
    const auto after = render(v);
    for (int x = 0; x < 24; ++x) CHECK_EQ(after[size_t(x)], rgb(kGreenC));
}

TEST(text_replace, a_wider_image_covers_its_neighbours_where_it_is_not_0) {
    auto v = scene();
    // Cells 1-2 (tiles 2 1), centred: the image reaches 8 pixels each side.
    auto p = pattern({tile_hash(v, 2), tile_hash(v, 1)}, 32, 3);
    p.align = TextReplacer::Align::Center;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 4; ++x) p.indices[size_t(y * 32 + x)] = 0;  // the left 4 columns clear
    add(v, p);
    new_frame(v);
    const auto after = render(v);
    CHECK_EQ(after[3], rgb(kRedC));   // under a clear pixel: the neighbour's tile
    CHECK_EQ(after[4], rgb(kBlueC));  // the neighbour, covered
    CHECK_EQ(after[8], rgb(kBlueC));  // the run
    CHECK_EQ(after[23], rgb(kBlueC));
    CHECK_EQ(after[31], rgb(kBlueC));  // cell 3, blank, covered
    CHECK_EQ(after[32], rgb(0));       // past the image: cell 4, blank
    CHECK_EQ(after[40], rgb(kRedC));
}

TEST(text_replace, colours_come_from_the_first_tiles_palette_line_and_can_be_required) {
    auto v = scene();
    auto p = pattern({tile_hash(v, 1), tile_hash(v, 2)}, 16, 3);
    p.palette = 1;
    add(v, p);
    new_frame(v);
    CHECK_EQ(render(v)[0], rgb(kRedC));  // drawn with line 0: no match
    const uint32_t row = 0xC000 + uint32_t((kLine / 8) * kCells * 2);
    word(v, row + 0, 1 | 0x2000);
    word(v, row + 2, 2 | 0x2000);
    new_frame(v);
    CHECK_EQ(render(v)[0], rgb(kWhiteC));  // index 3 of line 1
}

TEST(text_replace, a_cell_changed_since_the_match_shows_its_tile) {
    auto v = scene();
    add(v, pattern({tile_hash(v, 1), tile_hash(v, 2)}, 16, 3));
    new_frame(v);
    word(v, 0xC000 + uint32_t((kLine / 8) * kCells * 2) + 2, 1);
    const auto after = render(v);
    CHECK_EQ(after[8], rgb(kRedC));
}

TEST(text_replace, a_palette_or_priority_change_since_the_match_keeps_the_image) {
    // DATA LOAD dims a slot by changing its entries' palette line during a
    // picture: the run stays replaced, in the first cell's new colours.
    auto v = scene();
    add(v, pattern({tile_hash(v, 1), tile_hash(v, 2)}, 16, 3));
    new_frame(v);
    const uint32_t row = 0xC000 + uint32_t((kLine / 8) * kCells * 2);
    word(v, row + 0, 1 | 0x2000 | 0x8000);
    word(v, row + 2, 2 | 0x2000);
    const auto after = render(v);
    CHECK_EQ(after[0], rgb(kWhiteC));
    CHECK_EQ(after[15], rgb(kWhiteC));
    CHECK_EQ(after[16], rgb(kRedC));
    // A flip is a change of tile: that cell shows its tile.
    word(v, row + 2, 2 | 0x0800);
    CHECK_EQ(render(v)[8], rgb(kGreenC));
}

TEST(text_replace, text_written_during_the_picture_is_replaced_from_the_line_it_shows_on) {
    auto v = scene();
    const uint32_t row = 0xC000 + uint32_t((kLine / 8) * kCells * 2);
    word(v, row + 0, 0);
    word(v, row + 2, 0);
    add(v, pattern({tile_hash(v, 1), tile_hash(v, 2)}, 16, 3));
    new_frame(v);  // the run is not there when the picture starts
    CHECK_EQ(render(v, kLine - 8)[0], rgb(0));
    word(v, row + 0, 1);
    word(v, row + 2, 2);
    const auto after = render(v);
    CHECK_EQ(after[0], rgb(kBlueC));
    CHECK_EQ(after[15], rgb(kBlueC));
    CHECK_EQ(after[16], rgb(kRedC));
}

TEST(text_replace, a_run_cleared_and_rewritten_before_its_line_stays_replaced) {
    // DATA LOAD's slot panel: its rows are cleared and written again while
    // the picture is drawn, before the beam reaches them.
    auto v = scene();
    add(v, pattern({tile_hash(v, 1), tile_hash(v, 2)}, 16, 3));
    new_frame(v);
    const uint32_t row = 0xC000 + uint32_t((kLine / 8) * kCells * 2);
    for (int c = 0; c < kCells; ++c) word(v, row + uint32_t(c) * 2, 0);
    CHECK_EQ(render(v, kLine - 8)[0], rgb(0));  // a line above it
    word(v, row + 0, 1);
    word(v, row + 2, 2);
    word(v, row + 4, 1);
    const auto after = render(v);
    CHECK_EQ(after[0], rgb(kBlueC));
    CHECK_EQ(after[15], rgb(kBlueC));
    CHECK_EQ(after[16], rgb(kRedC));
    // Cleared while its row is drawn: the line shows what is there.
    for (int c = 0; c < kCells; ++c) word(v, row + uint32_t(c) * 2, 0);
    CHECK_EQ(render(v, kLine + 1)[0], rgb(0));
    word(v, row + 0, 1);
    word(v, row + 2, 2);
    CHECK_EQ(render(v, kLine + 2)[0], rgb(kBlueC));
}

TEST(text_replace, a_taller_run_follows_its_rows_below_the_line) {
    // Tiles 1 2 over 2 1: a change in the second row brings the tiles of
    // the first back on the lines drawn after it.
    auto v = scene();
    const uint32_t row = 0xC000 + uint32_t((kLine / 8) * kCells * 2);
    word(v, row + kCells * 2 + 0, 2);
    word(v, row + kCells * 2 + 2, 1);
    TextReplacer::Pattern p = pattern({tile_hash(v, 1), tile_hash(v, 2), tile_hash(v, 2), tile_hash(v, 1)}, 16, 3);
    p.rows = 2;
    p.cols = 2;
    p.height = 16;
    p.indices.assign(16 * 16, 3);
    add(v, p);
    new_frame(v);
    CHECK_EQ(render(v)[0], rgb(kBlueC));
    CHECK_EQ(render(v, kLine + 8)[0], rgb(kBlueC));
    word(v, row + kCells * 2 + 2, 2);
    CHECK_EQ(render(v, kLine + 1)[0], rgb(kRedC));
    CHECK_EQ(render(v, kLine + 9)[0], rgb(kGreenC));
}

TEST(text_replace, the_window_plane_is_matched_too) {
    auto v = scene();
    v.reg[3] = 0x2C;   // window at B000
    v.reg[18] = 0x80;  // the window from line 0 down: every line
    const uint32_t row = 0xB000 + uint32_t((kLine / 8) * kCells * 2);
    word(v, row + 2, 2);
    word(v, row + 4, 2);
    auto p = pattern({tile_hash(v, 2), tile_hash(v, 2)}, 16, 3);
    p.planes = TextReplacer::kPlaneW;
    add(v, p);
    new_frame(v);
    const auto after = render(v);
    CHECK_EQ(after[7], rgb(0));  // plane A is hidden by the window: the backdrop
    CHECK_EQ(after[8], rgb(kBlueC));
    CHECK_EQ(after[23], rgb(kBlueC));
}

TEST(text_replace, malformed_patterns_are_refused) {
    TextReplacer t;
    std::string error;
    auto p = pattern({1, 2}, 16, 3);
    p.height = 16;
    p.indices.assign(16 * 16, 1);
    CHECK(!t.add(p, &error));
    p = pattern({1, 2}, 16, 3);
    p.flips.pop_back();
    CHECK(!t.add(p, &error));
    CHECK(t.add(pattern({1, 2}, 16, 3), &error));
    CHECK_EQ(t.size(), size_t(1));
}

TEST(text_replace, tokens_name_the_hash_and_the_flip) {
    CHECK_STR(TextReplacer::token(0x0123456789abcdefull, 0), "0123456789abcdef");
    CHECK_STR(TextReplacer::token(0x0123456789abcdefull, 3), "0123456789abcdef:hv");
    uint64_t h = 0;
    uint8_t f = 9;
    CHECK(TextReplacer::parse_token("0123456789ABCDEF:v", h, f));
    CHECK_EQ(h, 0x0123456789abcdefull);
    CHECK_EQ(int(f), 2);
    CHECK(!TextReplacer::parse_token("0123456789abcde", h, f));
    CHECK(!TextReplacer::parse_token("0123456789abcdef:x", h, f));
}

TEST(text_replace, the_dump_lists_the_runs_on_screen) {
    auto v = scene();
    v.tiles = std::make_shared<TileReplacer>();
    v.tiles->dumping = true;
    auto& pending = v.tiles->text.pending;
    // Listed once the runs have stayed in place for a while.
    for (int i = 1; i < TextReplacer::kDumpSteady; ++i) new_frame(v);
    CHECK(pending.empty());
    new_frame(v);
    CHECK_EQ(pending.size(), size_t(1));
    if (pending.empty()) return;
    const std::string one = TextReplacer::token(tile_hash(v, 1), 0), two = TextReplacer::token(tile_hash(v, 2), 0);
    const std::string flipped = TextReplacer::token(tile_hash(v, 2), 1);
    // Cells 0-2 and 5-7 are one run: two blank cells between them would
    // split it, one would not -- here there are two.
    CHECK(pending[0].text.find("plane A\nrow " + one + " " + two + " " + one + "\n") != std::string::npos);
    CHECK(pending[0].text.find("plane A\nrow " + one + " " + flipped + " " + one + "\n") != std::string::npos);
    CHECK_EQ(pending[0].boxes.size(), size_t(2));
    if (pending[0].boxes.size() == 2) {
        CHECK_EQ(pending[0].boxes[0].x, 0);
        CHECK_EQ(pending[0].boxes[0].y, kLine);
        CHECK_EQ(pending[0].boxes[1].x, 40);
        CHECK_EQ(pending[0].boxes[1].w, 24);
    }
    // The same screen again is not listed again.
    pending.clear();
    for (int i = 0; i < 40; ++i) new_frame(v);
    CHECK(pending.empty());
    // Nor is a run that keeps moving (a scrolling background), until it stops.
    const uint32_t row = 0xC000 + uint32_t((kLine / 8) * kCells * 2);
    word(v, row + 40, 2);
    for (int i = 0; i < 60; ++i) {
        word(v, 0xF000, uint16_t(i * 8));
        new_frame(v);
    }
    CHECK(pending.empty());
    for (int i = 0; i < TextReplacer::kDumpSteady; ++i) new_frame(v);
    CHECK_EQ(pending.size(), size_t(1));
}
