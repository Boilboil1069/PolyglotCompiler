/**
 * @file     wasm_target.h
 * @brief    WebAssembly code generation
 *
 * @ingroup  Backend / WASM
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "backends/common/include/target_machine.h"
#include "middle/include/ir/ir_context.h"

namespace polyglot::backends::wasm {

// ============================================================================
// WebAssembly Section Types
// ============================================================================

/** @brief WasmSectionId enumeration. */
enum class WasmSectionId : std::uint8_t {
    kCustom    = 0,
    kType      = 1,
    kImport    = 2,
    kFunction  = 3,
    kTable     = 4,
    kMemory    = 5,
    kGlobal    = 6,
    kExport    = 7,
    kStart     = 8,
    kElement   = 9,
    kCode      = 10,
    kData      = 11,
    kDataCount = 12,
};

// ============================================================================
// WebAssembly Value Types
// ============================================================================

/** @brief WasmValType enumeration. */
enum class WasmValType : std::uint8_t {
    kI32       = 0x7F,
    kI64       = 0x7E,
    kF32       = 0x7D,
    kF64       = 0x7C,
    kV128      = 0x7B,
    kFuncRef   = 0x70,
    kExternRef = 0x6F,
};

// ============================================================================
// WebAssembly Export Kinds
// ============================================================================

/** @brief WasmExportKind enumeration. */
enum class WasmExportKind : std::uint8_t {
    kFunction = 0,
    kTable    = 1,
    kMemory   = 2,
    kGlobal   = 3,
};

// ============================================================================
// WebAssembly Function Signature
// ============================================================================

/** @brief WasmFuncType data structure. */
struct WasmFuncType {
    std::vector<WasmValType> params;
    std::vector<WasmValType> results;
};

// ============================================================================
// WebAssembly Import
// ============================================================================

/** @brief WasmImport data structure. */
struct WasmImport {
    std::string module;
    std::string name;
    WasmExportKind kind{WasmExportKind::kFunction};
    std::uint32_t type_index{0};
};

// ============================================================================
// WebAssembly Export
// ============================================================================

/** @brief WasmExport data structure. */
struct WasmExport {
    std::string name;
    WasmExportKind kind{WasmExportKind::kFunction};
    std::uint32_t index{0};
};

// ============================================================================
// WebAssembly Module Builder
// ============================================================================

/** @brief WasmTarget class. */
class WasmTarget : public polyglot::backends::TargetMachine {
 public:
    explicit WasmTarget(const polyglot::ir::IRContext *module = nullptr)
        : module_(module) {}

    void SetModule(const polyglot::ir::IRContext *module) { module_ = module; }

    std::string TargetTriple() const override { return "wasm32-unknown-unknown"; }
    std::string EmitAssembly() override;

    // Emit a complete WebAssembly binary module (.wasm)
    std::vector<std::uint8_t> EmitWasmBinary();

    // Emit a single instruction as WAT text (used by EmitAssembly)
    void EmitInstructionWAT(std::ostream &os,
                            const std::shared_ptr<ir::Instruction> &inst);

 private:
    // Section emitters
    void EmitTypeSection(std::vector<std::uint8_t> &out);
    void EmitImportSection(std::vector<std::uint8_t> &out);
    void EmitFunctionSection(std::vector<std::uint8_t> &out);
    void EmitMemorySection(std::vector<std::uint8_t> &out);
    void EmitGlobalSection(std::vector<std::uint8_t> &out);
    void EmitExportSection(std::vector<std::uint8_t> &out);
    void EmitCodeSection(std::vector<std::uint8_t> &out);

    // IR → Wasm lowering helpers
    WasmValType IRTypeToWasm(const ir::IRType &type) const;
    void LowerFunction(const ir::Function &fn, std::vector<std::uint8_t> &body);
    void LowerInstruction(const std::shared_ptr<ir::Instruction> &inst,
                          std::vector<std::uint8_t> &body);

    // LEB128 encoding helpers
    static void EmitU32Leb128(std::vector<std::uint8_t> &out, std::uint32_t value);
    static void EmitI32Leb128(std::vector<std::uint8_t> &out, std::int32_t value);
    static void EmitI64Leb128(std::vector<std::uint8_t> &out, std::int64_t value);
    static void EmitSection(std::vector<std::uint8_t> &out, WasmSectionId id,
                            const std::vector<std::uint8_t> &payload);
    static void EmitString(std::vector<std::uint8_t> &out, const std::string &str);

    const polyglot::ir::IRContext *module_{nullptr};

    // Accumulated module-level data
    std::vector<WasmFuncType> types_;
    std::vector<WasmImport>   imports_;
    std::vector<WasmExport>   exports_;
    std::vector<std::uint32_t> func_type_indices_;
    std::vector<std::vector<std::uint8_t>> func_bodies_;

    // Function name → WASM function index mapping (built during EmitWasmBinary)
    std::unordered_map<std::string, std::uint32_t> func_name_to_index_;

    // Shadow stack pointer global index for alloca lowering.
    // Set to 0 (the first global) when alloca instructions are present.
    std::uint32_t shadow_stack_global_{0};
    bool has_shadow_stack_{false};

    // Block depth tracking for structured control flow (br/br_if).
    // Maps IR basic block name → WASM block nesting depth.
    std::unordered_map<std::string, std::uint32_t> block_depth_map_;
    std::uint32_t current_block_depth_{0};

    // Diagnostic errors collected during lowering
    std::vector<std::string> lowering_errors_;
};

}  // namespace polyglot::backends::wasm
