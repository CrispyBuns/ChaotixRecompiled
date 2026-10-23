#include "runtime/rewind.h"

#include "runtime/lz.h"
#include "runtime/save_state.h"
#include "runtime/system.h"

#include <cstring>

namespace chaotix {

namespace {

// dst ^= src, eight bytes at a time.
void xor_into(uint8_t* dst, const uint8_t* src, size_t n) {
    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        uint64_t a, b;
        std::memcpy(&a, dst + i, 8);
        std::memcpy(&b, src + i, 8);
        a ^= b;
        std::memcpy(dst + i, &a, 8);
    }
    for (; i < n; ++i) dst[i] ^= src[i];
}

} // namespace

void RewindBuffer::set_budget(size_t bytes) {
    budget_ = bytes;
    trim();
}

void RewindBuffer::set_max_snapshots(size_t n) {
    max_snapshots_ = n < 1 ? 1 : n;
    trim();
}

void RewindBuffer::clear(bool release_memory) {
    if (release_memory) {
        std::vector<uint8_t>().swap(newest_);
        std::vector<uint8_t>().swap(scratch_);
        std::vector<uint8_t>().swap(packed_);
        std::deque<Delta>().swap(deltas_);
    }
    deltas_.clear();
    delta_bytes_ = 0;
    has_newest_ = false;
    restored_ = false;
    newest_frame_ = 0;
}

size_t RewindBuffer::memory() const {
    // Each delta's vector costs a little besides its bytes.
    return delta_bytes_ + deltas_.size() * sizeof(Delta) + newest_.capacity() + scratch_.capacity() +
           packed_.capacity();
}

uint64_t RewindBuffer::oldest_frame() const { return deltas_.empty() ? newest_frame_ : deltas_.front().frame; }

void RewindBuffer::push(const Machine& m) {
    restored_ = false;
    savestate::serialize(m, scratch_, 0);
    if (has_newest_ && scratch_.size() == newest_.size()) {
        // newest_ becomes the older one's delta: newest XOR new, compressed.
        xor_into(newest_.data(), scratch_.data(), newest_.size());
        lz::compress(newest_.data(), newest_.size(), packed_);
        Delta d;
        d.data.assign(packed_.begin(), packed_.end());  // exactly its size, not packed_'s capacity
        d.frame = newest_frame_;
        delta_bytes_ += d.data.size();
        deltas_.push_back(std::move(d));
    } else {
        deltas_.clear();
        delta_bytes_ = 0;
    }
    newest_.swap(scratch_);
    newest_frame_ = m.frame_count;
    has_newest_ = true;
    trim();
}

void RewindBuffer::trim() {
    while (!deltas_.empty() && (memory() > budget_ || deltas_.size() + 1 > max_snapshots_)) {
        delta_bytes_ -= deltas_.front().data.size();
        deltas_.pop_front();
    }
}

void RewindBuffer::restore(Machine& m) {
    savestate::deserialize(m, newest_.data(), newest_.size(), 0, nullptr);
    restored_ = true;
}

bool RewindBuffer::step_back(Machine& m) {
    if (!has_newest_) return false;
    // The first step from where the game has got to goes back to the newest
    // snapshot; after that, each step goes one further.
    if (!restored_ && m.frame_count != newest_frame_) {
        restore(m);
        return true;
    }
    if (deltas_.empty()) {
        restore(m);
        return false;
    }
    Delta& d = deltas_.back();
    if (scratch_.size() != newest_.size()) scratch_.resize(newest_.size());
    if (!lz::decompress(d.data.data(), d.data.size(), scratch_.data(), scratch_.size())) {
        // Cannot happen with data this class wrote; give up on the history
        // rather than restore something wrong.
        deltas_.clear();
        delta_bytes_ = 0;
        restore(m);
        return false;
    }
    xor_into(newest_.data(), scratch_.data(), newest_.size());
    newest_frame_ = d.frame;
    delta_bytes_ -= d.data.size();
    deltas_.pop_back();
    restore(m);
    return true;
}

} // namespace chaotix
