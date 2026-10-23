#include "text_replace.h"

#include "tile_replace.h"
#include "vdp.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace chaotix {

namespace {

struct Geometry {
    int pw = 0, ph = 0;  // cells
    uint32_t nt = 0;     // nametable address
};

Geometry geometry(const Vdp& v, int plane) {
    Geometry g;
    if (plane == 2) {
        const bool h40 = (v.reg[12] & 1) != 0;
        g.nt = h40 ? (uint32_t(v.reg[3] & 0x3C) << 10) : (uint32_t(v.reg[3] & 0x3E) << 10);
        g.pw = h40 ? 64 : 32;
        g.ph = 32;
        return g;
    }
    static const int sizes[4] = {32, 64, 32, 128};
    g.pw = sizes[v.reg[16] & 3];
    g.ph = sizes[(v.reg[16] >> 4) & 3];
    if ((v.reg[16] & 3) == 3 && g.ph > 32) g.ph = 32;  // as Vdp::render_line
    g.nt = plane == 0 ? (uint32_t(v.reg[2] & 0x38) << 10) : (uint32_t(v.reg[4] & 0x07) << 13);
    return g;
}

uint16_t entry_at(const Vdp& v, const Geometry& g, int cx, int cy) {
    const uint32_t ea = g.nt + uint32_t((cy * g.pw + cx) * 2);
    return uint16_t((v.vram[ea & 0xFFFF] << 8) | v.vram[(ea + 1) & 0xFFFF]);
}

int pattern_cells(const TextReplacer::Pattern& p) { return p.rows * p.cols; }

} // namespace

std::string TextReplacer::token(uint64_t hash, int flips) {
    char s[32];
    static const char* const suffix[4] = {"", ":h", ":v", ":hv"};
    std::snprintf(s, sizeof s, "%016llx%s", static_cast<unsigned long long>(hash), suffix[flips & 3]);
    return s;
}

bool TextReplacer::parse_token(const std::string& s, uint64_t& hash, uint8_t& flips) {
    if (s.size() < 16) return false;
    hash = 0;
    for (int i = 0; i < 16; ++i) {
        const char c = s[size_t(i)];
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        hash = (hash << 4) | uint64_t(d);
    }
    const std::string rest = s.substr(16);
    if (rest.empty()) flips = 0;
    else if (rest == ":h" || rest == ":H") flips = 1;
    else if (rest == ":v" || rest == ":V") flips = 2;
    else if (rest == ":hv" || rest == ":vh" || rest == ":HV" || rest == ":VH") flips = 3;
    else return false;
    return true;
}

bool TextReplacer::add(Pattern p, std::string* error) {
    auto fail = [error](const char* m) { if (error) *error = m; return false; };
    if (p.rows < 1 || p.cols < 1 || p.rows > 32 || p.cols > 128) return fail("a pattern is 1 to 32 rows of 1 to 128 tiles");
    if (p.hashes.size() != size_t(p.rows * p.cols) || p.flips.size() != p.hashes.size())
        return fail("every row must have the same number of tiles");
    if (p.height != p.rows * 8) return fail("the image must be 8 pixels high per row of tiles");
    if (p.width < 1 || p.width > 1024) return fail("the image must be 1 to 1024 pixels wide");
    if (p.indices.size() != size_t(p.width * p.height)) return fail("the image's pixels are missing");
    if (p.palette > 3 || (p.planes & kAnyPlane) == 0) return fail("bad plane or palette");
    for (auto& i : p.indices) i &= 15;
    by_first_[p.hashes[0]].push_back(int(patterns_.size()));
    patterns_.push_back(std::move(p));
    for (auto& pl : planes_) { pl.any = false; pl.hits.clear(); }  // their pointers may be stale
    for (auto& sp : seen_planes_) sp.wanted = false;  // until the next refresh
    return true;
}

void TextReplacer::refresh(const Vdp& v, const uint64_t* slot_hash, bool dumping) {
    ++frames_;
    if (!patterns_.empty()) {
        first_.assign(TileReplacer::kSlots, 0);
        for (int s = 0; s < TileReplacer::kSlots; ++s) first_[size_t(s)] = by_first_.count(slot_hash[s]) ? 1 : 0;
        for (int pl = 0; pl < 3; ++pl) match(v, pl, slot_hash);
    }
    if (dumping) dump(v, slot_hash);
}

void TextReplacer::update_line(const Vdp& v, int line, bool window, const uint64_t* slot_hash) {
    if (patterns_.empty() || first_.empty()) return;
    for (int pl = 0; pl < 3; ++pl) {
        const Seen& S = seen_planes_[size_t(pl)];
        if (!S.wanted || (pl == 2 && (!window || line < 0 || line >= 256))) continue;
        const Geometry g = geometry(v, pl);
        if (g.nt != S.nt || g.pw != S.pw || g.ph != S.ph) {
            match(v, pl, slot_hash);
            continue;
        }
        // The rows this line shows (one, or one per 16-pixel column with
        // per-column V-scroll, as render_plane reads them), and the rows a
        // pattern on them could span.
        uint64_t check[2] = {};  // up to 128 rows
        auto show = [&](int row) {
            for (int k = -(S.rows - 1); k <= S.rows - 1; ++k) {
                const int r = ((row + k) % g.ph + g.ph) % g.ph;
                check[r >> 6] |= uint64_t(1) << (r & 63);
            }
        };
        if (pl == 2) show(line >> 3);
        else if (v.reg[11] & 4) {
            for (int col = 0; col < 20; ++col) show(((line + v.vsram[col * 2 + pl]) & (g.ph * 8 - 1)) >> 3);
        } else {
            show(((line + v.vsram[pl]) & (g.ph * 8 - 1)) >> 3);
        }
        const size_t row_bytes = size_t(g.pw) * 2;
        bool changed = false;
        for (int r = 0; r < g.ph && !changed; ++r) {
            if (!((check[r >> 6] >> (r & 63)) & 1)) continue;
            const size_t at = g.nt + size_t(r) * row_bytes;
            const uint8_t* old = S.bytes.data() + size_t(r) * row_bytes;
            if (at + row_bytes <= 0x10000) changed = std::memcmp(v.vram + at, old, row_bytes) != 0;
            else
                for (size_t i = 0; i < row_bytes && !changed; ++i) changed = v.vram[(at + i) & 0xFFFF] != old[i];
        }
        if (changed) match(v, pl, slot_hash);
    }
}

void TextReplacer::match(const Vdp& v, int pl, const uint64_t* slot_hash) {
    Plane& P = planes_[size_t(pl)];
    P.any = false;
    P.hits.clear();
    const uint8_t bit = uint8_t(1 << pl);
    Seen& S = seen_planes_[size_t(pl)];
    S.wanted = false;
    S.rows = 1;
    for (const auto& p : patterns_)
        if (p.planes & bit) {
            S.wanted = true;
            S.rows = std::max(S.rows, p.rows);
        }
    if (!S.wanted) return;
    const Geometry g = geometry(v, pl);
    const int n = g.pw * g.ph;
    P.pw = g.pw;
    P.ph = g.ph;
    // What was matched, for update_line (a name table past the end of VRAM
    // wraps, as entry_at reads it).
    S.nt = g.nt;
    S.pw = g.pw;
    S.ph = g.ph;
    S.bytes.resize(size_t(n) * 2);
    for (size_t i = 0; i < S.bytes.size(); ++i) S.bytes[i] = v.vram[(g.nt + i) & 0xFFFF];

    struct Cand { int pi, cx, cy; };
    std::vector<Cand> cands;
    for (int cy = 0; cy < g.ph; ++cy)
        for (int cx = 0; cx < g.pw; ++cx) {
            const uint16_t e0 = entry_at(v, g, cx, cy);
            if (!first_[e0 & 0x7FF]) continue;
            const auto it = by_first_.find(slot_hash[e0 & 0x7FF]);
            if (it == by_first_.end()) continue;
            for (int pi : it->second) {
                const Pattern& p = patterns_[size_t(pi)];
                if (!(p.planes & bit) || p.rows > g.ph || p.cols > g.pw) continue;
                bool ok = true;
                for (int r = 0; r < p.rows && ok; ++r)
                    for (int c = 0; c < p.cols && ok; ++c) {
                        const uint16_t e = entry_at(v, g, (cx + c) % g.pw, (cy + r) % g.ph);
                        const size_t k = size_t(r * p.cols + c);
                        ok = slot_hash[e & 0x7FF] == p.hashes[k] && ((e >> 11) & 3) == p.flips[k] &&
                             (p.palette < 0 || ((e >> 13) & 3) == p.palette);
                    }
                if (ok) cands.push_back({pi, cx, cy});
            }
        }
    if (cands.empty()) return;
    // Longest first; ties in the order the patterns were loaded, then by place.
    std::stable_sort(cands.begin(), cands.end(), [&](const Cand& a, const Cand& b) {
        const int la = pattern_cells(patterns_[size_t(a.pi)]), lb = pattern_cells(patterns_[size_t(b.pi)]);
        if (la != lb) return la > lb;
        return a.pi < b.pi;
    });
    P.run.assign(size_t(n), -1);
    P.entry.assign(size_t(n), 0);
    P.over.assign(size_t(n), -1);
    const int wpx = g.pw * 8;
    for (const Cand& c : cands) {
        const Pattern& p = patterns_[size_t(c.pi)];
        bool free = true;
        for (int r = 0; r < p.rows && free; ++r)
            for (int k = 0; k < p.cols && free; ++k)
                free = P.run[size_t(((c.cy + r) % g.ph) * g.pw + (c.cx + k) % g.pw)] < 0;
        if (!free || P.hits.size() >= 0x7FFF) continue;
        const int h = int(P.hits.size());
        const uint16_t e0 = entry_at(v, g, c.cx, c.cy);
        int off = 0;
        if (p.align == Align::Center) off = (p.cols * 8 - p.width) / 2;
        else if (p.align == Align::Right) off = p.cols * 8 - p.width;
        const int ox = ((c.cx * 8 + off) % wpx + wpx) % wpx;
        const uint32_t ea0 = (g.nt + uint32_t((c.cy * g.pw + c.cx) * 2)) & 0xFFFF;
        P.hits.push_back({ox, c.cy * 8, &p, uint8_t((e0 >> 9) & 0x30), uint8_t((e0 >> 15) & 1), ea0});
        for (int r = 0; r < p.rows; ++r)
            for (int k = 0; k < p.cols; ++k) {
                const int cx = (c.cx + k) % g.pw, cy = (c.cy + r) % g.ph;
                const size_t i = size_t(cy * g.pw + cx);
                P.run[i] = int16_t(h);
                P.entry[i] = entry_at(v, g, cx, cy);
            }
    }
    // Images wider than their run (or placed off it) reach over the cells
    // beside it; those keep their tiles, under the image's non-zero pixels.
    for (int h = 0; h < int(P.hits.size()); ++h) {
        const Hit& hit = P.hits[size_t(h)];
        const int span = std::min(g.pw, ((hit.ox & 7) + hit.p->width + 7) / 8);
        for (int r = 0; r < hit.p->rows; ++r)
            for (int k = 0; k < span; ++k) {
                const size_t i = size_t(((hit.oy / 8 + r) % g.ph) * g.pw + (hit.ox / 8 + k) % g.pw);
                if (P.run[i] < 0 && P.over[i] < 0) P.over[i] = int16_t(h);
            }
    }
    P.any = !P.hits.empty();
}

void TextReplacer::dump(const Vdp& v, const uint64_t* slot_hash) {
    static const uint8_t zeros[32] = {};
    static const uint64_t blank = TileReplacer::hash(zeros);
    const int width = v.width(), height = v.height();

    struct Cell { int sx, sy, cx, cy; uint16_t e; bool blank; };
    struct Run { char plane; int sx, sy, cx, cy; std::vector<Cell> cells; };
    std::vector<Run> runs;

    for (int pl = 0; pl < 3; ++pl) {
        const Geometry g = geometry(v, pl);
        const char name = "ABW"[pl];
        for (int r = 0; r * 8 < height; ++r) {
            const int line = r * 8 + 4;
            // The window's columns on this line, as Vdp::render_line has them.
            int win_x0 = 0, win_x1 = 0;
            {
                const int wvp = (v.reg[18] & 0x1F) * 8;
                const bool down = (v.reg[18] & 0x80) != 0;
                if (down ? (line >= wvp) : (line < wvp)) { win_x1 = width; }
                else {
                    int whp = (v.reg[17] & 0x1F) * 16;
                    if (whp > width) whp = width;
                    if (v.reg[17] & 0x80) { win_x0 = whp; win_x1 = width; }
                    else win_x1 = whp;
                }
            }
            int hscroll = 0;
            if (pl < 2) {
                const uint32_t hs_base = uint32_t(v.reg[13] & 0x3F) << 10;
                uint32_t hs_addr;
                switch (v.reg[11] & 3) {
                case 2: hs_addr = hs_base + uint32_t(line & ~7) * 4; break;
                case 3: hs_addr = hs_base + uint32_t(line) * 4; break;
                default: hs_addr = hs_base; break;
                }
                if (pl == 1) hs_addr += 2;
                hscroll = ((v.vram[hs_addr & 0xFFFF] << 8) | v.vram[(hs_addr + 1) & 0xFFFF]) & 0x3FF;
            }
            std::vector<Cell> row;
            row.reserve(size_t(width / 8));
            for (int sc = 0; sc * 8 < width; ++sc) {
                const int x = sc * 8 + 4;
                const bool in_win = x >= win_x0 && x < win_x1;
                Cell c{};
                bool valid = true;
                if (pl == 2) {
                    valid = in_win;
                    c.cx = x >> 3; c.cy = line >> 3; c.sx = c.cx * 8; c.sy = c.cy * 8;
                } else {
                    valid = !(pl == 0 && in_win);
                    const int col = (x >> 4) > 19 ? 19 : (x >> 4);
                    const int vs = (v.reg[11] & 4) ? v.vsram[col * 2 + pl] : v.vsram[pl];
                    const int py = (line + vs) & (g.ph * 8 - 1);
                    const int px = (x - hscroll) & (g.pw * 8 - 1);
                    c.cx = px >> 3; c.cy = py >> 3;
                    c.sx = x - (px & 7); c.sy = line - (py & 7);
                }
                if (!valid) { c.cx = -1; row.push_back(c); continue; }
                c.e = entry_at(v, g, c.cx, c.cy);
                c.blank = slot_hash[c.e & 0x7FF] == blank;
                row.push_back(c);
            }
            // Runs of non-blank tiles, one blank tile inside allowed (a space
            // between words), broken by two blanks or a jump in the plane.
            Run cur{name, 0, 0, 0, 0, {}};
            std::vector<Cell> blanks;
            auto finish = [&]() {
                if (!cur.cells.empty()) {
                    cur.sx = cur.cells[0].sx; cur.sy = cur.cells[0].sy;
                    cur.cx = cur.cells[0].cx; cur.cy = cur.cells[0].cy;
                    runs.push_back(cur);
                }
                cur.cells.clear();
                blanks.clear();
            };
            const Cell* prev = nullptr;
            for (const Cell& c : row) {
                if (c.cx < 0) { finish(); prev = nullptr; continue; }
                if (prev && (c.cy != prev->cy || c.cx != (prev->cx + 1) % g.pw)) finish();
                prev = &c;
                if (c.blank) {
                    if (cur.cells.empty()) continue;
                    blanks.push_back(c);
                    if (blanks.size() >= 2) finish();
                    continue;
                }
                cur.cells.insert(cur.cells.end(), blanks.begin(), blanks.end());
                blanks.clear();
                cur.cells.push_back(c);
            }
            finish();
        }
    }

    // A screen is listed when it shows a run not listed before that has
    // stayed in place for kDumpSteady pictures: text on a menu does, while
    // a scrolling background shows new runs every picture and would
    // otherwise be listed over and over.
    std::vector<std::string> keys;
    std::vector<bool> steady;
    std::unordered_map<std::string, int> seen;
    bool fresh = false;
    for (const Run& r : runs) {
        std::string k(1, r.plane);
        for (const Cell& c : r.cells) k += " " + token(slot_hash[c.e & 0x7FF], (c.e >> 11) & 3);
        const std::string placed = k + " @" + std::to_string(r.sx) + "," + std::to_string(r.sy);
        const auto it = seen_.find(placed);
        const int count = std::min(kDumpSteady, (it == seen_.end() ? 0 : it->second) + 1);
        seen[placed] = count;
        steady.push_back(count >= kDumpSteady);
        fresh |= steady.back() && !dumped_runs_.count(k);
        keys.push_back(std::move(k));
    }
    seen_ = std::move(seen);
    if (!fresh || (dumped_once_ && frames_ - last_dump_ < uint64_t(kDumpGap))) return;
    dumped_once_ = true;
    last_dump_ = frames_;
    for (size_t i = 0; i < keys.size(); ++i)
        if (steady[i]) dumped_runs_.insert(keys[i]);

    Dump d;
    d.frame = frames_;
    char buf[1024];
    std::snprintf(buf, sizeof buf,
                  "# Mega Drive plane text on screen (%dx%d), picture %llu.\n"
                  "# Each run is a row of tiles a plane shows, numbered as in the PNG of the same name.\n"
                  "# To replace one, copy its \"plane\" and \"row\" lines into Mods/Textures/mdtext_<name>.txt\n"
                  "# and draw mdtext_<name>.png, palette-indexed, as high as the rows (8 pixels each).\n"
                  "# See docs/TEXTURE_MODS.md.\n\n",
                  width, height, static_cast<unsigned long long>(frames_));
    d.text = buf;
    for (size_t i = 0; i < runs.size(); ++i) {
        const Run& r = runs[i];
        const int id = int(i) + 1;
        unsigned lines = 0;
        for (const Cell& c : r.cells) lines |= 1u << ((c.e >> 13) & 3);
        std::string pals;
        for (int l = 0; l < 4; ++l)
            if (lines & (1u << l)) pals += (pals.empty() ? "" : ",") + std::to_string(l);
        const int w = int(r.cells.size());
        std::snprintf(buf, sizeof buf,
                      "# run %d: screen x %d y %d, %d tile%s (%dx8 pixels), plane cell %d,%d, palette line %s\n", id,
                      r.sx, r.sy, w, w == 1 ? "" : "s", w * 8, r.cx, r.cy, pals.c_str());
        d.text += buf;
        for (size_t j = 0; j < runs.size(); ++j)
            if (runs[j].plane == r.plane && runs[j].sx == r.sx && runs[j].sy == r.sy + 8 && runs[j].cells.size() == r.cells.size()) {
                std::snprintf(buf, sizeof buf,
                              "# run %d is right below it, as wide: for a two-tile-high font, use both rows\n",
                              int(j) + 1);
                d.text += buf;
            }
        d.text += std::string("plane ") + r.plane + "\nrow";
        for (const Cell& c : r.cells) d.text += " " + token(slot_hash[c.e & 0x7FF], (c.e >> 11) & 3);
        d.text += "\n\n";
        d.boxes.push_back({id, r.sx, r.sy, w * 8, 8, r.plane});
    }
    pending.push_back(std::move(d));
}

} // namespace chaotix
