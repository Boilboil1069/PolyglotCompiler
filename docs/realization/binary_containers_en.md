# Binary Containers & Target Triples

## Overview

Polyglot's linker, code generators and CLI now agree on a single
description of *what* binary they are producing.  Two value types
mediate that agreement, both living in `common/`:

| Type                                  | Header                              | Purpose                                                  |
| ------------------------------------- | ----------------------------------- | -------------------------------------------------------- |
| `polyglot::common::TargetTriple`      | `common/include/target_triple.h`    | LLVM-style `arch-vendor-os-env[-sub]` descriptor.        |
| `polyglot::common::BinaryContainer`   | `common/include/binary_container.h` | Container family (`kAuto / kELF / kPE / kMachO / kWasm`).|

`TargetTriple` is parsed once and stored on the `LinkerConfig`.
`BinaryContainer` is normally `kAuto`; the linker calls
`ResolveContainer(triple, requested)` to materialise the effective
container at link time.

## Triple parser

`ParseTargetTriple(spec)` returns a `TripleParseResult` value (no
exceptions, no aborts).  The parser accepts the canonical LLVM
spelling plus common aliases:

| Input                              | Arch        | Vendor   | OS       | Env      |
| ---------------------------------- | ----------- | -------- | -------- | -------- |
| `x86_64-pc-windows-msvc`           | `kX86_64`   | `kPc`    | `kWindows` | `kMsvc`  |
| `x86_64-pc-windows-gnu`            | `kX86_64`   | `kPc`    | `kWindows` | `kGnu`   |
| `aarch64-pc-windows-msvc`          | `kAArch64`  | `kPc`    | `kWindows` | `kMsvc`  |
| `i386-pc-windows-msvc`             | `kX86`      | `kPc`    | `kWindows` | `kMsvc`  |
| `x86_64-apple-darwin`              | `kX86_64`   | `kApple` | `kDarwin`  | —        |
| `aarch64-apple-darwin`             | `kAArch64`  | `kApple` | `kDarwin`  | —        |
| `arm64-apple-macos`                | `kAArch64`  | `kApple` | `kDarwin`  | —        |
| `x86_64-unknown-linux-gnu`         | `kX86_64`   | `kUnknown` | `kLinux` | `kGnu`   |
| `x86_64-unknown-linux-musl`        | `kX86_64`   | `kUnknown` | `kLinux` | `kMusl`  |
| `aarch64-unknown-linux-gnu`        | `kAArch64`  | `kUnknown` | `kLinux` | `kGnu`   |
| `aarch64-linux-android`            | `kAArch64`  | `kUnknown` | `kLinux` | `kAndroid` |
| `armv7-unknown-linux-gnueabihf`    | `kArm`      | `kUnknown` | `kLinux` | `kGnu`   |
| `riscv64-unknown-linux-gnu`        | `kRiscv64`  | `kUnknown` | `kLinux` | `kGnu`   |
| `riscv32-unknown-none-elf`         | `kRiscv32`  | `kUnknown` | `kNone`  | `kEabi`  |
| `wasm32-wasi`                      | `kWasm32`   | `kUnknown` | `kWasi`  | —        |
| `wasm32-unknown-unknown`           | `kWasm32`   | `kUnknown` | `kNone`  | —        |
| `x86_64-unknown-freebsd`           | `kX86_64`   | `kUnknown` | `kFreeBSD` | —        |

Aliases collapsed during parsing:

* arch: `amd64`, `x64` → `x86_64`; `arm64` → `aarch64`; `armv7a` →
  `armv7`; `i486/i586/i686/x86` → `i386`.
* OS: `macos`, `macosx`, `ios`, `tvos`, `watchos` → `darwin`;
  `mingw32`, `cygwin`, `win32` → `windows`.
* Env: `gnueabi`, `gnueabihf` → `gnu`; `musleabi`, `musleabihf` →
  `musl`; `elf`, `eabihf` → `eabi`.

`HostTriple()` synthesises the build host's triple from the
preprocessor macros (`_WIN32`, `__APPLE__`, `__linux__`,
`__aarch64__`, `_M_X64`, `__wasm__`, …).

## Container dispatch

| Triple OS         | `ContainerForOS`        |
| ----------------- | ----------------------- |
| `kWindows`        | `kPE`                   |
| `kDarwin`         | `kMachO`                |
| `kLinux / kFreeBSD` | `kELF`                |
| `kWasi`           | `kWasm`                 |
| `kNone / kUnknown` | `kELF` (default)       |

`ResolveContainer(triple, requested)`:

1. If `requested != kAuto`, the user wins — returned verbatim.
2. Else if `triple.arch ∈ {kWasm32, kWasm64}`, returns `kWasm`.
3. Else returns `ContainerForOS(triple.os)`.

## CLI integration

`polyc` and `polyld` accept the same two flags:

* `--target=<triple>` — overrides the default host triple.
* `--container=auto|elf|pe|macho|wasm` — overrides the resolved
  container.

The legacy `--target-os=linux|macos|windows` still works.  When set
without an explicit `--target`, it is folded into the canonical
triple by the compilation pipeline.

## Dispatch table (effective at link time)

`Linker::ResolveContainerAndTriple()` runs in the constructor and
pins `effective_container_`.  `Linker::GenerateOutput()` then maps
the pair `(OutputFormat, BinaryContainer)` to a concrete writer:

| OutputFormat        | kELF                        | kPE                  | kMachO                   | kWasm                |
| ------------------- | --------------------------- | -------------------- | ------------------------ | -------------------- |
| `kExecutable`       | `GenerateELFExecutable`     | `GeneratePEExecutable` | `GenerateMachOExecutable` | `GenerateWasmModule` |
| `kSharedLibrary`    | `GenerateELFSharedLibrary` (`ET_DYN`) | `GeneratePEDll`      | `GenerateMachODylib` (`MH_DYLIB`) | `GenerateWasmModule` |
| `kStaticLibrary`    | `GenerateStaticLibrary` (container-agnostic) | ↑ | ↑ | ↑ |
| `kRelocatable`      | `GenerateRelocatable` (container-agnostic) | ↑ | ↑ | ↑ |
| `kPEExecutable`     | always `GeneratePEExecutable` (legacy alias) | | | |

`OutputFormat::kExecutable` paired with `BinaryContainer::kAuto`
produces a structured error: callers must let
`ResolveContainerAndTriple()` settle the container, never reach
`GenerateOutput()` with `kAuto`.

### Suffix policy

`SuffixesFor(container)` returns the canonical executable / shared
/ static / object suffix for a container.  `polyc` and `polyld`
call `SuffixMatchesContainer(path, container, kind)` and emit
`polyc-warn-W2101` when a user-supplied `-o` path contradicts the
resolved container.  Unix executables accept any (or no) suffix
except that `.exe` and `.wasm` are reserved for their respective
containers.

## Migration guide

| Previous API                                  | New API                                           |
| --------------------------------------------- | ------------------------------------------------- |
| `LinkerConfig::target_arch`                   | `LinkerConfig::target_triple` (legacy field kept) |
| Hard-coded ELF in `GenerateOutput()`          | Dispatch via `effective_container_` (BIN-2)       |
| Backend `EmitObjectCode()`                    | Triple-aware overload to be added in BIN-2        |

## Known limits

* This change introduces only the *abstraction*; runtime dispatch in
  `GenerateOutput()` and the per-backend triple-aware codegen
  overloads land in BIN-2.
* `Vendor` is currently restricted to `pc`, `apple` and `unknown`;
  esoteric vendors (e.g. `nintendo`, `sony`) are not yet modelled.
* `SubArch` only distinguishes `v7` / `v8` / `v9` for ARM — finer
  CPU steppings (`armv8.2-a+sve2`) live elsewhere in the backend.

## PE path details (BIN-4)

The Windows PE path branches into two writers driven by `LinkerConfig::output_format`:

- `kPEExecutable` — `Linker::GeneratePEExecutable()` in `tools/polyld/src/linker.cpp` resolves the user entry (`_start` → `__ploy_main` → `main` priority order, overridden by `--entry`) and dispatches to `pe::BuildExeWithUserEntry` / `pe::BuildPrintlnSequencePE` / `pe::BuildExitZeroPE`.
- `kSharedLibrary` over a Windows triple — `Linker::GeneratePEDll()` in `tools/polyld/src/linker_pe.cpp` sets `IMAGE_FILE_DLL` (0x2000) via `BuildRequest::extra_file_characteristics`, embeds an `IMAGE_EXPORT_DIRECTORY` built by `pe::BuildExportSection`, and patches the Optional Header's Export Data Directory entry post-write.

### Export descriptors

Exports merge from three independent sources:

| Source | Polyld entry | Notes |
| --- | --- | --- |
| `--def <file>` | `pe::ParseDefFile` | Parses `LIBRARY` + `EXPORTS` sections; tolerates `IMPORTS`, `STUB`, etc. |
| `/EXPORT:<spec>` (cl-style) or `--export <spec>` (GNU-style) | `pe::ParseCliExportSpec` | Same right-hand-side grammar as one `EXPORTS` line. |
| `__declspec(dllexport)` | reserved (`pe::ExportSource::kDeclSpec`) | Wired by frontends in a future patch. |

`pe::MergeExports` deduplicates by public name and reports incompatible re-declarations as `polyld-err-E3201`. Identical re-declarations are silently coalesced; partial overlaps (e.g. one source omits the ordinal) are merged into the more specific entry.

### Export section layout

`pe::BuildExportSection(exports, dll_name, edata_rva)` produces:

```
IMAGE_EXPORT_DIRECTORY (40 B)
Export Address Table   (4 B per ordinal slot, indexed by ordinal − Base)
Name Pointer Table     (4 B per name, sorted lexicographically)
Ordinal Table          (2 B per name, parallel to NPT)
DLL name               (ASCIZ)
Export name strings    (ASCIZ each, in NPT order)
```

Auto-assigned ordinals start at 1 and skip values already claimed by explicit `@N` annotations. `NONAME` exports occupy their EAT slot but are excluded from the Name Pointer / Ordinal tables. `DATA` is propagated as a hint; the writer does not currently distinguish data from code in the EAT slot.

### Relocation translation

`pe::TranslateRelocationsToPEBaseRelocs(input, machine, out, errors)` is the bridge between the linker's neutral `Relocation` records and the writer's `BaseRelocation` array:

| Internal type | x86_64 PE | arm64 PE |
| --- | --- | --- |
| `R_X86_64_64`, `R_AARCH64_ABS64` | DIR64 (10) | DIR64 (10) |
| `R_X86_64_32`, `R_X86_64_32S`, `R_AARCH64_ABS32` | HIGHLOW (3) | HIGHLOW (3) |
| `R_AARCH64_CALL26` / `JUMP26` | — | ARM64 BRANCH26 (3) |
| `R_AARCH64_ADR_PREL_PG_HI21` | — | ARM64 PAGEBASE_REL21 (4) |
| `R_AARCH64_ADD_ABS_LO12_NC` | — | ARM64 PAGEOFFSET_12A (7) |
| `R_AARCH64_LDST64_ABS_LO12_NC` | — | ARM64 PAGEOFFSET_12L (9) |

PC-relative entries are skipped (PE base relocations only fix absolute addresses). Anything else — including ELF GOT/PLT-only codes such as `R_X86_64_GOTPCREL` — is reported as `polyld-err-E3210` and the call returns `false`. Translation continues for the remaining entries so a single pass surfaces the entire incompatible set.

## Release matrix and CI

PolyglotCompiler 1.42.0 ships a closed binary-container matrix.  Every
combination of the seven supported triples and the six representative
`tests/samples/` programs (`01_basic_linking`, `05_class_instantiation`,
`22_database_access`, `11_java_interop`, `14_async_pipeline`,
`19_file_io`) is exercised by `tests/integration/binary_matrix/` in
three passes:

1. **Static pass** — every cell is checked against
   `ResolveContainer(triple, kAuto)` and `SuffixesFor(container)` so the
   helper APIs cannot drift apart.
2. **Live host column** — the cell whose triple matches the build host
   actually runs `polyc -o <out>`; the produced file's magic bytes
   (`\x7FELF`, `MZ`, `\xCF\xFA\xED\xFE`, `\0asm`) must match what the
   resolver picked.
3. **Reverse assertion** — on a non-Windows host, asking polyc to
   produce `*.exe` must (a) flag `polyc-warn-W2101` and (b) write a
   file whose magic is the host's native container, never PE.  This
   permanently rules out the "fake .exe (actually ELF / Mach-O)"
   regression that was possible before 1.5.0.

`scripts/ci/run_binary_matrix.{sh,ps1}` drives the same matrix for CI
and parks artefacts under `artifacts/binary_matrix/<triple>/`.  The
scripts probe for `dumpbin` / `otool` / `readelf` / `wasm-objdump` and
fall back to in-tree byte readers when none of them are available.

`scripts/build_all_samples.{sh,ps1}` is the cross-platform regression
twin used by `tests/integration/samples_regression_test.cpp`; both
flavours emit the same `samples_report.json` schema (status buckets:
`OK`, `OUTPUT_MISMATCH`, `EMPTY_STDOUT`, `RUN_FAIL`, `LINK_FAIL`,
`COMPILE_FAIL`, `SKIP`).

## Performance baselines

`polybench link` exposes the per-container assembly cost as four
single-iteration measurements: `pe_link_time`, `macho_link_time`,
`elf_link_time`, `wasm_link_time`.  Each measurement assembles a
4 KiB-payload skeleton image identical in shape to what the production
writer emits for a hello-world program; the JSON is written to
`benchmark_link_times.json` with the active `target_triple` stamped at
the top.  Reference numbers on a 2026-vintage Apple-silicon host are
around 0.08–0.13 ms per iteration for all four containers; CI gates
regressions at `+15%` against the 1.4.x ELF baseline.
