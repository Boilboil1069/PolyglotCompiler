/**
 * @file     wasm_target.cpp
 * @brief    WebAssembly code generation implementation
 *
 * @ingroup  Backend / WASM
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include "backends/wasm/include/wasm_target.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "middle/include/ir/cfg.h"
#include "middle/include/ir/nodes/statements.h"

namespace polyglot::backends::wasm {

// ============================================================================
// WebAssembly Binary Format Constants
// ============================================================================

[[maybe_unused]] static constexpr std::uint8_t kWasmMagic[] = {0x00, 0x61, 0x73, 0x6D};
[[maybe_unused]] static constexpr std::uint8_t kWasmVersion[] = {0x01, 0x00, 0x00, 0x00};

// Wasm opcode constants
[[maybe_unused]] static constexpr std::uint8_t kOpUnreachable = 0x00;
[[maybe_unused]] static constexpr std::uint8_t kOpNop         = 0x01;
[[maybe_unused]] static constexpr std::uint8_t kOpBlock       = 0x02;
[[maybe_unused]] static constexpr std::uint8_t kOpLoop        = 0x03;
[[maybe_unused]] static constexpr std::uint8_t kOpIf          = 0x04;
[[maybe_unused]] static constexpr std::uint8_t kOpElse        = 0x05;
[[maybe_unused]] static constexpr std::uint8_t kOpEnd         = 0x0B;
[[maybe_unused]] static constexpr std::uint8_t kOpBr          = 0x0C;
[[maybe_unused]] static constexpr std::uint8_t kOpBrIf        = 0x0D;
[[maybe_unused]] static constexpr std::uint8_t kOpReturn      = 0x0F;
[[maybe_unused]] static constexpr std::uint8_t kOpCall        = 0x10;
[[maybe_unused]] static constexpr std::uint8_t kOpDrop        = 0x1A;
[[maybe_unused]] static constexpr std::uint8_t kOpSelect      = 0x1B;
[[maybe_unused]] static constexpr std::uint8_t kOpLocalGet    = 0x20;
[[maybe_unused]] static constexpr std::uint8_t kOpLocalSet    = 0x21;
[[maybe_unused]] static constexpr std::uint8_t kOpLocalTee    = 0x22;
[[maybe_unused]] static constexpr std::uint8_t kOpGlobalGet   = 0x23;
[[maybe_unused]] static constexpr std::uint8_t kOpGlobalSet   = 0x24;
[[maybe_unused]] static constexpr std::uint8_t kOpI32Load     = 0x28;
[[maybe_unused]] static constexpr std::uint8_t kOpI64Load     = 0x29;
[[maybe_unused]] static constexpr std::uint8_t kOpF32Load     = 0x2A;
[[maybe_unused]] static constexpr std::uint8_t kOpF64Load     = 0x2B;
[[maybe_unused]] static constexpr std::uint8_t kOpI32Store    = 0x36;
[[maybe_unused]] static constexpr std::uint8_t kOpI64Store    = 0x37;
[[maybe_unused]] static constexpr std::uint8_t kOpF32Store    = 0x38;
[[maybe_unused]] static constexpr std::uint8_t kOpF64Store    = 0x39;
[[maybe_unused]] static constexpr std::uint8_t kOpI32Const    = 0x41;
[[maybe_unused]] static constexpr std::uint8_t kOpI64Const    = 0x42;
[[maybe_unused]] static constexpr std::uint8_t kOpF32Const    = 0x43;
[[maybe_unused]] static constexpr std::uint8_t kOpF64Const    = 0x44;
[[maybe_unused]] static constexpr std::uint8_t kOpI32Eqz      = 0x45;
[[maybe_unused]] static constexpr std::uint8_t kOpI32Eq       = 0x46;
[[maybe_unused]] static constexpr std::uint8_t kOpI32Ne       = 0x47;
[[maybe_unused]] static constexpr std::uint8_t kOpI32LtS      = 0x48;
[[maybe_unused]] static constexpr std::uint8_t kOpI32LtU      = 0x49;
[[maybe_unused]] static constexpr std::uint8_t kOpI32GtS      = 0x4A;
[[maybe_unused]] static constexpr std::uint8_t kOpI32GtU      = 0x4B;
[[maybe_unused]] static constexpr std::uint8_t kOpI32LeS      = 0x4C;
[[maybe_unused]] static constexpr std::uint8_t kOpI32LeU      = 0x4D;
[[maybe_unused]] static constexpr std::uint8_t kOpI32GeS      = 0x4E;
[[maybe_unused]] static constexpr std::uint8_t kOpI32GeU      = 0x4F;
[[maybe_unused]] static constexpr std::uint8_t kOpI64Eqz      = 0x50;
[[maybe_unused]] static constexpr std::uint8_t kOpI64Eq       = 0x51;
[[maybe_unused]] static constexpr std::uint8_t kOpI64Ne       = 0x52;
[[maybe_unused]] static constexpr std::uint8_t kOpI64LtS      = 0x53;
[[maybe_unused]] static constexpr std::uint8_t kOpI64LtU      = 0x54;
[[maybe_unused]] static constexpr std::uint8_t kOpI64GtS      = 0x55;
[[maybe_unused]] static constexpr std::uint8_t kOpI64GtU      = 0x56;
[[maybe_unused]] static constexpr std::uint8_t kOpI64LeS      = 0x57;
[[maybe_unused]] static constexpr std::uint8_t kOpI64LeU      = 0x58;
[[maybe_unused]] static constexpr std::uint8_t kOpI64GeS      = 0x59;
[[maybe_unused]] static constexpr std::uint8_t kOpI64GeU      = 0x5A;
[[maybe_unused]] static constexpr std::uint8_t kOpI32Add      = 0x6A;
[[maybe_unused]] static constexpr std::uint8_t kOpI32Sub      = 0x6B;
[[maybe_unused]] static constexpr std::uint8_t kOpI32Mul      = 0x6C;
[[maybe_unused]] static constexpr std::uint8_t kOpI32DivS     = 0x6D;
[[maybe_unused]] static constexpr std::uint8_t kOpI32DivU     = 0x6E;
[[maybe_unused]] static constexpr std::uint8_t kOpI32RemS     = 0x6F;
[[maybe_unused]] static constexpr std::uint8_t kOpI32RemU     = 0x70;
[[maybe_unused]] static constexpr std::uint8_t kOpI32And      = 0x71;
[[maybe_unused]] static constexpr std::uint8_t kOpI32Or       = 0x72;
[[maybe_unused]] static constexpr std::uint8_t kOpI32Xor      = 0x73;
[[maybe_unused]] static constexpr std::uint8_t kOpI32Shl      = 0x74;
[[maybe_unused]] static constexpr std::uint8_t kOpI32ShrS     = 0x75;
[[maybe_unused]] static constexpr std::uint8_t kOpI32ShrU     = 0x76;
[[maybe_unused]] static constexpr std::uint8_t kOpI64Add      = 0x7C;
[[maybe_unused]] static constexpr std::uint8_t kOpI64Sub      = 0x7D;
[[maybe_unused]] static constexpr std::uint8_t kOpI64Mul      = 0x7E;
[[maybe_unused]] static constexpr std::uint8_t kOpI64DivS     = 0x7F;
[[maybe_unused]] static constexpr std::uint8_t kOpI64DivU     = 0x80;
[[maybe_unused]] static constexpr std::uint8_t kOpI64RemS     = 0x81;
[[maybe_unused]] static constexpr std::uint8_t kOpI64RemU     = 0x82;
[[maybe_unused]] static constexpr std::uint8_t kOpI64And      = 0x83;
[[maybe_unused]] static constexpr std::uint8_t kOpI64Or       = 0x84;
[[maybe_unused]] static constexpr std::uint8_t kOpI64Xor      = 0x85;
[[maybe_unused]] static constexpr std::uint8_t kOpI64Shl      = 0x86;
[[maybe_unused]] static constexpr std::uint8_t kOpI64ShrS     = 0x87;
[[maybe_unused]] static constexpr std::uint8_t kOpI64ShrU     = 0x88;
[[maybe_unused]] static constexpr std::uint8_t kOpF64Abs      = 0x99;
[[maybe_unused]] static constexpr std::uint8_t kOpF64Neg      = 0x9A;
[[maybe_unused]] static constexpr std::uint8_t kOpF64Ceil     = 0x9B;
[[maybe_unused]] static constexpr std::uint8_t kOpF64Floor    = 0x9C;
[[maybe_unused]] static constexpr std::uint8_t kOpF64Trunc    = 0x9D;
[[maybe_unused]] static constexpr std::uint8_t kOpF64Nearest  = 0x9E;
[[maybe_unused]] static constexpr std::uint8_t kOpF64Copysign = 0xA6;
[[maybe_unused]] static constexpr std::uint8_t kOpF64Add      = 0xA0;
[[maybe_unused]] static constexpr std::uint8_t kOpF64Sub      = 0xA1;
[[maybe_unused]] static constexpr std::uint8_t kOpF64Mul      = 0xA2;
[[maybe_unused]] static constexpr std::uint8_t kOpF64Div      = 0xA3;
[[maybe_unused]] static constexpr std::uint8_t kOpF64Eq       = 0x61;
[[maybe_unused]] static constexpr std::uint8_t kOpF64Ne       = 0x62;
[[maybe_unused]] static constexpr std::uint8_t kOpF64Lt       = 0x63;
[[maybe_unused]] static constexpr std::uint8_t kOpF64Le       = 0x65;
[[maybe_unused]] static constexpr std::uint8_t kOpF64Gt       = 0x64;
[[maybe_unused]] static constexpr std::uint8_t kOpF64Ge       = 0x66;
[[maybe_unused]] static constexpr std::uint8_t kOpI32WrapI64  = 0xA7;
[[maybe_unused]] static constexpr std::uint8_t kOpI64ExtendI32S = 0xAC;
[[maybe_unused]] static constexpr std::uint8_t kOpI64ExtendI32U = 0xAD;

// Block type void
[[maybe_unused]] static constexpr std::uint8_t kBlockTypeVoid = 0x40;

// ============================================================================
// LEB128 Encoding
// ============================================================================

void WasmTarget::EmitU32Leb128(std::vector<std::uint8_t> &out, std::uint32_t value) {
    do {
        std::uint8_t byte = value & 0x7F;
        value >>= 7;
        if (value != 0) byte |= 0x80;
        out.push_back(byte);
    } while (value != 0);
}

void WasmTarget::EmitI32Leb128(std::vector<std::uint8_t> &out, std::int32_t value) {
    bool more = true;
    while (more) {
        std::uint8_t byte = value & 0x7F;
        value >>= 7;
        if ((value == 0 && (byte & 0x40) == 0) ||
            (value == -1 && (byte & 0x40) != 0)) {
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
        if ((value == 0 && (byte & 0x40) == 0) ||
            (value == -1 && (byte & 0x40) != 0)) {
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

// ============================================================================
// IR Type → Wasm Value Type Mapping
// ============================================================================

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

// ============================================================================
// Section Emitters
// ============================================================================

void WasmTarget::EmitTypeSection(std::vector<std::uint8_t> &out) {
    if (types_.empty()) return;
    std::vector<std::uint8_t> payload;
    EmitU32Leb128(payload, static_cast<std::uint32_t>(types_.size()));
    for (auto &ft : types_) {
        payload.push_back(0x60);  // func type marker
        EmitU32Leb128(payload, static_cast<std::uint32_t>(ft.params.size()));
        for (auto p : ft.params) payload.push_back(static_cast<std::uint8_t>(p));
        EmitU32Leb128(payload, static_cast<std::uint32_t>(ft.results.size()));
        for (auto r : ft.results) payload.push_back(static_cast<std::uint8_t>(r));
    }
    EmitSection(out, WasmSectionId::kType, payload);
}

void WasmTarget::EmitImportSection(std::vector<std::uint8_t> &out) {
    if (imports_.empty()) return;
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
    if (func_type_indices_.empty()) return;
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
    EmitU32Leb128(payload, 1);  // 1 memory
    payload.push_back(0x00);    // flags: no maximum
    EmitU32Leb128(payload, 1);  // 1 initial page (64KiB)
    EmitSection(out, WasmSectionId::kMemory, payload);
}

void WasmTarget::EmitGlobalSection(std::vector<std::uint8_t> &out) {
    // Emit the shadow stack pointer global (__stack_pointer).
    // Initialised to the top of the first memory page (65536) so that the
    // stack grows downward, consistent with the LLVM WASM ABI convention.
    std::vector<std::uint8_t> payload;
    EmitU32Leb128(payload, 1);             // 1 global
    payload.push_back(static_cast<std::uint8_t>(WasmValType::kI32));  // type: i32
    payload.push_back(0x01);               // mutable
    // init_expr: i32.const 65536; end
    payload.push_back(kOpI32Const);
    EmitI32Leb128(payload, 65536);
    payload.push_back(kOpEnd);
    EmitSection(out, WasmSectionId::kGlobal, payload);
}

void WasmTarget::EmitExportSection(std::vector<std::uint8_t> &out) {
    if (exports_.empty()) return;
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
    if (func_bodies_.empty()) return;
    std::vector<std::uint8_t> payload;
    EmitU32Leb128(payload, static_cast<std::uint32_t>(func_bodies_.size()));
    for (auto &body : func_bodies_) {
        // Each function body is prefixed by its size
        EmitU32Leb128(payload, static_cast<std::uint32_t>(body.size()));
        payload.insert(payload.end(), body.begin(), body.end());
    }
    EmitSection(out, WasmSectionId::kCode, payload);
}

// ============================================================================
// IR Instruction → Wasm Bytecode
// ============================================================================

void WasmTarget::LowerInstruction(const std::shared_ptr<ir::Instruction> &inst,
                                   std::vector<std::uint8_t> &body) {
    // Binary instructions
    if (auto *bin = dynamic_cast<ir::BinaryInstruction *>(inst.get())) {
        using Op = ir::BinaryInstruction::Op;
        bool is_i64 = (bin->type.kind == ir::IRTypeKind::kI64);
        bool is_float = bin->type.IsFloat();
        switch (bin->op) {
            case Op::kAdd:  body.push_back(is_i64 ? kOpI64Add : kOpI32Add); break;
            case Op::kSub:  body.push_back(is_i64 ? kOpI64Sub : kOpI32Sub); break;
            case Op::kMul:  body.push_back(is_i64 ? kOpI64Mul : kOpI32Mul); break;
            case Op::kDiv:
            case Op::kSDiv: body.push_back(is_i64 ? kOpI64DivS : kOpI32DivS); break;
            case Op::kUDiv: body.push_back(is_i64 ? kOpI64DivU : kOpI32DivU); break;
            case Op::kRem:
            case Op::kSRem: body.push_back(is_i64 ? kOpI64RemS : kOpI32RemS); break;
            case Op::kURem: body.push_back(is_i64 ? kOpI64RemU : kOpI32RemU); break;
            case Op::kAnd:  body.push_back(is_i64 ? kOpI64And : kOpI32And); break;
            case Op::kOr:   body.push_back(is_i64 ? kOpI64Or  : kOpI32Or); break;
            case Op::kXor:  body.push_back(is_i64 ? kOpI64Xor : kOpI32Xor); break;
            case Op::kShl:  body.push_back(is_i64 ? kOpI64Shl : kOpI32Shl); break;
            case Op::kLShr: body.push_back(is_i64 ? kOpI64ShrU : kOpI32ShrU); break;
            case Op::kAShr: body.push_back(is_i64 ? kOpI64ShrS : kOpI32ShrS); break;
            case Op::kFAdd: body.push_back(kOpF64Add); break;
            case Op::kFSub: body.push_back(kOpF64Sub); break;
            case Op::kFMul: body.push_back(kOpF64Mul); break;
            case Op::kFDiv: body.push_back(kOpF64Div); break;
            case Op::kFRem: {
                // WASM has no frem instruction.  Lower as: x - trunc(x/y) * y
                // Stack before: [x, y]
                // We need to duplicate x and y.  Since WASM doesn't have dup,
                // we use local.tee + local.get if available, but for a simpler
                // approach we rely on the operands already being on the stack
                // twice (the IR builder should have arranged this).
                // Emit: x - trunc(x / y) * y
                // The operands are already duplicated by the emitter before
                // reaching this point, so we emit the arithmetic directly.
                // [x, y, x, y] -> div -> trunc -> [x, y, trunc(x/y)]
                // -> mul -> [x, trunc(x/y)*y] -> sub -> [x - trunc(x/y)*y]
                body.push_back(kOpF64Div);
                body.push_back(kOpF64Trunc);
                body.push_back(kOpF64Mul);
                body.push_back(kOpF64Sub);
                break;
            }
            case Op::kCmpEq:  body.push_back(is_float ? kOpF64Eq : (is_i64 ? kOpI64Eq : kOpI32Eq)); break;
            case Op::kCmpNe:  body.push_back(is_float ? kOpF64Ne : (is_i64 ? kOpI64Ne : kOpI32Ne)); break;
            case Op::kCmpLt:
            case Op::kCmpSlt: body.push_back(is_float ? kOpF64Lt : (is_i64 ? kOpI64LtS : kOpI32LtS)); break;
            case Op::kCmpSle: body.push_back(is_float ? kOpF64Le : (is_i64 ? kOpI64LeS : kOpI32LeS)); break;
            case Op::kCmpSgt: body.push_back(is_float ? kOpF64Gt : (is_i64 ? kOpI64GtS : kOpI32GtS)); break;
            case Op::kCmpSge: body.push_back(is_float ? kOpF64Ge : (is_i64 ? kOpI64GeS : kOpI32GeS)); break;
            case Op::kCmpUlt: body.push_back(is_i64 ? kOpI64LtU : kOpI32LtU); break;
            case Op::kCmpUle: body.push_back(is_i64 ? kOpI64LeU : kOpI32LeU); break;
            case Op::kCmpUgt: body.push_back(is_i64 ? kOpI64GtU : kOpI32GtU); break;
            case Op::kCmpUge: body.push_back(is_i64 ? kOpI64GeU : kOpI32GeU); break;
            case Op::kCmpFoe: body.push_back(kOpF64Eq); break;
            case Op::kCmpFne: body.push_back(kOpF64Ne); break;
            case Op::kCmpFlt: body.push_back(kOpF64Lt); break;
            case Op::kCmpFle: body.push_back(kOpF64Le); break;
            case Op::kCmpFgt: body.push_back(kOpF64Gt); break;
            case Op::kCmpFge: body.push_back(kOpF64Ge); break;
        }
        return;
    }

    // Return
    if (dynamic_cast<ir::ReturnStatement *>(inst.get())) {
        body.push_back(kOpReturn);
        return;
    }

    // Call
    if (auto *call = dynamic_cast<ir::CallInstruction *>(inst.get())) {
        // Resolve the callee name to its WASM function index.
        auto it = func_name_to_index_.find(call->callee);
        if (it != func_name_to_index_.end()) {
            body.push_back(kOpCall);
            EmitU32Leb128(body, it->second);
        } else {
            // Unresolved callee is a hard error — the resulting module would
            // trap or call the wrong function.  Record the error and emit
            // unreachable to guarantee a deterministic failure.
            lowering_errors_.push_back(
                "unresolved call target '" + call->callee +
                "'; cannot emit valid call instruction");
            body.push_back(kOpUnreachable);
        }
        return;
    }

    // Load
    if (auto *load = dynamic_cast<ir::LoadInstruction *>(inst.get())) {
        bool is_i64 = (load->type.kind == ir::IRTypeKind::kI64);
        bool is_f32 = (load->type.kind == ir::IRTypeKind::kF32);
        bool is_f64 = (load->type.kind == ir::IRTypeKind::kF64);
        if (is_f64)      body.push_back(kOpF64Load);
        else if (is_f32) body.push_back(kOpF32Load);
        else if (is_i64) body.push_back(kOpI64Load);
        else             body.push_back(kOpI32Load);
        // alignment + offset (both LEB128)
        std::uint32_t align_log2 = is_i64 || is_f64 ? 3 : 2;
        EmitU32Leb128(body, align_log2);
        EmitU32Leb128(body, 0);  // offset 0
        return;
    }

    // Store
    if (auto *store = dynamic_cast<ir::StoreInstruction *>(inst.get())) {
        // Determine value type from the second operand; default to i32
        bool is_i64 = false;
        bool is_f32 = false;
        bool is_f64 = false;
        if (!store->operands.empty()) {
            // The value operand type determines the store width
            auto vt = store->type;
            is_i64 = (vt.kind == ir::IRTypeKind::kI64);
            is_f32 = (vt.kind == ir::IRTypeKind::kF32);
            is_f64 = (vt.kind == ir::IRTypeKind::kF64);
        }
        if (is_f64)      body.push_back(kOpF64Store);
        else if (is_f32) body.push_back(kOpF32Store);
        else if (is_i64) body.push_back(kOpI64Store);
        else             body.push_back(kOpI32Store);
        std::uint32_t align_log2 = is_i64 || is_f64 ? 3 : 2;
        EmitU32Leb128(body, align_log2);
        EmitU32Leb128(body, 0);
        return;
    }

    // Cast
    if (auto *cast = dynamic_cast<ir::CastInstruction *>(inst.get())) {
        using CK = ir::CastInstruction::CastKind;
        switch (cast->cast) {
            case CK::kZExt:
            case CK::kIntToPtr:
                body.push_back(kOpI64ExtendI32U);
                break;
            case CK::kSExt:
                body.push_back(kOpI64ExtendI32S);
                break;
            case CK::kTrunc:
            case CK::kPtrToInt:
                body.push_back(kOpI32WrapI64);
                break;
            case CK::kBitcast:
            case CK::kFpExt:
            case CK::kFpTrunc:
                body.push_back(kOpNop);  // no-op for same-size reinterpret
                break;
        }
        return;
    }

    // Alloca — modeled via the WASM shadow stack.
    // The shadow stack pointer lives in global 0 (__stack_pointer).  An alloca
    // of `size` bytes is lowered as:
    //   global.get $__stack_pointer
    //   i32.const  <size>
    //   i32.sub
    //   local.tee  <result>       ;; the allocated address
    //   global.set $__stack_pointer
    // The caller is responsible for restoring the stack pointer on function
    // exit (see LowerFunction's epilogue).
    if (auto *alloca_inst = dynamic_cast<ir::AllocaInstruction *>(inst.get())) {
        // Determine allocation size from the pointed-to type
        std::uint32_t alloc_size = 16;  // default conservative alignment

        auto pointee = alloca_inst->type;
        if (pointee.kind == ir::IRTypeKind::kPointer && !pointee.subtypes.empty()) {
            auto &inner = pointee.subtypes[0];
            if (inner.kind == ir::IRTypeKind::kI32 || inner.kind == ir::IRTypeKind::kF32) alloc_size = 4;
            else if (inner.kind == ir::IRTypeKind::kI64 || inner.kind == ir::IRTypeKind::kF64) alloc_size = 8;
            else if (inner.kind == ir::IRTypeKind::kI8) alloc_size = 1;
            else if (inner.kind == ir::IRTypeKind::kI16) alloc_size = 2;
        }

        body.push_back(kOpGlobalGet);
        EmitU32Leb128(body, shadow_stack_global_);  // __stack_pointer
        body.push_back(kOpI32Const);
        EmitI32Leb128(body, static_cast<std::int32_t>(alloc_size));
        body.push_back(kOpI32Sub);
        // The result (new stack pointer value) is left on the operand stack
        // for subsequent use as the pointer.  Also write it back.
        body.push_back(kOpGlobalSet);
        EmitU32Leb128(body, shadow_stack_global_);
        // Push the new pointer again so the instruction has a result.
        body.push_back(kOpGlobalGet);
        EmitU32Leb128(body, shadow_stack_global_);
        return;
    }

    // Unreachable
    if (dynamic_cast<ir::UnreachableStatement *>(inst.get())) {
        body.push_back(kOpUnreachable);
        return;
    }

    // Branch (unconditional) → br
    if (auto *br = dynamic_cast<ir::BranchStatement *>(inst.get())) {
        body.push_back(kOpBr);
        // Compute relative block depth from current position to target
        std::uint32_t depth = 0;
        if (br->target && !br->target->name.empty()) {
            auto it = block_depth_map_.find(br->target->name);
            if (it != block_depth_map_.end()) {
                // depth = current nesting - target block index
                depth = current_block_depth_ > it->second
                            ? current_block_depth_ - it->second - 1
                            : 0;
            } else {
                lowering_errors_.push_back(
                    "branch target '" + br->target->name + "' not in block depth map");
            }
        }
        EmitU32Leb128(body, depth);
        return;
    }

    // Conditional branch → br_if
    if (auto *cbr = dynamic_cast<ir::CondBranchStatement *>(inst.get())) {
        body.push_back(kOpBrIf);
        std::uint32_t depth = 0;
        if (cbr->true_target && !cbr->true_target->name.empty()) {
            auto it = block_depth_map_.find(cbr->true_target->name);
            if (it != block_depth_map_.end()) {
                depth = current_block_depth_ > it->second
                            ? current_block_depth_ - it->second - 1
                            : 0;
            } else {
                lowering_errors_.push_back(
                    "br_if target '" + cbr->true_target->name + "' not in block depth map");
            }
        }
        EmitU32Leb128(body, depth);
        return;
    }

    // Unsupported instruction — emit unreachable trap and record diagnostic.
    // Previously this was a silent nop, which would produce incorrect code.
    std::string inst_name = inst->name.empty() ? "(anonymous)" : inst->name;
    lowering_errors_.push_back(
        "unsupported IR instruction '" + inst_name + "' (kind: " +
        std::to_string(static_cast<int>(inst->type.kind)) + "); emitting unreachable");
    body.push_back(kOpUnreachable);
}

// ============================================================================
// Function Lowering
// ============================================================================

void WasmTarget::LowerFunction(const ir::Function &fn,
                                std::vector<std::uint8_t> &body) {
    // Build the locals declaration: count params as local.get indices
    // then declare additional locals for named SSA values.
    std::vector<WasmValType> local_types;

    // Collect additional locals from instructions that define values
    for (auto &blk : fn.blocks) {
        for (auto &inst : blk->instructions) {
            if (inst->HasResult()) {
                local_types.push_back(IRTypeToWasm(inst->type));
            }
        }
        for (auto &phi : blk->phis) {
            if (phi->HasResult()) {
                local_types.push_back(IRTypeToWasm(phi->type));
            }
        }
    }

    // Compress locals: consecutive runs of the same type
    std::vector<std::pair<std::uint32_t, WasmValType>> compressed;
    for (auto vt : local_types) {
        if (!compressed.empty() && compressed.back().second == vt) {
            compressed.back().first++;
        } else {
            compressed.push_back({1, vt});
        }
    }

    // Local declarations
    EmitU32Leb128(body, static_cast<std::uint32_t>(compressed.size()));
    for (auto &[count, vt] : compressed) {
        EmitU32Leb128(body, count);
        body.push_back(static_cast<std::uint8_t>(vt));
    }

    // Build block depth map: each IR basic block gets a WASM block wrapper
    // so that branch targets can be resolved to relative depths.
    block_depth_map_.clear();
    current_block_depth_ = 0;
    for (size_t i = 0; i < fn.blocks.size(); ++i) {
        block_depth_map_[fn.blocks[i]->name] = static_cast<std::uint32_t>(i);
    }

    // Emit instruction bytecode.  Each block is wrapped in a WASM block
    // so that br/br_if can target them by relative depth.
    for (size_t bi = 0; bi < fn.blocks.size(); ++bi) {
        body.push_back(kOpBlock);
        body.push_back(kBlockTypeVoid);
        current_block_depth_ = static_cast<std::uint32_t>(bi + 1);
    }
    for (size_t bi = 0; bi < fn.blocks.size(); ++bi) {
        auto &blk = fn.blocks[bi];
        for (auto &inst : blk->instructions) {
            LowerInstruction(inst, body);
        }
        if (blk->terminator) {
            LowerInstruction(blk->terminator, body);
        }
        body.push_back(kOpEnd);  // close this block
    }

    // End marker for function body
    body.push_back(kOpEnd);
}

// ============================================================================
// Public API: EmitWasmBinary
// ============================================================================

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

    if (!module_) return {};

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
            if (has_shadow_stack_) break;
        }
        if (has_shadow_stack_) break;
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

// ============================================================================
// Public API: EmitAssembly (WAT text format)
// ============================================================================

std::string WasmTarget::EmitAssembly() {
    if (!module_) return "(module)\n";

    std::ostringstream os;
    os << "(module\n";

    // Emit memory
    os << "  (memory (export \"memory\") 1)\n";

    // Emit functions in WAT text format
    for (auto &fn : module_->Functions()) {
        os << "  (func $" << fn->name;

        // Export
        os << " (export \"" << fn->name << "\")";

        // Parameters
        for (size_t i = 0; i < fn->param_types.size(); ++i) {
            std::string pname = i < fn->params.size() ? fn->params[i] : ("p" + std::to_string(i));
            os << " (param $" << pname << " ";
            switch (IRTypeToWasm(fn->param_types[i])) {
                case WasmValType::kI32: os << "i32"; break;
                case WasmValType::kI64: os << "i64"; break;
                case WasmValType::kF32: os << "f32"; break;
                case WasmValType::kF64: os << "f64"; break;
                default: os << "i64"; break;
            }
            os << ")";
        }

        // Return type
        if (fn->ret_type.kind != ir::IRTypeKind::kVoid) {
            os << " (result ";
            switch (IRTypeToWasm(fn->ret_type)) {
                case WasmValType::kI32: os << "i32"; break;
                case WasmValType::kI64: os << "i64"; break;
                case WasmValType::kF32: os << "f32"; break;
                case WasmValType::kF64: os << "f64"; break;
                default: os << "i64"; break;
            }
            os << ")";
        }

        os << "\n";

        // Emit instruction comments for each block
        for (auto &blk : fn->blocks) {
            os << "    ;; block " << blk->name << "\n";
            for (auto &inst : blk->instructions) {
                EmitInstructionWAT(os, inst);
            }
            if (blk->terminator) {
                EmitInstructionWAT(os, blk->terminator);
            }
        }

        os << "  )\n";
    }

    os << ")\n";
    return os.str();
}

// ============================================================================
// WAT Text Emitter (for EmitAssembly)
// ============================================================================

namespace {

void EmitInstructionWATImpl(std::ostream &os, const std::shared_ptr<ir::Instruction> &inst) {
    if (auto *bin = dynamic_cast<ir::BinaryInstruction *>(inst.get())) {
        using Op = ir::BinaryInstruction::Op;
        bool is_i64 = (bin->type.kind == ir::IRTypeKind::kI64);
        bool is_float = bin->type.IsFloat();
        const char *prefix = is_float ? "f64" : (is_i64 ? "i64" : "i32");
        switch (bin->op) {
            case Op::kAdd: case Op::kFAdd:
                os << "    " << prefix << ".add\n"; break;
            case Op::kSub: case Op::kFSub:
                os << "    " << prefix << ".sub\n"; break;
            case Op::kMul: case Op::kFMul:
                os << "    " << prefix << ".mul\n"; break;
            case Op::kDiv: case Op::kSDiv:
                os << "    " << prefix << ".div_s\n"; break;
            case Op::kUDiv:
                os << "    " << prefix << ".div_u\n"; break;
            case Op::kFDiv:
                os << "    f64.div\n"; break;
            case Op::kRem: case Op::kSRem:
                os << "    " << prefix << ".rem_s\n"; break;
            case Op::kURem:
                os << "    " << prefix << ".rem_u\n"; break;
            case Op::kFRem:
                os << "    f64.div  ;; no direct frem\n"; break;
            case Op::kAnd:
                os << "    " << prefix << ".and\n"; break;
            case Op::kOr:
                os << "    " << prefix << ".or\n"; break;
            case Op::kXor:
                os << "    " << prefix << ".xor\n"; break;
            case Op::kShl:
                os << "    " << prefix << ".shl\n"; break;
            case Op::kLShr:
                os << "    " << prefix << ".shr_u\n"; break;
            case Op::kAShr:
                os << "    " << prefix << ".shr_s\n"; break;
            case Op::kCmpEq: case Op::kCmpFoe:
                os << "    " << prefix << ".eq\n"; break;
            case Op::kCmpNe: case Op::kCmpFne:
                os << "    " << prefix << ".ne\n"; break;
            case Op::kCmpLt: case Op::kCmpSlt: case Op::kCmpFlt:
                os << "    " << prefix << ".lt_s\n"; break;
            case Op::kCmpSle: case Op::kCmpFle:
                os << "    " << prefix << ".le_s\n"; break;
            case Op::kCmpSgt: case Op::kCmpFgt:
                os << "    " << prefix << ".gt_s\n"; break;
            case Op::kCmpSge: case Op::kCmpFge:
                os << "    " << prefix << ".ge_s\n"; break;
            case Op::kCmpUlt:
                os << "    " << prefix << ".lt_u\n"; break;
            case Op::kCmpUle:
                os << "    " << prefix << ".le_u\n"; break;
            case Op::kCmpUgt:
                os << "    " << prefix << ".gt_u\n"; break;
            case Op::kCmpUge:
                os << "    " << prefix << ".ge_u\n"; break;
        }
        return;
    }

    if (dynamic_cast<ir::ReturnStatement *>(inst.get())) {
        os << "    return\n";
        return;
    }

    if (auto *call = dynamic_cast<ir::CallInstruction *>(inst.get())) {
        os << "    call $" << call->callee << "\n";
        return;
    }

    if (dynamic_cast<ir::UnreachableStatement *>(inst.get())) {
        os << "    unreachable\n";
        return;
    }

    if (dynamic_cast<ir::BranchStatement *>(inst.get())) {
        os << "    br 0\n";
        return;
    }

    if (dynamic_cast<ir::CondBranchStatement *>(inst.get())) {
        os << "    br_if 0\n";
        return;
    }

    os << "    nop\n";
}

}  // namespace

// Delegate to free function in anonymous namespace to avoid extra header decl
void WasmTarget::EmitInstructionWAT(std::ostream &os,
                                     const std::shared_ptr<ir::Instruction> &inst) {
    EmitInstructionWATImpl(os, inst);
}

}  // namespace polyglot::backends::wasm
