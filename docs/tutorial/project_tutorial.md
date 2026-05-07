# PolyglotCompiler Project Tutorial

> **Version**: 3.0.0  
> **Last Updated**: 2026-05-07  
> **Project**: PolyglotCompiler 1.45.2  
> **Audience**: Contributors, integrators and toolchain authors

---

## Table of Contents

1. [Project Overview](#1-project-overview)
2. [Prerequisites](#2-prerequisites)
3. [Building from Source](#3-building-from-source)
4. [Project Architecture](#4-project-architecture)
5. [Using the Toolchain](#5-using-the-toolchain)
6. [Cross-Target & Container Matrix](#6-cross-target--container-matrix)
7. [Running Tests](#7-running-tests)
8. [Working with Sample Programs](#8-working-with-sample-programs)
9. [Development Workflow](#9-development-workflow)
10. [Frontend Development](#10-frontend-development)
11. [Middle Layer and IR](#11-middle-layer-and-ir)
12. [Backend Development](#12-backend-development)
13. [Adding a New Language Frontend](#13-adding-a-new-language-frontend)
14. [Plugins, Settings & Themes](#14-plugins-settings--themes)
15. [Debugging and Troubleshooting](#15-debugging-and-troubleshooting)
16. [Project Configuration Reference](#16-project-configuration-reference)

---

# 1. Project Overview

## 1.1 What is PolyglotCompiler?

PolyglotCompiler is a self-hosted multi-language compiler written in **C++20**. It compiles **C++**, **Python**, **Rust**, **Java**, **C# (.NET)**, **Go**, **JavaScript**, **Ruby** and the cross-language DSL **`.ploy`** to a shared SSA-form intermediate representation, then emits native code for **x86_64**, **ARM64** and **WebAssembly**. The linker (`polyld`) writes ELF, PE32+, Mach-O and Wasm directly; no external compiler is required for code generation.

### Core Capabilities

- **9 first-party frontends** registered through `FrontendRegistry`.
- **3 backends** (`backend_x86_64`, `backend_arm64`, `backend_wasm`) sharing a common `MachineIR` layer and a unified register / scheduling framework.
- **11 toolchain executables**: `polyc`, `polyld`, `polyasm`, `polyopt`, `polyrt`, `polytopo`, `polybench`, `polyls`, `polydoc`, `polyver`, `polyui`.
- **30 CTest targets** broken down by module (per-frontend, per-backend, runtime, linker, lsp, settings, samples regression, benchmarks).
- **Cross-language interop** via `.ploy` (`LINK`, `CALL`, `NEW`, `METHOD`, `GET`, `SET`, `WITH`, `DELETE`, `EXTEND`, `IMPORT … PACKAGE`, `MAP_TYPE`, `CONVERT`, `PIPELINE`).
- **Container matrix** — `polyc --target=<triple> --container=<auto|elf|pe|macho|wasm>` covers Linux / Windows / macOS / WASI on x86_64 and arm64.
- **Self-hosted Language Server** (`polyls`) over stdio JSON-RPC, consumed by `polyui` and any LSP-aware editor.
- **Plugin system** with a stable C ABI (frontends, optimisers, backends, IDE panels, formatters, linters, debuggers, completion / diagnostic providers).
- **Settings & theme contract** modelled on VS Code (three-layer JSON, schema, hot reload, command palette, keybindings, external `.polytheme.json`).

## 1.2 Technology Stack

| Component         | Technology                                                                            |
|-------------------|---------------------------------------------------------------------------------------|
| Language          | C++20                                                                                 |
| Build system      | CMake ≥ 3.20                                                                          |
| Build tool        | Ninja (recommended) or Make                                                           |
| Test framework    | Catch2 v3                                                                             |
| Formatting        | fmt ≥ 11.2.0                                                                          |
| JSON              | nlohmann/json                                                                         |
| Memory allocator  | mimalloc (`mi_*` APIs only; no global malloc override across dylib boundaries)        |
| GUI (optional)    | Qt 6 (recommended) or Qt 5.15+                                                         |
| License           | GPL v3                                                                                |

---

# 2. Prerequisites

## 2.1 Required Software

| Platform | Recommended Compiler          | Minimum Version |
|----------|-------------------------------|-----------------|
| Windows  | MSVC (Visual Studio 2022+)    | v19.30+         |
| Linux    | GCC                           | 12+             |
| Linux    | Clang                         | 15+             |
| macOS    | Apple Clang                   | 15+             |

| Build Tool | Version    | Notes                                                |
|------------|------------|------------------------------------------------------|
| CMake      | ≥ 3.20     | Build system generator                                |
| Ninja      | any recent | Recommended generator (faster than Make)              |

## 2.2 Optional Runtimes (for cross-language samples)

| Runtime              | Purpose                                 |
|----------------------|-----------------------------------------|
| Python 3.10+         | Python frontend & samples               |
| Rust (rustup)        | Rust frontend & samples                 |
| Java JDK 21+         | Java frontend & samples                 |
| .NET SDK 9+          | .NET (C#) frontend & samples            |
| Go 1.22+             | Go frontend & samples                   |
| Node.js 20+          | JavaScript frontend & samples           |
| Ruby 3.2+            | Ruby frontend & samples                 |

`polyver` probes the host for these toolchains and writes the result into the toolchain database that `polyc` consults during driver bring-up. Run it once after installing or updating any runtime:

```bash
polyver detect            # one-shot detection
polyver detect --json     # machine-readable inventory
```

## 2.3 Environment Setup Scripts

Sample-driven cross-language setup lives under `tests/samples/`:

```powershell
# Windows (PowerShell)
cd tests\samples
.\setup_env.ps1
```

```bash
# Linux / macOS
cd tests/samples
chmod +x setup_env.sh
./setup_env.sh
```

The scripts create the per-language environments (Python venv, cargo target dir, JDK, .NET SDK, Go module cache, Node `node_modules`, Bundler `Gemfile.lock`) and link them into every sample directory.

---

# 3. Building from Source

## 3.1 Clone

```bash
git clone <repository-url>
cd PolyglotCompiler
```

## 3.2 Build on Linux / macOS

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

## 3.3 Build on Windows (MSVC + Ninja)

```cmd
rem MUST use -arch=amd64 to avoid an x86 ↔ x64 linker mismatch.
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64
cmake -B build -G Ninja
cmake --build build -j
```

## 3.4 Build Targets

| Target              | Description                                        | Executable                  |
|---------------------|----------------------------------------------------|-----------------------------|
| `polyc`             | Compiler driver                                    | `polyc[.exe]`               |
| `polyld`            | Linker (ELF / PE32+ / Mach-O / Wasm)               | `polyld[.exe]`              |
| `polyasm`           | Assembler                                          | `polyasm[.exe]`             |
| `polyopt`           | Standalone IR optimiser                            | `polyopt[.exe]`             |
| `polyrt`            | Runtime tool (GC, FFI, async, profiling driver)    | `polyrt[.exe]`              |
| `polytopo`          | Topology graph analyser                            | `polytopo[.exe]`            |
| `polybench`         | Benchmark suite                                    | `polybench[.exe]`           |
| `polyls`            | Language Server (stdio JSON-RPC)                   | `polyls[.exe]`              |
| `polydoc`           | `.ploy` doc-comment extractor                      | `polydoc[.exe]`             |
| `polyver`           | Toolchain probe & database writer                  | `polyver[.exe]`             |
| `polyui`            | Qt desktop IDE (skipped if Qt is not found)        | `polyui[.exe]` / `polyui.app` |
| `unit_tests` …      | Aggregate + per-module test binaries (see §7)      | various                     |

```bash
cmake --build build --target polyc           # one tool
cmake --build build --target test_linker     # one test binary
cmake --build build                          # everything
```

## 3.5 CMake Dependencies

`Dependencies.cmake` declares the libraries fetched automatically by `FetchContent` into `build/_deps/`:

| Library         | Purpose                                                                                                  |
|-----------------|----------------------------------------------------------------------------------------------------------|
| Catch2          | Unit test framework                                                                                      |
| fmt             | Formatted I/O (≥ 11.2.0 required for Apple Clang 21)                                                     |
| nlohmann/json   | JSON parsing / serialisation                                                                             |
| mimalloc        | High-performance memory allocator (`mi_*` APIs only; global malloc override disabled across dylibs)      |

## 3.6 Build Configurations

```bash
cmake -B build      -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake -B build-rel  -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake -B build-rwd  -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake -B build-san  -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DPOLYGLOT_ENABLE_ASAN=ON -DPOLYGLOT_ENABLE_UBSAN=ON \
      -DBUILD_SHARED_LIBS=OFF
```

The `BUILD_SHARED_LIBS` option defaults to `ON`; a single shared common library is loaded by every executable to avoid duplication.

---

# 4. Project Architecture

## 4.1 Directory Structure

```
PolyglotCompiler/
├── frontends/              # 9 language frontends
│   ├── common/             #   Shared token pool, preprocessor, diagnostics
│   ├── cpp/                #   C++ frontend (lexer, parser, sema, lowering, constexpr)
│   ├── python/             #   Python frontend
│   ├── rust/               #   Rust frontend (cargo metadata + crate loader)
│   ├── java/               #   Java frontend (Java 8 / 17 / 21 / 23)
│   ├── dotnet/             #   .NET (C#) frontend (.NET 6 / 7 / 8 / 9)
│   ├── go/                 #   Go frontend (go.mod + GOROOT / GOPATH resolver)
│   ├── javascript/         #   JavaScript / TypeScript frontend (Node.js resolver)
│   ├── ruby/               #   Ruby frontend (require / Gemfile / Bundler)
│   └── ploy/               #   .ploy cross-language frontend
├── middle/                 # SSA IR, optimisation passes, PGO, LTO
│   ├── include/{ir,passes,pgo,lto}/
│   └── src/{ir,passes,pgo,lto}/
├── backends/               # 3 architecture backends
│   ├── common/             #   MachineIR, debug info (DWARF / PDB), object emission
│   ├── x86_64/             #   x86_64 backend (isel, regalloc, asm_printer, scheduler)
│   ├── arm64/              #   ARM64 backend (isel, regalloc, asm_printer)
│   └── wasm/               #   WebAssembly backend (shadow stack, WAT / binary)
├── runtime/                # Runtime system
│   ├── include/            #   GC / FFI / async / threading interfaces
│   └── src/{gc,interop,libs,services}/
│       └── libs/           #     python_rt, cpp_rt, rust_rt, java_rt, dotnet_rt,
│                           #     go_rt, javascript_rt, ruby_rt
├── common/                 # Type system, symbol table, DWARF 5, host triple, settings
├── tools/                  # Driver, linker, assembler, optimiser, runtime tool,
│                           # topology, benchmark, LSP server, doc, version probe, IDE
├── tests/
│   ├── unit/               #   Per-module unit tests (Catch2)
│   ├── integration/        #   Full-pipeline & binary-matrix tests
│   ├── benchmarks/         #   Micro + macro benchmarks
│   └── samples/            #   42 sample programs (`00_minimal` … `41_grammar_polish`)
├── scripts/                # CI helpers, packaging, samples harness, doc gates
├── docs/
│   ├── api/                #   API reference (bilingual)
│   ├── specs/              #   Language & IR specs, ABI, plugin spec
│   ├── realization/        #   Implementation details (bilingual)
│   ├── tutorial/           #   Tutorials (this file lives here)
│   └── demand/             #   Project demand log
├── CMakeLists.txt          # Root CMake configuration (project version lives here)
├── Dependencies.cmake      # External dependency declarations
└── VERSION.txt             # Plain-text version mirror, regenerated by CMake
```

## 4.2 Compilation Pipeline

```
Source Code
 (.cpp / .py / .rs / .java / .cs / .go / .js / .rb / .ploy)
        │
        ▼
┌─────────┐   Tokens   ┌─────────┐   AST    ┌─────────┐  Checked AST  ┌──────────┐  IR
│  Lexer  │──────────▶│ Parser  │────────▶│  Sema   │──────────────▶│ Lowering │────┐
└─────────┘            └─────────┘          └─────────┘                └──────────┘    │
                                                                                       ▼
                                                          ┌────────────────────────────────────┐
                                                          │   Shared SSA IR (CFG + functions)  │
                                                          └─────────────────┬──────────────────┘
                                                                            │
                                                          ┌─────────────────┼──────────────────┐
                                                          ▼                 ▼                  ▼
                                                    ┌──────────┐    ┌──────────┐         ┌──────────┐
                                                    │ Opt Pass │    │   PGO    │         │   LTO    │
                                                    │ Pipeline │    │ rewrite  │         │ rewrite  │
                                                    └────┬─────┘    └────┬─────┘         └────┬─────┘
                                                         └────────────────┴────────────────────┘
                                                                            ▼
                                                          ┌────────────────────────────────────┐
                                                          │  Backend (x86_64 / arm64 / wasm)   │
                                                          │  isel → regalloc → asm_printer     │
                                                          └─────────────────┬──────────────────┘
                                                                            ▼
                                                          ┌────────────────────────────────────┐
                                                          │  polyld → ELF / PE32+ / Mach-O /   │
                                                          │           Wasm  +  cross-language  │
                                                          │           glue & marshalling stubs │
                                                          └────────────────────────────────────┘
```

## 4.3 Frontend Architecture

Every frontend follows the same 4-stage pipeline:

| Stage     | Class               | Responsibility                                                |
|-----------|---------------------|---------------------------------------------------------------|
| Lexer     | `*_lexer.h`         | Tokenise source into the shared `Token` stream                |
| Parser    | `*_parser.h`        | Parse tokens into the language-specific AST                   |
| Sema      | `*_sema.h`          | Name resolution, type checking, semantic validation           |
| Lowering  | `*_lowering.h`      | Lower the annotated AST into the shared SSA IR                |

All frontends register with `FrontendRegistry` and share `frontend_common`'s `SharedTokenPool` (arena lexemes + open-addressing identifier interning + snapshot/restore for parser look-ahead).

## 4.4 CMake Library Targets

| Target                    | Source root              | Description                                          |
|---------------------------|--------------------------|------------------------------------------------------|
| `polyglot_common`         | `common/`                | Type system, symbol table, DWARF 5, host triple      |
| `frontend_common`         | `frontends/common/`      | Token pool, preprocessor, diagnostics                |
| `frontend_cpp`            | `frontends/cpp/`         | C++ frontend                                         |
| `frontend_python`         | `frontends/python/`      | Python frontend                                      |
| `frontend_rust`           | `frontends/rust/`        | Rust frontend                                        |
| `frontend_java`           | `frontends/java/`        | Java frontend                                        |
| `frontend_dotnet`         | `frontends/dotnet/`      | .NET (C#) frontend                                   |
| `frontend_go`             | `frontends/go/`          | Go frontend                                          |
| `frontend_javascript`     | `frontends/javascript/`  | JavaScript / TypeScript frontend                     |
| `frontend_ruby`           | `frontends/ruby/`        | Ruby frontend                                        |
| `frontend_ploy`           | `frontends/ploy/`        | `.ploy` cross-language frontend                      |
| `middle_ir`               | `middle/`                | IR, optimisation passes, PGO, LTO                    |
| `backend_x86_64`          | `backends/x86_64/`       | x86_64 backend                                       |
| `backend_arm64`           | `backends/arm64/`        | ARM64 backend                                        |
| `backend_wasm`            | `backends/wasm/`         | WebAssembly backend                                  |
| `runtime`                 | `runtime/`               | GC / FFI / async / threading                          |
| `linker_lib`              | `tools/polyld/`          | Linker library reused by `polyld` and tests          |
| `polyls_core`             | `tools/polyls/`          | LSP server core (capabilities, navigation, index)    |

---

# 5. Using the Toolchain

## 5.1 polyc — Compiler Driver

The driver auto-detects the source language from the file extension and pipes it through the matching frontend, the shared optimiser and the requested backend.

```bash
# Auto-detected
polyc sample.ploy   -o sample
polyc hello.cpp     -o hello
polyc script.py     -o script
polyc Main.java     -o main
polyc Program.cs    -o program
polyc app.go        -o app
polyc index.js      -o index
polyc gem.rb        -o gem

# Explicit language override
polyc --lang=cpp    input_file -o output

# Emit intermediate artifacts
polyc --emit-ir=output.ir   input.ploy
polyc --emit-asm=output.s   input.ploy
polyc --emit=call-graph:cg.json input.ploy
polyc --emit-obj=out.o      input.ploy

# Cross-target compilation
polyc --target=aarch64-apple-darwin    --container=macho -o app  main.cpp
polyc --target=x86_64-pc-windows-msvc  --container=pe    -o app.exe main.cpp
polyc --target=wasm32-wasi             --container=wasm  -o app.wasm main.cpp

# Settings & diagnostics
polyc --settings ./.polyglot/settings.json --print-effective-settings
polyc --check broken.ploy            # LSP-shaped JSON diagnostics on stdout
polyc --dump-token-pool              # SharedTokenPool stats
polyc --progress=json                # machine-readable stage events
polyc --clean-cache                  # clear incremental cache
```

When `polyc` invokes the linker it resolves `polyld` relative to its own binary location, so a built tree works in place without modifying `PATH`.

## 5.2 polyld — Linker

```bash
polyld -o program file1.o file2.o
polyld --polyglot -o program main.o bridge.o          # cross-language glue
polyld --target=aarch64-apple-darwin --container=macho -o app main.o
```

POSIX hosts receive mode `0755` automatically. On macOS arm64 the writer aligns every `LC_SEGMENT_64` to 16 KiB and emits `__DATA_CONST` as `rw-` with `SG_READ_ONLY`, which is what dyld 26 (Tahoe) requires before applying chained fixups.

## 5.3 polyasm — Assembler

```bash
polyasm input.s -o output.o
polyasm --target=arm64 input.s -o output.o
```

## 5.4 polyopt — Standalone Optimiser

```bash
polyopt -O0 input.ir -o out.ir            # canonicalise only
polyopt -O3 input.ir -o out.ir
polyopt --pass=constant-fold --pass=dce --pass=gvn input.ir -o out.ir
polyopt --pgo-profile=app.profdata -O2 input.ir -o out.ir
polyopt --lto-summary=app.lto.json input.ir -o out.ir
```

## 5.5 polyrt — Runtime Tool

```bash
polyrt --gc-mode=generational --threads=4
polyrt profile --json profile.json --duration-ms 5000 build/app
polyrt async   --json   # cooperative event-loop snapshot
polyrt async   --run=64 # drive the loop for 64 ticks
polyrt calltrace --json calltrace.json
```

## 5.6 polytopo — Topology Analyser

```bash
polytopo build/app.cgjson                     # ASCII text view (default)
polytopo --view-mode=dot  build/app.cgjson    # Graphviz DOT
polytopo --view-mode=json build/app.cgjson    # JSON (scriptable)
polytopo --filter-language=python build/app.cgjson
```

## 5.7 polyls — Language Server

`polyls` is a regular stdio LSP server that speaks LSP 3.17. `polyui` spawns one server per language using its `IdeLspBridge`; any other LSP-aware editor (VS Code, Neovim, Helix …) can spawn it too.

```bash
polyls                           # stdio server
polyls --log polyls.log          # capture JSON-RPC frames for debugging
```

Test coverage for the server lives under `test_polyls` (server / capabilities), `test_lsp` (framing & negotiation), `test_problems` (diagnostic surfacing) and `test_completion_ranker`.

## 5.8 polydoc — Doc Extractor

```bash
polydoc tests/samples/01_basic_linking/basic_linking.ploy        # Markdown to stdout
polydoc --json tests/samples/01_basic_linking/basic_linking.ploy # JSON to stdout
polydoc -o docs/out.md tests/samples/01_basic_linking/basic_linking.ploy
```

`polydoc` walks every `///` doc-comment block attached to top-level `FUNC`, `STRUCT`, `LET` or `VAR` declarations.

## 5.9 polyver — Toolchain Probe

```bash
polyver detect                  # print a table of detected toolchains
polyver detect --json           # JSON inventory
polyver db --print              # dump the toolchain database
polyver db --refresh            # force a re-detection
```

## 5.10 polybench — Benchmark Suite

```bash
polybench --suite=micro
polybench --suite=macro
polybench --all
polybench --fast                # subset used by the `benchmark_fast` CTest target
```

## 5.11 polyui — Desktop IDE

`polyui` is the Qt-based IDE. Highlights:

- Real-time diagnostics (LSP via `polyls`).
- Topology panel (link graph, language filter, batch operations).
- Profiler panel (`Ctrl+Alt+P`): flame, hotspots, timeline, per-language breakdown driven by `polybench` + `polyrt`.
- Call Analyzer panel (`Ctrl+Alt+G`): static graph from `polyc --emit=call-graph` with caller / callee trees and bounded DFS path search.
- Problems panel: filterable diagnostics list (severity, file substring, message regex).
- Theme manager (`Ctrl+K, Ctrl+T`), command palette (`Ctrl+Shift+P`), keybindings (`keybindings.json`).

---

# 6. Cross-Target & Container Matrix

`polyc --target=<triple>` and `polyc --container=<auto|elf|pe|macho|wasm>` cover every cell in the matrix below. Without `--target` the driver falls back to `common::HostTriple()`; without `--container` the OS family in column 3 is used. A mismatch between the resolved container and the `-o` suffix raises `polyc-warn-W2101`.

| Triple                       | Container | Default exec suffix |
|------------------------------|-----------|---------------------|
| `x86_64-pc-windows-msvc`     | PE32+     | `.exe`              |
| `aarch64-pc-windows-msvc`    | PE32+     | `.exe`              |
| `x86_64-unknown-linux-gnu`   | ELF       | (none)              |
| `aarch64-unknown-linux-gnu`  | ELF       | (none)              |
| `x86_64-apple-darwin`        | Mach-O    | (none)              |
| `aarch64-apple-darwin`       | Mach-O    | (none)              |
| `wasm32-wasi`                | Wasm      | `.wasm`             |

The matrix is exercised from two places:

- `scripts/ci/run_binary_matrix.{sh,ps1}` — runs every cell on the matching CI runner.
- `tests/integration/binary_matrix/` — fails the `integration_tests` target whenever a writer regresses.

Notes specific to macOS arm64 (latest fix landed in 1.45.2):

- All `LC_SEGMENT_64` records are aligned to 16 KiB (`PageSizeForArch(MachOArch::kArm64)`); 4 KiB alignment is silently rejected by the kernel mapper with exit status 137.
- `__DATA_CONST` is emitted as `rw-` with `flags = SG_READ_ONLY (0x10)` so dyld can apply chained fixups in place and re-protect the segment to `r--` afterwards.

---

# 7. Running Tests

## 7.1 CTest Matrix

The historical monolithic `unit_tests` binary was split into 24 per-module binaries; the aggregate target is preserved for backward compatibility. The CTest matrix has 30 targets:

| #  | Target                     | Scope                                                   |
|----|----------------------------|---------------------------------------------------------|
| 1  | `test_core`                | Common utilities, type system, symbol table             |
| 2  | `test_plugins`             | Plugin loader, sandbox, version contract                |
| 3  | `test_frontend_common`     | Token pool, preprocessor, diagnostics                   |
| 4  | `test_frontend_python`     | Python frontend                                         |
| 5  | `test_frontend_cpp`        | C++ frontend                                            |
| 6  | `test_frontend_rust`       | Rust frontend                                           |
| 7  | `test_frontend_ploy`       | `.ploy` frontend                                        |
| 8  | `test_frontend_java`       | Java frontend                                           |
| 9  | `test_frontend_dotnet`     | .NET (C#) frontend                                      |
| 10 | `test_frontend_javascript` | JavaScript frontend                                     |
| 11 | `test_frontend_ruby`       | Ruby frontend                                           |
| 12 | `test_frontend_go`         | Go frontend                                             |
| 13 | `test_middle`              | SSA IR, optimisation passes, PGO, LTO                   |
| 14 | `test_backends`            | x86_64 / ARM64 / Wasm code generation                   |
| 15 | `test_runtime`             | GC, FFI, marshalling, threading                         |
| 16 | `test_linker`              | `polyld` ELF / PE / Mach-O / Wasm writers, glue codegen |
| 17 | `test_topology`            | Topology graph, link validation                         |
| 18 | `test_settings`            | Three-layer settings, schema, hot reload                |
| 19 | `test_lsp`                 | LSP framing, JSON-RPC, capability negotiation           |
| 20 | `test_polyls`              | `polyls` server: completion, navigation, symbol index   |
| 21 | `test_problems`            | Diagnostic surfacing, problems panel contract           |
| 22 | `test_completion_ranker`   | Completion ranking heuristics                           |
| 23 | `test_topology_ui`         | Topology panel UI bindings                              |
| 24 | `test_e2e`                 | End-to-end pipelines                                    |
| 25 | `unit_tests`               | Aggregate compatibility binary                          |
| 26 | `integration_tests`        | Full pipeline & cross-language interop                  |
| 27 | `benchmark_tests`          | Benchmark suite                                         |
| 28 | `samples_regression`       | Drives `tests/samples/**` through `polyc` + `polyld`    |
| 29 | `benchmark_fast`           | Fast (`[fast]`) Catch2 subset for CI smoke              |
| 30 | `benchmark_full`           | Full benchmark run                                      |

## 7.2 Common Invocations

```bash
cd build
ctest --output-on-failure                       # everything
ctest -R test_frontend_ploy                     # one target
ctest -L benchmark                              # by label
./test_frontend_ploy [parser]                   # by Catch2 tag
./unit_tests "[ploy],[python]"                  # combined tags via aggregate
```

## 7.3 Sanitizer & Coverage Builds

```bash
cmake -B build-san -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DPOLYGLOT_ENABLE_ASAN=ON \
  -DPOLYGLOT_ENABLE_UBSAN=ON \
  -DBUILD_SHARED_LIBS=OFF
cmake --build build-san
cd build-san && ctest --output-on-failure -LE benchmark

cmake -B build-cov -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DPOLYGLOT_ENABLE_COVERAGE=ON \
  -DBUILD_SHARED_LIBS=OFF
cmake --build build-cov
cd build-cov && ctest --output-on-failure -LE benchmark
lcov --capture --directory . --output-file coverage.info
```

---

# 8. Working with Sample Programs

## 8.1 Inventory

`tests/samples/` ships **42 samples** ranging from a single-line minimum through full-stack multi-language pipelines. The harness drives them via `samples_regression`; each directory may contain `expected_output.txt` (byte-pinned stdout) or `expected_output.skip` (route to the SKIP bucket without invoking `polyc`).

| Range                | Theme                                                                            |
|----------------------|----------------------------------------------------------------------------------|
| `00_minimal`         | Single-line minimum sample (host-portable stdout pinning).                       |
| `01` … `09`          | Core `.ploy` interop (LINK, MAP_TYPE, PIPELINE, control flow, OOP, mixed).       |
| `10` … `16`          | Diagnostics, Java / .NET interop, generics, async, full stack, CONFIG / VENV.    |
| `17` … `30`          | Real-world domains (string / numeric / file / JSON / image / SQL / HTTP …).      |
| `31` … `41`          | Recent language features (typed handles, pattern match, defaults, EXTEND on dynamic langs, TRY/CATCH, async/await, generics, visibility/attrs, string literals, grammar polish). |

## 8.2 Sample Layout

```
NN_feature_name/
├── feature_name.ploy         # .ploy entry point
├── source_file.cpp           # per-language sources
├── source_file.py
├── source_file.rs
├── SourceFile.java
├── SourceFile.cs
├── source_file.go
├── source_file.js
├── source_file.rb
├── expected_output.txt       # byte-pinned stdout (optional)
└── expected_output.skip      # route to SKIP bucket (optional)
```

## 8.3 Compiling a Sample

```bash
polyc tests/samples/01_basic_linking/basic_linking.ploy   -o basic_linking
polyc tests/samples/09_mixed_pipeline/mixed_pipeline.ploy -o mixed_pipeline
polyc tests/samples/15_full_stack/full_stack.ploy         -o full_stack
```

## 8.4 Driving the Whole Matrix

```bash
# POSIX
scripts/build_all_samples.sh   --polyc build/polyc --polyld build/polyld
# Windows
scripts\build_all_samples.ps1  -Polyc build\polyc.exe -Polyld build\polyld.exe
```

The harness writes `samples_report.json` (with a top-level `ok` array sorted ASCII), which `samples_regression_test.cpp` consumes to assert that the OK set reported by the harness matches the OK set walked out of the per-sample status fields.

## 8.5 Suggested Learning Path

1. `00_minimal` → `01_basic_linking` → `02_type_mapping`
2. Control flow & pipelines: `03_pipeline`
3. Package management: `04_package_import`, `16_config_and_venv`
4. OOP interop: `05_class_instantiation` → `08_delete_extend`
5. Mixed language: `09_mixed_pipeline` → `15_full_stack`
6. Real-world domains: `17_string_processing` … `30_game_loop_demo`
7. Recent language features: `33_pattern_matching`, `36_try_catch`, `37_async_await`, `38_generics`, `39_visibility_attrs`, `40_string_literals`

---

# 9. Development Workflow

## 9.1 Typical Loop

```
1. Edit code
2. Build:        cmake --build build -j
3. Test:         (cd build && ctest --output-on-failure -R <target>)
4. Iterate
5. Verify:       (cd build && ctest --output-on-failure -LE benchmark)
```

## 9.2 Incremental Builds

Ninja recompiles only what changed. `compile_commands.json` is generated automatically (`-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` is the default) so `clangd` and IDE IntelliSense work out of the box.

## 9.3 Clean Rebuild

```bash
cmake --build build --target clean && cmake --build build -j
# or
rm -rf build && cmake -B build -G Ninja && cmake --build build -j
```

## 9.4 Doc & Format Gates (run before pushing)

```bash
# Format dry-run
find common middle frontends backends runtime tools \
  -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.c' \) \
  -not -path '*/deps/*' -not -path '*/.cache/*' -print0 \
  | xargs -0 clang-format --dry-run --Werror --style=file

# Bilingual doc sync gate (core docs)
python scripts/docs_sync_check.py --ci --scope core

# Path / heading / version lint
python scripts/docs_lint.py
```

---

# 10. Frontend Development

## 10.1 Layout

```
frontends/<language>/
├── include/
│   ├── <lang>_ast.h        # AST node definitions (where separated)
│   ├── <lang>_lexer.h
│   ├── <lang>_parser.h
│   ├── <lang>_sema.h
│   └── <lang>_lowering.h
└── src/
    ├── lexer/
    ├── parser/
    ├── sema/
    └── lowering/
```

## 10.2 Lexer

Lexers consume the shared `frontend_common::SharedTokenPool` so identifier interning and lexeme arenas are paid for once across the entire compilation unit:

```cpp
Token Lex(const std::string_view source) {
    auto handle = pool_.Intern(source);              // open-addressing identifier table
    return Token{TokenKind::kIdentifier, handle, loc_};
}
```

## 10.3 Parser

Parsers may snapshot / restore the token pool to support unbounded look-ahead:

```cpp
auto snapshot = pool_.Snapshot();
auto maybe_decl = TryParseDecl();
if (!maybe_decl) {
    pool_.Restore(snapshot);
}
```

## 10.4 Sema

Sema is responsible for name resolution, type checking, language-specific validation, and recording diagnostics through the shared `Diagnostics` sink. Errors carry source locations (file, line, column, length) and a stable diagnostic id (e.g. `polyc-err-E3010`).

## 10.5 Lowering

Lowering transforms the type-annotated AST into the shared SSA IR through `middle_ir`'s `IRBuilder`. Each `LET` becomes an `alloca` + `store`; each `CALL` becomes a typed call instruction; control flow lowers to basic blocks with explicit branches.

---

# 11. Middle Layer and IR

## 11.1 IR Design

- SSA-form, split into **functions → basic blocks → instructions**.
- Types are language-independent (`i1`, `i8`, `i16`, `i32`, `i64`, `f32`, `f64`, `void`, `T*`, struct types, array types).
- Calls are typed; cross-language calls carry an explicit ABI tag consumed by the linker glue generator.

## 11.2 Optimisation Passes

| Pass                        | Category   | Description                                          |
|-----------------------------|------------|------------------------------------------------------|
| Constant folding            | Transform  | Evaluate constant expressions at compile time        |
| Dead code elimination       | Transform  | Remove unreachable / unused IR                       |
| GVN                         | Transform  | Global value numbering                               |
| Inlining                    | Transform  | Replace call sites with bodies (with budget)         |
| Devirtualisation            | Transform  | Resolve virtual calls to direct calls                |
| Loop optimisation           | Transform  | LICM, unrolling, vectorisation                       |
| LCSSA / loop closed SSA     | Analysis   | Required by loop transforms                          |
| PGO rewriter                | Transform  | Apply profile data to inlining / layout decisions    |
| LTO summariser              | Analysis   | Build whole-program summary for LTO link step        |

## 11.3 IR Builder

```cpp
auto* func   = module.CreateFunction("compute", i32_, {i32_, i32_});
auto* entry  = func->CreateBlock("entry");
builder.SetInsertPoint(entry);
auto* sum    = builder.CreateAdd(func->arg(0), func->arg(1));
builder.CreateRet(sum);
```

---

# 12. Backend Development

## 12.1 Layout

```
backends/<arch>/
├── include/
│   ├── <arch>_target.h
│   ├── <arch>_register.h
│   └── machine_ir.h
└── src/
    ├── isel/
    ├── regalloc/
    │   ├── graph_coloring.cpp
    │   └── linear_scan.cpp
    ├── asm_printer/
    └── calling_convention/
```

## 12.2 Highlights

- **x86_64** — System V (Linux / macOS) + Win64 calling conventions; SSE2 / AVX / AVX2 / AVX-512 vectorisation.
- **ARM64** — AAPCS64; NEON SIMD; macOS arm64 emits Mach-O 16 KiB-aligned segments.
- **WASM** — WAT and binary outputs, shadow stack, WASI imports for the runtime bridge.

---

# 13. Adding a New Language Frontend

1. Create `frontends/<new_lang>/{include,src}` with the four pipeline files.
2. Add the CMake target in `frontends/CMakeLists.txt`:
    ```cmake
    add_library(frontend_new_lang ...)
    target_include_directories(frontend_new_lang PUBLIC frontends/new_lang/include)
    target_link_libraries(frontend_new_lang PUBLIC polyglot_common frontend_common middle_ir)
    ```
3. Register the frontend with `FrontendRegistry` in the new language's `*_frontend.cpp`:
    ```cpp
    static FrontendRegistration g_registration{
        /*language=*/"new_lang",
        /*extensions=*/{".nlg"},
        /*factory=*/[]() { return std::make_unique<NewLangFrontend>(); }
    };
    ```
4. Wire the language into `tools/polyc/src/driver.cpp` so the extension routes to the new factory.
5. Add tests:
    - `tests/unit/frontends/<new_lang>/` — Catch2 cases under a new tag.
    - Add a `test_frontend_<new_lang>` CMake target mirroring the existing per-frontend binaries.
6. Add at least one sample under `tests/samples/`.
7. Update `docs/realization/<new_lang>_frontend.md` (bilingual) and the language-table in `docs/specs/`.

Integration checklist:

- [ ] Lexer covers every keyword, operator and literal.
- [ ] Parser matches the language reference grammar.
- [ ] Sema enforces type rules and surfaces language-specific diagnostics.
- [ ] Lowering preserves observable semantics through the SSA IR.
- [ ] `polyc` driver detects the file extension.
- [ ] `test_frontend_<new_lang>` target is wired into `tests/CMakeLists.txt`.
- [ ] At least one `tests/samples/` entry runs to OK in `samples_regression`.

---

# 14. Plugins, Settings & Themes

## 14.1 Plugins

Plugins are shared libraries (`polyplug_*.so` / `polyplug_*.dylib` / `polyplug_*.dll`) discovered from the search paths in the settings or loaded by absolute path. They expose extensions through a stable C ABI:

| Capability       | Description                                                       |
|------------------|-------------------------------------------------------------------|
| Language          | Add a frontend via `FrontendRegistry` / `ILanguageFrontend`       |
| Optimiser         | Add a custom optimisation pass                                    |
| Backend           | Add a code-generation target                                       |
| Tool              | Add a CLI tool or pipeline stage                                  |
| UI Panel          | Add an IDE panel / dock widget                                    |
| Code Action       | Quick-fix / refactoring action                                    |
| Formatter / Linter / Debugger / Completion / Diagnostic / Template / Topology Proc | per-language editor extensions |

Every plugin declares `min_host_version`; the host rejects incompatible plugins at load time. A circuit breaker auto-disables a plugin after repeated callback failures (configurable via `SandboxPolicy`).

## 14.2 Settings

Three-layer JSON, exactly like VS Code:

1. `default settings` (built-in, validated against the JSON schema in `common/include/settings.schema.json`).
2. `~/.polyglot/settings.json` (user-wide).
3. `<workspace>/.polyglot/settings.json` (workspace-scoped).

Every CLI tool honours the same files via `--settings <path>` and `--print-effective-settings`. `polyui` reloads on save (the schema gates invalid keys).

## 14.3 Themes

External `.polytheme.json` files (with optional sibling `.qss`) discovered from a 3-layer contract: built-in qrc / `~/.polyglot/themes/` / `<workspace>/.polyglot/themes/`. Built-in: `polyglot.dark`, `polyglot.light`, `polyglot.hc`, `solarized.light`, `solarized.dark`. CLI: `--theme`, `--list-themes`, `--validate-theme`, `--headless --screenshot`.

---

# 15. Debugging and Troubleshooting

## 15.1 Build Issues

| Symptom                                                           | Cause / Fix                                                                                                              |
|-------------------------------------------------------------------|--------------------------------------------------------------------------------------------------------------------------|
| `LNK1112: module machine type 'x86' conflicts with target …` (Windows) | The MSVC environment was set up for x86. Always use `VsDevCmd.bat -arch=amd64`.                                          |
| `CMake Error: CMake 3.20 or higher is required`                   | Update CMake.                                                                                                            |
| `Could not find Ninja`                                            | Install Ninja and ensure it is on `PATH`.                                                                                |
| `fmt` build errors with Apple Clang 21                            | Reconfigure to fetch fmt ≥ 11.2.0.                                                                                       |

## 15.2 Sample / Runtime Issues

- **Missing per-language toolchain** — run `tests/samples/setup_env.{sh,ps1}` and re-run `polyver detect`.
- **Stale build after demand churn** — `cmake --build build --target clean && cmake --build build -j`.
- **macOS arm64 binary aborts at launch** — make sure you are using `polyld` ≥ 1.45.2 (the writer aligns segments to 16 KiB and tags `__DATA_CONST` with `SG_READ_ONLY`).
- **`execve(2)` returns 137** — POSIX hosts must produce mode `0755`; old `polyld` versions used `0644`. Upgrade.

## 15.3 Diagnostics Toolbox

```bash
polyc --emit-ir=debug.ir input.ploy            # inspect the IR
polyc --check input.ploy | jq '.diagnostics[]' # LSP-shaped JSON
ctest -V --output-on-failure -R <target>       # verbose CTest
./test_frontend_ploy "<test name>" -s          # Catch2 verbose
polyrt async --json                             # event-loop snapshot
polytopo --view-mode=dot build/app.cgjson | dot -Tsvg > app.svg
```

---

# 16. Project Configuration Reference

## 16.1 `CMakeLists.txt`

The root file declares:

1. Project metadata (name, **version**, languages).
2. C++20 standard.
3. Inclusion of `Dependencies.cmake`.
4. Library targets per component.
5. Executable targets per tool.
6. CTest configuration (per-module + aggregate).

The project version lives in `project(PolyglotCompiler VERSION X.Y.Z LANGUAGES C CXX)`. After bumping it, CMake regenerates `common/include/version.h` and `VERSION.txt` automatically; every C++ source consumes the version through `common/include/version.h` rather than hard-coded literals.

## 16.2 `Dependencies.cmake`

Declares external dependencies fetched with `FetchContent_Declare` / `FetchContent_MakeAvailable`. Update version pins here, never inline in component CMake files.

## 16.3 Key CMake Variables

| Variable                            | Default          | Description                                                                |
|-------------------------------------|------------------|----------------------------------------------------------------------------|
| `CMAKE_BUILD_TYPE`                  | `Debug`          | Build configuration                                                        |
| `CMAKE_CXX_STANDARD`                | `20`             | C++ standard                                                               |
| `CMAKE_EXPORT_COMPILE_COMMANDS`     | `ON`             | Generate `compile_commands.json`                                           |
| `BUILD_SHARED_LIBS`                 | `ON`             | Build common code as shared libraries                                      |
| `POLYGLOT_VERSION_SUFFIX`           | (empty)          | Pre-release suffix appended to the canonical version (e.g. `-pre.1`)       |
| `POLYGLOT_ENABLE_ASAN` / `_UBSAN`   | `OFF`            | Enable AddressSanitizer / UndefinedBehaviorSanitizer                       |
| `POLYGLOT_ENABLE_COVERAGE`          | `OFF`            | Enable lcov / gcov coverage instrumentation                                |

## 16.4 Documentation Map

| Path                  | Content                                                         |
|-----------------------|-----------------------------------------------------------------|
| `docs/api/`           | API reference (bilingual)                                        |
| `docs/specs/`         | Language & IR specifications, runtime ABI, plugin spec, packaging |
| `docs/realization/`   | Implementation details (bilingual)                              |
| `docs/tutorial/`      | Tutorials (this file lives here)                                |
| `docs/demand/`        | Demand log driving the project                                   |
| `docs/USER_GUIDE.md`  | Complete user guide (English)                                    |
| `docs/USER_GUIDE_zh.md` | Complete user guide (Chinese)                                  |
| `docs/CHANGELOG.md`   | Release history (English)                                        |
| `docs/CHANGELOG_zh.md` | Release history (Chinese)                                       |

## 16.5 CI Quality Gates

`.github/workflows/ci.yml` enforces:

| Gate            | Tool                                          | Description                                                                                              |
|-----------------|-----------------------------------------------|----------------------------------------------------------------------------------------------------------|
| Docs Lint       | `docs_lint.py` / `docs_sync_check.py`         | Path references, core bilingual sync, heading structure, version consistency                             |
| Format          | `clang-format-17`                             | Enforces `.clang-format` (`Standard: c++20`)                                                             |
| Static Analysis | `clang-tidy-17`                               | Runs across all `.cpp` sources under `common/middle/frontends/backends/runtime/tools` (no 50-file cap)   |
| Sanitizers      | ASan + UBSan                                  | Runs all non-benchmark suites (`-LE benchmark`)                                                          |
| Coverage        | `lcov` / `gcov`                               | Line coverage from non-benchmark tests                                                                   |
| Benchmark Smoke | `benchmark_fast`                              | `[fast]` Catch2 subset                                                                                   |
| Binary Matrix   | `scripts/ci/run_binary_matrix.{sh,ps1}`       | `--target` × `--container` matrix on Linux / macOS / Windows                                             |
| Samples Harness | `samples_regression` + `build_all_samples.*`  | Drives `tests/samples/**` and pins stdout via `expected_output.txt`; honours `expected_output.skip`      |

---

*Maintained by the PolyglotCompiler Team*  
*Last Updated: 2026-05-07*  
*Document Version: v3.0.0*
