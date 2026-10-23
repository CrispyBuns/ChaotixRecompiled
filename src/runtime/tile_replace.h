// Mega Drive tile replacement (issue #35): modders' versions of the VDP's
// 8x8 tiles, recognised by what the tile looks like rather than where it is.
//
// A tile is identified by a hash of its 32 bytes of pattern data (its pixels'
// palette indices), so the same picture is recognised wherever and whenever
// the game uploads it, and whatever palette line it is drawn with. A
// replacement gives new indices for the 64 pixels; they are coloured with the
// palette in use as they are drawn, so the game's fades and palette cycling
// still apply. Only what is shown changes: the VDP's status flags and every
// other piece of emulated state come from the original tiles.
//
// Dumping records each tile the game draws, once, with the palette it was
// drawn with, for modders to edit (the frontend writes them as PNG files).
#pragma once
#include "sprite_text.h"
#include "text_replace.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace chaotix {

class TileReplacer {
public:
    static constexpr int kSlots = 2048;  // 64 KiB of VRAM / 32 bytes per tile

    static uint64_t hash(const uint8_t* pattern32);

    // Replacement pixels for a tile: 64 palette indices, row by row (0-15;
    // 0 is transparent, as in the original).
    void add(uint64_t hash, const std::array<uint8_t, 64>& indices) { by_hash_[hash] = indices; }
    size_t size() const { return by_hash_.size(); }
    bool active() const { return dumping || !by_hash_.empty() || text.size() || sprite_text.size(); }

    // Once a frame, before it is drawn: hash every VRAM slot and note which
    // ones hold a tile that has a replacement. Collects the tiles drawn since
    // the last refresh for dumping, with their palette (ARGB per CRAM entry).
    void refresh(const uint8_t* vram, const uint32_t* cram_argb64);

    // Each VRAM slot's hash as of the last refresh.
    const uint64_t* slot_hashes() const { return slot_hash_; }
    // Text replaced by tile sequence (text_replace.h), matched against the
    // slot hashes; refreshed after them.
    TextReplacer text;
    // Text replaced by sprite (sprite_text.h), matched against the slot
    // hashes; refreshed after them.
    SpriteTextReplacer sprite_text;

    // The replacement for VRAM tile `tile` (low 11 bits), or nullptr.
    const uint8_t* slot(uint32_t tile) const { return slot_[tile & (kSlots - 1)]; }
    // A tile was drawn with palette line `line` (0-3).
    void used(uint32_t tile, int line) {
        if (dumping) used_[tile & (kSlots - 1)] = uint8_t(line + 1);
    }

    bool dumping = false;
    struct Dump {
        uint64_t hash;
        std::array<uint8_t, 64> indices;
        std::array<uint32_t, 16> palette;  // ARGB, entry 0 transparent
    };
    // Tiles not dumped before; the frontend takes and writes them.
    std::vector<Dump> pending;

private:
    std::unordered_map<uint64_t, std::array<uint8_t, 64>> by_hash_;
    const uint8_t* slot_[kSlots] = {};
    uint64_t slot_hash_[kSlots] = {};
    uint8_t used_[kSlots] = {};  // palette line + 1 since the last refresh
    std::unordered_set<uint64_t> dumped_;
};

} // namespace chaotix
