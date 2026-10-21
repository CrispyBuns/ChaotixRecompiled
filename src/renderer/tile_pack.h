// Mega Drive tile replacements as files (issue #35): Mods/Textures/md_<hash>.png,
// and the dumps the game writes for modders to start from.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace chaotix {

class TileReplacer;

// The file name of a tile: md_ and its hash in 16 hex digits.
std::string tile_file_name(uint64_t hash);

// Loads every md_<hash>.png in `dir` into `tiles`. A replacement must be an
// 8x8 palette-indexed PNG (as the dumps are); its indices 0-15 are drawn with
// the tile's palette in the game, 0 being transparent. Returns how many were
// loaded; `messages` says why any file was skipped.
int load_tile_pack(TileReplacer& tiles, const std::string& dir, std::vector<std::string>* messages);

// Writes the tiles the replacer has collected for dumping to `dir` (created
// if needed) and clears them. Returns how many files were written.
int write_tile_dumps(TileReplacer& tiles, const std::string& dir);

} // namespace chaotix
