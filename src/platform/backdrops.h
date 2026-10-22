#pragma once

// The menu's zone pictures (issue #8, frontend/backdrop.h), as textures.
//
// Each picture is made once per ROM by a worker thread running a machine of
// its own, then kept as a PNG in the user data folder (Cache/Backdrops), so
// later starts load it at once. The playing machine is never touched: the
// worker only reads a copy of the ROM. Until a picture exists the menu keeps
// its plain look, so nothing waits for the worker.

#include <SDL3/SDL.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "frontend/backdrop.h"
#include "runtime/rom.h"

namespace chaotix {

class MenuBackdrops {
public:
    MenuBackdrops() = default;
    MenuBackdrops(const MenuBackdrops&) = delete;
    MenuBackdrops& operator=(const MenuBackdrops&) = delete;
    ~MenuBackdrops() { shutdown(); }

    // The ROM the pictures are made from (copied) and the folder they are
    // cached in; a subfolder per ROM image, so a patched ROM gets its own.
    void configure(const Rom& rom, const std::string& cache_root);

    // Main thread, once a frame. While `wanted`, loads the cached pictures,
    // starts the worker on the first one missing, and turns what it finished
    // into a texture (and a PNG in the cache). Nothing happens while not
    // wanted, so the setting turned off costs nothing.
    void update(SDL_Renderer* renderer, bool wanted);
    // The next picture, when the menu opens.
    void advance();

    struct Shown {
        SDL_Texture* texture = nullptr;
        float width = 0, height = 0;
        uint32_t tint = 0, accent = 0;  // ARGB (backdrop_tint, backdrop_accent)
    };
    // False while no picture is ready.
    bool current(Shown& out) const;
    int ready_count() const;

    // Stops the worker (it gives up at its next frame) and frees the textures.
    void shutdown();

private:
    struct Entry {
        BackdropSpec spec;
        SDL_Texture* texture = nullptr;
        int width = 0, height = 0;
        uint32_t tint = 0, accent = 0;
        bool failed = false;   // the worker could not make it; not tried again this run
    };
    struct Finished {
        size_t index = 0;
        bool ok = false;
        std::string why;
        double ms = 0;
        BackdropImage image;
    };
    void make_texture(SDL_Renderer* renderer, Entry& e, const BackdropImage& image);
    std::string path_of(const Entry& e) const;

    std::vector<Entry> entries_;
    std::unique_ptr<Rom> rom_;
    std::string dir_;
    bool loaded_ = false;
    int current_ = -1;

    std::thread worker_;
    std::atomic<bool> cancel_{false};
    std::atomic<bool> working_{false};
    std::mutex mutex_;
    std::vector<Finished> finished_;  // guarded by mutex_
};

} // namespace chaotix
