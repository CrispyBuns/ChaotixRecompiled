// Mega Drive text replaced by sprite: a sprite, or a group of sprites at
// fixed distances, found in the sprite table is drawn from the modder's
// image instead, only there; flips must match; the group with the most tiles
// wins; the image keeps its first member's place among the sprites; the
// status flags still follow the original sprites.
#include "runtime/sprite_text.h"
#include "runtime/tile_replace.h"
#include "runtime/vdp.h"
#include "test_framework.h"

#include <algorithm>
#include <array>
#include <memory>

using namespace chaotix;

namespace {

constexpr uint32_t kSat = 0xE000;
const uint16_t kRedC = 0x000E, kGreenC = 0x00E0, kBlueC = 0x0E00, kWhiteC = 0x0EEE;

uint32_t rgb(uint16_t c) { return Vdp::cram_to_rgb(c) | 0xFF000000u; }

void word(Vdp& v, uint32_t at, uint16_t value) {
    v.vram[at & 0xFFFF] = uint8_t(value >> 8);
    v.vram[(at + 1) & 0xFFFF] = uint8_t(value);
}

// Sprite table entry `i`: w x h tiles at screen (x, y), pattern word `pat`,
// linked to `link`.
void sprite(Vdp& v, int i, int x, int y, int w, int h, uint16_t pat, int link) {
    const uint32_t e = kSat + uint32_t(i) * 8;
    word(v, e, uint16_t(y + 128));
    word(v, e + 2, uint16_t(((w - 1) << 10) | ((h - 1) << 8) | link));
    word(v, e + 4, pat);
    word(v, e + 6, uint16_t(x + 128));
}

// Tile 1: solid colour 1 (red); tile 2: solid colour 2 (green); tile 3:
// left half colour 1, right half 0. Colour 3 is blue (white on line 1). No
// sprites yet: entry 0 is a 1x1 sprite far off screen ending the list.
Vdp scene() {
    Vdp v;
    v.reset();
    v.reg[1] = 0x40;
    v.reg[2] = 0x30;
    v.reg[4] = 5;
    v.reg[5] = 0x70;  // sprites at E000
    v.reg[12] = 1;
    v.reg[13] = 0x3C;
    v.cram[1] = kRedC;
    v.cram[2] = kGreenC;
    v.cram[3] = kBlueC;
    v.cram[16 + 1] = kGreenC;
    v.cram[16 + 3] = kWhiteC;
    std::fill(v.vram + 32, v.vram + 64, uint8_t(0x11));
    std::fill(v.vram + 64, v.vram + 96, uint8_t(0x22));
    for (int r = 0; r < 8; ++r) {
        v.vram[96 + r * 4] = 0x11;
        v.vram[96 + r * 4 + 1] = 0x11;
    }
    sprite(v, 0, 400, 300, 1, 1, 0, 0);
    return v;
}

std::array<uint32_t, Vdp::kMaxWidth> render(Vdp& v, int line) {
    std::array<uint32_t, Vdp::kMaxWidth> out{};
    uint8_t backdrop[Vdp::kMaxWidth]{};
    v.render_line(line, out.data(), backdrop, 0);
    return out;
}

void new_frame(Vdp& v) { render(v, 0); }

uint64_t tile_hash(const Vdp& v, int tile) { return TileReplacer::hash(v.vram + tile * 32); }

SpriteTextReplacer::Member member(int dx, int dy, int w, int h, std::vector<uint64_t> hashes, uint8_t flips = 0) {
    SpriteTextReplacer::Member m;
    m.dx = dx;
    m.dy = dy;
    m.w = w;
    m.h = h;
    m.flips = flips;
    m.hashes = std::move(hashes);
    return m;
}

SpriteTextReplacer::Pattern pattern(std::vector<SpriteTextReplacer::Member> members, int width, int height,
                                    uint8_t index) {
    SpriteTextReplacer::Pattern p;
    p.members = std::move(members);
    p.width = width;
    p.height = height;
    p.indices.assign(size_t(width * height), index);
    return p;
}

void add(Vdp& v, SpriteTextReplacer::Pattern p) {
    if (!v.tiles) v.tiles = std::make_shared<TileReplacer>();
    std::string error;
    CHECK(v.tiles->sprite_text.add(std::move(p), &error));
}

} // namespace

TEST(sprite_text, a_sprite_is_drawn_from_the_image_and_only_there) {
    auto v = scene();
    // Entry 0 -> 1 -> 2: a 2x1 sprite of tiles 1 2 at x 16, and the same
    // tiles at x 100 drawn as a 1x2 sprite (another size: no match).
    sprite(v, 0, 16, 40, 2, 1, 1, 1);
    sprite(v, 1, 100, 40, 1, 2, 1, 0);
    const auto before = render(v, 40);
    CHECK_EQ(before[16], rgb(kRedC));
    CHECK_EQ(before[24], rgb(kGreenC));
    add(v, pattern({member(0, 0, 2, 1, {tile_hash(v, 1), tile_hash(v, 2)})}, 16, 8, 3));
    new_frame(v);
    const auto after = render(v, 40);
    CHECK_EQ(after[15], rgb(0));
    CHECK_EQ(after[16], rgb(kBlueC));
    CHECK_EQ(after[31], rgb(kBlueC));
    CHECK_EQ(after[32], rgb(0));
    CHECK_EQ(after[100], rgb(kRedC));  // the 1x2 sprite
    CHECK_EQ(render(v, 39)[16], rgb(0));
    CHECK_EQ(render(v, 48)[16], rgb(0));
}

TEST(sprite_text, the_image_keeps_its_size_place_and_alignment) {
    auto v = scene();
    sprite(v, 0, 16, 40, 2, 1, 1, 0);
    // 8x16, centred on the 16-pixel sprite, moved 1 pixel down.
    auto p = pattern({member(0, 0, 2, 1, {tile_hash(v, 1), tile_hash(v, 2)})}, 8, 16, 3);
    p.align = SpriteTextReplacer::Align::Center;
    p.offset_y = 1;
    for (int x = 0; x < 8; ++x) p.indices[size_t(x)] = 0;  // its top row clear
    add(v, p);
    new_frame(v);
    CHECK_EQ(render(v, 40)[20], rgb(0));    // the sprite's first line: nothing
    CHECK_EQ(render(v, 41)[20], rgb(0));    // the image's clear top row
    const auto l = render(v, 42);
    CHECK_EQ(l[19], rgb(0));
    CHECK_EQ(l[20], rgb(kBlueC));
    CHECK_EQ(l[27], rgb(kBlueC));
    CHECK_EQ(l[28], rgb(0));
    CHECK_EQ(render(v, 56)[20], rgb(kBlueC));  // below the sprite: the image goes on
    CHECK_EQ(render(v, 57)[20], rgb(0));
}

TEST(sprite_text, flipped_sprites_are_matched_by_their_flip) {
    auto v = scene();
    sprite(v, 0, 16, 40, 2, 1, 1 | 0x0800, 1);  // flipped: shows 2 1
    sprite(v, 1, 64, 40, 2, 1, 1, 0);
    auto p = pattern({member(0, 0, 2, 1, {tile_hash(v, 1), tile_hash(v, 2)}, 1)}, 16, 8, 3);
    add(v, p);
    new_frame(v);
    const auto after = render(v, 40);
    CHECK_EQ(after[16], rgb(kBlueC));  // drawn as it is, not flipped
    CHECK_EQ(after[64], rgb(kRedC));
    CHECK_EQ(after[72], rgb(kGreenC));
    CHECK(SpriteTextReplacer::parse_size_token("2x1:h", p.members[0].w, p.members[0].h, p.members[0].flips));
    CHECK_EQ(int(p.members[0].flips), 1);
    CHECK_STR(SpriteTextReplacer::size_token(4, 3, 3), "4x3:hv");
}

TEST(sprite_text, a_group_matches_only_at_its_distances) {
    auto v = scene();
    // "1 2" at 16,40 with "3" 24 pixels right; "1 2" at 16,80 with "3" 25
    // pixels right.
    sprite(v, 0, 16, 40, 2, 1, 1, 1);
    sprite(v, 1, 40, 40, 1, 1, 3, 2);
    sprite(v, 2, 16, 80, 2, 1, 1, 3);
    sprite(v, 3, 41, 80, 1, 1, 3, 0);
    add(v, pattern({member(0, 0, 2, 1, {tile_hash(v, 1), tile_hash(v, 2)}), member(24, 0, 1, 1, {tile_hash(v, 3)})},
                   32, 8, 3));
    new_frame(v);
    const auto a = render(v, 40);
    for (int x = 16; x < 48; ++x) CHECK_EQ(a[size_t(x)], rgb(kBlueC));
    const auto b = render(v, 80);
    CHECK_EQ(b[16], rgb(kRedC));
    CHECK_EQ(b[41], rgb(kRedC));
    CHECK_EQ(b[45], rgb(0));
}

TEST(sprite_text, member_offsets_are_relative_to_the_first) {
    auto v = scene();
    sprite(v, 0, 40, 40, 1, 1, 3, 1);
    sprite(v, 1, 16, 48, 2, 1, 1, 0);
    // In screen coordinates; the first member (tile 3) is right of and above
    // the second: the image starts at the bounding box, x 16 y 40.
    add(v, pattern({member(40, 40, 1, 1, {tile_hash(v, 3)}), member(16, 48, 2, 1, {tile_hash(v, 1), tile_hash(v, 2)})},
                   32, 16, 3));
    new_frame(v);
    CHECK_EQ(render(v, 40)[16], rgb(kBlueC));
    CHECK_EQ(render(v, 55)[47], rgb(kBlueC));
    CHECK_EQ(render(v, 55)[48], rgb(0));
}

TEST(sprite_text, the_group_with_the_most_tiles_wins) {
    auto v = scene();
    sprite(v, 0, 16, 40, 2, 1, 1, 1);
    sprite(v, 1, 32, 40, 1, 1, 3, 0);
    add(v, pattern({member(0, 0, 2, 1, {tile_hash(v, 1), tile_hash(v, 2)})}, 16, 8, 2));
    add(v, pattern({member(0, 0, 2, 1, {tile_hash(v, 1), tile_hash(v, 2)}), member(16, 0, 1, 1, {tile_hash(v, 3)})},
                   24, 8, 3));
    new_frame(v);
    const auto a = render(v, 40);
    for (int x = 16; x < 40; ++x) CHECK_EQ(a[size_t(x)], rgb(kBlueC));
    CHECK_EQ(v.tiles->sprite_text.hits().size(), size_t(1));
}

TEST(sprite_text, the_image_keeps_its_first_members_place_among_the_sprites) {
    auto v = scene();
    // Entry 0 (in front): tile 2 at x 20; entry 1: the replaced 1x1 tile 1
    // at x 16; entry 2 (behind): tile 3 at x 12.
    sprite(v, 0, 20, 40, 1, 1, 2, 1);
    sprite(v, 1, 16, 40, 1, 1, 1, 2);
    sprite(v, 2, 12, 40, 1, 1, 3, 0);
    add(v, pattern({member(0, 0, 1, 1, {tile_hash(v, 1)})}, 8, 8, 3));
    new_frame(v);
    const auto a = render(v, 40);
    CHECK_EQ(a[12], rgb(kRedC));    // the sprite behind, where the image is not
    CHECK_EQ(a[16], rgb(kBlueC));   // the image over the sprite behind
    CHECK_EQ(a[19], rgb(kBlueC));
    CHECK_EQ(a[20], rgb(kGreenC));  // the sprite in front over the image
    CHECK_EQ(a[23], rgb(kGreenC));
}

TEST(sprite_text, the_status_flags_follow_the_original_sprites) {
    auto v = scene();
    sprite(v, 0, 16, 40, 1, 1, 1, 1);
    sprite(v, 1, 20, 40, 1, 1, 1, 0);  // overlaps entry 0: a collision
    add(v, pattern({member(0, 0, 1, 1, {tile_hash(v, 1)})}, 8, 8, 3));
    new_frame(v);
    v.sprite_collision = false;
    const auto a = render(v, 40);
    CHECK_EQ(a[16], rgb(kBlueC));
    CHECK_EQ(a[27], rgb(kBlueC));
    CHECK(v.sprite_collision);
    // And no collision is made up: the image itself collides with nothing.
    v.sprite_collision = false;
    render(v, 30);
    CHECK(!v.sprite_collision);
}

TEST(sprite_text, a_palette_change_keeps_the_image_a_move_does_not) {
    auto v = scene();
    sprite(v, 0, 16, 40, 1, 1, 1, 0);
    add(v, pattern({member(0, 0, 1, 1, {tile_hash(v, 1)})}, 8, 8, 3));
    new_frame(v);
    // During the picture: the palette line changes; the image follows it.
    word(v, kSat + 4, 1 | 0x2000);
    CHECK_EQ(render(v, 40)[16], rgb(kWhiteC));
    // The sprite moves: it is drawn as it is, without the image.
    word(v, kSat + 6, 17 + 128);
    const auto a = render(v, 41);
    CHECK_EQ(a[16], rgb(0));
    CHECK_EQ(a[17], rgb(kGreenC));  // tile 1 in line 1: colour 17
    // Matched again, at the new place, from the next picture.
    new_frame(v);
    CHECK_EQ(render(v, 41)[17], rgb(kWhiteC));
}

TEST(sprite_text, a_required_palette_line) {
    auto v = scene();
    sprite(v, 0, 16, 40, 1, 1, 1, 0);
    auto p = pattern({member(0, 0, 1, 1, {tile_hash(v, 1)})}, 8, 8, 3);
    p.palette = 1;
    add(v, p);
    new_frame(v);
    CHECK_EQ(render(v, 40)[16], rgb(kRedC));
    word(v, kSat + 4, 1 | 0x2000);
    new_frame(v);
    CHECK_EQ(render(v, 40)[16], rgb(kWhiteC));
}

TEST(sprite_text, malformed_patterns_are_refused) {
    SpriteTextReplacer t;
    std::string error;
    CHECK(!t.add(pattern({}, 8, 8, 1), &error));
    CHECK(!t.add(pattern({member(0, 0, 2, 1, {1})}, 8, 8, 1), &error));      // 2x1 with one tile
    CHECK(!t.add(pattern({member(0, 0, 5, 1, {1, 2, 3, 4, 5})}, 8, 8, 1), &error));
    auto p = pattern({member(0, 0, 1, 1, {1})}, 8, 8, 1);
    p.indices.pop_back();
    CHECK(!t.add(p, &error));
    CHECK(!t.add(pattern({member(0, 0, 1, 1, {1}), member(0, 0, 1, 1, {1})}, 8, 8, 1), &error));
    CHECK(t.add(pattern({member(0, 0, 1, 1, {1})}, 8, 8, 1), &error));
    CHECK_EQ(t.size(), size_t(1));
    int w, h;
    uint8_t f;
    CHECK(!SpriteTextReplacer::parse_size_token("0x1", w, h, f));
    CHECK(!SpriteTextReplacer::parse_size_token("4x5", w, h, f));
    CHECK(!SpriteTextReplacer::parse_size_token("4x3:x", w, h, f));
    CHECK(SpriteTextReplacer::parse_size_token("4X3:vh", w, h, f));
    CHECK_EQ(w * 10 + h, 43);
    CHECK_EQ(int(f), 3);
}

TEST(sprite_text, nothing_changes_without_a_match) {
    auto v = scene();
    sprite(v, 0, 16, 40, 2, 1, 1, 1);
    sprite(v, 1, 20, 40, 1, 1, 3, 0);
    const auto before = render(v, 40);
    v.sprite_collision = false;
    render(v, 40);
    const bool collided = v.sprite_collision;
    add(v, pattern({member(0, 0, 1, 1, {12345})}, 8, 8, 3));
    new_frame(v);
    v.sprite_collision = false;
    const auto after = render(v, 40);
    CHECK(before == after);
    CHECK_EQ(v.sprite_collision, collided);
}

TEST(sprite_text, the_dump_lists_the_sprites_in_groups) {
    auto v = scene();
    v.tiles = std::make_shared<TileReplacer>();
    v.tiles->dumping = true;
    // Two sprites side by side (8 pixels apart), one on another line.
    sprite(v, 0, 16, 40, 2, 1, 1, 1);
    sprite(v, 1, 40, 40, 1, 1, 3, 2);
    sprite(v, 2, 16, 80, 1, 1, 2, 0);
    auto& pending = v.tiles->sprite_text.pending;
    for (int i = 1; i < SpriteTextReplacer::kDumpSteady; ++i) new_frame(v);
    CHECK(pending.empty());
    new_frame(v);
    CHECK_EQ(pending.size(), size_t(1));
    if (pending.empty()) return;
    const std::string one = TextReplacer::token(tile_hash(v, 1), 0), two = TextReplacer::token(tile_hash(v, 2), 0),
                      three = TextReplacer::token(tile_hash(v, 3), 0);
    const std::string& text = pending[0].text;
    CHECK(text.find("sprite 0 0 2x1 " + one + " " + two + "\nsprite 24 0 1x1 " + three + "\n") != std::string::npos);
    CHECK(text.find("sprite 0 0 1x1 " + two + "\n") != std::string::npos);
    CHECK_EQ(pending[0].boxes.size(), size_t(2));
    if (pending[0].boxes.size() == 2) {
        CHECK_EQ(pending[0].boxes[0].x, 16);
        CHECK_EQ(pending[0].boxes[0].y, 40);
        CHECK_EQ(pending[0].boxes[0].w, 32);
        CHECK_EQ(pending[0].boxes[0].h, 8);
        CHECK_EQ(int(pending[0].boxes[0].plane), int('S'));
    }
    pending.clear();
    for (int i = 0; i < 40; ++i) new_frame(v);
    CHECK(pending.empty());
}
