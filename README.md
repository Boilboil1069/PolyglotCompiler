# PolyglotCompiler

<p align="center">
  <strong>A modern multi-language compiler with cross-language interoperability</strong><br/>
  <strong>现代多语言编译器，支持跨语言互操作</strong>
</p>

<p align="center">
  <img alt="C++20" src="https://img.shields.io/badge/C%2B%2B-20-blue.svg"/>
  <img alt="CMake" src="https://img.shields.io/badge/CMake-3.20+-green.svg"/>
  <img alt="License" src="https://img.shields.io/badge/License-GPLv3-blue.svg"/>
  <img alt="Tests" src="https://img.shields.io/badge/Tests-171_cases_|_523_assertions-brightgreen.svg"/>
  <img alt="Platform" src="https://img.shields.io/badge/Platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey.svg"/>
</p>

---

## Overview / 项目概述

PolyglotCompiler is a multi-language compiler that compiles **C++**, **Python**, and **Rust** source code into a unified intermediate representation (IR), and provides cross-language interoperability through the **`.ploy`** domain-specific language. It features its own frontends, optimisation passes, backends targeting x86_64 and ARM64, and a runtime with garbage collection and FFI support.

PolyglotCompiler 是一个多语言编译器项目，将 **C++**、**Python** 和 **Rust** 源代码编译为统一的中间表示（IR），并通过 **`.ploy`** 领域特定语言实现跨语言互操作。项目拥有自己的前端、优化 Pass、面向 x86_64/ARM64 的后端，以及包含垃圾回收和 FFI 的运行时系统。

### Key Features / 核心特性

- **Multi-Frontend Architecture** — Dedicated frontends for C++, Python, Rust, and `.ploy`
- **Shared IR** — All languages compile to a common SSA-form intermediate representation
- **Cross-Language Linking** — The `.ploy` DSL enables function-level and OOP-level interop between languages
- **OOP Interop** — `NEW`, `METHOD`, `GET`, `SET`, `WITH` keywords for cross-language class instantiation, method calls, attribute access, and resource management
- **Package Manager Integration** — Auto-discover packages via pip/conda/uv/pipenv/poetry/cargo/pkg-config
- **Dual Backend** — Code generation for x86_64 (SSE/AVX) and ARM64 (NEON)
- **25+ Optimisation Passes** — Including PGO, LTO, loop optimisations, devirtualisation
- **Runtime System** — 4 GC algorithms, FFI bindings, container marshalling, threading

---

## Architecture / 架构设计

```
┌─────────────┐  ┌──────────────┐  ┌──────────────┐  ┌─────────────┐
│  C++ Source  │  │ Python Source │  │  Rust Source  │  │ .ploy Source │
└──────┬──────┘  └──────┬───────┘  └──────┬───────┘  └──────┬──────┘
       │                │                  │                  │
       ▼                ▼                  ▼                  ▼
┌──────────────┐ ┌──────────────┐ ┌──────────────┐ ┌──────────────┐
│ C++ Frontend │ │Python Frontend│ │ Rust Frontend│ │ Ploy Frontend│
└──────┬──────┘ └──────┬───────┘ └──────┬───────┘ └──────┬───────┘
       │               │                │                 │
       └───────────────┴────────┬───────┘                 │
                                │                         │
                          ┌─────▼─────┐            ┌──────▼──────┐
                          │ Shared IR │            │  Polyglot   │
                          │   (SSA)   │            │   Linker    │
                          └─────┬─────┘            └──────┬──────┘
                                │                         │
                      ┌─────────┴─────────┐               │
                      ▼                   ▼               │
               ┌───────────┐      ┌───────────┐           │
               │  x86_64   │      │   ARM64   │           │
               │  Backend  │      │  Backend  │           │
               └─────┬─────┘      └─────┬─────┘           │
                     │                   │                │
                     └─────────┬─────────┘                │
                               ▼                          ▼
                        ┌─────────────┐          ┌─────────────┐
                        │ Object Files│          │ Glue Code   │
                        └──────┬──────┘          └──────┬──────┘
                               └────────┬───────────────┘
                                        ▼
                                 ┌─────────────┐
                                 │  Executable  │
                                 └─────────────┘
```

> **Important:** PolyglotCompiler uses its own frontends (`frontend_cpp`, `frontend_python`, `frontend_rust`) to compile all source languages to a shared IR. It does **NOT** depend on external compilers (MSVC/GCC/rustc/CPython). The `polyc` driver may optionally invoke a system linker (`polyld` or `clang`) only for the final link step.

---

## Quick Start / 快速开始

### Prerequisites / 环境要求

- **C++20** compatible compiler (MSVC 2022+, GCC 12+, Clang 15+)
- **CMake** 3.20+
- **Ninja** (recommended) or Make

All library dependencies (fmt, nlohmann_json, Catch2, mimalloc) are fetched automatically via CMake `FetchContent`.

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

### Language Keywords (52)

```
LINK    IMPORT    EXPORT    MAP_TYPE   PIPELINE   FUNC     CONFIG
LET     VAR       STRUCT    VOID       INT        FLOAT    STRING
BOOL    ARRAY     LIST      TUPLE      DICT       OPTION
RETURN  IF        ELSE      WHILE      FOR        IN       MATCH
CASE    DEFAULT   BREAK     CONTINUE
AS      AND       OR        NOT        CALL       CONVERT  MAP_FUNC
NEW     METHOD    GET       SET        WITH
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
| Type Conversion | `CONVERT(value, FLOAT)` | Explicit type conversion |
| Type Mapping | `MAP_TYPE(cpp::int, python::int);` | Cross-language type mapping |
| Pipeline | `PIPELINE name { ... }` | Multi-stage processing pipeline |
| Package Manager | `CONFIG CONDA "env_name";` | Configure package discovery |
| Type Annotation | `LET model: python::nn::Module = NEW(...);` | Qualified type annotations |

### Compilation Model / 编译模型

PolyglotCompiler uses its own frontends to compile **all** languages (C++, Python, Rust) to a shared SSA-form IR. The `.ploy` frontend produces cross-language call descriptors consumed by the **PolyglotLinker**, which generates FFI glue code, type marshalling, and ownership tracking. The resulting IR is lowered through the backend to produce a **unified native binary** — no external compilers or interpreters are invoked at compile time.

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

---

## Project Structure / 项目结构

```
PolyglotCompiler/
├── frontends/
│   ├── common/         # Shared frontend infrastructure (token pool, diagnostics)
│   ├── cpp/            # C++ frontend (lexer, parser, sema, lowering, constexpr)
│   ├── python/         # Python frontend (lexer, parser, sema, lowering)
│   ├── rust/           # Rust frontend (lexer, parser, sema, lowering)
│   └── ploy/           # .ploy cross-language frontend (lexer, parser, sema, lowering)
├── middle/             # Middle layer: IR, SSA, CFG, optimisation passes, PGO, LTO
├── backends/
│   ├── common/         # Shared backend (debug info, object file emission)
│   ├── x86_64/         # x86_64 backend (isel, regalloc, asm_printer, scheduler)
│   └── arm64/          # ARM64 backend (isel, regalloc, asm_printer)
├── runtime/            # Runtime: GC (4 algorithms), FFI, marshalling, threading
├── common/             # Common utilities: type system, symbol table, DWARF5
├── tools/              # Compiler driver (polyc), linker (polyld), assembler, etc.
├── tests/
│   ├── unit/           # Unit tests (Catch2)
│   ├── samples/        # .ploy and C++ sample files
│   └── integration/    # Integration tests
└── docs/               # Documentation (bilingual: Chinese + English)
```

---

## Testing / 测试

The project uses **Catch2** as the testing framework. Tests are automatically discovered from `tests/unit/`.

```bash
# Run all tests
./unit_tests

# Run .ploy frontend tests
./unit_tests [ploy]

# Run by category
./unit_tests [ploy][lexer]       # Lexer tests
./unit_tests [ploy][parser]      # Parser tests
./unit_tests [ploy][sema]        # Semantic analysis tests
./unit_tests [ploy][lowering]    # IR lowering tests
./unit_tests [ploy][integration] # Integration tests
./unit_tests [ploy][pkgmgr]     # Package manager tests
```

### Test Statistics / 测试统计

| Suite | Tag | Cases | Coverage |
|-------|-----|-------|----------|
| .ploy Frontend | `[ploy]` | 171 (523 assertions) | Lexer / Parser / Sema / IR / Integration / Package / OOP |
| GC Algorithms | `[gc]` | 40+ | 4 GC algorithms |
| Optimisation Passes | `[opt]` | 50+ | 25+ passes |
| Python Features | `[python]` | 25+ | 25+ advanced features |
| Rust Features | `[rust]` | 28+ | 28+ advanced features |

---

## Dependencies / 依赖

Managed automatically via CMake `FetchContent`:

| Dependency | Purpose |
|-----------|---------|
| [fmt](https://github.com/fmtlib/fmt) | Formatted output |
| [nlohmann/json](https://github.com/nlohmann/json) | JSON processing |
| [Catch2](https://github.com/catchorg/Catch2) | Unit testing framework |
| [mimalloc](https://github.com/microsoft/mimalloc) | High-performance memory allocator |

---

## Documentation / 文档

All documentation is provided in **bilingual** format (Chinese + English) under `docs/`:

| Document | Description |
|----------|-------------|
| [`USER_GUIDE.md`](docs/USER_GUIDE.md) | Complete user guide (English) |
| [`USER_GUIDE_zh.md`](docs/USER_GUIDE_zh.md) | Complete user guide (Chinese / 完整用户指南) |
| [`docs/realization/`](docs/realization/) | Implementation details (bilingual) |
| [`docs/specs/`](docs/specs/) | Language specifications |

---

## License / 许可证

This project is licensed under the **GNU General Public License v3.0** — see the [LICENSE](LICENSE) file for details.

---

*Maintained by PolyglotCompiler Team / PolyglotCompiler 团队维护*  
*Last updated / 最后更新: 2026-02-20*
