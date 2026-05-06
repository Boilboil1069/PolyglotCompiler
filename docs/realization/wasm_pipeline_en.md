# Wasm Linker Pipeline (BIN-6)

This note describes how `polyld` treats `.wasm` modules as first-class
inputs and how the multi-module merger satisfies imports across them.
The implementation lives in [tools/polyld/include/linker_wasm.h](../../tools/polyld/include/linker_wasm.h)
and [tools/polyld/src/linker_wasm.cpp](../../tools/polyld/src/linker_wasm.cpp).

## Overview

The dispatch chain in
[tools/polyld/src/linker.cpp](../../tools/polyld/src/linker.cpp)
resolves the requested target via `TargetTriple` and, when the OS
field is `wasi` or the binary container is `kWasm`, hands control to
`Linker::GenerateWasmModule()` defined in `linker_wasm.cpp`.  That
member walks each input file, parses any byte stream that begins with
the `\0asm\1\0\0\0` preamble, and chooses one of three code paths:

| Inputs | Path |
|---|---|
| 0 wasm files | Synthesise a minimal valid module containing the merged `.text` payload as a custom section (`polyglot.text`). |
| 1 wasm file  | Round-trip parse → emit so LEB128 widths are normalised. |
| N wasm files | Run `LinkWasmModules` to produce one merged module. |

When the resolved target OS is `wasi` the pipeline re-parses the
emitted image and runs `ValidateWasiEntry`, which fails with
`polyld-err-E3320` if no `_start` function is exported.

## Section roster

The parser and emitter cover the full WebAssembly 1.0 section table
(`type` 0x01 through `data` 0x0B) plus the `datacount` (0x0C) and
`custom` (0x00) sections.  The emitter writes them in canonical order
and skips empty sections so the resulting file has no zero-length
section headers.

## Multi-module index spaces

Each WebAssembly module owns six independent index spaces (functions,
tables, memories, globals, data segments, element segments).  Within
each space the imported entries occupy the lower indices and local
definitions follow.  The merger maintains, for every source module, a
per-kind table that maps a *source-local* index to its index in the
merged module.  Two structural facts drive the layout:

1. **Surviving imports go first.**  The merger walks every input,
   tries to satisfy each import against the global export table, and
   appends the unresolved imports to a single merged import vector.
   Indices in this vector are assigned in input-then-declaration order
   so the output is deterministic.
2. **Local definitions come next.**  Each source module's local
   functions / tables / memories / globals are concatenated in input
   order and assigned merged indices that start at the kept-import
   count for that kind.

For data and element segments only the local count matters because
those spaces have no imports.

## Import resolution

`LinkWasmModules` builds a single `name → (source, kind, local_index)`
map across every input, then walks each import in declaration order:

* If a sibling module exports a matching name with the same
  `ImportKind`, the import is *resolved*.  For function imports the
  declared signature is also matched against the exporter's
  `FuncType` (after type interning).  A signature mismatch surfaces
  `polyld-err-E3330`.
* If the kind matches but the resolver's export is not actually a
  function (cross-kind shadowing), the merger reports
  `polyld-err-E3310`.
* Otherwise the import survives into the merged module verbatim.

For each resolved import, every reference in code bodies, exports and
element segments is rewritten through the merged index map so the
caller never observes the original import slot.

## Code-body re-write

Function bodies retain their raw byte representation (the parser does
not decode them in single-module mode, so SIMD modules round-trip
losslessly).  When a body needs index re-mapping the merger runs a
full WebAssembly 1.0 + bulk-memory + reference-types instruction
walker:

| Opcode(s) | Re-mapped immediate(s) |
|---|---|
| `0x10` `call`                        | funcidx |
| `0x11` `call_indirect`               | typeidx, tableidx |
| `0x23` / `0x24` `global.get/set`     | globalidx |
| `0x25` / `0x26` `table.get/set`      | tableidx |
| `0xD2` `ref.func`                    | funcidx |
| `0xFC 8` `memory.init`               | dataidx, memidx |
| `0xFC 9` `data.drop`                 | dataidx |
| `0xFC 10` `memory.copy`              | memidx, memidx |
| `0xFC 11` `memory.fill`              | memidx |
| `0xFC 12` `table.init`               | elemidx, tableidx |
| `0xFC 13` `elem.drop`                | elemidx |
| `0xFC 14` `table.copy`               | tableidx, tableidx |
| `0xFC 15..17` `table.grow/size/fill` | tableidx |
| Block-type slot in `block`/`loop`/`if` | typeidx (when present) |

The walker leaves local indices, label indices, memargs and constant
literals untouched.  Encountering a `0xFD` (SIMD) or `0xFE` (atomic)
prefix during a multi-module merge fails with `polyld-err-E3340`.

## Error codes

| Code              | Meaning |
|-------------------|---------|
| `polyld-err-E3300` | Parse failure (bad preamble, truncated section, unknown section id). |
| `polyld-err-E3310` | Unresolved or cross-kind import. |
| `polyld-err-E3320` | Missing `_start` for a `wasi` target. |
| `polyld-err-E3330` | Function-import signature mismatch. |
| `polyld-err-E3340` | Code-body rewrite failure (out-of-range index, unsupported opcode). |

## Debug cheat sheet

* `wasm-objdump -h <file>` — print the section roster.
* `wasm-validate <file>` — run the spec validator.
* `wasmtime run --invoke _start <file>` — call the WASI entry point.
