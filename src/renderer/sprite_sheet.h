// Whole-bank sprite sheets (docs/TEXTURE_MODS.md): every frame of a bank of
// the 32X bank table in one palette-indexed PNG, 32x_<BB>.png, with a text
// sidecar, 32x_<BB>.txt, saying where each frame's cell is and where the
// graphic's box lies in it. A character has over a hundred frames in two
// banks (the body bank C and C+8), and dumping writes only what the game
// happened to draw; a sheet is every frame at once, side by side.
//
// Exporting reads the banks straight from the ROM (the characters' banks
// 0-15 are drawn from it as they are) and, for the banks a scene has
// decompressed into SDRAM, from the machine's memory as it is. Loading turns
// each cell into the replacement 32x_<BB>_<FF>.png would have been
// (sprite_pack.h), so a sheet is only another way to supply replacements:
// nothing of it reaches the emulated machine.
//
// The layout: cells of one size per sheet, sixteen to a row (frame 0x00-0x0F
// on the first row, 0x10-0x1F on the second, ...), with the hotspot at the
// same place in every cell, so the frames of an animation line up as they
// do in the game. Each cell is the union of the bank's boxes around the
// hotspot. A 1-pixel grid in index 0xFF separates the cells (and fills the
// cells of empty slots); index 0xFE fills each cell outside its graphic's
// box. Neither is a value the game's graphics use (they stay below 0x80),
// and both read as transparent when a sheet is loaded.
#pragma once
#include "runtime/sprite_record.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace chaotix {

class Machine;
class SpritePack;

constexpr uint8_t kSheetGrid = 0xFF;     // the lines between cells, and empty slots
constexpr uint8_t kSheetOutside = 0xFE;  // a cell outside its graphic's box
constexpr int kSheetColumns = 16;

// One frame slot of a bank: its graphic as an image of its box, and the box
// around the hotspot (x0..x1 columns, x1 exclusive; y0..y1 lines, y1
// inclusive, as the graphic's header has them, divided by 256).
struct BankFrame {
    enum Kind : uint8_t { Empty, Graphic, Same };  // Same: the graphic of an earlier frame
    Kind kind = Empty;
    int same = -1;  // Same: that frame
    int x0 = 0, x1 = 0, y0 = 0, y1 = 0;
    SpriteImage image;
};

struct SpriteBank {
    int bank = -1;
    std::vector<BankFrame> frames;
    std::array<uint32_t, 256> palette{};  // ARGB, for the sheet's PNG
};

// Where everything is in a sheet.
struct SheetCell {
    int frame = 0;
    BankFrame::Kind kind = BankFrame::Empty;
    int same = -1;
    int x = 0, y = 0, w = 0, h = 0;          // the cell in the sheet
    int x0 = 0, x1 = 0, y0 = 0, y1 = 0;      // the box around the hotspot
};
struct SheetLayout {
    int bank = -1;
    int width = 0, height = 0;   // the sheet
    int cell_w = 0, cell_h = 0;
    int hot_x = 0, hot_y = 0;    // the hotspot within every cell
    std::vector<SheetCell> cells;
};

// The banks of the ROM's bank table (ROM 0x0A0000) whose graphics are in the
// ROM as they are drawn: banks 0-15, the characters. `bank` is read with the
// same rules as SpriteRecorder's index (a frame table ends where the next
// one starts, at the first graphic it points past, or at an entry that is not
// a graphic; FFFFFFFF is an empty slot). The palette is the character's
// colours as shown for the player (pixel p in the character's colour p, the
// ROM's table at 0x0018E4). False (and `why`) for a bank with no graphics or
// one that does not read as graphics.
bool read_rom_bank(const std::vector<uint8_t>& rom, int bank, SpriteBank& out, std::string* why);
// The banks above 15 the scene has in the bank table in SDRAM (decompressed
// there, or drawn from ROM), in the colours of the 32X palette as it is.
std::vector<SpriteBank> read_loaded_banks(const Machine& m);

SheetLayout sheet_layout(const SpriteBank& bank);
// The sheet's pixels (palette indices, row-major, layout.width x height).
std::vector<uint8_t> sheet_pixels(const SpriteBank& bank, const SheetLayout& layout);
std::string sheet_sidecar(const SheetLayout& layout);
// False (and `why`, with the line) for a sidecar that is not one, or whose
// cells do not fit together.
bool parse_sheet_sidecar(const std::string& text, SheetLayout& out, std::string* why);

// The replacements a sheet holds: one per cell of a graphic (not the empty
// slots, nor the frames that repeat an earlier one), as 32x_<BB>_<FF>.png
// would hold it. A cell's replacement is its box; pixels painted to the right
// of the box or below it, within the cell, make it larger, the way a larger
// single file grows from the same top-left corner. Pixels left of or above
// the box cannot be drawn there and are left out (`warnings` says so).
// Indices 0, 0xFE and 0xFF are transparent. False (and `why`) if the image
// is not the layout's size.
struct SheetReplacement {
    int frame = 0;
    SpriteImage image;
};
bool sheet_replacements(const SheetLayout& layout, const uint8_t* px, int width, int height,
                        std::vector<SheetReplacement>& out, std::vector<std::string>* warnings, std::string* why);

// Writes the sheets of banks 0-15 from the ROM and of the banks loaded in
// SDRAM (read_loaded_banks) to `dir` (created if needed), 32x_<BB>.png with
// 32x_<BB>.txt. Returns how many sheets were written.
int export_sprite_sheets(const Machine& m, const std::string& dir, std::vector<std::string>* messages);

// Loads every 32x_<BB>.png with its 32x_<BB>.txt in `dir` into `pack`.
// Returns how many replacements the sheets gave.
int load_sprite_sheets(SpritePack& pack, const std::string& dir, std::vector<std::string>* messages);

} // namespace chaotix
