# PolyglotCompiler Complete Guide

> A fully-featured multi-language compiler project  
> Supports C++, Python, Rust, Java, C# (.NET) → x86_64/ARM64/WebAssembly  
> With .ploy cross-language linking frontend

**Version**: v1.0.0  
**Last Updated**: 2026-03-15

---

## Table of Contents

1. [Project Overview](#1-project-overview)
2. [Quick Start](#2-quick-start)
3. [Architecture Design](#3-architecture-design)
4. [.ploy Cross-Language Linking Frontend](#4-ploy-cross-language-linking-frontend)
5. [Implemented Features](#5-implemented-features)
6. [Usage Guide](#6-usage-guide)
7. [IR Design Specification](#7-ir-design-specification)
8. [Optimisation System](#8-optimisation-system)
9. [Runtime System](#9-runtime-system)
10. [Testing Framework](#10-testing-framework)
11. [Build & Integration](#11-build--integration)
12. [Developer Guide](#12-developer-guide)
13. [Plugin System](#13-plugin-system)
14. [Appendix](#14-appendix)

---

# 1. Project Overview

## 1.1 Introduction

PolyglotCompiler is a modern multi-language compiler project that uses a multi-frontend shared intermediate representation (IR) architecture, and implements cross-language function-level linking and object-oriented interoperability through the `.ploy` language.

**Core Goals:**

- ✅ **Multi-Language Support**: Complete compilation frontends for C++, Python, Rust, Java, and .NET (C#)
- ✅ **Triple Backend Architecture**: x86_64 (SSE/AVX), ARM64 (NEON), and WebAssembly backends
- ✅ **Complete Toolchain**: Compiler (`polyc`), linker (`polyld`), optimiser (`polyopt`), assembler (`polyasm`), runtime tool (`polyrt`), benchmark (`polybench`), IDE (`polyui`)
- ✅ **Cross-Language Linking**: Declarative syntax via `.ploy` for function-level cross-language interop
- ✅ **Cross-Language OOP**: `NEW` / `METHOD` / `GET` / `SET` / `WITH` / `DELETE` / `EXTEND` keywords for class instantiation, method calls, attribute access, resource management, object destruction, and class extension
- ✅ **Package Manager Integration**: Supports pip/conda/uv/pipenv/poetry/cargo/pkg-config/NuGet/Maven/Gradle
- ✅ **Feature-Complete Implementation**: Comprehensive implementation with all frontends operational; Java and .NET test coverage is still expanding

## 1.2 Feature Overview

### Language Support

| Language | Frontend | IR Lowering | Advanced Features | Status |
|----------|----------|-------------|-------------------|--------|
| **C++** | ✅ | ✅ | OOP / Templates / RTTI / Exceptions / constexpr / SIMD | **Complete** |
| **Python** | ✅ | ✅ | Type annotations / Inference / Decorators / Generators / async / 25+ advanced | **Complete** |
| **Rust** | ✅ | ✅ | Borrow checking / Closures / Lifetimes / Traits / 28+ advanced | **Complete** |
| **Java** | ✅ | ✅ | Java 8/17/21/23 / Records / Sealed classes / Pattern matching / Text blocks / Switch expressions | **Core Complete** |
| **.NET (C#)** | ✅ | ✅ | .NET 6/7/8/9 / Records / Top-level statements / Primary constructors / File-scoped namespaces / Nullable ref types | **Core Complete** |
| **.ploy** | ✅ | ✅ | Cross-language linking / Pipelines / Package mgmt / OOP interop (NEW/METHOD/GET/SET/WITH) | **Complete** |

### Platform Support

| Architecture | Instruction Selection | Register Allocation | Calling Convention | Status |
|-------------|----------------------|--------------------|--------------------|--------|
| **x86_64** | ✅ | ✅ Graph colouring / Linear scan | ✅ SysV ABI | **Complete** |
| **ARM64** | ✅ | ✅ Graph colouring / Linear scan | ✅ AAPCS64 | **Complete** |
| **WebAssembly** | ✅ | ✅ Stack machine | ✅ WASM ABI | **Complete** |

### Toolchain

| Tool | Purpose | Executable |
|------|---------|-----------|
| Compiler driver | Source → IR → Target code | `polyc` |
| Linker | Object file linking + cross-language glue | `polyld` |
| Assembler | Assembly → Object file | `polyasm` |
| Optimiser | IR optimisation passes | `polyopt` |
| Runtime tool | GC / FFI / Thread management | `polyrt` |
| Benchmark | Performance evaluation suite | `polybench` |
| IDE | Qt-based desktop IDE with syntax highlighting, real-time diagnostics, and compilation | `polyui` |

---

# 2. Quick Start

## 2.1 Prerequisites

- **CMake** 3.20+
- **C++20** compatible compiler (MSVC 2022+ / GCC 12+ / Clang 15+)
- **Ninja** or **Make** build tool (Ninja recommended)

**Auto-fetched CMake dependencies:**
- **fmt**: Formatting library
- **nlohmann_json**: JSON library
- **Catch2**: Testing framework
- **mimalloc**: High-performance memory allocator

## 2.2 Building the Project

```bash
# Clone the project
git clone <repository-url>
cd PolyglotCompiler

# Build (Linux/macOS)
mkdir -p build && cd build
cmake .. -G Ninja
ninja

# Build (Windows + MSVC)
# Note: MUST use -arch=amd64 to avoid x86/x64 linker mismatch
call "C:\...\VsDevCmd.bat" -arch=amd64
mkdir build && cd build
cmake .. -G Ninja
ninja

# Run all tests
./unit_tests         # Linux/macOS
unit_tests.exe       # Windows
```

## 2.3 First C++ Program

```cpp
// hello.cpp
int main() {
    return 42;
}
```

```bash
polyc --lang=cpp --emit-ir=hello.ir hello.cpp    # Generate IR
polyc --lang=cpp -o hello.o hello.cpp             # Generate object file
```

## 2.4 First Cross-Language Pipeline

```ploy
// pipeline.ploy — Python ML + C++ post-processing
CONFIG VENV python "/opt/ml-env";
IMPORT python PACKAGE numpy >= 1.20 AS np;
IMPORT cpp::image_processor;

LINK(cpp, python, process, np::mean) {
    MAP_TYPE(cpp::double, python::float);
}

PIPELINE inference {
    FUNC run(data: LIST(f64)) -> f64 {
        LET avg = CALL(python, np::mean, data);
        LET result = CALL(cpp, image_processor::enhance, avg);
        RETURN result;
    }
}

EXPORT inference AS "run_inference";
```

---

# 3. Architecture Design

## 3.1 Overall Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              Source Code                                    │
│             C++ / Python / Rust / Java / C# (.NET) / .ploy                  │
└─────────────────────────────────────┬───────────────────────────────────────┘
                                      │
     ┌──────────┬──────────┬──────────┼──────────┬──────────┐
     │          │          │          │          │          │
     ▼          ▼          ▼          ▼          ▼          ▼
 ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐ ┌──────────┐
 │  C++   │ │ Python │ │  Rust  │ │  Java  │ │  .NET  │ │  .ploy   │
 │Frontend│ │Frontend│ │Frontend│ │Frontend│ │Frontend│ │ Frontend │
 └───┬────┘ └───┬────┘ └───┬────┘ └───┬────┘ └───┬────┘ └────┬─────┘
     │          │          │          │          │           │
     └──────────┴──────────┴──────────┴──────────┘       ┌───┘
                           │                             │
                           ▼                             ▼
                  ┌────────────────┐          ┌───────────────────┐
                  │  Shared IR     │          │ PolyglotLinker    │
                  │ (Middle Layer) │          │ (Cross-Language)  │
                  └────────┬───────┘          └───────────────────┘
                     │
        ┌────────────┼────────────┐
        │            │            │
        ▼            ▼            ▼
    ┌──────┐     ┌──────┐     ┌──────┐
    │ SSA  │     │ CFG  │     │ Opt  │
    │Trans │     │Build │     │Pass  │
    └──┬───┘     └──┬───┘     └──┬───┘
       └────────────┼────────────┘
                    │
           ┌────────────────┐
           │  Backend       │
           │ (x86_64/ARM64) │
           └────────┬───────┘
                    │
          ┌─────────┼─────────┐
          │         │         │
          ▼         ▼         ▼
      ┌───────┐ ┌────────┐ ┌──────┐
      │ISelect│ │RegAlloc│ │AsmGen│
      └───┬───┘ └──┬─────┘ └──┬───┘
          └────────┼───────────┘
                   │
           ┌───────────────┐
           │ Object File   │
           │ (ELF/Mach-O/  │
           │  POBJ)        │
           └───────────────┘
```

**Key Design Principles:**
- All languages share a unified IR intermediate representation
- Each frontend independently implements the complete Lexer → Parser → Sema → Lowering pipeline
- All frontends are registered with a central **FrontendRegistry** using the `ILanguageFrontend` interface, enabling unified dispatch from CLI, IDE, tests, and plugins
- The `.ploy` frontend is unique in that its IR is consumed by the PolyglotLinker, generating cross-language glue code
- Six frontends: C++, Python, Rust, Java, .NET (C#), and .ploy

## 3.2 Directory Structure

```
PolyglotCompiler/
├── frontends/              # Frontends (6 language frontends)
│   ├── common/             # Shared frontend facilities (Token, Diagnostics, Preprocessor, Registry)
│   │   ├── include/        #   language_frontend.h (ILanguageFrontend interface)
│   │   │                   #   frontend_registry.h (FrontendRegistry singleton)
│   │   │                   #   diagnostics.h, lexer_base.h, sema_context.h
│   │   └── src/            #   frontend_registry.cpp, token_pool.cpp, preprocessor.cpp
│   ├── cpp/                # C++ frontend
│   │   ├── include/        #   cpp_frontend.h, cpp_lexer.h, cpp_parser.h, cpp_sema.h, cpp_lowering.h
│   │   └── src/            #   cpp_frontend.cpp, lexer/, parser/, sema/, lowering/, constexpr/
│   ├── python/             # Python frontend
│   │   ├── include/        #   python_frontend.h, python_lexer.h, python_parser.h, python_sema.h, python_lowering.h
│   │   └── src/            #   python_frontend.cpp, lexer/, parser/, sema/, lowering/
│   ├── rust/               # Rust frontend
│   │   ├── include/        #   rust_frontend.h, rust_lexer.h, rust_parser.h, rust_sema.h, rust_lowering.h
│   │   └── src/            #   rust_frontend.cpp, lexer/, parser/, sema/, lowering/
│   ├── java/               # Java frontend (Java 8/17/21/23)
│   │   ├── include/        #   java_frontend.h, java_ast.h, java_lexer.h, java_parser.h, java_sema.h, java_lowering.h
│   │   └── src/            #   java_frontend.cpp, lexer/, parser/, sema/, lowering/
│   ├── dotnet/             # .NET frontend (C# .NET 6/7/8/9)
│   │   ├── include/        #   dotnet_frontend.h, dotnet_ast.h, dotnet_lexer.h, dotnet_parser.h, dotnet_sema.h, dotnet_lowering.h
│   │   └── src/            #   dotnet_frontend.cpp, lexer/, parser/, sema/, lowering/
│   └── ploy/               # .ploy cross-language linking frontend
│       ├── include/        #   ploy_frontend.h, ploy_ast.h, ploy_lexer.h, ploy_parser.h, ploy_sema.h, ploy_lowering.h
│       └── src/            #   ploy_frontend.cpp, lexer/, parser/, sema/, lowering/
├── middle/                 # Middle layer
│   ├── include/
│   │   ├── ir/             #   IR definitions (cfg.h, ssa.h, verifier.h, analysis.h, data_layout.h, etc.)
│   │   ├── passes/         #   Optimisation pass interfaces (analysis/, transform/)
│   │   ├── pgo/            #   Profile-Guided Optimization
│   │   └── lto/            #   Link-Time Optimization
│   └── src/
│       ├── ir/             #   builder, ir_context, cfg, ssa, verifier, printer, parser, template_instantiator, opt
│       ├── passes/         #   pass_manager, optimizations/ (constant_fold, dead_code_elim, common_subexpr,
│       │                   #     inlining, devirtualization, loop_optimization, gvn, advanced_optimizations)
│       ├── pgo/            #   profile_data.cpp
│       └── lto/            #   link_time_optimizer.cpp
├── backends/               # Backends (3 architecture backends)
│   ├── common/             # Shared backend facilities
│   │   └── src/            #   debug_info.cpp, debug_emitter.cpp, dwarf_builder.cpp, object_file.cpp
│   ├── x86_64/             # x86_64 backend
│   │   ├── include/        #   x86_target.h, x86_register.h, machine_ir.h, instruction_scheduler.h
│   │   └── src/            #   isel/, regalloc/ (graph_coloring, linear_scan), asm_printer/, optimizations, calling_convention
│   ├── arm64/              # ARM64 backend
│   │   ├── include/        #   arm64_target.h, arm64_register.h, machine_ir.h
│   │   └── src/            #   isel/, regalloc/ (graph_coloring, linear_scan), asm_printer/, calling_convention
│   └── wasm/               # WebAssembly backend
│       ├── include/        #   wasm_target.h
│       └── src/            #   wasm_target.cpp
├── runtime/                # Runtime
│   ├── include/            # GC, FFI, service interfaces
│   └── src/
│       ├── gc/             #   mark_sweep, generational, copying, incremental, gc_strategy, runtime
│       ├── interop/        #   ffi, memory, marshalling, type_mapping, calling_convention, container_marshal
│       ├── libs/           #   base.c, base_gc_bridge.cpp, python_rt.c, cpp_rt.c, rust_rt.c, java_rt.c, dotnet_rt.c
│       └── services/       #   exception, reflection, threading
├── common/                 # Project-wide common facilities
│   ├── include/
│   │   ├── core/           #   type_system.h, source_loc.h, symbol_table.h
│   │   ├── ir/             #   ir_node.h
│   │   ├── debug/          #   dwarf5.h
│   │   └── utils/          #   Utilities
│   └── src/
│       ├── core/           #   type_system.cpp, symbol_table.cpp
│       └── debug/          #   dwarf5.cpp
├── tools/                  # Toolchain (7 executables)
│   ├── polyc/              # Compiler driver (driver.cpp ~1773 lines)
│   ├── polyld/             # Linker (linker.cpp + polyglot_linker.cpp ~3790 lines)
│   ├── polyasm/            # Assembler (assembler.cpp)
│   ├── polyopt/            # Optimiser (optimizer.cpp)
│   ├── polyrt/             # Runtime tool (polyrt.cpp)
│   ├── polybench/          # Benchmark (benchmark_suite.cpp)
│   └── ui/                 # Qt-based desktop IDE (polyui) — platform-separated source layout
│       ├── common/         #   Cross-platform shared code (mainwindow, code_editor, syntax_highlighter, file_browser, output_panel, compiler_service, terminal_widget)
│       ├── windows/        #   Windows-specific entry point (main.cpp)
│       ├── linux/          #   Linux-specific entry point (main.cpp)
│       └── macos/          #   macOS-specific entry point (main.cpp)
├── tests/                  # Tests
│   ├── unit/               # Unit tests (Catch2 framework) — 743 test cases
│   │   └── frontends/ploy/ #   ploy_test.cpp (216 test cases)
│   ├── samples/            # Sample programs (16 categorised directories with .ploy/.cpp/.py/.rs/.java/.cs)
│   ├── integration/        # Integration tests (compile pipeline / interop / performance) — 52 test cases
│   └── benchmarks/         # Benchmark tests (micro / macro) — 18 test cases
└── docs/                   # Documentation
    ├── api/                # API reference (bilingual)
    ├── specs/              # Language & IR specifications
    ├── realization/        # Implementation docs (8 topics × 2 languages = 16 files)
    ├── tutorial/           # Tutorials (ploy language + project, bilingual)
    ├── demand/             # Requirements
    ├── USER_GUIDE.md       # This document (English)
    └── USER_GUIDE_zh.md    # Complete guide (Chinese)
```

## 3.3 Frontend Design

Each frontend follows a unified 4-stage pipeline:

```
Source Code
    │
    ▼
┌─────────┐    Token Stream    ┌─────────┐    AST   ┌─────────┐   Annotated AST    ┌──────────┐ IR Module
│  Lexer  │──────────────────▶ │ Parser  │────────▶ │  Sema   │───────────────────▶│ Lowering │──────────▶
└─────────┘                    └─────────┘          └─────────┘                    └──────────┘
```

### 3.3.1 Unified Frontend Registry

All language frontends are managed through a **FrontendRegistry** — a singleton registration center that replaces hard-coded if/else dispatch chains. The CLI (`polyc`), the IDE (`polyui`), tests, and plugins all share this single dispatch mechanism.

**Architecture:**

```
                    ┌──────────────────────────┐
                    │    FrontendRegistry       │
                    │    (Singleton)            │
                    ├──────────────────────────┤
                    │  Register(frontend)       │
                    │  GetFrontend(name/alias)  │
                    │  GetFrontendByExtension() │
                    │  DetectLanguage(path)     │
                    │  SupportedLanguages()     │
                    │  AllFrontends()           │
                    └───────────┬──────────────┘
                                │
         ┌──────────┬───────────┼───────────┬──────────┬──────────┐
         │          │           │           │          │          │
         ▼          ▼           ▼           ▼          ▼          ▼
     ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐
     │  Ploy  │ │  C++   │ │ Python │ │  Rust  │ │  Java  │ │ .NET   │
     │Frontend│ │Frontend│ │Frontend│ │Frontend│ │Frontend│ │Frontend│
     └────────┘ └────────┘ └────────┘ └────────┘ └────────┘ └────────┘
         │          │           │           │          │          │
         └──────────┴───────────┴───────────┴──────────┴──────────┘
                                │
                    ┌───────────────────────┐
                    │  ILanguageFrontend     │
                    │  (Abstract Interface)  │
                    ├───────────────────────┤
                    │  Name() -> string      │
                    │  DisplayName() -> string│
                    │  Extensions() -> [...]  │
                    │  Aliases() -> [...]     │
                    │  Tokenize(source)       │
                    │  Analyze(source, diags) │
                    │  Lower(source, ir_ctx)  │
                    │  NeedsPreprocessing()   │
                    └───────────────────────┘
```

**Key features:**

- **Auto-registration**: Each frontend adapter uses the `REGISTER_FRONTEND()` macro to automatically register itself at static initialization time. No manual wiring is needed.
- **Multi-key lookup**: Frontends can be looked up by canonical name (e.g. `"cpp"`), alias (e.g. `"c"`, `"c++"`, `"csharp"`), or file extension (e.g. `".py"`, `".rs"`).
- **Language detection**: `DetectLanguage(file_path)` infers the language from a file's extension.
- **Thread-safe**: All registry operations are mutex-protected.
- **Extensible**: Third-party plugins can register new language frontends at runtime via the same interface.

**Supported identifiers:**

| Frontend | Canonical Name | Aliases | Extensions |
|----------|---------------|---------|------------|
| C++ | `cpp` | `c`, `c++` | `.cpp`, `.cc`, `.cxx`, `.c`, `.hpp`, `.h` |
| Python | `python` | `py` | `.py` |
| Rust | `rust` | `rs` | `.rs` |
| Java | `java` | — | `.java` |
| .NET/C# | `dotnet` | `csharp`, `c#` | `.cs`, `.vb` |
| .ploy | `ploy` | — | `.ploy`, `.poly` |

**Source files:**

| File | Description |
|------|-------------|
| `frontends/common/include/language_frontend.h` | `ILanguageFrontend` abstract interface and `FrontendOptions`/`FrontendResult` types |
| `frontends/common/include/frontend_registry.h` | `FrontendRegistry` singleton and `REGISTER_FRONTEND()` macro |
| `frontends/common/src/frontend_registry.cpp` | Registry implementation (lookup, enumeration, clear) |
| `frontends/<lang>/include/<lang>_frontend.h` | Per-language adapter header |
| `frontends/<lang>/src/<lang>_frontend.cpp` | Per-language adapter implementation with auto-registration |

### 3.3.2 Frontend Pipeline Stages

**Frontend scale overview:**

| Frontend | Source Path | Core Features |
|----------|------------|---------------|
| C++ | `frontends/cpp/` (5 compilation units) | OOP, Templates, RTTI, Exceptions, constexpr, SIMD |
| Python | `frontends/python/` (4 compilation units) | Type annotations, Inference, 25+ advanced features |
| Rust | `frontends/rust/` (4 compilation units) | Borrow checking, Lifetimes, Closures, 28+ advanced features |
| Java | `frontends/java/` (4 compilation units) | Java 8/17/21/23, Records, Sealed classes, Pattern matching, Switch expressions, Text blocks |
| .NET (C#) | `frontends/dotnet/` (4 compilation units) | .NET 6/7/8/9, Records, Top-level statements, Primary constructors, Nullable reference types |
| .ploy | `frontends/ploy/` (6 compilation units) | LINK, IMPORT, PIPELINE, CONFIG, Package management, OOP interop |

---

# 4. .ploy Cross-Language Linking Frontend

## 4.1 Language Overview

`.ploy` is a declarative domain-specific language (DSL) for describing function-level linking and object-oriented interoperability between different programming languages. It is not a general-purpose language, but rather a "cross-language glue".

### Design Goals

1. **Declarative Linking** — Concise syntax for describing cross-language function call relationships
2. **OOP Interoperability** — `NEW` / `METHOD` / `GET` / `SET` / `WITH` support for cross-language class instantiation, method calls, attribute access, and resource management
3. **Type Mapping** — Automatic handling of type conversions between languages
4. **Package Manager Integration** — Directly reference packages from each language ecosystem (numpy, rayon, opencv, etc.)
5. **Pipeline Orchestration** — Organise multi-stage cross-language workflows into reusable pipelines

### Frontend Components

| Component | Header | Implementation | Lines | Responsibility |
|-----------|--------|----------------|-------|---------------|
| Lexer | `ploy_lexer.h` | `lexer.cpp` | ~295 | 54 keywords, operators, literals |
| Parser | `ploy_parser.h` | `parser.cpp` | ~2020 | Declaration / statement / expression parsing |
| Semantic Analyser | `ploy_sema.h` | `sema.cpp` | ~1950 | Type checking, param validation, package discovery, version verification |
| IR Generator | `ploy_lowering.h` | `lowering.cpp` | ~1530 | AST → IR transformation |

### Keyword List (54)

```
// Declaration keywords
LINK      IMPORT    EXPORT    MAP_TYPE   PIPELINE   FUNC      CONFIG

// Variables and types
LET       VAR       STRUCT    VOID       INT        FLOAT     STRING
BOOL      ARRAY     LIST      TUPLE      DICT       OPTION

// Control flow
RETURN    IF        ELSE      WHILE      FOR        IN        MATCH
CASE      DEFAULT   BREAK     CONTINUE

// Operators
AS        AND       OR        NOT        CALL       CONVERT   MAP_FUNC

// Object-oriented operations
NEW       METHOD    GET       SET        WITH       DELETE    EXTEND

// Values
TRUE      FALSE     NULL      PACKAGE

// Package managers
VENV      CONDA     UV        PIPENV     POETRY
```

## 4.2 Core Syntax

### 4.2.1 LINK — Cross-Language Function Linking

```ploy
// Basic syntax
LINK(target_language, source_language, target_function, source_function);

// Linking with type mapping
LINK(cpp, python, process_data, np::compute) {
    MAP_TYPE(cpp::std::vector_double, python::list);
    MAP_TYPE(cpp::int, python::int);
}

// With struct mapping
LINK(cpp, rust, transform, serde::parse) {
    MAP_TYPE(cpp::MyStruct, rust::MyStruct);
}
```

**Semantics:** A LINK declaration tells the PolyglotLinker to generate glue code that converts the output of `source_function` into input acceptable by `target_function`.

### 4.2.2 IMPORT — Module and Package Import

```ploy
// ---- Qualified module import ----
IMPORT cpp::math_utils;
IMPORT rust::serde;

// ---- Package import (using PACKAGE keyword) ----
IMPORT python PACKAGE numpy;

// With version constraint
IMPORT python PACKAGE numpy >= 1.20;
IMPORT python PACKAGE scipy == 1.10.0;
IMPORT rust PACKAGE serde >= 1.0;

// With alias
IMPORT python PACKAGE numpy >= 1.20 AS np;

// Selective import (import specific symbols only)
IMPORT python PACKAGE numpy::(array, mean, std);

// Selective import from sub-module
IMPORT python PACKAGE numpy.linalg::(solve, inv);

// Selective import + version constraint
IMPORT python PACKAGE numpy::(array, mean) >= 1.20;

// Selective import + version constraint
IMPORT python PACKAGE torch::(tensor, no_grad) >= 2.0;

// Whole-package import + alias (without selective import)
IMPORT python PACKAGE torch >= 2.0 AS pt;
```

> **Restriction:** Selective import `::()` and `AS` alias **cannot be used simultaneously**.
> For example, `torch::(tensor, no_grad) AS pt` is illegal — `pt` cannot determine whether it refers to `tensor` or `no_grad`.

**Syntax parsing order:**

```
IMPORT <language> PACKAGE <package_name>[::(<symbol_list>)] [<version_operator> <version>] [AS <alias>];
```

> Note: Selective import `::()` follows the package name directly; version constraints and aliases come after. Selective import and alias are mutually exclusive.

**Version operators:**

| Operator | Meaning | Example |
|----------|---------|---------|
| `>=` | Greater or equal | `>= 1.20` |
| `<=` | Less or equal | `<= 2.0` |
| `==` | Exact match | `== 1.10.0` |
| `>` | Strictly greater | `> 1.0` |
| `<` | Strictly less | `< 3.0` |
| `~=` | Compatible version (PEP 440) | `~= 1.20` → `>= 1.20, < 2.0` |

### 4.2.3 CONFIG — Package Manager Configuration

Configure a specific package manager environment for package discovery.

```ploy
// ---- pip / venv / virtualenv ----
CONFIG VENV python "/opt/ml-env";
CONFIG VENV "/home/user/.virtualenvs/ml";     // Default language: python

// ---- Conda environment ----
CONFIG CONDA python "ml_env";                 // Conda environment name
CONFIG CONDA "data_science";                  // Default language: python

// ---- uv-managed virtual environment ----
CONFIG UV python "D:/venvs/uv_env";

// ---- Pipenv project ----
CONFIG PIPENV python "C:/projects/myapp";

// ---- Poetry project ----
CONFIG POETRY python "C:/projects/poetry_app";
```

**Rules:**
- Only one CONFIG per language per compilation unit
- Duplicate configuration for the same language produces a compile-time error
- CONFIG must appear before the IMPORT that uses it
- Language parameter is optional, defaults to `python`

### 4.2.4 PIPELINE — Multi-Stage Pipeline

```ploy
PIPELINE image_pipeline {
    FUNC load(paths: LIST(STRING)) -> LIST(LIST(f64)) {
        LET images = CALL(rust, rayon::par_load, paths);
        RETURN images;
    }

    FUNC preprocess(images: LIST(LIST(f64))) -> LIST(LIST(f64)) {
        LET processed = CALL(cpp, opencv::resize_batch, images);
        RETURN processed;
    }

    FUNC classify(data: LIST(LIST(f64))) -> LIST(STRING) {
        LET results = CALL(python, pt::forward, data);
        RETURN results;
    }
}
```

### 4.2.5 EXPORT — Symbol Export

```ploy
EXPORT image_pipeline AS "classify_images";   // Export with alias
EXPORT compute;                                // Use original name
```

### 4.2.6 Type System

| Category | Types | Syntax |
|----------|-------|--------|
| Primitives | Integer, Float, Boolean, String, Void | `INT`, `FLOAT`, `BOOL`, `STRING`, `VOID` |
| Containers | List, Tuple, Dict, Optional | `LIST(T)`, `TUPLE(T1, T2)`, `DICT(K, V)`, `OPTION(T)` |
| Struct | User-defined composite type | `STRUCT Point { x: f64, y: f64 }` |
| Array | Fixed-length array | `ARRAY(INT)` |
| Function | Function signature | `(INT, FLOAT) -> BOOL` |
| Qualified | Language-specific type | `cpp::int`, `python::str`, `rust::Vec`, `java::String`, `dotnet::int` |

### 4.2.7 Control Flow

```ploy
// IF / ELSE
IF condition {
    ...
} ELSE IF other {
    ...
} ELSE {
    ...
}

// WHILE loop
WHILE condition {
    ...
}

// FOR .. IN range loop
FOR item IN collection {
    ...
}

FOR i IN 0..10 {
    ...
}

// MATCH pattern matching
MATCH value {
    CASE 1 { ... }
    CASE 2 { ... }
    DEFAULT { ... }
}

// BREAK / CONTINUE
WHILE TRUE {
    IF done { BREAK; }
    CONTINUE;
}
```

### 4.2.8 Variable Declaration and Expressions

```ploy
// Variable declaration
LET x = 42;                        // Immutable
VAR y: FLOAT = 3.14;               // Mutable, with type annotation
LET z: LIST(INT) = [1, 2, 3];      // Container literal

// Arithmetic expressions
LET sum = a + b * c;

// Logical expressions
IF x > 0 AND y < 10 { ... }

// Cross-language call
LET result = CALL(python, np::mean, data);

// Type conversion
LET converted = CONVERT(value, FLOAT);

// Cross-language class instantiation
LET model = NEW(python, torch::nn::Linear, 784, 10);

// Cross-language method call
LET output = METHOD(python, model, forward, input_data);

// Attribute access
LET weight = GET(python, model, weight);
SET(python, model, training, FALSE);

// Resource management
WITH(python, open_file) AS handle {
    LET data = METHOD(python, handle, read);
}

// Type annotation with qualified types
LET model: python::nn::Module = NEW(python, torch::nn::Linear, 784, 10);

// Container literals
LET list = [1, 2, 3];
LET tuple = (1, "hello", 3.14);
LET dict = {"key1": 10, "key2": 20};
LET point = Point { x: 1.0, y: 2.0 };     // Struct literal
```

### 4.2.9 MAP_FUNC — Mapping Function

```ploy
MAP_FUNC convert(x: INT) -> FLOAT {
    LET result = CONVERT(x, FLOAT);
    RETURN result;
}
```

### 4.2.10 NEW — Cross-Language Class Instantiation

The `NEW` keyword creates class instances (objects) from other languages within `.ploy`. Returns an opaque handle (`Any` type) that can be passed to `METHOD`, `GET`, `SET`, `WITH`, `CALL`, or `LINK`.

```ploy
// Basic syntax
NEW(language, class_name, arg1, arg2, ...)

// Example: Create Python class instance
IMPORT python PACKAGE torch >= 2.0;
LET model = NEW(python, torch::nn::Linear, 784, 10);

// No-argument construction
LET empty_list = NEW(python, list);

// String argument
LET conn = NEW(python, sqlite3::Connection, "data.db");

// Rust struct construction
IMPORT rust PACKAGE serde;
LET parser = NEW(rust, serde::json::Parser);
```

**Semantic rules:**
- First argument must be a known language identifier (`python`, `cpp`, `rust`)
- Second argument is the class name, supporting `::` qualified paths
- Subsequent arguments are passed as constructor parameters
- Returns `Any` type (opaque object handle)

**IR generation:** Constructor bridge stubs are named `__ploy_bridge_ploy_<lang>_<class>____init__`, return type is `Pointer(Void)`.

### 4.2.11 METHOD — Cross-Language Method Call

The `METHOD` keyword calls methods on objects from other languages within `.ploy`. The receiver object is passed as the first argument to the generated stub function.

```ploy
// Basic syntax
METHOD(language, object_expression, method_name, arg1, arg2, ...)

// Example: Call Python object method
LET model = NEW(python, torch::nn::Linear, 784, 10);
LET output = METHOD(python, model, forward, input_data);

// No extra arguments
LET params = METHOD(python, model, parameters);

// Chained calls
LET optimizer = NEW(python, torch::optim::Adam, params, 0.001);
METHOD(python, optimizer, zero_grad);
LET loss = METHOD(python, model, compute_loss, predictions, labels);
METHOD(python, loss, backward);
METHOD(python, optimizer, step);
```

**Semantic rules:**
- First argument must be a known language identifier
- Second argument is the object expression (typically a variable returned by `NEW`)
- Third argument is the method name, supporting `::` qualified paths
- Subsequent arguments are passed as method parameters
- Returns `Any` type

**IR generation:** Method bridge stubs are named `__ploy_bridge_ploy_<lang>_<method>`, with the receiver object inserted as the first argument.

### 4.2.12 GET — Cross-Language Attribute Access

The `GET` keyword reads an attribute from a foreign object. It generates a `__getattr__` bridge stub.

```ploy
// Basic syntax
GET(language, object_expression, attribute_name)

// Example: Read Python object attribute
LET model = NEW(python, torch::nn::Linear, 784, 10);
LET weight = GET(python, model, weight);
LET bias = GET(python, model, bias);

// Read Rust struct field
LET value = GET(rust, config, max_retries);

// Use in expression
IF GET(python, model, training) {
    METHOD(python, model, eval);
}
```

**Semantic rules:**
- First argument must be a known language identifier (`python`, `cpp`, `rust`)
- Second argument is the object expression
- Third argument is the attribute name (must be a valid identifier)
- Returns `Any` type

**IR generation:** Attribute access bridge stubs are named `__ploy_bridge_ploy_<lang>___getattr__<attr_name>`.

### 4.2.13 SET — Cross-Language Attribute Assignment

The `SET` keyword writes a value to a foreign object's attribute. It generates a `__setattr__` bridge stub.

```ploy
// Basic syntax
SET(language, object_expression, attribute_name, value)

// Example: Write Python object attribute
LET model = NEW(python, torch::nn::Linear, 784, 10);
SET(python, model, training, FALSE);
SET(python, model, learning_rate, 0.001);

// Set Rust struct field
SET(rust, config, max_retries, 5);

// Value can be any expression
SET(python, model, weight, CALL(python, np::zeros, 784));
```

**Semantic rules:**
- First argument must be a known language identifier
- Second argument is the object expression
- Third argument is the attribute name (must be a valid identifier)
- Fourth argument is the value expression
- Returns `Any` type (the assigned value is passed through for chaining)

**IR generation:** Attribute assignment bridge stubs are named `__ploy_bridge_ploy_<lang>___setattr__<attr_name>`. Both the object and value are passed as arguments.

### 4.2.14 WITH — Cross-Language Resource Management

The `WITH` keyword provides automatic resource management, similar to Python's `with` statement. It generates `__enter__` and `__exit__` bridge stubs, ensuring the resource is properly cleaned up.

```ploy
// Basic syntax
WITH(language, resource_expression) AS variable_name {
    // body — variable_name is bound to __enter__ return value
}

// Example: File handling
LET f = NEW(python, open, "data.csv");
WITH(python, f) AS handle {
    LET data = METHOD(python, handle, read);
    LET lines = METHOD(python, data, splitlines);
}

// Database connection
WITH(python, NEW(python, sqlite3::connect, "app.db")) AS db {
    LET cursor = METHOD(python, db, cursor);
    METHOD(python, cursor, execute, "SELECT * FROM users");
    LET rows = METHOD(python, cursor, fetchall);
}

// Multiple WITH blocks
WITH(python, resource1) AS r1 {
    METHOD(python, r1, process);
}
WITH(python, resource2) AS r2 {
    METHOD(python, r2, process);
}
```

**Semantic rules:**
- First argument must be a known language identifier
- Second argument is the resource expression (any expression that evaluates to an object with `__enter__`/`__exit__`)
- The `AS` keyword is followed by a variable name bound within the body scope
- The body is a block of statements enclosed in `{ }`
- The bound variable is available within the body scope
- Returns `Any` type

**IR generation:**
1. Evaluate the resource expression
2. Call the `__enter__` bridge stub → bind result to the variable name
3. Execute the body statements
4. Call the `__exit__` bridge stub for cleanup
5. Two cross-language descriptors are recorded: one for `__enter__` and one for `__exit__`

**Complete example — PyTorch training loop:**

```ploy
LINK(cpp, python, train_model, torch_train);
IMPORT python PACKAGE torch >= 2.0;
IMPORT python PACKAGE numpy >= 1.20 AS np;

PIPELINE ml_pipeline {
    FUNC train(data: LIST(f64), epochs: INT) -> LIST(f64) {
        LET model = NEW(python, torch::nn::Linear, 784, 10);
        LET optimizer = NEW(python, torch::optim::SGD,
                            METHOD(python, model, parameters), 0.01);

        // Attribute access
        LET lr = GET(python, optimizer, learning_rate);
        SET(python, model, training, TRUE);

        LET loss = METHOD(python, model, forward, data);
        METHOD(python, loss, backward);
        METHOD(python, optimizer, step);

        // Resource management
        WITH(python, NEW(python, open, "model.pt")) AS f {
            METHOD(python, torch, save, model, f);
        }

        RETURN CALL(python, np::array, loss);
    }
}

EXPORT ml_pipeline AS "train_ml";
```

### 4.2.6 Object Destruction — `DELETE`

The `DELETE` keyword provides explicit cross-language object destruction:

```ploy
DELETE(python, obj);
DELETE(cpp, ptr);
DELETE(rust, handle);
```

**Syntax:**
```
DELETE ( language , expression )
```

**Semantic rules:**
- First argument must be a known language identifier (`python`, `cpp`, `rust`)
- Second argument is the expression evaluating to the object to destroy
- The object must have been created via `NEW` or obtained from a cross-language call
- Returns `Void` type (destructor calls do not produce a value)

**IR generation:**
- **Python**: Generates a call to `__ploy_py_del` bridge stub (invokes `del` / reference release)
- **C++**: Generates a call to `__ploy_cpp_delete` bridge stub (invokes `delete` / destructor)
- **Rust**: Generates a call to `__ploy_rust_drop` bridge stub (invokes `drop()`)
- A `CrossLangCallDescriptor` is recorded for the runtime linker

**Example — Cleanup after training:**
```ploy
LET model = NEW(python, torch::nn::Linear, 784, 10);
// ... use model ...
DELETE(python, model);
```

### 4.2.7 Class Extension — `EXTEND`

The `EXTEND` keyword enables cross-language class extension (inheritance):

```ploy
EXTEND(python, torch::nn::Module) AS MyModel {
    FUNC forward(x: LIST(f64)) -> LIST(f64) {
        RETURN CALL(python, self::linear, x);
    }
}
```

**Syntax:**
```
EXTEND ( language , base_class ) AS DerivedName {
    FUNC method1 ( params ) -> ReturnType { body }
    FUNC method2 ( params ) -> ReturnType { body }
    ...
}
```

**Semantic rules:**
- First argument must be a known language identifier
- Second argument is the base class to extend (can be namespace-qualified, e.g. `torch::nn::Module`)
- `AS` keyword followed by the derived class name
- Body contains one or more `FUNC` declarations (method overrides / additions)
- The derived class name is registered in the symbol table as a type
- Each method's signature is validated (parameter count and types)

**IR generation:**
1. For each method, a bridge function `__ploy_extend_DerivedName_method` is created via `ir_ctx_.CreateFunction()`
2. The method body is lowered into the bridge function
3. A `__ploy_extend_register` call is generated with the class metadata
4. A `CrossLangCallDescriptor` is recorded for the runtime linker

**Example — Extending a Python class from .ploy:**
```ploy
LINK(cpp, python, run_model, inference);
IMPORT python PACKAGE torch >= 2.0;

PIPELINE neural_net {
    EXTEND(python, torch::nn::Module) AS CustomNet {
        FUNC forward(x: LIST(f64)) -> LIST(f64) {
            LET hidden = METHOD(python, self, relu, x);
            RETURN METHOD(python, self, linear, hidden);
        }

        FUNC reset_parameters() -> VOID {
            METHOD(python, self, init_weights);
        }
    }

    FUNC main() -> INT {
        LET net = NEW(python, CustomNet, 784, 10);
        LET result = METHOD(python, net, forward, input_data);
        DELETE(python, net);
        RETURN 0;
    }
}
```

### 4.2.8 Error Checking

The `.ploy` semantic analyzer provides comprehensive error checking:

**Parameter Count Mismatch:**
```
Error [E3010]: Parameter count mismatch in call to 'process'
  --> pipeline.ploy:15:9
   | Expected 3 argument(s), got 1
   = suggestion: Check the function signature for 'process'
```

**Type Mismatch:**
```
Error [E3011]: Type mismatch for parameter 1 in call to 'compute'
  --> pipeline.ploy:22:5
   | Expected 'INT', got 'STRING'
   = suggestion: Consider using CONVERT to convert the argument type
```

**Undefined Symbol:**
```
Error [E3001]: Undefined variable 'unknown_var'
  --> pipeline.ploy:8:13
```

**Error Code Ranges:**

| Range | Category | Examples |
|-------|----------|----------|
| 1xxx | Lexer | Invalid character, unterminated string |
| 2xxx | Parser | Unexpected token, missing delimiter |
| 3xxx | Semantic | Undefined variable, type mismatch, param count |
| 4xxx | Lowering | Unsupported target, codegen failure |
| 5xxx | Linker | Unresolved symbol, duplicate definition |

All error reports include source location, error codes, and optional suggestions. Traceback chains are supported for errors originating from multiple related locations.

## 4.3 Package Manager Auto-Discovery

The `.ploy` frontend automatically discovers installed packages during semantic analysis.

### Discovery Mechanism

| Package Manager | Discovery Command | CONFIG Syntax |
|----------------|-------------------|---------------|
| pip (venv/virtualenv) | `<venv>/python -m pip list --format=freeze` | `CONFIG VENV "path";` |
| Conda | `conda list -n <env_name> --export` | `CONFIG CONDA "env_name";` |
| uv | `<venv>/python -m pip list --format=freeze` or `uv pip list --format=freeze` | `CONFIG UV "path";` |
| Pipenv | `pipenv run pip list --format=freeze` | `CONFIG PIPENV "project_path";` |
| Poetry | `poetry run pip list --format=freeze` | `CONFIG POETRY "project_path";` |
| Cargo (Rust) | `cargo install --list` | Automatic |
| pkg-config (C/C++) | `pkg-config --list-all` | Automatic |

### Workflow

1. **Parse CONFIG** — Determine package manager type and environment path
2. **Package Index Phase** — Before semantic analysis, the `PackageIndexer` class runs package-manager commands (`pip list`, `cargo install --list`, `pkg-config`, `mvn`, `dotnet`) as an explicit pre-compilation phase with per-command timeouts (default 10s) and retry logic
3. **Cache Results** — Discovered packages and versions are cached in a session-level `PackageDiscoveryCache` (keyed by `language|manager|env_path`); a shared cache can be reused across multiple `PloySema` compilations to avoid redundant external-command invocations
4. **Sema reads from cache** — `PloySema` reads pre-populated cache entries but never shells out on its own (`enable_package_discovery` defaults to `false`)
5. **Version Verification** — Verify version constraints using 6 operators
6. **Symbol Verification** — Verify selective imports have no duplicates

### Package Index Phase

The `PackageIndexer` class decouples slow, side-effecting environment probing from the fast, deterministic semantic analysis pass:

```cpp
#include "frontends/ploy/include/package_indexer.h"

auto cache  = std::make_shared<PackageDiscoveryCache>();
auto runner = std::make_shared<DefaultCommandRunner>(std::chrono::seconds{10});

PackageIndexerOptions idx_opts;
idx_opts.command_timeout = std::chrono::seconds{10};
idx_opts.verbose = true;

PackageIndexer indexer(cache, runner, idx_opts);
indexer.BuildIndex({"python", "rust", "java"});

// Pass the pre-populated cache to sema
PloySemaOptions sema_opts;
sema_opts.enable_package_discovery = false;   // sema never shells out
sema_opts.discovery_cache = cache;            // pre-populated by indexer
PloySema sema(diagnostics, sema_opts);
```

The CLI (`polyc`) exposes this via:

| Flag | Description |
|------|-------------|
| `--package-index` | Run the explicit package-index phase before sema (default) |
| `--no-package-index` | Skip the package-index phase for faster compilation |
| `--pkg-timeout=<ms>` | Per-command timeout for package indexing (default 10000) |

### Command Runner with Timeout

The `DefaultCommandRunner` now supports configurable per-command timeouts:

```cpp
auto runner = std::make_shared<DefaultCommandRunner>(std::chrono::seconds{10});
CommandResult result = runner->RunWithResult("pip list --format=freeze");

if (result.Ok()) {
    // result.stdout_output contains the command output
} else if (result.timed_out) {
    // Command exceeded the timeout
} else {
    // result.exit_code indicates the failure reason
}
```

### Discovery Switch

Discovery can be toggled via `PloySemaOptions::enable_package_discovery`:

```cpp
PloySemaOptions opts;
opts.enable_package_discovery = false;   // default — sema never shells out
PloySema sema(diagnostics, opts);
```

When disabled (the default), `IMPORT PACKAGE` statements are still recorded, but no `_popen`/`popen` calls are executed.  Callers should use `PackageIndexer` as a pre-phase instead.

### Cache Strategy

`PackageDiscoveryCache` is a thread-safe (mutex-guarded), session-level cache.  The canonical key is:

```
language + "|" + manager_kind + "|" + env_path
```

For example: `"python|pip|/home/user/venv"`.  When multiple `.ploy` files are compiled in the same session, callers can inject a shared `PackageDiscoveryCache` via `PloySemaOptions::discovery_cache` to avoid re-running the same `pip list` / `cargo install --list` command.

### Command Runner Abstraction

All external command execution is routed through the `ICommandRunner` interface (`command_runner.h`).  The default implementation `DefaultCommandRunner` uses `_popen`/`popen`.  Tests can inject a `MockCommandRunner` to verify discovery behaviour without spawning real processes.

### Platform Adaptation

- **Windows**: Virtual environment Python is at `<venv_path>\Scripts\python.exe`
- **Unix/macOS**: Virtual environment Python is at `<venv_path>/bin/python`

## 4.4 Compilation Model

> **Core Idea: PolyglotCompiler itself compiles all languages; `.ploy` describes the cross-language connection relationships.**

```
┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐
│  C++ Source  │  │Python Source│  │ Rust Source  │  │ Java Source  │  │  C# Source   │
└──────┬──────┘  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘
       │                │                │                │                │
       ▼                ▼                ▼                ▼                ▼
┌────────────┐  ┌────────────┐  ┌────────────┐  ┌────────────┐  ┌────────────┐
│C++ Frontend│  │  Python    │  │   Rust     │  │   Java     │  │  .NET      │
│ (polyglot) │  │  Frontend  │  │  Frontend  │  │  Frontend  │  │  Frontend  │
└─────┬──────┘  └─────┬──────┘  └─────┬──────┘  └─────┬──────┘  └─────┬──────┘
      │               │               │               │               │
      └───────┬───────┴───────┬───────┴───────┬───────┴───────┬───────┘
              │               │               │               │
              ▼           Shared IR           ▼               │
        ┌───────────┐                   ┌───────────┐         │
        │  .ploy    │                   │  Polyglot │         │
        │  Frontend │──────────────────▶│  Linker   │◄────────┘
        └───────────┘                   └─────┬─────┘
                                              │
                                    ┌─────────┼─────────┐
                                    ▼         ▼         ▼
                               ┌────────┐┌────────┐┌────────┐
                               │ x86_64 ││ ARM64  ││  WASM  │
                               │Backend ││Backend ││Backend │
                               └───┬────┘└───┬────┘└───┬────┘
                                   └─────────┼─────────┘
                                             ▼
                                       ┌───────────┐
                                       │  Object   │
                                       │File / Exe │
                                       └───────────┘
```

**Important:** PolyglotCompiler uses its own frontends (`frontend_cpp`, `frontend_python`, `frontend_rust`, `frontend_java`, `frontend_dotnet`, `frontend_ploy`) to compile all language source code to a unified IR, then generates target code through triple backends (x86_64/ARM64/WebAssembly). It does **not depend** on external compilers (MSVC/GCC/rustc/CPython/javac/dotnet) for the compilation stage. The `polyc` driver (`driver.cpp`) may invoke a system linker (`polyld` or `clang`) for the final link step to produce an executable.

### Capability Matrix

| Feature | Status | Description |
|---------|--------|-------------|
| C++ ↔ Python function call | ✅ | Via FFI glue code + type marshalling |
| C++ ↔ Rust function call | ✅ | Via C ABI extern functions |
| Python ↔ Rust function call | ✅ | Via FFI bridge |
| Java ↔ C++ function call | ✅ | Via JNI bridge + `__ploy_java_*` runtime |
| Java ↔ Python function call | ✅ | Via JNI ↔ CPython C API bridge |
| .NET ↔ C++ function call | ✅ | Via CoreCLR hosting + `__ploy_dotnet_*` runtime |
| .NET ↔ Python function call | ✅ | Via CoreCLR ↔ CPython C API bridge |
| Java ↔ .NET interop | ✅ | Via IR-level unification + bidirectional runtime bridge |
| Primitive type marshalling | ✅ | int, float, bool, string, void |
| Container type marshalling | ✅ | list, tuple, dict, optional (basic types) |
| Struct mapping | ✅ | Cross-language struct field conversion (flat structs) |
| Package import + version constraint | ✅ | `IMPORT python PACKAGE numpy >= 1.20;` |
| Selective import | ✅ | `IMPORT python PACKAGE numpy::(array, mean);` |
| Multi-package-manager support | ✅ | pip/conda/uv/pipenv/poetry/cargo/pkg-config/NuGet/Maven/Gradle |
| Multi-stage pipelines | ✅ | PIPELINE + cross-language CALL |
| Control flow orchestration | ✅ | IF/WHILE/FOR/MATCH |
| Cross-language class instantiation | ✅ | `NEW(python, torch::nn::Linear, 784, 10)` |
| Cross-language method call | ✅ | `METHOD(java, service, processRequest, data)` |
| Cross-language attribute access | ✅ | `GET(dotnet, config, ConnectionString)` |
| Cross-language attribute assignment | ✅ | `SET(python, model, training, FALSE)` |
| Automatic resource management | ✅ | `WITH(python, f) AS handle { ... }` |
| Class inheritance extension | ✅ | `EXTEND(java, BaseService) { ... }` |
| Object destruction | ✅ | `DELETE(dotnet, dbConnection)` |
| Type annotations | ✅ | `LET model: python::nn::Module = NEW(...)` |
| Interface mapping | ✅ | `MAP_TYPE(python::nn::Module, cpp::NeuralNet)` |

### Architecture Constraints

| Constraint | Explanation |
|-----------|-------------|
| **Function/Object-level granularity** | Cannot mix different language code within a single function body, but supports cross-language function calls, class instantiation, method calls, attribute access, and resource management |
| **Runtime overhead** | Cross-language calls involve argument marshalling |
| **Memory model differences** | Each language manages its own memory; ownership tracked via OwnershipTracker |
| **WASM alloca** | Stack allocations are lowered to a shadow stack model (mutable i32 global at 65536, grows downward); linear memory layout differs from native backends |
| **Linker strict mode** | Unresolved cross-language symbols are hard errors — the linker will not generate glue stubs for unresolved pairs |
| **Sema strict mode** | When enabled, `.ploy` sema emits warnings for untyped parameters, `Any` fallbacks, and missing annotations; default is permissive |

---

# 5. Implemented Features

## 5.1 C++ Frontend (`frontend_cpp`)

### Basic Syntax ✅
- Function declarations and definitions, recursion, overloading
- Control flow: if-else, while, for, switch
- Variable declarations, scoping, name lookup

### Object-Oriented ✅
- Classes and objects, constructors/destructors
- Single and multiple inheritance, virtual inheritance (diamond)
- Access control (public/protected/private)
- Virtual functions and polymorphism

### Operator Overloading ✅
- Arithmetic, comparison, logical, bitwise
- Subscript operator `[]`, assignment operator

### Templates ✅
- Function and class templates
- Template parameter deduction, specialisation and partial specialisation
- Instantiation cache (`template_instantiator.cpp`)

### RTTI ✅
- `typeid` operator, `dynamic_cast`, `static_cast`

### Exception Handling ✅
- try-catch-throw, multiple catch clauses
- IR support: invoke/landingpad/resume

### Floating-Point Arithmetic ✅
- fadd, fsub, fmul, fdiv; floating-point comparison; fpext, fptrunc, fptosi

### constexpr ✅
- Compile-time function execution, constexpr variables
- Implementation: `frontends/cpp/src/constexpr/cpp_constexpr.cpp`

### SIMD Vectorisation ✅
- Loop vectorisation: SSE/AVX (x86_64), NEON (ARM64)

## 5.2 Python Frontend (`frontend_python`) ✅

- Type-annotated functions
- Control flow (if/while/for/match)
- Classes and objects
- 25+ advanced features (decorators, generators, async/await, comprehensions, match statement, etc.)

## 5.3 Rust Frontend (`frontend_rust`) ✅

- Functions, structs
- Borrow checker and lifetimes
- Closures
- 28+ advanced features (Traits, enums, pattern matching, smart pointers, etc.)

## 5.4 .ploy Frontend (`frontend_ploy`) ✅

See [Chapter 4](#4-ploy-cross-language-linking-frontend) for full details. Complete feature list:

- 54-keyword lexical analysis
- LINK/IMPORT/EXPORT/MAP_TYPE/PIPELINE/FUNC/STRUCT/CONFIG declaration parsing
- Version constraint verification (6 operators)
- Selective import with symbol verification
- 5 package manager configurations (VENV/CONDA/UV/PIPENV/POETRY)
- Automatic package discovery
- Cross-language class instantiation (`NEW`) and method call (`METHOD`)
- Cross-language attribute access (`GET`) and assignment (`SET`)
- Automatic resource management (`WITH`)
- Cross-language object destruction (`DELETE`) and class extension (`EXTEND`)
- Comprehensive error checking: parameter count / type mismatch / traceback chains
- Type annotations with qualified types
- Interface mapping via `MAP_TYPE`
- Complete type system (primitives + containers + structs + function types)
- Control flow (IF/ELSE/WHILE/FOR/MATCH/BREAK/CONTINUE)
- Struct literals (with lookahead disambiguation using LexerBase SaveState/RestoreState)
- IR Lowering (functions/pipelines/linking/expressions/control flow/OOP interop)

## 5.5 Java Frontend (`frontend_java`) ✅

Java frontend supporting Java 8, 17, 21, and 23 core features (test coverage expanding):

### Lexer
- Full Java keyword and operator tokenization
- String / character / numeric / annotation literals
- Single-line and multi-line comment handling

### Parser
- Classes, interfaces, enums, records (Java 16+)
- Sealed classes and permits (Java 17+)
- Pattern matching instanceof (Java 16+)
- Switch expressions (Java 14+)
- Text blocks (Java 13+)
- Methods, constructors, fields, generics
- Import and package declarations
- Lambda expressions and method references
- Try-with-resources (Java 7+)

### Semantic Analysis
- Type resolution and scope management
- Method overloading detection
- Record component implicit accessors
- Sealed class permit validation
- Constructor and method body analysis
- Binary expression type inference

### IR Lowering
- Class → struct lowering with constructor (`<init>`) generation
- Method lowering with mangled names (`ClassName::method`)
- Enum → integer constant lowering
- Record → struct with component accessors
- Interface → vtable generation
- `System.out.println` → `__ploy_java_print` runtime bridge

### Runtime Bridge (`java_rt`)
- JNI-compatible bridge API
- Version-aware initialization (Java 8/17/21/23)
- Object lifecycle management

## 5.6 .NET Frontend (`frontend_dotnet`) ✅

C# (.NET) frontend supporting .NET 6, 7, 8, and 9 core features (test coverage expanding):

### Lexer
- Full C# keyword and operator tokenization (including `??`, `?.`, `=>`)
- Regular and verbatim string literals
- Numeric literals (hex, binary, decimal)
- XML documentation comment handling

### Parser
- Classes, structs, interfaces, enums, records (.NET 5+)
- Namespaces and file-scoped namespaces (C# 10)
- Top-level statements (C# 9+)
- Primary constructors (C# 12)
- Using directives (including global usings, C# 10)
- Delegates
- Properties with get/set accessors
- Destructors/finalizers
- Methods, constructors, fields, generics
- Nullable reference types
- Pattern matching and switch expressions

### Semantic Analysis
- Type resolution and scope management
- Namespace-scoped symbol lookup
- Property and event analysis
- Constructor and method body analysis
- Using directive registration
- Type inference for `var` declarations

### IR Lowering
- Class/struct → struct lowering with constructor (`.ctor`) generation
- Method lowering with mangled names (`ClassName::Method`)
- Enum → integer constant lowering
- Namespace → scope prefix
- Top-level statement → `<Program>::Main` entry point
- `Console.WriteLine` → `__ploy_dotnet_print` runtime bridge

### Runtime Bridge (`dotnet_rt`)
- CoreCLR-compatible bridge API
- Version-aware initialization (.NET 6/7/8/9)
- Garbage-collected object interop

---

# 6. Usage Guide

## 6.1 Compiler Driver (`polyc`)

```bash
# Basic usage — auto-detects language from file extension
polyc <input_file> -o <output>

# Explicit language selection
polyc --lang=ploy input.ploy -o output

# Full options
polyc [options] <input_file>
```

### Options

| Option | Description |
|--------|-------------|
| `--lang=<cpp\|python\|rust\|java\|dotnet\|ploy>` | Source language (auto-detected from extension if omitted) |
| `--arch=<x86_64\|arm64\|wasm>` | Target architecture (default: x86_64) |
| `-O<0\|1\|2\|3>` | Optimisation level |
| `--emit-ir=<file>` | Output IR text |
| `--emit-asm=<file>` | Output assembly |
| `-o <file>` | Output object file / executable |
| `--debug` | Generate debug info (DWARF 5) |
| `--regalloc=<linear-scan\|graph-coloring>` | Register allocator selection |
| `--obj-format=<elf\|macho\|coff\|pobj>` | Object file format (auto-detected per OS: `coff` on Windows, `elf` on Linux, `macho` on macOS) |
| `--quiet` / `-q` | Suppress progress output |
| `--no-aux` | Disable auxiliary file generation |
| `--force` | Continue compilation despite errors |
| `--strict` | Strict mode: reject placeholder types, disable degraded stubs |
| `--permissive` | Permissive mode: allow placeholder types (override Release default) |
| `-h` / `--help` | Show usage information |

> **Note:** Release builds default to strict mode (via `POLYC_DEFAULT_STRICT`).
> Use `--permissive` to override.  `--strict` and `--force` are mutually exclusive.

### Compilation Modes

`polyc` supports two compilation modes that control how aggressively placeholder types and unresolved cross-language type information are treated:

| Mode | Placeholder types | `--force` stubs | Default in |
|------|-------------------|-----------------|------------|
| **Strict** | Error | Blocked | Release builds |
| **Permissive** | Warning | Allowed | Debug builds |

**Strict mode** (`--strict` or Release default):
- Semantic analysis reports placeholder-type fallbacks (e.g., `Any` for untyped parameters) as **errors**
- IR verifier rejects functions with unresolved placeholder `I64` return types
- Lowering rejects cross-language calls whose return type cannot be resolved
- Degraded stub generation via `--force` is prohibited
- `--strict` and `--force` are mutually exclusive

**Permissive mode** (`--permissive` or Debug default):
- Placeholder-type fallbacks are reported as **warnings**
- I64 placeholder return types are accepted by the verifier
- `--force` can generate degraded stubs for incomplete compilations

### Language Auto-Detection

`polyc` automatically detects the source language from the file extension:

| Extension | Language |
|-----------|----------|
| `.ploy` | ploy |
| `.py` | python |
| `.cpp`, `.cc`, `.cxx`, `.c` | cpp |
| `.rs` | rust |
| `.java` | java |
| `.cs` | dotnet |

### Progress Output

By default, `polyc` prints detailed progress to stderr:

```
========================================
 PolyglotCompiler v1.0.0  (polyc)
========================================
[polyc] Source: basic_linking.ploy
[polyc] Language: ploy (auto-detected)
[polyc] Arch: x86_64
[polyc] Opt level: O0
[polyc] Output: basic_linking
----------------------------------------
[polyc] Aux dir: ./aux
[polyc] Lexing (.ploy)... done (1.8ms)
[polyc]   -> ./aux/basic_linking.tokens.paux (3613 bytes, binary)
[polyc] Parsing (.ploy)... done (2.1ms)
[polyc]   -> ./aux/basic_linking.ast.paux (199 bytes, binary)
[polyc] Semantic analysis (.ploy)... done (0.8ms)
[polyc]   -> ./aux/basic_linking.symbols.paux (212 bytes, binary)
[polyc] IR lowering (.ploy)... done (1.1ms)
[polyc]   -> ./aux/basic_linking.ir.paux (878 bytes, binary)
[polyc]   -> ./aux/basic_linking.descriptors.paux (572 bytes, binary)
[polyc] SSA conversion + verification... done (0.6ms)
[polyc] Assembly generation... done (1.0ms)
[polyc] Object code emission... done (1.3ms)
[polyc]   -> ./aux/basic_linking.asm.paux (1330 bytes, binary)
[polyc] Per-language library emission...
[polyc]   -> ./aux/basic_linking_cpp.lib.pobj (cpp bridge library)
[polyc]   -> ./aux/basic_linking_python.lib.pobj (python bridge library)
done (1.5ms)
[polyc] Emit object... [polyc] Produced: basic_linking.obj
[polyc]   -> ./aux/basic_linking.obj (object copy in aux)
done (5.7ms)
----------------------------------------
[polyc] Target: x86_64-unknown-elf
[polyc] Object format: coff
[polyc] Total time: 27.7ms
[polyc] Aux files (binary): ./aux
[polyc] Compilation successful.
========================================
```

Use `--quiet` to suppress progress output.

### Auxiliary Files

By default, `polyc` generates intermediate files in an `aux/` subdirectory alongside the source file. All auxiliary files use the **PAUX binary container format** (header: `"PAUX"` magic + version + section count) to prevent exposure of intermediate data as plaintext.

| File | Content |
|------|---------|
| `<stem>.tokens.paux` | Lexer token dump (kind, location, text) — binary |
| `<stem>.ast.paux` | AST summary (declaration count, locations) — binary |
| `<stem>.symbols.paux` | Symbol table entries (kind, type) — binary |
| `<stem>.ir.paux` | IR text representation — binary |
| `<stem>.descriptors.paux` | Cross-language call descriptors — binary |
| `<stem>.asm.paux` | Generated assembly code — binary |
| `<stem>.obj` / `<stem>.o` | COFF/ELF/Mach-O object file (platform-dependent) |
| `<stem>_<lang>.lib.pobj` | Per-language bridge library (e.g., `_cpp.lib.pobj`, `_python.lib.pobj`) |

#### PAUX Binary Format

```
Offset  Size  Field
0       4     Magic: "PAUX"
4       2     Version: 1
6       2     Section count: N
8       8     Reserved (zero)
16      ...   Sections: for each section:
                uint16 name_length + name_bytes + uint32 data_length + data_bytes
```

#### Per-Language Bridge Libraries

Each language referenced in a `.ploy` file gets its own bridge library object file in `aux/`. For example, a `.ploy` file that links C++ and Python functions produces:
- `<stem>_cpp.lib.pobj` — contains bridge symbols for C++ interop
- `<stem>_python.lib.pobj` — contains bridge symbols for Python interop

This enables separate compilation and linking per language instead of monolithic output.

### Binary Object Output

In **compile** mode (default), `polyc` now produces a binary object file alongside the intermediate files:
- On **Windows**: COFF `.obj` file (compatible with MSVC `link.exe`)
- On **Linux**: ELF `.o` file
- On **macOS**: Mach-O `.o` file

In **link** mode, `polyc` additionally invokes the system linker to produce an executable.

### Optimisation Levels

| Level | Description | Enabled Optimisations |
|-------|-------------|----------------------|
| -O0 | No optimisation | None |
| -O1 | Basic optimisation | Constant folding, DCE |
| -O2 | Standard optimisation | +CSE, Inlining, GVN |
| -O3 | Aggressive optimisation | +Devirtualisation, Vectorisation, Loop optimisation, Advanced |

## 6.2 Linker (`polyld`)

```bash
polyld -o program file1.o file2.o file3.o
polyld -static -o program main.o -lmylib      # Static linking
polyld -shared -o libmylib.so obj1.o obj2.o    # Shared library
```

`polyld` internally contains the `PolyglotLinker`, which consumes the `.ploy` frontend's IR to generate cross-language glue code:

- `CrossLangCallDescriptor` — Describes type mapping for cross-language calls
- `LinkEntry` — Describes source/target function pairs from LINK declarations
- `ResolveLinks()` — Resolves all link entries and generates glue code
- Container type detection: `IsListType()`, `IsDictType()`, `IsTupleType()`, `IsStructType()`

## 6.3 PGO (Profile-Guided Optimization)

```bash
# 1. Generate instrumented version
polyc -fprofile-generate input.cpp -o app.instrumented

# 2. Run to collect profile
./app.instrumented < typical_input.txt

# 3. Optimise using profile
polyc -fprofile-use=default.profdata input.cpp -o app.optimized
```

Implementation: `middle/src/pgo/profile_data.cpp`

## 6.4 LTO (Link-Time Optimization)

```bash
# Thin LTO (recommended)
polyc -flto=thin -c file1.cpp -o file1.o
polyc -flto=thin file1.o file2.o -o app
```

Implementation: `middle/src/lto/link_time_optimizer.cpp`

## 6.5 .ploy Sample Files

The project includes 12 `.ploy` samples in `tests/samples/`:

| File | Content |
|------|---------|
| `basic_linking.ploy` | Basic LINK + MAP_TYPE |
| `complex_types.ploy` | Structs, container type mapping |
| `container_marshalling.ploy` | LIST/TUPLE/DICT marshalling |
| `advanced_pipeline.ploy` | Multi-stage PIPELINE |
| `multi_language_pipeline.ploy` | Three-language pipeline |
| `pipeline_control_flow.ploy` | IF/WHILE/FOR/MATCH |
| `mixed_compilation.ploy` | Mixed compilation example |
| `error_handling.ploy` | Error handling |
| `package_import.ploy` | Package import + version constraints + selective import + CONFIG |
| `cross_lang_class_instantiation.ploy` | Cross-language class instantiation with mixed calls |
| `11_java_interop/java_interop.ploy` | Java interop: StringProcessor class + Python text analysis |
| `12_dotnet_interop/dotnet_interop.ploy` | .NET interop: DataService class + Python statistics |

## 6.6 Sample Environment Setup

The `tests/samples/` directory includes environment setup scripts to create local Python and Rust environments for running samples.

### Windows (PowerShell)

```powershell
cd tests/samples
.\setup_env.ps1
```

### Linux / macOS (Bash)

```bash
cd tests/samples
chmod +x setup_env.sh
./setup_env.sh
```

The scripts create:
- **Python venv** in `tests/samples/env/python/` with numpy, torch, typing-extensions
- **Rust toolchain** in `tests/samples/env/rust/` with isolated RUSTUP_HOME/CARGO_HOME

The `env/` directory is excluded from git via `.gitignore`.

## 6.7 IDE (`polyui`)

`polyui` is a Qt-based desktop IDE for PolyglotCompiler. It provides an integrated development environment with syntax highlighting, real-time diagnostics, and a project file browser.

### Prerequisites

- **Qt 6** (recommended) or **Qt 5.15+** (Widgets module required)
- CMake automatically discovers Qt under:
  - `D:\Qt` (Windows)
  - `deps/qt/` (project-local, all platforms — installed via `aqtinstall`)
  - System path (e.g. Homebrew, apt)
- Pass `-DQT_ROOT=<path>` to override

**Quick setup (no Qt installed):**

```bash
# macOS / Linux
./tools/ui/setup_qt.sh

# Windows (PowerShell)
.\tools\ui\setup_qt.ps1
```

This downloads pre-built Qt 6.10.2 binaries into `deps/qt/` (ignored by git).

### Building

```bash
cmake --build build --target polyui
```

If Qt is not found, the `polyui` target is silently skipped without affecting other targets.

On **Windows**, `windeployqt` is automatically invoked after linking to copy the required Qt DLLs (e.g. `Qt6Cored.dll`, `Qt6Guid.dll`, `Qt6Widgetsd.dll`) and platform plugins into the build directory. This means `polyui.exe` can be double-clicked directly from the build folder without manual DLL setup.

On **macOS**, `macdeployqt` is automatically invoked to bundle Qt frameworks inside the `.app` directory. The application is built as a native macOS bundle with Retina display support.

On **Linux**, the build produces a standard ELF executable. Ensure the Qt runtime libraries are available on the target system (typically via the distribution's package manager).

### Platform-Separated Source Layout

The `polyui` source code is organized by platform:

```
tools/ui/
├── common/          # Shared cross-platform code (compiled on all platforms)
│   ├── src/         #   mainwindow.cpp, code_editor.cpp, syntax_highlighter.cpp,
│   │                #   file_browser.cpp, output_panel.cpp, compiler_service.cpp,
│   │                #   settings_dialog.cpp, git_panel.cpp, build_panel.cpp,
│   │                #   debug_panel.cpp, theme_manager.cpp
│   └── include/     #   Corresponding header files (incl. theme_manager.h)
├── windows/         # Windows-specific entry point (main.cpp)
├── linux/           # Linux-specific entry point (main.cpp)
└── macos/           # macOS-specific entry point (main.cpp)
```

CMake automatically selects the correct platform-specific `main.cpp` based on the host OS. Each platform entry point may contain OS-specific initialization (e.g. `windeployqt` on Windows, `xcb` platform default on Linux, `MACOSX_BUNDLE` on macOS).

### Launching

```bash
# Open the IDE
./build/polyui

# Open a project folder directly
./build/polyui --folder /path/to/project
```

### Features

| Feature | Description |
|---------|-------------|
| **Syntax Highlighting** | Uses compiler frontend tokenizers for accurate, language-aware highlighting across all 6 supported languages |
| **Real-time Diagnostics** | Invokes the full compiler pipeline (lexer → parser → sema) to report errors and warnings as you type |
| **File Browser** | Tree-view project navigator with filters for supported source file extensions |
| **Tabbed Editor** | Multi-file editing with tab management (new, open, save, close) |
| **Output Panel** | Three-tab output area: compiler output, error table (clickable to jump to source), and log |
| **Integrated Terminal** | Embedded shell (PowerShell on Windows, bash/zsh on Linux/macOS) with ANSI colour support, command history, and multiple instances |
| **Code Navigation** | Double-click errors in the diagnostics table to jump to the corresponding source location |
| **Bracket Matching** | Highlights matching brackets / parentheses / braces at the cursor position |
| **Auto-indent** | Maintains indentation level on new lines |
| **Zoom** | `Ctrl+Plus` / `Ctrl+Minus` to adjust editor font size |
| **Dark Theme** | Unified theme management via ThemeManager with 4 built-in colour schemes (Dark, Light, Monokai, Solarized Dark); all panels switch in unison |
| **Compile & Run** | One-click compile and run (`Ctrl+R`) — automatically locates the output binary and launches it via QProcess with stdout/stderr streamed to the log panel; stop a running process with `Ctrl+Shift+R` |
| **Settings Dialog** | 7-category preferences dialog (Appearance, Editor, Compiler, Environment, Build, Debug, Key Bindings) with persistent `QSettings` storage |
| **Custom Keybindings** | Edit shortcuts in the Key Bindings settings page via QKeySequenceEdit; apply, reset per-action; custom shortcuts are persisted via QSettings and loaded at startup |
| **Settings Linkage** | CMake path, build directory, debugger path and other settings automatically propagate to the Build and Debug panels; changes take effect immediately |
| **Git Integration** | Built-in Git panel with status, staging, commit, branch management, push/pull/fetch, diff viewer, log history, and stash support |
| **Build System** | CMake integration panel with configure/build/clean, generator and build-type selection, target discovery, and error parsing |
| **Debugger** | Integrated debug panel supporting lldb and gdb with breakpoints, stepping, full call-stack frame parsing (module, function, file, line), variable type/value parsing, live watch-expression evaluation with result updates, watch context menu (Remove / Remove All / Evaluate), break-on-entry option, and debug console |

### Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| `Ctrl+N` | New file |
| `Ctrl+O` | Open file |
| `Ctrl+S` | Save file |
| `Ctrl+Shift+S` | Save as |
| `Ctrl+W` | Close current tab |
| `Ctrl+Z` | Undo |
| `Ctrl+Y` | Redo |
| `Ctrl+X` / `Ctrl+C` / `Ctrl+V` | Cut / Copy / Paste |
| `Ctrl+F` | Find |
| `Ctrl+B` | Compile current file |
| `Ctrl+R` | Compile and run current file |
| `Ctrl+Shift+R` | Stop running process |
| `Ctrl+Shift+B` | Analyze current file (diagnostics only) |
| `` Ctrl+` `` | Toggle integrated terminal |
| `` Ctrl+Shift+` `` | Open new terminal instance |
| `Ctrl+Plus` / `Ctrl+Minus` | Zoom in / Zoom out |
| `Ctrl+,` | Open Settings dialog |
| `F9` | Start debugging |
| `F10` | Step over |
| `F11` | Step into |
| `Shift+F11` | Step out |

### Supported Languages

The IDE leverages the same frontend tokenizers used by `polyc`, ensuring accurate highlighting and diagnostics for:

- C++ (`.cpp`, `.h`, `.hpp`, `.cxx`)
- Python (`.py`)
- Rust (`.rs`)
- Java (`.java`)
- C# / .NET (`.cs`)
- Ploy (`.ploy`)

### Integrated Terminal

The IDE includes a built-in terminal panel that provides interactive shell access without leaving the editor.

**Features:**
- **Platform-aware shell detection**: automatically launches PowerShell on Windows, zsh on macOS, and bash (or `$SHELL`) on Linux
- **Multiple terminal instances**: create any number of independent terminal sessions; each runs in its own tab
- **ANSI colour support**: basic SGR escape codes are parsed and rendered with appropriate colours
- **Command history**: navigate previous commands with Up/Down arrows (up to 500 entries)
- **Keyboard shortcuts**: `Ctrl+C` sends interrupt, `Ctrl+L` clears the screen, `Ctrl+`\` toggles the panel
- **Working directory sync**: new terminals open in the project root shown in the file browser

**Usage:**
1. Press `` Ctrl+` `` to toggle the terminal panel, or use the **Terminal → Toggle Terminal** menu
2. Press `` Ctrl+Shift+` `` to open an additional terminal instance
3. Use the **Terminal** menu for Clear, Restart, and New Terminal actions
4. Close individual terminals via the tab close button

### Settings

Open the Settings dialog via **File → Settings** or `Ctrl+,`. Preferences are organized into 7 categories:

| Category | Options |
|----------|--------|
| **Appearance** | Theme, UI font family and size |
| **Editor** | Font family, size, tab width, word wrap, show whitespace, line numbers |
| **Compiler** | Default optimization level, target architecture, C++ standard, extra flags |
| **Environment** | Project root, CMake path, debugger path, PATH overrides |
| **Build** | Default generator, build type, parallel jobs, CMake extra arguments |
| **Debug** | Preferred debugger (lldb/gdb/auto), stop on entry, break on exceptions |
| **Key Bindings** | View and reference all keyboard shortcuts |

All settings are persisted via `QSettings` ("PolyglotCompiler"/"IDE") and applied immediately upon clicking **Apply** or **OK**.

### Git Integration

The Git panel (toggle via **View → Git Panel**) provides version control without leaving the IDE.

**Capabilities:**
- **Status view**: color-coded tree showing staged, unstaged, and untracked files (green/yellow/grey icons)
- **Staging**: stage/unstage individual files or all files via toolbar buttons and context menu
- **Commit**: enter commit messages with optional amend support; commit from the toolbar
- **Branch management**: create, delete, merge, and checkout branches from the branch selector
- **Remote operations**: push, pull, and fetch run asynchronously with progress feedback in the output area
- **Diff viewer**: view file diffs in the output text area; double-click a file to show its diff
- **Log history**: browse commit history with hash, author, date, and message
- **Stash**: stash and pop working directory changes via toolbar actions

### Build System

The Build panel (toggle via **View → Build Panel**) integrates CMake-based project building.

#### Modular CMake Structure

The project uses a modular `add_subdirectory()` layout that splits the monolithic top-level `CMakeLists.txt` into per-directory build files:

```
CMakeLists.txt          ← project setup, compiler flags, dependencies, add_subdirectory() calls
├── common/CMakeLists.txt      ← polyglot_common library
├── middle/CMakeLists.txt      ← middle_ir (IR, optimization passes, PGO, LTO)
├── frontends/CMakeLists.txt   ← frontend_common + all language frontends
├── backends/CMakeLists.txt    ← backend_x86_64, backend_arm64, backend_wasm
├── runtime/CMakeLists.txt     ← runtime library (GC, FFI, interop, services)
├── tools/CMakeLists.txt       ← polyc, polyasm, polyld, polyopt, polyrt, polybench, polyui
└── tests/CMakeLists.txt       ← unit_tests, integration_tests, benchmark_tests
```

Unit test sources are listed explicitly per module (not via `GLOB_RECURSE`), enabling precise rebuild granularity.

**Capabilities:**
- **Configure**: run `cmake` with the selected generator (Makefiles, Ninja, Xcode, etc.) and build type (Debug, Release, RelWithDebInfo, MinSizeRel)
- **Build / Rebuild**: build the entire project or individual targets discovered from `cmake --build --target help`
- **Clean**: remove all build artifacts
- **Error parsing**: compiler output is parsed for GCC/Clang-style and MSVC-style error messages; matched errors appear in the error tree and emit `BuildErrorFound` to open the source location
- **Progress**: a progress bar tracks CMake's percentage-based build output
- **Path discovery**: automatically searches `/opt/homebrew/bin`, `/usr/local/bin`, `/usr/bin`, and `$PATH` for the `cmake` executable

**Usage:**
1. Set the source and build directories via the browse buttons or type paths directly
2. Choose a generator and build type, then click **Configure**
3. Select a build target (or "all") and click **Build**
4. Errors appear in the tree below; double-click to navigate to the source location

### Debugging

The Debug panel (toggle via **View → Debug Panel**) provides interactive debugging via lldb or gdb.

**Capabilities:**
- **Debugger auto-detection**: prefers lldb on macOS, gdb on Linux; configurable in Settings
- **Breakpoints**: toggle breakpoints by file and line, add conditional breakpoints, remove all; breakpoints are listed in a dedicated tree
- **Execution control**: Start (`F9`), Stop, Pause, Continue, Step Over (`F10`), Step Into (`F11`), Step Out (`Shift+F11`), Run to Cursor
- **Call stack**: view the current call stack with frame number, function name, file, and line
- **Variables**: inspect local variables with name, value, and type
- **Watch expressions**: add arbitrary expressions to evaluate in the current debug context
- **Debug console**: send raw debugger commands and view responses
- **Source navigation**: when the debugger stops, the IDE opens the corresponding source file and highlights the current line via the `DebugLocationChanged` signal

**Usage:**
1. Set the executable path and optional program arguments at the top of the panel
2. Toggle breakpoints from the breakpoint tree (Add / Remove All)
3. Click **Start** or press `F9` to begin debugging
4. Use the toolbar buttons or keyboard shortcuts to step through code
5. Inspect variables and call stack in the side panels; add watch expressions as needed
6. Use the console tab to send raw debugger commands

---

# 7. IR Design Specification

## 7.1 Overview

PolyglotCompiler's intermediate representation (IR) is a complete SSA-form, explicit control flow, explicit memory model intermediate language.

Core implementation files:
- `middle/src/ir/builder.cpp` — IR builder
- `middle/src/ir/ir_context.cpp` — IR context management
- `middle/src/ir/cfg.cpp` — Control flow graph
- `middle/src/ir/ssa.cpp` — SSA transformation
- `middle/src/ir/analysis.cpp` — IR analysis
- `middle/src/ir/verifier.cpp` — IR verifier
- `middle/src/ir/printer.cpp` — IR text output
- `middle/src/ir/parser.cpp` — IR text parsing
- `middle/src/ir/data_layout.cpp` — Data layout
- `middle/src/ir/template_instantiator.cpp` — Template instantiation

## 7.2 Type System

### Scalar Types
- **Integer**: `i1, i8, i16, i32, i64`
- **Floating-point**: `f32, f64`
- **Other**: `void`

### Aggregate Types
- **Pointer**: Single pointee type
- **Array**: `[N x T]`
- **Vector**: `<N x T>` (for SIMD)
- **Struct**: `{T0, T1, ...}`
- **Function**: `(ret, params...)`

## 7.3 Core Instruction Set

### Arithmetic / Logic
```
add, sub, mul, sdiv/udiv, srem/urem     # Integer arithmetic
and, or, xor, shl, lshr, ashr           # Bitwise operations
icmp (eq, ne, slt, sle, sgt, sge,       # Integer comparison
      ult, ule, ugt, uge)
fadd, fsub, fmul, fdiv, frem             # Floating-point arithmetic
fcmp (foe, fne, flt, fle, fgt, fge)     # Floating-point comparison
```

### Memory Operations
```
alloca      # Stack allocation
load        # Load
store       # Store
gep         # Address computation (GetElementPtr)
```

### Control Flow
```
ret         # Return
br          # Unconditional branch
cbr         # Conditional branch
switch      # Multi-way branch
```

### Calls and Exceptions
```
call        # Direct/indirect call
invoke      # Call that may throw
landingpad  # Exception landing pad
resume      # Exception resume
```

### SSA
```
phi         # SSA merge node
```

### Type Conversions
```
sext, zext, trunc           # Integer extension/truncation
fpext, fptrunc              # Floating-point extension/truncation
fptosi, sitofp              # Float ↔ Integer
bitcast                     # Bit cast
```

## 7.4 Calling Conventions

| Convention | Integer Parameters | Float Parameters | Return Value | Stack Alignment |
|------------|-------------------|-----------------|-------------|-----------------|
| x86_64 SysV | RDI, RSI, RDX, RCX, R8, R9 | XMM0-7 | RAX/XMM0 | 16 bytes |
| ARM64 AAPCS64 | X0-X7 | V0-V7 | X0/V0 | 16 bytes |

Implementation:
- `backends/x86_64/src/calling_convention.cpp`
- `backends/arm64/src/calling_convention.cpp`
- `runtime/src/interop/calling_convention.cpp` (cross-language calling convention adaptation)

## 7.5 IR Verification Rules

- Each basic block has exactly one terminator instruction
- Operands must pass type checking
- Definitions must dominate uses (SSA property)
- Phi node input types must match result type
- Function argument count and types must match the declaration

---

# 8. Optimisation System

## 8.1 Pass Manager

```cpp
// middle/src/passes/pass_manager.cpp
class PassManager {
    std::vector<std::unique_ptr<Pass>> passes_;
public:
    void AddPass(std::unique_ptr<Pass> pass);
    void Run();
};
```

## 8.2 Optimisation Pass List

### Core Transform Passes (7 implementation files)

| Pass | File | Function |
|------|------|----------|
| `ConstantFold` | `constant_fold.cpp` | Constant folding |
| `DeadCodeElim` | `dead_code_elim.cpp` | Dead code elimination |
| `CommonSubExpr` | `common_subexpr.cpp` | Common subexpression elimination (CSE) |
| `Inlining` | `inlining.cpp` | Function inlining |
| `Devirtualization` | `devirtualization.cpp` | Virtual function devirtualisation |
| `LoopOptimization` | `loop_optimization.cpp` | Loop optimisation (unrolling/LICM/fusion/splitting/tiling/interchange) |
| `GVN` | `gvn.cpp` | Global value numbering |

### Analysis Passes (2)

| Pass | File | Function |
|------|------|----------|
| `AliasAnalysis` | `alias.h` | Alias analysis |
| `DominanceAnalysis` | `dominance.h` | Dominance analysis |

### Advanced Optimisations (in `advanced_optimizations.cpp`)

Contains 20+ advanced optimisations:

- **Loop**: Tail call optimisation, loop predication, software pipelining
- **Data flow**: Strength reduction, induction variable elimination, SCCP
- **Memory**: Escape analysis, scalar replacement, dead store elimination
- **Parallel**: Auto-vectorisation
- **Other**: Partial evaluation, code sinking/hoisting, jump threading, prefetch insertion, branch prediction optimisation, memory layout optimisation

### Backend Optimisations

| Pass | File | Function |
|------|------|----------|
| Instruction scheduling | `backends/x86_64/src/asm_printer/scheduler.cpp` | Instruction reordering to reduce stalls |
| x86_64 optimisations | `backends/x86_64/src/optimizations.cpp` | Peephole optimisation, instruction fusion |

## 8.3 PGO Support

- Implementation: `middle/src/pgo/profile_data.cpp`
- Runtime performance counters
- Profile data persistence
- Call-frequency-based inlining decisions
- Hot code layout optimisation

## 8.4 LTO Support

- Implementation: `middle/src/lto/link_time_optimizer.cpp`
- Cross-module optimisation
- Global dead code elimination
- Thin LTO (faster, lower memory usage)

---

# 9. Runtime System

## 9.1 Garbage Collection (4 Algorithms)

| GC Type | File | Characteristics | Use Case |
|---------|------|----------------|----------|
| Mark-Sweep | `mark_sweep.cpp` | General purpose | General applications |
| Generational | `generational.cpp` | Exploits object age distribution | Most applications (recommended) |
| Copying | `copying.cpp` | Semi-space collection, reduces fragmentation | Short-lived objects |
| Incremental | `incremental.cpp` | Tri-colour marking, low pause | Low-latency requirements |

GC strategy selection: `gc_strategy.cpp`

## 9.2 FFI Interop

| Component | File | Function |
|-----------|------|----------|
| FFI Bindings | `interop/ffi.cpp` | FFI bindings, ownership tracking, dynamic library loading |
| Type Marshalling | `interop/marshalling.cpp` | Integer/float/string marshalling |
| Container Marshalling | `interop/container_marshal.cpp` | list/tuple/dict/optional marshalling |
| Type Mapping | `interop/type_mapping.cpp` | Cross-language type mapping registry |
| Calling Convention | `interop/calling_convention.cpp` | Cross-language calling convention adaptation |
| Memory Management | `interop/memory.cpp` | Cross-language memory management |

## 9.3 Language Runtimes

| Component | File | Function |
|-----------|------|----------|
| Base Runtime | `libs/base.c` | Base runtime facilities |
| GC Bridge | `libs/base_gc_bridge.cpp` | GC ↔ Runtime bridge |
| Python RT | `libs/python_rt.c` | Python runtime support |
| C++ RT | `libs/cpp_rt.c` | C++ runtime support |
| Rust RT | `libs/rust_rt.c` | Rust runtime support |

## 9.4 Services

| Service | File | Function |
|---------|------|----------|
| Exception Handling | `services/exception.cpp` | Cross-language exception handling |
| Reflection | `services/reflection.cpp` | Runtime type information |
| Threading | `services/threading.cpp` | Thread pool, task scheduling, synchronisation primitives, coroutines |

## 9.5 Debug Information

- Full DWARF 5 support (`common/src/debug/dwarf5.cpp`)
- Unified DWARF builder (`backends/common/src/dwarf_builder.cpp`)
- Debug info emitter (`backends/common/src/debug_info.cpp`, `debug_emitter.cpp`)
- PDB support with MSF block layout, TPI type records, and RFC 4122 v4 GUID generation
- CFA (Call Frame Address) with register save rules and alignment
- ELF object files with SHT_RELA relocation sections
- Mach-O object files with LC_SEGMENT_64, LC_SYMTAB, nlist_64 symbol table
- Variable location tracking (debuggable after optimisation)
- Inline function debugging
- Separate debug info support
- JSON source map emission

---

# 10. Testing Framework

## 10.1 Test Overview

The project uses the **Catch2** testing framework. There are three test executables:

| Executable | Source Directory | Tags | Description |
|-----------|-----------------|------|-------------|
| `unit_tests` | `tests/unit/` | `[ploy]`, `[gc]`, `[opt]`, etc. | Unit tests for all modules — **743 cases** |
| `integration_tests` | `tests/integration/` | `[integration]` | End-to-end compilation pipeline, interop, performance stress — **52 cases** |
| `benchmark_tests` | `tests/benchmarks/` | `[benchmark]` | Micro and macro performance benchmarks — **18 cases** |

### Test Suite Summary

| Test Suite | Tag | Test Cases | Coverage |
|-----------|-----|-----------|----------|
| .ploy Frontend | `[ploy]` | 216 | Lexer / Parser / Sema / IR / Integration / Package mgmt / OOP interop / Error checking |
| Python Frontend | `[python]` | 127 | 25+ advanced features, type annotations, async, comprehensions |
| Rust Frontend | `[rust]` | 46 | Borrow checking, lifetimes, closures, traits |
| Linker | `[linker]` | 36 | Symbol resolution, ELF/MachO/COFF, cross-language glue |
| FFI / Interop | `[ffi]` | 39 | FFI bindings, marshalling, type mapping, ownership tracking |
| E2E Pipeline | `[e2e]` | 29 | Full pipeline from source → object code |
| Java Frontend | `[java]` | 22 | Java 8/17/21/23 features |
| .NET Frontend | `[dotnet]` | 24 | .NET 6/7/8/9 features |
| GC Algorithms | `[gc]` | 20 | 4 GC algorithms (mark-sweep, generational, copying, incremental) |
| Preprocessor | `[preprocessor]` | 18 | Object/function-like macros, directives, token pool |
| Optimisation Passes | `[opt]` | 17 | Constant fold, DCE, CSE, GVN, inlining, devirtualisation |
| Threading Services | `[threading]` | 16 | Thread pool, synchronisation primitives, coroutines |
| LTO | `[lto]` | 14 | Link-time optimisation, cross-module opt |
| PGO | `[pgo]` | 13 | Profile-guided optimisation |
| Backend | `[backend]` | 12 | Instruction selection, register allocation, scheduler |
| C++ Frontend | `[cpp]` | 10 | OOP, templates, RTTI, exceptions, constexpr |
| DWARF5 Debug | `[dwarf5]` | 7 | DWARF 5 debug info generation |
| Debug Info | `[debug]` | 4 | PDB, source map, debug emitter |
| Integration Tests | `[integration]` | 52 | Full pipeline / Cross-language interop / Performance stress |
| Benchmark Tests | `[benchmark]` | 18 | Micro-benchmarks (lexer/parser/sema/lowering) / Macro-benchmarks (scaling/OOP/pipeline) |

## 10.2 .ploy Test Details

| Category | Tag | Count | Coverage |
|----------|-----|-------|----------|
| Lexer | `[ploy][lexer]` | 17 | Keywords (54), identifiers, numbers, strings, operators |
| Parser | `[ploy][parser]` | 40 | LINK/IMPORT/EXPORT/FUNC/PIPELINE/STRUCT/CONFIG/NEW/METHOD/GET/SET/WITH/DELETE/EXTEND |
| Semantic Analysis | `[ploy][sema]` | 40 | Type checking, scoping, version verification, package discovery, OOP interop, error checking |
| IR Generation | `[ploy][lowering]` | 29 | Functions/pipelines/linking/expressions/control flow/OOP interop/DELETE/EXTEND |
| Integration | `[ploy][integration]` | 20 | Complete pipeline end-to-end |
| Diagnostics | `[ploy][diagnostics]` | 6 | Error codes, suggestions, traceback, formatting |
| Error Checking | `[ploy][error]` | 10 | Param count mismatch, type mismatch, error code validation |
| Version Constraints | `[ploy][version]` | 5+ | 6 version operators |
| Selective Import | `[ploy][selective]` | 7 | Single/multi-symbol, version combination, alias |
| CONFIG VENV | `[ploy][venv]` | 6 | Parsing/validation/duplicate detection/invalid language |
| Multi-Package-Manager | `[ploy][pkgmgr]` | 17 | CONDA/UV/PIPENV/POETRY parsing, validation, integration |

## 10.3 Running Tests

```bash
# Run all unit tests
./unit_tests

# Run .ploy tests
./unit_tests [ploy]

# Run by tag
./unit_tests [ploy][pkgmgr]    # Package manager tests
./unit_tests [ploy][lexer]      # Lexer tests
./unit_tests [ploy][sema]       # Semantic analysis tests

# Run integration tests
./integration_tests [integration]
./integration_tests [compile]   # Compilation pipeline tests only
./integration_tests [interop]   # Interop tests only
./integration_tests [perf]      # Performance stress tests only

# Run benchmark tests
./benchmark_tests [benchmark]
./benchmark_tests [micro]       # Micro-benchmarks only
./benchmark_tests [macro]       # Macro-benchmarks only

# Run benchmarks via CTest tiers (set POLYBENCH_MODE env var):
ctest -L fast                   # Quick smoke-run (1 warmup, few iterations)
ctest -L full                   # Full statistical run (5 warmup, many iterations)
ctest -L benchmark              # Default tier (3 warmup, moderate iterations)
```

> **Recommended build type for benchmarks**: `Release` or `RelWithDebInfo`.  Debug builds include assertions and sanitiser overhead that distort measurements.

```bash
# Verbose output
./unit_tests [ploy] -r compact

# Windows note: needs <nul to prevent pip command from hanging
unit_tests.exe [ploy] -r compact 2>&1 <nul
integration_tests.exe [integration] -r compact 2>&1 <nul
benchmark_tests.exe [benchmark] -r compact 2>&1 <nul
```

## 10.4 Sample Programs

The `tests/samples/` directory contains 16 categorised sample directories, each with `.ploy`, `.cpp`, `.py`, `.rs`, `.java`, and/or `.cs` source files:

```
tests/samples/
├── README.md                           # Sample overview
├── 01_basic_linking/                   # Basic LINK + CALL interop
│   ├── basic_linking.ploy
│   ├── math_bridge.cpp
│   ├── math_bridge.py
│   └── math_bridge.rs
├── 02_type_mapping/                    # Structs, container type mapping
├── 03_pipeline/                        # Multi-stage PIPELINE
├── 04_package_import/                  # IMPORT with version constraints + CONFIG
├── 05_class_instantiation/             # NEW/METHOD cross-language OOP
├── 06_attribute_access/                # GET/SET attribute access
├── 07_resource_management/             # WITH resource management
├── 08_delete_extend/                   # DELETE/EXTEND object lifecycle
├── 09_mixed_pipeline/                  # Combined ML pipeline (C++/Python/Rust)
├── 10_error_handling/                  # Error scenarios and diagnostics
├── 11_java_interop/                    # Java interop (NEW/METHOD)
├── 12_dotnet_interop/                  # .NET interop (NEW/METHOD)
├── 13_generic_containers/              # Generic container interop
├── 14_async_pipeline/                  # Async multi-stage signal processing
├── 15_full_stack/                      # Five-language full-stack
└── 16_config_and_venv/                 # Environment config, package versions
```

## 10.5 Integration Tests

The `tests/integration/` directory contains 52 integration tests across 3 categories:

```
tests/integration/
├── compile_tests/
│   └── compile_pipeline_test.cpp       # 17 tests: full compilation pipeline
├── interop_tests/
│   └── interop_test.cpp                # 14 tests: cross-language interop
└── performance/
    └── perf_test.cpp                   # 14 tests: performance stress
```

### Integration Test Categories

| Category | Tag | Tests | Coverage |
|----------|-----|-------|----------|
| Compile Pipeline | `[integration][compile]` | 17 | LINK+CALL, STRUCT, PIPELINE, IF/ELSE/WHILE/FOR, NEW/METHOD, GET/SET, WITH, DELETE, EXTEND, MATCH, ML pipeline, multi-function PIPELINE |
| Cross-Language Interop | `[integration][interop]` | 14 | LINK chains, NEW creation, METHOD chains, lifecycle (NEW→METHOD→DELETE), multi-lang objects, GET/SET, WITH/nested, EXTEND, combined OOP, three-language |
| Performance Stress | `[integration][perf]` | 14 | 50/100 functions, 10/20-level nesting, 50/100 CALLs, 20/50-stage pipelines, complex mixed program, lexer/parser throughput |

## 10.6 Benchmark Tests

The `tests/benchmarks/` directory contains 18 benchmark tests across 2 categories:

```
tests/benchmarks/
├── micro/
│   └── micro_bench.cpp                 # 8 tests: per-stage benchmarks
└── macro/
    └── macro_bench.cpp                 # 10 tests: full pipeline throughput
```

### Micro-Benchmarks

Measure individual compiler stage throughput with warmup and statistical reporting:

| Test | Description |
|------|-------------|
| Lexer small/medium/large | Token throughput for 3 program sizes |
| Parser small/medium/large | AST construction throughput |
| Sema medium | Semantic analysis throughput |
| Lowering medium | IR generation throughput |

### Macro-Benchmarks

Measure full pipeline (lex → parse → sema → lower → IR print) performance:

| Test | Description |
|------|-------------|
| Pipeline 10/50/100/200 funcs | Throughput scaling with function count |
| Scaling linearity check | Verify sub-quadratic scaling (2x input < 5x time) |
| OOP 10/25 classes | EXTEND + NEW + METHOD + DELETE performance |
| Pipeline 10×5 / 20×10 | Multi-stage pipeline with per-stage CALLs |
| IR size growth | Verify IR output size grows linearly with program size |

---

# 11. Build & Integration

## 11.1 CMake Build Targets

| Target | Type | Main Dependencies |
|--------|------|-------------------|
| `polyglot_common` | Static library | fmt, nlohmann_json (7 compilation units, incl. dwarf_builder) |
| `frontend_common` | Static library | polyglot_common |
| `frontend_cpp` | Static library | frontend_common (5 compilation units: lexer/parser/sema/lowering/constexpr) |
| `frontend_python` | Static library | frontend_common (4 compilation units) |
| `frontend_rust` | Static library | frontend_common (4 compilation units) |
| `frontend_java` | Static library | frontend_common (4 compilation units: lexer/parser/sema/lowering) |
| `frontend_dotnet` | Static library | frontend_common (4 compilation units: lexer/parser/sema/lowering) |
| `frontend_ploy` | Static library | frontend_common, middle_ir (6 compilation units) |
| `middle_ir` | Static library | polyglot_common (15 compilation units) |
| `backend_x86_64` | Static library | polyglot_common (7 compilation units) |
| `backend_arm64` | Static library | polyglot_common (6 compilation units) |
| `backend_wasm` | Static library | polyglot_common, middle_ir (1 compilation unit) |
| `runtime` | Static library | — (23 compilation units, incl. java_rt/dotnet_rt/object_lifecycle) |
| `linker_lib` | Object library | polyglot_common, frontend_ploy |
| `polyc` | Executable | All frontends + backends + IR + runtime |
| `polyld` | Executable | polyglot_common, frontend_ploy |
| `polyasm` | Executable | Backends + IR |
| `polyopt` | Executable | middle_ir + backends |
| `polyrt` | Executable | runtime |
| `polybench` | Executable | All |
| `polyui` | Executable | All frontends + Qt6::Widgets (optional, requires Qt) |
| `unit_tests` | Executable | All + Catch2 |
| `integration_tests` | Executable | All + Catch2 |
| `benchmark_tests` | Executable | All + Catch2 |

## 11.2 Dependency Management

Dependencies are auto-fetched via `Dependencies.cmake` using `FetchContent`:

| Dependency | Purpose |
|-----------|---------|
| fmt | Formatted output |
| nlohmann_json | JSON processing |
| Catch2 | Unit testing framework |
| mimalloc | High-performance memory allocation |

## 11.3 Known Issues and Fixes

| Issue | Cause | Fix |
|-------|-------|-----|
| x86/x64 linker mismatch | VsDevCmd defaults to x86 | Use `-arch=amd64` |
| Struct literal parsing ambiguity | `Ident {` may be struct or block | LexerBase `SaveState()/RestoreState()` lookahead |
| Sema type name case | `INT` vs `int` vs `i32` | `ResolveType` supports multiple case variants |
| Test pip command hangs | Windows stdin waiting | Test commands use `<nul` redirection |

---

# 12. Developer Guide

## 12.1 Adding a New .ploy Keyword

1. Add a new token to the `TokenKind` enum in `ploy_lexer.h`
2. Add the string → token mapping in the keyword map in `lexer.cpp`
3. Add corresponding parsing logic in `parser.cpp`
4. Add semantic checks in `sema.cpp`
5. Add IR generation in `lowering.cpp`
6. Add tests in `ploy_test.cpp` (lexer + parser + sema + IR + integration)

## 12.2 Adding a New Package Manager

1. Add a new value to `VenvConfigDecl::ManagerKind` enum in `ploy_ast.h`
2. Add the corresponding keyword in `lexer.cpp`
3. Add a new branch in `ParseConfigDecl` in `parser.cpp`
4. Add `DiscoverPythonPackagesViaXxx()` method declaration in `ploy_sema.h`
5. Implement discovery logic in `sema.cpp`, add dispatch in `DiscoverPythonPackages`
6. Add test cases

## 12.3 Code Style

- **C++20** standard
- English code comments
- Use `namespace polyglot::ploy` (or `polyglot::python`, etc.)
- Class names: `PascalCase`
- Method names: `PascalCase` (e.g., `ParseFuncDecl`)
- Variable names: `snake_case`
- Member variables: `trailing_underscore_`
- Use `core::SourceLoc` for error location (note: use `file` field, not `filename`)
- Use `core::Type` type system (note: `core::Type::Any()`, not `core::Type::Function`)

## 12.4 Documentation Standards

- All documentation is provided in both Chinese and English
- Chinese filenames have `_zh` suffix, English has no suffix
- Documentation is stored in the `docs/realization/` directory

---

# 13. Plugin System

PolyglotCompiler supports dynamic plugins via a stable **C ABI** interface. Plugins are shared libraries that register themselves with the host at load time and can extend the compiler or IDE with new languages, optimisation passes, backends, linters, formatters, code actions, syntax themes, and more.

## 13.1 Architecture

```
┌─────────────────────────────────────────────────────┐
│               PolyglotCompiler Host                    │
│  ┌────────────────┐  ┌──────────────┐  ┌───────────┐  │
│  │ PluginManager  │  │ Host Services│  │ polyui   │  │
│  │ (discover,     │  │ (log, diag,  │  │ (settings│  │
│  │  load, unload) │  │  settings,  │  │  dialog) │  │
│  └────────┬───────┘  │  open_file) │  └───────────┘  │
│           │          └──────┬───────┘                │
└───────────┼────────────┼─────────────────────┘
           │            │        C ABI boundary
     ┌─────┴──────────┴─────┐
     │  Plugin (.so/.dll/.dylib) │
     │  polyplug_<name>          │
     │  polyglot_plugin_info()   │
     │  polyglot_plugin_init()   │
     │  polyglot_plugin_shutdown │
     └──────────────────────────┘
```

- **C ABI**: All exported functions use `extern "C"` linkage for binary compatibility across compilers and platforms.
- **Naming convention**: Plugin shared libraries must be named `polyplug_<name>` (e.g. `polyplug_myformatter.so`).
- **Discovery**: `PluginManager` scans search paths for matching files and loads them via `dlopen` / `LoadLibrary`.

## 13.2 Capability Flags

Each plugin declares a bitmask of capabilities in its `PolyglotPluginInfo`:

| Flag | Value | Description |
|------|-------|-------------|
| `POLYGLOT_CAP_LANGUAGE` | `1 << 0` | Language frontend |
| `POLYGLOT_CAP_OPTIMIZER` | `1 << 1` | Optimisation pass |
| `POLYGLOT_CAP_BACKEND` | `1 << 2` | Code-generation backend |
| `POLYGLOT_CAP_TOOL` | `1 << 3` | CLI tool / pipeline stage |
| `POLYGLOT_CAP_UI_PANEL` | `1 << 4` | IDE panel / dock widget |
| `POLYGLOT_CAP_SYNTAX_THEME` | `1 << 5` | Syntax highlighting theme |
| `POLYGLOT_CAP_FILE_TYPE` | `1 << 6` | File type registration |
| `POLYGLOT_CAP_CODE_ACTION` | `1 << 7` | Quick-fix / refactoring |
| `POLYGLOT_CAP_FORMATTER` | `1 << 8` | Code formatter |
| `POLYGLOT_CAP_LINTER` | `1 << 9` | Code linter |
| `POLYGLOT_CAP_DEBUGGER` | `1 << 10` | Debugger integration |

## 13.3 Mandatory Plugin Exports

Every plugin must export these three functions:

```c
// Return static plugin metadata (name, version, author, capabilities).
PolyglotPluginInfo polyglot_plugin_info(void);

// Initialise the plugin. The host passes a PolyglotHostServices struct
// containing function pointers for logging, diagnostics, settings, etc.
// Return 0 on success, non-zero on failure.
int polyglot_plugin_init(PolyglotHostServices services);

// Clean up resources before unload.
void polyglot_plugin_shutdown(void);
```

## 13.4 Optional Plugin Exports

Plugins may optionally export provider functions depending on their declared capabilities:

```c
// Language frontend — return a PolyglotLanguageProvider struct.
PolyglotLanguageProvider polyglot_plugin_get_language(void);

// Optimiser pass — return a PolyglotOptimizerPass struct.
PolyglotOptimizerPass polyglot_plugin_get_optimizer(void);

// Code action — return a PolyglotCodeAction struct.
PolyglotCodeAction polyglot_plugin_get_code_action(void);

// Formatter — return a PolyglotFormatter struct.
PolyglotFormatter polyglot_plugin_get_formatter(void);

// Linter — return a PolyglotLinter struct.
PolyglotLinter polyglot_plugin_get_linter(void);
```

## 13.5 Host Services

The host provides the following services to plugins through `PolyglotHostServices`:

| Service | Signature | Purpose |
|---------|-----------|----------|
| `log` | `void (*)(void*, int, const char*)` | Log a message at a given severity level |
| `emit_diagnostic` | `void (*)(void*, const PolyglotDiagnostic*)` | Emit a compiler diagnostic |
| `get_setting` | `int (*)(void*, const char*, char*, int)` | Read a host setting by key |
| `set_setting` | `void (*)(void*, const char*, const char*)` | Write a host setting |
| `open_file` | `void (*)(void*, const char*, int)` | Request the IDE to open a file at a line |

## 13.6 Plugin File Locations

Plugins are discovered from the following search paths:

| Platform | System Path | User Path |
|----------|------------|------------|
| **macOS** | `<app>/plugins/` | `~/Library/Application Support/PolyglotCompiler/plugins/` |
| **Linux** | `<app>/plugins/` | `~/.local/share/PolyglotCompiler/plugins/` |
| **Windows** | `<app>\plugins\` | `%APPDATA%\PolyglotCompiler\plugins\` |

Plugins can also be loaded manually from the **Settings → Plugins** page in `polyui`.

## 13.7 Quick Example (C)

```c
#include "plugins/plugin_api.h"

static PolyglotHostServices host;

PolyglotPluginInfo polyglot_plugin_info(void) {
    PolyglotPluginInfo info = {0};
    info.api_version    = POLYGLOT_PLUGIN_API_VERSION;
    info.name           = "My Linter";
    info.version        = "1.0.0";
    info.author         = "Your Name";
    info.description    = "A sample linter plugin";
    info.capabilities   = POLYGLOT_CAP_LINTER;
    return info;
}

int polyglot_plugin_init(PolyglotHostServices services) {
    host = services;
    host.log(host.context, 0, "My Linter plugin loaded");
    return 0;
}

void polyglot_plugin_shutdown(void) {
    host.log(host.context, 0, "My Linter plugin unloaded");
}
```

For the complete specification, see [`docs/specs/plugin_specification.md`](specs/plugin_specification.md).

---

# 14. Appendix

## 14.1 Glossary

| Term | Full Name | Explanation |
|------|-----------|-------------|
| AST | Abstract Syntax Tree | Tree representation of source code |
| IR | Intermediate Representation | Shared internal representation |
| SSA | Static Single Assignment | Each variable assigned exactly once |
| CFG | Control Flow Graph | Graph of basic blocks |
| DCE | Dead Code Elimination | Remove unreachable code |
| CSE | Common Subexpression Elimination | Reuse computed values |
| GVN | Global Value Numbering | Global redundancy elimination |
| RTTI | Run-Time Type Information | Runtime type inspection |
| FFI | Foreign Function Interface | Call functions in other languages |
| PGO | Profile-Guided Optimization | Optimise using runtime data |
| LTO | Link-Time Optimization | Optimise across modules |
| LICM | Loop-Invariant Code Motion | Hoist invariant computations out of loops |
| DSL | Domain-Specific Language | Language for a specific domain |
| POBJ | Polyglot Object | PolyglotCompiler custom object file format |

## 14.2 .ploy Keyword Quick Reference

| Keyword | Purpose | Example |
|---------|---------|---------|
| `LINK` | Cross-language function linking | `LINK(cpp, python, f, g);` |
| `IMPORT` | Module/package import | `IMPORT python PACKAGE numpy >= 1.20;` |
| `EXPORT` | Symbol export | `EXPORT func AS "name";` |
| `MAP_TYPE` | Type mapping | `MAP_TYPE(cpp::int, python::int);` |
| `PIPELINE` | Multi-stage pipeline | `PIPELINE name { ... }` |
| `FUNC` | Function declaration | `FUNC f(x: INT) -> INT { ... }` |
| `LET` / `VAR` | Variable declaration | `LET x = 42;` / `VAR y = 0;` |
| `CALL` | Cross-language call | `CALL(python, np::mean, data)` |
| `STRUCT` | Struct definition | `STRUCT Point { x: f64, y: f64 }` |
| `MAP_FUNC` | Mapping function | `MAP_FUNC convert(x: INT) -> FLOAT { ... }` |
| `CONVERT` | Type conversion | `CONVERT(value, FLOAT)` |
| `NEW` | Cross-language class instantiation | `NEW(python, torch::nn::Linear, 784, 10)` |
| `METHOD` | Cross-language method call | `METHOD(python, model, forward, data)` |
| `GET` | Cross-language attribute access | `GET(python, model, weight)` |
| `SET` | Cross-language attribute assignment | `SET(python, model, training, FALSE)` |
| `WITH` | Automatic resource management | `WITH(python, f) AS handle { ... }` |
| `DELETE` | Cross-language object destruction | `DELETE(python, obj)` |
| `EXTEND` | Cross-language class extension | `EXTEND(python, Base) AS Derived { ... }` |
| `CONFIG` | Environment configuration | `CONFIG CONDA "env";` |
| `VENV` | pip/venv environment | `CONFIG VENV python "/path";` |
| `CONDA` | Conda environment | `CONFIG CONDA "env_name";` |
| `UV` | uv-managed environment | `CONFIG UV python "/path";` |
| `PIPENV` | Pipenv project | `CONFIG PIPENV "project_path";` |
| `POETRY` | Poetry project | `CONFIG POETRY "project_path";` |

## 14.3 Version Operators

| Operator | Meaning | Example |
|----------|---------|---------|
| `>=` | Greater or equal | `>= 1.20` |
| `<=` | Less or equal | `<= 2.0` |
| `==` | Exact match | `== 1.10.0` |
| `>` | Strictly greater | `> 1.0` |
| `<` | Strictly less | `< 3.0` |
| `~=` | Compatible version (PEP 440) | `~= 1.20` → `>= 1.20, < 2.0` |

## 14.4 References

- "Compilers: Principles, Techniques, and Tools" (Dragon Book)
- "Engineering a Compiler" (Whale Book)
- LLVM: https://llvm.org/
- Rust Compiler: https://github.com/rust-lang/rust
- PEP 440: https://peps.python.org/pep-0440/

## 14.5 Release Packaging

PolyglotCompiler provides platform-specific scripts to build release packages:

| Platform | Script | Output |
|----------|--------|--------|
| Windows | `scripts/package_windows.ps1` | Portable `.zip` + NSIS installer `.exe` |
| Linux | `scripts/package_linux.sh` | Portable `.tar.gz` |
| macOS | `scripts/package_macos.sh` | Portable `.tar.gz` (with `.app` bundle) |

```bash
# Windows (PowerShell)
.\scripts\package_windows.ps1

# Linux
./scripts/package_linux.sh

# macOS
./scripts/package_macos.sh
```

See `docs/specs/release_packaging.md` for full details, prerequisites, and version management.

## 14.6 Changelog

### v1.0.5 (2026-03-17)

**Documentation Single-Sourcing & Auto-Verification (2026-03-17-7)**
- ✅ Fixed path references: README.md, USER_GUIDE.md, USER_GUIDE_zh.md, `setup_qt.sh`, `setup_qt.ps1` all now reference correct `tools/ui/setup_qt.*` paths (was `scripts/setup_qt.*`)
- ✅ Fixed dead links: `plugin_specification.md` / `plugin_specification_zh.md` links in USER_GUIDE now resolve correctly
- ✅ Fixed `language_spec.md` / `language_spec_zh.md`: runtime bridge header paths updated to `runtime/include/libs/*.h`
- ✅ Fixed `project_tutorial.md` / `project_tutorial_zh.md`: driver path updated to `tools/polyc/src/driver.cpp`
- ✅ New `scripts/docs_lint.py`: comprehensive documentation linter with 9 check categories:
  - DL001: Path reference validation (backtick-quoted paths must exist in repo)
  - DL002/DL003: Bilingual pairing — every `*.md` must have `*_zh.md` counterpart
  - DL004: Heading structure sync — EN/ZH heading counts per level must match
  - DL005: Dead link detection — relative Markdown links must resolve
  - DL006: Version consistency — version strings across docs must agree
  - DL007: Orphan detection — docs not referenced from README or USER_GUIDE
  - DL008: TODO/FIXME markers in published docs
  - DL009: Placeholder text detection
- ✅ New `scripts/docs_generate.py`: single-source documentation generator with template markers (`<!-- BEGIN:section_name -->` / `<!-- END:section_name -->`); supports `--apply` / `--check` modes; patches test badges, Qt paths, dependency tables, version footers from `docs/_variables.json`
- ✅ New `scripts/docs_sync_check.py`: bilingual synchronisation checker comparing heading structures and content length divergence between EN/ZH pairs
- ✅ New `scripts/check_include_deps.py`: include dependency layer constraint enforcer (middle→common/ir, backends→frontends, frontends→backends)
- ✅ New `docs/_variables.json`: single source of truth for version numbers, test counts, paths, dependency info, tool names — eliminates manual multi-file updates
- ✅ Template markers inserted in README.md (`test_badge`, `qt_setup_en`, `dependencies_table`, `version_footer_en`) and USER_GUIDE footers (`version_footer_en`, `version_footer_zh`)
- ✅ CI integration: added `docs-lint` job to `.github/workflows/ci.yml` running `docs_lint.py --ci`, `docs_sync_check.py --ci`, and `docs_generate.py --check` on every push/PR
- ✅ Updated test badge from 813 → 808 (accurate count); version footer updated to v1.0.4 / 2026-03-17
- ✅ All 808 unit tests passing (34085 assertions); 51/52 integration tests passing

### v1.0.4 (2026-03-17)

**Plugin System & UI Extensibility (2026-03-17-6)**
- ✅ Plugin host callbacks fully implemented: `emit_diagnostic` forwards diagnostics to IDE output panel and fires `DIAGNOSTIC` event to subscribers; `open_file` delegates to registered `OpenFileCallback` (wired to IDE tab manager); `register_file_type` populates central file-type registry
- ✅ Event subscription system: plugins can subscribe to 8 event types (`FILE_OPENED`, `FILE_SAVED`, `FILE_CLOSED`, `BUILD_STARTED`, `BUILD_FINISHED`, `DIAGNOSTIC`, `WORKSPACE_CHANGED`, `THEME_CHANGED`) via `subscribe_event` / `unsubscribe_event` host services
- ✅ `PluginManager::FireEvent()`: thread-safe dispatch — snapshots subscribers under lock, delivers events outside lock to avoid re-entry deadlocks
- ✅ Convenience fire helpers: `FireFileOpened()`, `FireFileSaved()`, `FireBuildStarted()`, `FireBuildFinished()`, `FireWorkspaceChanged()`, `FireThemeChanged()`
- ✅ File type registry: `RegisterFileType()` / `GetLanguageForExtension()` / `GetRegisteredFileTypes()` — plugins can add new file-type associations at runtime
- ✅ Menu contribution system: `RegisterMenuItem()` / `UnregisterMenuItem()` / `GetMenuContributions()` / `ExecuteMenuAction()` — plugins can add IDE menu items with callbacks
- ✅ `PolyglotEvent` struct and `PFN_polyglot_plugin_on_event` export — plugins implement a single event handler for all subscribed events
- ✅ `PolyglotMenuContribution` struct with `menu_path`, `action_id`, `label`, `shortcut`, and `callback`
- ✅ Extended `PolyglotHostServices` with 4 new function pointers: `subscribe_event`, `unsubscribe_event`, `register_menu_item`, `unregister_menu_item`
- ✅ New `ActionManager` class: centralized action/keybinding management extracted from MainWindow; supports plugin-contributed actions with `RegisterPluginAction()` / `RemovePluginActions()`; keybinding persistence via `LoadKeybindings()` / `SaveKeybindings()`
- ✅ New `PanelManager` class: manages bottom panel tabs; supports plugin-contributed panels via `RegisterPluginPanel()` / `RemovePluginPanels()`; unified `TogglePanel()` / `ShowPanel()` / `IsPanelActive()` API replacing scattered toggle logic
- ✅ MainWindow refactored: panel toggle/show logic delegates to `PanelManager`; keybinding management delegates to `ActionManager`; plugin lifecycle events wired to file open/save/compile/workspace/theme changes
- ✅ Plugin diagnostic forwarding: `SetDiagnosticCallback()` wired to IDE output panel
- ✅ Plugin menu item forwarding: `SetMenuItemRegisteredCallback()` wired to `ActionManager`
- ✅ Plugin file-type forwarding: `SetFileTypeRegisteredCallback()` wired to IDE output log
- ✅ 12 new unit tests: event type distinctness, event struct zero-init, menu contribution struct, subscribe/unsubscribe safety, fire-event-no-subscribers, file type registry CRUD, menu contributions empty, ExecuteMenuAction unknown, DispatchOpenFile with/without callback, FileType registered callback, diagnostic callback
- ✅ Test count: 796 → 808 test cases; assertions: 34056 → 34085 — all passing

### v1.0.3 (2026-03-17)

**Test Quality Improvements (2026-03-17-5)**
- ✅ Replaced `REQUIRE(true)` in `lto_test.cpp` with optimizer statistics behavior assertions (all stats == 0 for empty module, module survives all optimization stages)
- ✅ Replaced `REQUIRE(true)` in `gc_algorithms_test.cpp` with GC stats verification: `collections >= 10`, `total_allocations >= 500` for multi-cycle test; `total_allocations >= 200` for incremental collection
- ✅ Replaced commented-out performance benchmarks with real throughput assertions (`total_allocations >= 10000`, `collections >= 1`)
- ✅ Replaced `SUCCEED()` in `java_test.cpp` and `dotnet_test.cpp` with semantic analysis behavior checks (verifying diagnostics are type-mapping related, not crashes)
- ✅ Replaced `REQUIRE(true)` in `threading_services_test.cpp` with phase-tracking atomics for barrier reset and exact-sum verification for large workloads
- ✅ Strengthened Java/DotNet enum and struct lowering tests with positive diagnostic-state assertions
- ✅ New `compilation_behavior_test.cpp`: 15 new behavior-level and failure-path test cases:
  - C++ single/multiple function IR output correctness
  - C++ conditional produces multiple basic blocks
  - x86_64 assembly contains function label and `ret` instruction
  - x86_64 object code has `.text` section with non-zero bytes and function symbol
  - ARM64 assembly output contains function label
  - IR verification passes on well-formed IR; catches malformed (empty function) IR
  - Ploy `LINK` produces correct cross-language call descriptors
  - Ploy `FUNC` produces named IR function
  - Failure paths: parse error diagnostics, sema error on undefined variable, lowering invalid code returns failure
- ✅ Test count: 781 → 796 test cases; assertions: 3985 → 34056 — all passing

### v1.0.2 (2026-03-17)

**Package Discovery Refactoring (2026-03-17-3)**
- ✅ `CommandResult` struct: structured output with `stdout_output`, `exit_code`, `timed_out`, `failed`, `Ok()` convenience method
- ✅ `ICommandRunner::RunWithResult()`: new primary interface with configurable per-command timeout; `Run()` kept for backward compatibility and delegates to `RunWithResult()`
- ✅ `DefaultCommandRunner`: timeout support via `std::async` + `std::future::wait_for()`; configurable default timeout (10s)
- ✅ `PackageIndexer` class: explicit pre-compilation package-index phase, decoupled from sema; runs discovery commands with timeouts and retries, populates shared `PackageDiscoveryCache`
- ✅ `PackageIndexerOptions`: configurable `command_timeout`, `max_retries`, `verbose` flag
- ✅ `PackageIndexer::Stats`: tracks `languages_indexed`, `commands_executed`, `commands_timed_out`, `commands_failed`, `packages_found`, `total_duration`
- ✅ `PackageIndexer::SetProgressCallback()`: optional per-language progress reporting during indexing
- ✅ `PloySemaOptions::enable_package_discovery` default changed from `true` to `false` — sema never shells out; callers use `PackageIndexer` as pre-phase
- ✅ `polyc` driver: PackageIndexer wired as Phase 2.5 before sema; scans AST for referenced languages and indexes only those
- ✅ CLI flags: `--package-index` (default), `--no-package-index`, `--pkg-timeout=<ms>`
- ✅ 10 new test cases: `CommandResult::Ok()`, `MockCommandRunner` structured result, timeout simulation, `PackageIndexer` cache population, cache skip, timeout handling, multi-language indexing, pre-phase → sema cache flow, progress callback, defaults verification

**Build System Modularization (2026-03-17-4)**
- ✅ Top-level `CMakeLists.txt` reduced from ~690 lines to ~80 lines — project setup, compiler flags, dependencies, and `add_subdirectory()` calls only
- ✅ `common/CMakeLists.txt`: `polyglot_common` library (type system, symbol table, plugins, debug info)
- ✅ `middle/CMakeLists.txt`: `middle_ir` library (IR, optimization passes, PGO, LTO)
- ✅ `frontends/CMakeLists.txt`: `frontend_common` + 6 language frontend libraries
- ✅ `backends/CMakeLists.txt`: `backend_x86_64`, `backend_arm64`, `backend_wasm`
- ✅ `runtime/CMakeLists.txt`: `runtime` library (GC, FFI, interop, services)
- ✅ `tools/CMakeLists.txt`: `polyc`, `polyasm`, `polyld`, `polyopt`, `polyrt`, `polybench`, `polyui` (with full Qt detection logic)
- ✅ `tests/CMakeLists.txt`: explicit per-module source lists replacing `GLOB_RECURSE` for unit tests; `integration_tests` and `benchmark_tests` retained with scoped globs
- ✅ Test count: 781 test cases / 3985 assertions — all passing

### v1.0.1 (2026-03-17)
- ✅ Two explicit compilation modes: **strict** and **permissive** (`--strict` / `--permissive` CLI flags)
- ✅ Release builds default to strict mode via `POLYC_DEFAULT_STRICT` compile definition
- ✅ Strict mode: semantic analysis reports placeholder-type fallbacks as errors instead of warnings
- ✅ Strict mode: IR verifier rejects functions with unresolved placeholder I64 return types
- ✅ Strict mode: lowering rejects cross-language calls with unknown return types
- ✅ Strict mode: degraded stubs via `--force` are blocked; `--strict` and `--force` are mutually exclusive
- ✅ Permissive mode: all placeholder-type fallbacks visible as warnings (previously silent)
- ✅ `ReportStrictDiag()` helper: routes diagnostics to error (strict) or warning (permissive)
- ✅ `IRType::is_placeholder` flag distinguishes genuine I64 from unresolved fallback I64

### v1.0.0 (2026-03-15)
- ✅ Project version unified to **1.0.0** (all previous v5.x/v4.x versions renumbered to v0.5.x/v0.4.x)
- ✅ Release packaging scripts for all 3 platforms (`scripts/package_windows.ps1`, `scripts/package_linux.sh`, `scripts/package_macos.sh`)
- ✅ Windows: portable ZIP archive + NSIS installer (`scripts/installer.nsi`) with PATH registration and Start Menu shortcuts
- ✅ Linux: portable `.tar.gz` with Qt library bundling and wrapper script
- ✅ macOS: portable `.tar.gz` with `macdeployqt` integration for `.app` bundle
- ✅ Packaging documentation (`docs/specs/release_packaging.md` / `_zh.md`)
- ✅ Version references updated across CMakeLists.txt, all tool source files, README.md, and USER_GUIDE (EN/ZH)

### v0.5.5 (2026-03-15)
- ✅ Unified theme system via `ThemeManager` singleton — 4 built-in colour schemes (Dark, Light, Monokai, Solarized Dark); all panels (main window, build, debug, git, settings) apply styles from a single source
- ✅ Compile & Run (`Ctrl+R`): QProcess-based workflow compiles current file, locates output binary, and launches it with stdout/stderr streamed to the log panel
- ✅ Stop (`Ctrl+Shift+R`): terminates a running process and cancels an active build
- ✅ Debug panel: full call-stack frame parsing for both lldb and GDB/MI output (module, function, file, line); variable type/value extraction; watch expression evaluation with live result updates
- ✅ Debug panel: watch context menu (Remove / Remove All / Evaluate) wired to `OnRemoveWatch` and `OnEvaluateWatch`
- ✅ Debug panel: configurable debugger path and break-on-entry option from Settings
- ✅ Custom keybindings: editable via `QKeySequenceEdit` in Settings → Key Bindings page; 28 default actions; custom shortcuts persisted in `QSettings` and applied at startup
- ✅ Settings linkage: CMake path, build directory, and debugger path automatically propagate to `BuildPanel` and `DebugPanel` on apply
- ✅ New source file `theme_manager.cpp` / `theme_manager.h` added to `tools/ui/common/`

### v0.5.4 (2026-03-11)
- ✅ Qt-based desktop IDE (`polyui`): syntax highlighting, real-time diagnostics, file browser, tabbed editor, output panel, bracket matching, dark theme
- ✅ IDE uses compiler frontend tokenizers for accurate, language-aware highlighting across all 6 supported languages
- ✅ IDE keyboard shortcuts: Ctrl+B compile, Ctrl+Shift+B analyze, Ctrl+N/O/S/W file management
- ✅ CMake Qt6/Qt5 auto-discovery: prefers standalone Qt installation at `D:\Qt` over bundled copies; pass `-DQT_ROOT=<path>` to override
- ✅ Default `ninja` build now generates all executables (polyc, polyld, polyasm, polyopt, polyrt, polybench, polyui)
- ✅ Platform-separated `polyui` source layout: shared code in `tools/ui/common/`, platform-specific entry points in `tools/ui/windows/`, `tools/ui/linux/`, `tools/ui/macos/`; CMake auto-selects the correct `main.cpp` per OS
- ✅ macOS support: `MACOSX_BUNDLE` with `macdeployqt` post-build; Linux support: `xcb` platform default, `.desktop` integration
- ✅ Integrated terminal in the IDE: embedded shell (PowerShell/bash/zsh), ANSI colour parsing, command history, multiple instances, `Ctrl+\`` toggle
- ✅ Cleaned up legacy `tools/ui/ployui_windows/`, `tools/ui/src/`, and `tools/ui/include/` directories
- ✅ Full documentation audit and statistics refresh — 293 source files, 91,457 lines of code
- ✅ Test growth: 743 unit (was 734) + 52 integration (was 50) + 18 benchmark = **813 total** (was 802)
- ✅ Ploy frontend expanded to 6 compilation units (added `command_runner.cpp`, `package_discovery_cache.cpp`)
- ✅ Updated all documentation (README, USER_GUIDE EN/ZH, tutorials) with current statistics

### v0.5.2 (2026-02-22)
- ✅ New `PloySemaOptions` configuration struct with `enable_package_discovery` switch
- ✅ `PloySema` constructor accepts options; backward-compatible default constructor preserved
- ✅ Session-level `PackageDiscoveryCache` — thread-safe, keyed by `language|manager|env_path`
- ✅ `ICommandRunner` abstraction — external command execution decoupled from `_popen`; test-mockable
- ✅ `DiscoverPackages` cache-first flow — hit → merge cached results; miss → run + store
- ✅ Benchmark suites disable package discovery, isolating compiler-only performance
- ✅ Lowering micro-benchmark timing fix — parse/sema excluded from timing window
- ✅ Benchmark fast/full tiers via `POLYBENCH_MODE` env var (`fast`/`full`/default)
- ✅ CTest: `benchmark_fast` and `benchmark_full` targets with labels
- ✅ Discovery unit tests: disabled-no-cmd, repeated-analyze-once, key-isolation, cache-roundtrip

### v0.5.1 (2026-02-22)
- ✅ Linker strong failure mode — unresolved symbols are hard errors; placeholder stubs no longer generated
- ✅ Linker main flow fatal when cross-language entries exist but resolution fails
- ✅ `polyc` and `polyasm` now accept `--arch=wasm` for WebAssembly target
- ✅ WASM backend: real function call index resolution via name→index map (no more hardcoded indices)
- ✅ WASM backend: alloca lowered to shadow stack model (mutable i32 global, grows downward from 65536)
- ✅ WASM backend: unsupported IR instructions emit `unreachable` + diagnostic error (no silent NOPs)
- ✅ `.ploy` sema strict mode (`SetStrictMode(true)`) — warns on untyped params, `Any` fallbacks, missing annotations
- ✅ `.ploy` lowering consults sema symbol table before I64 fallback; uses `KnownSignatures` for return/param types
- ✅ Debug emitter: FDE now has proper CFA instructions (push rbp / mov rbp,rsp frame setup)
- ✅ Debug emitter: PDB TPI stream emits LF_POINTER type records for pointer types
- ✅ Mach-O object file: per-section `reloff`/`nreloc` computed and written; relocation_info entries emitted
- ✅ Rust and Python frontend lowering: diagnostic warnings for unknown type → I64 fallbacks
- ✅ Rust advanced_features_test.cpp: all `REQUIRE(true)` replaced with behavioral parse + AST assertions
- ✅ E2E tests: added ARM64 object code emission test and WASM multi-function binary smoke test

### v0.5.0 (2026-02-22)
- ✅ Full project documentation update — all docs refreshed to reflect current state
- ✅ WebAssembly (WASM) backend added (`backends/wasm/`)
- ✅ Unified DWARF builder compiled into `polyglot_common` library
- ✅ PDB emission: MSF block layout, TPI type records (LF_ARGLIST/LF_PROCEDURE), RFC 4122 v4 GUID
- ✅ CFA initialisation with register save rules and NOP alignment
- ✅ ELF: e_machine architecture switching (x86_64/ARM64), full SHT_RELA relocation sections
- ✅ Mach-O: complete LC_SEGMENT_64, LC_SYMTAB, nlist_64 symbol table, section mapping
- ✅ IR parser/printer symmetry: `fadd/fsub/fmul/fdiv/frem` parsing + global/const declarations
- ✅ Preprocessor test coverage: 18 dedicated test cases for macros, directives, token pool
- ✅ Debug tests Windows-compatible: `TmpPath()` via `std::filesystem::temp_directory_path()`
- ✅ Cross-language link chain fully connected: polyc → PolyglotLinker → glue code → binary
- ✅ Real IR lowering for all 6 frontends (no fallback stubs)
- ✅ E2E tests enabled: 29 end-to-end test cases
- ✅ Linker completeness: COFF/PE, ELF, Mach-O all implemented
- ✅ polyopt: reads IR files and runs optimisation pipeline
- ✅ polybench: full benchmark suite (compilation + E2E)
- ✅ polyrt: FFI subcommand, real GC/thread statistics
- ✅ Tutorial documentation added (`docs/tutorial/`)
- ✅ 16 sample programs (was 12)
- ✅ Total: 813 test cases across 3 suites (743 unit + 52 integration + 18 benchmark)

### v0.4.3 (2026-02-20)
- ✅ Added cross-language object destruction `DELETE` keyword
- ✅ Added cross-language class extension `EXTEND` keyword
- ✅ Enhanced diagnostics infrastructure: severity levels, error codes (1xxx-5xxx), traceback chains, suggestions
- ✅ Added parameter count mismatch checking in semantic analysis
- ✅ Added type mismatch checking in semantic analysis
- ✅ All error reports upgraded to use structured error codes
- ✅ AST / Lexer / Parser / Sema / IR Lowering full chain implementation for DELETE/EXTEND
- ✅ 36 new test cases, total 207 test cases, 598 assertions
- ✅ Keywords count 52 → 54

### v0.4.2 (2026-02-20)
- ✅ Added cross-language attribute access `GET` and assignment `SET` keywords
- ✅ Added automatic resource management `WITH` keyword
- ✅ Added type annotations with qualified types
- ✅ Added interface mapping via `MAP_TYPE` for classes
- ✅ AST / Lexer / Parser / Sema / IR Lowering full chain implementation for GET/SET/WITH
- ✅ 31 new test cases, total 171 test cases, 523 assertions
- ✅ Bilingual documentation (USER_GUIDE.md / USER_GUIDE_zh.md)
- ✅ Keywords count 49 → 52

### v0.4.1 (2026-02-20)
- ✅ Added cross-language class instantiation `NEW` and method call `METHOD` keywords
- ✅ AST / Lexer / Parser / Sema / IR Lowering full chain implementation
- ✅ 22 new test cases, total 140 test cases, 443 assertions
- ✅ Bilingual feature documentation (`class_instantiation.md` / `class_instantiation_zh.md`)
- ✅ Keywords count 47 → 49

### v0.5.3 (2026-02-22)
- ✅ Broke `common` ↔ `middle` circular dependency — canonical IR headers now live in `middle/include/ir/`; `common/include/ir/` contains forwarding shims for backward compatibility
- ✅ Added CI include-lint script (`scripts/check_include_deps.py`) enforcing layer constraints: `middle/` must not include `common/include/ir/`, backends must not include frontends
- ✅ Unified debug-info modelling — added `common/include/debug/debug_info_adapter.h` with `ConvertToBackendDebugInfo()` bridging `polyglot::debug` (rich DWARF model) and `polyglot::backends` (flat emission model)
- ✅ Added optimisation pipeline & switch matrix documentation (`docs/specs/optimization_pipeline.md` / `_zh.md`)
- ✅ Added runtime C ABI reference documentation (`docs/specs/runtime_abi.md` / `_zh.md`)
- ✅ Unified tool-layer namespaces: `polyc` and `polybench` now use `namespace polyglot::tools`, matching `polyasm` / `polyopt` / `polyrt`

### v0.4.0 (2026-02-19)
- ✅ Added complete .ploy cross-language linking frontend chapter
- ✅ Multi-package-manager support: CONFIG CONDA / UV / PIPENV / POETRY
- ✅ Version constraint verification (6 operators)
- ✅ Selective import
- ✅ 117+ test cases, 361+ assertions
- ✅ Comprehensive review and rewrite of the complete guide (v0.3.0 → v0.4.0)
- ✅ Precise description of compilation flow (PolyglotCompiler's own frontends, no external compilers)

### v0.3.0 (2026-02-01)
- ✅ Chapters 11-15 (implementation analysis / testing / advanced optimisations / achievements)
- ✅ 33+ optimisation passes, 4 GC algorithms
- ✅ PGO/LTO/DWARF5 support

### v0.2.0 (2026-01-29)
- ✅ Loop optimisation, GVN, constexpr, DWARF 5

### v0.1.0 (2026-01-15)
- ✅ Basic compilation chain: 3 frontends, 2 backends, basic optimisations

---

<!-- BEGIN:version_footer_en -->
*Maintained by PolyglotCompiler Team*  
*Last Updated: 2026-03-17*  
*Document Version: v1.0.4*
<!-- END:version_footer_en -->
