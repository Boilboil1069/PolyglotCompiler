# WASM Backend Translation-Unit Layout

The WebAssembly backend was originally implemented as a single 1047-line
`wasm_target.cpp` covering eight unrelated concerns (binary-format
constants, LEB128 encoding, IR-type mapping, seven section emitters, the
big `LowerInstruction` switch, `LowerFunction`, the public
`EmitWasmBinary` driver, and the WAT text printer).  This layout made
every downstream change touch a 1k-line file and inflated the blast
radius of every fix.

The backend is now split into one private constants header, a thin
public-entry-point TU, and six focused implementation TUs.  All public
method signatures, byte-level binary output, and diagnostic ordering are
preserved — guarded by the existing eight `wasm_target_test` cases plus
four new `wasm_split_smoke_test` cases.

## 1. File layout

```
backends/wasm/
    include/
        wasm_target.h              public class declaration (unchanged)
        internal/
            wasm_constants.h       backend-private opcode + magic constants
    src/
        wasm_target.cpp            EmitWasmBinary public entry point
        wasm_target_backend.cpp    ITargetBackend adapter (untouched)
        encoding/
            leb128.cpp             EmitU32Leb128 / EmitI32Leb128 /
                                   EmitI64Leb128 / EmitString / EmitSection
        lowering/
            type_mapping.cpp       IRTypeToWasm
            function_lowerer.cpp   LowerFunction (locals + block-depth)
            instruction_lowerer.cpp LowerInstruction (11 IR shapes)
        sections/
            section_emitters.cpp   7 EmitXxxSection methods
        wat_printer.cpp            EmitAssembly + EmitInstructionWAT
```

## 2. Constants header — `internal::` visibility

`backends/wasm/include/internal/wasm_constants.h` is the only header
under an `internal` directory in the backend tree.  It is consumed
exclusively by translation units in `backends/wasm/src/**`; no
`include/` tree above the `internal/` boundary references it.  All
constants are declared `inline constexpr` (C++17), so multiple TUs can
include the header without ODR violations.

The header collects:

| Group | Symbols |
|-------|---------|
| Magic / version | `kWasmMagic`, `kWasmVersion` |
| Control flow   | `kOpUnreachable`, `kOpNop`, `kOpBlock`, `kOpLoop`, `kOpIf`, `kOpElse`, `kOpEnd`, `kOpBr`, `kOpBrIf`, `kOpReturn`, `kOpCall`, `kOpDrop`, `kOpSelect` |
| Locals/globals | `kOpLocalGet`, `kOpLocalSet`, `kOpLocalTee`, `kOpGlobalGet`, `kOpGlobalSet` |
| Memory         | `kOpI32Load…F64Store` (8 entries) |
| Numeric        | `kOpI32Const…F64Const`, full `i32/i64/f64` arithmetic, comparison, and conversion opcodes |
| Block type     | `kBlockTypeVoid` |

## 3. Encoding TU — `encoding/leb128.cpp`

Hosts the five LEB128 / framing helpers.  All five remain private static
methods of `WasmTarget`; only the definitions moved.  Because LEB128 is
a leaf concern with no IR dependencies, this TU includes only `<cstdint>`
/ `<string>` / `<vector>` plus `wasm_target.h` — the smallest TU in the
backend.

## 4. Type-mapping TU — `lowering/type_mapping.cpp`

`IRTypeToWasm` is the only IR-aware leaf function: it maps
`ir::IRTypeKind` to `WasmValType`.  Pointer-like kinds are folded to
`kI32` (wasm32 pointer width).

## 5. Section-emitter TU — `sections/section_emitters.cpp`

Hosts the seven section emitters:

| Section | Method | Notes |
|---------|--------|-------|
| Type     | `EmitTypeSection`     | Skipped if `types_` empty. |
| Import   | `EmitImportSection`   | Skipped if `imports_` empty. |
| Function | `EmitFunctionSection` | Skipped if `func_type_indices_` empty. |
| Memory   | `EmitMemorySection`   | Always emits a single 1-page memory. |
| Global   | `EmitGlobalSection`   | Emits the shadow-stack `__stack_pointer` global initialised to 65536 (top of first page). |
| Export   | `EmitExportSection`   | Skipped if `exports_` empty. |
| Code     | `EmitCodeSection`     | Skipped if `func_bodies_` empty. |

Only this TU and `wasm_target.cpp` need the `internal::` constants; both
import them with explicit `using` declarations.

## 6. Instruction-lowering TU — `lowering/instruction_lowerer.cpp`

`LowerInstruction` is the largest single method (~325 lines) and now
lives in its own TU.  It dispatches on 11 IR instruction shapes:

1. `BinaryInstruction` — 26 binary / compare opcodes including the
   `kFRem` lowering `x − trunc(x / y) · y`.
2. `ReturnStatement`.
3. `CallInstruction` — resolves callee via `func_name_to_index_`;
   unresolved callees record a hard error and emit `unreachable`.
4. `LoadInstruction` — width / alignment derived from `load->type`.
5. `StoreInstruction` — width / alignment derived from `store->type`.
6. `CastInstruction` — `kZExt/kIntToPtr → i64.extend_i32_u`,
   `kSExt → i64.extend_i32_s`, `kTrunc/kPtrToInt → i32.wrap_i64`,
   `kBitcast/kFpExt/kFpTrunc → nop`.
7. `AllocaInstruction` — shadow-stack lowering against
   `__stack_pointer` (global 0); leaves the new pointer on the operand
   stack.
8. `UnreachableStatement`.
9. `BranchStatement` — `br` with relative depth from
   `block_depth_map_` (set up by `LowerFunction`).
10. `CondBranchStatement` — `br_if` with the same depth resolution.
11. Fallback — emits `unreachable` and records an
   `unsupported IR instruction` diagnostic in `lowering_errors_`.

## 7. Function-lowering TU — `lowering/function_lowerer.cpp`

`LowerFunction`:

1. Collects WASM local types (one per IR instruction / phi that has a
   result).
2. RLE-compresses consecutive runs of identical types into the locals
   header.
3. Builds `block_depth_map_` from IR basic-block names → integer index.
4. Emits a `block (block_type_void)` opener for each IR basic block,
   then the lowered instructions, then a matching `end` per block plus
   the function-body `end`.

## 8. WAT-printer TU — `wat_printer.cpp`

`EmitAssembly` produces a `(module ... (func $name (export …) (param …)
(result …) … ))` text representation.  The instruction-printer helper
`EmitInstructionWATImpl` lives in this TU's anonymous namespace; the
class member `EmitInstructionWAT` is a thin trampoline so the helper has
no public exposure.

## 9. Public-entry TU — `wasm_target.cpp`

The kept `wasm_target.cpp` is now a 143-line file containing only
`EmitWasmBinary`, the public binary-emit driver.  Three phases:

1. **Type collection** — walks `module_->Functions()`, dedupes
   signatures into `types_`, populates `func_type_indices_` and the
   per-function export entry, and builds `func_name_to_index_`.
2. **Function-body lowering** — calls `LowerFunction` for every IR
   function, accumulating bytecode into `func_bodies_`.
3. **Section assembly** — concatenates the magic + version header and
   the seven sections in canonical order, conditionally including the
   global section when any function used `alloca`.

Diagnostics accumulated in `lowering_errors_` are flushed to `stderr`
between phase 2 and phase 3 — order preserved exactly as before the
split.

## 10. CMake wiring

`backends/CMakeLists.txt` lists all eight WASM TUs in `backend_wasm`:

```cmake
add_library(backend_wasm
    wasm/src/wasm_target.cpp
    wasm/src/wasm_target_backend.cpp
    wasm/src/encoding/leb128.cpp
    wasm/src/lowering/type_mapping.cpp
    wasm/src/lowering/function_lowerer.cpp
    wasm/src/lowering/instruction_lowerer.cpp
    wasm/src/sections/section_emitters.cpp
    wasm/src/wat_printer.cpp
)
```

`target_include_directories(backend_wasm PUBLIC ${CMAKE_SOURCE_DIR})`
makes the `internal/` header reachable through its source-relative
include path.

The WASM adapter (`wasm_target_backend.cpp`) inherits the default
`ITargetBackend::EmitBitcode`, which routes through the polyglot bitcode
serialiser; no WASM-specific override is required.

## 11. Byte-equivalence guarantee

The split is purely structural.  Two test groups guard the contract:

- `tests/unit/backends/wasm_target_test.cpp` — eight pre-existing
  end-to-end cases covering `EmitAssembly` text shape, `EmitWasmBinary`
  magic bytes, parameter type mapping, void returns, control-flow block
  annotations, empty modules, and multi-export ordering.
- `tests/unit/backends/wasm_split_smoke_test.cpp` — four new cases
  asserting the empty-module 8-byte header, the canonical section-id
  sequence `(1, 3, 5, 7, 10)` for a single `add(i32,i32) → i32`, the
  `(module ... (func $add ...))` WAT shape, and indirect LEB128
  exercise via type-table dedup (two identical-signature functions must
  yield function-section bytes `0x02 0x00 0x00`).

## 12. Adding a new lowering

Choose the TU by responsibility:

| Change | TU |
|--------|-----|
| New IR opcode → wasm bytecode | `lowering/instruction_lowerer.cpp` |
| New section in the binary | `sections/section_emitters.cpp` (+ wire into `wasm_target.cpp` phase 3) |
| New IR type → wasm type | `lowering/type_mapping.cpp` |
| New WAT line | `wat_printer.cpp` |
| New opcode constant | `internal/wasm_constants.h` |

Public-method signatures must stay in sync with `wasm_target.h`; if a
signature change is required, update the header first.

## 13. Troubleshooting

- **Unresolved external symbol `WasmTarget::Foo`** — the method is
  declared in the header but its TU is missing from the
  `add_library(backend_wasm …)` list.  Add the new TU to
  `backends/CMakeLists.txt`.
- **Multiple definitions of `kOpXxx`** — happens if a TU re-defines a
  constant that the `internal/wasm_constants.h` header already exports.
  The header owns every constant; remove the local copy.
- **Binary differs from the previous release for the same IR** — diff
  the offending section in the byte stream against the legacy build,
  then bisect by reverting the smallest TU at a time.  The split is
  byte-equivalent by construction, so any drift indicates an unrelated
  regression introduced after the split.
