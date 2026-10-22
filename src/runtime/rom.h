// ROM loading and verification. The ROM is never part of the repository; the
// user supplies it and we verify it against known hashes.
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace chaotix {

struct MarsHeader {
    char module_name[17] = {};
    uint32_t version = 0;
    uint32_t source = 0;       // ROM offset of the SH-2 image
    uint32_t dest = 0;         // SDRAM offset
    uint32_t size = 0;
    uint32_t master_entry = 0;
    uint32_t slave_entry = 0;
    uint32_t master_vbr = 0;
    uint32_t slave_vbr = 0;
};

enum class RomVersion { Unknown, ChaotixJUE };

// Bytes written over the ROM image at a file offset: an in-memory data patch
// made by the program itself (character mods' stats) rather than read from a
// patch file.
struct RomEdit {
    uint32_t offset = 0;
    std::vector<uint8_t> bytes;
};

struct Rom {
    std::vector<uint8_t> data;   // big-endian image, padded to power of two
    size_t file_size = 0;
    std::string sha1;
    RomVersion version = RomVersion::Unknown;
    MarsHeader mars;
    uint16_t header_checksum = 0;
    char domestic_name[49] = {};

    // After patches (issue #35, runtime/rom_patch.h): `data` is the patched
    // image, while `sha1` and `version` stay the user's ROM's, since that is
    // what the recompiled code was built from. `original` is that ROM's image,
    // for validating recompiled code; `patched` lists the bytes that changed.
    std::vector<uint8_t> original;
    std::vector<std::pair<uint32_t, uint32_t>> patched;
    std::string patched_sha1;

    bool load(const std::string& path, std::string* error);
    bool load_memory(const uint8_t* bytes, size_t n, std::string* error);
    // Applies the patch files in this order; one that fails is skipped and
    // said so in `messages`. Returns how many were applied.
    int apply_patches(const std::vector<std::string>& paths, std::vector<std::string>* messages);
    // Writes the edits over the ROM as it is now (after any patch files, so
    // call this second), with the same bookkeeping as a patch: `patched`,
    // `original` and the header checksum. An edit past the end of the file
    // is skipped and said so. Returns how many bytes changed; edits that
    // leave every byte as it was change nothing at all.
    int apply_edits(const std::vector<RomEdit>& edits, std::vector<std::string>* messages);
    // The image the recompiled code was made from.
    const std::vector<uint8_t>& unpatched() const { return original.empty() ? data : original; }
    uint16_t compute_checksum() const;  // Mega Drive style (sum of words from 0x200)
    uint32_t read32(uint32_t off) const {
        return (uint32_t(data[off]) << 24) | (uint32_t(data[off + 1]) << 16) | (uint32_t(data[off + 2]) << 8) | data[off + 3];
    }
    uint16_t read16(uint32_t off) const { return uint16_t((data[off] << 8) | data[off + 1]); }

private:
    // Takes `image` (a ROM file's bytes, changed from `before`) as this ROM,
    // fixing the header checksum if `before`'s was right. False if the result
    // is not a ROM we can load, leaving this one as it was.
    bool adopt(std::vector<uint8_t> image, const std::vector<uint8_t>& before, std::vector<std::string>* messages);
};

std::string sha1_hex(const uint8_t* data, size_t n);
const char* rom_version_name(RomVersion v);

} // namespace chaotix
