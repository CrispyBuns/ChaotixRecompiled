#include "platform/backdrops.h"

#include <chrono>
#include <filesystem>
#include <utility>

#include "renderer/image_io.h"
#include "runtime/log.h"

namespace chaotix {

namespace {

// Bumped whenever the way a picture is made changes, so that older cached
// pictures are made again rather than kept.
constexpr const char* kRecipe = "v1";

} // namespace

void MenuBackdrops::configure(const Rom& rom, const std::string& cache_root) {
    shutdown();
    rom_ = std::make_unique<Rom>(rom);
    // The image actually played: a ROM patch can change the levels' art.
    const std::string& id = rom.patched.empty() ? rom.sha1 : rom.patched_sha1;
    dir_ = cache_root + std::string(kRecipe) + "-" + id.substr(0, 12) + "/";
    entries_.clear();
    for (const BackdropSpec& spec : backdrop_specs()) {
        Entry e;
        e.spec = spec;
        entries_.push_back(e);
    }
    loaded_ = false;
    current_ = -1;
}

std::string MenuBackdrops::path_of(const Entry& e) const { return dir_ + e.spec.key + ".png"; }

void MenuBackdrops::make_texture(SDL_Renderer* renderer, Entry& e, const BackdropImage& image) {
    SDL_Texture* t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STATIC, image.width,
                                       image.height);
    if (!t) {
        LOGW("menu", "backdrop texture: %s", SDL_GetError());
        return;
    }
    SDL_UpdateTexture(t, nullptr, image.pixels.data(), image.width * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    // The picture is scaled by a fraction to cover the window and lies under
    // a translucent tint: smoothed, it stays quiet behind the text.
    SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
    e.texture = t;
    e.width = image.width;
    e.height = image.height;
    e.tint = backdrop_tint(image);
    e.accent = backdrop_accent(image);
}

void MenuBackdrops::update(SDL_Renderer* renderer, bool wanted) {
    if (!rom_ || !renderer) return;
    if (worker_.joinable() && !working_.load()) worker_.join();
    std::vector<Finished> done;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        done.swap(finished_);
    }
    for (Finished& f : done) {
        Entry& e = entries_[f.index];
        if (!f.ok) {
            e.failed = true;
            LOGW("menu", "backdrop %s: %s", e.spec.key, f.why.c_str());
            continue;
        }
        LOGI("menu", "backdrop %s made in %.0f ms", e.spec.key, f.ms);
        // Written here rather than on the worker: the PNG writer's tables are
        // shared, and this takes a few milliseconds once per picture.
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::u8path(dir_), ec);
        if (!write_png(path_of(e), f.image.pixels.data(), f.image.width, f.image.height, f.image.width))
            LOGW("menu", "cannot write %s", path_of(e).c_str());
        make_texture(renderer, e, f.image);
    }
    if (!wanted) return;
    if (!loaded_) {
        loaded_ = true;
        for (Entry& e : entries_) {
            PngImage png;
            std::error_code ec;
            if (!std::filesystem::exists(std::filesystem::u8path(path_of(e)), ec)) continue;
            if (!read_png(path_of(e), png, nullptr) || png.width <= 0 || png.height <= 0) continue;
            BackdropImage image;
            image.width = png.width;
            image.height = png.height;
            image.pixels = std::move(png.argb);
            make_texture(renderer, e, image);
        }
    }
    if (current_ < 0)
        for (size_t i = 0; i < entries_.size(); ++i)
            if (entries_[i].texture) { current_ = int(i); break; }
    if (worker_.joinable()) return;
    // One picture at a time, in the list's order, so the first is ready as
    // soon as possible and a slow device is never running more than one.
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].texture || entries_[i].failed) continue;
        working_ = true;
        cancel_ = false;
        const BackdropSpec spec = entries_[i].spec;
        const Rom* rom = rom_.get();
        worker_ = std::thread([this, i, spec, rom] {
            // Below the game and the audio: the picture can wait.
            SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_LOW);
            Finished f;
            f.index = i;
            const auto t0 = std::chrono::steady_clock::now();
            f.ok = render_backdrop(*rom, spec, f.image, &f.why, &cancel_);
            f.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                finished_.push_back(std::move(f));
            }
            working_ = false;
        });
        break;
    }
}

void MenuBackdrops::advance() {
    if (entries_.empty()) return;
    for (size_t step = 1; step <= entries_.size(); ++step) {
        const size_t i = (size_t(current_ < 0 ? 0 : current_) + step) % entries_.size();
        if (entries_[i].texture) { current_ = int(i); return; }
    }
}

bool MenuBackdrops::current(Shown& out) const {
    if (current_ < 0 || size_t(current_) >= entries_.size() || !entries_[size_t(current_)].texture) return false;
    const Entry& e = entries_[size_t(current_)];
    out.texture = e.texture;
    out.width = float(e.width);
    out.height = float(e.height);
    out.tint = e.tint;
    out.accent = e.accent;
    return true;
}

int MenuBackdrops::ready_count() const {
    int n = 0;
    for (const Entry& e : entries_) n += e.texture != nullptr;
    return n;
}

void MenuBackdrops::shutdown() {
    cancel_ = true;
    if (worker_.joinable()) worker_.join();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        finished_.clear();
    }
    for (Entry& e : entries_) {
        if (e.texture) SDL_DestroyTexture(e.texture);
        e.texture = nullptr;
    }
    current_ = -1;
    loaded_ = false;
}

} // namespace chaotix
