/**
 * @file     section_emitters.cpp
 * @brief    WebAssembly module section emitters
 *
 * @ingroup  Backend / WASM
 * @author   Manning Cyrus
 * @date     2026-04-28
 */
#include <cstdint>
#include <vector>

#include "backends/wasm/include/internal/wasm_constants.h"
#include "backends/wasm/include/wasm_target.h"

namespace polyglot::backends::wasm {

using internal::kOpEnd;
using internal::kOpI32Const;

void WasmTarget::EmitTypeSection(std::vector<std::uint8_t> &out) {
  if (types_.empty())
    return;
  std::vector<std::uint8_t> payload;
  EmitU32Leb128(payload, static_cast<std::uint32_t>(types_.size()));
  for (auto &ft : types_) {
    payload.push_back(0x60); // func type marker
    EmitU32Leb128(payload, static_cast<std::uint32_t>(ft.params.size()));
    for (auto p : ft.params)
      payload.push_back(static_cast<std::uint8_t>(p));
    EmitU32Leb128(payload, static_cast<std::uint32_t>(ft.results.size()));
    for (auto r : ft.results)
      payload.push_back(static_cast<std::uint8_t>(r));
  }
  EmitSection(out, WasmSectionId::kType, payload);
}

void WasmTarget::EmitImportSection(std::vector<std::uint8_t> &out) {
  if (imports_.empty())
    return;
  std::vector<std::uint8_t> payload;
  EmitU32Leb128(payload, static_cast<std::uint32_t>(imports_.size()));
  for (auto &imp : imports_) {
    EmitString(payload, imp.module);
    EmitString(payload, imp.name);
    payload.push_back(static_cast<std::uint8_t>(imp.kind));
    EmitU32Leb128(payload, imp.type_index);
  }
  EmitSection(out, WasmSectionId::kImport, payload);
}

void WasmTarget::EmitFunctionSection(std::vector<std::uint8_t> &out) {
  if (func_type_indices_.empty())
    return;
  std::vector<std::uint8_t> payload;
  EmitU32Leb128(payload, static_cast<std::uint32_t>(func_type_indices_.size()));
  for (auto idx : func_type_indices_) {
    EmitU32Leb128(payload, idx);
  }
  EmitSection(out, WasmSectionId::kFunction, payload);
}

void WasmTarget::EmitMemorySection(std::vector<std::uint8_t> &out) {
  // Emit a single linear memory with 1 page minimum, no maximum
  std::vector<std::uint8_t> payload;
  EmitU32Leb128(payload, 1); // 1 memory
  payload.push_back(0x00);   // flags: no maximum
  EmitU32Leb128(payload, 1); // 1 initial page (64KiB)
  EmitSection(out, WasmSectionId::kMemory, payload);
}

void WasmTarget::EmitGlobalSection(std::vector<std::uint8_t> &out) {
  // Emit the shadow stack pointer global (__stack_pointer).
  // Initialised to the top of the first memory page (65536) so that the
  // stack grows downward, consistent with the LLVM WASM ABI convention.
  std::vector<std::uint8_t> payload;
  EmitU32Leb128(payload, 1);                                       // 1 global
  payload.push_back(static_cast<std::uint8_t>(WasmValType::kI32)); // type: i32
  payload.push_back(0x01);                                         // mutable
  // init_expr: i32.const 65536; end
  payload.push_back(kOpI32Const);
  EmitI32Leb128(payload, 65536);
  payload.push_back(kOpEnd);
  EmitSection(out, WasmSectionId::kGlobal, payload);
}

void WasmTarget::EmitExportSection(std::vector<std::uint8_t> &out) {
  if (exports_.empty())
    return;
  std::vector<std::uint8_t> payload;
  EmitU32Leb128(payload, static_cast<std::uint32_t>(exports_.size()));
  for (auto &exp : exports_) {
    EmitString(payload, exp.name);
    payload.push_back(static_cast<std::uint8_t>(exp.kind));
    EmitU32Leb128(payload, exp.index);
  }
  EmitSection(out, WasmSectionId::kExport, payload);
}

void WasmTarget::EmitCodeSection(std::vector<std::uint8_t> &out) {
  if (func_bodies_.empty())
    return;
  std::vector<std::uint8_t> payload;
  EmitU32Leb128(payload, static_cast<std::uint32_t>(func_bodies_.size()));
  for (auto &body : func_bodies_) {
    // Each function body is prefixed by its size
    EmitU32Leb128(payload, static_cast<std::uint32_t>(body.size()));
    payload.insert(payload.end(), body.begin(), body.end());
  }
  EmitSection(out, WasmSectionId::kCode, payload);
}

}  // namespace polyglot::backends::wasm
