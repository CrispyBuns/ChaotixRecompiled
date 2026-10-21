#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace chaotix {

// Writes an XRGB8888 image as an uncompressed (stored-deflate) PNG.
bool write_png(const std::string& path, const uint32_t* pixels, int width, int height, int stride_pixels);

// A decoded PNG. `argb` is always filled; an indexed (colour type 3) image
// also keeps its palette indices and palette, so replacement tiles can be
// matched against the game's own colours.
struct PngImage {
    int width = 0, height = 0;
    bool indexed = false;            // colour type 3
    std::vector<uint8_t> indices;    // indexed: one palette index per pixel, row-major
    std::vector<uint32_t> palette;   // indexed: ARGB8888 entries (alpha from tRNS, else 0xFF)
    std::vector<uint32_t> argb;      // always filled: ARGB8888 per pixel (indexed images expanded too)
};

// Reads a PNG from a UTF-8 path. Grey, RGB, indexed, grey+alpha and RGBA at
// the usual bit depths are supported; 16-bit and interlaced images are not.
// On failure `error` (if given) says why.
bool read_png(const std::string& path, PngImage& out, std::string* error);
bool decode_png(const uint8_t* data, size_t n, PngImage& out, std::string* error);

// Palette-indexed, 8-bit depth, with a tRNS chunk when any entry is not opaque.
bool write_png_indexed(const std::string& path, const uint8_t* indices, int width, int height,
                       const uint32_t* palette_argb, int palette_size);

// Writes interleaved stereo int16 samples as a WAV file.
bool write_wav(const std::string& path, const std::vector<int16_t>& stereo, int rate);

// FNV-1a hash of an image region, used by rendering regression tests.
uint64_t image_hash(const uint32_t* pixels, int width, int height, int stride_pixels);

} // namespace chaotix
