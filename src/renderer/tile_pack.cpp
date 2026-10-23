#include "tile_pack.h"

#include "image_io.h"
#include "runtime/tile_replace.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

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

int load_text_pack(TileReplacer& tiles, const std::string& dir, std::vector<std::string>* messages) {
    auto say = [messages](std::string m) { if (messages) messages->push_back(std::move(m)); };
    namespace fs = std::filesystem;
    std::error_code ec;
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(fs::u8path(dir), ec)) {
        const std::string name = e.path().filename().u8string();
        if (e.is_regular_file(ec) && name.size() > 11 && name.compare(0, 7, "mdtext_") == 0 &&
            name.compare(name.size() - 4, 4, ".txt") == 0)
            files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());  // the loading order breaks ties between patterns
    auto lower = [](std::string s) {
        for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    int loaded = 0;
    for (const auto& path : files) {
        const std::string name = path.filename().u8string();
        std::ifstream in(path);
        if (!in) { say(name + ": cannot be read"); continue; }
        TextReplacer::Pattern p;
        p.name = name.substr(0, name.size() - 4);
        std::string error, line;
        int number = 0;
        while (error.empty() && std::getline(in, line)) {
            ++number;
            const size_t comment = line.find_first_of("#;");
            if (comment != std::string::npos) line.erase(comment);
            std::istringstream words(line);
            std::string key;
            if (!(words >> key)) continue;
            std::vector<std::string> args;
            for (std::string w; words >> w;) args.push_back(w);
            key = lower(key);
            const std::string at = "line " + std::to_string(number) + ": ";
            if (key == "row") {
                if (args.empty()) { error = at + "a row needs tiles"; break; }
                if (p.rows && int(args.size()) != p.cols) { error = at + "every row must have the same number of tiles"; break; }
                p.cols = int(args.size());
                for (const auto& a : args) {
                    uint64_t h;
                    uint8_t f;
                    if (!TextReplacer::parse_token(a, h, f)) {
                        error = at + "\"" + a + "\" is not a tile (16 hex digits, then :h, :v or :hv if flipped)";
                        break;
                    }
                    p.hashes.push_back(h);
                    p.flips.push_back(f);
                }
                ++p.rows;
            } else if (key == "plane") {
                uint8_t planes = 0;
                for (const auto& a : args) {
                    const std::string w = lower(a);
                    if (w == "a") planes |= TextReplacer::kPlaneA;
                    else if (w == "b") planes |= TextReplacer::kPlaneB;
                    else if (w == "w" || w == "window") planes |= TextReplacer::kPlaneW;
                    else if (w == "any") planes |= TextReplacer::kAnyPlane;
                    else { planes = 0; break; }
                }
                if (!planes) error = at + "plane is A, B, W (the window) or any";
                p.planes = planes;
            } else if (key == "palette") {
                const std::string w = args.size() == 1 ? lower(args[0]) : "";
                if (w == "any") p.palette = -1;
                else if (w.size() == 1 && w[0] >= '0' && w[0] <= '3') p.palette = w[0] - '0';
                else error = at + "palette is a palette line, 0 to 3, or any";
            } else if (key == "align") {
                const std::string w = args.size() == 1 ? lower(args[0]) : "";
                if (w == "left") p.align = TextReplacer::Align::Left;
                else if (w == "center" || w == "centre") p.align = TextReplacer::Align::Center;
                else if (w == "right") p.align = TextReplacer::Align::Right;
                else error = at + "align is left, center or right";
            } else {
                error = at + "unknown entry \"" + key + "\" (row, plane, palette or align)";
            }
        }
        if (error.empty() && !p.rows) error = "no row of tiles";
        if (!error.empty()) { say(name + ": " + error); continue; }
        const std::string png_name = p.name + ".png";
        PngImage img;
        if (!read_png((path.parent_path() / fs::u8path(png_name)).u8string(), img, &error)) {
            say(png_name + ": " + error);
            continue;
        }
        if (!img.indexed) {
            say(png_name + ": must be a palette-indexed PNG (indices 1-15 of the text's palette line, 0 transparent)");
            continue;
        }
        p.width = img.width;
        p.height = img.height;
        p.indices = img.indices;
        if (!tiles.text.add(std::move(p), &error)) { say(name + ": " + error); continue; }
        ++loaded;
    }
    return loaded;
}

namespace {

// 3x5 digits for the dump's labels, a row of three bits per line.
const uint8_t kDigits[10][5] = {
    {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1},
    {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7},
};

} // namespace

int write_text_dumps(TileReplacer& tiles, const std::string& dir, const uint32_t* pixels, int width, int height,
                     int stride_pixels, int extra) {
    auto& pending = tiles.text.pending;
    if (pending.empty()) return 0;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::u8path(dir), ec);
    int written = 0;
    for (const auto& d : pending) {
        char base[64];
        std::snprintf(base, sizeof base, "/text_%06llu", static_cast<unsigned long long>(d.frame));
        {
            std::ofstream out(std::filesystem::u8path(dir + base + ".txt"), std::ios::binary);
            out << d.text;
            if (!out) continue;
        }
        // The picture at twice the size, so the labels can be read.
        const int W = width * 2, H = height * 2;
        std::vector<uint32_t> img(size_t(W) * size_t(H));
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) img[size_t(y * W + x)] = pixels[(y / 2) * stride_pixels + x / 2];
        auto put = [&](int x, int y, uint32_t c) {
            if (x >= 0 && y >= 0 && x < W && y < H) img[size_t(y * W + x)] = c;
        };
        auto fill = [&](int x0, int y0, int w, int h, uint32_t c) {
            for (int y = y0; y < y0 + h; ++y)
                for (int x = x0; x < x0 + w; ++x) put(x, y, c);
        };
        for (const auto& b : d.boxes) {
            const uint32_t colour = b.plane == 'A' ? 0xFFFF40u : b.plane == 'B' ? 0x40FFFFu : 0xFF40FFu;
            const int x0 = (b.x + extra) * 2 - 1, y0 = b.y * 2 - 1, x1 = (b.x + extra + b.w) * 2, y1 = (b.y + b.h) * 2;
            for (int x = x0; x <= x1; ++x) { put(x, y0, colour); put(x, y1, colour); }
            for (int y = y0; y <= y1; ++y) { put(x0, y, colour); put(x1, y, colour); }
            // The number left of the box (right of it at the left edge), in
            // the box's colour on black.
            const std::string label = std::to_string(b.id);
            const int lw = int(label.size()) * 8 + 2;
            int lx = x0 - lw - 1;
            if (lx < 0) lx = x1 + 2;
            const int ly = y0 + 2;
            fill(lx, ly, lw, 14, 0x000000u);
            for (size_t i = 0; i < label.size(); ++i)
                for (int r = 0; r < 5; ++r)
                    for (int c = 0; c < 3; ++c)
                        if (kDigits[label[i] - '0'][r] & (4 >> c))
                            fill(lx + 2 + int(i) * 8 + c * 2, ly + 2 + r * 2, 2, 2, colour);
        }
        if (write_png(dir + base + ".png", img.data(), W, H, W)) ++written;
    }
    pending.clear();
    return written;
}

} // namespace chaotix
