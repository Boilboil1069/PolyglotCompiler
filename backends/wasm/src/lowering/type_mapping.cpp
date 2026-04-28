/**
 * @file     type_mapping.cpp
 * @brief    IR type → WebAssembly value type mapping
 *
 * @ingroup  Backend / WASM
 * @author   Manning Cyrus
 * @date     2026-04-28
 */
#include "backends/wasm/include/wasm_target.h"

namespace polyglot::backends::wasm {

WasmValType WasmTarget::IRTypeToWasm(const ir::IRType &type) const {
  switch (type.kind) {
  case ir::IRTypeKind::kI1:
  case ir::IRTypeKind::kI8:
  case ir::IRTypeKind::kI16:
  case ir::IRTypeKind::kI32:
    return WasmValType::kI32;
  case ir::IRTypeKind::kI64:
    return WasmValType::kI64;
  case ir::IRTypeKind::kF32:
    return WasmValType::kF32;
  case ir::IRTypeKind::kF64:
    return WasmValType::kF64;
  case ir::IRTypeKind::kPointer:
  case ir::IRTypeKind::kReference:
    // Pointers are represented as i32 in wasm32
    return WasmValType::kI32;
  default:
    return WasmValType::kI64;
  }
}

}  // namespace polyglot::backends::wasm
