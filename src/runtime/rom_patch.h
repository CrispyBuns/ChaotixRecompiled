// ROM patches from the Mods folder (issue #35): IPS and BPS files, the formats
// ROM hacking tools write, applied to the ROM in memory when the game starts.
// The user's ROM file is never changed. Graphics, palettes and other data can
// be modded this way without rebuilding anything; see docs/ROM_PATCHES.md for
// what happens when a patch changes code.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace chaotix::rom_patch {

// Applies an IPS or BPS patch (told apart by its header) to `image`, a ROM
// file's bytes. BPS checks its source, target and patch CRCs. On failure
// `image` is left as it was and `error` says why.
bool apply(std::vector<uint8_t>& image, const uint8_t* patch, size_t n, std::string* error);
bool apply_ips(std::vector<uint8_t>& image, const uint8_t* patch, size_t n, std::string* error);
bool apply_bps(std::vector<uint8_t>& image, const uint8_t* patch, size_t n, std::string* error);

// The byte ranges (offset, length) where `after` differs from `before`; bytes
// past the end of the shorter one count as changed.
std::vector<std::pair<uint32_t, uint32_t>> changed_ranges(const std::vector<uint8_t>& before,
                                                          const std::vector<uint8_t>& after);

uint32_t crc32(const uint8_t* p, size_t n);

} // namespace chaotix::rom_patch
