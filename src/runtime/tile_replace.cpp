#include "tile_replace.h"

namespace chaotix {

uint64_t TileReplacer::hash(const uint8_t* p) {
    uint64_t h = 1469598103934665603ull;  // FNV-1a
    for (int i = 0; i < 32; ++i) h = (h ^ p[i]) * 1099511628211ull;
    return h;
}

void TileReplacer::refresh(const uint8_t* vram, const uint32_t* cram_argb64) {
    // The tiles drawn last frame are dumped from the slot hashes of that
    // frame, before they are recomputed for this one.
    if (dumping) {
        for (int s = 0; s < kSlots; ++s) {
            if (!used_[s]) continue;
            const int line = used_[s] - 1;
            used_[s] = 0;
            const uint64_t h = slot_hash_[s];
            if (!dumped_.insert(h).second) continue;
            const uint8_t* p = vram + s * 32;
            bool blank = true;
            for (int i = 0; i < 32 && blank; ++i) blank = p[i] == 0;
            if (blank || hash(p) != h) continue;  // empty, or overwritten since
            Dump d{h, {}, {}};
            for (int i = 0; i < 32; ++i) {
                d.indices[size_t(i * 2)] = p[i] >> 4;
                d.indices[size_t(i * 2 + 1)] = p[i] & 15;
            }
            for (int c = 0; c < 16; ++c) d.palette[size_t(c)] = c ? cram_argb64[line * 16 + c] : 0;
            pending.push_back(d);
        }
    }
    for (int s = 0; s < kSlots; ++s) {
        const uint64_t h = hash(vram + s * 32);
        slot_hash_[s] = h;
        auto it = by_hash_.find(h);
        slot_[s] = it == by_hash_.end() ? nullptr : it->second.data();
    }
}

} // namespace chaotix
