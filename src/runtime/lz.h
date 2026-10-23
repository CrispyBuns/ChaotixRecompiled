// A small, fast LZ77 compressor for save states and rewind snapshots (issue
// #40). No library is needed, so every platform (Android and iOS included)
// builds it the same way.
//
// The format is LZ4's block format: a token byte (literal count in the high
// nibble, match length - 4 in the low one, 15 meaning "more bytes follow"),
// the literals, a 16-bit little-endian offset and the rest of the match
// length. The last sequence has literals only. Machine state is mostly long
// runs (zeroed RAM, an XOR against the previous snapshot), which this
// encodes as offset-1 matches at memory speed.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace chaotix::lz {

// Replaces `out` with the compressed form of src[0..n).
void compress(const uint8_t* src, size_t n, std::vector<uint8_t>& out);
// Decompresses into dst, which must be exactly the original size. False for
// input that is corrupt or does not fill dst exactly; never reads or writes
// out of bounds.
bool decompress(const uint8_t* src, size_t n, uint8_t* dst, size_t dst_size);

} // namespace chaotix::lz
