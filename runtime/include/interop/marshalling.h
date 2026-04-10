/**
 * @file     marshalling.h
 * @brief    Cross-language interoperability
 *
 * @ingroup  Runtime / Interop
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace polyglot::runtime::interop {

/** @brief Endianness enumeration. */
enum class Endianness { kLittle, kBig };

/** @brief Marshalling data structure. */
struct Marshalling {
  size_t size{0};
  size_t alignment{1};
  Endianness endianness{Endianness::kLittle};
};

// Marshal an integral value into a byte vector with the requested endianness and width.
std::vector<uint8_t> MarshalInt(uint64_t value, size_t size, Endianness endianness);
// Unmarshal an integral value from bytes with the requested endianness and width.
uint64_t UnmarshalInt(const std::vector<uint8_t> &bytes, Endianness endianness);

}  // namespace polyglot::runtime::interop
