// Save states and rewind (issue #40) without the ROM: a machine on a blank
// cartridge, its memory filled with a pattern, saved, loaded and run on.
// The ROM tests in CMakeLists.txt (save_state_*) check a real game carries on
// bit-identically after a load.
#include "audio/music_mods.h"
#include "game/achievements.h"
#include "input/touch_controls.h"
#include "runtime/lz.h"
#include "runtime/rewind.h"
#include "runtime/save_state.h"
#include "runtime/system.h"
#include "test_framework.h"

#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace chaotix;

namespace {

const char kSha[] = "0123456789abcdef0123456789abcdef01234567";

std::unique_ptr<Machine> blank_machine() {
    auto m = std::make_unique<Machine>();
    m->rom.data.assign(0x400000, 0);
    m->rom.sha1 = kSha;
    m->reset();
    return m;
}

uint32_t next(uint32_t& seed) {
    seed = seed * 1664525u + 1013904223u;
    return seed >> 8;
}

// Memory and a few registers set to a pattern, as a game in progress would
// leave them; the hardware registers are left as reset made them, so the
// machine still runs sensibly.
void scribble(Machine& m, uint32_t seed) {
    for (auto& b : m.wram) b = uint8_t(next(seed));
    for (size_t i = 0; i < sizeof m.sdram; i += 7) m.sdram[i] = uint8_t(next(seed));
    for (size_t i = 0; i < sizeof m.vdp.vram; i += 3) m.vdp.vram[i] = uint8_t(next(seed));
    for (auto& fb : m.mars.fb)
        for (size_t i = 0; i < sizeof fb; i += 5) fb[i] = uint8_t(next(seed));
    for (auto& c : m.vdp.cram) c = uint16_t(next(seed) & 0xEEE);
    for (auto& c : m.mars.pal) c = uint16_t(next(seed));
    for (auto& px : m.framebuffer) px = next(seed);
    for (auto& d : m.m68k.d) d = next(seed);
    m.sh2[0].r[3] = next(seed);
    m.plane_shift = 8;
    m.level_seen_frame = 1234;
}

std::vector<uint8_t> raw(const Machine& m, unsigned flags = savestate::kPicture) {
    std::vector<uint8_t> out;
    savestate::serialize(m, out, flags);
    return out;
}

} // namespace

TEST(lz, round_trips_any_data) {
    uint32_t seed = 7;
    std::vector<std::vector<uint8_t>> inputs;
    inputs.push_back({});
    inputs.push_back({42});
    for (size_t n : {15, 16, 17, 100, 4096}) {
        std::vector<uint8_t> v(n);
        for (auto& b : v) b = uint8_t(next(seed));
        inputs.push_back(v);
    }
    inputs.push_back(std::vector<uint8_t>(1 << 20, 0));  // a long run
    std::vector<uint8_t> mixed(300000, 0);
    for (size_t i = 0; i < mixed.size(); i += 1 + next(seed) % 900)
        for (size_t k = 0; k < 40 && i + k < mixed.size(); ++k) mixed[i + k] = uint8_t(next(seed));
    inputs.push_back(mixed);
    std::vector<uint8_t> repeats;
    for (int i = 0; i < 70000; ++i) repeats.push_back(uint8_t("ABCDEFG"[i % 7]));
    inputs.push_back(repeats);
    for (const auto& in : inputs) {
        std::vector<uint8_t> packed, back(in.size());
        lz::compress(in.data(), in.size(), packed);
        CHECK(lz::decompress(packed.data(), packed.size(), back.data(), back.size()));
        CHECK(back == in);
    }
    // Runs pack down to almost nothing.
    std::vector<uint8_t> packed;
    lz::compress(inputs[7].data(), inputs[7].size(), packed);
    CHECK(packed.size() < 5000);
}

TEST(lz, refuses_damaged_input_without_overrunning) {
    std::vector<uint8_t> in(20000);
    uint32_t seed = 3;
    for (size_t i = 0; i < in.size(); ++i) in[i] = uint8_t(i % 50 < 30 ? 0 : next(seed));
    std::vector<uint8_t> packed;
    lz::compress(in.data(), in.size(), packed);
    std::vector<uint8_t> out(in.size());
    // The wrong size, either way.
    CHECK(!lz::decompress(packed.data(), packed.size(), out.data(), out.size() - 1));
    std::vector<uint8_t> big(in.size() + 1);
    CHECK(!lz::decompress(packed.data(), packed.size(), big.data(), big.size()));
    // Truncated, and bytes changed: false or (rarely) wrong data, never a crash.
    for (size_t cut = 0; cut < packed.size(); cut += 97) CHECK(!lz::decompress(packed.data(), cut, out.data(), out.size()));
    for (size_t i = 0; i < packed.size(); i += 13) {
        std::vector<uint8_t> bad = packed;
        bad[i] ^= 0xA5;
        lz::decompress(bad.data(), bad.size(), out.data(), out.size());
    }
}

TEST(save_state, round_trip_is_byte_identical) {
    auto a = blank_machine();
    scribble(*a, 1);
    const std::vector<uint8_t> host = {1, 2, 3};
    const std::vector<uint8_t> file = savestate::save(*a, host, 1700000000);
    auto b = blank_machine();
    std::string err;
    std::vector<uint8_t> host_back;
    savestate::Info info;
    CHECK(savestate::load(*b, file, &err, &host_back, &info));
    CHECK_STR(err, "");
    CHECK(host_back == host);
    CHECK(raw(*a) == raw(*b));
    CHECK(savestate::save(*b, host, 1700000000) == file);
    CHECK_EQ(info.frame, a->frame_count);
    CHECK_EQ(info.saved_unix, int64_t(1700000000));
    CHECK_STR(info.rom_sha1, kSha);
    CHECK_STR(info.game_version, savestate::game_version());
    // Compressed well below the raw size.
    CHECK(file.size() < savestate::raw_size(savestate::kPicture));
}

TEST(save_state, a_loaded_machine_runs_on_identically) {
    auto a = blank_machine();
    scribble(*a, 2);
    for (int i = 0; i < 3; ++i) a->run_frame();
    auto b = blank_machine();
    CHECK(savestate::load(*b, savestate::save(*a), nullptr));
    for (int i = 0; i < 4; ++i) {
        a->run_frame();
        b->run_frame();
        CHECK(raw(*a) == raw(*b));
    }
    CHECK_EQ(savestate::state_hash(*a), savestate::state_hash(*b));
}

TEST(save_state, loading_keeps_the_cartridge_save) {
    auto a = blank_machine();
    std::memset(a->sram, 0x11, sizeof a->sram);
    const std::vector<uint8_t> file = savestate::save(*a);
    auto b = blank_machine();
    std::memset(b->sram, 0x22, sizeof b->sram);
    b->sram_dirty = true;
    CHECK(savestate::load(*b, file, nullptr));
    bool kept = true;
    for (uint8_t v : b->sram) kept = kept && v == 0x22;
    CHECK(kept);
    CHECK(b->sram_dirty);
}

TEST(save_state, loading_moves_the_code_epochs_on) {
    auto a = blank_machine();
    a->sdram_code_epoch = 50;
    const std::vector<uint8_t> file = savestate::save(*a);
    auto b = blank_machine();
    b->sdram_code_epoch = 7;
    b->cache_code_epoch[1] = 9;
    CHECK(savestate::load(*b, file, nullptr));
    // Never back to a value the recompiled code may have cached a check at.
    CHECK_EQ(b->sdram_code_epoch, 8u);
    CHECK_EQ(b->cache_code_epoch[1], 10u);
}

TEST(save_state, refuses_another_rom_version_or_damage) {
    auto a = blank_machine();
    scribble(*a, 3);
    const std::vector<uint8_t> file = savestate::save(*a);
    auto b = blank_machine();
    scribble(*b, 4);
    const std::vector<uint8_t> before = raw(*b);
    std::string err;
    auto refused = [&](const std::vector<uint8_t>& f, const char* why) {
        err.clear();
        CHECK(!savestate::load(*b, f, &err));
        if (err.find(why) == std::string::npos) tf::fail(__FILE__, __LINE__, "\"" + err + "\" lacks \"" + why + "\"");
        CHECK(raw(*b) == before);  // untouched
    };
    b->rom.sha1 = "ffffffffffffffffffffffffffffffffffffffff";
    refused(file, "another ROM");
    b->rom.sha1 = kSha;
    b->rom.patched_sha1 = "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";
    refused(file, "ROM patches");
    b->rom.patched_sha1.clear();
    std::vector<uint8_t> f = file;
    f[8] = uint8_t(savestate::kFormatVersion + 1);
    refused(f, "format");
    f = file;
    f[16] = '9';  // the version text
    refused(f, "version");
    f = file;
    f[f.size() / 2] ^= 0x5A;
    refused(f, "damaged");
    f = file;
    f.resize(f.size() - 10);
    refused(f, "truncated");
    refused(std::vector<uint8_t>{'n', 'o', 'p', 'e'}, "not a save state");
    // And the file as it was still loads.
    CHECK(savestate::load(*b, file, &err));
}

TEST(save_state, raw_state_is_the_same_size_with_and_without_a_picture_but_for_it) {
    CHECK_EQ(savestate::raw_size(savestate::kPicture) - savestate::raw_size(0), sizeof(Machine::framebuffer));
    auto a = blank_machine();
    CHECK_EQ(raw(*a, 0).size(), savestate::raw_size(0));
    std::string err;
    std::vector<uint8_t> r = raw(*a, 0);
    r.pop_back();
    CHECK(!savestate::deserialize(*a, r.data(), r.size(), 0, &err));
}

TEST(rewind, steps_back_through_exact_earlier_states) {
    auto m = blank_machine();
    RewindBuffer rw;
    rw.set_budget(size_t(64) << 20);
    std::vector<std::vector<uint8_t>> taken;
    std::vector<uint64_t> frames;
    uint32_t seed = 9;
    for (int i = 0; i < 20; ++i) {
        // A few bytes change between snapshots, as between frames of play.
        for (int k = 0; k < 50; ++k) m->wram[next(seed) % sizeof m->wram] = uint8_t(next(seed));
        m->frame_count += 2;
        rw.push(*m);
        taken.push_back(raw(*m, 0));
        frames.push_back(m->frame_count);
    }
    CHECK_EQ(rw.snapshots(), size_t(20));
    CHECK_EQ(rw.newest_frame(), frames.back());
    CHECK_EQ(rw.oldest_frame(), frames.front());
    // The machine runs on past the newest: the first step goes back to it.
    m->frame_count += 1;
    m->wram[0] ^= 0xFF;
    CHECK(rw.step_back(*m));
    CHECK(raw(*m, 0) == taken.back());
    for (int i = 18; i >= 0; --i) {
        const bool more = rw.step_back(*m);
        CHECK(raw(*m, 0) == taken[size_t(i)]);
        CHECK(more);  // the step that reaches the oldest still succeeds
    }
    // Nothing older: m stays at the oldest.
    CHECK(!rw.step_back(*m));
    CHECK(raw(*m, 0) == taken.front());
    // Playing on from there starts a new history.
    m->frame_count += 2;
    rw.push(*m);
    CHECK_EQ(rw.snapshots(), size_t(2));
    rw.clear(true);
    CHECK_EQ(rw.snapshots(), size_t(0));
    CHECK(!rw.step_back(*m));
}

TEST(rewind, keeps_to_its_budget_and_snapshot_limit) {
    auto m = blank_machine();
    RewindBuffer rw;
    // Room for the working buffers and a few deltas of big changes.
    const size_t budget = savestate::raw_size(0) * 3 + 1500000;
    rw.set_budget(budget);
    uint32_t seed = 11;
    for (int i = 0; i < 40; ++i) {
        for (int k = 0; k < 20000; ++k) m->sdram[next(seed) % sizeof m->sdram] = uint8_t(next(seed));
        m->frame_count += 2;
        rw.push(*m);
        CHECK(rw.memory() <= budget);
    }
    CHECK(rw.snapshots() < 40);
    CHECK(rw.snapshots() >= 2);
    // What is left still steps back exactly to the newest snapshot.
    const std::vector<uint8_t> newest = raw(*m, 0);
    m->frame_count += 1;
    CHECK(rw.step_back(*m));
    CHECK(raw(*m, 0) == newest);
    rw.set_max_snapshots(3);
    CHECK_EQ(rw.snapshots(), size_t(3));
    m->frame_count += 2;
    rw.push(*m);
    CHECK_EQ(rw.snapshots(), size_t(3));
}

TEST(save_state, the_replacement_musics_place_comes_back) {
    audio::Track t;
    t.samples.assign(2000, 100);
    t.frames = 1000;
    audio::MusicMods a;
    a.set_track(5, t);
    auto m = std::make_unique<Machine>();
    m->music_events = {5};
    m->audio_out.assign(200, 0);
    a.after_frame(*m, 0);
    CHECK_EQ(a.playing(), 5);
    const std::vector<uint8_t> pos = a.save_position();
    audio::MusicMods b;
    b.set_track(5, t);
    b.load_position(pos);
    CHECK_EQ(b.playing(), 5);
    CHECK(b.save_position() == pos);
    // Malformed bytes change nothing; a piece without a file plays nothing.
    audio::MusicMods c;
    c.load_position({1, 2, 3});
    CHECK_EQ(c.playing(), 0);
    c.load_position(pos);
    CHECK_EQ(c.playing(), 0);
}

TEST(achievements, a_jump_in_time_is_not_a_change) {
    achievements::Tracker t;
    std::string err;
    CHECK(t.load_definitions_text("[vars]\nrings = word FFE008\n[achievement More]\nid = more\nwhen = rings > prev(rings)\n", &err));
    std::vector<uint8_t> wram(0x10000, 0);
    int unlocked = 0;
    auto count = [&](const achievements::Achievement&) { ++unlocked; };
    t.update_memory(wram.data(), wram.size(), count);
    t.update_memory(wram.data(), wram.size(), count);
    // A state with more rings is loaded: no "more rings than last frame".
    t.forget_previous();
    wram[0xE009] = 50;
    t.update_memory(wram.data(), wram.size(), count);
    CHECK_EQ(unlocked, 0);
    wram[0xE009] = 51;  // and then one is collected
    t.update_memory(wram.data(), wram.size(), count);
    CHECK_EQ(unlocked, 1);
}

TEST(touch, the_rewind_button_sits_beside_fast_forward) {
    TouchControls t;
    t.layout(1920, 1080, false, false, true, false);
    for (const auto& b : t.buttons()) CHECK(b.mask != TOUCH_REWIND);
    for (bool sonic_cd : {false, true}) {
        t.layout(1920, 1080, false, sonic_cd, true, true);
        const TouchButton* rew = nullptr;
        const TouchButton* ff = nullptr;
        for (const auto& b : t.buttons()) {
            if (b.mask == TOUCH_REWIND) rew = &b;
            if (b.mask == TOUCH_FAST_FORWARD) ff = &b;
        }
        CHECK(rew && ff);
        if (!rew || !ff) continue;
        CHECK(rew->cx < ff->cx);  // as on a tape deck
        CHECK_EQ(t.hit(rew->cx, rew->cy), TOUCH_REWIND);
        CHECK_EQ(t.hit(ff->cx, ff->cy), TOUCH_FAST_FORWARD);
    }
    // On its own it takes the corner.
    t.layout(1920, 1080, false, false, false, true);
    for (const auto& b : t.buttons())
        if (b.mask == TOUCH_REWIND) CHECK_EQ(t.hit(b.cx, b.cy), TOUCH_REWIND);
}
