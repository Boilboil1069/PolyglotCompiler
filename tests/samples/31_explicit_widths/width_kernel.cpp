/**
 * @file     width_kernel.cpp
 * @brief    Host kernel demonstrating cross-language consumption of
 *           ploy-side explicit-width buffers (`i32` / `u32`).
 *
 * @ingroup  Samples / 31_explicit_widths
 * @author   Manning Cyrus
 * @date     2026-04-29
 */

#include <cstddef>
#include <cstdint>

extern "C" std::int64_t sum_pixels(const std::int32_t *pixels,
                                   std::uint32_t        channel_count,
                                   std::size_t          element_count) {
  // Width contract: i32 in -> i64 out so the accumulator cannot overflow
  // for any sample-sized input.  channel_count is u32 to match the ploy
  // alias `ChannelCount = u32`; the ABI layer guarantees zero-extension.
  std::int64_t accumulator = 0;
  for (std::size_t i = 0; i < element_count; ++i) {
    accumulator += static_cast<std::int64_t>(pixels[i]);
  }
  return accumulator * static_cast<std::int64_t>(channel_count);
}
