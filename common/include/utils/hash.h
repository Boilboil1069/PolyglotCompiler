/**
 * @file     hash.h
 * @brief    Shared utility classes
 *
 * @ingroup  Common / Utils
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <cstddef>

namespace polyglot::utils {

inline void HashCombine(std::size_t &seed, std::size_t value) {
  seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

}  // namespace polyglot::utils
