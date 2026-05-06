/**
 * @file     linker_wasm.h
 * @brief    Wasm linker support: parse / merge / emit `.wasm` modules
 *           in the polyld dispatch chain.  Mirrors the layout of
 *           `linker_pe.h` and `linker_macho.h`.
 *
 * The functions in this header are pure on the data types defined
 * below — they have no dependency on the rest of the `Linker` class so
 * the parser, the merger and the emitter can be unit-tested in
 * isolation from any `Linker` instance.
 *
 * @ingroup  Tool / polyld
 * @author   Manning Cyrus
 * @date     2026-05-06
 */

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace polyglot::linker::wasm {

// ---------------------------------------------------------------------------
// Wire constants
// ---------------------------------------------------------------------------

constexpr std::array<std::uint8_t, 4> kMagic   = {0x00, 0x61, 0x73, 0x6D};
constexpr std::array<std::uint8_t, 4> kVersion = {0x01, 0x00, 0x00, 0x00};

enum class SectionId : std::uint8_t {
  kCustom   = 0x00,
  kType     = 0x01,
  kImport   = 0x02,
  kFunction = 0x03,
  kTable    = 0x04,
  kMemory   = 0x05,
  kGlobal   = 0x06,
  kExport   = 0x07,
  kStart    = 0x08,
  kElement  = 0x09,
  kCode     = 0x0A,
  kData     = 0x0B,
  kDataCount = 0x0C,
};

enum class ValType : std::uint8_t {
  kI32     = 0x7F,
  kI64     = 0x7E,
  kF32     = 0x7D,
  kF64     = 0x7C,
  kV128    = 0x7B,
  kFuncRef = 0x70,
  kExternRef = 0x6F,
};

enum class ImportKind : std::uint8_t {
  kFunc   = 0x00,
  kTable  = 0x01,
  kMemory = 0x02,
  kGlobal = 0x03,
};

using ExportKind = ImportKind;

// ---------------------------------------------------------------------------
// In-memory module representation
// ---------------------------------------------------------------------------

struct FuncType {
  std::vector<ValType> params;
  std::vector<ValType> results;
  bool operator==(const FuncType &o) const {
    return params == o.params && results == o.results;
  }
};

struct Limits {
  std::uint32_t min{0};
  std::uint32_t max{0};
  bool          has_max{false};
  bool          shared{false};
};

struct TableType {
  ValType elem{ValType::kFuncRef};
  Limits  limits;
};

struct MemoryType {
  Limits limits;
};

struct GlobalType {
  ValType valtype{ValType::kI32};
  bool    mutability{false};
};

struct Import {
  std::string  module_name;
  std::string  name;
  ImportKind   kind{ImportKind::kFunc};
  std::uint32_t type_index{0};   ///< For kFunc only.
  TableType     table_type{};    ///< For kTable.
  MemoryType    memory_type{};   ///< For kMemory.
  GlobalType    global_type{};   ///< For kGlobal.
};

struct Export {
  std::string  name;
  ExportKind   kind{ExportKind::kFunc};
  std::uint32_t index{0};
};

/// One function body as raw bytes plus its type index.  The body bytes
/// retain the leading `local_count + locals[]` declaration so the
/// emitter can write them back verbatim once index re-mapping is done.
struct Function {
  std::uint32_t            type_index{0};
  std::vector<std::uint8_t> body;
};

struct Global {
  GlobalType                type;
  std::vector<std::uint8_t> init_expr; ///< Including trailing 0x0B.
};

/// Active or passive element segment.  We keep raw `init` byte spans
/// because re-encoding `ref.func`/`ref.null` payloads after re-indexing
/// is the merger's responsibility (handled in the code-section pass).
struct ElementSegment {
  std::uint32_t              flags{0};
  std::uint32_t              table_index{0};
  std::vector<std::uint8_t>  offset_expr;
  ValType                    elem_type{ValType::kFuncRef};
  std::vector<std::uint32_t> func_indices;   ///< Decoded for re-mapping.
  std::vector<std::vector<std::uint8_t>> init_exprs; ///< For "expression" form.
  bool                       uses_expressions{false};
};

struct DataSegment {
  std::uint32_t              flags{0};
  std::uint32_t              memory_index{0};
  std::vector<std::uint8_t>  offset_expr;
  std::vector<std::uint8_t>  bytes;
};

struct CustomSection {
  std::string                name;
  std::vector<std::uint8_t>  payload; ///< Excludes the leading name string.
};

struct Module {
  std::vector<FuncType>       types;
  std::vector<Import>         imports;
  std::vector<Function>       functions;     ///< Excludes imported funcs.
  std::vector<TableType>      tables;        ///< Excludes imported tables.
  std::vector<MemoryType>     memories;      ///< Excludes imported memories.
  std::vector<Global>         globals;       ///< Excludes imported globals.
  std::vector<Export>         exports;
  std::vector<ElementSegment> elements;
  std::vector<DataSegment>    data_segments;
  std::vector<CustomSection>  custom_sections;
  bool                        has_start{false};
  std::uint32_t               start_func{0};
  bool                        has_data_count{false};
  std::uint32_t               data_count{0};
};

// ---------------------------------------------------------------------------
// Parser / emitter / linker
// ---------------------------------------------------------------------------

/// Parse a `.wasm` module from `bytes`.  Returns `true` on success;
/// otherwise `error_out` holds a `polyld-err-E33xx` tagged diagnostic
/// and `out` is left in an indeterminate state.
bool ParseWasmModule(const std::vector<std::uint8_t> &bytes,
                     Module &out, std::string &error_out);

/// Re-emit a `Module` as a fresh `.wasm` byte stream.  Bytewise
/// stable: `EmitWasmModule(parse(bytes))` round-trips for any module
/// the parser accepts, modulo equivalent LEB128 encodings.
std::vector<std::uint8_t> EmitWasmModule(const Module &m);

/// Merge two or more parsed modules following wasm-ld conventions:
/// imports are resolved against exports of sibling modules of matching
/// signature; local function indices are re-mapped through the merged
/// module's index space; `_start` from the *first* input wins.
///
/// On unresolved imports the function returns `false` with a
/// `polyld-err-E3310` diagnostic in `error_out`; on type mismatches
/// the diagnostic is `polyld-err-E3330`.
bool LinkWasmModules(const std::vector<Module> &inputs,
                     Module &out, std::string &error_out);

/// Validate that a fully-linked module exports a `_start` function
/// when the target OS is `wasi`.  Returns `false` and writes
/// `polyld-err-E3320` to `error_out` when missing.
bool ValidateWasiEntry(const Module &m, std::string &error_out);

// ---------------------------------------------------------------------------
// LEB128 helpers (also reused by the writer; exposed for unit tests)
// ---------------------------------------------------------------------------

/// Decode an unsigned LEB128 starting at `p` (must point inside
/// `[begin, end)`).  Advances `p` past the consumed bytes.  Returns
/// `false` on overrun or an over-long encoding (>5 bytes for u32).
bool DecodeULeb32(const std::uint8_t *&p, const std::uint8_t *end,
                  std::uint32_t &value);

/// Encode `value` as an unsigned LEB128 padded to exactly 5 bytes.
/// Used when we patch indices in already-laid-out function bodies and
/// need a fixed-width slot so surrounding offsets stay valid.
void EncodeULeb32Padded5(std::vector<std::uint8_t> &out, std::uint32_t value);

/// Encode `value` as a minimum-length unsigned LEB128.
void EncodeULeb32(std::vector<std::uint8_t> &out, std::uint32_t value);

/// Encode a signed LEB128 (used only in tests; the merger doesn't need
/// to write signed LEBs since constant immediates are not re-mapped).
void EncodeSLeb32(std::vector<std::uint8_t> &out, std::int32_t value);

}  // namespace polyglot::linker::wasm
