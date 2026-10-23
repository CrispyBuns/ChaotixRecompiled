#include "runtime/lz.h"

#include <cstring>

namespace chaotix::lz {

namespace {

constexpr int kHashBits = 16;
constexpr size_t kMinMatch = 4;
constexpr size_t kMaxOffset = 65535;

inline uint32_t read32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

inline uint64_t read64(const uint8_t* p) {
    uint64_t v;
    std::memcpy(&v, p, 8);
    return v;
}

inline uint32_t hash4(uint32_t v) { return (v * 2654435761u) >> (32 - kHashBits); }

// A count of 15 or more continues in bytes of 255 and a final remainder.
void put_length(std::vector<uint8_t>& out, size_t rest) {
    while (rest >= 255) {
        out.push_back(255);
        rest -= 255;
    }
    out.push_back(uint8_t(rest));
}

void put_sequence(std::vector<uint8_t>& out, const uint8_t* lit, size_t nlit, size_t offset, size_t mlen) {
    const size_t ml = mlen - kMinMatch;
    out.push_back(uint8_t((nlit >= 15 ? 15 : nlit) << 4 | (ml >= 15 ? 15 : ml)));
    if (nlit >= 15) put_length(out, nlit - 15);
    out.insert(out.end(), lit, lit + nlit);
    out.push_back(uint8_t(offset));
    out.push_back(uint8_t(offset >> 8));
    if (ml >= 15) put_length(out, ml - 15);
}

void put_last(std::vector<uint8_t>& out, const uint8_t* lit, size_t nlit) {
    out.push_back(uint8_t((nlit >= 15 ? 15 : nlit) << 4));
    if (nlit >= 15) put_length(out, nlit - 15);
    out.insert(out.end(), lit, lit + nlit);
}

} // namespace

void compress(const uint8_t* src, size_t n, std::vector<uint8_t>& out) {
    out.clear();
    out.reserve(n / 4 + 64);
    // Positions + 1, so 0 means empty. One table per thread, reused: clearing
    // 256 KiB costs far less than allocating it.
    thread_local std::vector<uint32_t> table;
    table.assign(size_t(1) << kHashBits, 0);
    size_t anchor = 0, ip = 0;
    if (n >= 16) {
        const size_t last = n - kMinMatch;  // a match may start up to here
        while (ip <= last) {
            const uint32_t seq = read32(src + ip);
            const uint32_t h = hash4(seq);
            const size_t ref1 = table[h];
            table[h] = uint32_t(ip + 1);
            if (ref1 && ip - (ref1 - 1) <= kMaxOffset && read32(src + ref1 - 1) == seq) {
                const size_t ref = ref1 - 1;
                size_t len = kMinMatch;
                while (ip + len + 8 <= n && read64(src + ref + len) == read64(src + ip + len)) len += 8;
                while (ip + len < n && src[ref + len] == src[ip + len]) ++len;
                put_sequence(out, src + anchor, ip - anchor, ip - ref, len);
                ip += len;
                anchor = ip;
                if (ip >= 2 && ip - 2 <= last) table[hash4(read32(src + ip - 2))] = uint32_t(ip - 2 + 1);
                continue;
            }
            // Skip faster through data that does not compress.
            ip += 1 + ((ip - anchor) >> 6);
        }
    }
    put_last(out, src + anchor, n - anchor);
}

bool decompress(const uint8_t* src, size_t n, uint8_t* dst, size_t dst_size) {
    size_t ip = 0, op = 0;
    auto length = [&](size_t& v) {
        for (;;) {
            if (ip >= n) return false;
            const uint8_t b = src[ip++];
            v += b;
            if (b != 255) return true;
        }
    };
    while (ip < n) {
        const uint8_t token = src[ip++];
        size_t nlit = token >> 4;
        if (nlit == 15 && !length(nlit)) return false;
        if (nlit > n - ip || nlit > dst_size - op) return false;
        std::memcpy(dst + op, src + ip, nlit);
        ip += nlit;
        op += nlit;
        if (ip == n) break;  // the last sequence: literals only
        if (n - ip < 2) return false;
        size_t dist = size_t(src[ip]) | size_t(src[ip + 1]) << 8;
        ip += 2;
        size_t mlen = (token & 15) + kMinMatch;
        if ((token & 15) == 15 && !length(mlen)) return false;
        if (dist == 0 || dist > op || mlen > dst_size - op) return false;
        // An overlapping copy repeats the last `dist` bytes. Each pass copies
        // at most `dist` bytes (no overlap within one memcpy), after which
        // twice as much of the pattern lies behind, so the step doubles.
        while (mlen) {
            const size_t c = mlen < dist ? mlen : dist;
            std::memcpy(dst + op, dst + op - dist, c);
            op += c;
            mlen -= c;
            dist += c;
        }
    }
    return op == dst_size;
}

} // namespace chaotix::lz
