// Character mods (runtime/character_mods.h) on a synthetic ROM that has the
// game's tables where the game has them: the colour pointers at $8818E4, the
// movement table at $88BE58, Espio's cycle sets at $88E7BA and the names at
// $8F7254. No real ROM is needed.
#include "runtime/character_mods.h"
#include "runtime/rom.h"
#include "runtime/stage_select.h"
#include "test_framework.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace chaotix;

namespace {

constexpr int kKnuckles = 2, kVector = 4, kEspio = 7;

void put16(std::vector<uint8_t>& v, uint32_t off, uint32_t x) { v[off] = uint8_t(x >> 8); v[off + 1] = uint8_t(x); }
void put32(std::vector<uint8_t>& v, uint32_t off, uint32_t x) { put16(v, off, x >> 16); put16(v, off + 2, x); }

// Character k's colours: 18 distinct ones, bright enough to fade.
uint16_t rom_colour(int k, int i) { return uint16_t((10 + i % 8) | (12 + k) << 5 | (20 + i / 8) << 10); }

std::vector<uint8_t> synthetic_rom_bytes() {
    std::vector<uint8_t> v(0x240000, 0);
    std::memcpy(v.data() + 0x100, "SEGA 32X", 8);
    for (int k = 0; k < 8; ++k) {
        // Colours in the banked window ($9xxxxx = ROM 0x2xxxxx), as the game's.
        const uint32_t colours = 0x230000 + 0x40 * uint32_t(k);
        put32(v, 0x18E4 + 4 * uint32_t(k), colours + 0x700000);
        for (int i = 0; i < 18; ++i) put16(v, colours + 2 * uint32_t(i), rom_colour(k, i));
        // Movement records, 0x4A bytes each.
        const uint32_t rec = 0xBE78 + 0x4A * uint32_t(k);
        put32(v, 0xBE58 + 4 * uint32_t(k), rec + 0x880000);
        put16(v, rec + 0x06, 0x0A + uint32_t(k));   // acceleration
        put32(v, rec + 0x0A, 0x3800);               // gravity
        put16(v, rec + 0x16, 0x0600);               // jump
    }
    // Espio's cycle sets: set 0 is his own pixels 2-6, as in the game.
    for (int s = 0; s < 16; ++s)
        for (int j = 0; j < 5; ++j)
            put16(v, 0xE7BA + 2 * uint32_t(s * 5 + j), s ? uint16_t(0x0400 * uint32_t(j + 1) + uint32_t(s)) : rom_colour(kEspio, 1 + j));
    // The names: one word per character, the offset from the word to its text.
    const char* names[] = {"MIGHTY    ", "**********", "KNUCKLES  ", "CHARMY BEE", "VECTOR    ", "BOMB      ", "HEAVY     ", "ESPIO     "};
    uint32_t text = 0x774E8;
    for (int k = 0; k < 8; ++k) {
        put16(v, 0x77254 + 2 * uint32_t(k), uint16_t(text - (0x77254 + 2 * uint32_t(k))));
        std::memcpy(v.data() + text, names[k], std::strlen(names[k]) + 1);
        text += uint32_t(std::strlen(names[k]) + 1);
    }
    // A correct header checksum, so the patch bookkeeping keeps it correct.
    uint16_t sum = 0;
    for (size_t i = 0x200; i + 1 < v.size(); i += 2) sum = uint16_t(sum + (v[i] << 8 | v[i + 1]));
    put16(v, 0x18E, sum);
    return v;
}

Rom synthetic_rom() {
    const std::vector<uint8_t> v = synthetic_rom_bytes();
    Rom rom;
    std::string err;
    CHECK(rom.load_memory(v.data(), v.size(), &err));
    return rom;
}

CharacterMod parsed(int id, const std::string& text, std::vector<std::string>* messages = nullptr) {
    CharacterMod m;
    m.id = id;
    parse_character_mod(text, m, "test.ini", messages);
    return m;
}

// CRAM as the game leaves it: character `k`'s colours at entry base+1, and
// the copies at +0x80 with the priority bit flipped.
void load_block(uint16_t* cram, int k, int base, bool priority) {
    for (int i = 0; i < 18; ++i) {
        const uint16_t c = uint16_t(rom_colour(k, i) | (priority ? 0x8000 : 0));
        cram[base + 1 + i] = c;
        cram[base + 0x81 + i] = uint16_t(c ^ 0x8000);
    }
}

uint16_t fade_out(uint16_t c, int n) {
    auto f = [n](int v) { return v > n ? v - n : 0; };
    return uint16_t(f(c & 31) | f((c >> 5) & 31) << 5 | f((c >> 10) & 31) << 10);
}

} // namespace

TEST(character_mods, colours_round_to_the_nearest_32x_level) {
    CHECK_EQ(characters::colour_from_rgb(0xFFFFFF), 0x7FFF);
    CHECK_EQ(characters::colour_from_rgb(0x000000), 0x0000);
    CHECK_EQ(characters::colour_from_rgb(0xFF0000), 0x001F);   // red is the low bits
    CHECK_EQ(characters::colour_from_rgb(0x0000FF), 0x7C00);
    // Every level the compositor shows, (v << 3) | (v >> 2), comes back as itself.
    for (uint32_t v = 0; v < 32; ++v) {
        const uint32_t e = (v << 3) | (v >> 2);
        CHECK_EQ(characters::colour_from_rgb(e << 16 | e << 8 | e), uint16_t(v | v << 5 | v << 10));
    }
    CHECK_EQ(characters::colour_from_rgb(0x040000), 0x0000);   // 4/255 is nearer 0 than 1/31
    CHECK_EQ(characters::colour_from_rgb(0x050000), 0x0001);
}

TEST(character_mods, ini_files_give_colours_cycle_sets_and_stats) {
    std::vector<std::string> messages;
    const CharacterMod m = parsed(kEspio,
        "; a comment\n"
        "# also a comment\n"
        "[Palette]\n"
        "1 = #FF0000\n"
        " 18 = 0x7C00 ; inline comment\n"
        "3 = $0123\n"
        "[Cycle]\n"
        "15 = #000000 #FFFFFF 0x0001 $0002 #0000FF\n"
        "[Stats]\n"
        "Acceleration = 0x18\n"
        "gravity = 14336\n"
        "jump = $700\n",
        &messages);
    CHECK_EQ(messages.size(), size_t(0));
    CHECK(m.palette_set[0] && m.palette_set[2] && m.palette_set[17]);
    CHECK(!m.palette_set[1]);
    CHECK_EQ(m.palette[0], 0x001F);
    CHECK_EQ(m.palette[2], 0x0123);
    CHECK_EQ(m.palette[17], 0x7C00);
    CHECK(m.cycle_set[15] && !m.cycle_set[0]);
    CHECK_EQ(m.cycle[15][1], 0x7FFF);
    CHECK_EQ(m.cycle[15][2], 0x0001);
    CHECK_EQ(m.cycle[15][4], 0x7C00);
    CHECK_EQ(m.stats.size(), size_t(3));
    CHECK_STR(m.stats[0].first->name, "acceleration");
    CHECK_EQ(m.stats[0].second, 0x18u);
    CHECK_EQ(m.stats[1].first->size, 4);
    CHECK_EQ(m.stats[1].second, 0x3800u);
    CHECK_EQ(m.stats[2].second, 0x700u);
    CHECK(m.has_palette());
}

TEST(character_mods, lines_it_cannot_use_are_said_and_skipped) {
    std::vector<std::string> messages;
    const CharacterMod m = parsed(kKnuckles,
        "1 = #FFFFFF\n"             // outside any section
        "[Palette]\n"
        "0 = #FFFFFF\n"             // pixel 0 is transparent, not a colour
        "19 = #FFFFFF\n"
        "2 = red\n"
        "3 = 0x8000\n"              // the priority bit is the game's
        "4 = #FFF\n"
        "[Cycle]\n"
        "0 = #000000 #000000\n"     // five colours
        "16 = #000000 #000000 #000000 #000000 #000000\n"
        "[Stats]\n"
        "speed = 3\n"
        "acceleration = 0x10000\n"  // a word
        "abilities_a = 0x100\n"     // a byte
        "jump = 12abc\n"
        "[Nope]\n"
        "x = 1\n"
        "[Stats\n",
        &messages);
    CHECK_EQ(messages.size(), size_t(14));
    CHECK(!m.has_palette());
    CHECK(m.stats.empty());
    CHECK(messages[0].find("test.ini:1:") == 0);
}

TEST(character_mods, the_files_are_found_by_character_name) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "chaotix_character_mods_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    auto write = [&](const char* name, const char* text) { std::ofstream(dir / name) << text; };
    write("Espio.INI", "[Palette]\n1 = #FFFFFF\n");
    write("knuckles.ini", "[Stats]\njump = 0x700\n");
    write("slot1.ini", "[Stats]\njump = 0x700\n");
    write("sonic.ini", "[Stats]\njump = 0x700\n");
    write("README.txt", "not a mod");
    std::vector<std::string> messages;
    const std::vector<CharacterMod> mods = load_character_mods(dir.u8string(), &messages);
    CHECK_EQ(mods.size(), size_t(2));
    if (mods.size() == 2) {
        CHECK_EQ(mods[0].id, kKnuckles);
        CHECK_EQ(mods[1].id, kEspio);
    }
    CHECK_EQ(messages.size(), size_t(2));  // slot1 and sonic
    CHECK(load_character_mods((dir / "missing").u8string(), &messages).empty());
    fs::remove_all(dir);
}

TEST(character_mods, fades_are_recognised_and_followed) {
    uint16_t orig[3] = {0x7FFF, 0x1234, 0x2529};
    uint16_t shown[3];
    Fade f;
    // The same colours.
    CHECK(match_fade(orig, orig, 3, &f));
    CHECK_EQ(int(f.kind), int(Fade::None));
    // Fade-out: every component 5 lower, none below 0.
    for (int i = 0; i < 3; ++i) shown[i] = fade_out(orig[i], 5);
    CHECK(match_fade(shown, orig, 3, &f));
    CHECK_EQ(int(f.kind), int(Fade::Out));
    CHECK_EQ(f.n, 5);
    CHECK_EQ(apply_fade(0x7FFF, f), uint16_t(26 | 26 << 5 | 26 << 10));
    CHECK_EQ(apply_fade(0x0421, f), 0x0000);
    // All black is the end of any fade, and shows black.
    shown[0] = shown[1] = shown[2] = 0;
    CHECK(match_fade(shown, orig, 3, &f));
    CHECK_EQ(apply_fade(0x7FFF, f), 0x0000);
    // Fade-in after 4 steps: min(4, target).
    auto fade_in = [](uint16_t c, int n) {
        auto g = [n](int v) { return v < n ? v : n; };
        return uint16_t(g(c & 31) | g((c >> 5) & 31) << 5 | g((c >> 10) & 31) << 10);
    };
    for (int i = 0; i < 3; ++i) shown[i] = fade_in(orig[i], 4);
    CHECK(match_fade(shown, orig, 3, &f));
    CHECK_EQ(int(f.kind), int(Fade::In));
    CHECK_EQ(f.n, 4);
    CHECK_EQ(apply_fade(0x7FFF, f), uint16_t(4 | 4 << 5 | 4 << 10));
    CHECK_EQ(apply_fade(0x0421, f), 0x0421);  // already at most 4
    // Not a fade: brighter than the original, or not uniform.
    shown[0] = 0x7FFF; shown[1] = 0x1235; shown[2] = 0x0421;
    CHECK(!match_fade(shown, orig, 3, &f));
    for (int i = 0; i < 3; ++i) shown[i] = fade_out(orig[i], 5);
    shown[2] = fade_out(orig[2], 1);
    CHECK(!match_fade(shown, orig, 3, &f));
}

TEST(character_mods, the_palette_shown_has_the_characters_colours) {
    const Rom rom = synthetic_rom();
    std::string text = "[Palette]\n";
    for (int i = 1; i <= 18; ++i) {
        char line[32];
        std::snprintf(line, sizeof line, "%d = 0x%04X\n", i, 0x1000 + i);
        text += line;
    }
    std::vector<CharacterMod> mods = {parsed(kKnuckles, text)};
    CharacterPalettes pal(rom, mods);
    CHECK(!pal.empty());
    uint16_t cram[256];
    for (int i = 0; i < 256; ++i) cram[i] = uint16_t(0x2000 + i);
    uint8_t offsets[16] = {};
    // Not on screen: the emulated palette itself.
    CHECK(pal.display(cram, offsets) == cram);
    // Knuckles as the player, Vector as the partner.
    load_block(cram, kKnuckles, 0x12, true);
    load_block(cram, kVector, 0x24, false);
    offsets[kKnuckles] = offsets[kKnuckles + 8] = 0x12;
    offsets[kVector] = offsets[kVector + 8] = 0x24;
    const uint16_t* shown = pal.display(cram, offsets);
    CHECK(shown != cram);
    CHECK_EQ(pal.substituted(), 1);
    for (int i = 0; i < 18; ++i) {
        // Each entry keeps its own priority bit from CRAM.
        CHECK_EQ(shown[0x13 + i], uint16_t(0x8000 | (0x1000 + 1 + i)));
        CHECK_EQ(shown[0x93 + i], uint16_t(0x1000 + 1 + i));
        CHECK_EQ(shown[0x25 + i], cram[0x25 + i]);   // Vector is not modded
    }
    CHECK_EQ(shown[0x12], cram[0x12]);
    CHECK_EQ(shown[0x37], cram[0x37]);
    CHECK_EQ(shown[0x01], cram[0x01]);
    // Asked again with nothing changed, nothing is worked out again.
    const uint64_t n = pal.recomputed();
    CHECK(pal.display(cram, offsets) == shown);
    CHECK_EQ(pal.recomputed(), n);
    // Swapped: the partner now, from the other block.
    load_block(cram, kVector, 0x12, true);
    load_block(cram, kKnuckles, 0x24, false);
    offsets[kKnuckles] = 0x24;
    offsets[kVector] = 0x12;
    shown = pal.display(cram, offsets);
    CHECK_EQ(shown[0x25], uint16_t(0x1000 + 1));
    CHECK_EQ(shown[0xA5], uint16_t(0x8000 | (0x1000 + 1)));
    CHECK_EQ(shown[0x13], cram[0x13]);
    // A fade-out step: the mod's colours, faded the same way.
    for (int i = 0; i < 18; ++i) {
        cram[0x25 + i] = fade_out(cram[0x25 + i], 7);
        cram[0xA5 + i] = uint16_t(cram[0x25 + i] | 0x8000);
    }
    shown = pal.display(cram, offsets);
    for (int i = 0; i < 18; ++i) CHECK_EQ(shown[0x25 + i], fade_out(uint16_t(0x1000 + 1 + i), 7));
    // Another scene's colours where the stale offset says Knuckles: as they are.
    cram[0x26] = 0x7FFF;
    CHECK(pal.display(cram, offsets) == cram);
    CHECK(pal.refused() > 0);
    // A copy at +0x80 the game no longer keeps in step is left alone.
    load_block(cram, kKnuckles, 0x24, false);
    cram[0xA5] = 0x0123;
    shown = pal.display(cram, offsets);
    CHECK_EQ(shown[0x25], uint16_t(0x1000 + 1));
    CHECK_EQ(shown[0xA5], 0x0123);
}

TEST(character_mods, the_same_character_twice_is_found_by_its_colours) {
    const Rom rom = synthetic_rom();
    std::vector<CharacterMod> mods = {parsed(kKnuckles, "[Palette]\n1 = 0x0001\n")};
    CharacterPalettes pal(rom, mods);
    uint16_t cram[256] = {};
    uint8_t offsets[16] = {};
    load_block(cram, kKnuckles, 0x12, false);
    load_block(cram, kKnuckles, 0x24, false);
    offsets[kKnuckles] = 0x24;   // one byte, so only one of the blocks
    const uint16_t* shown = pal.display(cram, offsets);
    CHECK_EQ(pal.substituted(), 2);
    CHECK_EQ(shown[0x13], 0x0001);
    CHECK_EQ(shown[0x25], 0x0001);
    // Colours the file does not give stay the ROM's.
    CHECK_EQ(shown[0x14], rom_colour(kKnuckles, 1));
}

TEST(character_mods, espios_cycle_sets_are_followed) {
    const Rom rom = synthetic_rom();
    std::vector<CharacterMod> mods = {
        parsed(kEspio, "[Palette]\n1 = 0x0011\n3 = 0x0033\n[Cycle]\n5 = 0x0051 0x0052 0x0053 0x0054 0x0055\n")};
    CharacterPalettes pal(rom, mods);
    uint16_t cram[256] = {};
    uint8_t offsets[16] = {};
    offsets[kEspio] = 0x12;
    load_block(cram, kEspio, 0x12, false);
    auto set = [&](int s) {
        for (int j = 0; j < 5; ++j) {
            const uint16_t c = rom.read16(0xE7BA + 2 * uint32_t(s * 5 + j));
            cram[0x14 + j] = c;
            cram[0x94 + j] = uint16_t(c | 0x8000);
        }
    };
    // The cycle's set 5: the file's set 5.
    set(5);
    const uint16_t* shown = pal.display(cram, offsets);
    CHECK_EQ(pal.substituted(), 1);
    CHECK_EQ(shown[0x13], 0x0011);
    for (int j = 0; j < 5; ++j) CHECK_EQ(shown[0x14 + j], uint16_t(0x0051 + j));
    // Set 9, which the file does not give: its own colour for pixel 3, Espio's
    // for the rest.
    set(9);
    shown = pal.display(cram, offsets);
    CHECK_EQ(shown[0x15], 0x0033);
    CHECK_EQ(shown[0x14], cram[0x14]);
    CHECK_EQ(shown[0x16], cram[0x16]);
    CHECK_EQ(shown[0x13], 0x0011);
}

TEST(character_mods, stats_are_a_data_patch_of_the_record) {
    Rom rom = synthetic_rom();
    const uint32_t rec = stat_record_offset(rom, kKnuckles);
    CHECK_EQ(rec, uint32_t(0xBE78 + 0x4A * kKnuckles));
    CHECK_EQ(stat_record_offset(rom, 8), 0u);
    std::vector<CharacterMod> mods = {parsed(kKnuckles, "[Stats]\nacceleration = 0x14\ngravity = 0x12345678\nabilities_b = 0xC0\n")};
    const std::vector<RomEdit> edits = stat_edits(rom, mods, nullptr);
    CHECK_EQ(edits.size(), size_t(3));
    if (edits.size() == 3) {
        CHECK_EQ(edits[0].offset, rec + 6);
        CHECK(edits[0].bytes == std::vector<uint8_t>({0x00, 0x14}));
        CHECK_EQ(edits[1].offset, rec + 0x0A);
        CHECK(edits[1].bytes == std::vector<uint8_t>({0x12, 0x34, 0x56, 0x78}));
        CHECK_EQ(edits[2].offset, rec + 2);
        CHECK(edits[2].bytes == std::vector<uint8_t>({0xC0}));
    }
    const std::string sha1 = rom.sha1;
    std::vector<std::string> messages;
    CHECK(rom.apply_edits(edits, &messages) > 0);
    CHECK_EQ(rom.read16(rec + 6), 0x14);
    CHECK_EQ(rom.read32(rec + 0x0A), 0x12345678u);
    CHECK_EQ(rom.data[rec + 2], 0xC0);
    // The patch bookkeeping: the user's ROM's hash and image kept, the changed
    // bytes listed, the header checksum fixed.
    CHECK_STR(rom.sha1, sha1);
    CHECK(!rom.original.empty());
    CHECK_EQ(rom.unpatched()[rec + 6 + 1], 0x0A + kKnuckles);
    CHECK(!rom.patched.empty());
    for (const auto& r : rom.patched) CHECK(r.first >= 0x18E && r.first + r.second <= rec + 0x0E);
    CHECK_EQ(rom.compute_checksum(), rom.read16(0x18E));
    CHECK(!messages.empty());
}

TEST(character_mods, stats_as_the_rom_has_them_change_nothing) {
    Rom rom = synthetic_rom();
    std::vector<CharacterMod> mods = {parsed(kKnuckles, "[Stats]\nacceleration = 0x0C\njump = 0x600\n")};
    CHECK_EQ(rom.apply_edits(stat_edits(rom, mods, nullptr), nullptr), 0);
    CHECK(rom.original.empty());
    CHECK(rom.patched.empty());
    // Past the end of the ROM: said, and skipped.
    std::vector<std::string> messages;
    CHECK_EQ(rom.apply_edits({RomEdit{uint32_t(rom.file_size - 1), {1, 2}}}, &messages), 0);
    CHECK_EQ(messages.size(), size_t(1));
}

TEST(character_mods, stats_stack_on_patch_files) {
    Rom rom = synthetic_rom();
    // An IPS patch renaming Knuckles, then the stats.
    const std::string ips_path = (std::filesystem::temp_directory_path() / "chaotix_character_mods_test.ips").u8string();
    {
        std::ofstream f(ips_path, std::ios::binary);
        const uint8_t p[] = {'P', 'A', 'T', 'C', 'H', 0x07, 0x74, 0xFE, 0x00, 0x05, 'S', 'O', 'N', 'I', 'C', 'E', 'O', 'F'};
        f.write(reinterpret_cast<const char*>(p), sizeof p);
    }
    CHECK_EQ(rom.apply_patches({ips_path}, nullptr), 1);
    std::vector<CharacterMod> mods = {parsed(kKnuckles, "[Stats]\njump = 0x700\n")};
    CHECK(rom.apply_edits(stat_edits(rom, mods, nullptr), nullptr) > 0);
    CHECK_EQ(rom.read16(stat_record_offset(rom, kKnuckles) + 0x16), 0x700);
    CHECK_STR(stage_select::character_names(rom)[kKnuckles], "SONICLES");
    CHECK_EQ(rom.compute_checksum(), rom.read16(0x18E));
    std::filesystem::remove(ips_path);
}

TEST(character_mods, names_come_from_the_roms_table) {
    Rom rom = synthetic_rom();
    std::vector<std::string> names = stage_select::character_names(rom);
    CHECK_EQ(names.size(), size_t(8));
    CHECK_STR(names[0], "MIGHTY");
    CHECK_STR(names[1], "**********");
    CHECK_STR(names[3], "CHARMY BEE");
    CHECK_STR(names[7], "ESPIO");
    CHECK(stage_select::is_placeholder_name(names[1]));
    CHECK(stage_select::is_placeholder_name(""));
    CHECK(!stage_select::is_placeholder_name(names[0]));
    // A patched ROM that names slot 1.
    std::memcpy(rom.data.data() + 0x774F3, "SONIC     ", 10);
    names = stage_select::character_names(rom);
    CHECK_STR(names[1], "SONIC");
    CHECK(!stage_select::is_placeholder_name(names[1]));
    // An offset out of the ROM, and text with no end: the built-in names.
    put16(rom.data, 0x77254 + 2 * 4, 0x7FFF);
    rom.file_size = 0x77254 + 0x7FFF;
    std::memset(rom.data.data() + 0x77254 + 2 * 6 + int16_t(rom.read16(0x77254 + 12)), 'X', 64);
    names = stage_select::character_names(rom);
    CHECK_STR(names[4], "VECTOR");
    CHECK_STR(names[6], "HEAVY");
    CHECK_STR(names[2], "KNUCKLES");
}
