#include "runtime/include/interop/marshalling.h"

namespace polyglot::runtime::interop {

std::vector<uint8_t> MarshalInt(uint64_t value, size_t size, Endianness endianness) {
  std::vector<uint8_t> out(size, 0);
  for (size_t i = 0; i < size; ++i) {
    size_t idx = (endianness == Endianness::kLittle) ? i : (size - 1 - i);
    out[idx] = static_cast<uint8_t>((value >> (i * 8)) & 0xFF);
  }
  return out;
}

uint64_t UnmarshalInt(const std::vector<uint8_t> &bytes, Endianness endianness) {
  uint64_t value = 0;
  size_t size = bytes.size();
  for (size_t i = 0; i < size && i < 8; ++i) {
    size_t idx = (endianness == Endianness::kLittle) ? i : (size - 1 - i);
    value |= static_cast<uint64_t>(bytes[idx]) << (i * 8);
  }
  return value;
}

}  // namespace polyglot::runtime::interop
