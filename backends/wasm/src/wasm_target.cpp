/**
 * @file     wasm_target.cpp
 * @brief    WebAssembly code generation — public binary emit entry point
 *
 * @ingroup  Backend / WASM
 * @author   Manning Cyrus
 * @date     2026-04-10
 *
 * @note  This translation unit hosts only the public ``EmitWasmBinary``
 *        entry point.  All other ``WasmTarget`` methods are split into
 *        dedicated TUs under ``backends/wasm/src/{encoding,lowering,
 *        sections}/`` and ``wat_printer.cpp``; opcode constants live in
 *        ``backends/wasm/include/internal/wasm_constants.h``.
 */
#include <cstdint>
#include <iostream>
#include <utility>
#include <vector>

#include "middle/include/ir/cfg.h"
#include "middle/include/ir/nodes/statements.h"

#include "backends/wasm/include/internal/wasm_constants.h"
#include "backends/wasm/include/wasm_target.h"

namespace polyglot::backends::wasm {

using internal::kWasmMagic;
using internal::kWasmVersion;

std::vector<std::uint8_t> WasmTarget::EmitWasmBinary() {
  // Reset accumulated state
  types_.clear();
  imports_.clear();
  exports_.clear();
  func_type_indices_.clear();
  func_bodies_.clear();
  func_name_to_index_.clear();
  has_shadow_stack_ = false;
  lowering_errors_.clear();

  if (!module_)
    return {};

  // Phase 1: Collect function type signatures and build type section
  std::uint32_t func_index = 0;
  for (auto &fn : module_->Functions()) {
    // Build function name → index map for call resolution
    func_name_to_index_[fn->name] = func_index;
    WasmFuncType ft;
    for (auto &pt : fn->param_types) {
      ft.params.push_back(IRTypeToWasm(pt));
    }
    if (fn->ret_type.kind != ir::IRTypeKind::kVoid) {
      ft.results.push_back(IRTypeToWasm(fn->ret_type));
    }

    // Deduplicate types
    std::uint32_t type_idx = static_cast<std::uint32_t>(types_.size());
    for (std::uint32_t i = 0; i < types_.size(); ++i) {
      if (types_[i].params == ft.params && types_[i].results == ft.results) {
        type_idx = i;
        break;
      }
    }
    if (type_idx == types_.size()) {
      types_.push_back(ft);
    }

    func_type_indices_.push_back(type_idx);

    // Export all functions (public visibility)
    WasmExport exp;
    exp.name = fn->name;
    exp.kind = WasmExportKind::kFunction;
    exp.index = func_index;
    exports_.push_back(exp);

    ++func_index;
  }

  // Phase 1.5: Scan for alloca instructions — if any function uses alloca,
  // we need a shadow stack global pointer (__stack_pointer).
  for (auto &fn : module_->Functions()) {
    for (auto &blk : fn->blocks) {
      for (auto &inst : blk->instructions) {
        if (dynamic_cast<ir::AllocaInstruction *>(inst.get())) {
          has_shadow_stack_ = true;
          break;
        }
      }
      if (has_shadow_stack_)
        break;
    }
    if (has_shadow_stack_)
      break;
  }

  // Phase 2: Lower function bodies
  for (auto &fn : module_->Functions()) {
    std::vector<std::uint8_t> body;
    LowerFunction(*fn, body);
    func_bodies_.push_back(std::move(body));
  }

  // Also export memory
  {
    WasmExport mem_export;
    mem_export.name = "memory";
    mem_export.kind = WasmExportKind::kMemory;
    mem_export.index = 0;
    exports_.push_back(mem_export);
  }

  // Report lowering diagnostics
  if (!lowering_errors_.empty()) {
    for (const auto &err : lowering_errors_) {
      std::cerr << "[wasm] " << err << "\n";
    }
  }

  // Phase 3: Assemble the binary
  std::vector<std::uint8_t> binary;

  // Magic number and version
  binary.insert(binary.end(), std::begin(kWasmMagic), std::end(kWasmMagic));
  binary.insert(binary.end(), std::begin(kWasmVersion), std::end(kWasmVersion));

  // Sections in canonical order
  EmitTypeSection(binary);
  EmitImportSection(binary);
  EmitFunctionSection(binary);
  EmitMemorySection(binary);
  if (has_shadow_stack_) {
    EmitGlobalSection(binary);
  }
  EmitExportSection(binary);
  EmitCodeSection(binary);

  return binary;
}

}  // namespace polyglot::backends::wasm