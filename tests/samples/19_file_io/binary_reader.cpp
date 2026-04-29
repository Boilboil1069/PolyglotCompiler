// binary_reader.cpp — Pull-style binary reader bounded by a maximum chunk size.
// Part of the PolyglotCompiler sample matrix.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

extern "C" {

size_t polyglot_read_chunk(const uint8_t *src, size_t src_len,
                            uint8_t *dst, size_t dst_cap) {
    size_t copy = src_len < dst_cap ? src_len : dst_cap;
    for (size_t i = 0; i < copy; ++i) {
        dst[i] = src[i];
    }
    return copy;
}


}  // extern "C"
