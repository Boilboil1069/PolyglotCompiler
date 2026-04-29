// greyscale_kernel.cpp — RGB-to-greyscale conversion with the Rec. 601 luma weights.
// Part of the PolyglotCompiler sample matrix.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

extern "C" {

void polyglot_to_grey(const uint8_t *rgb, size_t n_pixels,
                       uint8_t *out) {
    for (size_t i = 0; i < n_pixels; ++i) {
        const double r = rgb[i * 3 + 0];
        const double g = rgb[i * 3 + 1];
        const double b = rgb[i * 3 + 2];
        const double y = 0.299 * r + 0.587 * g + 0.114 * b;
        out[i] = static_cast<uint8_t>(y);
    }
}


}  // extern "C"
