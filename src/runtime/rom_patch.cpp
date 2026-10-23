#include "rom_patch.h"

#include <algorithm>
#include <cstring>

namespace chaotix::rom_patch {

uint32_t crc32(const uint8_t* p, size_t n) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

bool apply(std::vector<uint8_t>& image, const uint8_t* patch, size_t n, std::string* error) {
    if (n >= 5 && std::memcmp(patch, "PATCH", 5) == 0) return apply_ips(image, patch, n, error);
    if (n >= 4 && std::memcmp(patch, "BPS1", 4) == 0) return apply_bps(image, patch, n, error);
    if (error) *error = "not an IPS or BPS patch";
    return false;
}

// IPS: "PATCH", then records of a 3-byte offset and a 2-byte length followed
// by that many bytes -- or, with length 0, a 2-byte count and one byte to
// repeat -- up to "EOF", optionally followed by a 3-byte length to truncate to.
bool apply_ips(std::vector<uint8_t>& image, const uint8_t* p, size_t n, std::string* error) {
    auto fail = [error](const char* why) { if (error) *error = why; return false; };
    std::vector<uint8_t> out = image;
    size_t i = 5;
    for (;;) {
        if (i + 3 > n) return fail("IPS patch ends before EOF");
        if (std::memcmp(p + i, "EOF", 3) == 0) {
            i += 3;
            break;
        }
        const uint32_t off = uint32_t(p[i]) << 16 | uint32_t(p[i + 1]) << 8 | p[i + 2];
        i += 3;
        if (i + 2 > n) return fail("IPS record cut short");
        const uint32_t len = uint32_t(p[i]) << 8 | p[i + 1];
        i += 2;
        if (len) {
            if (i + len > n) return fail("IPS record cut short");
            if (out.size() < off + len) out.resize(off + len, 0);
            std::memcpy(out.data() + off, p + i, len);
            i += len;
        } else {
            if (i + 3 > n) return fail("IPS record cut short");
            const uint32_t count = uint32_t(p[i]) << 8 | p[i + 1];
            const uint8_t value = p[i + 2];
            i += 3;
            if (out.size() < off + count) out.resize(off + count, 0);
            std::memset(out.data() + off, value, count);
        }
    }
    if (i + 3 <= n) out.resize(uint32_t(p[i]) << 16 | uint32_t(p[i + 1]) << 8 | p[i + 2]);
    image = std::move(out);
    return true;
}

// BPS (byuu's format): "BPS1", source size, target size, metadata, then
// actions copying from the source, the patch, or earlier in either; the
// footer holds the CRCs of the source, the target and the patch itself.
bool apply_bps(std::vector<uint8_t>& image, const uint8_t* p, size_t n, std::string* error) {
    auto fail = [error](const char* why) { if (error) *error = why; return false; };
    if (n < 4 + 3 + 12) return fail("BPS patch too short");
    const size_t end = n - 12;
    auto u32 = [p](size_t at) { return uint32_t(p[at]) | uint32_t(p[at + 1]) << 8 | uint32_t(p[at + 2]) << 16 | uint32_t(p[at + 3]) << 24; };
    if (crc32(p, n - 4) != u32(n - 4)) return fail("BPS patch is damaged (its own checksum does not match)");
    if (crc32(image.data(), image.size()) != u32(n - 12))
        return fail("BPS patch was made for a different ROM (source checksum does not match)");
    size_t i = 4;
    bool ok = true;
    auto number = [&]() -> uint64_t {
        uint64_t data = 0, shift = 1;
        for (;;) {
            if (i >= end) { ok = false; return 0; }
            const uint8_t x = p[i++];
            data += (x & 0x7F) * shift;
            if (x & 0x80) break;
            shift <<= 7;
            data += shift;
        }
        return data;
    };
    const uint64_t source_size = number(), target_size = number(), meta = number();
    if (!ok || source_size != image.size()) return fail("BPS patch was made for a different ROM (size does not match)");
    if (target_size > (64u << 20)) return fail("BPS target too large");
    i += size_t(meta);
    std::vector<uint8_t> out(size_t(target_size), 0);
    size_t out_off = 0;
    int64_t source_rel = 0, target_rel = 0;
    while (ok && i < end) {
        const uint64_t data = number();
        const uint64_t command = data & 3, length = (data >> 2) + 1;
        if (!ok || out_off + length > out.size()) return fail("BPS patch is malformed");
        if (command == 0) {  // SourceRead
            if (out_off + length > image.size()) return fail("BPS patch is malformed");
            std::memcpy(out.data() + out_off, image.data() + out_off, size_t(length));
            out_off += size_t(length);
        } else if (command == 1) {  // TargetRead
            if (i + length > end) return fail("BPS patch is malformed");
            std::memcpy(out.data() + out_off, p + i, size_t(length));
            i += size_t(length);
            out_off += size_t(length);
        } else {
            const uint64_t d = number();
            const int64_t delta = int64_t(d >> 1) * ((d & 1) ? -1 : 1);
            if (command == 2) {  // SourceCopy
                source_rel += delta;
                if (source_rel < 0 || uint64_t(source_rel) + length > image.size()) return fail("BPS patch is malformed");
                std::memcpy(out.data() + out_off, image.data() + source_rel, size_t(length));
                source_rel += int64_t(length);
                out_off += size_t(length);
            } else {  // TargetCopy: may overlap what it is writing, byte by byte
                target_rel += delta;
                if (target_rel < 0 || uint64_t(target_rel) >= out_off) return fail("BPS patch is malformed");
                for (uint64_t k = 0; k < length; ++k) out[out_off++] = out[size_t(target_rel++)];
            }
        }
    }
    if (!ok || out_off != out.size()) return fail("BPS patch is malformed");
    if (crc32(out.data(), out.size()) != u32(n - 8)) return fail("BPS result does not match its checksum");
    image = std::move(out);
    return true;
}

std::vector<std::pair<uint32_t, uint32_t>> changed_ranges(const std::vector<uint8_t>& before,
                                                          const std::vector<uint8_t>& after) {
    std::vector<std::pair<uint32_t, uint32_t>> out;
    const size_t n = std::max(before.size(), after.size());
    size_t i = 0;
    while (i < n) {
        auto differs = [&](size_t k) { return k >= before.size() || k >= after.size() || before[k] != after[k]; };
        if (!differs(i)) { ++i; continue; }
        const size_t start = i;
        while (i < n && differs(i)) ++i;
        out.push_back({uint32_t(start), uint32_t(i - start)});
    }
    return out;
}

} // namespace chaotix::rom_patch
