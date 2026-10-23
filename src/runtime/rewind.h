// Rewind (issue #40): a ring of machine snapshots to step back through.
//
// Snapshots are raw save states without the picture (runtime/save_state.h).
// Only the newest is kept whole; every older one is kept as its XOR against
// the next newer one, compressed (runtime/lz.h). Two snapshots a few frames
// apart differ in a few kilobytes, so the XOR is almost all zeros and packs
// down to a small fraction of the ~1 MB raw state. Stepping back undoes one
// delta at a time; the oldest is simply dropped when the memory budget is
// reached (nothing depends on it).
//
// The owner decides how often to push (every few frames) and what to do
// after a step back (the app runs one frame, muted, to have a picture).
#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace chaotix {

class Machine;

class RewindBuffer {
public:
    // Total memory the buffer may hold, deltas and working buffers together.
    void set_budget(size_t bytes);
    size_t budget() const { return budget_; }
    // And at most this many snapshots, however small they are.
    void set_max_snapshots(size_t n);
    // Forgets every snapshot; with release_memory the working buffers (about
    // three raw states) are given back too.
    void clear(bool release_memory = false);

    // Takes a snapshot of m (between frames).
    void push(const Machine& m);
    // Puts m back one snapshot: to the newest one if m has run on since it
    // was taken, otherwise to the one before it. Returns false, leaving m at
    // the oldest snapshot, when there is nothing older; false and m untouched
    // when the buffer is empty.
    bool step_back(Machine& m);

    size_t snapshots() const { return has_newest_ ? deltas_.size() + 1 : 0; }
    // Bytes held: the compressed deltas plus the working buffers.
    size_t memory() const;
    uint64_t oldest_frame() const;
    uint64_t newest_frame() const { return newest_frame_; }

private:
    void restore(Machine& m);
    void trim();

    struct Delta {
        std::vector<uint8_t> data;  // lz(older XOR newer)
        uint64_t frame = 0;         // the older snapshot's frame
    };
    size_t budget_ = 64u << 20;
    size_t max_snapshots_ = ~size_t(0);
    std::vector<uint8_t> newest_, scratch_, packed_;
    std::deque<Delta> deltas_;
    size_t delta_bytes_ = 0;
    uint64_t newest_frame_ = 0;
    bool has_newest_ = false;
    bool restored_ = false;  // m was put back to newest_ and has not been pushed since
};

} // namespace chaotix
