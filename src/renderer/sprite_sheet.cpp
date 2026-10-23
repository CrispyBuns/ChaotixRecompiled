#include "sprite_sheet.h"

#include "image_io.h"
#include "runtime/sprite_replace.h"
#include "runtime/system.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace chaotix {

namespace {

constexpr uint32_t kRomBankTable = 0x0A0000;   // ROM: 128 frame table pointers (SH-2 0x020A0000)
constexpr uint32_t kRomPalettes = 0x0018E4;    // ROM: 8 68K pointers to each character's 18 colours
constexpr uint32_t kSdramBankTable = 0x3614;   // SDRAM: the scene's copy of the bank table
constexpr int kMaxSheetSide = 8192;

inline uint16_t be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
inline uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }

inline uint32_t argb555(uint16_t c) {
    uint32_t r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
    r = (r << 3) | (r >> 2); g = (g << 3) | (g >> 2); b = (b << 3) | (b >> 2);
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

// SH-2 memory as the blitters read graphics from it: the ROM and, for a
// machine, SDRAM (cached or cache-through). A host pointer and how many
// bytes follow it there.
struct Memory {
    const std::vector<uint8_t>* rom = nullptr;
    const uint8_t* sdram = nullptr;
    const uint8_t* at(uint32_t a, uint32_t* avail) const {
        if ((a >> 29) > 1) return nullptr;
        const uint32_t p = a & 0x1FFFFFFF;
        if (p >= 0x02000000 && p < 0x02400000 && rom && !rom->empty()) {
            const uint32_t off = (p - 0x02000000) & uint32_t(rom->size() - 1);
            *avail = uint32_t(rom->size()) - off;
            return rom->data() + off;
        }
        if (sdram && p >= 0x06000000 && p < 0x08000000) {
            const uint32_t off = p & 0x3FFFF;
            *avail = 0x40000 - off;
            return sdram + off;
        }
        return nullptr;
    }
    bool read32(uint32_t a, uint32_t* v) const {
        uint32_t n = 0;
        const uint8_t* p = at(a, &n);
        if (!p || n < 4) return false;
        *v = be32(p);
        return true;
    }
    bool plausible(uint32_t a) const {
        uint32_t n = 0;
        return (a & 1) == 0 && at(a, &n) && n >= 10;
    }
};

inline uint32_t canonical(uint32_t a) { return a & 0xDFFFFFFF; }
inline bool in_rom(uint32_t a) { const uint32_t p = a & 0x1FFFFFFF; return p >= 0x02000000 && p < 0x02400000; }

// The frames of bank `b` of `table` (128 SH-2 pointers), by the rules of
// SpriteRecorder::rebuild_index, so that a sheet's frames are the names the
// recorder gives the graphics. False if any graphic does not read as one
// (a bank still compressed in ROM reads as nonsense).
bool read_bank(const Memory& mem, const uint32_t* table, int b, SpriteBank& out) {
    out = SpriteBank{};
    out.bank = b;
    const uint32_t t = canonical(table[b]);
    if (!mem.plausible(t)) return false;
    uint32_t end = ~0u;
    for (int i = 0; i < 128; ++i) {
        const uint32_t s = canonical(table[i]);
        if (mem.plausible(s) && s > t) end = std::min(end, s);
    }
    std::vector<uint32_t> addr;
    for (uint32_t f = 0; f < 256; ++f) {
        const uint32_t a = t + 4 * f;
        uint32_t v = 0;
        if (a >= end || !mem.read32(a, &v)) break;
        if (v == 0xFFFFFFFFu) { addr.push_back(0); continue; }
        v = canonical(v);
        if (!mem.plausible(v)) break;
        if (v > t && in_rom(v) == in_rom(t)) end = std::min(end, v);
        addr.push_back(v);
    }
    bool any = false;
    for (size_t f = 0; f < addr.size(); ++f) {
        BankFrame fr;
        if (addr[f]) {
            const auto first = std::find(addr.begin(), addr.begin() + std::ptrdiff_t(f), addr[f]);
            if (first != addr.begin() + std::ptrdiff_t(f)) {
                // The recorder names a graphic by its first slot, so this
                // one is replaced through that frame's cell.
                fr.kind = BankFrame::Same;
                fr.same = int(first - addr.begin());
            } else {
                uint32_t avail = 0;
                const uint8_t* g = mem.at(addr[f], &avail);
                const uint32_t size = g ? SpriteRecorder::graphic_size(g, std::min<uint32_t>(avail, 0x10000)) : 0;
                if (!size || !graphic_image(g, size, fr.image) || fr.image.width > 256 || fr.image.height > 256)
                    return false;
                fr.kind = BankFrame::Graphic;
                fr.x0 = int16_t(be16(g));
                fr.x1 = int16_t(be16(g + 2));
                fr.y0 = int8_t(uint8_t(be16(g + 4) >> 8));
                fr.y1 = int8_t(uint8_t(be16(g + 6) >> 8));
                for (const uint8_t v : fr.image.px)
                    if (v >= kSheetOutside) return false;  // the sheet's own indices
                any = true;
            }
        }
        out.frames.push_back(std::move(fr));
    }
    // Trailing empty slots name nothing.
    while (!out.frames.empty() && out.frames.back().kind == BankFrame::Empty) out.frames.pop_back();
    return any;
}

void guide_colours(std::array<uint32_t, 256>& pal) {
    pal[kSheetOutside] = 0xFF383848u;  // a dark slate: room the graphic may grow into
    pal[kSheetGrid] = 0xFFFF00FFu;     // magenta, which no character wears
}

bool parse_int(const std::string& s, int* v) {
    if (s.empty() || s.size() > 7) return false;
    char* end = nullptr;
    const long n = std::strtol(s.c_str(), &end, 10);
    if (*end) return false;
    *v = int(n);
    return true;
}

bool parse_hex2(const std::string& s, int* v) {
    if (s.size() != 2 || !std::isxdigit(static_cast<unsigned char>(s[0])) || !std::isxdigit(static_cast<unsigned char>(s[1])))
        return false;
    *v = int(std::strtol(s.c_str(), nullptr, 16));
    return true;
}

std::string hex2(int v) {
    char b[8];
    std::snprintf(b, sizeof b, "%02x", unsigned(v) & 0xFF);
    return b;
}

std::string hex16(uint64_t v) {
    char b[20];
    std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(v));
    return b;
}

bool parse_hex16(const std::string& s, uint64_t* v) {
    if (s.size() != 16) return false;
    uint64_t n = 0;
    for (const char ch : s) {
        const int c = std::tolower(static_cast<unsigned char>(ch));
        if (c >= '0' && c <= '9') n = n << 4 | unsigned(c - '0');
        else if (c >= 'a' && c <= 'f') n = n << 4 | unsigned(c - 'a' + 10);
        else return false;
    }
    *v = n;
    return true;
}

// FNV-1a of a cell's pixels, row by row, as SpriteRecorder::hash.
uint64_t cell_hash(const SheetCell& c, const uint8_t* px, int width) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (int y = c.y; y < c.y + c.h; ++y)
        for (int x = c.x; x < c.x + c.w; ++x) {
            h ^= px[size_t(y) * size_t(width) + size_t(x)];
            h *= 0x100000001b3ull;
        }
    return h;
}

std::string sheet_name(int bank, const char* ext) { return "32x_" + hex2(bank) + ext; }

bool read_file(const std::filesystem::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream s;
    s << f.rdbuf();
    out = s.str();
    return true;
}

} // namespace

bool read_rom_bank(const std::vector<uint8_t>& rom, int bank, SpriteBank& out, std::string* why) {
    auto fail = [why](const std::string& m) { if (why) *why = m; return false; };
    if (bank < 0 || bank > 15) return fail("only banks 0 to 15 (the characters) are drawn from the ROM as they are");
    if (rom.size() < kRomBankTable + 512) return fail("the ROM is too small");
    uint32_t table[128];
    for (int i = 0; i < 128; ++i) table[i] = be32(rom.data() + kRomBankTable + 4 * i);
    Memory mem;
    mem.rom = &rom;
    if (!read_bank(mem, table, bank, out)) return fail("no graphics");
    // The colours shown for the player: pixel p (1-18) in the character's
    // colour p, from the 68K table the level set-up loads them from.
    const uint32_t ptr = be32(rom.data() + kRomPalettes + 4 * uint32_t(bank & 7));
    const uint32_t off = ptr - 0x700000u;
    for (int i = 1; i < 256; ++i) out.palette[size_t(i)] = 0xFF000000u | uint32_t(0x40 + (i & 0x7F)) * 0x010101u;
    if (ptr >= 0x900000 && ptr < 0xA00000 && off + 36 <= rom.size())
        for (int p = 1; p <= 18; ++p) out.palette[size_t(p)] = argb555(be16(rom.data() + off + 2 * uint32_t(p - 1)));
    out.palette[0] = 0;
    guide_colours(out.palette);
    return true;
}

std::vector<SpriteBank> read_loaded_banks(const Machine& m) {
    std::vector<SpriteBank> banks;
    uint32_t table[128];
    for (int i = 0; i < 128; ++i) table[i] = be32(m.sdram + kSdramBankTable + 4 * i);
    Memory mem;
    mem.rom = &m.rom.data;
    mem.sdram = m.sdram;
    for (int b = 16; b < 128; ++b) {
        // A slot pointing at an earlier bank's table (several point at
        // Mighty's) holds graphics the recorder names by that bank.
        bool repeat = false;
        for (int e = 0; e < b && !repeat; ++e) repeat = canonical(table[e]) == canonical(table[b]);
        SpriteBank bank;
        if (repeat || !read_bank(mem, table, b, bank)) continue;
        bank.palette[0] = 0;
        for (int i = 1; i < 256; ++i) bank.palette[size_t(i)] = argb555(m.mars.pal[i]);
        guide_colours(bank.palette);
        banks.push_back(std::move(bank));
    }
    return banks;
}

SheetLayout sheet_layout(const SpriteBank& bank) {
    SheetLayout l;
    l.bank = bank.bank;
    int left = 0, right = 0, up = 0, down = 0;
    for (const BankFrame& f : bank.frames) {
        if (f.kind != BankFrame::Graphic) continue;
        left = std::max(left, -f.x0);
        right = std::max(right, f.x1);
        up = std::max(up, -f.y0);
        down = std::max(down, f.y1 + 1);
    }
    l.cell_w = std::max(1, left + right);
    l.cell_h = std::max(1, up + down);
    l.hot_x = left;
    l.hot_y = up;
    const int n = int(bank.frames.size());
    const int cols = std::max(1, std::min(n, kSheetColumns)), rows = std::max(1, (n + kSheetColumns - 1) / kSheetColumns);
    l.width = cols * (l.cell_w + 1) + 1;
    l.height = rows * (l.cell_h + 1) + 1;
    for (int i = 0; i < n; ++i) {
        const BankFrame& f = bank.frames[size_t(i)];
        SheetCell c;
        c.frame = i;
        c.kind = f.kind;
        c.same = f.same;
        c.x = 1 + (i % kSheetColumns) * (l.cell_w + 1);
        c.y = 1 + (i / kSheetColumns) * (l.cell_h + 1);
        c.w = l.cell_w;
        c.h = l.cell_h;
        if (f.kind == BankFrame::Graphic) { c.x0 = f.x0; c.x1 = f.x1; c.y0 = f.y0; c.y1 = f.y1; }
        l.cells.push_back(c);
    }
    return l;
}

std::vector<uint8_t> sheet_pixels(const SpriteBank& bank, const SheetLayout& l) {
    std::vector<uint8_t> px(size_t(l.width) * size_t(l.height), kSheetGrid);
    for (const SheetCell& c : l.cells) {
        if (c.kind == BankFrame::Empty) continue;
        // A repeated frame shows the graphic it repeats, so a run of frames
        // still reads as one.
        const BankFrame* f = &bank.frames[size_t(c.frame)];
        if (f->kind == BankFrame::Same) f = &bank.frames[size_t(f->same)];
        for (int y = 0; y < c.h; ++y)
            std::fill_n(px.begin() + std::ptrdiff_t(size_t(c.y + y) * size_t(l.width) + size_t(c.x)), c.w, kSheetOutside);
        const int bx = c.x + l.hot_x + f->x0, by = c.y + l.hot_y + f->y0;
        for (int y = 0; y < f->image.height; ++y)
            for (int x = 0; x < f->image.width; ++x)
                px[size_t(by + y) * size_t(l.width) + size_t(bx + x)] = f->image.px[size_t(y * f->image.width + x)];
    }
    return px;
}

void sheet_hash_cells(SheetLayout& l, const uint8_t* px) {
    for (SheetCell& c : l.cells) {
        c.hashed = c.kind == BankFrame::Graphic;
        if (c.hashed) c.hash = cell_hash(c, px, l.width);
    }
}

std::string sheet_sidecar(const SheetLayout& l) {
    std::ostringstream s;
    s << "# Knuckles' Chaotix Recompiled: every frame of bank " << hex2(l.bank) << " in " << sheet_name(l.bank, ".png")
      << " (docs/TEXTURE_MODS.md).\n"
         "# frame FF X Y W H x0 x1 y0 y1: frame FF's cell in the sheet (X, Y its top-left\n"
         "# pixel), and the graphic's box around the hotspot: columns x0 to x1 - 1, lines\n"
         "# y0 to y1. The hotspot is at (X, Y) + hotspot in every cell. \"empty\" is an\n"
         "# empty slot; \"same GG\" repeats frame GG's graphic and is replaced through it.\n"
         "# The last word of a graphic's line is a hash of its cell as exported: a cell\n"
         "# left as it was is not loaded, so the game draws its own graphic there.\n"
         "# Index 0 is transparent; 0xFE (outside the box) and 0xFF (the grid) are too.\n"
      << "chaotix-sprite-sheet 1\n"
      << "bank " << hex2(l.bank) << "\n"
      << "size " << l.width << " " << l.height << "\n"
      << "cell " << l.cell_w << " " << l.cell_h << "\n"
      << "hotspot " << l.hot_x << " " << l.hot_y << "\n";
    for (const SheetCell& c : l.cells) {
        s << "frame " << hex2(c.frame) << " " << c.x << " " << c.y << " " << c.w << " " << c.h;
        if (c.kind == BankFrame::Empty) s << " empty";
        else if (c.kind == BankFrame::Same) s << " same " << hex2(c.same);
        else s << " " << c.x0 << " " << c.x1 << " " << c.y0 << " " << c.y1;
        if (c.kind == BankFrame::Graphic && c.hashed) s << " " << hex16(c.hash);
        s << "\n";
    }
    return s.str();
}

bool parse_sheet_sidecar(const std::string& text, SheetLayout& out, std::string* why) {
    out = SheetLayout{};
    std::istringstream in(text);
    std::string line;
    int number = 0;
    bool header = false, have_size = false, have_cell = false, have_hot = false;
    std::set<int> frames;
    auto fail = [&](const std::string& m) {
        if (why) *why = number ? "line " + std::to_string(number) + ": " + m : m;
        return false;
    };
    while (std::getline(in, line)) {
        ++number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        std::istringstream words(line);
        std::vector<std::string> w;
        for (std::string t; words >> t;) w.push_back(t);
        if (w.empty()) continue;
        if (!header) {
            if (w.size() != 2 || w[0] != "chaotix-sprite-sheet") return fail("not a sprite sheet's description (chaotix-sprite-sheet 1 first)");
            if (w[1] != "1") return fail("version " + w[1] + " is not known (1 is)");
            header = true;
            continue;
        }
        auto ints = [&](size_t from, size_t n, int* v) {
            if (w.size() < from + n) return false;
            for (size_t i = 0; i < n; ++i)
                if (!parse_int(w[from + i], &v[i])) return false;
            return true;
        };
        if (w[0] == "bank") {
            if (w.size() != 2 || !parse_hex2(w[1], &out.bank) || out.bank > 0x7F) return fail("bank: two hex digits, 00 to 7f");
        } else if (w[0] == "size" || w[0] == "cell" || w[0] == "hotspot") {
            int v[2];
            if (w.size() != 3 || !ints(1, 2, v)) return fail(w[0] + ": two numbers");
            if (w[0] == "size") {
                if (v[0] < 1 || v[1] < 1 || v[0] > kMaxSheetSide || v[1] > kMaxSheetSide) return fail("size: out of range");
                out.width = v[0];
                out.height = v[1];
                have_size = true;
            } else if (w[0] == "cell") {
                if (v[0] < 1 || v[1] < 1 || v[0] > kMaxSheetSide || v[1] > kMaxSheetSide) return fail("cell: out of range");
                out.cell_w = v[0];
                out.cell_h = v[1];
                have_cell = true;
            } else {
                if (v[0] < 0 || v[1] < 0 || v[0] > kMaxSheetSide || v[1] > kMaxSheetSide) return fail("hotspot: out of range");
                out.hot_x = v[0];
                out.hot_y = v[1];
                have_hot = true;
            }
        } else if (w[0] == "frame") {
            SheetCell c;
            int r[4];
            if (w.size() < 7 || !parse_hex2(w[1], &c.frame) || !ints(2, 4, r))
                return fail("frame: FF X Y W H, then the box, empty or same GG");
            if (!frames.insert(c.frame).second) return fail("frame " + w[1] + " is described twice");
            c.x = r[0]; c.y = r[1]; c.w = r[2]; c.h = r[3];
            if (w.size() == 7 && w[6] == "empty") {
                c.kind = BankFrame::Empty;
            } else if (w.size() == 8 && w[6] == "same") {
                if (!parse_hex2(w[7], &c.same)) return fail("same: two hex digits");
                c.kind = BankFrame::Same;
            } else {
                int b[4];
                if ((w.size() != 10 && w.size() != 11) || !ints(6, 4, b))
                    return fail("frame " + w[1] + ": the box is x0 x1 y0 y1, then optionally the cell's hash");
                if (w.size() == 11 && !parse_hex16(w[10], &c.hash))
                    return fail("frame " + w[1] + ": the cell's hash is 16 hex digits");
                c.hashed = w.size() == 11;
                c.kind = BankFrame::Graphic;
                c.x0 = b[0]; c.x1 = b[1]; c.y0 = b[2]; c.y1 = b[3];
                if (c.x1 <= c.x0 || c.y1 < c.y0) return fail("frame " + w[1] + ": an empty box");
            }
            out.cells.push_back(c);
        } else {
            return fail("unknown entry '" + w[0] + "'");
        }
    }
    number = 0;
    if (!header) return fail("not a sprite sheet's description (chaotix-sprite-sheet 1 first)");
    if (out.bank < 0) return fail("no bank");
    if (!have_size || !have_cell || !have_hot) return fail("size, cell and hotspot are needed");
    for (const SheetCell& c : out.cells) {
        const std::string f = "frame " + hex2(c.frame);
        if (c.w < 1 || c.h < 1 || c.x < 0 || c.y < 0 || c.x + c.w > out.width || c.y + c.h > out.height)
            return fail(f + ": its cell is not inside the sheet");
        if (c.kind == BankFrame::Same) {
            const bool ok = std::any_of(out.cells.begin(), out.cells.end(),
                                        [&](const SheetCell& o) { return o.frame == c.same && o.kind == BankFrame::Graphic; });
            if (!ok) return fail(f + ": repeats frame " + hex2(c.same) + ", which is not a graphic");
        }
        if (c.kind != BankFrame::Graphic) continue;
        // The box, placed at the hotspot, must lie in the cell.
        if (out.hot_x + c.x0 < 0 || out.hot_x + c.x1 > c.w || out.hot_y + c.y0 < 0 || out.hot_y + c.y1 + 1 > c.h)
            return fail(f + ": its box does not fit in its cell around the hotspot");
    }
    return true;
}

bool sheet_replacements(const SheetLayout& l, const uint8_t* px, int width, int height, std::vector<SheetReplacement>& out,
                        std::vector<std::string>* warnings, std::string* why) {
    out.clear();
    if (width != l.width || height != l.height) {
        if (why) *why = "the image is " + std::to_string(width) + "x" + std::to_string(height) + ", its description says " +
                        std::to_string(l.width) + "x" + std::to_string(l.height);
        return false;
    }
    auto value = [&](int x, int y) {
        const uint8_t v = px[size_t(y) * size_t(width) + size_t(x)];
        return v == kSheetOutside || v == kSheetGrid ? uint8_t(0) : v;
    };
    for (const SheetCell& c : l.cells) {
        if (c.kind != BankFrame::Graphic) continue;
        // Unedited: the game's own graphic is drawn, at no cost, even where
        // the game rewrites it while the scene runs.
        if (c.hashed && cell_hash(c, px, width) == c.hash) continue;
        const int bx = c.x + l.hot_x + c.x0, by = c.y + l.hot_y + c.y0;
        int w = c.x1 - c.x0, h = c.y1 - c.y0 + 1;
        // Pixels painted right of or below the box, within the cell, make
        // the replacement larger from the same top-left corner.
        bool outside = false;
        for (int y = c.y; y < c.y + c.h; ++y)
            for (int x = c.x; x < c.x + c.w; ++x) {
                if (!value(x, y)) continue;
                if (x < bx || y < by) { outside = true; continue; }
                w = std::max(w, x - bx + 1);
                h = std::max(h, y - by + 1);
            }
        if (outside && warnings)
            warnings->push_back("frame " + hex2(c.frame) + ": pixels left of or above its box are left out "
                                "(a replacement grows only to the right and down)");
        SheetReplacement r;
        r.frame = c.frame;
        r.image.width = w;
        r.image.height = h;
        r.image.px.resize(size_t(w) * size_t(h));
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) r.image.px[size_t(y * w + x)] = value(bx + x, by + y);
        out.push_back(std::move(r));
    }
    return true;
}

int export_sprite_sheets(const Machine& m, const std::string& dir, std::vector<std::string>* messages) {
    auto say = [messages](std::string s) { if (messages) messages->push_back(std::move(s)); };
    std::vector<SpriteBank> banks;
    for (int b = 0; b < 16; ++b) {
        SpriteBank bank;
        if (read_rom_bank(m.rom.data, b, bank, nullptr)) banks.push_back(std::move(bank));
    }
    for (auto& bank : read_loaded_banks(m)) banks.push_back(std::move(bank));
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::u8path(dir), ec);
    int written = 0;
    for (const SpriteBank& bank : banks) {
        SheetLayout l = sheet_layout(bank);
        const std::vector<uint8_t> px = sheet_pixels(bank, l);
        sheet_hash_cells(l, px.data());
        const std::string png = dir + "/" + sheet_name(bank.bank, ".png");
        const std::string txt = dir + "/" + sheet_name(bank.bank, ".txt");
        std::ofstream f(std::filesystem::u8path(txt), std::ios::binary);
        f << sheet_sidecar(l);
        f.close();
        if (!f || !write_png_indexed(png, px.data(), l.width, l.height, bank.palette.data(), 256)) {
            say(sheet_name(bank.bank, ".png") + ": cannot be written");
            continue;
        }
        size_t graphics = 0;
        for (const SheetCell& c : l.cells) graphics += c.kind == BankFrame::Graphic;
        say(sheet_name(bank.bank, ".png") + ": " + std::to_string(l.cells.size()) + " frames, " + std::to_string(graphics) +
            " graphics");
        ++written;
    }
    return written;
}

int load_sprite_sheets(SpritePack& pack, const std::string& dir, std::vector<std::string>* messages) {
    auto say = [messages](std::string m) { if (messages) messages->push_back(std::move(m)); };
    namespace fs = std::filesystem;
    std::error_code ec;
    int loaded = 0;
    for (const auto& e : fs::directory_iterator(fs::u8path(dir), ec)) {
        const std::string name = e.path().filename().u8string();
        int bank = 0;
        if (!e.is_regular_file(ec) || name.size() != 10 || name.compare(0, 4, "32x_") != 0 ||
            name.compare(6, 4, ".png") != 0)
            continue;
        if (!parse_hex2(name.substr(4, 2), &bank)) { say(name + ": not a sheet's name (32x_<bank>.png, in hex)"); continue; }
        if (bank > 0x7F) { say(name + ": the bank table has banks 00 to 7f"); continue; }
        fs::path side = e.path();
        side.replace_extension(".txt");
        std::string text, why;
        if (!read_file(side, text)) { say(name + ": a sheet needs its description, " + side.filename().u8string()); continue; }
        SheetLayout l;
        if (!parse_sheet_sidecar(text, l, &why)) { say(side.filename().u8string() + ": " + why); continue; }
        if (l.bank != bank) { say(side.filename().u8string() + ": describes bank " + hex2(l.bank) + ", not " + hex2(bank)); continue; }
        PngImage img;
        if (!read_png(e.path().u8string(), img, &why)) { say(name + ": " + why); continue; }
        if (!img.indexed) { say(name + ": must be a palette-indexed PNG, like the exported sheet"); continue; }
        std::vector<SheetReplacement> reps;
        std::vector<std::string> warnings;
        if (!sheet_replacements(l, img.indices.data(), img.width, img.height, reps, &warnings, &why)) {
            say(name + ": " + why);
            continue;
        }
        for (const auto& w : warnings) say(name + ": " + w);
        for (auto& r : reps) pack.add(bank, r.frame, std::move(r.image));
        loaded += int(reps.size());
    }
    return loaded;
}

} // namespace chaotix
