#include "image_io.h"
#include <cstdio>
#include <cstring>
#include <vector>

namespace chaotix {

namespace {

uint32_t crc32(const uint8_t* d, size_t n, uint32_t crc = 0) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ d[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

uint32_t adler32(const uint8_t* d, size_t n) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < n; ++i) { a = (a + d[i]) % 65521; b = (b + a) % 65521; }
    return (b << 16) | a;
}

void put32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(uint8_t(x >> 24)); v.push_back(uint8_t(x >> 16)); v.push_back(uint8_t(x >> 8)); v.push_back(uint8_t(x));
}

uint32_t get32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }

void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    put32(out, uint32_t(data.size()));
    std::vector<uint8_t> td(type, type + 4);
    td.insert(td.end(), data.begin(), data.end());
    out.insert(out.end(), td.begin(), td.end());
    put32(out, crc32(td.data(), td.size()));
}

// A zlib stream of stored (uncompressed) deflate blocks. The files written
// here are small or only for debugging, so not compressing keeps the writer
// trivial; every reader accepts stored blocks.
std::vector<uint8_t> zlib_stored(const std::vector<uint8_t>& raw) {
    std::vector<uint8_t> z;
    z.push_back(0x78); z.push_back(0x01);
    size_t pos = 0;
    while (pos < raw.size() || raw.empty()) {
        size_t n = raw.size() - pos;
        if (n > 65535) n = 65535;
        bool last = pos + n >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(uint8_t(n)); z.push_back(uint8_t(n >> 8));
        z.push_back(uint8_t(~n)); z.push_back(uint8_t((~n) >> 8));
        z.insert(z.end(), raw.begin() + long(pos), raw.begin() + long(pos + n));
        pos += n;
        if (raw.empty()) break;
    }
    put32(z, adler32(raw.data(), raw.size()));
    return z;
}

std::vector<uint8_t> png_header(int w, int h, int depth, int colour_type) {
    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<uint8_t> ihdr;
    put32(ihdr, uint32_t(w)); put32(ihdr, uint32_t(h));
    ihdr.push_back(uint8_t(depth)); ihdr.push_back(uint8_t(colour_type)); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    chunk(out, "IHDR", ihdr);
    return out;
}

bool write_file(const std::string& path, const std::vector<uint8_t>& data) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    return std::fclose(f) == 0 && ok;
}

bool fail(std::string* error, const char* msg) {
    if (error) *error = msg;
    return false;
}

// --- Inflate -----------------------------------------------------------------
// A compact decoder for deflate (RFC 1951), in the manner of zlib's puff.c:
// canonical Huffman codes are decoded a bit at a time from per-length counts.
// That is slow next to table-driven decoders but plenty for tile-sized PNGs,
// and small enough to check by eye. Every read is bounds-checked; running off
// the end of the input sets `overrun` and reads zeros, and callers check the
// flag before trusting anything they decoded.

struct BitReader {
    const uint8_t* p;
    size_t n;
    size_t pos = 0;
    uint32_t buf = 0;
    int count = 0;
    bool overrun = false;

    uint32_t bits(int need) {
        while (count < need) {
            if (pos >= n) { overrun = true; return 0; }
            buf |= uint32_t(p[pos++]) << count;
            count += 8;
        }
        const uint32_t v = buf & ((1u << need) - 1);
        buf >>= need;
        count -= need;
        return v;
    }
    // Stored blocks and the Adler-32 start on a byte boundary. Whole bytes
    // are never left in `buf` (bytes are loaded only as bits are needed), so
    // dropping the partial byte is enough.
    void align() { buf = 0; count = 0; }
};

constexpr int kMaxBits = 15;

struct Huffman {
    uint16_t count[kMaxBits + 1];
    uint16_t symbol[288];
};

// Builds a decoder from code lengths. Over-subscribed lengths are corrupt;
// an incomplete code is allowed (deflate uses one for a lone distance code)
// and its unused codes simply fail to decode.
bool build(Huffman& h, const uint8_t* lengths, int n) {
    std::memset(h.count, 0, sizeof h.count);
    for (int i = 0; i < n; ++i) ++h.count[lengths[i]];
    int left = 1;
    for (int len = 1; len <= kMaxBits; ++len) {
        left <<= 1;
        left -= h.count[len];
        if (left < 0) return false;
    }
    uint16_t offs[kMaxBits + 1];
    offs[1] = 0;
    for (int len = 1; len < kMaxBits; ++len) offs[len + 1] = uint16_t(offs[len] + h.count[len]);
    for (int i = 0; i < n; ++i)
        if (lengths[i]) h.symbol[offs[lengths[i]]++] = uint16_t(i);
    return true;
}

int decode(BitReader& br, const Huffman& h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= kMaxBits; ++len) {
        code |= int(br.bits(1));
        const int count = h.count[len];
        if (code - count < first) return h.symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
        if (br.overrun) return -1;
    }
    return -1;
}

const uint16_t kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                               35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                                257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

// Decodes one Huffman-coded block's symbols into `out`, which may not grow
// past `limit` (the size the image header implies).
bool codes(BitReader& br, std::vector<uint8_t>& out, size_t limit, const Huffman& lit, const Huffman& dist) {
    for (;;) {
        const int sym = decode(br, lit);
        if (sym < 0 || br.overrun) return false;
        if (sym < 256) {
            if (out.size() >= limit) return false;
            out.push_back(uint8_t(sym));
        } else if (sym == 256) {
            return true;
        } else {
            const int li = sym - 257;
            if (li >= 29) return false;
            const size_t len = kLenBase[li] + br.bits(kLenExtra[li]);
            const int di = decode(br, dist);
            if (di < 0 || di >= 30 || br.overrun) return false;
            const size_t d = kDistBase[di] + br.bits(kDistExtra[di]);
            if (br.overrun || d > out.size() || len > limit - out.size()) return false;
            // Byte by byte: a copy may overlap the bytes it is producing.
            for (size_t i = 0; i < len; ++i) out.push_back(out[out.size() - d]);
        }
    }
}

// The fixed codes of block type 1. A function-local static is built once,
// safely, even if tiles are loaded from more than one thread.
struct FixedCodes {
    Huffman lit, dist;
    FixedCodes() {
        uint8_t l[288];
        for (int i = 0; i < 144; ++i) l[i] = 8;
        for (int i = 144; i < 256; ++i) l[i] = 9;
        for (int i = 256; i < 280; ++i) l[i] = 7;
        for (int i = 280; i < 288; ++i) l[i] = 8;
        build(lit, l, 288);
        for (int i = 0; i < 30; ++i) l[i] = 5;
        build(dist, l, 30);
    }
};

bool inflate(BitReader& br, std::vector<uint8_t>& out, size_t limit) {
    static const FixedCodes fixed;

    bool last = false;
    while (!last) {
        last = br.bits(1) != 0;
        const uint32_t type = br.bits(2);
        if (br.overrun) return false;
        if (type == 0) {
            br.align();
            if (br.n - br.pos < 4) return false;
            const size_t len = size_t(br.p[br.pos]) | size_t(br.p[br.pos + 1]) << 8;
            const size_t nlen = size_t(br.p[br.pos + 2]) | size_t(br.p[br.pos + 3]) << 8;
            br.pos += 4;
            if (len != (~nlen & 0xFFFF) || br.n - br.pos < len || len > limit - out.size()) return false;
            out.insert(out.end(), br.p + br.pos, br.p + br.pos + len);
            br.pos += len;
        } else if (type == 1) {
            if (!codes(br, out, limit, fixed.lit, fixed.dist)) return false;
        } else if (type == 2) {
            const int nlit = int(br.bits(5)) + 257, ndist = int(br.bits(5)) + 1, nclen = int(br.bits(4)) + 4;
            if (br.overrun || nlit > 286 || ndist > 30) return false;
            static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            uint8_t lengths[320] = {};
            for (int i = 0; i < nclen; ++i) lengths[order[i]] = uint8_t(br.bits(3));
            Huffman clen;
            if (br.overrun || !build(clen, lengths, 19)) return false;
            // The literal/length and distance code lengths come as one run,
            // and a repeat may cross from one set into the other.
            int i = 0;
            while (i < nlit + ndist) {
                int sym = decode(br, clen);
                if (sym < 0 || br.overrun) return false;
                if (sym < 16) { lengths[i++] = uint8_t(sym); continue; }
                uint8_t value = 0;
                int repeat;
                if (sym == 16) {
                    if (i == 0) return false;
                    value = lengths[i - 1];
                    repeat = 3 + int(br.bits(2));
                } else if (sym == 17) {
                    repeat = 3 + int(br.bits(3));
                } else {
                    repeat = 11 + int(br.bits(7));
                }
                if (br.overrun || i + repeat > nlit + ndist) return false;
                while (repeat--) lengths[i++] = value;
            }
            if (lengths[256] == 0) return false;  // no end-of-block code
            Huffman lit, dist;
            if (!build(lit, lengths, nlit) || !build(dist, lengths + nlit, ndist)) return false;
            if (!codes(br, out, limit, lit, dist)) return false;
        } else {
            return false;
        }
    }
    return true;
}

// --- PNG ---------------------------------------------------------------------

constexpr int kMaxDimension = 8192;

int paeth(int a, int b, int c) {
    const int p = a + b - c;
    const int pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return a;
    return pb <= pc ? b : c;
}

uint32_t argb(uint32_t a, uint32_t r, uint32_t g, uint32_t b) { return a << 24 | r << 16 | g << 8 | b; }

} // namespace

bool write_png(const std::string& path, const uint32_t* px, int w, int h, int stride) {
    std::vector<uint8_t> raw;
    raw.reserve(size_t(h) * (size_t(w) * 3 + 1));
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        for (int x = 0; x < w; ++x) {
            uint32_t p = px[y * stride + x];
            raw.push_back(uint8_t(p >> 16)); raw.push_back(uint8_t(p >> 8)); raw.push_back(uint8_t(p));
        }
    }
    std::vector<uint8_t> out = png_header(w, h, 8, 2);
    chunk(out, "IDAT", zlib_stored(raw));
    chunk(out, "IEND", {});
    return write_file(path, out);
}

bool write_png_indexed(const std::string& path, const uint8_t* indices, int w, int h,
                       const uint32_t* palette, int palette_size) {
    if (w <= 0 || h <= 0 || palette_size <= 0 || palette_size > 256) return false;
    std::vector<uint8_t> raw;
    raw.reserve(size_t(h) * (size_t(w) + 1));
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        for (int x = 0; x < w; ++x) {
            const uint8_t i = indices[size_t(y) * size_t(w) + size_t(x)];
            if (i >= palette_size) return false;
            raw.push_back(i);
        }
    }
    std::vector<uint8_t> plte, trns;
    for (int i = 0; i < palette_size; ++i) {
        plte.push_back(uint8_t(palette[i] >> 16)); plte.push_back(uint8_t(palette[i] >> 8)); plte.push_back(uint8_t(palette[i]));
        trns.push_back(uint8_t(palette[i] >> 24));
    }
    // Entries past the last translucent one default to opaque, so they need
    // not be written (and no tRNS at all when everything is opaque).
    while (!trns.empty() && trns.back() == 0xFF) trns.pop_back();
    std::vector<uint8_t> out = png_header(w, h, 8, 3);
    chunk(out, "PLTE", plte);
    if (!trns.empty()) chunk(out, "tRNS", trns);
    chunk(out, "IDAT", zlib_stored(raw));
    chunk(out, "IEND", {});
    return write_file(path, out);
}

bool decode_png(const uint8_t* data, size_t n, PngImage& out, std::string* error) {
    out = PngImage{};
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (n < 8 || std::memcmp(data, sig, 8) != 0) return fail(error, "not a PNG file");

    uint32_t w = 0, h = 0;
    int depth = 0, ctype = -1;
    bool have_ihdr = false, have_iend = false, have_trns = false;
    std::vector<uint8_t> plte, trns, idat;
    size_t pos = 8;
    while (!have_iend) {
        if (n - pos < 12) return fail(error, "PNG is truncated");
        const uint32_t len = get32(data + pos);
        if (len > n - pos - 12) return fail(error, "PNG is truncated");
        const uint8_t* type = data + pos + 4;
        const uint8_t* body = data + pos + 8;
        if (crc32(type, size_t(len) + 4) != get32(body + len)) return fail(error, "PNG chunk CRC mismatch (file is corrupt)");
        pos += size_t(len) + 12;

        if (std::memcmp(type, "IHDR", 4) == 0) {
            if (have_ihdr || len != 13) return fail(error, "bad PNG header");
            have_ihdr = true;
            w = get32(body);
            h = get32(body + 4);
            depth = body[8];
            ctype = body[9];
            if (body[10] != 0 || body[11] != 0) return fail(error, "unknown PNG compression or filter method");
            if (body[12] == 1) return fail(error, "interlaced (Adam7) PNGs are not supported; save it without interlacing");
            if (body[12] != 0) return fail(error, "unknown PNG interlace method");
            if (w == 0 || h == 0) return fail(error, "PNG has no pixels");
            if (w > kMaxDimension || h > kMaxDimension) return fail(error, "PNG is larger than 8192x8192");
            if (depth == 16) return fail(error, "16-bit PNGs are not supported; save it with 8 bits per channel");
            const bool low_depth_ok = ctype == 0 || ctype == 3;
            const bool depth_ok = depth == 8 || (low_depth_ok && (depth == 1 || depth == 2 || depth == 4));
            if (ctype != 0 && ctype != 2 && ctype != 3 && ctype != 4 && ctype != 6) return fail(error, "unknown PNG colour type");
            if (!depth_ok) return fail(error, "invalid PNG bit depth for its colour type");
        } else if (!have_ihdr) {
            return fail(error, "PNG does not start with its header");
        } else if (std::memcmp(type, "PLTE", 4) == 0) {
            if (len == 0 || len % 3 != 0 || len > 768) return fail(error, "bad PNG palette");
            plte.assign(body, body + len);
        } else if (std::memcmp(type, "tRNS", 4) == 0) {
            have_trns = true;
            trns.assign(body, body + len);
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            idat.insert(idat.end(), body, body + len);
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            have_iend = true;
        } else if (!(type[0] & 0x20)) {
            // An uppercase first letter marks a chunk needed to show the
            // image correctly; one we do not know means we cannot.
            return fail(error, "PNG uses an unsupported critical chunk");
        }
    }
    if (!have_ihdr) return fail(error, "PNG has no header");
    if (idat.empty()) return fail(error, "PNG has no image data");
    if (ctype == 3 && plte.empty()) return fail(error, "indexed PNG has no palette");

    const int channels = ctype == 2 ? 3 : ctype == 4 ? 2 : ctype == 6 ? 4 : 1;
    const size_t pixel_bits = size_t(channels) * size_t(depth);
    const size_t row_bytes = (size_t(w) * pixel_bits + 7) / 8;
    const size_t bpp = pixel_bits < 8 ? 1 : pixel_bits / 8;  // filter's byte distance
    const size_t raw_size = (row_bytes + 1) * h;

    // zlib wrapper: deflate, a window no bigger than 32K, no preset dictionary.
    if (idat.size() < 6) return fail(error, "PNG image data is truncated");
    if ((idat[0] & 0x0F) != 8 || (idat[0] >> 4) > 7 || (idat[1] & 0x20) || ((idat[0] << 8) | idat[1]) % 31 != 0)
        return fail(error, "PNG image data is not a valid zlib stream");
    std::vector<uint8_t> raw;
    raw.reserve(raw_size);
    BitReader br{idat.data(), idat.size()};
    br.pos = 2;
    if (!inflate(br, raw, raw_size)) return fail(error, "PNG image data is corrupt or truncated");
    br.align();
    if (br.n - br.pos < 4) return fail(error, "PNG image data is truncated");
    if (adler32(raw.data(), raw.size()) != get32(br.p + br.pos)) return fail(error, "PNG image data checksum mismatch (file is corrupt)");
    if (raw.size() != raw_size) return fail(error, "PNG image data is the wrong size");

    // Undo the per-row filters in place; row y's bytes start at y*(row_bytes+1)+1.
    for (size_t y = 0; y < h; ++y) {
        uint8_t* row = raw.data() + y * (row_bytes + 1);
        const uint8_t filter = row[0];
        uint8_t* cur = row + 1;
        const uint8_t* prev = y ? cur - (row_bytes + 1) : nullptr;
        for (size_t i = 0; i < row_bytes; ++i) {
            const int a = i >= bpp ? cur[i - bpp] : 0;
            const int b = prev ? prev[i] : 0;
            const int c = prev && i >= bpp ? prev[i - bpp] : 0;
            int pred;
            switch (filter) {
            case 0: pred = 0; break;
            case 1: pred = a; break;
            case 2: pred = b; break;
            case 3: pred = (a + b) / 2; break;
            case 4: pred = paeth(a, b, c); break;
            default: return fail(error, "PNG uses an unknown row filter");
            }
            cur[i] = uint8_t(cur[i] + pred);
        }
    }

    out.width = int(w);
    out.height = int(h);
    out.argb.resize(size_t(w) * h);
    if (ctype == 3) {
        out.indexed = true;
        out.indices.resize(size_t(w) * h);
        for (size_t i = 0; i < plte.size() / 3; ++i) {
            const uint32_t a = i < trns.size() ? trns[i] : 0xFF;
            out.palette.push_back(argb(a, plte[i * 3], plte[i * 3 + 1], plte[i * 3 + 2]));
        }
    }
    const int grey_max = (1 << depth) - 1;
    const uint32_t trns_grey = trns.size() >= 2 ? uint32_t(trns[0]) << 8 | trns[1] : 0x10000;
    for (size_t y = 0; y < h; ++y) {
        const uint8_t* row = raw.data() + y * (row_bytes + 1) + 1;
        for (size_t x = 0; x < w; ++x) {
            uint32_t& px = out.argb[y * w + x];
            if (depth < 8) {
                // Samples are packed from the most significant bit down.
                const size_t bit = x * size_t(depth);
                const int v = (row[bit / 8] >> (8 - depth - int(bit % 8))) & grey_max;
                if (ctype == 3) {
                    if (size_t(v) >= out.palette.size()) return fail(error, "PNG pixel uses a colour past the end of its palette");
                    out.indices[y * w + x] = uint8_t(v);
                    px = out.palette[size_t(v)];
                } else {
                    const uint32_t g = uint32_t(v * 255 / grey_max);
                    px = argb(have_trns && uint32_t(v) == trns_grey ? 0 : 0xFF, g, g, g);
                }
                continue;
            }
            const uint8_t* s = row + x * size_t(channels);
            switch (ctype) {
            case 0:
                px = argb(have_trns && s[0] == trns_grey ? 0 : 0xFF, s[0], s[0], s[0]);
                break;
            case 2: {
                const bool clear = have_trns && trns.size() >= 6 && trns[0] == 0 && trns[1] == s[0] &&
                                   trns[2] == 0 && trns[3] == s[1] && trns[4] == 0 && trns[5] == s[2];
                px = argb(clear ? 0 : 0xFF, s[0], s[1], s[2]);
                break;
            }
            case 3:
                if (s[0] >= out.palette.size()) return fail(error, "PNG pixel uses a colour past the end of its palette");
                out.indices[y * w + x] = s[0];
                px = out.palette[s[0]];
                break;
            case 4: px = argb(s[1], s[0], s[0], s[0]); break;
            default: px = argb(s[3], s[0], s[1], s[2]); break;
            }
        }
    }
    return true;
}

bool read_png(const std::string& path, PngImage& out, std::string* error) {
    out = PngImage{};
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return fail(error, "cannot open the file");
    std::vector<uint8_t> data;
    uint8_t buf[65536];
    size_t got;
    while ((got = std::fread(buf, 1, sizeof buf, f)) > 0) {
        data.insert(data.end(), buf, buf + got);
        // Far more than an 8192x8192 RGBA image needs even uncompressed.
        if (data.size() > (size_t(512) << 20)) {
            std::fclose(f);
            return fail(error, "file is too large to be a supported PNG");
        }
    }
    const bool read_error = std::ferror(f) != 0;
    std::fclose(f);
    if (read_error) return fail(error, "cannot read the file");
    return decode_png(data.data(), data.size(), out, error);
}

bool write_wav(const std::string& path, const std::vector<int16_t>& stereo, int rate) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    auto u32 = [&](uint32_t v) { uint8_t b[4] = {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)}; std::fwrite(b, 1, 4, f); };
    auto u16 = [&](uint16_t v) { uint8_t b[2] = {uint8_t(v), uint8_t(v >> 8)}; std::fwrite(b, 1, 2, f); };
    uint32_t data = uint32_t(stereo.size() * 2);
    std::fwrite("RIFF", 1, 4, f); u32(36 + data); std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(1); u16(2); u32(uint32_t(rate)); u32(uint32_t(rate) * 4); u16(4); u16(16);
    std::fwrite("data", 1, 4, f); u32(data);
    for (int16_t s : stereo) u16(uint16_t(s));
    return std::fclose(f) == 0;
}

uint64_t image_hash(const uint32_t* px, int w, int h, int stride) {
    uint64_t hsh = 1469598103934665603ull;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            uint32_t p = px[y * stride + x] & 0xFFFFFF;
            for (int k = 0; k < 3; ++k) { hsh ^= (p >> (8 * k)) & 0xFF; hsh *= 1099511628211ull; }
        }
    return hsh;
}

} // namespace chaotix
