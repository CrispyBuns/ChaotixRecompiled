// Mega Drive text replacement by sprite: the sprite counterpart of
// text_replace.h, for menu text the game draws with VDP sprites (the DATA
// LOAD / PLAY / DELETE banners' letters, PLAY STYLE's 1P 2P CPU, the 8x16
// font's zone and character names, SOUND TEST). Their letter tiles are
// shared between strings, so replacing a tile (TileReplacer) changes every
// string that uses it; a pattern names the sprites showing one string and
// replaces only them.
//
// A pattern is one sprite or a group of sprites at fixed places relative to
// each other. Each member is a sprite's size (1-4 x 1-4 tiles), its flip
// bits and the hashes of its tiles in VRAM order (column by column, as the
// VDP reads them: TileReplacer::hash, the same as md_<hash>.png). Once a
// frame, before it is drawn, the sprite table is searched: a match is a
// sprite showing one member's tiles with, for each other member, a sprite
// showing its tiles exactly that many pixels away -- or, for a member the
// game has left out of the list, nothing there because the member would be
// wholly off screen (beyond the native screen and the widescreen margins
// the picture shows): the game leaves out the sprites of a banner sliding
// in or out that are past the screen's edge. Distances are compared as the
// VDP places sprites: X modulo 512 (9 bits), Y modulo 1024. Where matches
// share sprites the one with the most tiles present wins (then a whole
// group over a partial one, then the pattern loaded first).
//
// The member sprites are then not drawn, and the image is, as one sprite in
// the place in the sprite list of the first member present (in front of
// the sprites after it, behind those before it), with that member's
// priority bit and palette line: indices 1-15 of that line, 0 transparent.
// It is placed at the members' bounding box (align left, center or right
// when it is not as wide; an offset moves it), relative to that member, and
// drawn clipped to the picture like any sprite. A member whose table entry
// moves or changes tiles during the picture brings the whole group's
// sprites back on the lines drawn after; a change of palette line or
// priority does not (the image follows the member's).
//
// Output only: the status flags (sprite collision, overflow) still come
// from the original sprites, and nothing here is read by the machine.
//
// Dumping lists the sprites on screen, grouped into suggested patterns
// (sprites side by side on the same line with the same palette line), for
// modders to copy (the frontend writes the list with a labelled screenshot).
#pragma once
#include "text_replace.h"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace chaotix {

class Vdp;

class SpriteTextReplacer {
public:
    using Align = TextReplacer::Align;
    static constexpr int kMaxSprites = 80;  // the sprite table's entries (H40)

    SpriteTextReplacer() { hit_of_.fill(-1); }

    struct Member {
        int dx = 0, dy = 0;            // pixels from the first member's top-left
        int w = 1, h = 1;              // tiles
        uint8_t flips = 0;             // bit 0 horizontal, bit 1 vertical
        std::vector<uint64_t> hashes;  // w * h, column by column
    };
    struct Pattern {
        std::string name;
        std::vector<Member> members;   // the first is at 0,0 once added
        int palette = -1;              // 0-3: matches only when the first member present has that line
        Align align = Align::Left;
        int offset_x = 0, offset_y = 0;
        int width = 0, height = 0;     // the image
        std::vector<uint8_t> indices;  // width * height, 0-15
        // Set by add(): the tiles in all, and the image's top-left relative
        // to the first member's.
        int tiles = 0;
        int image_x = 0, image_y = 0;
    };

    // Adds a pattern (member offsets are made relative to the first);
    // returns false (with `error`) when it is malformed.
    bool add(Pattern p, std::string* error);
    size_t size() const { return patterns_.size(); }
    bool active(bool dumping) const { return dumping || !patterns_.empty(); }

    // Once a frame, before it is drawn, after the tile hashes are refreshed:
    // finds the matches, and with `dumping` lists the sprites on screen.
    void refresh(const Vdp& v, const uint64_t* slot_hash, bool dumping);

    struct Hit {
        const Pattern* p;
        uint8_t first;   // the sprite table entry of the first member present
        uint8_t place;   // its place in the sprite list: lower is in front
        int dx, dy;      // that member's offset from the pattern's first member
        std::vector<uint8_t> links;  // every present member's entry
    };
    // The matches of this picture; empty without patterns.
    const std::vector<Hit>& hits() const { return hits_; }
    // The hit whose member sprite table entry `link` is, or -1.
    int hit_of(int link) const { return link >= 0 && link < kMaxSprites ? hit_of_[size_t(link)] : -1; }
    // Whether entry `link` still shows what it showed when matched (place
    // and tiles; palette line and priority may differ). `sat` is the table.
    bool unchanged(const uint8_t* vram, uint32_t sat, int link) const;
    // Every member of hit `h` unchanged.
    bool unchanged_hit(const uint8_t* vram, uint32_t sat, int h) const;

    // Dumping, as TextReplacer: a list of the sprites on screen when it has
    // groups not listed before that stayed in place for kDumpSteady
    // pictures, at most every kDumpGap pictures. Boxes have plane 'S'.
    std::vector<TextReplacer::Dump> pending;
    static constexpr int kDumpGap = TextReplacer::kDumpGap;
    static constexpr int kDumpSteady = TextReplacer::kDumpSteady;

    // A sprite's size token: "WxH", then ":h", ":v" or ":hv" if flipped.
    static std::string size_token(int w, int h, int flips);
    static bool parse_size_token(const std::string& s, int& w, int& h, uint8_t& flips);

    // One sprite of the table, as read for matching and dumping.
    struct Sprite {
        int link, place;
        int x, y;        // screen pixels
        int w, h;        // tiles
        uint16_t pat;    // the pattern word: tile, flips, palette, priority
        uint8_t flips;
    };
    static std::vector<Sprite> read_table(const Vdp& v);
    static uint32_t table_address(const Vdp& v);

private:
    bool member_matches(const Member& m, const Sprite& s, const uint64_t* slot_hash) const;
    void match(const Vdp& v, const std::vector<Sprite>& table, const uint64_t* slot_hash);
    // Whether a member at screen pixels (x, y) would show nothing: wholly
    // outside the picture.
    static bool off_screen(const Vdp& v, const Member& m, int x, int y);
    void dump(const Vdp& v, const std::vector<Sprite>& table, const uint64_t* slot_hash);

    std::vector<Pattern> patterns_;
    // A member's first tile -> (pattern, member)
    std::unordered_map<uint64_t, std::vector<std::pair<int, int>>> by_tile_;
    std::vector<Hit> hits_;
    std::array<int8_t, kMaxSprites> hit_of_{};
    std::array<std::array<uint16_t, 4>, kMaxSprites> saved_{};  // entries as matched
    uint64_t frames_ = 0, last_dump_ = 0;
    bool dumped_once_ = false;
    std::unordered_set<std::string> dumped_groups_;
    std::unordered_map<std::string, int> seen_;
};

} // namespace chaotix
