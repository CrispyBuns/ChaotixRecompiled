// Mega Drive text replacement by tile sequence: a modder's image drawn over
// a run of plane tiles, for text the game builds from reusable letter tiles
// (one tile, or two stacked, per letter, shared by every string). Replacing
// the "O" tile by its pixels (TileReplacer) changes every O; a pattern names
// the whole run -- "O P T I O N S" -- and replaces only where it appears.
//
// A pattern is a grid of cells, `rows` x `cols`, each a tile hash (the same
// hash as md_<hash>.png, TileReplacer::hash) and the cell's flip bits. Once a
// frame, before the frame is drawn, each plane's nametable (A, B, window) is
// searched for the patterns, in nametable coordinates: a match is a place in
// the plane, so it scrolls with the plane, per line or per column, as the
// tiles do. The longest pattern wins where matches overlap. The cells of a
// match are then drawn from the pattern's image instead of from their tiles
// (single-tile replacements do not apply to them); an image wider than the
// run also covers the plane's pixels beside it, where the image is not 0.
//
// Colours are palette indices, as in md_ tiles: 1-15 of the palette line the
// run's first tile is drawn with, 0 transparent, so fades and palette
// changes (a highlighted menu entry) still apply. The priority bit is the
// first tile's too. Both are read as each line is drawn: a run whose entries
// change only their palette line or priority during the picture (DATA LOAD
// dims a slot that way) stays replaced, in the new colours; a cell whose
// tile or flips change shows its tile.
//
// Output only, like the tile replacer: nothing here is read by the machine.
//
// Dumping lists the runs of tiles each plane shows, for modders to copy into
// patterns (the frontend writes the list with a labelled screenshot).
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace chaotix {

class Vdp;

class TextReplacer {
public:
    enum PlaneBit : uint8_t { kPlaneA = 1, kPlaneB = 2, kPlaneW = 4, kAnyPlane = 7 };
    enum class Align : uint8_t { Left, Center, Right };

    struct Pattern {
        std::string name;
        int rows = 0, cols = 0;
        std::vector<uint64_t> hashes;  // rows * cols, row by row
        std::vector<uint8_t> flips;    // per cell: bit 0 horizontal, bit 1 vertical
        uint8_t planes = kAnyPlane;
        int palette = -1;              // 0-3: matches only with that palette line
        Align align = Align::Left;
        int width = 0, height = 0;     // the image; height is rows * 8
        std::vector<uint8_t> indices;  // width * height, 0-15
    };

    // Adds a pattern; returns false (with `error`) when it is malformed.
    bool add(Pattern p, std::string* error);
    size_t size() const { return patterns_.size(); }
    bool active(bool dumping) const { return dumping || !patterns_.empty(); }

    // Once a frame, before it is drawn, after the tile hashes are refreshed:
    // finds the matches, and with `dumping` lists the runs on screen.
    void refresh(const Vdp& v, const uint64_t* slot_hash, bool dumping);

    // A match, in the plane's pixel coordinates.
    struct Hit {
        int ox, oy;            // the image's top-left pixel in the plane (may wrap)
        const Pattern* p;
        uint8_t pal;           // palette line << 4, as matched
        uint8_t prio;
        uint32_t entry0;       // the first cell's name table entry's address
    };
    struct Plane {
        int pw = 0, ph = 0;                  // in cells, as when matched
        std::vector<int16_t> run;            // per cell: the hit drawn instead of the tile, or -1
        std::vector<uint16_t> entry;         // per run cell: its nametable entry when matched
        std::vector<int16_t> over;           // per cell: a hit whose image reaches over it, or -1
        std::vector<Hit> hits;
        bool any = false;
    };
    // Plane 0 = A, 1 = B, 2 = window; nullptr when nothing matched there or
    // the plane's size has changed since.
    const Plane* plane(int i, int pw, int ph) const {
        const Plane& p = planes_[i];
        return p.any && p.pw == pw && p.ph == ph ? &p : nullptr;
    }
    // The hit drawn instead of cell `cell` showing entry `e`, or nullptr: the
    // entry's tile and flips must be as matched, its palette line and
    // priority need not.
    static const Hit* run_hit(const Plane& p, int cell, uint16_t e) {
        const int h = p.run[size_t(cell)];
        return h >= 0 && ((p.entry[size_t(cell)] ^ e) & 0x1FFF) == 0 ? &p.hits[size_t(h)] : nullptr;
    }
    // The palette line (<< 4) and priority the hit is drawn with now: its
    // first cell's.
    static void colours(const Hit& h, const uint8_t* vram, uint8_t& pal, uint8_t& prio) {
        const uint16_t e = uint16_t((vram[h.entry0 & 0xFFFF] << 8) | vram[(h.entry0 + 1) & 0xFFFF]);
        pal = uint8_t((e >> 9) & 0x30);
        prio = uint8_t((e >> 15) & 1);
    }
    static const Hit* over_hit(const Plane& p, int cell) {
        const int h = p.over[size_t(cell)];
        return h >= 0 ? &p.hits[size_t(h)] : nullptr;
    }
    // The image's 8 pixels at plane pixels (x..x+7, y), 0 outside it.
    static void pixels(const Hit& h, int x, int y, int plane_w, int plane_h, uint8_t out[8]) {
        const int iy = ((y - h.oy) % plane_h + plane_h) % plane_h;
        for (int k = 0; k < 8; ++k) {
            const int ix = ((x + k - h.ox) % plane_w + plane_w) % plane_w;
            out[k] = (ix < h.p->width && iy < h.p->height) ? h.p->indices[size_t(iy * h.p->width + ix)] : 0;
        }
    }

    // Dumping: a list of the runs on screen, when it has runs not listed
    // before that have stayed in place for kDumpSteady pictures (at most
    // every kDumpGap pictures); the frontend writes it.
    struct DumpBox { int id, x, y, w, h; char plane; };
    struct Dump {
        uint64_t frame = 0;
        std::string text;
        std::vector<DumpBox> boxes;
    };
    std::vector<Dump> pending;
    static constexpr int kDumpGap = 30;
    static constexpr int kDumpSteady = 10;

    // The token for a cell in pattern and dump files: 16 hex digits, then
    // ":h", ":v" or ":hv" for a flipped tile.
    static std::string token(uint64_t hash, int flips);
    static bool parse_token(const std::string& s, uint64_t& hash, uint8_t& flips);

private:
    void match(const Vdp& v, int plane, const uint64_t* slot_hash);
    void dump(const Vdp& v, const uint64_t* slot_hash);

    std::vector<Pattern> patterns_;
    std::unordered_map<uint64_t, std::vector<int>> by_first_;  // first cell's hash -> patterns
    std::array<Plane, 3> planes_;
    std::vector<uint8_t> first_;      // per VRAM slot: its hash starts a pattern
    uint64_t frames_ = 0, last_dump_ = 0;
    bool dumped_once_ = false;
    std::unordered_set<std::string> dumped_runs_;
    std::unordered_map<std::string, int> seen_;  // run at its place -> pictures in a row
};

} // namespace chaotix
