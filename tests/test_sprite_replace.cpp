// 32X sprite replacement (issue #35): replacement graphics drawn by the
// rebuild of the blitters, the composed picture over fills, polygons and the
// widescreen margins, the dumps and the pack files; on hand-built graphics
// and a machine without a ROM.
#include "cpu/sh2/sh2.h"
#include "renderer/image_io.h"
#include "renderer/sprite_pack.h"
#include "runtime/patches.h"
#include "runtime/sprite_record.h"
#include "runtime/sprite_replace.h"
#include "runtime/system.h"
#include "test_framework.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <string>
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

// A graphic in the game's format (see test_sprite_record.cpp).
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

// The box is x -2..2, lines 0..2 (the bottom line is inclusive): four pixels
// from x -2 on line 0 (one transparent), two on line 1, nothing on line 2.
std::vector<uint8_t> small() {
    return graphic(-2, 2, 0, 2, {{-2, 2, 0, {1, 2, 0, 4}}, {0, 2, 1, {5, 6}}});
}

SpriteImage image_of(const std::vector<uint8_t>& g) {
    SpriteImage img;
    CHECK(graphic_image(g.data(), uint32_t(g.size()), img));
    return img;
}

std::vector<uint8_t> replaced(const std::vector<uint8_t>& g, const SpriteImage& img) {
    std::vector<uint8_t> out;
    std::string why;
    CHECK(replacement_graphic(g.data(), uint32_t(g.size()), img, out, &why));
    return out;
}

std::vector<uint8_t> warp_params(int scale) {
    std::vector<uint8_t> warp;
    for (int axis = 0; axis < 2; ++axis)
        for (int v : {scale, 0, 0x40, 0}) put16(warp, v);
    for (int i = 0; i < 256; ++i) put16(warp, (i * 97) % 512 - 256);
    return warp;
}

} // namespace

TEST(sprite_replace, a_graphic_becomes_an_image_of_its_box) {
    const SpriteImage img = image_of(small());
    CHECK_EQ(img.width, 4);
    CHECK_EQ(img.height, 3);
    const std::vector<uint8_t> want = {1, 2, 0, 4, 0, 0, 5, 6, 0, 0, 0, 0};
    CHECK(img.px == want);
    // A span outside the box cannot be an image's pixel.
    const std::vector<uint8_t> out = graphic(0, 2, 0, 1, {{0, 2, 0, {7, 7}}, {30, 32, 0, {8, 8}}});
    SpriteImage none;
    CHECK(!graphic_image(out.data(), uint32_t(out.size()), none));
}

TEST(sprite_replace, the_dumped_image_as_a_replacement_draws_exactly_the_original) {
    // Every way of drawing: flips, the palette half, the stipple, odd
    // hotspots, the colour offset, the scaled and the warped blitters, a clip.
    const std::vector<std::vector<uint8_t>> graphics = {
        small(),
        graphic(-4, 4, -2, 1, {{-4, 0, -2, {1, 0, 3, 4}}, {-2, 4, -1, {5, 6, 7, 8, 9, 10}}, {0, 2, 1, {11, 0}}}),
    };
    for (const auto& g : graphics) {
        const std::vector<uint8_t> r = replaced(g, image_of(g));
        CHECK(r == g);  // the same spans, the same pixels
        for (uint32_t flags = 0; flags < 12; ++flags)
            for (int x : {100, 101, 318}) {
                SpriteDraw d = call(x, 50, flags);
                d.colour = uint8_t(flags * 0x12);
                const SpriteCanvas a = draw(d, g), b = draw(d, r);
                CHECK(a.px == b.px && a.cov == b.cov);
                d.kind = SpriteKind::Scaled;
                d.flags &= 7;
                d.sx = 0xC0;
                d.sy = 0x80;
                const SpriteCanvas sa = draw(d, g), sb = draw(d, r);
                CHECK(sa.px == sb.px && sa.cov == sb.cov);
                const std::vector<uint8_t> warp = warp_params(0x80);
                d.kind = SpriteKind::Warped;
                const SpriteCanvas wa = draw(d, g, warp.data()), wb = draw(d, r, warp.data());
                CHECK(wa.px == wb.px && wa.cov == wb.cov);
            }
    }
}

TEST(sprite_replace, replaced_pixels_are_drawn_as_the_call_draws_the_original) {
    const std::vector<uint8_t> g = small();
    SpriteImage img = image_of(g);
    // New values, one where the original's span holds a transparent pixel
    // (x 0, line 0), one where no span reaches (x -2, line 2), and one
    // pixel taken away (x -1, line 0).
    img.px = {0x21, 0, 0x23, 0x24, 0, 0, 0x25, 0x26, 0x27, 0, 0, 0};
    const std::vector<uint8_t> r = replaced(g, img);
    const SpriteCanvas c = draw(call(100, 50), r);
    CHECK_EQ(c.px[at(98, 50)], 0x21);
    CHECK_EQ(c.cov[at(99, 50)], 0);  // transparent: the original's pixel is gone
    CHECK_EQ(c.px[at(100, 50)], 0x23);
    CHECK_EQ(c.px[at(101, 50)], 0x24);
    CHECK_EQ(c.px[at(101, 51)], 0x26);
    CHECK_EQ(c.px[at(98, 52)], 0x27);
    CHECK_EQ(coverage(c), 6);
    // The colour offset of a character, and the other palette half.
    SpriteDraw d = call(100, 50);
    d.colour = 0x12;
    CHECK_EQ(draw(d, r).px[at(98, 52)], 0x39);
    d.flags = 4;
    CHECK_EQ(draw(d, r).px[at(98, 52)], 0xB9);
    // Flipped: column s on X - 1 - s, line l on Y - 1 - l.
    const SpriteCanvas h = draw(call(100, 50, 3), r);
    CHECK_EQ(h.px[at(101, 49)], 0x21);
    CHECK_EQ(h.px[at(101, 47)], 0x27);
    // The stipple keeps the original's columns: even source columns, so
    // the new span on line 2 starts at an even column too.
    const SpriteCanvas s = draw(call(100, 50, 8), r);
    CHECK_EQ(s.px[at(98, 50)], 0x21);
    CHECK_EQ(s.px[at(100, 50)], 0x23);
    CHECK_EQ(s.cov[at(101, 50)], 0);
    CHECK_EQ(s.px[at(98, 52)], 0x27);
    // Scaled twice the size: 2 x 2 each, from X + 2 x0.
    d = call(100, 50);
    d.kind = SpriteKind::Scaled;
    d.sx = d.sy = 0x80;
    const SpriteCanvas z = draw(d, r);
    CHECK_EQ(z.px[at(96, 54)], 0x27);
    CHECK_EQ(z.px[at(97, 55)], 0x27);
    CHECK_EQ(z.cov[at(98, 50)], 0);
}

TEST(sprite_replace, another_size_gets_a_box_of_its_own_at_the_same_corner) {
    const std::vector<uint8_t> g = small();
    SpriteImage big;
    big.width = 7;
    big.height = 4;
    big.px.assign(28, 5);
    big.px[0] = 0;   // its top-left pixel transparent
    big.px[27] = 9;  // its bottom-right one
    const std::vector<uint8_t> r = replaced(g, big);
    const SpriteCanvas c = draw(call(100, 50), r);
    CHECK_EQ(c.cov[at(98, 50)], 0);
    CHECK_EQ(c.px[at(99, 50)], 5);
    CHECK_EQ(c.px[at(104, 53)], 9);   // x -2 + 6, line 3
    CHECK_EQ(c.cov[at(105, 53)], 0);  // the odd width is padded with transparent pixels
    CHECK_EQ(coverage(c), 27);
    // Flipped about the same hotspot, scaled from the same corner.
    const SpriteCanvas h = draw(call(100, 50, 1), r);
    CHECK_EQ(h.px[at(95, 53)], 9);
    SpriteDraw d = call(100, 50);
    d.kind = SpriteKind::Scaled;
    d.sx = d.sy = 0x80;
    const SpriteCanvas z = draw(d, r);
    CHECK_EQ(z.px[at(109, 57)], 9);
    CHECK_EQ(coverage(z), 4 * 27);
    // Smaller works the same way.
    SpriteImage one;
    one.width = one.height = 1;
    one.px = {3};
    const SpriteCanvas o = draw(call(100, 50), replaced(g, one));
    CHECK_EQ(o.px[at(98, 50)], 3);
    CHECK_EQ(coverage(o), 1);
    // The spans hold columns and lines as bytes: from x -2 a box cannot be
    // 140 pixels wide.
    SpriteImage wide;
    wide.width = 140;
    wide.height = 1;
    wide.px.assign(140, 1);
    std::vector<uint8_t> out;
    std::string why;
    CHECK(!replacement_graphic(g.data(), uint32_t(g.size()), wide, out, &why));
    CHECK(why.find("too large") != std::string::npos);
    SpriteImage empty;
    CHECK(!replacement_graphic(g.data(), uint32_t(g.size()), empty, out, &why));
}

TEST(sprite_replace, the_dump_palette_shows_each_value_as_it_was_drawn) {
    uint16_t cram[256];
    for (int i = 0; i < 256; ++i) cram[i] = uint16_t(i);  // red = i & 31, green = i >> 5
    auto argb = [](uint16_t c) {
        uint32_t r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
        return 0xFF000000u | ((r << 3 | r >> 2) << 16) | ((g << 3 | g >> 2) << 8) | (b << 3 | b >> 2);
    };
    uint32_t pal[256];
    SpriteDraw d = call(0, 0);
    d.colour = 0x12;
    sprite_dump_palette(d, cram, pal);
    CHECK_EQ(pal[0], 0u);
    CHECK_EQ(pal[1], argb(0x13));
    CHECK_EQ(pal[0xEE], 0u);  // 0xEE + 0x12 wraps to 0: transparent, as drawn
    CHECK_EQ(pal[0xEF], argb(0x01));
    d.flags = 4;  // the plain blitter subtracts 0x80, the scaled ones OR it in
    sprite_dump_palette(d, cram, pal);
    CHECK_EQ(pal[1], argb(0x93));
    CHECK_EQ(pal[0x70], argb(0x02));
    d.kind = SpriteKind::Scaled;
    sprite_dump_palette(d, cram, pal);
    CHECK_EQ(pal[0x70], argb(0x82));
    d.flags = 8;  // the stipple is not the palette half
    d.kind = SpriteKind::Plain;
    sprite_dump_palette(d, cram, pal);
    CHECK_EQ(pal[1], argb(0x13));
}

namespace {

void be32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v);
}

// A machine with no ROM to run: the hooked routines' first instructions, a
// native clip rectangle, bank 0x21 with small() as frame 1, a standard line
// table in both buffers, a palette of distinct colours, packed pixels on.
std::unique_ptr<Machine> machine() {
    auto m = std::make_unique<Machine>();
    m->rom.data.resize(0x400000);
    m->reset();
    m->m68k.stopped = true;
    auto code = [&](uint32_t pc, uint16_t op) { m->sdram[pc & 0x3FFFF] = uint8_t(op >> 8); m->sdram[(pc & 0x3FFFF) + 1] = uint8_t(op); };
    code(patches::kSh2RecolourCopy, 0x2F46);
    code(patches::kSh2Blit, 0xD91A);
    code(patches::kSh2BlitScaled, 0xD02C);
    m->sdram[0x3836] = 0x01; m->sdram[0x3837] = 0x40;  // right 320
    be32(m->sdram + 0x383C, 223 << 8);
    m->sdram[0x3840] = 0x00; m->sdram[0x3841] = 0x40;  // scale limits
    m->sdram[0x3842] = 0x04; m->sdram[0x3843] = 0x00;
    be32(m->sdram + 0x3614 + 4 * 0x21, 0x06010000);
    be32(m->sdram + 0x10000, 0x06010100);
    be32(m->sdram + 0x10004, 0x06010200);
    const std::vector<uint8_t> g = small();
    std::copy(g.begin(), g.end(), m->sdram + 0x10200);
    for (int b = 0; b < 2; ++b)
        for (int y = 0; y < 256; ++y) {
            m->mars.fb[b][2 * y] = uint8_t((0x100 + 0x100 * y) >> 8);
            m->mars.fb[b][2 * y + 1] = 0;
        }
    for (int i = 0; i < 256; ++i) m->mars.pal[i] = uint16_t(i * 0x0101 & 0x7FFF);
    m->mars.adapter_ctrl = 1;
    m->mars.bitmap_mode = 1;
    m->mars.fb_display = m->mars.fb_select_req = 0;
    return m;
}

// The display flips, as at V-blank.
void flip(Machine& m) {
    m.mars.fb_display ^= 1;
    m.mars.fb_select_req = m.mars.fb_display;
    if (m.sprites) m.sprites->flipped(m);
}

// The blitter call the hook sees, then its writes through the overwrite
// image, made by the machine as the blitter's stores would be.
void blit(Machine& m, int x, int y, uint32_t flags = 0) {
    sh2::State c{};
    c.r[1] = uint32_t(x); c.r[2] = uint32_t(y); c.r[3] = flags; c.r[4] = 0x06010200;
    m.sprites->hook(m, c, patches::kSh2Blit);
    const SpriteList& l = m.sprites->list(m.mars.fb_display ^ 1);
    const SpriteDraw& d = l.draws.back();
    auto store = [](void* user, uint32_t o, uint8_t v) { static_cast<Machine*>(user)->sh2_write(0, 0x24020000u + o, v, 1); };
    CHECK(rasterise_sprite(d, l.bytes(d), d.size, store, &m));
}

// A polygon's pixel (a direct write), and an auto fill of a line.
void poke(Machine& m, int x, int y, uint8_t v) { m.sh2_write(0, 0x24000000u + at(x, y), v, 1); }
void fill_line(Machine& m, int y, uint16_t data) {
    m.sh2_write(0, 0x20004104, 159, 2);
    m.sh2_write(0, 0x20004106, (0x200 + 512 * y) / 2, 2);
    m.sh2_write(0, 0x20004108, data, 2);
}

std::shared_ptr<SpritePack> pack_of(SpriteImage img) {
    auto p = std::make_shared<SpritePack>();
    p->add(0x21, 1, std::move(img));
    return p;
}

struct Shown {
    const uint8_t* fb;
    const uint8_t* margin;
    bool composed;
};
Shown shown(Machine& m) {
    Shown s{m.mars.fb[m.mars.fb_display], m.fb_margin[m.mars.fb_display], false};
    m.sprites->shown(m, s.fb, s.margin);
    s.composed = s.fb != m.mars.fb[m.mars.fb_display];
    return s;
}

} // namespace

TEST(sprite_replace, the_picture_is_composed_over_fills_and_polygons) {
    auto m = machine();
    m->sprites = std::make_shared<SpriteRecorder>();
    SpriteImage img = image_of(small());
    img.px = {0x31, 0x32, 0x33, 0, 0, 0, 0x35, 0x36, 0x37, 0x37, 0x37, 0x37};
    m->sprites->set_pack(pack_of(img));
    flip(*m);  // the back buffer is tracked from its start
    fill_line(*m, 50, 0x0303);
    fill_line(*m, 52, 0x0303);
    poke(*m, 99, 52, 0x44);   // a polygon before the sprite, under its new pixels
    blit(*m, 100, 50);
    poke(*m, 101, 51, 0x55);  // a polygon after the sprite, over it
    const uint8_t* fb = m->mars.fb[m->mars.fb_display ^ 1];
    CHECK_EQ(fb[at(101, 50)], 4);  // the machine drew the original
    flip(*m);
    const Shown s = shown(*m);
    CHECK(s.composed);
    CHECK_EQ(s.fb[at(98, 50)], 0x31);
    CHECK_EQ(s.fb[at(100, 50)], 0x33);  // a transparent pixel of the original
    CHECK_EQ(s.fb[at(101, 50)], 0x03);  // taken away: the fill under it shows
    CHECK_EQ(s.fb[at(100, 51)], 0x35);
    CHECK_EQ(s.fb[at(101, 51)], 0x55);  // the polygon drawn after stays on top
    CHECK_EQ(s.fb[at(99, 52)], 0x37);   // the polygon drawn before is covered
    CHECK_EQ(s.fb[at(101, 52)], 0x37);
    CHECK_EQ(s.fb[at(102, 50)], 0x03);  // nothing replaced there
    // The machine's buffer is not touched.
    CHECK_EQ(m->mars.fb[m->mars.fb_display][at(101, 50)], 4);
    CHECK_EQ(m->sprites->pictures_composed, 1u);

    // And that is what the output shows (the 32X pixel over the backdrop).
    m->vdp.reg[12] = 1;
    m->run_frame();
    auto rgb = [](uint16_t c) {
        uint32_t r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
        return 0xFF000000u | ((r << 3 | r >> 2) << 16) | ((g << 3 | g >> 2) << 8) | (b << 3 | b >> 2);
    };
    CHECK_EQ(m->framebuffer[50 * kScreenWidth + 98], rgb(m->mars.pal[0x31]));
    CHECK_EQ(m->framebuffer[50 * kScreenWidth + 101], rgb(m->mars.pal[0x03]));
    CHECK_EQ(m->framebuffer[51 * kScreenWidth + 101], rgb(m->mars.pal[0x55]));
}

TEST(sprite_replace, nothing_is_composed_without_a_replaced_graphic_or_before_tracking) {
    auto m = machine();
    m->sprites = std::make_shared<SpriteRecorder>();
    auto pack = std::make_shared<SpritePack>();
    pack->add(0x22, 0, image_of(small()));  // another graphic
    m->sprites->set_pack(pack);
    flip(*m);
    blit(*m, 100, 50);
    flip(*m);
    CHECK(!shown(*m).composed);
    CHECK_EQ(m->sprites->pictures_composed, 0u);
    // A recorder put on while a buffer was being drawn waits for its next
    // start: what was drawn before cannot be composed again.
    m->sprites = std::make_shared<SpriteRecorder>();
    m->sprites->set_pack(pack_of(image_of(small())));
    blit(*m, 100, 50);
    flip(*m);
    CHECK(!shown(*m).composed);
    blit(*m, 100, 50);
    flip(*m);
    CHECK(shown(*m).composed);
    // Forgotten (the machine's state replaced), it waits again.
    m->sprites->reset();
    CHECK(!shown(*m).composed);
    // An empty pack is no pack: nothing is tracked.
    m->sprites->set_pack(std::make_shared<SpritePack>());
    CHECK(!m->sprites->tracks_writes());
}

TEST(sprite_replace, the_widescreen_margins_are_composed_in_the_shadow) {
    auto m = machine();
    m->sprites = std::make_shared<SpriteRecorder>();
    SpriteImage img = image_of(small());
    for (uint8_t& v : img.px) if (v) v = uint8_t(v + 0x40);
    m->sprites->set_pack(pack_of(img));
    flip(*m);
    m->wide_active = true;
    m->sdram[0x3834] = 0xFF; m->sdram[0x3835] = 0xC0;  // left -64
    fill_line(*m, 50, 0x0202);  // also clears the line's margins in the shadow
    blit(*m, 1, 50);            // pixels at x -1 and 0..2
    const int b = m->mars.fb_display ^ 1;
    CHECK_EQ(m->fb_margin[b][at(-1, 50)], 1);
    flip(*m);
    const Shown s = shown(*m);
    CHECK(s.composed);
    CHECK_EQ(s.margin[at(-1, 50)], 0x41);
    CHECK_EQ(s.fb[at(0, 50)], 0x42);
    CHECK_EQ(s.fb[at(2, 50)], 0x44);
    CHECK_EQ(s.margin[at(-2, 50)], 0x02);  // the cleared margin
    CHECK_EQ(m->fb_margin[m->mars.fb_display][at(-1, 50)], 1);
}

TEST(sprite_replace, each_graphic_drawn_is_dumped_once_with_its_colours) {
    auto m = machine();
    m->sprites = std::make_shared<SpriteRecorder>();
    m->sprites->dumping = true;
    sh2::State c{};
    c.r[1] = 100; c.r[2] = 50; c.r[3] = 4; c.r[4] = 0x06010200;
    m->sprites->hook(*m, c, patches::kSh2Blit);
    m->sprites->hook(*m, c, patches::kSh2Blit);
    flip(*m);
    CHECK_EQ(m->sprites->pending.size(), size_t(1));
    const SpriteDump& d = m->sprites->pending.at(0);
    CHECK_EQ(d.bank, 0x21);
    CHECK_EQ(d.frame, 1);
    CHECK_EQ(d.image.width, 4);
    CHECK_EQ(d.image.height, 3);
    CHECK(d.image.px == image_of(small()).px);
    uint32_t pal[256];
    SpriteDraw drawn = call(100, 50, 4);
    sprite_dump_palette(drawn, m->mars.pal, pal);
    CHECK(std::equal(pal, pal + 256, d.palette.begin()));
    CHECK(sprite_file_name(d.bank, d.frame, d.hash) == std::string("32x_21_01.png"));
    m->sprites->pending.clear();
    m->sprites->hook(*m, c, patches::kSh2Blit);
    flip(*m);
    flip(*m);
    CHECK(m->sprites->pending.empty());
}

TEST(sprite_pack, files_are_named_and_loaded_as_the_dumps_are) {
    CHECK(sprite_file_name(2, 0x51, 0) == std::string("32x_02_51.png"));
    CHECK(sprite_file_name(-1, -1, 0x0123456789abcdefull) == std::string("32x_0123456789abcdef.png"));
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "chaotix_test_sprite_pack";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const std::string d = dir.string();
    const uint8_t px[6] = {0, 1, 2, 3, 0x40, 0};
    const uint32_t pal[256] = {};
    CHECK(write_png_indexed(d + "/32x_02_51.png", px, 3, 2, pal, 256));
    CHECK(write_png_indexed(d + "/32x_0123456789abcdef.png", px, 2, 3, pal, 256));
    CHECK(write_png_indexed(d + "/32x_80_00.png", px, 3, 2, pal, 256));      // no such bank
    CHECK(write_png_indexed(d + "/32x_zz_00.png", px, 3, 2, pal, 256));      // not hex
    CHECK(write_png_indexed(d + "/32x_badname.png", px, 3, 2, pal, 256));
    CHECK(write_png_indexed(d + "/md_0000000000000000.png", px, 3, 2, pal, 256));  // a tile's: not ours
    const uint32_t rgb[6] = {};
    CHECK(write_png(d + "/32x_03_00.png", rgb, 3, 2, 3));                       // full colour
    if (FILE* f = std::fopen((d + "/32x_04_00.png").c_str(), "wb")) {
        std::fputs("\x89PNG\r\n\x1a\n not really", f);
        std::fclose(f);
    }
    SpritePack pack;
    std::vector<std::string> messages;
    CHECK_EQ(load_sprite_pack(pack, d, &messages), 2);
    CHECK_EQ(pack.size(), size_t(2));
    CHECK_EQ(messages.size(), size_t(5));
    bool full_colour = false;
    for (const auto& msg : messages) full_colour |= msg.find("32x_03_00.png: must be a palette-indexed PNG") != std::string::npos;
    CHECK(full_colour);
    SpriteDraw a = call(0, 0);
    a.bank = 2;
    a.frame = 0x51;
    const SpriteImage* img = pack.find(a);
    CHECK(img && img->width == 3 && img->height == 2 && img->px[4] == 0x40);
    SpriteDraw h = call(0, 0);
    h.hash = 0x0123456789abcdefull;
    CHECK(pack.find(h) && pack.find(h)->width == 2);
    a.bank = 0x22;  // not in the pack by its slot: by its hash, then
    a.hash = h.hash;
    CHECK(pack.find(a) == pack.find(h));
    fs::remove_all(dir, ec);
}
