#pragma once

#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

#include "backends/common/include/target_machine.h"
#include "middle/include/ir/ir_context.h"

namespace polyglot::backends::wasm {

// ============================================================================
// WebAssembly Section Types
// ============================================================================

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

enum class WasmExportKind : std::uint8_t {
    kFunction = 0,
    kTable    = 1,
    kMemory   = 2,
    kGlobal   = 3,
};

// ============================================================================
// WebAssembly Function Signature
// ============================================================================

struct WasmFuncType {
    std::vector<WasmValType> params;
    std::vector<WasmValType> results;
};

// ============================================================================
// WebAssembly Import
// ============================================================================

struct WasmImport {
    std::string module;
    std::string name;
    WasmExportKind kind{WasmExportKind::kFunction};
    std::uint32_t type_index{0};
};

// ============================================================================
// WebAssembly Export
// ============================================================================

struct WasmExport {
    std::string name;
    WasmExportKind kind{WasmExportKind::kFunction};
    std::uint32_t index{0};
};

// ============================================================================
// WebAssembly Module Builder
// ============================================================================

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
};

}  // namespace polyglot::backends::wasm
