#include "tile_pack.h"

#include "image_io.h"
#include "runtime/tile_replace.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace chaotix {

std::string tile_file_name(uint64_t hash) {
    char name[32];
    std::snprintf(name, sizeof name, "md_%016llx.png", static_cast<unsigned long long>(hash));
    return name;
}

int load_tile_pack(TileReplacer& tiles, const std::string& dir, std::vector<std::string>* messages) {
    auto say = [messages](std::string m) { if (messages) messages->push_back(std::move(m)); };
    namespace fs = std::filesystem;
    std::error_code ec;
    int loaded = 0;
    for (const auto& e : fs::directory_iterator(fs::u8path(dir), ec)) {
        const std::string name = e.path().filename().u8string();
        if (!e.is_regular_file(ec) || name.size() != 23 || name.compare(0, 3, "md_") != 0 ||
            name.compare(19, 4, ".png") != 0)
            continue;
        char* end = nullptr;
        const uint64_t hash = std::strtoull(name.substr(3, 16).c_str(), &end, 16);
        if (!end || *end) continue;
        PngImage img;
        std::string err;
        if (!read_png(e.path().u8string(), img, &err)) { say(name + ": " + err); continue; }
        if (img.width != 8 || img.height != 8) { say(name + ": must be 8x8 pixels, like the dumped tile"); continue; }
        if (!img.indexed) {
            // A full-colour image cannot follow the game's palette (fades,
            // cycling), so the colours must be indices into it.
            say(name + ": must be a palette-indexed PNG, like the dumped tile");
            continue;
        }
        std::array<uint8_t, 64> px{};
        for (int i = 0; i < 64; ++i) px[size_t(i)] = img.indices[size_t(i)] & 15;
        tiles.add(hash, px);
        ++loaded;
    }
    return loaded;
}

int write_tile_dumps(TileReplacer& tiles, const std::string& dir) {
    if (tiles.pending.empty()) return 0;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::u8path(dir), ec);
    int written = 0;
    for (const auto& d : tiles.pending) {
        const std::string path = dir + "/" + tile_file_name(d.hash);
        if (write_png_indexed(path, d.indices.data(), 8, 8, d.palette.data(), 16)) ++written;
    }
    tiles.pending.clear();
    return written;
}

} // namespace chaotix
