/**
 * @file     leb128.cpp
 * @brief    LEB128 + section-frame encoding helpers for the WASM backend
 *
 * @ingroup  Backend / WASM
 * @author   Manning Cyrus
 * @date     2026-04-28
 */
#include <cstdint>
#include <string>
#include <vector>

#include "backends/wasm/include/wasm_target.h"

namespace polyglot::backends::wasm {

void WasmTarget::EmitU32Leb128(std::vector<std::uint8_t> &out, std::uint32_t value) {
  do {
    std::uint8_t byte = value & 0x7F;
    value >>= 7;
    if (value != 0)
      byte |= 0x80;
    out.push_back(byte);
  } while (value != 0);
}

void WasmTarget::EmitI32Leb128(std::vector<std::uint8_t> &out, std::int32_t value) {
  bool more = true;
  while (more) {
    std::uint8_t byte = value & 0x7F;
    value >>= 7;
    if ((value == 0 && (byte & 0x40) == 0) || (value == -1 && (byte & 0x40) != 0)) {
      more = false;
    } else {
      byte |= 0x80;
    }
    out.push_back(byte);
  }
}

void WasmTarget::EmitI64Leb128(std::vector<std::uint8_t> &out, std::int64_t value) {
  bool more = true;
  while (more) {
    std::uint8_t byte = value & 0x7F;
    value >>= 7;
    if ((value == 0 && (byte & 0x40) == 0) || (value == -1 && (byte & 0x40) != 0)) {
      more = false;
    } else {
      byte |= 0x80;
    }
    out.push_back(byte);
  }
}

void WasmTarget::EmitString(std::vector<std::uint8_t> &out, const std::string &str) {
  EmitU32Leb128(out, static_cast<std::uint32_t>(str.size()));
  out.insert(out.end(), str.begin(), str.end());
}

void WasmTarget::EmitSection(std::vector<std::uint8_t> &out, WasmSectionId id,
                             const std::vector<std::uint8_t> &payload) {
  out.push_back(static_cast<std::uint8_t>(id));
  EmitU32Leb128(out, static_cast<std::uint32_t>(payload.size()));
  out.insert(out.end(), payload.begin(), payload.end());
}

}  // namespace polyglot::backends::wasm
