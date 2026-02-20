# PolyglotCompiler Complete Guide

> A fully-featured multi-language compiler project  
> Supports C++, Python, Rust → x86_64/ARM64  
> With .ploy cross-language linking frontend

**Version**: v4.2  
**Last Updated**: 2026-02-20

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
14. [Appendix](#14-appendix)

---

# 1. Project Overview

## 1.1 Introduction

PolyglotCompiler is a modern multi-language compiler project that uses a multi-frontend shared intermediate representation (IR) architecture, and implements cross-language function-level linking and object-oriented interoperability through the `.ploy` language.

**Core Goals:**

- ✅ **Multi-Language Support**: Complete compilation frontends for C++, Python, Rust
- ✅ **Multi-Target Platforms**: x86_64 and ARM64 architecture backends
- ✅ **Complete Toolchain**: Compiler (`polyc`), linker (`polyld`), optimiser (`polyopt`), assembler (`polyasm`), runtime tool (`polyrt`), benchmark (`polybench`)
- ✅ **Cross-Language Linking**: Declarative syntax via `.ploy` for function-level cross-language interop
- ✅ **Cross-Language OOP**: `NEW` / `METHOD` / `GET` / `SET` / `WITH` keywords for class instantiation, method calls, attribute access, and resource management
- ✅ **Package Manager Integration**: Supports pip/conda/uv/pipenv/poetry/cargo/pkg-config
- ✅ **Production Quality**: Complete implementation, not a minimal prototype

## 1.2 Feature Overview

### Language Support

| Language | Frontend | IR Lowering | Advanced Features | Status |
|----------|----------|-------------|-------------------|--------|
| **C++** | ✅ | ✅ | OOP / Templates / RTTI / Exceptions / constexpr / SIMD | **Complete** |
| **Python** | ✅ | ✅ | Type annotations / Inference / Decorators / Generators / async / 25+ advanced | **Complete** |
| **Rust** | ✅ | ✅ | Borrow checking / Closures / Lifetimes / Traits / 28+ advanced | **Complete** |
| **.ploy** | ✅ | ✅ | Cross-language linking / Pipelines / Package mgmt / OOP interop (NEW/METHOD/GET/SET/WITH) | **Complete** |

### Platform Support

| Architecture | Instruction Selection | Register Allocation | Calling Convention | Status |
|-------------|----------------------|--------------------|--------------------|--------|
| **x86_64** | ✅ | ✅ Graph colouring / Linear scan | ✅ SysV ABI | **Complete** |
| **ARM64** | ✅ | ✅ Graph colouring / Linear scan | ✅ AAPCS64 | **Complete** |

### Toolchain

| Tool | Purpose | Executable |
|------|---------|-----------|
| Compiler driver | Source → IR → Target code | `polyc` |
| Linker | Object file linking + cross-language glue | `polyld` |
| Assembler | Assembly → Object file | `polyasm` |
| Optimiser | IR optimisation passes | `polyopt` |
| Runtime tool | GC / FFI / Thread management | `polyrt` |
| Benchmark | Performance evaluation suite | `polybench` |

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
┌─────────────────────────────────────────────────────────────┐
│                        Source Code                           │
│           C++ / Python / Rust / .ploy                       │
└─────────────────────┬───────────────────────────────────────┘
                      │
        ┌─────────────┼──────────────┬───────────────┐
        │             │              │               │
        ▼             ▼              ▼               ▼
   ┌────────┐   ┌────────┐   ┌────────┐      ┌──────────┐
   │  C++   │   │ Python │   │  Rust  │      │  .ploy   │
   │Frontend│   │Frontend│   │Frontend│      │ Frontend │
   └────┬───┘   └────┬───┘   └────┬───┘      └────┬─────┘
        │            │            │                │
        └────────────┼────────────┘           ┌────┘
                     │                        │
                     ▼                        ▼
            ┌────────────────┐     ┌───────────────────┐
            │  Shared IR     │     │ PolyglotLinker    │
            │ (Middle Layer) │     │ (Cross-Language)  │
            └────────┬───────┘     └───────────────────┘
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
- The `.ploy` frontend is unique in that its IR is consumed by the PolyglotLinker, generating cross-language glue code

## 3.2 Directory Structure

```
PolyglotCompiler/
├── frontends/              # Frontends (4 language frontends)
│   ├── common/             # Shared frontend facilities (Token, Diagnostics, Preprocessor)
│   │   └── src/            #   token_pool.cpp, preprocessor.cpp
│   ├── cpp/                # C++ frontend
│   │   ├── include/        #   cpp_lexer.h, cpp_parser.h, cpp_sema.h, cpp_lowering.h
│   │   └── src/            #   lexer/, parser/, sema/, lowering/, constexpr/
│   ├── python/             # Python frontend
│   │   ├── include/        #   python_lexer.h, python_parser.h, python_sema.h, python_lowering.h
│   │   └── src/            #   lexer/, parser/, sema/, lowering/
│   ├── rust/               # Rust frontend
│   │   ├── include/        #   rust_lexer.h, rust_parser.h, rust_sema.h, rust_lowering.h
│   │   └── src/            #   lexer/, parser/, sema/, lowering/
│   └── ploy/               # .ploy cross-language linking frontend
│       ├── include/        #   ploy_ast.h, ploy_lexer.h, ploy_parser.h, ploy_sema.h, ploy_lowering.h
│       └── src/            #   lexer/, parser/, sema/, lowering/
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
├── backends/               # Backends (2 architecture backends)
│   ├── common/             # Shared backend facilities
│   │   └── src/            #   debug_info.cpp, debug_emitter.cpp, object_file.cpp
│   ├── x86_64/             # x86_64 backend
│   │   ├── include/        #   x86_target.h, x86_register.h, machine_ir.h, instruction_scheduler.h
│   │   └── src/            #   isel/, regalloc/ (graph_coloring, linear_scan), asm_printer/, optimizations, calling_convention
│   └── arm64/              # ARM64 backend
│       ├── include/        #   arm64_target.h, arm64_register.h, machine_ir.h
│       └── src/            #   isel/, regalloc/ (graph_coloring, linear_scan), asm_printer/, calling_convention
├── runtime/                # Runtime
│   ├── include/            # GC, FFI, service interfaces
│   └── src/
│       ├── gc/             #   mark_sweep, generational, copying, incremental, gc_strategy, runtime
│       ├── interop/        #   ffi, memory, marshalling, type_mapping, calling_convention, container_marshal
│       ├── libs/           #   base.c, base_gc_bridge.cpp, python_rt.c, cpp_rt.c, rust_rt.c
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
├── tools/                  # Toolchain (6 executables)
│   ├── polyc/              # Compiler driver (driver.cpp ~1069 lines)
│   ├── polyld/             # Linker (linker.cpp + polyglot_linker.cpp ~522 lines)
│   ├── polyasm/            # Assembler (assembler.cpp)
│   ├── polyopt/            # Optimiser (optimizer.cpp)
│   ├── polyrt/             # Runtime tool (polyrt.cpp)
│   ├── polybench/          # Benchmark (benchmark_suite.cpp)
│   └── ui/                 # UI tool
├── tests/                  # Tests
│   ├── unit/               # Unit tests (Catch2 framework)
│   │   └── frontends/ploy/ #   ploy_test.cpp (171+ test cases)
│   ├── samples/            # Sample programs (10 .ploy files + C++ test files)
│   ├── integration/        # Integration tests
│   └── benchmarks/         # Performance benchmarks
└── docs/                   # Documentation
    ├── realization/        # Implementation docs (6 topics × 2 languages = 12 files)
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
┌─────────┐    Token Stream    ┌─────────┐    AST    ┌─────────┐    Annotated AST    ┌──────────┐    IR Module
│  Lexer  │──────────────────▶│ Parser  │────────▶│  Sema   │───────────────────▶│ Lowering │──────────▶
└─────────┘                    └─────────┘          └─────────┘                    └──────────┘
```

**Frontend scale overview:**

| Frontend | Source Path | Core Features |
|----------|------------|---------------|
| C++ | `frontends/cpp/` (5 compilation units) | OOP, Templates, RTTI, Exceptions, constexpr, SIMD |
| Python | `frontends/python/` (4 compilation units) | Type annotations, Inference, 25+ advanced features |
| Rust | `frontends/rust/` (4 compilation units) | Borrow checking, Lifetimes, Closures, 28+ advanced features |
| .ploy | `frontends/ploy/` (4 compilation units) | LINK, IMPORT, PIPELINE, CONFIG, Package management, OOP interop |

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
| Lexer | `ploy_lexer.h` | `lexer.cpp` | ~295 | 52 keywords, operators, literals |
| Parser | `ploy_parser.h` | `parser.cpp` | ~1900 | Declaration / statement / expression parsing |
| Semantic Analyser | `ploy_sema.h` | `sema.cpp` | ~1590 | Type checking, package discovery, version verification |
| IR Generator | `ploy_lowering.h` | `lowering.cpp` | ~1410 | AST → IR transformation |

### Keyword List (52)

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
NEW       METHOD    GET       SET        WITH

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
| Qualified | Language-specific type | `cpp::int`, `python::str`, `rust::Vec` |

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
2. **Trigger Discovery** — Execute the discovery command when `IMPORT PACKAGE` is encountered (once per language per compilation unit)
3. **Cache Results** — Discovered packages and versions are cached in `discovered_packages_` map
4. **Version Verification** — Verify version constraints using 6 operators
5. **Symbol Verification** — Verify selective imports have no duplicates

### Platform Adaptation

- **Windows**: Virtual environment Python is at `<venv_path>\Scripts\python.exe`
- **Unix/macOS**: Virtual environment Python is at `<venv_path>/bin/python`

## 4.4 Compilation Model

> **Core Idea: PolyglotCompiler itself compiles all languages; `.ploy` describes the cross-language connection relationships.**

```
┌─────────────┐   ┌─────────────┐   ┌─────────────┐
│  C++ Source  │   │Python Source │   │  Rust Source │
└──────┬──────┘   └──────┬──────┘   └──────┬──────┘
       │                 │                 │
       ▼                 ▼                 ▼
┌──────────────┐  ┌──────────────┐  ┌──────────────┐
│ C++ Frontend │  │Python Frontend│  │ Rust Frontend│
│ (polyglot)   │  │ (polyglot)   │  │ (polyglot)   │
└──────┬──────┘  └──────┬──────┘  └──────┬──────┘
       │                 │                 │
       └────────────┬────┴────────────┬────┘
                    │   Shared IR     │
                    ▼                 ▼
              ┌───────────┐    ┌───────────┐
              │  .ploy    │    │  Polyglot │
              │  Frontend │───▶│  Linker   │
              └───────────┘    └─────┬─────┘
                                     │
                                     ▼
                              ┌───────────┐
                              │  Unified  │
                              │  Binary   │
                              └───────────┘
```

**Important:** PolyglotCompiler uses its own frontends (`frontend_cpp`, `frontend_python`, `frontend_rust`) to compile all language source code to a unified IR, then generates target code through the backend. It does **NOT depend** on external compilers (MSVC/GCC/rustc/CPython). The `polyc` driver (`driver.cpp`) may optionally invoke a system linker (`polyld` or `clang`) only for the final link step to produce an executable.

### Capability Matrix

| Feature | Status | Description |
|---------|--------|-------------|
| C++ ↔ Python function call | ✅ | Via FFI glue code + type marshalling |
| C++ ↔ Rust function call | ✅ | Via C ABI extern functions |
| Python ↔ Rust function call | ✅ | Via FFI bridge |
| Primitive type marshalling | ✅ | int, float, bool, string, void |
| Container type marshalling | ✅ | list, tuple, dict, optional |
| Struct mapping | ✅ | Cross-language struct field conversion |
| Package import + version constraint | ✅ | `IMPORT python PACKAGE numpy >= 1.20;` |
| Selective import | ✅ | `IMPORT python PACKAGE numpy::(array, mean);` |
| Multi-package-manager support | ✅ | pip/conda/uv/pipenv/poetry/cargo/pkg-config |
| Multi-stage pipelines | ✅ | PIPELINE + cross-language CALL |
| Control flow orchestration | ✅ | IF/WHILE/FOR/MATCH |
| Cross-language class instantiation | ✅ | `NEW(python, torch::nn::Linear, 784, 10)` |
| Cross-language method call | ✅ | `METHOD(python, model, forward, data)` |
| Cross-language attribute access | ✅ | `GET(python, model, weight)` |
| Cross-language attribute assignment | ✅ | `SET(python, model, training, FALSE)` |
| Automatic resource management | ✅ | `WITH(python, f) AS handle { ... }` |
| Type annotations | ✅ | `LET model: python::nn::Module = NEW(...)` |
| Interface mapping | ✅ | `MAP_TYPE(python::nn::Module, cpp::NeuralNet)` |

### Architecture Constraints

| Constraint | Explanation |
|-----------|-------------|
| **Function/Object-level granularity** | Cannot mix different language code within a single function body, but supports cross-language function calls, class instantiation, method calls, attribute access, and resource management |
| **Runtime overhead** | Cross-language calls involve argument marshalling |
| **Memory model differences** | Each language manages its own memory; ownership tracked via OwnershipTracker |

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

- 52-keyword lexical analysis
- LINK/IMPORT/EXPORT/MAP_TYPE/PIPELINE/FUNC/STRUCT/CONFIG declaration parsing
- Version constraint verification (6 operators)
- Selective import with symbol verification
- 5 package manager configurations (VENV/CONDA/UV/PIPENV/POETRY)
- Automatic package discovery
- Cross-language class instantiation (`NEW`) and method call (`METHOD`)
- Cross-language attribute access (`GET`) and assignment (`SET`)
- Automatic resource management (`WITH`)
- Type annotations with qualified types
- Interface mapping via `MAP_TYPE`
- Complete type system (primitives + containers + structs + function types)
- Control flow (IF/ELSE/WHILE/FOR/MATCH/BREAK/CONTINUE)
- Struct literals (with lookahead disambiguation using LexerBase SaveState/RestoreState)
- IR Lowering (functions/pipelines/linking/expressions/control flow/OOP interop)

---

# 6. Usage Guide

## 6.1 Compiler Driver (`polyc`)

```bash
# Basic usage
polyc [options] <input_file>

# Options
--lang=<cpp|python|rust|ploy>   # Source language
--arch=<x86_64|arm64>           # Target architecture
-O<0|1|2|3>                     # Optimisation level
--emit-ir=<file>                # Output IR text
--emit-asm=<file>               # Output assembly
-o <file>                       # Output object file / executable
--debug                         # Generate debug info (DWARF 5)
--regalloc=<linear-scan|graph-coloring>  # Register allocator selection
--obj-format=<elf|macho|pobj>   # Object file format
```

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

The project includes 10 `.ploy` samples in `tests/samples/`:

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
fadd, fsub, fmul, fdiv                  # Floating-point arithmetic
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
- Debug info emitter (`backends/common/src/debug_info.cpp`, `debug_emitter.cpp`)
- Variable location tracking (debuggable after optimisation)
- Inline function debugging
- Separate debug info support

---

# 10. Testing Framework

## 10.1 Test Overview

The project uses the **Catch2** testing framework. Test sources are automatically collected via `file(GLOB_RECURSE)` from all `.cpp` files under `tests/unit/`.

| Test Suite | Tag | Test Cases | Coverage |
|-----------|-----|-----------|----------|
| .ploy Frontend | `[ploy]` | 171 (523 assertions) | Lexer / Parser / Sema / IR / Integration / Package mgmt / OOP interop |
| GC Algorithms | `[gc]` | 40+ | 4 GC algorithms |
| Optimisation Passes | `[opt]` | 50+ | 25+ optimisation passes |
| Python Features | `[python]` | 25+ | 25+ Python advanced features |
| Rust Features | `[rust]` | 28+ | 28+ Rust advanced features |
| Backend Optimisations | `[backend]` | 40+ | Scheduler, fusion, etc. |
| Threading Services | `[threading]` | 30+ | Concurrency, synchronisation primitives |

## 10.2 .ploy Test Details

| Category | Tag | Count | Coverage |
|----------|-----|-------|----------|
| Lexer | `[ploy][lexer]` | 15 | Keywords (52), identifiers, numbers, strings, operators |
| Parser | `[ploy][parser]` | 37 | LINK/IMPORT/EXPORT/FUNC/PIPELINE/STRUCT/CONFIG/NEW/METHOD/GET/SET/WITH |
| Semantic Analysis | `[ploy][sema]` | 31 | Type checking, scoping, version verification, package discovery, OOP interop |
| IR Generation | `[ploy][lowering]` | 25 | Functions/pipelines/linking/expressions/control flow/OOP interop |
| Integration | `[ploy][integration]` | 19 | Complete pipeline end-to-end |
| Version Constraints | `[ploy][version]` | 5+ | 6 version operators |
| Selective Import | `[ploy][selective]` | 7 | Single/multi-symbol, version combination, alias |
| CONFIG VENV | `[ploy][venv]` | 6 | Parsing/validation/duplicate detection/invalid language |
| Multi-Package-Manager | `[ploy][pkgmgr]` | 17 | CONDA/UV/PIPENV/POETRY parsing, validation, integration |

## 10.3 Running Tests

```bash
# Run all tests
./unit_tests

# Run .ploy tests
./unit_tests [ploy]

# Run by tag
./unit_tests [ploy][pkgmgr]    # Package manager tests
./unit_tests [ploy][lexer]      # Lexer tests
./unit_tests [ploy][sema]       # Semantic analysis tests

# Verbose output
./unit_tests [ploy] -r compact

# Windows note: needs <nul to prevent pip command from hanging
unit_tests.exe [ploy] -r compact 2>&1 <nul
```

## 10.4 Sample Programs

The `tests/samples/` directory contains 10 `.ploy` sample files and multiple C++ test files:

```
tests/samples/
├── advanced_pipeline.ploy              # Multi-stage pipeline
├── basic_linking.ploy                  # Basic linking
├── complex_types.ploy                  # Complex types
├── container_marshalling.ploy          # Container marshalling
├── cross_lang_class_instantiation.ploy # Cross-language class instantiation with mixed calls
├── error_handling.ploy                 # Error handling
├── mixed_compilation.ploy              # Mixed compilation
├── multi_language_pipeline.ploy        # Three-language pipeline
├── package_import.ploy                 # Package import
├── pipeline_control_flow.ploy          # Pipeline control flow
├── class_test.cpp                      # C++ OOP test
├── complete_implementation_test.cpp    # Complete implementation test
├── e2e_test.cpp                        # End-to-end test
├── exception_test.cpp                  # Exception test
├── float_test.cpp                      # Floating-point test
├── simd_test.cpp                       # SIMD test
├── hello_world/                        # Getting started
├── interop/                            # Interop examples
└── ir/                                 # IR examples
```

---

# 11. Build & Integration

## 11.1 CMake Build Targets

| Target | Type | Main Dependencies |
|--------|------|-------------------|
| `polyglot_common` | Static library | fmt, nlohmann_json |
| `frontend_common` | Static library | polyglot_common |
| `frontend_cpp` | Static library | frontend_common (5 compilation units: lexer/parser/sema/lowering/constexpr) |
| `frontend_python` | Static library | frontend_common (4 compilation units) |
| `frontend_rust` | Static library | frontend_common (4 compilation units) |
| `frontend_ploy` | Static library | frontend_common, middle_ir (4 compilation units) |
| `middle_ir` | Static library | polyglot_common (15 compilation units) |
| `backend_x86_64` | Static library | polyglot_common (7 compilation units) |
| `backend_arm64` | Static library | polyglot_common (6 compilation units) |
| `runtime` | Static library | — (18 compilation units) |
| `linker_lib` | Object library | polyglot_common, frontend_ploy |
| `polyc` | Executable | All frontends + backends + IR + runtime |
| `polyld` | Executable | polyglot_common, frontend_ploy |
| `polyasm` | Executable | Backends + IR |
| `polyopt` | Executable | middle_ir + backends |
| `polyrt` | Executable | runtime |
| `polybench` | Executable | All |
| `unit_tests` | Executable | All + Catch2 |

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

# 13. Appendix

## 13.1 Glossary

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

## 13.2 .ploy Keyword Quick Reference

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
| `CONFIG` | Environment configuration | `CONFIG CONDA "env";` |
| `VENV` | pip/venv environment | `CONFIG VENV python "/path";` |
| `CONDA` | Conda environment | `CONFIG CONDA "env_name";` |
| `UV` | uv-managed environment | `CONFIG UV python "/path";` |
| `PIPENV` | Pipenv project | `CONFIG PIPENV "project_path";` |
| `POETRY` | Poetry project | `CONFIG POETRY "project_path";` |

## 13.3 Version Operators

| Operator | Meaning | Example |
|----------|---------|---------|
| `>=` | Greater or equal | `>= 1.20` |
| `<=` | Less or equal | `<= 2.0` |
| `==` | Exact match | `== 1.10.0` |
| `>` | Strictly greater | `> 1.0` |
| `<` | Strictly less | `< 3.0` |
| `~=` | Compatible version (PEP 440) | `~= 1.20` → `>= 1.20, < 2.0` |

## 13.4 References

- "Compilers: Principles, Techniques, and Tools" (Dragon Book)
- "Engineering a Compiler" (Whale Book)
- LLVM: https://llvm.org/
- Rust Compiler: https://github.com/rust-lang/rust
- PEP 440: https://peps.python.org/pep-0440/

## 13.5 Changelog

### v4.2 (2026-02-20)
- ✅ Added cross-language attribute access `GET` and assignment `SET` keywords
- ✅ Added automatic resource management `WITH` keyword
- ✅ Added type annotations with qualified types
- ✅ Added interface mapping via `MAP_TYPE` for classes
- ✅ AST / Lexer / Parser / Sema / IR Lowering full chain implementation for GET/SET/WITH
- ✅ 31 new test cases, total 171 test cases, 523 assertions
- ✅ Bilingual documentation (USER_GUIDE.md / USER_GUIDE_zh.md)
- ✅ Keywords count 49 → 52

### v4.1 (2026-02-20)
- ✅ Added cross-language class instantiation `NEW` and method call `METHOD` keywords
- ✅ AST / Lexer / Parser / Sema / IR Lowering full chain implementation
- ✅ 22 new test cases, total 140 test cases, 443 assertions
- ✅ Bilingual feature documentation (`class_instantiation.md` / `class_instantiation_zh.md`)
- ✅ Keywords count 47 → 49

### v4.0 (2026-02-19)
- ✅ Added complete .ploy cross-language linking frontend chapter
- ✅ Multi-package-manager support: CONFIG CONDA / UV / PIPENV / POETRY
- ✅ Version constraint verification (6 operators)
- ✅ Selective import
- ✅ 117+ test cases, 361+ assertions
- ✅ Comprehensive review and rewrite of the complete guide (v3.0 → v4.0)
- ✅ Precise description of compilation flow (PolyglotCompiler's own frontends, no external compilers)

### v3.0 (2026-02-01)
- ✅ Chapters 11-15 (implementation analysis / testing / advanced optimisations / achievements)
- ✅ 33+ optimisation passes, 4 GC algorithms
- ✅ PGO/LTO/DWARF5 support

### v2.0 (2026-01-29)
- ✅ Loop optimisation, GVN, constexpr, DWARF 5

### v1.0 (2026-01-15)
- ✅ Basic compilation chain: 3 frontends, 2 backends, basic optimisations

---

*Maintained by PolyglotCompiler Team*  
*Last Updated: 2026-02-20*  
*Document Version: v4.2*
