# PolyglotCompiler

<p align="center">
  <strong>A modern multi-language compiler with cross-language interoperability</strong><br/>
  <strong>现代多语言编译器，支持跨语言互操作</strong>
</p>

<p align="center">
  <img alt="C++20" src="https://img.shields.io/badge/C%2B%2B-20-blue.svg"/>
  <img alt="CMake" src="https://img.shields.io/badge/CMake-3.20+-green.svg"/>
  <img alt="License" src="https://img.shields.io/badge/License-GPLv3-blue.svg"/>
  <img alt="Tests" src="https://img.shields.io/badge/Tests-813_cases_|_3_suites-brightgreen.svg"/>
  <img alt="Platform" src="https://img.shields.io/badge/Platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey.svg"/>
</p>

---

## Overview / 项目概述

PolyglotCompiler is a multi-language compiler that compiles **C++**, **Python**, **Rust**, **Java**, and **C# (.NET)** source code into a unified intermediate representation (IR), and provides cross-language interoperability through the **`.ploy`** domain-specific language. It features its own frontends, optimisation passes, backends targeting x86_64, ARM64, and WebAssembly, and a runtime with garbage collection and FFI support.

PolyglotCompiler 是一个多语言编译器项目，将 **C++**、**Python**、**Rust**、**Java** 和 **C# (.NET)** 源代码编译为统一的中间表示（IR），并通过 **`.ploy`** 领域特定语言实现跨语言互操作。项目拥有自己的前端、优化 Pass、面向 x86_64/ARM64/WebAssembly 的后端，以及包含垃圾回收和 FFI 的运行时系统。

### Key Features / 核心特性

- **Multi-Frontend Architecture** — Dedicated frontends for C++, Python, Rust, Java, C# (.NET), and `.ploy`
- **Shared IR** — All languages compile to a common SSA-form intermediate representation
- **Cross-Language Linking** — The `.ploy` DSL enables function-level and OOP-level interop between languages
- **OOP Interop** — `NEW`, `METHOD`, `GET`, `SET`, `WITH`, `DELETE`, `EXTEND` keywords for cross-language class instantiation, method calls, attribute access, and resource management
- **Package Manager Integration** — Auto-discover packages via pip/conda/uv/pipenv/poetry/cargo/NuGet/Maven/Gradle/pkg-config
- **Triple Backend** — Code generation for x86_64 (SSE/AVX), ARM64 (NEON), and WebAssembly (shadow stack, WAT/binary)
- **25+ Optimisation Passes** — Including PGO, LTO, loop optimisations, devirtualisation
- **Runtime System** — 4 GC algorithms, FFI bindings, container marshalling, threading
- **Plugin System** — Stable C ABI plugin interface for extending languages, optimisers, backends, linters, formatters, and IDE panels
- **Debug Info** — Unified DWARF 5, PDB (Windows), and JSON source map emission
- **813 Test Cases** — Unit (743), Integration (52), Benchmark (18) across 3 test suites

---

## Architecture / 架构设计

```
┌─────────────┐  ┌──────────────┐  ┌──────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐
│  C++ Source │  │ Python Source│  │  Rust Source │  │ Java Source │  │  C# Source  │  │ .ploy Source│
└──────┬──────┘  └──────┬───────┘  └──────┬───────┘  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘
       │                │                 │                 │                │                │
       ▼                ▼                 ▼                 ▼                ▼                ▼
┌──────────────┐ ┌───────────────┐ ┌──────────────┐ ┌──────────────┐ ┌──────────────┐ ┌──────────────┐
│ C++ Frontend │ │Python Frontend│ │ Rust Frontend│ │ Java Frontend│ │ .NET Frontend│ │ Ploy Frontend│
└──────┬───────┘ └──────┬────────┘ └──────┬───────┘ └───────┬──────┘ └──────┬───────┘ └──────┬───────┘
       │                │                 │                 │               │                │
       └────────────────┴────────┬────────┴─────────────────┘               │                │
                                 │                                          │                │ 
                                 ▼                                          ▼                │
                          ┌───────────┐                             ┌─────────────┐          │
                          │ Shared IR │                             │  Polyglot   │◄─────────┘
                          │   (SSA)   │                             │   Linker    │
                          └─────┬─────┘                             └──────┬──────┘
                                │                                          │
                      ┌─────────┼─────────────────┐                        │
                      ▼         ▼                 ▼                        │
               ┌───────────┐ ┌───────────┐ ┌───────────┐                   │
               │  x86_64   │ │   ARM64   │ │   WASM    │                   │
               │  Backend  │ │  Backend  │ │  Backend  │                   │
               └─────┬─────┘ └─────┬─────┘ └─────┬─────┘                   │
                     │             │             │                         │
                     └──────┬──────┴─────────────┘                         │
                            ▼                                              ▼
                     ┌─────────────┐                              ┌─────────────┐
                     │ Object Files│                              │ Glue Code   │
                     └──────┬──────┘                              └──────┬──────┘
                            └────────┬───────────────────────────────────┘
                                     ▼
                              ┌─────────────┐
                              │  Executable │
                              └─────────────┘
```

> **Important:** PolyglotCompiler uses its own frontends (`frontend_cpp`, `frontend_python`, `frontend_rust`, `frontend_java`, `frontend_dotnet`) to compile all source languages to a shared IR. It does **NOT** depend on external compilers (MSVC/GCC/rustc/CPython/javac/dotnet). The `polyc` driver may optionally invoke a system linker (`polyld` or `clang`) only for the final link step.

---

## Quick Start / 快速开始

### Prerequisites / 环境要求

- **C++20** compatible compiler (MSVC 2022+, GCC 12+, Clang 15+)
- **CMake** 3.20+
- **Ninja** (recommended) or Make

All library dependencies (fmt, nlohmann_json, Catch2, mimalloc) are fetched automatically via CMake `FetchContent`.

**Optional (for IDE):**
- **Qt 6** (recommended) or Qt 5.15+ (for the `polyui` desktop IDE).
  - CMake auto-discovers Qt under `D:\Qt` (Windows), `deps/qt/` (project-local, all platforms), or the system path.
  - Pass `-DQT_ROOT=<path>` to override.
  - If Qt is not installed, run `./scripts/setup_qt.sh` (macOS/Linux) or `.\scripts\setup_qt.ps1` (Windows) to download pre-built Qt 6 binaries via `aqtinstall`.
  - If Qt is not found, the IDE target is silently skipped.

### Build / 构建

```bash
# Clone the repository
git clone https://github.com/user/PolyglotCompiler.git
cd PolyglotCompiler

# Configure (dependencies are fetched automatically)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug

# Build everything
cmake --build build

# Run tests
cd build && ./unit_tests [ploy] -r compact
```

**Windows (Visual Studio 2022+):**

```cmd
rem Activate MSVC toolchain — MUST use -arch=amd64
call "C:\...\VsDevCmd.bat" -arch=amd64
cmake -B build -G Ninja
cmake --build build --target unit_tests
cd build && unit_tests.exe [ploy] -r compact 2>&1 <nul
```

### Usage / 使用

```bash
# Compile a source file
polyc --lang=cpp -O2 -o output input.cpp

# Compile .ploy cross-language specification
polyc --lang=ploy input.ploy

# Link object files
polyld -o program file1.o file2.o

# Assemble
polyasm input.s -o output.o

# Optimise IR
polyopt -O3 input.ir -o optimised.ir
```

---

## The .ploy Language / .ploy 跨语言链接语言

`.ploy` is a domain-specific language for describing cross-language interoperability. It serves as a "glue language" that orchestrates function calls, class instantiation, method invocation, attribute access, resource management, and data flow between C++, Python, and Rust.

### Example: Cross-Language ML Pipeline

```ploy
IMPORT python PACKAGE torch >= 2.0;
IMPORT python PACKAGE numpy >= 1.20 AS np;
IMPORT cpp::image_processing;

PIPELINE ml_pipeline {
    FUNC preprocess(path: STRING) -> LIST(f64) {
        LET raw = CALL(cpp, image_processing::load, path);
        LET tensor = CALL(python, np::array, raw);
        RETURN tensor;
    }

    FUNC train(data: LIST(f64)) -> INT {
        LET model = NEW(python, torch::nn::Linear, 784, 10);
        LET optimizer = NEW(python, torch::optim::Adam,
                            METHOD(python, model, parameters), 0.001);

        LET output = METHOD(python, model, forward, data);
        LET loss_val = METHOD(python, output, mean);
        METHOD(python, loss_val, backward);
        METHOD(python, optimizer, step);

        LET lr = GET(python, optimizer, learning_rate);
        SET(python, model, training, FALSE);

        RETURN 0;
    }

    FUNC evaluate(model_path: STRING) -> FLOAT {
        LET f = NEW(python, open, model_path);
        WITH(python, f) AS handle {
            LET data = METHOD(python, handle, read);
            RETURN CALL(python, np::mean, data);
        }
    }
}

EXPORT ml_pipeline AS "train_model";
```

### Language Keywords (54)

```
LINK    IMPORT    EXPORT    MAP_TYPE   PIPELINE   FUNC     CONFIG
LET     VAR       STRUCT    VOID       INT        FLOAT    STRING
BOOL    ARRAY     LIST      TUPLE      DICT       OPTION
RETURN  IF        ELSE      WHILE      FOR        IN       MATCH
CASE    DEFAULT   BREAK     CONTINUE
AS      AND       OR        NOT        CALL       CONVERT  MAP_FUNC
NEW     METHOD    GET       SET        WITH       DELETE   EXTEND
TRUE    FALSE     NULL      PACKAGE
VENV    CONDA     UV        PIPENV     POETRY
```

### Core Syntax / 核心语法

| Feature | Syntax | Description |
|---------|--------|-------------|
| Function Link | `LINK(cpp, python, f, g);` | Cross-language function binding |
| Package Import | `IMPORT python PACKAGE numpy >= 1.20;` | Import with version constraints |
| Selective Import | `IMPORT python PACKAGE torch::(tensor, no_grad);` | Import specific symbols |
| Function Call | `CALL(python, np::mean, data)` | Cross-language function call |
| Class Instantiation | `NEW(python, torch::nn::Linear, 784, 10)` | Create foreign class instance |
| Method Call | `METHOD(python, model, forward, data)` | Call method on foreign object |
| Attribute Get | `GET(python, obj, weight)` | Read foreign object attribute |
| Attribute Set | `SET(python, obj, threshold, 0.5)` | Write foreign object attribute |
| Resource Management | `WITH(python, resource) AS r { ... }` | Auto `__enter__`/`__exit__` |
| Object Destruction | `DELETE(python, obj)` | Destroy foreign object |
| Class Extension | `EXTEND(python, Base) AS Derived { ... }` | Cross-language class inheritance |
| Type Conversion | `CONVERT(value, FLOAT)` | Explicit type conversion |
| Type Mapping | `MAP_TYPE(cpp::int, python::int);` | Cross-language type mapping |
| Pipeline | `PIPELINE name { ... }` | Multi-stage processing pipeline |
| Package Manager | `CONFIG CONDA "env_name";` | Configure package discovery |
| Type Annotation | `LET model: python::nn::Module = NEW(...);` | Qualified type annotations |

### Compilation Model / 编译模型

PolyglotCompiler uses its own frontends to compile **all** languages (C++, Python, Rust, Java, C#/.NET) to a shared SSA-form IR. The `.ploy` frontend produces cross-language call descriptors consumed by the **PolyglotLinker**, which generates FFI glue code, type marshalling, and ownership tracking. The resulting IR is lowered through the backend to produce native code — a system linker may be invoked in the final link stage to assemble the executable.

---

## Toolchain / 工具链

| Tool | Binary | Purpose |
|------|--------|---------|
| Compiler Driver | `polyc` | Source → IR → Target code |
| Linker | `polyld` | Object file linking + cross-language glue |
| Assembler | `polyasm` | Assembly → Object file |
| Optimiser | `polyopt` | IR optimisation passes |
| Runtime Tool | `polyrt` | GC / FFI / Thread management |
| Benchmark | `polybench` | Performance evaluation suite |
| IDE | `polyui` | Qt-based desktop IDE with syntax highlighting, real-time diagnostics, and compilation |

---

## Plugin System / 插件系统

PolyglotCompiler supports a C ABI plugin interface for extending the compiler and IDE. Plugins are shared libraries (`polyplug_*.so` / `polyplug_*.dylib` / `polyplug_*.dll`) discovered automatically from search paths or loaded manually.

PolyglotCompiler 支持 C ABI 插件接口，用于扩展编译器和 IDE。插件为共享库（`polyplug_*.so` / `polyplug_*.dylib` / `polyplug_*.dll`），可从搜索路径自动发现或手动加载。

| Capability | Description |
|-----------|-------------|
| Language | Add new language frontends |
| Optimiser | Add custom optimisation passes |
| Backend | Add new code-generation targets |
| Tool | Add CLI tools or pipeline stages |
| UI Panel | Add IDE panels / dock widgets |
| Syntax Theme | Add syntax highlighting themes |
| File Type | Register new file types |
| Code Action | Add quick-fix / refactoring actions |
| Formatter | Add code formatters |
| Linter | Add code linters |
| Debugger | Add debugger integrations |

See [`docs/specs/plugin_specification.md`](docs/specs/plugin_specification.md) for the full specification.  
详细说明见 [`docs/specs/plugin_specification_zh.md`](docs/specs/plugin_specification_zh.md)。

---

## Project Structure / 项目结构

```
PolyglotCompiler/
├── frontends/
│   ├── common/         # Shared frontend infrastructure (token pool, preprocessor, diagnostics)
│   ├── cpp/            # C++ frontend (lexer, parser, sema, lowering, constexpr)
│   ├── python/         # Python frontend (lexer, parser, sema, lowering)
│   ├── rust/           # Rust frontend (lexer, parser, sema, lowering)
│   ├── java/           # Java frontend (lexer, parser, sema, lowering) — Java 8/17/21/23
│   ├── dotnet/         # .NET (C#) frontend (lexer, parser, sema, lowering) — .NET 6/7/8/9
│   └── ploy/           # .ploy cross-language frontend (lexer, parser, sema, lowering)
├── middle/             # Middle layer: IR, SSA, CFG, optimisation passes, PGO, LTO
├── backends/
│   ├── common/         # Shared backend (debug info, DWARF, PDB, object file emission)
│   ├── x86_64/         # x86_64 backend (isel, regalloc, asm_printer, scheduler)
│   ├── arm64/          # ARM64 backend (isel, regalloc, asm_printer)
│   └── wasm/           # WebAssembly backend (wasm_target)
├── runtime/            # Runtime: GC (4 algorithms), FFI, marshalling, threading
│   └── src/libs/       # Language runtimes: python_rt, cpp_rt, rust_rt, java_rt, dotnet_rt
├── common/             # Common utilities: type system, symbol table, DWARF5
├── tools/              # Compiler driver (polyc), linker (polyld), assembler, IDE (polyui), etc.
├── tests/
│   ├── unit/           # Unit tests — 743 cases (Catch2)
│   ├── integration/    # Integration tests — 52 cases
│   ├── benchmarks/     # Benchmark tests — 18 cases
│   └── samples/        # 16 categorised sample programs (.ploy/.cpp/.py/.rs/.java/.cs)
└── docs/               # Documentation (bilingual: Chinese + English)
    ├── api/            # API reference
    ├── specs/          # Language & IR specifications
    ├── realization/    # Implementation details
    └── tutorial/       # Tutorials (ploy language + project)
```

---

## Testing / 测试

The project uses **Catch2** as the testing framework with three test suites, totalling **813 test cases**.

```bash
# Run all tests via CTest
cd build && ctest

# Run unit tests
./unit_tests

# Run .ploy frontend tests
./unit_tests [ploy]

# Run by category
./unit_tests [ploy][lexer]       # Lexer tests
./unit_tests [ploy][parser]      # Parser tests
./unit_tests [ploy][sema]        # Semantic analysis tests
./unit_tests [ploy][lowering]    # IR lowering tests
./unit_tests [python]            # Python frontend tests
./unit_tests [rust]              # Rust frontend tests
./unit_tests [java]              # Java frontend tests
./unit_tests [dotnet]            # .NET frontend tests

# Run integration tests
./integration_tests [integration]

# Run benchmark tests
./benchmark_tests [benchmark]
```

### Test Statistics / 测试统计

| Suite | Cases | Tags | Coverage |
|-------|-------|------|----------|
| **Unit Tests** | **743** | 231 tags | All frontends, IR, optimisation, GC, FFI, debug, linker, runtime, preprocessor, E2E |
| **Integration Tests** | **52** | `[integration]` | Full pipeline, cross-language interop, performance stress |
| **Benchmark Tests** | **18** | `[benchmark]` | Micro (lexer/parser/sema/lowering) + Macro (scaling/OOP/pipeline) |
| **Total** | **813** | — | — |

### Unit Test Breakdown / 单元测试明细

| Category | Tag | Cases |
|----------|-----|-------|
| .ploy Frontend | `[ploy]` | 216 |
| Python Frontend | `[python]` | 127 |
| Rust Frontend | `[rust]` | 46 |
| Java Frontend | `[java]` | 22 |
| .NET Frontend | `[dotnet]` | 24 |
| C++ Frontend | `[cpp]` | 10 |
| FFI / Interop | `[ffi]` | 39 |
| Linker | `[linker]` | 36 |
| E2E Pipeline | `[e2e]` | 29 |
| GC Algorithms | `[gc]` | 20 |
| Optimisation Passes | `[opt]` | 17 |
| Preprocessor | `[preprocessor]` | 18 |
| Threading | `[threading]` | 16 |
| LTO | `[lto]` | 14 |
| PGO | `[pgo]` | 13 |
| Backend | `[backend]` | 12 |
| DWARF5 | `[dwarf5]` | 7 |
| Debug | `[debug]` | 4 |

---

## Dependencies / 依赖

Managed automatically via CMake `FetchContent`:

| Dependency | Purpose |
|-----------|---------|
| [fmt](https://github.com/fmtlib/fmt) | Formatted output |
| [nlohmann/json](https://github.com/nlohmann/json) | JSON processing |
| [Catch2](https://github.com/catchorg/Catch2) | Unit testing framework |
| [mimalloc](https://github.com/microsoft/mimalloc) | High-performance memory allocator |

**Optional (not fetched by CMake — must be pre-installed):**

| Dependency | Purpose |
|-----------|---------|
| [Qt 6 (recommended) / Qt 5.15+](https://www.qt.io/) | Desktop IDE (`polyui`). Auto-discovered under `D:\Qt` (Windows), `deps/qt/` (all platforms), or system path. Run `scripts/setup_qt.sh` / `setup_qt.ps1` to install automatically. Skipped if not found |

---

## Documentation / 文档

All documentation is provided in **bilingual** format (Chinese + English) under `docs/`:

| Document | Description |
|----------|-------------|
| [`USER_GUIDE.md`](docs/USER_GUIDE.md) | Complete user guide (English) |
| [`USER_GUIDE_zh.md`](docs/USER_GUIDE_zh.md) | Complete user guide (Chinese / 完整用户指南) |
| [`docs/api/`](docs/api/) | API reference (bilingual) |
| [`docs/specs/`](docs/specs/) | Language & IR specifications, optimisation pipeline, runtime ABI, plugin specification |
| [`docs/realization/`](docs/realization/) | Implementation details (bilingual, 8 topics) |
| [`docs/tutorial/`](docs/tutorial/) | Tutorials: ploy language + project (bilingual) |

---

## Release Packaging / 发布打包

Pre-built release packages can be created with the provided scripts:

```bash
# Windows (PowerShell) — portable zip + NSIS installer
.\scripts\package_windows.ps1

# Linux — portable tar.gz
./scripts/package_linux.sh

# macOS — portable tar.gz with .app bundle
./scripts/package_macos.sh
```

See [`docs/specs/release_packaging.md`](docs/specs/release_packaging.md) for full details.  
详细说明见 [`docs/specs/release_packaging_zh.md`](docs/specs/release_packaging_zh.md)。

---

## License / 许可证

This project is licensed under the **GNU General Public License v3.0** — see the [LICENSE](LICENSE) file for details.

---

*Maintained by PolyglotCompiler Team / PolyglotCompiler 团队维护*  
*Last updated / 最后更新: 2026-03-15 (v1.0.0)*
