// Whole-bank sprite sheets: banks read from a ROM image, the layout and its
// description, the round trip from graphics to a sheet and back, cells that
// grow, the files and their errors; on a hand-built ROM image.
#include "renderer/image_io.h"
#include "renderer/sprite_pack.h"
#include "renderer/sprite_sheet.h"
#include "runtime/sprite_record.h"
#include "runtime/sprite_replace.h"
#include "test_framework.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <initializer_list>
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

void put32(std::vector<uint8_t>& rom, uint32_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) rom[at + uint32_t(i)] = uint8_t(v >> (24 - 8 * i));
}

// A 4 MB ROM image whose bank table (0x0A0000) has bank 2: a graphic, an
// empty slot, a second graphic, the first again; then bank 3 (one graphic).
// Bank 2's colours (the 68K pointer at 0x0018E4 + 8) start at ROM 0x200000.
const std::vector<uint8_t> g0 = graphic(-4, 4, -3, 0, {{-4, 0, -3, {1, 2, 3, 4}}, {-2, 4, 0, {5, 6, 0, 7, 8, 9}}});
const std::vector<uint8_t> g1 = graphic(0, 6, -10, -5, {{0, 6, -10, {1, 1, 1, 1, 1, 1}}, {2, 4, -5, {18, 17}}});
std::vector<uint8_t> test_rom() {
    std::vector<uint8_t> rom(0x400000);
    const uint32_t t2 = 0x100000, t3 = 0x100100;
    put32(rom, 0x0A0000 + 4 * 2, 0x02000000 + t2);
    put32(rom, 0x0A0000 + 4 * 3, 0x02000000 + t3);
    const uint32_t a0 = t2 + 0x20, a1 = t2 + 0x60;
    put32(rom, t2, 0x02000000 + a0);
    put32(rom, t2 + 4, 0xFFFFFFFFu);
    put32(rom, t2 + 8, 0x02000000 + a1);
    put32(rom, t2 + 12, 0x02000000 + a0);
    std::copy(g0.begin(), g0.end(), rom.begin() + a0);
    std::copy(g1.begin(), g1.end(), rom.begin() + a1);
    put32(rom, t3, 0x02000000 + t3 + 0x10);
    std::copy(g1.begin(), g1.end(), rom.begin() + t3 + 0x10);
    put32(rom, 0x0018E4 + 4 * 2, 0x900000);
    for (int p = 0; p < 18; ++p) {
        rom[0x200000 + 2 * p] = uint8_t(p);   // 32X colours: blue, green, red from the top
        rom[0x200000 + 2 * p + 1] = 0x1F;     // red 31
    }
    return rom;
}

SpriteImage image_of(const std::vector<uint8_t>& g) {
    SpriteImage i;
    graphic_image(g.data(), uint32_t(g.size()), i);
    return i;
}

bool same_image(const SpriteImage& a, const SpriteImage& b) {
    return a.width == b.width && a.height == b.height && a.px == b.px;
}

std::string error_of(const std::string& text) {
    SheetLayout l;
    std::string why;
    return parse_sheet_sidecar(text, l, &why) ? std::string() : why;
}

const char* kGood =
    "chaotix-sprite-sheet 1\n"
    "bank 02\n"
    "size 23 12\n"
    "cell 10 10\n"
    "hotspot 4 6\n"
    "frame 00 1 1 10 10 -4 4 -3 0\n"
    "frame 01 12 1 10 10 empty\n";

} // namespace

TEST(sprite_sheet, a_bank_is_read_from_the_rom_with_its_empty_and_repeated_slots) {
    const std::vector<uint8_t> rom = test_rom();
    SpriteBank b;
    std::string why;
    CHECK(read_rom_bank(rom, 2, b, &why));
    CHECK_EQ(b.frames.size(), size_t(4));
    CHECK(b.frames[0].kind == BankFrame::Graphic);
    CHECK(b.frames[1].kind == BankFrame::Empty);
    CHECK(b.frames[2].kind == BankFrame::Graphic);
    CHECK(b.frames[3].kind == BankFrame::Same && b.frames[3].same == 0);
    CHECK(same_image(b.frames[0].image, image_of(g0)));
    CHECK(b.frames[2].x0 == 0 && b.frames[2].x1 == 6 && b.frames[2].y0 == -10 && b.frames[2].y1 == -5);
    // The character's colours for pixels 1-18; 0 transparent.
    CHECK_EQ(b.palette[0], 0u);
    CHECK_EQ(b.palette[1], 0xFFFF0000u);
    CHECK_EQ(b.palette[kSheetGrid], 0xFFFF00FFu);
    // Bank 3 ends where nothing more is a graphic.
    CHECK(read_rom_bank(rom, 3, b, &why));
    CHECK_EQ(b.frames.size(), size_t(1));
    CHECK(!read_rom_bank(rom, 4, b, &why));   // no table
    CHECK(!read_rom_bank(rom, 20, b, &why));  // not a character bank
}

TEST(sprite_sheet, a_sheet_gives_back_every_graphic_it_was_made_from) {
    const std::vector<uint8_t> rom = test_rom();
    SpriteBank b;
    CHECK(read_rom_bank(rom, 2, b, nullptr));
    const SheetLayout l = sheet_layout(b);
    // One cell size, the union of the boxes around the hotspot.
    CHECK_EQ(l.cell_w, 10);
    CHECK_EQ(l.cell_h, 11);
    CHECK_EQ(l.hot_x, 4);
    CHECK_EQ(l.hot_y, 10);
    CHECK_EQ(l.width, 4 * 11 + 1);
    CHECK_EQ(l.height, 11 + 2);
    const std::vector<uint8_t> px = sheet_pixels(b, l);
    // The grid, the room outside a box, the hotspot at one place in each cell.
    CHECK_EQ(px[0], kSheetGrid);
    CHECK_EQ(px[size_t(1 * l.width + 1)], kSheetOutside);
    CHECK_EQ(px[size_t((1 + 10 - 3) * l.width + 1 + 4 - 4)], 1);   // g0's first pixel at (-4, -3)
    CHECK_EQ(px[size_t((1 + 10 - 10) * l.width + 23 + 4)], 1);     // g1's at (0, -10)
    CHECK_EQ(px[size_t(5 * l.width + 12)], kSheetGrid);             // the empty slot

    // Through its description and back.
    SheetLayout back;
    std::string why;
    CHECK(parse_sheet_sidecar(sheet_sidecar(l), back, &why));
    CHECK_EQ(back.bank, 2);
    CHECK(back.width == l.width && back.height == l.height && back.hot_x == l.hot_x && back.hot_y == l.hot_y);
    CHECK_EQ(back.cells.size(), l.cells.size());
    for (size_t i = 0; i < l.cells.size() && i < back.cells.size(); ++i) {
        const SheetCell &a = l.cells[i], &c = back.cells[i];
        CHECK(a.frame == c.frame && a.kind == c.kind && a.same == c.same && a.x == c.x && a.y == c.y && a.w == c.w &&
              a.h == c.h && a.x0 == c.x0 && a.x1 == c.x1 && a.y0 == c.y0 && a.y1 == c.y1);
    }
    // Unchanged cells are the originals; nothing for the empty and repeated slots.
    std::vector<SheetReplacement> reps;
    std::vector<std::string> warnings;
    CHECK(sheet_replacements(back, px.data(), l.width, l.height, reps, &warnings, &why));
    CHECK_EQ(reps.size(), size_t(2));
    CHECK(warnings.empty());
    if (reps.size() == 2) {
        CHECK(reps[0].frame == 0 && same_image(reps[0].image, image_of(g0)));
        CHECK(reps[1].frame == 2 && same_image(reps[1].image, image_of(g1)));
        // As the original's replacement, it draws exactly the original.
        std::vector<uint8_t> out;
        CHECK(replacement_graphic(g0.data(), uint32_t(g0.size()), reps[0].image, out, &why));
        CHECK(out == g0);
    }
    CHECK(!sheet_replacements(back, px.data(), l.width - 1, l.height, reps, &warnings, &why));
}

TEST(sprite_sheet, a_cell_grows_right_and_down_and_not_left_or_up) {
    const std::vector<uint8_t> rom = test_rom();
    SpriteBank b;
    CHECK(read_rom_bank(rom, 2, b, nullptr));
    const SheetLayout l = sheet_layout(b);
    std::vector<uint8_t> px = sheet_pixels(b, l);
    // Frame 0's box is columns 1..8 and lines 8..11 of its cell (1..10,
    // 1..11): paint two columns to its right, and a pixel above it. Frame
    // 2's box is columns 27..32 and lines 1..6: paint the cell's last line.
    px[size_t(9 * l.width + 10)] = 9;
    px[size_t(3 * l.width + 3)] = 9;
    px[size_t(11 * l.width + 27)] = 9;
    std::vector<SheetReplacement> reps;
    std::vector<std::string> warnings;
    std::string why;
    CHECK(sheet_replacements(l, px.data(), l.width, l.height, reps, &warnings, &why));
    CHECK_EQ(reps.size(), size_t(2));
    if (reps.size() != 2) return;
    CHECK_EQ(reps[0].image.width, 10);
    CHECK_EQ(reps[0].image.height, 4);
    CHECK_EQ(reps[0].image.px[size_t(1 * 10 + 9)], 9);
    CHECK_EQ(reps[0].image.px[size_t(1 * 10 + 8)], 0);  // the room outside the box is transparent
    CHECK_EQ(reps[1].image.width, 6);
    CHECK_EQ(reps[1].image.height, 11);
    CHECK_EQ(reps[1].image.px[size_t(10 * 6)], 9);
    CHECK_EQ(warnings.size(), size_t(1));
    // Grid and room indices inside the box are transparent too.
    px = sheet_pixels(b, l);
    px[size_t(8 * l.width + 1)] = kSheetGrid;
    CHECK(sheet_replacements(l, px.data(), l.width, l.height, reps, &warnings, &why));
    CHECK_EQ(reps[0].image.px[0], 0);
    CHECK_EQ(reps[0].image.width, 8);
}

TEST(sprite_sheet, malformed_descriptions_are_refused) {
    CHECK(error_of(kGood).empty());
    CHECK(!error_of("").empty());
    CHECK(!error_of(std::string("chaotix-sprite-sheet 2\n") + (kGood + 23)).empty());
    CHECK(error_of(std::string("# a comment\n\n") + kGood + "  # trailing\r\n").empty());
    auto with = [](const std::string& extra) { return error_of(std::string(kGood) + extra); };
    CHECK(with("colour 3\n").find("unknown entry 'colour'") != std::string::npos);
    CHECK(with("frame 00 1 1 10 10 empty\n").find("described twice") != std::string::npos);
    CHECK(!with("frame 02 1 1 10 10 -4\n").empty());                 // a box of one number
    CHECK(!with("frame 2 1 1 10 10 empty\n").empty());               // frames are two hex digits
    CHECK(!with("frame 02 1 1 10 10 4 -4 0 0\n").empty());           // an empty box
    CHECK(!with("frame 02 20 1 10 10 empty\n").empty());             // outside the sheet
    CHECK(!with("frame 02 1 1 10 10 -5 4 -3 0\n").empty());          // the box out of its cell
    CHECK(!with("frame 02 1 1 10 10 -4 4 -3 4\n").empty());          // ... at the bottom
    CHECK(!with("frame 02 12 1 10 10 same 01\n").empty());           // repeats an empty slot
    CHECK(with("frame 02 12 1 10 10 same 00\n").empty());
    CHECK(!with("bank 80\n").empty());
    CHECK(!with("size 99999999 1\n").empty());
    CHECK(!with("hotspot -1 0\n").empty());
    CHECK(!error_of("chaotix-sprite-sheet 1\nsize 23 12\ncell 10 10\nhotspot 4 6\n").empty());  // no bank
    CHECK(!error_of("chaotix-sprite-sheet 1\nbank 02\ncell 10 10\nhotspot 4 6\n").empty());     // no size
    CHECK(error_of(std::string(kGood) + "frame 02 12 1 10 10 -4 4 -3 x\n").find("line 8") != std::string::npos);
}

TEST(sprite_sheet, sheets_load_as_a_pack_and_single_files_override_their_cells) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "chaotix_test_sprite_sheet";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const std::string d = dir.string();
    const std::vector<uint8_t> rom = test_rom();
    SpriteBank b;
    CHECK(read_rom_bank(rom, 2, b, nullptr));
    const SheetLayout l = sheet_layout(b);
    const std::vector<uint8_t> px = sheet_pixels(b, l);
    auto write_text = [&d](const std::string& name, const std::string& text) {
        if (FILE* f = std::fopen((d + "/" + name).c_str(), "wb")) {
            std::fputs(text.c_str(), f);
            std::fclose(f);
        }
    };
    CHECK(write_png_indexed(d + "/32x_02.png", px.data(), l.width, l.height, b.palette.data(), 256));
    write_text("32x_02.txt", sheet_sidecar(l));
    // A single file for frame 2 wins over its cell.
    const uint8_t one[4] = {3, 3, 3, 3};
    CHECK(write_png_indexed(d + "/32x_02_02.png", one, 2, 2, b.palette.data(), 256));
    // Refused: no description; a description of another bank; full colour;
    // the wrong size; not a sheet's name.
    CHECK(write_png_indexed(d + "/32x_05.png", px.data(), l.width, l.height, b.palette.data(), 256));
    CHECK(write_png_indexed(d + "/32x_06.png", px.data(), l.width, l.height, b.palette.data(), 256));
    write_text("32x_06.txt", sheet_sidecar(l));
    const std::vector<uint32_t> rgb(size_t(l.width) * size_t(l.height));
    CHECK(write_png(d + "/32x_07.png", rgb.data(), l.width, l.height, l.width));
    SheetLayout seven = l;
    seven.bank = 7;
    write_text("32x_07.txt", sheet_sidecar(seven));
    CHECK(write_png_indexed(d + "/32x_08.png", px.data(), l.width - 2, l.height, b.palette.data(), 256));
    SheetLayout eight = l;
    eight.bank = 8;
    write_text("32x_08.txt", sheet_sidecar(eight));
    CHECK(write_png_indexed(d + "/32x_zz.png", px.data(), l.width, l.height, b.palette.data(), 256));

    SpritePack pack;
    std::vector<std::string> messages;
    CHECK_EQ(load_sprite_pack(pack, d, &messages), 2);
    CHECK_EQ(messages.size(), size_t(5));
    auto said = [&messages](const char* what) {
        for (const auto& m : messages)
            if (m.find(what) != std::string::npos) return true;
        return false;
    };
    CHECK(said("32x_05.png: a sheet needs its description"));
    CHECK(said("32x_06.txt: describes bank 02, not 06"));
    CHECK(said("32x_07.png: must be a palette-indexed PNG"));
    CHECK(said("32x_08.png: the image is"));
    CHECK(said("32x_zz.png: not a sheet's name"));
    SpriteDraw draw;
    draw.bank = 2;
    draw.frame = 0;
    CHECK(pack.find(draw) && same_image(*pack.find(draw), image_of(g0)));
    draw.frame = 2;
    CHECK(pack.find(draw) && pack.find(draw)->width == 2);
    draw.frame = 1;
    CHECK(pack.find(draw) == nullptr);
    draw.frame = 3;  // the repeat is named by frame 0, never by itself
    CHECK(pack.find(draw) == nullptr);
    fs::remove_all(dir, ec);
}
