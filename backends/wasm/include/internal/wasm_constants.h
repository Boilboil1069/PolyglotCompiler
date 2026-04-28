/**
 * @file     wasm_constants.h
 * @brief    WebAssembly binary format constants (private header)
 *
 * @ingroup  Backend / WASM
 * @author   Manning Cyrus
 * @date     2026-04-28
 *
 * @note  This is a backend-private header consumed only by translation units
 *        under `backends/wasm/src/**`.  It is intentionally not exposed via
 *        `backends/wasm/include/`.
 */
#pragma once

#include <cstdint>

namespace polyglot::backends::wasm::internal {

// ============================================================================
// Module Header
// ============================================================================

inline constexpr std::uint8_t kWasmMagic[]   = {0x00, 0x61, 0x73, 0x6D};
inline constexpr std::uint8_t kWasmVersion[] = {0x01, 0x00, 0x00, 0x00};

// ============================================================================
// Wasm Opcodes
// ============================================================================

inline constexpr std::uint8_t kOpUnreachable    = 0x00;
inline constexpr std::uint8_t kOpNop            = 0x01;
inline constexpr std::uint8_t kOpBlock          = 0x02;
inline constexpr std::uint8_t kOpLoop           = 0x03;
inline constexpr std::uint8_t kOpIf             = 0x04;
inline constexpr std::uint8_t kOpElse           = 0x05;
inline constexpr std::uint8_t kOpEnd            = 0x0B;
inline constexpr std::uint8_t kOpBr             = 0x0C;
inline constexpr std::uint8_t kOpBrIf           = 0x0D;
inline constexpr std::uint8_t kOpReturn         = 0x0F;
inline constexpr std::uint8_t kOpCall           = 0x10;
inline constexpr std::uint8_t kOpDrop           = 0x1A;
inline constexpr std::uint8_t kOpSelect         = 0x1B;
inline constexpr std::uint8_t kOpLocalGet       = 0x20;
inline constexpr std::uint8_t kOpLocalSet       = 0x21;
inline constexpr std::uint8_t kOpLocalTee       = 0x22;
inline constexpr std::uint8_t kOpGlobalGet      = 0x23;
inline constexpr std::uint8_t kOpGlobalSet      = 0x24;
inline constexpr std::uint8_t kOpI32Load        = 0x28;
inline constexpr std::uint8_t kOpI64Load        = 0x29;
inline constexpr std::uint8_t kOpF32Load        = 0x2A;
inline constexpr std::uint8_t kOpF64Load        = 0x2B;
inline constexpr std::uint8_t kOpI32Store       = 0x36;
inline constexpr std::uint8_t kOpI64Store       = 0x37;
inline constexpr std::uint8_t kOpF32Store       = 0x38;
inline constexpr std::uint8_t kOpF64Store       = 0x39;
inline constexpr std::uint8_t kOpI32Const       = 0x41;
inline constexpr std::uint8_t kOpI64Const       = 0x42;
inline constexpr std::uint8_t kOpF32Const       = 0x43;
inline constexpr std::uint8_t kOpF64Const       = 0x44;
inline constexpr std::uint8_t kOpI32Eqz         = 0x45;
inline constexpr std::uint8_t kOpI32Eq          = 0x46;
inline constexpr std::uint8_t kOpI32Ne          = 0x47;
inline constexpr std::uint8_t kOpI32LtS         = 0x48;
inline constexpr std::uint8_t kOpI32LtU         = 0x49;
inline constexpr std::uint8_t kOpI32GtS         = 0x4A;
inline constexpr std::uint8_t kOpI32GtU         = 0x4B;
inline constexpr std::uint8_t kOpI32LeS         = 0x4C;
inline constexpr std::uint8_t kOpI32LeU         = 0x4D;
inline constexpr std::uint8_t kOpI32GeS         = 0x4E;
inline constexpr std::uint8_t kOpI32GeU         = 0x4F;
inline constexpr std::uint8_t kOpI64Eqz         = 0x50;
inline constexpr std::uint8_t kOpI64Eq          = 0x51;
inline constexpr std::uint8_t kOpI64Ne          = 0x52;
inline constexpr std::uint8_t kOpI64LtS         = 0x53;
inline constexpr std::uint8_t kOpI64LtU         = 0x54;
inline constexpr std::uint8_t kOpI64GtS         = 0x55;
inline constexpr std::uint8_t kOpI64GtU         = 0x56;
inline constexpr std::uint8_t kOpI64LeS         = 0x57;
inline constexpr std::uint8_t kOpI64LeU         = 0x58;
inline constexpr std::uint8_t kOpI64GeS         = 0x59;
inline constexpr std::uint8_t kOpI64GeU         = 0x5A;
inline constexpr std::uint8_t kOpF64Eq          = 0x61;
inline constexpr std::uint8_t kOpF64Ne          = 0x62;
inline constexpr std::uint8_t kOpF64Lt          = 0x63;
inline constexpr std::uint8_t kOpF64Gt          = 0x64;
inline constexpr std::uint8_t kOpF64Le          = 0x65;
inline constexpr std::uint8_t kOpF64Ge          = 0x66;
inline constexpr std::uint8_t kOpI32Add         = 0x6A;
inline constexpr std::uint8_t kOpI32Sub         = 0x6B;
inline constexpr std::uint8_t kOpI32Mul         = 0x6C;
inline constexpr std::uint8_t kOpI32DivS        = 0x6D;
inline constexpr std::uint8_t kOpI32DivU        = 0x6E;
inline constexpr std::uint8_t kOpI32RemS        = 0x6F;
inline constexpr std::uint8_t kOpI32RemU        = 0x70;
inline constexpr std::uint8_t kOpI32And         = 0x71;
inline constexpr std::uint8_t kOpI32Or          = 0x72;
inline constexpr std::uint8_t kOpI32Xor         = 0x73;
inline constexpr std::uint8_t kOpI32Shl         = 0x74;
inline constexpr std::uint8_t kOpI32ShrS        = 0x75;
inline constexpr std::uint8_t kOpI32ShrU        = 0x76;
inline constexpr std::uint8_t kOpI64Add         = 0x7C;
inline constexpr std::uint8_t kOpI64Sub         = 0x7D;
inline constexpr std::uint8_t kOpI64Mul         = 0x7E;
inline constexpr std::uint8_t kOpI64DivS        = 0x7F;
inline constexpr std::uint8_t kOpI64DivU        = 0x80;
inline constexpr std::uint8_t kOpI64RemS        = 0x81;
inline constexpr std::uint8_t kOpI64RemU        = 0x82;
inline constexpr std::uint8_t kOpI64And         = 0x83;
inline constexpr std::uint8_t kOpI64Or          = 0x84;
inline constexpr std::uint8_t kOpI64Xor         = 0x85;
inline constexpr std::uint8_t kOpI64Shl         = 0x86;
inline constexpr std::uint8_t kOpI64ShrS        = 0x87;
inline constexpr std::uint8_t kOpI64ShrU        = 0x88;
inline constexpr std::uint8_t kOpF64Abs         = 0x99;
inline constexpr std::uint8_t kOpF64Neg         = 0x9A;
inline constexpr std::uint8_t kOpF64Ceil        = 0x9B;
inline constexpr std::uint8_t kOpF64Floor       = 0x9C;
inline constexpr std::uint8_t kOpF64Trunc       = 0x9D;
inline constexpr std::uint8_t kOpF64Nearest     = 0x9E;
inline constexpr std::uint8_t kOpF64Add         = 0xA0;
inline constexpr std::uint8_t kOpF64Sub         = 0xA1;
inline constexpr std::uint8_t kOpF64Mul         = 0xA2;
inline constexpr std::uint8_t kOpF64Div         = 0xA3;
inline constexpr std::uint8_t kOpF64Copysign    = 0xA6;
inline constexpr std::uint8_t kOpI32WrapI64     = 0xA7;
inline constexpr std::uint8_t kOpI64ExtendI32S  = 0xAC;
inline constexpr std::uint8_t kOpI64ExtendI32U  = 0xAD;

// ============================================================================
// Block Type
// ============================================================================

inline constexpr std::uint8_t kBlockTypeVoid = 0x40;

}  // namespace polyglot::backends::wasm::internal
