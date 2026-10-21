// ROM patches from the Mods folder (issue #35): IPS and BPS, built here by
// hand so the formats' corners are covered without any real patch file.
#include "runtime/rom.h"
#include "runtime/rom_patch.h"
#include "test_framework.h"

#include <cstdio>
#include <cstring>
#include <fstream>

using namespace chaotix;

namespace {

void put24(std::vector<uint8_t>& v, uint32_t x) { v.push_back(uint8_t(x >> 16)); v.push_back(uint8_t(x >> 8)); v.push_back(uint8_t(x)); }
void put16(std::vector<uint8_t>& v, uint32_t x) { v.push_back(uint8_t(x >> 8)); v.push_back(uint8_t(x)); }
void put32le(std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back(uint8_t(x >> (8 * i))); }

// BPS's variable-length numbers.
void number(std::vector<uint8_t>& v, uint64_t x) {
    for (;;) {
        const uint8_t b = x & 0x7F;
        x >>= 7;
        if (!x) { v.push_back(0x80 | b); return; }
        v.push_back(b);
        --x;
    }
}

std::vector<uint8_t> finish_bps(std::vector<uint8_t> p, const std::vector<uint8_t>& source, const std::vector<uint8_t>& target) {
    put32le(p, rom_patch::crc32(source.data(), source.size()));
    put32le(p, rom_patch::crc32(target.data(), target.size()));
    put32le(p, rom_patch::crc32(p.data(), p.size()));
    return p;
}

} // namespace

TEST(rom_patch, ips_writes_fills_grows_and_truncates) {
    std::vector<uint8_t> image(16, 0x11);
    std::vector<uint8_t> p = {'P', 'A', 'T', 'C', 'H'};
    put24(p, 2); put16(p, 3); p.insert(p.end(), {0xA0, 0xA1, 0xA2});   // bytes
    put24(p, 8); put16(p, 0); put16(p, 4); p.push_back(0x55);         // run of 4
    put24(p, 18); put16(p, 1); p.push_back(0x77);                     // past the end
    p.insert(p.end(), {'E', 'O', 'F'});
    std::string err;
    CHECK(rom_patch::apply(image, p.data(), p.size(), &err));
    CHECK_EQ(image.size(), size_t(19));
    CHECK_EQ(image[1], 0x11);
    CHECK_EQ(image[2], 0xA0);
    CHECK_EQ(image[4], 0xA2);
    CHECK_EQ(image[8], 0x55);
    CHECK_EQ(image[11], 0x55);
    CHECK_EQ(image[12], 0x11);
    CHECK_EQ(image[18], 0x77);

    put24(p, 10);  // the optional truncation after EOF
    CHECK(rom_patch::apply(image, p.data(), p.size(), &err));
    CHECK_EQ(image.size(), size_t(10));
}

TEST(rom_patch, ips_that_is_cut_short_changes_nothing) {
    std::vector<uint8_t> image(16, 0x11);
    std::vector<uint8_t> p = {'P', 'A', 'T', 'C', 'H'};
    put24(p, 2); put16(p, 8); p.push_back(0xA0);  // promises 8 bytes, has 1
    std::string err;
    CHECK(!rom_patch::apply(image, p.data(), p.size(), &err));
    CHECK(!err.empty());
    CHECK_EQ(image[2], 0x11);
    const uint8_t junk[] = {'n', 'o', 'p', 'e', '!', '!'};
    CHECK(!rom_patch::apply(image, junk, sizeof junk, &err));
}

TEST(rom_patch, bps_runs_every_action_and_checks_its_crcs) {
    std::vector<uint8_t> source;
    for (int i = 0; i < 32; ++i) source.push_back(uint8_t(i));
    // Target: source[0..8), "HI", source[20..24), then four bytes copied from
    // earlier in the target itself (from offset 8: "HI", 20, 21).
    std::vector<uint8_t> target(source.begin(), source.begin() + 8);
    target.insert(target.end(), {'H', 'I'});
    target.insert(target.end(), source.begin() + 20, source.begin() + 24);
    target.insert(target.end(), {'H', 'I', 20, 21});
    std::vector<uint8_t> p = {'B', 'P', 'S', '1'};
    number(p, source.size());
    number(p, target.size());
    number(p, 0);                              // no metadata
    number(p, ((8 - 1) << 2) | 0);             // SourceRead 8
    number(p, ((2 - 1) << 2) | 1); p.insert(p.end(), {'H', 'I'});  // TargetRead "HI"
    number(p, ((4 - 1) << 2) | 2); number(p, 20 << 1);             // SourceCopy 4 from +20
    number(p, ((4 - 1) << 2) | 3); number(p, 8 << 1);              // TargetCopy 4 from target 8
    p = finish_bps(p, source, target);

    std::vector<uint8_t> image = source;
    std::string err;
    CHECK(rom_patch::apply(image, p.data(), p.size(), &err));
    CHECK(image == target);

    // Made for another ROM: refused, nothing changed.
    std::vector<uint8_t> other = source;
    other[5] ^= 1;
    CHECK(!rom_patch::apply(other, p.data(), p.size(), &err));
    CHECK(err.find("different ROM") != std::string::npos);
    CHECK_EQ(other[5], uint8_t(5 ^ 1));

    // Damaged in transit: refused.
    std::vector<uint8_t> bad = p;
    bad[6] ^= 0x40;
    image = source;
    CHECK(!rom_patch::apply(image, bad.data(), bad.size(), &err));
    CHECK(image == source);
}

TEST(rom_patch, changed_ranges_finds_the_differences) {
    const std::vector<uint8_t> a = {1, 2, 3, 4, 5, 6};
    const std::vector<uint8_t> b = {1, 9, 9, 4, 5, 6, 7, 8};
    const auto r = rom_patch::changed_ranges(a, b);
    CHECK_EQ(r.size(), size_t(2));
    CHECK(r[0] == std::make_pair(uint32_t(1), uint32_t(2)));
    CHECK(r[1] == std::make_pair(uint32_t(6), uint32_t(2)));
    CHECK(rom_patch::changed_ranges(a, a).empty());
}

TEST(rom_patch, a_patched_rom_keeps_the_original_for_the_recompiled_code) {
    // A minimal 32X image: what Rom::load_memory needs to accept it.
    std::vector<uint8_t> file(0x1000, 0);
    std::memcpy(file.data() + 0x100, "SEGA 32X", 8);
    Rom rom;
    std::string err;
    CHECK(rom.load_memory(file.data(), file.size(), &err));
    const std::string sha1 = rom.sha1;

    std::vector<uint8_t> p = {'P', 'A', 'T', 'C', 'H'};
    put24(p, 0x800); put16(p, 2); p.insert(p.end(), {0xBE, 0xEF});
    p.insert(p.end(), {'E', 'O', 'F'});
    const std::string good = "rom_patch_test_a.ips", bad = "rom_patch_test_b.ips";
    { std::ofstream(good, std::ios::binary).write(reinterpret_cast<const char*>(p.data()), std::streamsize(p.size())); }
    { std::ofstream(bad, std::ios::binary) << "not a patch"; }

    std::vector<std::string> messages;
    CHECK_EQ(rom.apply_patches({bad, good}, &messages), 1);
    CHECK_EQ(messages.size(), size_t(3));
    CHECK(messages[0].rfind("skipped", 0) == 0);
    CHECK(messages[2].find("checksum") != std::string::npos);  // the boot check would stop the game
    CHECK_EQ(rom.data[0x800], 0xBE);
    CHECK_EQ(rom.data[0x801], 0xEF);
    CHECK_EQ(rom.unpatched()[0x800], 0x00);           // validation's reference
    CHECK_STR(rom.sha1, sha1);                         // still the user's ROM
    CHECK(rom.patched_sha1 != sha1);
    CHECK_EQ(rom.compute_checksum(), rom.header_checksum);
    CHECK_EQ(rom.patched.size(), size_t(2));
    CHECK(rom.patched[0].first == 0x18E);                 // the checksum
    CHECK(rom.patched[1] == std::make_pair(uint32_t(0x800), uint32_t(2)));
    std::remove(good.c_str());
    std::remove(bad.c_str());
}
