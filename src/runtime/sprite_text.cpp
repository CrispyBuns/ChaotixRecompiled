#include "sprite_text.h"

#include "tile_replace.h"
#include "vdp.h"

#include <algorithm>
#include <cstdio>

namespace chaotix {

namespace {

// The bits of a sprite table entry's four words that say where it is and
// what it shows: Y; the size (not the link); the tile and flips (not the
// palette line or priority); X.
constexpr uint16_t kMask[4] = {0x03FF, 0x0F00, 0x1FFF, 0x01FF};

uint16_t word_at(const uint8_t* vram, uint32_t a) {
    return uint16_t((vram[a & 0xFFFF] << 8) | vram[(a + 1) & 0xFFFF]);
}

uint64_t tile_hash(const SpriteTextReplacer::Sprite& s, int k, const uint64_t* slot_hash) {
    return slot_hash[((s.pat & 0x7FF) + k) & 0x7FF];
}

} // namespace

std::string SpriteTextReplacer::size_token(int w, int h, int flips) {
    static const char* const suffix[4] = {"", ":h", ":v", ":hv"};
    return std::to_string(w) + "x" + std::to_string(h) + suffix[flips & 3];
}

bool SpriteTextReplacer::parse_size_token(const std::string& s, int& w, int& h, uint8_t& flips) {
    if (s.size() < 3 || s[0] < '1' || s[0] > '4' || (s[1] != 'x' && s[1] != 'X') || s[2] < '1' || s[2] > '4')
        return false;
    w = s[0] - '0';
    h = s[2] - '0';
    const std::string rest = s.substr(3);
    if (rest.empty()) flips = 0;
    else if (rest == ":h" || rest == ":H") flips = 1;
    else if (rest == ":v" || rest == ":V") flips = 2;
    else if (rest == ":hv" || rest == ":vh" || rest == ":HV" || rest == ":VH") flips = 3;
    else return false;
    return true;
}

bool SpriteTextReplacer::add(Pattern p, std::string* error) {
    auto fail = [error](const char* m) { if (error) *error = m; return false; };
    if (p.members.empty() || p.members.size() > size_t(kMaxSprites)) return fail("a pattern is 1 to 80 sprites");
    for (const auto& m : p.members) {
        if (m.w < 1 || m.w > 4 || m.h < 1 || m.h > 4) return fail("a sprite is 1 to 4 tiles wide and high");
        if (m.hashes.size() != size_t(m.w * m.h)) return fail("a sprite lists one tile per tile of its size");
    }
    if (p.width < 1 || p.width > 1024 || p.height < 1 || p.height > 512)
        return fail("the image must be 1 to 1024 pixels wide and 1 to 512 high");
    if (p.indices.size() != size_t(p.width * p.height)) return fail("the image's pixels are missing");
    if (p.palette > 3) return fail("bad palette");
    // Offsets relative to the first member; the members' bounding box.
    const int ax = p.members[0].dx, ay = p.members[0].dy;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    p.tiles = 0;
    for (size_t i = 0; i < p.members.size(); ++i) {
        auto& m = p.members[i];
        m.dx -= ax;
        m.dy -= ay;
        if (m.dx < -512 || m.dx > 512 || m.dy < -512 || m.dy > 512) return fail("sprites more than 512 pixels apart");
        for (size_t j = 0; j < i; ++j)
            if (p.members[j].dx == m.dx && p.members[j].dy == m.dy && p.members[j].hashes == m.hashes &&
                p.members[j].flips == m.flips)
                return fail("the same sprite twice at the same place");
        x0 = i ? std::min(x0, m.dx) : m.dx;
        y0 = i ? std::min(y0, m.dy) : m.dy;
        x1 = i ? std::max(x1, m.dx + m.w * 8) : m.dx + m.w * 8;
        y1 = i ? std::max(y1, m.dy + m.h * 8) : m.dy + m.h * 8;
        p.tiles += m.w * m.h;
    }
    int off = 0;
    if (p.align == Align::Center) off = (x1 - x0 - p.width) / 2;
    else if (p.align == Align::Right) off = x1 - x0 - p.width;
    p.image_x = x0 + off + p.offset_x;
    p.image_y = y0 + p.offset_y;
    for (auto& i : p.indices) i &= 15;
    for (size_t i = 0; i < p.members.size(); ++i) {
        auto& list = by_tile_[p.members[i].hashes[0]];
        if (std::find(list.begin(), list.end(), std::make_pair(int(patterns_.size()), int(i))) == list.end())
            list.emplace_back(int(patterns_.size()), int(i));
    }
    patterns_.push_back(std::move(p));
    hits_.clear();  // their pointers may be stale
    hit_of_.fill(-1);
    return true;
}

uint32_t SpriteTextReplacer::table_address(const Vdp& v) {
    const bool h40 = (v.reg[12] & 1) != 0;
    return h40 ? (uint32_t(v.reg[5] & 0x7E) << 9) : (uint32_t(v.reg[5] & 0x7F) << 9);
}

std::vector<SpriteTextReplacer::Sprite> SpriteTextReplacer::read_table(const Vdp& v) {
    // The list as Vdp::render_line follows it: from entry 0 along the links.
    std::vector<Sprite> out;
    const bool h40 = (v.reg[12] & 1) != 0;
    const int max_sprites = h40 ? 80 : 64;
    const uint32_t sat = table_address(v);
    bool seen[kMaxSprites] = {};
    int link = 0;
    for (int place = 0; place < max_sprites; ++place) {
        if (seen[link]) break;
        seen[link] = true;
        const uint32_t e = sat + uint32_t(link) * 8;
        const uint16_t y = word_at(v.vram, e), sz = word_at(v.vram, e + 2), pat = word_at(v.vram, e + 4),
                       x = word_at(v.vram, e + 6);
        // X 0 masks the sprites after it on its lines; it shows nothing itself.
        if ((x & 0x1FF) != 0)
            out.push_back({link, place, int(x & 0x1FF) - 128, int(y & 0x3FF) - 128, ((sz >> 10) & 3) + 1,
                           ((sz >> 8) & 3) + 1, pat, uint8_t((pat >> 11) & 3)});
        link = sz & 0x7F;
        if (link == 0 || link >= max_sprites) break;
    }
    return out;
}

bool SpriteTextReplacer::member_matches(const Member& m, const Sprite& s, const uint64_t* slot_hash) const {
    if (s.w != m.w || s.h != m.h || s.flips != m.flips) return false;
    for (int k = 0; k < m.w * m.h; ++k)
        if (tile_hash(s, k, slot_hash) != m.hashes[size_t(k)]) return false;
    return true;
}

void SpriteTextReplacer::refresh(const Vdp& v, const uint64_t* slot_hash, bool dumping) {
    ++frames_;
    if (patterns_.empty() && !dumping) return;
    const std::vector<Sprite> table = read_table(v);
    if (!patterns_.empty()) match(v, table, slot_hash);
    if (dumping) dump(v, table, slot_hash);
}

bool SpriteTextReplacer::off_screen(const Vdp& v, const Member& m, int x, int y) {
    const int left = -v.shown_extra, right = v.width() + v.shown_extra;
    const int top = 0, bottom = v.height() + v.shown_extra_bottom;
    return x + m.w * 8 <= left || x >= right || y + m.h * 8 <= top || y >= bottom;
}

void SpriteTextReplacer::match(const Vdp& v, const std::vector<Sprite>& table, const uint64_t* slot_hash) {
    hits_.clear();
    hit_of_.fill(-1);
    // A candidate: per member, its sprite (an index into `table`) or -1
    // when the member is left out, off screen; `ref` is the first present.
    struct Cand { int pi, ref, tiles; bool whole; std::vector<int> sprites; };
    std::vector<Cand> cands;
    for (size_t si = 0; si < table.size(); ++si) {
        const Sprite& s = table[si];
        const auto it = by_tile_.find(tile_hash(s, 0, slot_hash));
        if (it == by_tile_.end()) continue;
        for (const auto& [pi, mi] : it->second) {
            const Pattern& p = patterns_[size_t(pi)];
            const Member& ms = p.members[size_t(mi)];
            if (!member_matches(ms, s, slot_hash)) continue;
            if (p.palette >= 0 && ((s.pat >> 13) & 3) != p.palette) continue;
            Cand c{pi, mi, 0, true, std::vector<int>(p.members.size(), -1)};
            c.sprites[size_t(mi)] = int(si);
            bool ok = true;
            for (size_t mj = 0; mj < p.members.size() && ok; ++mj) {
                if (int(mj) == mi) continue;
                const Member& m = p.members[mj];
                // Where the VDP shows a sprite placed that far from s: X is
                // 9 bits (screen -128 to 383), Y 10 bits.
                const int x = ((s.x + 128 - ms.dx + m.dx) & 511) - 128;
                const int y = ((s.y + 128 - ms.dy + m.dy) & 1023) - 128;
                int found = -1;
                for (size_t ti = 0; ti < table.size() && found < 0; ++ti) {
                    const Sprite& t = table[ti];
                    if (t.x != x || t.y != y) continue;
                    if (std::find(c.sprites.begin(), c.sprites.end(), int(ti)) != c.sprites.end()) continue;
                    if (member_matches(m, t, slot_hash)) found = int(ti);
                }
                if (found >= 0) {
                    // Found from the first member present, not from this one.
                    ok = int(mj) > mi;
                    c.sprites[mj] = found;
                } else {
                    ok = off_screen(v, m, x, y);
                    c.whole = false;
                }
            }
            if (!ok) continue;
            for (size_t mj = 0; mj < p.members.size(); ++mj)
                if (c.sprites[mj] >= 0) c.tiles += p.members[mj].w * p.members[mj].h;
            cands.push_back(std::move(c));
        }
    }
    if (cands.empty()) return;
    // The most tiles present first, then whole groups; ties in the order the
    // patterns were loaded, then by the first present member's place in
    // the list.
    std::stable_sort(cands.begin(), cands.end(), [&](const Cand& a, const Cand& b) {
        if (a.tiles != b.tiles) return a.tiles > b.tiles;
        if (a.whole != b.whole) return a.whole;
        if (a.pi != b.pi) return a.pi < b.pi;
        return table[size_t(a.sprites[size_t(a.ref)])].place < table[size_t(b.sprites[size_t(b.ref)])].place;
    });
    const uint32_t sat = table_address(v);
    std::vector<bool> used(table.size(), false);
    for (const Cand& c : cands) {
        bool free = true;
        for (int si : c.sprites) free = free && (si < 0 || !used[size_t(si)]);
        if (!free) continue;
        const int h = int(hits_.size());
        const Pattern& p = patterns_[size_t(c.pi)];
        const Sprite& ref = table[size_t(c.sprites[size_t(c.ref)])];
        Hit hit{&p, uint8_t(ref.link), uint8_t(ref.place), p.members[size_t(c.ref)].dx, p.members[size_t(c.ref)].dy, {}};
        for (int si : c.sprites) {
            if (si < 0) continue;
            used[size_t(si)] = true;
            const int link = table[size_t(si)].link;
            hit.links.push_back(uint8_t(link));
            hit_of_[size_t(link)] = int8_t(h);
            for (int w = 0; w < 4; ++w) saved_[size_t(link)][size_t(w)] = word_at(v.vram, sat + uint32_t(link) * 8 + uint32_t(w) * 2);
        }
        hits_.push_back(std::move(hit));
    }
}

bool SpriteTextReplacer::unchanged(const uint8_t* vram, uint32_t sat, int link) const {
    for (int w = 0; w < 4; ++w)
        if ((word_at(vram, sat + uint32_t(link) * 8 + uint32_t(w) * 2) ^ saved_[size_t(link)][size_t(w)]) & kMask[w])
            return false;
    return true;
}

bool SpriteTextReplacer::unchanged_hit(const uint8_t* vram, uint32_t sat, int h) const {
    for (uint8_t link : hits_[size_t(h)].links)
        if (!unchanged(vram, sat, link)) return false;
    return true;
}

void SpriteTextReplacer::dump(const Vdp& v, const std::vector<Sprite>& table, const uint64_t* slot_hash) {
    static const uint8_t zeros[32] = {};
    static const uint64_t blank = TileReplacer::hash(zeros);
    const int width = v.width(), height = v.height();

    // The sprites on screen that show something.
    std::vector<const Sprite*> shown;
    for (const Sprite& s : table) {
        if (s.x + s.w * 8 <= 0 || s.x >= width || s.y + s.h * 8 <= 0 || s.y >= height) continue;
        bool any = false;
        for (int k = 0; k < s.w * s.h && !any; ++k) any = tile_hash(s, k, slot_hash) != blank;
        if (any) shown.push_back(&s);
    }
    // Suggested groups: sprites on the same line (same Y and height), with
    // the same palette line, side by side (at most 8 pixels apart, or
    // overlapping by at most 8), from left to right.
    std::vector<const Sprite*> sorted = shown;
    std::stable_sort(sorted.begin(), sorted.end(), [](const Sprite* a, const Sprite* b) {
        if (a->y != b->y) return a->y < b->y;
        if (a->h != b->h) return a->h < b->h;
        if (((a->pat >> 13) & 3) != ((b->pat >> 13) & 3)) return ((a->pat >> 13) & 3) < ((b->pat >> 13) & 3);
        if (a->x != b->x) return a->x < b->x;
        return a->place < b->place;
    });
    std::vector<std::vector<const Sprite*>> groups;
    for (const Sprite* s : sorted) {
        if (!groups.empty()) {
            const Sprite* last = groups.back().back();
            int right = 0;
            for (const Sprite* g : groups.back()) right = std::max(right, g->x + g->w * 8);
            const int gap = s->x - right;
            if (last->y == s->y && last->h == s->h && ((last->pat >> 13) & 3) == ((s->pat >> 13) & 3) && gap <= 8 &&
                gap >= -8) {
                groups.back().push_back(s);
                continue;
            }
        }
        groups.push_back({s});
    }
    auto tiles_of = [&](const Sprite& s) {
        std::string t;
        for (int k = 0; k < s.w * s.h; ++k) t += " " + TextReplacer::token(tile_hash(s, k, slot_hash), 0);
        return t;
    };
    auto line_of = [&](const Sprite& s, int dx, int dy) {
        return "sprite " + std::to_string(dx) + " " + std::to_string(dy) + " " + size_token(s.w, s.h, s.flips) + tiles_of(s);
    };

    // Listed as the plane text is: when a group not listed before has stayed
    // in place for kDumpSteady pictures.
    std::vector<std::string> keys;
    std::vector<bool> steady;
    std::unordered_map<std::string, int> seen;
    bool fresh = false;
    for (const auto& g : groups) {
        std::string k;
        for (const Sprite* s : g) k += line_of(*s, s->x - g[0]->x, s->y - g[0]->y) + "\n";
        const std::string placed = k + "@" + std::to_string(g[0]->x) + "," + std::to_string(g[0]->y);
        const auto it = seen_.find(placed);
        const int count = std::min(kDumpSteady, (it == seen_.end() ? 0 : it->second) + 1);
        seen[placed] = count;
        steady.push_back(count >= kDumpSteady);
        fresh |= steady.back() && !dumped_groups_.count(k);
        keys.push_back(std::move(k));
    }
    seen_ = std::move(seen);
    if (!fresh || (dumped_once_ && frames_ - last_dump_ < uint64_t(kDumpGap))) return;
    dumped_once_ = true;
    last_dump_ = frames_;
    for (size_t i = 0; i < keys.size(); ++i)
        if (steady[i]) dumped_groups_.insert(keys[i]);

    TextReplacer::Dump d;
    d.frame = frames_;
    char buf[1024];
    std::snprintf(buf, sizeof buf,
                  "# Mega Drive sprites on screen (%dx%d), picture %llu.\n"
                  "# Each group is sprites side by side on one line, numbered as in the PNG of the same name.\n"
                  "# To replace one, copy its \"sprite\" lines into Mods/Textures/mdsprite_<name>.txt (drop\n"
                  "# the sprites that are not part of the text, or join groups) and draw mdsprite_<name>.png,\n"
                  "# palette-indexed, the size of the group. See docs/TEXTURE_MODS.md.\n\n",
                  width, height, static_cast<unsigned long long>(frames_));
    d.text = buf;
    for (size_t i = 0; i < groups.size(); ++i) {
        const auto& g = groups[i];
        const int id = int(i) + 1;
        int x0 = g[0]->x, y0 = g[0]->y, x1 = x0, y1 = y0, tiles = 0;
        for (const Sprite* s : g) {
            x0 = std::min(x0, s->x);
            y0 = std::min(y0, s->y);
            x1 = std::max(x1, s->x + s->w * 8);
            y1 = std::max(y1, s->y + s->h * 8);
            tiles += s->w * s->h;
        }
        std::snprintf(buf, sizeof buf,
                      "# group %d: screen x %d y %d, %d sprite%s, %d tiles, %dx%d pixels, palette line %d, priority %d\n",
                      id, x0, y0, int(g.size()), g.size() == 1 ? "" : "s", tiles, x1 - x0, y1 - y0,
                      (g[0]->pat >> 13) & 3, (g[0]->pat >> 15) & 1);
        d.text += buf;
        for (const Sprite* s : g) {
            std::snprintf(buf, sizeof buf, "#   entry %d (list place %d) at %d,%d, tile %03x\n", s->link, s->place, s->x,
                          s->y, s->pat & 0x7FF);
            d.text += buf;
        }
        for (const Sprite* s : g) d.text += line_of(*s, s->x - g[0]->x, s->y - g[0]->y) + "\n";
        d.text += "\n";
        d.boxes.push_back({id, x0, y0, x1 - x0, y1 - y0, 'S'});
    }
    pending.push_back(std::move(d));
}

} // namespace chaotix
