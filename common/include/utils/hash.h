#pragma once

#include <cstddef>

namespace polyglot::utils {

inline void HashCombine(std::size_t &seed, std::size_t value) {
  seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

}  // namespace polyglot::utils
