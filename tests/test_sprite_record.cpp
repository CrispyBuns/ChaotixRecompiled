// 32X sprite recording and the host-side rebuild (issue #35): the rasteriser
// on hand-built graphics, drawing as the SH-2 blitters do, and the recorder's
// bookkeeping on a machine without a ROM.
#include "cpu/sh2/sh2.h"
#include "runtime/patches.h"
#include "runtime/sprite_record.h"
#include "runtime/system.h"
#include "test_framework.h"

#include <initializer_list>
#include <memory>
#include <vector>

using namespace chaotix;

namespace {

struct Span {
    int xs, xe, line;
    std::vector<uint8_t> px;
};

void put16(std::vector<uint8_t>& v, int x) {
    v.push_back(uint8_t(uint16_t(x) >> 8));
    v.push_back(uint8_t(x));
}

// A graphic in the game's format: box (x in pixels, y in lines x 256), then
// spans of xe - xs pixels at line x 256, then an end span.
std::vector<uint8_t> graphic(int x0, int x1, int y0, int y1, std::initializer_list<Span> spans) {
    std::vector<uint8_t> g;
    put16(g, x0);
    put16(g, x1);
    put16(g, y0 * 256);
    put16(g, y1 * 256);
    for (const Span& s : spans) {
        g.push_back(uint8_t(s.xs));
        g.push_back(uint8_t(s.xe));
        put16(g, s.line * 256);
        g.insert(g.end(), s.px.begin(), s.px.end());
    }
    g.push_back(0);
    g.push_back(0);
    put16(g, 0);
    return g;
}

// Frame buffer offset of a screen pixel (line table: line y at 0x200 + 512 y).
uint32_t at(int x, int y) { return uint32_t(0x200 + 512 * y + x) & 0x1FFFF; }

SpriteDraw call(int x, int y, uint32_t flags = 0) {
    SpriteDraw d;
    d.x = x;
    d.y = y;
    d.flags = flags;
    d.clip.scale_min = 0x40;
    d.clip.scale_max = 0x400;
    return d;
}

SpriteCanvas draw(const SpriteDraw& d, const std::vector<uint8_t>& g, const uint8_t* warp = nullptr) {
    SpriteCanvas c;
    CHECK(rasterise_sprite(d, g.data(), uint32_t(g.size()), c, warp));
    return c;
}

int coverage(const SpriteCanvas& c) {
    int n = 0;
    for (uint8_t v : c.cov) n += v;
    return n;
}

// Two lines: four pixels from x -2 on line 0 (one transparent), two on line 1.
std::vector<uint8_t> small() {
    return graphic(-2, 2, 0, 2, {{-2, 2, 0, {1, 2, 0, 4}}, {0, 2, 1, {5, 6}}});
}

} // namespace

TEST(sprite_raster, a_plain_call_puts_each_pixel_at_hotspot_plus_its_offset) {
    const SpriteCanvas c = draw(call(100, 50), small());
    CHECK_EQ(c.px[at(98, 50)], 1);
    CHECK_EQ(c.px[at(99, 50)], 2);
    CHECK_EQ(c.cov[at(100, 50)], 0);  // zero is transparent
    CHECK_EQ(c.px[at(101, 50)], 4);
    CHECK_EQ(c.px[at(100, 51)], 5);
    CHECK_EQ(c.px[at(101, 51)], 6);
    CHECK_EQ(coverage(c), 5);
}

TEST(sprite_raster, an_odd_hotspot_gives_the_same_pixels) {
    // The blitter copies words, shifted a byte for an odd X; the zero bytes
    // around them are transparent.
    const SpriteCanvas c = draw(call(101, 50), small());
    CHECK_EQ(c.px[at(99, 50)], 1);
    CHECK_EQ(c.px[at(102, 50)], 4);
    CHECK_EQ(c.px[at(102, 51)], 6);
    CHECK_EQ(coverage(c), 5);
}

TEST(sprite_raster, flips_mirror_around_the_pixel_left_of_and_the_line_above_the_hotspot) {
    // h-flip: source column s lands on X - 1 - s; v-flip: line l on Y - 1 - l.
    const SpriteCanvas h = draw(call(100, 50, 1), small());
    CHECK_EQ(h.px[at(101, 50)], 1);
    CHECK_EQ(h.px[at(100, 50)], 2);
    CHECK_EQ(h.px[at(98, 50)], 4);
    CHECK_EQ(h.px[at(99, 51)], 5);
    CHECK_EQ(h.px[at(98, 51)], 6);
    const SpriteCanvas v = draw(call(100, 50, 2), small());
    CHECK_EQ(v.px[at(98, 49)], 1);
    CHECK_EQ(v.px[at(101, 48)], 6);
    const SpriteCanvas hv = draw(call(100, 50, 3), small());
    CHECK_EQ(hv.px[at(101, 49)], 1);
    CHECK_EQ(hv.px[at(98, 48)], 6);
    CHECK_EQ(coverage(hv), 5);
}

TEST(sprite_raster, flag_4_takes_the_other_palette_half_and_flag_8_every_second_column) {
    const SpriteCanvas p = draw(call(100, 50, 4), small());
    CHECK_EQ(p.px[at(98, 50)], 0x81);
    CHECK_EQ(p.px[at(101, 51)], 0x86);
    CHECK_EQ(p.cov[at(100, 50)], 0);
    // Stipple: the even source pixels; flipped, the odd ones, so that both
    // land on the same screen columns.
    const SpriteCanvas s = draw(call(100, 50, 8), small());
    CHECK_EQ(s.px[at(98, 50)], 1);
    CHECK_EQ(s.cov[at(99, 50)], 0);
    CHECK_EQ(s.px[at(100, 51)], 5);
    CHECK_EQ(s.cov[at(101, 51)], 0);
    CHECK_EQ(coverage(s), 2);  // the third even pixel is the transparent one
    const SpriteCanvas sh = draw(call(100, 50, 9), small());
    CHECK_EQ(sh.px[at(100, 50)], 2);
    CHECK_EQ(sh.px[at(98, 50)], 4);
    CHECK_EQ(sh.px[at(98, 51)], 6);
    CHECK_EQ(coverage(sh), 3);
    // Flags 12-15 are past the end of the blitter's jump table.
    SpriteCanvas none;
    const std::vector<uint8_t> g = small();
    CHECK(!rasterise_sprite(call(100, 50, 12), g.data(), uint32_t(g.size()), none));
}

TEST(sprite_raster, a_character_takes_the_colour_offset_and_can_wrap_to_transparent) {
    SpriteDraw d = call(100, 50);
    d.colour = 0x12;
    const SpriteCanvas c = draw(d, small());
    CHECK_EQ(c.px[at(98, 50)], 0x13);
    CHECK_EQ(c.cov[at(100, 50)], 0);  // transparent stays transparent
    d.colour = 0xFC;                   // 4 + 0xFC wraps to 0: transparent
    const SpriteCanvas w = draw(d, small());
    CHECK_EQ(w.cov[at(101, 50)], 0);
    CHECK_EQ(w.px[at(98, 50)], 0xFD);
}

TEST(sprite_raster, clipping_follows_the_clip_rectangle_and_its_inclusive_bottom) {
    SpriteDraw d = call(319, 223);
    const SpriteCanvas c = draw(d, small());  // x 317..320, lines 223..224
    CHECK_EQ(c.px[at(317, 223)], 1);
    CHECK_EQ(c.px[at(318, 223)], 2);
    CHECK_EQ(c.cov[at(320, 223)], 0);  // right edge is exclusive
    CHECK_EQ(c.cov[at(319, 224)], 0);  // line 224 is below the bottom (223, inclusive)
    CHECK_EQ(coverage(c), 2);
    d = call(1, 0, 2);  // v-flip at line 0: everything is above the top
    SpriteCanvas none;
    const std::vector<uint8_t> g = small();
    CHECK(rasterise_sprite(d, g.data(), uint32_t(g.size()), none));
    CHECK_EQ(coverage(none), 0);
    // A widened rectangle lets the pixels into the line padding (x < 0 is the
    // end of the line before), where the machine sends them to its shadow.
    d = call(1, 10);
    d.clip.left = -64;
    const SpriteCanvas wide = draw(d, small());
    CHECK_EQ(wide.px[at(-1, 10)], 1);
}

TEST(sprite_raster, a_box_inside_the_rectangle_skips_the_span_clipping) {
    // The blitter tests the graphic's box: inside, spans are copied as they
    // are, even one the box does not hold (it trusts the box).
    const std::vector<uint8_t> g = graphic(0, 2, 0, 1, {{0, 2, 0, {7, 7}}, {30, 32, 0, {8, 8}}});
    const SpriteCanvas c = draw(call(300, 10), g);
    CHECK_EQ(c.px[at(331, 10)], 8);  // past the right edge, drawn anyway
    const SpriteCanvas e = draw(call(319, 10), g);  // box across the edge: spans clipped
    CHECK_EQ(e.px[at(319, 10)], 7);
    CHECK_EQ(e.cov[at(349, 10)], 0);
}

TEST(sprite_raster, the_scaled_blitter_stretches_by_the_inverse_of_the_scale) {
    // 0x80 (8.8) is twice the size: each pixel fills 2 x 2, from X + 2 x0.
    SpriteDraw d = call(100, 50);
    d.kind = SpriteKind::Scaled;
    d.sx = d.sy = 0x80;
    const SpriteCanvas c = draw(d, small());
    CHECK_EQ(c.px[at(96, 50)], 1);
    CHECK_EQ(c.px[at(97, 51)], 1);
    CHECK_EQ(c.px[at(98, 50)], 2);
    CHECK_EQ(c.px[at(99, 51)], 2);
    CHECK_EQ(c.cov[at(100, 50)], 0);
    CHECK_EQ(c.px[at(103, 51)], 4);
    CHECK_EQ(c.px[at(100, 52)], 5);
    CHECK_EQ(c.px[at(103, 53)], 6);
    CHECK_EQ(coverage(c), 20);
    // At 0x100 it is the plain blitter's picture.
    d.sx = d.sy = 0x100;
    const SpriteCanvas one = draw(d, small());
    const SpriteCanvas plain = draw(call(100, 50), small());
    CHECK(one.px == plain.px && one.cov == plain.cov);
    // Out of the limits at 0x06003840: nothing.
    d.sx = 0x500;
    SpriteCanvas none;
    const std::vector<uint8_t> g = small();
    CHECK(rasterise_sprite(d, g.data(), uint32_t(g.size()), none));
    CHECK_EQ(coverage(none), 0);
}

TEST(sprite_raster, scaled_positions_are_truncated_16_16_steps) {
    // 0xC0: step 0x1000000 / 0xC0 = 0x15555, a little under 1.333 px, so
    // column s starts at floor(X + s * 0x15555 / 65536): 100, 101, 102, 103
    // (3 steps are 0x3FFFF, just short of 4) and 105.
    SpriteDraw d = call(100, 50);
    d.kind = SpriteKind::Scaled;
    d.sx = 0xC0;
    d.sy = 0x100;
    const std::vector<uint8_t> g = graphic(0, 4, 0, 1, {{0, 4, 0, {1, 2, 3, 4}}});
    const SpriteCanvas c = draw(d, g);
    CHECK_EQ(c.px[at(100, 50)], 1);
    CHECK_EQ(c.px[at(101, 50)], 2);
    CHECK_EQ(c.px[at(102, 50)], 3);
    CHECK_EQ(c.px[at(103, 50)], 4);
    CHECK_EQ(c.px[at(104, 50)], 4);
    CHECK_EQ(c.cov[at(105, 50)], 0);
    // Flipped, positions run leftwards from just under X (X - 1/65536):
    // rightmost columns 99, 98, 97, then 96 (exactly 96.0 after three
    // steps) covering two.
    d.flags = 1;
    const SpriteCanvas f = draw(d, g);
    CHECK_EQ(f.px[at(99, 50)], 1);
    CHECK_EQ(f.px[at(98, 50)], 2);
    CHECK_EQ(f.px[at(97, 50)], 3);
    CHECK_EQ(f.px[at(96, 50)], 4);
    CHECK_EQ(f.px[at(95, 50)], 4);
    CHECK_EQ(f.cov[at(94, 50)], 0);
}

TEST(sprite_raster, the_scaled_blitter_sign_extends_upper_palette_pixels_into_their_word) {
    // mov.b sign-extends, and the routine ORs pixels into a 16-bit store:
    // a pixel of 0x80 or more doubled in width at an even column fills the
    // whole word with 0xFF, as the game's frame buffer shows (the game's own
    // graphics keep their scaled pixels below 0x80).
    SpriteDraw d = call(100, 50);
    d.kind = SpriteKind::Scaled;
    d.sx = 0x80;
    d.sy = 0x100;
    const std::vector<uint8_t> g = graphic(0, 2, 0, 1, {{0, 2, 0, {0x85, 0x05}}});
    const SpriteCanvas c = draw(d, g);
    CHECK_EQ(c.px[at(100, 50)], 0xFF);
    CHECK_EQ(c.px[at(101, 50)], 0xFF);
    CHECK_EQ(c.px[at(102, 50)], 0x05);
    CHECK_EQ(c.px[at(103, 50)], 0x05);
}

TEST(sprite_raster, the_warped_blitter_without_a_wave_is_a_plain_scale) {
    // Parameters per axis: p0 the scale (8.8), p1 and p2 the sine's phase
    // across the graphic, p3 its amplitude. p0 0x100 and no amplitude is 1:1.
    std::vector<uint8_t> warp;
    for (int axis = 0; axis < 2; ++axis)
        for (int v : {0x100, 0, 0x40, 0}) put16(warp, v);
    for (int i = 0; i < 256; ++i) put16(warp, (i * 97) % 512 - 256);  // any sine table
    SpriteDraw d = call(100, 50);
    d.kind = SpriteKind::Warped;
    const std::vector<uint8_t> g = graphic(-2, 2, 0, 2, {{-2, 2, 0, {1, 2, 0, 4}}, {0, 2, 1, {5, 6}}});
    const SpriteCanvas c = draw(d, g, warp.data());
    const SpriteCanvas plain = draw(call(100, 50), g);
    CHECK(c.px == plain.px && c.cov == plain.cov);
    // p0 0x80 doubles it.
    warp[0] = 0x00;
    warp[1] = 0x80;
    const SpriteCanvas w = draw(d, g, warp.data());
    CHECK_EQ(w.px[at(96, 50)], 1);
    CHECK_EQ(w.px[at(97, 50)], 1);
    CHECK_EQ(w.px[at(103, 51)], 6);
    CHECK_EQ(coverage(w), 10);
    // With a wave, the columns are as wide as the sine says but still add up
    // to the graphic's scaled size (8 columns at 2x).
    warp[6] = 0x00;
    warp[7] = 0x80;  // amplitude
    const std::vector<uint8_t> row = graphic(0, 8, 0, 1, {{0, 8, 0, {1, 1, 1, 1, 1, 1, 1, 1}}});
    const SpriteCanvas wave = draw(d, row, warp.data());
    CHECK_EQ(coverage(wave), 16);
}

TEST(sprite_raster, a_list_is_drawn_in_order) {
    SpriteList list;
    const std::vector<uint8_t> g = small();
    for (int k = 0; k < 2; ++k) {
        SpriteDraw d = call(100 + k, 50);
        d.colour = uint8_t(0x10 * k);
        d.data = uint32_t(list.pool.size());
        d.size = uint32_t(g.size());
        list.pool.insert(list.pool.end(), g.begin(), g.end());
        list.draws.push_back(d);
    }
    SpriteCanvas c;
    rebuild_sprites(list, c);
    CHECK_EQ(c.px[at(98, 50)], 1);     // only the first
    CHECK_EQ(c.px[at(99, 50)], 0x11);  // the second over the first
}

TEST(sprite_raster, graphic_size_finds_the_end_span) {
    const std::vector<uint8_t> g = small();
    CHECK_EQ(SpriteRecorder::graphic_size(g.data(), uint32_t(g.size())), uint32_t(g.size() - 2));
    CHECK_EQ(SpriteRecorder::graphic_size(g.data(), 12), 0u);  // runs past the limit
    const std::vector<uint8_t> odd = graphic(0, 3, 0, 1, {{0, 3, 0, {1, 2, 3}}});
    CHECK_EQ(SpriteRecorder::graphic_size(odd.data(), uint32_t(odd.size())), 0u);
}

namespace {

void be32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v);
}

// A machine with no ROM, the hooked routines' first instructions in place
// (the recorder checks them), a native clip rectangle, a bank table whose
// bank 0x21 has two frames in SDRAM, and a graphic as frame 1.
std::unique_ptr<Machine> machine() {
    auto m = std::make_unique<Machine>();
    auto code = [&](uint32_t pc, uint16_t op) { m->sdram[pc & 0x3FFFF] = uint8_t(op >> 8); m->sdram[(pc & 0x3FFFF) + 1] = uint8_t(op); };
    code(patches::kSh2RecolourCopy, 0x2F46);
    code(patches::kSh2Blit, 0xD91A);
    code(patches::kSh2BlitScaled, 0xD02C);
    m->sdram[0x3836] = 0x01; m->sdram[0x3837] = 0x40;  // right 320
    be32(m->sdram + 0x383C, 223 << 8);
    be32(m->sdram + 0x3614 + 4 * 0x21, 0x06010000);
    be32(m->sdram + 0x10000, 0x06010100);
    be32(m->sdram + 0x10004, 0x06010200);
    const std::vector<uint8_t> g = small();
    std::copy(g.begin(), g.end(), m->sdram + 0x10200);
    m->mars.fb_display = 0;
    return m;
}

} // namespace

TEST(sprite_record, a_blit_is_recorded_for_the_back_buffer_with_its_bank_and_frame) {
    auto m = machine();
    SpriteRecorder r;
    sh2::State c{};
    c.r[1] = 100; c.r[2] = 50; c.r[3] = 1; c.r[4] = 0x26010200;  // cache-through
    r.hook(*m, c, patches::kSh2Blit);
    CHECK_EQ(r.list(1).draws.size(), size_t(1));  // the back buffer
    CHECK_EQ(r.list(0).draws.size(), size_t(0));
    const SpriteDraw& d = r.list(1).draws[0];
    CHECK_EQ(d.bank, 0x21);
    CHECK_EQ(d.frame, 1);
    CHECK_EQ(d.x, 100);
    CHECK_EQ(d.flags, 1u);
    CHECK_EQ(d.size, uint32_t(small().size() - 2));
    CHECK_EQ(d.clip.right, 320);
    // Its picture comes back from the recorded bytes alone.
    SpriteCanvas canvas;
    rebuild_sprites(r.list(1), canvas);
    CHECK_EQ(canvas.px[at(101, 50)], 1);
    // An address that no table holds is named by its bytes only.
    std::copy(m->sdram + 0x10200, m->sdram + 0x10230, m->sdram + 0x11000);
    c.r[4] = 0x06011000;
    r.hook(*m, c, patches::kSh2Blit);
    CHECK_EQ(r.list(1).draws[1].bank, -1);
    CHECK_EQ(r.list(1).draws[1].hash, d.hash);
}

TEST(sprite_record, a_character_is_named_by_the_recolour_copys_source) {
    auto m = machine();
    SpriteRecorder r;
    sh2::State c{};
    c.r[1] = 0x06010200; c.r[2] = 0x06005310; c.r[3] = 0x24;
    r.hook(*m, c, patches::kSh2RecolourCopy);
    // What the copy at 0x06000810 leaves in the scratch buffer.
    std::vector<uint8_t> copy = small();
    for (size_t i : {12, 13, 15, 20, 21}) copy[i] = uint8_t(copy[i] + 0x24);
    std::copy(copy.begin(), copy.end(), m->sdram + 0x5310);
    c.r[1] = 10; c.r[2] = 20; c.r[3] = 0; c.r[4] = 0x06005310; c.r[5] = 0x80; c.r[6] = 0x80;
    r.hook(*m, c, patches::kSh2BlitScaled);
    const SpriteDraw& d = r.list(1).draws.at(0);
    CHECK_EQ(d.graphic, 0x06005310u);
    CHECK_EQ(d.source, 0x06010200u);
    CHECK_EQ(d.colour, 0x24);
    CHECK_EQ(d.bank, 0x21);
    CHECK(d.kind == SpriteKind::Scaled);
    CHECK_EQ(d.sx, 0x80);
}

TEST(sprite_record, a_copy_changed_after_the_recolour_is_drawn_as_its_bytes) {
    // The routine before the draws from 0x0600D310 changes its copy; the
    // blitter then draws what the copy holds, not the recoloured source.
    auto m = machine();
    SpriteRecorder r;
    sh2::State c{};
    const std::vector<uint8_t> g = small();
    std::copy(g.begin(), g.end(), m->sdram + 0x5310);  // the copy, offset 0
    c.r[1] = 0x06010200; c.r[2] = 0x06005310; c.r[3] = 0;
    r.hook(*m, c, patches::kSh2RecolourCopy);
    m->sdram[0x5310 + 12] = 9;  // changed after the copy
    c.r[1] = 100; c.r[2] = 50; c.r[3] = 0; c.r[4] = 0x06005310;
    r.hook(*m, c, patches::kSh2Blit);
    const SpriteDraw& d = r.list(1).draws.at(0);
    CHECK_EQ(d.source, 0x06005310u);
    CHECK_EQ(d.bank, -1);
    SpriteCanvas canvas;
    rebuild_sprites(r.list(1), canvas);
    CHECK_EQ(canvas.px[at(98, 50)], 9);
}

TEST(sprite_record, a_flip_shows_a_complete_list_and_starts_the_new_back_buffers) {
    auto m = machine();
    SpriteRecorder r;
    sh2::State c{};
    c.r[4] = 0x06010200;
    r.hook(*m, c, patches::kSh2Blit);
    m->mars.fb_display = 1;
    r.flipped(*m);
    CHECK_EQ(r.displayed(*m).draws.size(), size_t(1));
    r.hook(*m, c, patches::kSh2Blit);
    r.hook(*m, c, patches::kSh2Blit);
    CHECK_EQ(r.list(0).draws.size(), size_t(2));
    const uint64_t serial = r.list(1).serial;
    m->mars.fb_display = 0;
    r.flipped(*m);
    CHECK_EQ(r.displayed(*m).draws.size(), size_t(2));
    CHECK_EQ(r.list(1).draws.size(), size_t(0));
    CHECK(r.list(1).serial != serial);
    // Another routine at a hooked address is not a blitter call.
    m->sdram[patches::kSh2Blit & 0x3FFFF] = 0;
    r.hook(*m, c, patches::kSh2Blit);
    CHECK_EQ(r.list(1).draws.size(), size_t(0));
}

TEST(sprite_record, the_self_test_tells_blitter_bytes_from_polygons) {
    auto m = machine();
    auto r = std::make_shared<SpriteRecorder>();
    r->track_writes = true;
    m->sprites = r;
    sh2::State c{};
    c.r[1] = 100; c.r[2] = 50; c.r[4] = 0x06010200;
    r->hook(*m, c, patches::kSh2Blit);
    // What the blitter writes (overwrite image) into the back buffer ...
    const uint8_t px[5][3] = {{98, 50, 1}, {99, 50, 2}, {101, 50, 4}, {100, 51, 5}, {101, 51, 6}};
    for (const auto& p : px) {
        m->mars.fb[1][at(p[0], p[1])] = p[2];
        r->fb_write(*m, at(p[0], p[1]), p[2], 1, true);
    }
    // ... and a polygon over one of its pixels afterwards.
    m->mars.fb[1][at(99, 50)] = 9;
    r->fb_write(*m, at(99, 50), 9, 1, false);
    m->mars.fb_display = 1;
    r->flipped(*m);
    SpriteCanvas canvas;
    SpriteCheck k = check_sprites(*m, *r, canvas);
    CHECK_EQ(k.compared, 4u);
    CHECK_EQ(k.mismatches(), 0u);
    CHECK_EQ(k.covered, 1u);
    // A blitter byte the rebuild does not explain is a mismatch.
    m->mars.fb[1][at(101, 51)] = 7;
    k = check_sprites(*m, *r, canvas);
    CHECK_EQ(k.wrong, 1u);
    CHECK_EQ(uint32_t(k.first), at(101, 51));
}
