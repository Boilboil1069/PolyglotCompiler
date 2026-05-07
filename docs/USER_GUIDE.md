# PolyglotCompiler User Guide

> **Document Version**: 4.0.0  
> **Last Updated**: 2026-05-07  
> **Project**: PolyglotCompiler 1.45.2  
> **Companion**: [USER_GUIDE_zh.md](USER_GUIDE_zh.md)

A complete, hands-on guide to PolyglotCompiler — a multi-language compiler
toolchain that ingests C++, Python, Rust, Java, C#/.NET, Go, JavaScript,
Ruby, and the in-house **Ploy** glue language, lowers everything to a
single unified IR, and emits native code for **x86_64**, **ARM64**, and
**WebAssembly**. The companion IDE (`polyui`) ships an LSP-driven
multi-language editor, debugger, profiler, call analyzer, package
manager view, and test explorer.

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [Getting Started](#2-getting-started)
3. [Architecture Overview](#3-architecture-overview)
4. [The Ploy Glue Language](#4-the-ploy-glue-language)
5. [Language Frontends](#5-language-frontends)
6. [Tools and CLI Drivers](#6-tools-and-cli-drivers)
7. [Unified IR](#7-unified-ir)
8. [Middle-end Optimisation](#8-middle-end-optimisation)
9. [Runtime](#9-runtime)
10. [Backends](#10-backends)
11. [Linker and Object Format](#11-linker-and-object-format)
12. [IDE (`polyui`) and Language Server](#12-ide-polyui-and-language-server)
13. [Diagnostics, Profiling, and Call Analysis](#13-diagnostics-profiling-and-call-analysis)
14. [Testing, Samples, and CI](#14-testing-samples-and-ci)
15. [Build System and Dependencies](#15-build-system-and-dependencies)
16. [Extending the Compiler](#16-extending-the-compiler)
17. [Plugin SDK](#17-plugin-sdk)
18. [Appendix](#18-appendix)

---

## 1. Introduction

### 1.1 What PolyglotCompiler is

PolyglotCompiler turns mixed-language source trees into a single linked
artefact. A program may import a C++ image filter, a Rust serialiser,
a Python ML model, and a Go HTTP client, glue them with a `.ploy` driver
file, and produce one executable for x86_64, ARM64, or WebAssembly. The
compiler is built around three guarantees:

1. **One IR for everything.** Every supported language lowers to the
   same SSA, three-address representation. Optimisation, debug info,
   and code generation are written once and reused across frontends.
2. **First-class cross-language calls.** A `bridge_call` IR opcode
   crosses language boundaries through a single FFI marshalling layer
   (`runtime::ffi::BridgeFrame`). The linker verifies that every bridge
   site has a registered counterpart.
3. **Production-ready tooling.** Eleven CLI drivers and a Qt 6 IDE ship
   in the same repository so the end-to-end developer loop (edit →
   compile → debug → profile → package) is exercised by CI on every
   commit.

### 1.2 Capability matrix at a glance

| Domain                     | Status in 1.45.2                                                                                          |
|----------------------------|-----------------------------------------------------------------------------------------------------------|
| Frontends                  | C++, Python, Rust, Java, .NET (C#), Go, JavaScript, Ruby, Ploy — 9 in total.                              |
| Backends                   | x86_64 (System V + Win64 + macOS Mach-O), ARM64 (AAPCS64, Linux ELF + macOS Mach-O), WebAssembly MVP+SIMD. |
| Tool drivers               | `polyc`, `polyld`, `polyasm`, `polyopt`, `polyrt`, `polybench`, `polytopo`, `polyls`, `polydoc`, `polyver`, `polyui` — 11 in total. |
| Garbage collectors         | Mark-and-Sweep, Tri-colour, Generational, Reference-Counting (4 algorithms, runtime-selectable).          |
| Test surface               | 30 CTest targets covering unit, integration, e2e, benchmark, and tooling.                                 |
| Sample suite               | 42 numbered samples (`00_minimal` … `41_grammar_polish`) plus the `01_basic_linking_v2` variant.          |
| IDE                        | Qt 6 polyui with LSP, DAP, Profiler, Call Analyzer, SCM, Test Explorer, Package Manager.                  |
| Bilingual docs             | EN + ZH lock-step under `docs/`, gated by `docs_lint` and `docs_sync` CI targets.                         |

### 1.3 Reading order

Newcomers should run the [Getting Started](#2-getting-started) chapter
end-to-end before dipping into the per-language frontends. Compiler
hackers can jump to [Unified IR](#7-unified-ir) and
[Middle-end Optimisation](#8-middle-end-optimisation). Tooling users
should head straight to chapters 12–14. Plugin authors can skip to
[chapter 17](#17-plugin-sdk).

### 1.4 Conventions

* `polyc`, `polyld`, … in `monospace` always refer to the binaries shipped
  in `build/`.
* All shell snippets assume the repository root as the working directory
  unless noted.
* macOS / Linux paths use `/` separators; Windows snippets use the
  PowerShell `;` and back-tick line continuation.
* `<…>` denotes a placeholder; `[…]` denotes an optional argument.
* Code comments throughout the project are written in English; this
  guide follows the same convention.

### 1.5 Where to find what

| You want to …                                  | Open                                                                                |
|------------------------------------------------|-------------------------------------------------------------------------------------|
| Build the project for the first time           | [§ 2.2](#22-cloning-and-building)                                                   |
| Compile a single C++ file                      | [§ 2.3](#23-first-c-program)                                                        |
| Wire C++ into a Python pipeline                | [§ 2.4](#24-first-cross-language-pipeline)                                          |
| Understand the Ploy grammar                    | [Chapter 4](#4-the-ploy-glue-language) and [tutorial/ploy_language_tutorial.md](tutorial/ploy_language_tutorial.md) |
| Write a new optimisation pass                  | [§ 8.6](#86-writing-a-pass) and [§ 16.4](#164-adding-a-middle-end-pass)              |
| Add a brand-new source language                | [§ 16.5](#165-adding-a-new-frontend) and [§ 17.3](#173-mandatory-exports)            |
| Profile a long-running program                 | [§ 13.3](#133-profiler) and [tutorial/profiling_quickstart.md](tutorial/profiling_quickstart.md) |
| Ship a release build                           | [§ 18.3](#183-release-packaging)                                                    |

---

## 2. Getting Started

### 2.1 Prerequisites

| Platform        | Toolchain                                                                                  |
|-----------------|--------------------------------------------------------------------------------------------|
| macOS 13+       | Xcode 15 Command Line Tools, CMake 3.27+, Ninja 1.11+, Python 3.11+, Qt 6.6+ (`brew install qt`).   |
| Ubuntu 22.04+   | `clang-17` or `gcc-13`, CMake 3.27+, Ninja 1.11+, Python 3.11+, `libssl-dev`, `qt6-base-dev`.       |
| Windows 11      | Visual Studio 2022 (17.8+) or LLVM 17 + Ninja, CMake 3.27+, Python 3.11+, Qt 6.6+ MSVC kit. |

Optional but recommended for full per-frontend coverage:

| Frontend       | Optional toolchain                                                       |
|----------------|--------------------------------------------------------------------------|
| Python         | CPython 3.11+, optionally `conda`, `uv`, or `poetry` for env discovery.  |
| Rust           | Rust 1.78 (`rustup default stable`).                                     |
| Java           | JDK 21 (Temurin, Microsoft Build of OpenJDK, or Liberica).               |
| .NET           | .NET 8 SDK.                                                              |
| Go             | Go 1.22+.                                                                |
| JavaScript     | Node.js 20+, optionally `pnpm` or `yarn`.                                |
| Ruby           | Ruby 3.3, `bundler` for projects with a `Gemfile`.                       |
| Cross compile  | `lld` 17+, `wasi-sdk` 22 for `wasm32-wasi`, target sysroots under `~/sdk`. |

### 2.2 Cloning and building

```sh
git clone https://example.invalid/polyglot/PolyglotCompiler.git
cd PolyglotCompiler
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
```

The first build pulls third-party dependencies declared in
[Dependencies.cmake](Dependencies.cmake) and produces every tool listed
in [section 1.2](#12-capability-matrix-at-a-glance). Expect 4–8 minutes
on a modern laptop with a warm dependency cache.

#### 2.2.1 Useful CMake options

| Option                         | Default              | Notes                                                              |
|--------------------------------|----------------------|--------------------------------------------------------------------|
| `CMAKE_BUILD_TYPE`             | `Debug`              | Use `RelWithDebInfo` for normal development.                       |
| `POLY_BUILD_IDE`               | `ON`                 | Set to `OFF` for headless / CI build pruned of Qt.                 |
| `POLY_BUILD_TESTS`             | `ON`                 | Disable to skip the 30 CTest targets.                              |
| `POLY_BUILD_BENCHMARKS`        | `ON`                 | Disable to skip `benchmark_*` JSON producers.                      |
| `POLY_BUILD_SAMPLES`           | `ON`                 | Disable to skip `samples_smoke`.                                   |
| `POLY_SANITIZE`                | `OFF`                | `address`, `undefined`, `thread`, or `address;undefined`.          |
| `POLY_COVERAGE`                | `OFF`                | Adds `--coverage` and `-fprofile-instr-generate`.                  |
| `POLY_ENABLE_LTO`              | `OFF`                | Build the toolchain itself with thin LTO.                          |
| `POLY_TARGET_TRIPLES`          | host                 | Semicolon list of extra triples for cross-compile defaults.        |
| `POLY_DEFAULT_GC`              | `tricolor`           | `msweep` / `tricolor` / `generational` / `refcount`.               |

#### 2.2.2 Headless / CI build

```sh
cmake -S . -B build-ci -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DPOLY_BUILD_IDE=OFF \
      -DPOLY_BUILD_BENCHMARKS=OFF
cmake --build build-ci -j
ctest --test-dir build-ci -j --output-on-failure
```

#### 2.2.3 Single-target build

Sometimes you only want one tool back:

```sh
cmake --build build --target polyc
cmake --build build --target test_runtime
cmake --build build --target polyui polyls
```

### 2.3 First C++ program

```sh
echo 'int main() { return 42; }' > hello.cpp
build/polyc hello.cpp -o hello
./hello; echo $?     # prints 42
```

`polyc` auto-detects the language from the extension, dispatches to the
C++ frontend, lowers to unified IR, runs the default O2 pipeline, and
emits a native executable for the host triple.

To inspect each stage of the pipeline:

```sh
build/polyc hello.cpp --emit=ir:hello.ir          # textual IR
build/polyc hello.cpp --emit=asm:hello.s          # backend assembly
build/polyc hello.cpp --emit=obj:hello.o          # object file
build/polyc hello.cpp --print-passes              # pass pipeline only
build/polyc hello.cpp -o hello --opt=O0 -g        # debug build
```

### 2.4 First cross-language pipeline

`tests/samples/09_mixed_pipeline/mixed_pipeline.ploy` glues a C++ image
filter and a Python classifier:

```ploy
LINK cpp::filter::sharpen   AS sharpen(image: bytes) -> bytes;
LINK python::ml::classify   AS classify(image: bytes) -> string;

PIPELINE main(path: string) -> string {
    LET raw     = io::read_file(path);
    LET sharper = sharpen(raw);
    RETURN classify(sharper);
}
```

Build and run:

```sh
build/polyc tests/samples/09_mixed_pipeline/mixed_pipeline.ploy \
            -o build/mixed_pipeline
build/mixed_pipeline tests/samples/09_mixed_pipeline/sample.png
```

The driver discovers `*.cpp` and `*.py` siblings via the **package
manager auto-discovery** rules ([§ 4.4](#44-package-manager-auto-discovery)),
invokes the right frontend for each, links the IR modules, and produces
a single binary.

### 2.5 First WebAssembly build

```sh
build/polyc hello.cpp --target=wasm32-wasi -o hello.wasm
wasmtime hello.wasm; echo $?
```

The wasm backend writes a valid WASM 1.0 module with the SIMD-128
opcode extension; see [chapter 10](#10-backends).

### 2.6 First Ploy program from scratch

```ploy
FN factorial(n: i64) -> i64 {
    IF n <= 1 { RETURN 1; }
    RETURN n * factorial(n - 1);
}

FN main() -> i32 {
    PRINT(factorial(10));
    RETURN 0;
}
```

```sh
build/polyc fact.ploy -o fact && ./fact
3628800
```

### 2.7 Running the test suite

```sh
ctest --test-dir build -j --output-on-failure
ctest --test-dir build -R "frontend"
ctest --test-dir build -R "samples_smoke" -V
```

### 2.8 Launching the IDE

```sh
build/polyui &
```

Open `tests/samples/09_mixed_pipeline/` from the welcome page; the LSP
panel logs the `initialize` exchange with `polyls`, `pyright`, and
`clangd`. See [chapter 12](#12-ide-polyui-and-language-server).

---

## 3. Architecture Overview

### 3.1 Top-level pipeline

```
                          ┌─────────────┐
   .cpp .py .rs .java .cs │  Frontends  │   ──►  unified IR (text or binary)
   .go .js .rb .ploy      └─────┬───────┘
                                │
                                ▼
                       ┌────────────────┐
                       │   middle-end   │   pass manager + analyses
                       └────────┬───────┘
                                │
                                ▼
                       ┌────────────────┐
                       │ backend select │   x86_64 / arm64 / wasm
                       └────────┬───────┘
                                │
                                ▼
                       ┌────────────────┐
                       │   polyld link  │   ELF / Mach-O / PE / WASM
                       └────────┬───────┘
                                │
                                ▼
                          executable / library / wasm module
```

### 3.2 Directory layout

| Path                | Contents                                                                  |
|---------------------|---------------------------------------------------------------------------|
| `frontends/`        | One sub-directory per source language; each builds a static lib.          |
| `middle/`           | IR module, pass manager, optimisation passes, verifier.                   |
| `backends/`         | `x86_64/`, `arm64/`, `wasm/`, plus `common/` register allocator and ISel. |
| `runtime/`          | GC, FFI bridge, language runtimes, profiler hooks, debug info.            |
| `tools/`            | The 11 tool drivers (`polyc` … `polyui`).                                 |
| `tests/`            | `unit/`, `integration/`, `e2e/`, `benchmark/`, `samples/` (42 numbered).  |
| `docs/`             | This guide, tutorials, specs, realization notes, and the demand log.     |
| `scripts/`          | Repo automation (docs lint, sync check, packaging).                       |
| `common/`           | Shared utilities (diagnostics, file I/O, JSON, CLI).                      |
| `deps/`             | Vendored or pinned third-party sources.                                  |

### 3.3 Module ownership

Each subsystem owns its public headers under `include/<subsystem>/` and
exports a single CMake target. Cross-module access is restricted to those
public headers; the build fails if a module reaches into another module's
internal layout.

| Subsystem      | Public header root              | CMake target           |
|----------------|---------------------------------|------------------------|
| common         | `common/include/poly/common/`   | `poly_common`          |
| frontends      | `frontends/<lang>/include/`     | `frontend_<lang>`      |
| middle         | `middle/include/poly/middle/`   | `poly_middle`          |
| backends       | `backends/<arch>/include/`      | `backend_<arch>`       |
| runtime        | `runtime/include/poly/runtime/` | `poly_runtime`         |
| tools          | `tools/<name>/include/`         | one executable per dir |

### 3.4 Data flow inside `polyc`

```
argv  →  CLI parser  →  driver
                            │
                            ├─►  source discovery   (per-extension)
                            │       └─► package manager probe (4.3)
                            ├─►  frontend dispatch
                            │       └─► IR module assembly
                            ├─►  middle-end pass manager
                            ├─►  backend lowering
                            ├─►  polyld invocation (in-process)
                            └─►  artefact write
```

### 3.5 Concurrency model

Inside the toolchain:

* Per-frontend lowering runs on the global task scheduler
  (`runtime::tasks::Pool`).
* The pass manager parallelises function-level passes; module-level and
  loop-level passes run serially per module.
* `polyld` parallelises object emission per output section.
* The IDE uses one Qt main thread plus a worker pool that mirrors the
  runtime task scheduler.

### 3.6 Telemetry and observability

Every long-running tool exposes a `--trace=<sink>` flag that emits a
Chrome-trace-compatible JSON stream. The IDE consumes the same stream
in the **Compile Pipeline Inspector** panel. Telemetry is opt-in and
documented in [realization/telemetry_en.md](realization/telemetry_en.md).

---

## 4. The Ploy Glue Language

Full reference: [tutorial/ploy_language_tutorial.md](tutorial/ploy_language_tutorial.md).

### 4.1 Why Ploy

Ploy exists to glue heterogeneous code units. A `.ploy` file declares
imports, type maps, conversions, and pipelines without re-implementing
the host languages. It is intentionally small: the entire grammar fits
in 54 keywords.

### 4.2 Lexical and syntactic anchors

* Statements terminate with `;` (the formal grammar requires it; the
  built-in `polyc --format` enforces it).
* Block delimiters are `{` and `}`.
* Identifiers are ASCII or full UTF-8 (NFC-normalised).
* Comment forms: `// line`, `/* block */`, `/// doc`.
* String literals support extended forms `r"…"`, `b"…"`, `f"…"`,
  `rb"…"`, `f"""…"""` (v1.17).

### 4.3 Core constructs

| Keyword          | Purpose                                                       |
|------------------|---------------------------------------------------------------|
| `LET` / `VAR`    | Immutable / mutable bindings.                                 |
| `FN`             | Function definition.                                          |
| `STRUCT`         | Aggregate types.                                              |
| `OPTION` / `MATCH` | Sum types and exhaustive pattern matching.                  |
| `LINK`           | Import a host symbol with `from="<lang>:<spec>"`.             |
| `IMPORT … PACKAGE` | Pull a host package; supports versions and selective imports. |
| `MAP_TYPE`       | Bidirectional type bridging.                                  |
| `CONVERT`        | User-defined conversion function.                             |
| `PIPELINE`       | Composable data-flow chain.                                   |
| `TRY` / `CATCH`  | Structured error handling (v1.13).                            |
| `ASYNC` / `AWAIT`| Asynchronous functions and await points (v1.14).              |
| `GENERIC`        | Constrained generics (v1.15).                                 |
| `PUBLIC` / `PRIVATE` / `INTERNAL` | Visibility (v1.16).                          |
| `CLASS` / `NEW`  | Bridge to host-language class instantiation (v1.20).          |

The full keyword list (54 entries) lives in section 23 of the language
tutorial.

### 4.4 Package manager auto-discovery

When `polyc` is invoked with a `.ploy` driver it walks sibling
directories and recognises the manifests below, then asks the matching
frontend to ingest the project:

| Manifest                              | Frontend     | Notes                                      |
|---------------------------------------|--------------|--------------------------------------------|
| `CMakeLists.txt`                      | C++          | Uses configure-time export of compile flags. |
| `pyproject.toml`, `requirements.txt`  | Python       | Conda / uv / poetry / venv all detected.   |
| `Cargo.toml`                          | Rust         | Workspaces honoured.                       |
| `pom.xml`, `build.gradle[.kts]`       | Java         | Maven and Gradle outputs used as classpath. |
| `*.csproj`, `*.sln`                   | .NET         | NuGet restore is delegated to `dotnet`.    |
| `go.mod`                              | Go           | `GOPATH`-free modules only.                |
| `package.json`                        | JavaScript   | npm / pnpm / yarn lockfiles used for resolution. |
| `Gemfile`                             | Ruby         | Bundler resolves; `ruby` for the runtime.  |
| `.ploy.toml`                          | Ploy         | Project-level Ploy settings.               |

### 4.5 IMPORT package syntax

```ploy
IMPORT python PACKAGE numpy >= 1.20;                  // version constraint
IMPORT python PACKAGE numpy::(array, mean);           // selective import
IMPORT python PACKAGE numpy >= 1.20 AS np;            // alias the package
IMPORT rust   PACKAGE serde::(Serialize, Deserialize);
IMPORT cpp    PACKAGE eigen >= 3.4;
IMPORT java   PACKAGE org.json::(JSONObject) AS json;
```

A package alias may not coexist with a selective-import list whose
target is ambiguous. The IDE flags such cases at edit time with
diagnostic id `polyc-err-E0612`.

### 4.6 Cross-language calls

```ploy
LINK cpp::graphics::draw_point  AS draw(p: ptr<u8>) -> void;
LINK python::numpy::mean        AS mean(xs: list<f64>) -> f64;
LINK rust::serde_json::to_string AS to_json<T>(value: T) -> string;

FN demo() -> void {
    LET avg = mean([1.0, 2.0, 3.0, 4.0]);
    PRINT(avg);
}
```

The `LINK` form lowers to a `bridge_call` IR opcode that the linker
verifies. Type marshalling rules live in
[realization/bridge_marshalling.md](realization/bridge_marshalling.md).

### 4.7 Class instantiation across languages

```ploy
LINK cpp::geometry::Point AS Point CLASS {
    NEW(x: f64, y: f64) -> Point;
    FN  norm(self: Point) -> f64;
};

FN distance() -> f64 {
    LET a = NEW Point(0.0, 0.0);
    LET b = NEW Point(3.0, 4.0);
    RETURN b.norm() - a.norm();
}
```

`CLASS` blocks declare the bridged shape; each method is lowered to a
`bridge_call` whose first argument is the boxed receiver.

### 4.8 Compilation model

```
.ploy → ploy frontend ──┐
.cpp  → cpp  frontend ──┤
.py   → py   frontend ──┼──► unified IR ──► middle-end ──► backend ──► polyld ──► artefact
.rs   → rust frontend ──┘
```

Cross-language calls are lowered to **bridge stubs** that marshal
arguments through the runtime's FFI layer; the call graph emitted by
`polyc --emit=call-graph` distinguishes bridge edges so the IDE can
render them in a contrasting colour.

### 4.9 Diagnostic identifiers

Ploy diagnostics share the catalogue with every other frontend; ids are
formatted `polyc-(err|warn)-<E####|W####>`. Common entries:

| Id            | Meaning                                                |
|---------------|--------------------------------------------------------|
| `polyc-err-E0101` | Unexpected token / parse failure.                  |
| `polyc-err-E0210` | Type mismatch in `LINK` signature.                 |
| `polyc-err-E0311` | Unknown package in `IMPORT … PACKAGE`.             |
| `polyc-err-E0405` | Async function awaited outside `ASYNC` context.    |
| `polyc-err-E0612` | Ambiguous selective import + alias combination.    |
| `polyc-warn-W0701`| Unused `LINK` declaration.                         |
| `polyc-warn-W0903`| Bridge call without matching counterpart symbol.   |

The full catalogue is at [specs/ploy_diagnostics.md](specs/ploy_diagnostics.md).

---

## 5. Language Frontends

Each frontend is a static library under `frontends/<lang>/`, exposes a
single `Lower(Source) → IRModule` entry point, and owns its language-
specific diagnostics that map to the unified `polyc-(err|warn)-E####`
identifiers.

| Frontend       | Lib target          | Test target                | Highlights                                                            |
|----------------|---------------------|----------------------------|-----------------------------------------------------------------------|
| `frontend_cpp` | `frontend_cpp`      | `test_frontend_cpp`        | C++20 subset, templates with constraint folding, RTTI off by default. |
| `frontend_python` | `frontend_python`| `test_frontend_python`     | Python 3.11 subset, type hints lowered to IR types where possible.    |
| `frontend_rust`| `frontend_rust`     | `test_frontend_rust`       | Rust 2024 subset, ownership lowered to scope-bounded RC handles.      |
| `frontend_java`| `frontend_java`     | `test_frontend_java`       | Java 21 subset, sealed classes and records.                           |
| `frontend_dotnet` | `frontend_dotnet`| `test_frontend_dotnet`    | C# 12 subset, value-type structs, `async`/`await` lowered to coroutines. |
| `frontend_go`  | `frontend_go`       | `test_frontend_go`         | Go 1.22 subset, goroutines lowered onto the runtime's task scheduler. |
| `frontend_javascript` | `frontend_javascript` | `test_frontend_javascript` | ES2023 subset, NaN-tagged values, optional Number → i64 narrowing. |
| `frontend_ruby`| `frontend_ruby`     | `test_frontend_ruby`       | Ruby 3.3 subset, blocks lowered to closures.                          |
| `frontend_ploy`| `frontend_ploy`     | `test_frontend_ploy`       | Glue language; full grammar, generics, async, error handling.         |
| common         | `frontend_common`   | `test_frontend_common`     | Shared diagnostic, source-map, and lookup machinery.                  |

A frontend is "complete" when it (a) parses the documented subset,
(b) lowers to IR that the verifier accepts, (c) emits diagnostics in the
catalogue, and (d) passes its `test_frontend_<lang>` target.

### 5.1 C++ frontend

Supports C++20 with a few practical restrictions:

* `<thread>` and `<atomic>` lower to runtime task primitives.
* RTTI is off by default; enable per-translation-unit with
  `// poly: rtti on`.
* Exception unwinding uses the platform-native unwinder; on wasm a JS
  trap path is used.
* Templates are instantiated lazily; constraint folding runs before
  instantiation.

Example C++ that becomes part of a `.ploy` pipeline:

```cpp
// frontends/cpp/examples/sharpen.cpp
#include <vector>
#include <cstdint>

extern "C" std::vector<uint8_t> sharpen(const std::vector<uint8_t> &raw) {
    // Apply a 3x3 sharpening kernel; returns a new buffer.
    std::vector<uint8_t> out(raw.size());
    // ... kernel implementation ...
    return out;
}
```

### 5.2 Python frontend

Supports Python 3.11 syntax. Type hints are honoured where they yield
unambiguous IR types; un-annotated parameters lower to `box<value>`.

```python
# frontends/python/examples/classify.py
from __future__ import annotations
import numpy as np

def classify(image: bytes) -> str:
    arr = np.frombuffer(image, dtype=np.uint8)
    return "cat" if arr.mean() > 127 else "dog"
```

The frontend resolves `numpy` via the package-manager probe; the
matching `IMPORT python PACKAGE numpy` declaration in the Ploy driver
is what makes the import valid at link time.

### 5.3 Rust frontend

Rust 2024 subset, ownership lowered to scope-bounded RC handles so that
the IR verifier can model lifetimes uniformly across languages.
`Result<T, E>` lowers to `option<variant<T, E>>`.

### 5.4 Java frontend

Java 21 subset; sealed classes and records lower directly to IR
`variant`/`struct`. Generics use type erasure plus a parallel reified
table for cross-language calls.

### 5.5 .NET frontend

C# 12 subset; value types stay as IR `struct`. `async`/`await` lower to
the runtime's coroutine machinery. Nullable reference types are
honoured.

### 5.6 Go frontend

Go 1.22 subset; goroutines schedule onto the runtime task pool, channels
lower to runtime primitives. Build tags are supported via comment
directives.

### 5.7 JavaScript frontend

ES2023 subset; values are NaN-tagged when type inference cannot prove a
narrower type. Number → i64 narrowing happens when type-feedback or
explicit `| 0` patterns are present.

### 5.8 Ruby frontend

Ruby 3.3 subset; blocks lower to closures, common DSL patterns
(`define_method`, `attr_accessor`) are recognised and folded during
lowering.

### 5.9 Ploy frontend

The reference frontend; entire grammar in 54 keywords. Drives package
discovery, bridge declaration, conversions, pipelines, async, and
generics. The same frontend powers `polyc --format`.

### 5.10 Frontend authoring rules

* Public lowering API must accept a `SourceBuffer` and return an
  `IRModule`. No global state.
* Diagnostics use the shared `Diagnostic` type from `frontend_common`.
* Each frontend ships a `tests/unit/frontend_<lang>/` target.
* Bridge stub registration goes through `runtime::ffi::Registry`.

---

## 6. Tools and CLI Drivers

### 6.1 `polyc` — compiler driver

```
polyc [options] <inputs…> [-o <output>]
  --target=<triple>            x86_64 / aarch64 / wasm32 + os/abi
  --emit=<kind>[:<path>]       ir, asm, obj, call-graph, profile-symbols
  --opt=<level>                O0 | O1 | O2 | O3 | Os | Oz
  --check                      run frontend + verifier only, emit JSON diagnostics
  --profile-instrument         insert __ploy_rt_call_enter/exit hooks
  --pgo=<mode>                 instrument | use=<profdata>
  --lto=<mode>                 thin | full
  --gc=<algo>                  msweep | tricolor | generational | refcount
  --link=<spec>                forward to polyld
  --print-passes               dump pass pipeline and exit
  --format                     run the built-in formatter on inputs
  --jobs=<N>                   parallel frontend jobs (default: hardware threads)
  --trace=<path>               write a Chrome-trace JSON for the whole run
```

Examples:

```sh
polyc main.ploy -o main                                          # default O2
polyc main.ploy --opt=O3 --lto=thin -o main                      # release
polyc main.ploy --target=aarch64-apple-darwin -o main.arm64
polyc --check main.ploy | jq '.diagnostics[].message'
polyc --emit=ir:main.ir --emit=asm:main.s main.ploy
polyc --pgo=instrument main.ploy -o main_inst
polyc --pgo=use=main.profdata main.ploy -o main_pgo
```

Exit codes:

| Code | Meaning                                       |
|------|-----------------------------------------------|
| 0    | Success.                                      |
| 1    | Errors emitted; no artefact written.          |
| 2    | Usage / I/O failure.                          |
| 3    | Internal compiler error (with stack trace).   |

### 6.2 `polyld` — linker

`polyld` consumes IR and native object inputs, resolves symbols across
language boundaries, and writes ELF / Mach-O / PE / WASM containers.

```
polyld [options] <inputs…> -o <output>
  --target=<triple>     output triple
  --shared              produce a shared library
  --static              produce a static archive
  --entry=<symbol>      override entry point
  --map=<path>          write a link map
  --gc-sections         drop unreferenced sections
  --lto=<mode>          thin | full
  --bridge-table=<path> emit a JSON bridge call table
  --verify              re-run IR verifier after dead-code stripping
```

The macOS arm64 emitter aligns segments to 16 KiB and stamps
`__DATA_CONST` with the `SG_READ_ONLY` flag (`0x10`) for dyld 26
chained-fixup compatibility.

### 6.3 `polyasm` — assembler / disassembler

Implements the full backend ISA tables. Used standalone for hand-written
fixtures, and by the IDE's binary inspector to render disassembly.

```
polyasm <input>                  disassemble (auto-detect format)
polyasm --asm    <input>         emit textual assembly
polyasm --obj    <input>         emit relocatable object
polyasm --ir     <input>         round-trip text IR ↔ binary IR
polyasm --target=<triple>        override host triple
polyasm --print-isa              dump ISA tables for the current target
```

### 6.4 `polyopt` — IR optimiser

Stand-alone driver around the middle-end pass manager. Handy for
`opt`-style experiments:

```sh
polyopt -O3 -print-after-all in.ir
polyopt -passes='mem2reg,instcombine,gvn' in.ir -o out.ir
polyopt -opt-bisect-limit=42 -print-pass-list in.ir
polyopt -analyse=dominators -print-analysis in.ir
```

### 6.5 `polyrt` — runtime CLI

Front-door for runtime services consumed without the IDE:

```
polyrt profile   --json out.json --duration-ms 5000 <program>
polyrt profile   --stream out.ndjson <program>
polyrt calltrace --json calltrace.json
polyrt gc-stats  --algo=tricolor <program>
polyrt heap-snapshot --out heap.json <program>
polyrt schedule-stats --interval-ms 100 <program>
```

### 6.6 `polybench` — benchmark harness

Drives the eight benchmark suites and writes JSON reports
(`build/benchmark_*.json`) that the dashboard renders.

```
polybench --suite=compilation   --out build/benchmark_compilation.json
polybench --suite=e2e           --out build/benchmark_e2e.json
polybench --suite=gc            --out build/benchmark_gc.json
polybench --suite=link_times    --out build/benchmark_link_times.json
polybench --suite=optimizations --out build/benchmark_optimizations.json
polybench --suite=comparison    --out build/benchmark_comparison.json
polybench --all                 # runs every suite
polybench --baseline=<path>     # compare against a baseline JSON
```

### 6.7 `polytopo` — call-graph topology viewer

Headless counterpart to the Call Analyzer panel:

```
polytopo build/mixed.cgjson                   # ASCII summary
polytopo --view-mode=dot  build/mixed.cgjson  # Graphviz DOT
polytopo --view-mode=json build/mixed.cgjson  # machine-readable
polytopo --filter-language=python build/mixed.cgjson
polytopo --filter-bridge build/mixed.cgjson   # bridge edges only
polytopo --root=main --depth=4 build/mixed.cgjson
polytopo build/mixed.cgjson | dot -Tsvg > mixed.svg
```

### 6.8 `polyls` — language server

LSP 3.17 stdio server consumed by `polyui` and any third-party editor.

```
polyls                                  # speak LSP on stdio
polyls --log polyls.log                 # log every frame
polyls --capabilities                   # print supported capabilities
polyls --root=<path>                    # workspace root override
```

See [tutorial/lsp_quickstart.md](tutorial/lsp_quickstart.md) for the
end-to-end walk-through.

### 6.9 `polydoc` — documentation extractor

Parses `///` doc comments out of every supported frontend and writes
HTML / Markdown bundles. Powers the in-IDE hover summaries.

```
polydoc --in src/ --out docs/api/   --format=md
polydoc --in src/ --out docs/api/   --format=html
polydoc --check  src/               # lint comment quality
```

### 6.10 `polyver` — version & manifest tool

Reports compiler, runtime, and bridge ABI versions; verifies a build
manifest against the on-disk artefacts.

```
polyver --version
polyver --abi
polyver --check-manifest build/manifest.json
polyver --emit-manifest  build/manifest.json
```

### 6.11 `polyui` — IDE shell

Qt 6 desktop application that hosts every interactive panel: editor,
LSP, DAP, Profiler, Call Analyzer, Test Explorer, SCM, Package Manager,
Notifications, and Bookmarks.

```
polyui                              # launch with last session
polyui <workspace>                  # launch on a specific workspace
polyui --reset-settings             # discard user settings
polyui --safe-mode                  # skip plugins and last session
```

See [chapter 12](#12-ide-polyui-and-language-server).

---

## 7. Unified IR

### 7.1 Overview

The IR is a typed, SSA, three-address form with explicit control-flow
edges. It supports two serialisations: a textual `.ir` round-trippable
through `polyasm --ir`, and a binary `.ir.bin` consumed by the
incremental cache.

### 7.2 Type system

| Category   | Types                                                           |
|------------|-----------------------------------------------------------------|
| Integers   | `i1`, `i8`, `i16`, `i32`, `i64`, `i128`, signed/unsigned variants. |
| Floats     | `f16`, `f32`, `f64`, `f128`.                                    |
| Vectors    | `<N x T>` SIMD vectors (used by the wasm SIMD backend and the x86_64 AVX path). |
| Aggregates | `struct {…}`, `array<T, N>`, `tuple<…>`.                        |
| References | `ptr<T, addrspace>`, `ref<T>` (GC-tracked), `weak<T>`.          |
| Sums       | `option<T>`, `variant<T0, T1, …>`.                              |
| Functions  | `fn(args…) -> T`, `async fn(args…) -> T`.                       |
| Boxed      | `box<T>` for cross-language value passing.                      |

### 7.3 Core instruction set

| Family       | Examples                                                          |
|--------------|-------------------------------------------------------------------|
| Arithmetic   | `add`, `sub`, `mul`, `sdiv/udiv`, `srem/urem`, `fadd…`             |
| Bitwise      | `and`, `or`, `xor`, `shl`, `lshr`, `ashr`, `popcnt`, `clz`         |
| Memory       | `alloc`, `load`, `store`, `gep`, `memcpy`, `memset`                |
| Control      | `br`, `cbr`, `switch`, `ret`, `unreachable`, `landingpad`          |
| Calls        | `call`, `tailcall`, `invoke`, `bridge_call` (cross-language)       |
| GC           | `gc.alloc`, `gc.barrier.write`, `gc.safepoint`                     |
| Async        | `async.suspend`, `async.resume`, `async.await`                     |
| SIMD         | `vec.add`, `vec.mul`, `vec.shuffle`, `vec.broadcast`               |
| Conversion   | `trunc`, `sext`, `zext`, `bitcast`, `fptosi`, `sitofp`             |

### 7.4 Textual form

```
fn main() -> i32 {
entry:
    %0 = call @factorial(i64 10)
    %1 = call @print_i64(i64 %0)
    ret i32 0
}

fn factorial(i64 %n) -> i64 {
entry:
    %cond = icmp.sle i64 %n, 1
    cbr i1 %cond, base, recurse
base:
    ret i64 1
recurse:
    %n1   = sub i64 %n, 1
    %r    = call @factorial(i64 %n1)
    %res  = mul i64 %n, %r
    ret i64 %res
}
```

### 7.5 Calling conventions

* **System V AMD64** for Linux/macOS x86_64.
* **Win64** for Windows x86_64.
* **AAPCS64** for Linux/macOS aarch64.
* **wasm-32** with multi-value returns.
* **Bridge ABI** for cross-language calls — values are marshalled via
  `runtime::ffi::BridgeFrame` so that callee-language runtimes see
  conventional values.

### 7.6 Verifier rules

The verifier (`middle/verifier`) rejects any IR module that:

* uses an SSA value before its definition,
* references an undeclared block or function,
* mixes pointer address spaces in a single `gep`,
* calls across languages without a `bridge_call`,
* skips a `gc.safepoint` between two GC allocations on the same path,
* leaves an `async.suspend` outside an `async fn`.

A failing module aborts compilation with `polyc-err-E0801`.

### 7.7 Binary serialisation

`.ir.bin` uses a length-prefixed, content-addressed format:

```
magic:    "POLI"  (4 bytes)
version:  u32     (current = 4)
strtab:   compressed string pool
typetab:  type interning table
fns:      one record per function (header + blocks + insts)
metadata: source map + DI + bridge index
crc32:    over everything above
```

The format is forward-compatible: older `polyc` versions reject newer
modules with `polyc-err-E0810` rather than mis-decoding them.

---

## 8. Middle-end Optimisation

### 8.1 Pass manager

The pass manager is a dependency-aware scheduler with three pass kinds:
**Module**, **Function**, and **Loop**. Analyses cache results across
passes and invalidate when their preserved set is broken.

### 8.2 Default `-O2` pipeline

`mem2reg → instcombine → simplifycfg → gvn → licm → loop-unroll →
inliner → tailcall → dse → adce → late-instcombine → simplifycfg`.

`-O3` adds `loop-vectorize`, `slp-vectorize`, and a second inliner pass.
`-Os/-Oz` swap the inliner for a size-aware variant and enable
`merge-functions` and `cold-cc`.

### 8.3 Pass catalogue

| Pass               | Kind     | Notes                                                  |
|--------------------|----------|--------------------------------------------------------|
| `mem2reg`          | Function | Promote allocas to SSA registers.                      |
| `instcombine`      | Function | Peephole simplifications.                              |
| `simplifycfg`      | Function | Merge basic blocks, fold trivial branches.             |
| `gvn`              | Function | Global value numbering with PRE.                       |
| `licm`             | Loop     | Loop invariant code motion.                            |
| `loop-unroll`      | Loop     | Heuristic + pragma unrolling.                          |
| `loop-vectorize`   | Loop     | SLP-aware vectoriser; uses `<N x T>` IR types.         |
| `inliner`          | Module   | Cost-modelled inliner; PGO-aware.                      |
| `tailcall`         | Function | Mark eligible calls; backend lowers.                   |
| `dse`              | Function | Dead store elimination.                                |
| `adce`             | Function | Aggressive dead code elimination.                      |
| `merge-functions`  | Module   | Merge identical function bodies.                       |
| `cold-cc`          | Module   | Apply cold calling convention to rare paths.           |
| `bridge-fuse`      | Module   | Fuse adjacent `bridge_call` ops on same target lang.   |
| `gc-strip`         | Module   | Drop GC barriers proven unnecessary.                   |

### 8.4 PGO

```sh
polyc --pgo=instrument hello.cpp -o hello_inst
./hello_inst   # writes default.profraw
llvm-profdata merge -o default.profdata default.profraw
polyc --pgo=use=default.profdata hello.cpp -o hello_pgo
```

Profile data is consumed by the inliner, branch-probability analyser,
and the wasm backend's hot-cold splitter.

### 8.5 LTO

`polyc --lto=thin` and `--lto=full` switch the link to whole-program
optimisation. Thin LTO keeps per-module summaries and parallelises
codegen.

### 8.6 Writing a pass

```cpp
// middle/passes/example_pass.cpp
#include "poly/middle/pass.h"

namespace poly::middle {

class ExamplePass : public FunctionPass {
public:
    static constexpr std::string_view ID = "example";
    bool Run(Function &fn, FunctionAnalyses &analyses) override {
        bool changed = false;
        for (auto &block : fn) {
            for (auto it = block.begin(); it != block.end(); ) {
                if (IsRedundant(*it)) {
                    it = block.Erase(it);
                    changed = true;
                } else {
                    ++it;
                }
            }
        }
        return changed;
    }
    void GetAnalysisUsage(AnalysisUsage &au) const override {
        au.AddRequired<DominatorTree>();
        au.SetPreservesCFG();
    }
};

REGISTER_PASS(ExamplePass);

}  // namespace poly::middle
```

Then add the pass id to the desired pipeline level in
`middle/pipeline_defaults.cpp`. Document the pass in
[realization/middle_passes.md](realization/middle_passes.md).

### 8.7 Inspecting the pipeline

```
polyopt -print-passes              # list pass IDs
polyopt -print-after=instcombine   # dump IR after a pass
polyopt -opt-bisect-limit=42       # binary-search regressions
polyopt -time-passes               # per-pass wall time
```

---

## 9. Runtime

### 9.1 Garbage collectors

| Algorithm        | Header                                | Notes                                       |
|------------------|---------------------------------------|---------------------------------------------|
| Mark-and-Sweep   | `runtime/gc/mark_sweep.h`             | Stop-the-world, low memory overhead.        |
| Tri-colour       | `runtime/gc/tricolor.h`               | Default; concurrent marking; lower pauses.  |
| Generational     | `runtime/gc/generational.h`           | Young/old split with write barriers.        |
| Reference-count  | `runtime/gc/refcount.h`               | Cycle collector for reference cycles.       |

Selectable per build (`--gc=<algo>`) or per program by setting
`POLY_GC` at runtime. Stats: `polyrt gc-stats`.

#### 9.1.1 Tuning knobs

| Env var               | Meaning                                                |
|-----------------------|--------------------------------------------------------|
| `POLY_GC`             | Select algorithm at process start.                     |
| `POLY_GC_HEAP_INIT`   | Initial heap size (e.g. `64m`).                        |
| `POLY_GC_HEAP_MAX`    | Hard limit; OOM beyond.                                |
| `POLY_GC_PAUSE_GOAL_MS` | Pause-time goal for tri-colour and generational.    |
| `POLY_GC_LOG`         | `none` / `summary` / `verbose`.                       |
| `POLY_GC_TRACE`       | Path to a Chrome-trace JSON of GC events.              |

### 9.2 FFI bridge

`runtime/ffi/` implements `BridgeFrame`, the universal marshalling
record used by all cross-language calls. Each language runtime registers
boxer / unboxer pairs for its native value representations.

```cpp
// runtime/ffi/bridge_frame.h
struct BridgeFrame {
    LanguageId   from;
    LanguageId   to;
    Span<Value>  args;
    Value        result;
    Diagnostic  *diag;   // out parameter
};
```

A bridged call site looks like:

```
%boxed = bridge_call @python::ml::classify, box<bytes> %img
```

### 9.3 Language runtimes

`runtime/lang/{cpp,py,rust,java,dotnet,go,js,ruby,ploy}/` provide the
minimum machinery each frontend needs at run time: exception unwinder,
async scheduler, value boxing, intrinsic helpers. Each runtime exposes
an `Init(Host *host)` and `Shutdown()` pair invoked from the main
program prologue / epilogue.

### 9.4 Services

* **Profiler hooks** (`__ploy_rt_call_enter/exit`) — toggled with
  `__ploy_rt_call_trace_enable`.
* **Task scheduler** — work-stealing pool consumed by `async`/`await`,
  Go-frontend goroutines, and parallel passes. Configurable via
  `POLY_TASKS_THREADS`.
* **Telemetry** — opt-in counters routed through `polytelemetry`; off by
  default. See [realization/telemetry_en.md](realization/telemetry_en.md).
* **stdout pipeline** — multiplexed stdout/stderr capture for the IDE
  console; see
  [realization/runtime_stdout_pipeline.md](realization/runtime_stdout_pipeline.md).

### 9.5 Debug information

DWARF 5 on ELF/Mach-O, CodeView on PE, and the wasm "name" + DWARF
sections on WebAssembly. Source maps cover Ploy line directives so the
debugger steps in the original `.ploy` file even when execution is
inside a host-language frame.

### 9.6 Exceptions

Each language runtime implements `Throw(Value)` and `Catch(Type)`
operations expressed in IR by `landingpad` and `invoke`. Cross-language
exceptions propagate as boxed `Value`s; the receiving runtime decides
whether to rethrow natively or surface as an error result.

---

## 10. Backends

### 10.1 x86_64

Targets System V AMD64 (Linux + macOS) and Win64 (Windows). Selects AVX2
opportunistically, AVX-512 only with `-mavx512f`. Uses the shared
common register allocator and instruction selector.

#### 10.1.1 Register classes

| Class | Registers                                                          |
|-------|--------------------------------------------------------------------|
| GPR   | rax, rcx, rdx, rbx, rsi, rdi, rsp, rbp, r8…r15.                    |
| XMM   | xmm0…xmm15 (xmm16…xmm31 with AVX-512).                             |
| K     | k0…k7 (mask registers, AVX-512).                                   |

### 10.2 ARM64

AAPCS64 ABI, Linux ELF and macOS Mach-O containers. macOS Mach-O
objects are emitted with 16 KiB segment alignment, and `__DATA_CONST`
is stamped with `SG_READ_ONLY (0x10)` for dyld 26 chained-fixup
compatibility — without this flag the loader rejects the binary on
macOS 26 arm64 hosts.

#### 10.2.1 Register classes

| Class | Registers              |
|-------|------------------------|
| X     | x0…x30, sp.            |
| V     | v0…v31 (NEON).         |

### 10.3 WebAssembly

Targets the MVP plus the SIMD-128, bulk memory, and reference-types
proposals. Emits valid WASM 1.0 modules and a paired `.wasm.map` source
map. Compatible with `wasmtime`, `wasmer`, and browsers.

Feature flags:

| Flag             | Effect                                                     |
|------------------|------------------------------------------------------------|
| `--simd`         | Emit SIMD-128 opcodes.                                     |
| `--threads`      | Emit shared-memory + atomics.                              |
| `--bulk-memory`  | Emit `memory.copy` / `memory.fill`.                        |
| `--reference-types` | Emit `externref` and table.grow.                        |
| `--gc`           | Emit the GC proposal opcodes (preview).                    |

### 10.4 Cross-compilation

```sh
polyc hello.cpp --target=aarch64-linux-gnu      -o hello.aarch64
polyc hello.cpp --target=x86_64-pc-windows-msvc -o hello.exe
polyc hello.cpp --target=wasm32-wasi            -o hello.wasm
polyc hello.cpp --target=aarch64-apple-darwin   -o hello.macho
```

A sysroot must be configured for non-host triples; see
[realization/cross_compilation.md](realization/cross_compilation.md).

---

## 11. Linker and Object Format

### 11.1 Containers

| Container | Platforms                  | Notes                                                              |
|-----------|----------------------------|--------------------------------------------------------------------|
| ELF       | Linux x86_64 / aarch64     | Standard relocations + GNU-style `.note.GNU-stack`.                |
| Mach-O    | macOS x86_64 / arm64       | 16 KiB alignment on arm64; `SG_READ_ONLY` on `__DATA_CONST`.       |
| PE/COFF   | Windows x86_64             | Validated by the `pe_smoke` target; `/SUBSYSTEM:CONSOLE` default.  |
| WASM      | wasm32-wasi / wasm32-unknown | Imports separated by module name; SIMD opcodes gated on `--simd`. |

### 11.2 Symbol resolution

`polyld` keeps a single per-link symbol table keyed by mangled name and
language. Cross-language calls go through the bridge ABI; the linker
verifies that every `bridge_call` has a registered counterpart in the
target language's runtime.

Mangling scheme:

```
_PLY<lang>_<module>_<func>_<argtypes>
```

Where `<lang>` is `c`, `p`, `r`, `j`, `n`, `g`, `s`, `b`, `y` for the
nine supported languages. The full grammar is in
[specs/symbol_mangling.md](specs/symbol_mangling.md).

### 11.3 Incremental cache

`build/.cache/` stores `.ir.bin` and `.obj` keyed by content hash. The
default policy reuses cached entries when the source hash, target
triple, optimisation level, and pass pipeline all match. Eviction is
LRU bounded by `POLY_CACHE_MAX_BYTES` (default 4 GiB).

### 11.4 Bridge call table

`polyld --bridge-table=<path>` writes a JSON manifest that lists every
bridge call site, its source language, target language, and target
symbol. The IDE's Call Analyzer panel ingests the same file.

### 11.5 Linker scripts

`polyld --script=<path>` honours a small linker-script subset:

```
SECTIONS {
    .text   : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }
    .bss    : { *(.bss*) }
}
```

---

## 12. IDE (`polyui`) and Language Server

### 12.1 Components

* `polyui` — Qt 6 shell.
* `polyls` — stdio LSP server for Ploy plus dispatcher for third-party
  servers per language.
* `IdeLspBridge` — IDE-side adapter that translates editor events into
  LSP messages, debounced at 200 ms.
* `IdeDapBridge` — DAP adapter for `lldb-dap`, `debugpy`, `delve`,
  `netcoredbg`, and node-debug2.

### 12.2 Default LSP server map

| Language     | Default `command`                       |
|--------------|-----------------------------------------|
| ploy         | `polyls`                                |
| cpp          | `clangd`                                |
| python       | `pyright-langserver --stdio`            |
| rust         | `rust-analyzer`                         |
| java         | `jdtls`                                 |
| dotnet       | `csharp-ls`                             |
| go           | `gopls`                                 |
| javascript   | `typescript-language-server --stdio`    |
| ruby         | `solargraph stdio`                      |

Override under **Settings → Language Servers**.

### 12.3 Workflow inside `polyui`

1. Open a file → `IdeLspBridge` spawns the matching server and sends
   `initialize` → `initialized` → `didOpen`.
2. Edits within a 200 ms debounce window are coalesced into a single
   `didChange`.
3. Inbound `publishDiagnostics` is rendered in the gutter and the
   Problems panel.
4. `Ctrl+Click` → `textDocument/definition`; `Shift+F12` → `references`;
   `F2` → `rename`.
5. On exit the IDE sends `shutdown` + `exit` to every active server.

### 12.4 Inspecting traffic

The bottom **LSP** dock (toggle with `Ctrl+Alt+L`) logs every message
in both directions. `polyls --log polyls.log` captures the same frames
to disk.

### 12.5 Panel overview

| Panel                | Toggle           | Purpose                                       |
|----------------------|------------------|-----------------------------------------------|
| Problems             | `Ctrl+Shift+M`   | Aggregated diagnostics across all servers.    |
| Profiler             | `Ctrl+Alt+P`     | Flame, hotspots, timeline, languages.         |
| Call Analyzer        | `Ctrl+Alt+G`     | Static call graph + runtime overlay.          |
| Test Explorer        | `Ctrl+Alt+T`     | CTest tree + inline run/debug.                |
| Package Manager      | `Ctrl+Alt+M`     | Per-language dependency graph & vuln scan.    |
| Notifications        | bell icon        | Persistent IDE notifications.                 |
| Bookmarks            | `Ctrl+Alt+K`     | Per-line bookmarks across the workspace.      |
| TODO / FIXME index   | `Ctrl+Alt+J`     | Background-scanned tag index.                 |
| Compile Pipeline     | `Ctrl+Alt+I`     | Chrome-trace view of `polyc` runs.            |
| IR Viewer            | `Ctrl+Alt+R`     | Side-by-side textual IR + diff.               |
| Asm Viewer           | `Ctrl+Alt+A`     | Backend assembly with source mapping.         |
| Topology Live        | `Ctrl+Alt+Y`     | Live sample topology panel.                   |
| Bridge Panel         | `Ctrl+Alt+B`     | Cross-language bridge call inspector.         |
| Marshalling View     | `Ctrl+Alt+W`     | FFI value marshalling step-through.           |
| Test Coverage        | `Ctrl+Alt+V`     | Coverage overlay on the editor gutter.        |

### 12.6 Editor features

* Multi-cursor, column selection, multi-tab editor.
* Quick Open (`Ctrl+P`), Symbol Search (`Ctrl+T`), global text search
  (`Ctrl+Shift+F`).
* Folding, formatting (`Shift+Alt+F`), EditorConfig support.
* Semantic syntax highlighting from each LSP server.
* Inline diagnostics, code actions (`Ctrl+.`), refactoring (rename,
  extract, inline).
* Bracket pair colourisation.

### 12.7 SCM

Built-in git client: diff view, blame gutter, three-way merge resolver,
PR review (GitHub + GitLab). Authentication is delegated to the OS
keychain.

### 12.8 Debugging (DAP)

`F5` launches the active Run/Debug profile; the DAP bridge selects the
adapter from the file's language. Breakpoints, watch expressions, step
in/out, conditional breakpoints, hot reload (Python, .NET, JS, Java),
and inline value display are all supported.

### 12.9 Tasks and Run/Debug picker

`Ctrl+Shift+B` picks a build task; `F5` picks a debug profile.
Profiles live in `.polyui/launch.json` and follow the VS Code DAP
shape so existing snippets are reusable.

### 12.10 Test Explorer

Tree view of every CTest target plus per-language test discoverers
(pytest, gtest, junit, xunit, mocha, rspec). Inline run / debug,
coverage overlay, history. See
[realization/test_explorer_en.md](realization/test_explorer_en.md).

### 12.11 Package Manager view

Per-language dependency graph plus a vulnerability scan that consumes
OSV data. Updates and lock-file edits are surfaced as code actions.

### 12.12 Cross-language navigation

`Ctrl+Click` on a `LINK` target jumps to the host-language source. The
**Bridge Panel** (`Ctrl+Alt+B`) summarises every bridge call in the
workspace.

### 12.13 Compile Pipeline Inspector

Visualises the Chrome-trace JSON written by `polyc --trace`. Each
frontend, pass and backend phase shows up as a coloured span.

### 12.14 Sample / Tutorial Browser

The welcome page exposes every sample under `tests/samples/` plus the
tutorial set under `docs/tutorial/`. **Topology Live** renders a live
diagram of the current sample's bridge graph.

### 12.15 Remote development

SSH, WSL, container and dev-container backends; the IDE forwards file
operations and process spawns to the remote host while keeping the
LSP / DAP adapters local. Configuration is described in
[realization/remote_dev_en.md](realization/remote_dev_en.md).

### 12.16 AI assistant

Chat, inline suggestions, refactor, and a privacy mode that pins
requests to a local model. The provider is pluggable via the plugin
SDK.

### 12.17 Collaboration

Pull-request reviews, inline comments, issue browser, and a
Live-Share-style collaborative editor.

### 12.18 Extensions, Marketplace and Workspaces

Plugins are loaded from `<config>/polyui/plugins/`; the marketplace
fetches from `https://marketplace.example.invalid/polyui/`. Workspaces
are folders with a `.polyui/` directory pinning settings, launches and
extensions.

### 12.19 Shell features

Welcome page, customisable status bar with 9 built-in slots
(`branch`, `problems`, `language`, `language_server`, `encoding`, `eol`,
`indent`, `package_manager`, `profiler`), recent files (`Ctrl+R`),
recent workspaces (`Ctrl+E`), and full session restore. See
[tutorial/shell_en.md](tutorial/shell_en.md) for the long form.

### 12.20 i18n, accessibility, telemetry

UI strings are extracted via `lupdate`; English and Simplified Chinese
ship by default. Accessibility includes a high-contrast theme,
keyboard-only navigation, and screen-reader hints. Telemetry is opt-in
and documented per-event.

### 12.21 File-type viewers

Image viewer (PNG/JPEG/WebP/GIF/SVG/BMP), hex viewer with chunked I/O
for files ≥ 1 GiB, binary inspector covering ELF/PE/Mach-O/WASM, and
SQLite client with SQL console. See
[tutorial/viewers_en.md](tutorial/viewers_en.md).

### 12.22 Keyboard shortcut cheat sheet

| Action                  | Shortcut          |
|-------------------------|-------------------|
| Quick Open file         | `Ctrl+P`          |
| Symbol search           | `Ctrl+T`          |
| Global text search      | `Ctrl+Shift+F`    |
| Format document         | `Shift+Alt+F`     |
| Code action             | `Ctrl+.`          |
| Rename symbol           | `F2`              |
| Go to definition        | `F12`             |
| Find references         | `Shift+F12`       |
| Toggle Problems panel   | `Ctrl+Shift+M`    |
| Toggle LSP panel        | `Ctrl+Alt+L`      |
| Toggle Profiler         | `Ctrl+Alt+P`      |
| Toggle Call Analyzer    | `Ctrl+Alt+G`      |
| Toggle Test Explorer    | `Ctrl+Alt+T`      |
| Toggle Package Manager  | `Ctrl+Alt+M`      |
| Toggle Bookmarks        | `Ctrl+Alt+K`      |
| Toggle TODO index       | `Ctrl+Alt+J`      |
| Recent files            | `Ctrl+R`          |
| Recent workspaces       | `Ctrl+E`          |
| Run / Debug             | `F5`              |
| Build task              | `Ctrl+Shift+B`    |
| Step over (debug)       | `F10`             |
| Step into (debug)       | `F11`             |

---

## 13. Diagnostics, Profiling, and Call Analysis

### 13.1 Diagnostic identifiers

Every diagnostic carries a stable id of the form
`polyc-(err|warn)-<E####|W####>`. The full catalogue lives in
[specs/ploy_diagnostics.md](specs/ploy_diagnostics.md).

Severity mapping:

| Severity | LSP DiagnosticSeverity | Behaviour                                          |
|----------|------------------------|----------------------------------------------------|
| Error    | 1                      | Aborts compilation; gutter mark red.               |
| Warning  | 2                      | Compilation continues; gutter mark yellow.         |
| Info     | 3                      | Informational note.                                |
| Hint     | 4                      | Editor hint (e.g. unused var).                     |

### 13.2 Problems panel

Live diagnostics aggregated across every language server. Filter by
severity, file substring, or message regex; double-click to jump.
Background scanner handles workspaces over 2 000 files in 50-file
batches per 50 ms tick. Quickstart:
[tutorial/problems_panel_quickstart.md](tutorial/problems_panel_quickstart.md).

### 13.3 Profiler

Workflow:

```sh
polyc --profile-instrument main.ploy -o build/main \
      --emit=call-graph:build/main.cgjson \
      --emit=profile-symbols:build/main.symjson
polyrt profile --json build/main.profile.json --duration-ms 2000 build/main
```

Open `build/main.profile.json` in the Profiler panel. Tabs:

* **Flame** — flame graph by stack.
* **Hotspots** — top-N self-time symbols.
* **Timeline** — per-thread Gantt with GC and bridge events.
* **Languages** — self-time grouped by host language plus the virtual
  `bridge` language for FFI overhead.

NDJSON streaming variant (`--stream`) drives a live tail in the IDE.
Quickstart: [tutorial/profiling_quickstart.md](tutorial/profiling_quickstart.md).

### 13.4 Call Analyzer

Loads `*.cgjson` produced by `--emit=call-graph`. Features:

* BFS-longest-path column layout.
* Language-pair filter list.
* Bounded-DFS path search between any two functions.
* Runtime-call-count overlay when the Profiler is active.
* Double-click a node to jump to its source.

Headless equivalent: `polytopo`. Quickstart:
[tutorial/call_analyzer_quickstart.md](tutorial/call_analyzer_quickstart.md).

### 13.5 Bridge inspector

The Bridge Panel shows every cross-language call site, its source and
target symbols, the marshalling rule applied, and a sampled value
preview. Useful when debugging mismatched FFI signatures.

### 13.6 Heap snapshot

`polyrt heap-snapshot --out heap.json <program>` dumps the GC heap into
a JSON tree compatible with the IDE's heap viewer. The viewer supports
diffing two snapshots to find leaks.

---

## 14. Testing, Samples, and CI

### 14.1 CTest targets (30 in total)

```
unit_tests             test_core              test_middle
test_backends          test_runtime           test_linker
test_lsp               test_polyls            test_completion_ranker
test_plugins           test_problems          test_settings
test_topology          test_topology_ui       test_e2e
integration_tests      benchmark_tests        pe_smoke
test_frontend_common
test_frontend_cpp      test_frontend_python   test_frontend_rust
test_frontend_java     test_frontend_dotnet   test_frontend_go
test_frontend_javascript test_frontend_ruby   test_frontend_ploy
samples_smoke          docs_lint              docs_sync
```

### 14.2 Running tests

```sh
ctest --test-dir build -j --output-on-failure
ctest --test-dir build -R "frontend"            # subset by name
ctest --test-dir build -R "benchmark" -V        # verbose
ctest --test-dir build -L "fast"                # by label
ctest --test-dir build --rerun-failed           # last failures only
ctest --test-dir build -T memcheck              # under valgrind
```

### 14.3 Sample tour (42 numbered + variants)

`tests/samples/00_minimal` … `tests/samples/41_grammar_polish` form a
graduated tour of the language and tooling. Each sample has its own
`README` and is exercised by the `samples_smoke` target.

| Sample                                | Demonstrates                                  |
|---------------------------------------|-----------------------------------------------|
| `00_minimal`                          | Single `.ploy` file, no host imports.         |
| `01_basic_linking` / `_v2`            | Cross-language linking with `LINK`.           |
| `02_struct_types`                     | Aggregate types and pattern matching.         |
| `03_generic_functions`                | Constrained generics.                         |
| `04_error_handling`                   | `TRY` / `CATCH` and propagation.              |
| `05_async_basics`                     | `ASYNC` / `AWAIT` essentials.                 |
| `06_pipelines`                        | `PIPELINE` syntax.                            |
| `07_io_and_files`                     | Standard I/O bridges.                         |
| `08_collections`                      | List / map / set bridging across languages.   |
| `09_mixed_pipeline`                   | C++ + Python pipeline driver.                 |
| `10_rust_serde`                       | Rust serde bridged from Ploy.                 |
| `11_java_records`                     | Java records mapped to IR `struct`.           |
| `12_dotnet_async`                     | C# async lowered to the runtime scheduler.    |
| `13_go_concurrency`                   | Goroutines and channels.                      |
| `14_javascript_promises`              | JS Promise interop with Ploy `ASYNC`.         |
| `15_async_await`                      | Async functions and the task scheduler.       |
| `16_ruby_blocks`                      | Ruby blocks lowered to closures.              |
| `17_optional_match`                   | `OPTION` / `MATCH` exhaustiveness.            |
| `18_extended_strings`                 | `r"…"`, `b"…"`, `f"…"` literals.              |
| `19_visibility`                       | `PUBLIC` / `PRIVATE` / `INTERNAL`.            |
| `20_class_bridge`                     | Class instantiation across languages.         |
| `21_polydoc`                          | Doc-comment extraction.                       |
| `22_database_access`                  | SQLite client + SQL console.                  |
| `23_http_client`                      | Cross-language HTTP client.                   |
| `24_grpc_service`                     | gRPC service end-to-end.                      |
| `25_simd_kernels`                     | SIMD intrinsics on x86_64 / arm64.            |
| `26_pgo_demo`                         | PGO instrument + use loop.                    |
| `27_lto_demo`                         | Thin / full LTO.                              |
| `28_gc_tuning`                        | GC algorithm comparison.                      |
| `29_plugin_panel`                     | Plugin SDK example.                           |
| `30_wasm_build`                       | WebAssembly target end-to-end.                |
| `31_wasm_simd`                        | WASM SIMD-128.                                |
| `32_remote_dev`                       | Remote development container.                 |
| `33_dap_walkthrough`                  | DAP debugging across languages.               |
| `34_test_explorer`                    | Test Explorer integration.                    |
| `35_coverage`                         | Coverage builds and Coverage panel.           |
| `36_profiler_streaming`               | Live profile streaming.                       |
| `37_call_analyzer_paths`              | Path search in the Call Analyzer.             |
| `38_heap_snapshot`                    | Heap snapshots and diff.                      |
| `39_telemetry_optin`                  | Telemetry opt-in flow.                        |
| `40_release_packaging`                | End-to-end release packaging.                 |
| `41_grammar_polish`                   | Latest grammar refinements regression.        |

### 14.4 Benchmarks

`build/benchmark_*.json` — `compilation`, `e2e`, `gc`, `link_times`,
`optimizations`, `comparison`. Driven by `polybench`, gated on regression
thresholds in CI. Results render in the IDE's Benchmark dashboard.

### 14.5 CI quality gates

| Gate                        | Action                                                     |
|-----------------------------|------------------------------------------------------------|
| Build matrix                | macOS arm64, Ubuntu x86_64, Windows x86_64.                |
| `ctest` full                | All 30 targets must pass.                                  |
| Sanitizer build             | ASan + UBSan run of `unit_tests` and `test_runtime`.       |
| TSan build                  | `test_runtime` and `samples_smoke` under TSan weekly.      |
| Coverage                    | Coverage build of `unit_tests`; threshold ≥ 80 %.          |
| `docs_lint` / `docs_sync`   | Bilingual structure + path checks.                         |
| `samples_smoke`             | All 42 samples build and run.                              |
| Benchmark regression        | `benchmark_comparison.json` within 5 % of baseline.        |

### 14.6 Local pre-flight script

```sh
scripts/preflight.sh
```

Runs `clang-format --dry-run`, `clang-tidy`, `docs_lint.py`,
`docs_sync_check.py`, and a subset of fast CTest targets in under a
minute.

---

## 15. Build System and Dependencies

### 15.1 Top-level CMake targets

```sh
cmake --build build --target polyc polyld polyui polyls   # core toolchain
cmake --build build --target unit_tests                   # one CTest target
cmake --build build --target docs                         # docs bundle
cmake --build build --target package                      # binary release
cmake --build build --target install                      # local install
cmake --build build --target benchmark_tests              # benchmark exe
```

### 15.2 Dependency manifest

Third-party libraries are declared in
[Dependencies.cmake](Dependencies.cmake). Pulled in via
`FetchContent_Declare`; pinned by tag and SHA. CI fails if either
moves under your feet. Major dependencies:

| Library      | Use                                                            |
|--------------|----------------------------------------------------------------|
| `spdlog`     | Internal logging (English log strings only).                   |
| `nlohmann/json` | JSON I/O for diagnostics, profile streams, plugin manifests. |
| `Catch2`     | Unit test framework.                                           |
| `Qt6`        | IDE shell.                                                     |
| `lldb`       | DAP adapter for native debugging.                              |
| `zstd`       | Compression for `.ir.bin` and the incremental cache.           |
| `re2`        | Diagnostic message regex matching in the Problems panel.       |

### 15.3 Sanitizer / coverage builds

```sh
cmake -S . -B build-asan -G Ninja -DPOLY_SANITIZE=address
cmake -S . -B build-ubsan -G Ninja -DPOLY_SANITIZE=undefined
cmake -S . -B build-tsan -G Ninja -DPOLY_SANITIZE=thread
cmake -S . -B build-cov  -G Ninja -DPOLY_COVERAGE=ON
```

### 15.4 Container images

`docker/` ships reproducible Ubuntu 22.04 and Alpine 3.19 images used
by CI. Each image bakes the host toolchain plus the optional language
SDKs needed by every frontend.

```sh
docker build -t polyglot/ubuntu-ci -f docker/ubuntu-ci.Dockerfile .
docker run --rm -it -v "$PWD":/work polyglot/ubuntu-ci \
       cmake -S /work -B /work/build-docker -G Ninja
```

### 15.5 Versioning

The project version is the single source of truth in the root
[CMakeLists.txt](CMakeLists.txt) (`project(PolyglotCompiler VERSION
1.45.2)`). All bumps must touch that line; tooling in
`scripts/bump_version.py` enforces this.

```sh
python scripts/bump_version.py --part=patch   # 1.45.2 → 1.45.3
python scripts/bump_version.py --part=minor   # 1.45.2 → 1.46.0
python scripts/bump_version.py --part=major   # 1.45.2 → 2.0.0
```

The script updates the root `CMakeLists.txt`, the version embedded in
`polyver`, and the bilingual changelog headers.

### 15.6 Install layout

`cmake --install build --prefix /opt/polyglot` produces:

```
/opt/polyglot/
├── bin/    polyc, polyld, polyasm, polyopt, polyrt, polybench,
│           polytopo, polyls, polydoc, polyver, polyui
├── lib/    libpoly_runtime.* libpoly_middle.* …
├── include/poly/...
├── share/polyglot/samples/
└── share/doc/polyglot/  (HTML rendering of docs/)
```

---

## 16. Extending the Compiler

### 16.1 Adding a Ploy keyword

1. Extend the lexer table in `frontends/ploy/lexer.cpp`.
2. Add the grammar production in `frontends/ploy/parser.cpp`.
3. Lower in `frontends/ploy/lower.cpp` to existing IR.
4. Document the keyword in
   [tutorial/ploy_language_tutorial.md](tutorial/ploy_language_tutorial.md)
   and its ZH counterpart.
5. Add a sample under `tests/samples/`.
6. Add a frontend test under `tests/unit/frontend_ploy/`.

### 16.2 Adding a package manager

1. Implement detection in `frontends/common/package_discovery.cpp`.
2. Add a fixture project under `tests/integration/package_managers/`.
3. Update [§ 4.4](#44-package-manager-auto-discovery) in this guide and
   its ZH counterpart.
4. Add a CI fixture image if a new SDK is required.

### 16.3 Adding a backend feature flag

1. Add the flag to the relevant `backends/<arch>/features.cpp` table.
2. Wire up the codegen branch.
3. Add an `e2e` test that exercises the feature.
4. Document the flag in [chapter 10](#10-backends).

### 16.4 Adding a middle-end pass

See [§ 8.6](#86-writing-a-pass) for the code skeleton. Then:

1. Register the pass id in `middle/pipeline_defaults.cpp`.
2. Add a unit test under `tests/unit/middle/passes/`.
3. Document the pass in [§ 8.3](#83-pass-catalogue) and in
   [realization/middle_passes.md](realization/middle_passes.md).
4. If the pass changes the default O2 pipeline, update the benchmark
   baseline.

### 16.5 Adding a new frontend

1. Create `frontends/<lang>/` with the standard layout
   (`include/`, `src/`, `tests/`, `examples/`).
2. Implement `Lower(SourceBuffer) → IRModule`.
3. Register language extension in `frontends/common/extension_table.cpp`.
4. Add a `test_frontend_<lang>` CTest target.
5. Add at least one sample under `tests/samples/`.
6. Add the frontend to chapters 5 and 6 of this guide and its ZH
   counterpart.

### 16.6 Adding a target triple

1. Implement `backends/<arch>/triple_<os>.cpp` for the new ABI.
2. Add a sysroot setup script under `scripts/sysroot/`.
3. Add a cross-compile e2e test under `tests/e2e/cross/`.

### 16.7 Code style

* C++: clang-format config at `.clang-format`. clang-tidy gate enforces
  modernize-* and bugprone-*.
* Ploy: built-in `polyc --format` is the formatter of record.
* All in-source comments must be English.
* No "minimal", "stub", or "placeholder" prose in comments.
* Public APIs have `///` doc comments parsed by `polydoc`.

### 16.8 Documentation rules

* Every English doc has a `_zh` counterpart in the same directory.
* Bilingual edits ship in the same commit; CI runs
  `scripts/docs_sync_check.py`.
* Headings and structure mirror across the pair.
* No demand-document identifiers in comments or docs.
* Version bumps update the root `CMakeLists.txt`.

### 16.9 Submitting changes

1. Fork; create a topic branch.
2. Run `scripts/preflight.sh`.
3. Open a PR; CI runs the full matrix.
4. Address review comments; squash if asked.
5. A maintainer merges with `Squash & merge` once gates are green.

---

## 17. Plugin SDK

### 17.1 Architecture

Plugins are dynamic libraries discovered by `polyui` from
`<config>/polyui/plugins/` and by `polyc` from `<config>/polyc/plugins/`.
Each plugin exports a small C ABI surface and declares its capability
flags up front so the host can sandbox unknown extensions.

### 17.2 Capability flags

| Flag                         | Grants                                                    |
|------------------------------|-----------------------------------------------------------|
| `POLY_CAP_FRONTEND`          | Register a new source-language frontend.                  |
| `POLY_CAP_BACKEND`           | Register a new code-generation backend.                   |
| `POLY_CAP_PASS`              | Register middle-end optimisation passes.                  |
| `POLY_CAP_PANEL`             | Add a UI panel to `polyui`.                               |
| `POLY_CAP_LSP_PROVIDER`      | Provide an LSP server for a language.                     |
| `POLY_CAP_DAP_PROVIDER`      | Provide a DAP debugger.                                   |
| `POLY_CAP_PACKAGE_MANAGER`   | Register a package-manager detector.                      |
| `POLY_CAP_AI_PROVIDER`       | Provide an AI assistant backend.                          |
| `POLY_CAP_VIEWER`            | Register a file-type viewer.                              |

### 17.3 Mandatory exports

```c
const PolyPluginManifest *poly_plugin_manifest(void);
int  poly_plugin_init(PolyHostServices *host);
void poly_plugin_shutdown(void);
```

### 17.4 Optional exports

```c
int  poly_plugin_register_frontend(PolyFrontendRegistry *r);
int  poly_plugin_register_backend(PolyBackendRegistry *r);
int  poly_plugin_register_passes(PolyPassRegistry *r);
int  poly_plugin_register_panels(PolyPanelRegistry *r);
int  poly_plugin_register_lsp(PolyLspRegistry *r);
int  poly_plugin_register_dap(PolyDapRegistry *r);
int  poly_plugin_register_pkg(PolyPackageRegistry *r);
int  poly_plugin_register_ai(PolyAiRegistry *r);
int  poly_plugin_register_viewer(PolyViewerRegistry *r);
```

### 17.5 Host services

Plugins receive a `PolyHostServices` struct providing logging, settings
access, file I/O, telemetry, and an event bus. Plugins must not link
against host internals directly.

```c
typedef struct PolyHostServices {
    int  api_version;
    void (*log)(int level, const char *msg);
    int  (*get_setting)(const char *key, char *out, size_t out_size);
    int  (*set_setting)(const char *key, const char *value);
    int  (*read_file)(const char *path, void **buf, size_t *len);
    int  (*emit_event)(const char *topic, const char *payload);
    int  (*subscribe)(const char *topic, void (*cb)(const char *));
} PolyHostServices;
```

### 17.6 Plugin file locations

| OS       | Location                                     |
|----------|----------------------------------------------|
| Linux    | `~/.config/polyui/plugins/`                  |
| macOS    | `~/Library/Application Support/polyui/plugins/` |
| Windows  | `%APPDATA%\polyui\plugins\`                  |

### 17.7 Quick example

```c
#include "polyplugin.h"

static const PolyPluginManifest g_manifest = {
    .api_version = POLY_PLUGIN_API_VERSION,
    .name        = "hello-plugin",
    .version     = "1.0.0",
    .capabilities = POLY_CAP_PANEL,
};

const PolyPluginManifest *poly_plugin_manifest(void) { return &g_manifest; }

int poly_plugin_init(PolyHostServices *host) {
    host->log(POLY_LOG_INFO, "hello-plugin loaded");
    return POLY_OK;
}

void poly_plugin_shutdown(void) {}
```

Build:

```sh
cc -shared -fPIC -I /opt/polyglot/include hello.c \
   -o ~/.config/polyui/plugins/hello.so
```

### 17.8 Plugin testing

`tests/unit/plugins/` contains a host harness that loads plugins out of
a fixture directory, exercises every registered capability, and checks
for clean shutdown.

---

## 18. Appendix

### 18.1 Glossary

| Term            | Definition                                                              |
|-----------------|-------------------------------------------------------------------------|
| Bridge call     | A cross-language call lowered through `runtime::ffi::BridgeFrame`.      |
| CTest target    | A logical test executable registered with CTest (30 in this project).   |
| Driver          | Any of the 11 tool binaries shipped under `build/`.                     |
| Frontend        | A static library that lowers one source language to unified IR.         |
| Pipeline        | A `PIPELINE` chain in Ploy; also the optimisation pass sequence.        |
| Sample          | A numbered project under `tests/samples/`.                              |
| Triple          | The `arch-vendor-os-abi` string passed to `--target=`.                  |
| Sysroot         | Toolchain-relative root used during cross-compile.                      |
| Bridge ABI      | The marshalling contract used by `bridge_call`.                         |
| Safepoint       | An IR location at which the GC may run.                                 |

### 18.2 Tool ↔ chapter cross-reference

| Tool         | Chapter                                  |
|--------------|------------------------------------------|
| `polyc`      | [6.1](#61-polyc--compiler-driver)        |
| `polyld`     | [6.2](#62-polyld--linker), [11](#11-linker-and-object-format) |
| `polyasm`    | [6.3](#63-polyasm--assembler--disassembler) |
| `polyopt`    | [6.4](#64-polyopt--ir-optimiser), [8](#8-middle-end-optimisation) |
| `polyrt`     | [6.5](#65-polyrt--runtime-cli), [9](#9-runtime), [13.3](#133-profiler) |
| `polybench`  | [6.6](#66-polybench--benchmark-harness), [14.4](#144-benchmarks) |
| `polytopo`   | [6.7](#67-polytopo--call-graph-topology-viewer), [13.4](#134-call-analyzer) |
| `polyls`     | [6.8](#68-polyls--language-server), [12](#12-ide-polyui-and-language-server) |
| `polydoc`    | [6.9](#69-polydoc--documentation-extractor) |
| `polyver`    | [6.10](#610-polyver--version--manifest-tool) |
| `polyui`     | [6.11](#611-polyui--ide-shell), [12](#12-ide-polyui-and-language-server) |

### 18.3 Release packaging

```sh
# Linux
cmake --build build --target package
# macOS — produces a notarisable .pkg
cmake --build build --target package_macos
# Windows (PowerShell)
cmake --build build --target package_windows
```

Output bundles land in `build/release/`. Each bundle ships:

* All 11 tool binaries.
* The runtime shared libraries.
* The HTML rendering of `docs/`.
* The 42 numbered samples.
* A signed manifest verifiable with `polyver --check-manifest`.

### 18.4 Environment variables reference

| Variable                    | Purpose                                                 |
|-----------------------------|---------------------------------------------------------|
| `POLY_GC`                   | Select the runtime GC algorithm.                        |
| `POLY_GC_HEAP_INIT`         | GC initial heap size.                                   |
| `POLY_GC_HEAP_MAX`          | GC heap upper bound.                                    |
| `POLY_GC_PAUSE_GOAL_MS`     | GC pause-time goal.                                     |
| `POLY_TASKS_THREADS`        | Task-pool size.                                         |
| `POLY_CACHE_DIR`            | Override incremental-cache directory.                   |
| `POLY_CACHE_MAX_BYTES`      | Cache LRU upper bound.                                  |
| `POLY_LOG_LEVEL`            | `error` / `warn` / `info` / `debug` / `trace`.          |
| `POLY_TELEMETRY`            | `off` (default) / `on`.                                 |
| `POLY_PROFILE_INTERVAL_MS`  | Profiler sampling period.                               |
| `POLYLS_LOG`                | Path for `polyls` frame log.                            |
| `POLYUI_PLUGIN_DIR`         | Override plugin discovery directory.                    |

### 18.5 Configuration file locations

| Item              | Location                                          |
|-------------------|---------------------------------------------------|
| User settings     | `~/.config/polyui/settings.json`                  |
| Workspace settings| `<workspace>/.polyui/settings.json`               |
| Launch profiles   | `<workspace>/.polyui/launch.json`                 |
| Plugins           | `~/.config/polyui/plugins/`                       |
| LSP overrides     | `~/.config/polyui/lsp.json`                       |
| Cache             | `~/.cache/polyglot/`                              |

### 18.6 References

* [tutorial/project_tutorial.md](tutorial/project_tutorial.md) — guided
  tour for newcomers.
* [tutorial/ploy_language_tutorial.md](tutorial/ploy_language_tutorial.md)
  — Ploy language reference.
* [specs/](specs/) — machine-readable schemas (call graph, profile
  stream, diagnostics).
* [realization/](realization/) — design notes per subsystem.
* [api/](api/) — public ABI documents (`polyls`, plugin SDK, runtime).

### 18.7 Frequently asked questions

**Q. Why a custom IR instead of LLVM?**
A. PolyglotCompiler needs first-class `bridge_call`, GC barriers, and
async primitives in the IR. Targeting LLVM was prototyped and rejected
because pretending these were intrinsics forced too many backend
work-arounds.

**Q. Can I use only one frontend?**
A. Yes. Set `POLY_BUILD_FRONTENDS="cpp;ploy"` at configure time to drop
the rest.

**Q. Are macOS arm64 binaries notarisable?**
A. Yes. The Mach-O emitter produces 16 KiB-aligned segments and stamps
`__DATA_CONST` with `SG_READ_ONLY (0x10)`, satisfying dyld 26
chained-fixup requirements that notarisation enforces.

**Q. Does the IDE work on Wayland?**
A. Yes; Qt 6.6 native Wayland is the default on Linux. X11 is still
supported via XWayland.

**Q. Is there a stable plugin ABI?**
A. The plugin ABI version is bumped only on incompatible changes. The
plugin host refuses to load plugins compiled against a different
`POLY_PLUGIN_API_VERSION`.

### 18.8 Changelog

A condensed changelog ships in [VERSION.txt](../VERSION.txt) and the
release notes generated by `scripts/release_notes.py`. The current
release is **PolyglotCompiler 1.45.2**.

### 18.9 Licence

PolyglotCompiler is distributed under the licence stored at
[LICENSE](../LICENSE). Third-party components retain their original
licences as recorded in [Dependencies.cmake](Dependencies.cmake).
