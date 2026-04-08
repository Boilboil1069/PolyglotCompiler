# PolyglotCompiler Project Tutorial

> **Version**: 2.0.0
> **Last Updated**: 2026-02-22
> **Project**: PolyglotCompiler  

---

## Table of Contents

1. [Project Overview](#1-project-overview)
2. [Prerequisites](#2-prerequisites)
3. [Building from Source](#3-building-from-source)
4. [Project Architecture](#4-project-architecture)
5. [Using the Toolchain](#5-using-the-toolchain)
6. [Running Tests](#6-running-tests)
7. [Working with Sample Programs](#7-working-with-sample-programs)
8. [Development Workflow](#8-development-workflow)
9. [Frontend Development](#9-frontend-development)
10. [Middle Layer and IR](#10-middle-layer-and-ir)
11. [Backend Development](#11-backend-development)
12. [Adding a New Language Frontend](#12-adding-a-new-language-frontend)
13. [Debugging and Troubleshooting](#13-debugging-and-troubleshooting)
14. [Project Configuration Reference](#14-project-configuration-reference)

---

# 1. Project Overview

## 1.1 What Is PolyglotCompiler?

PolyglotCompiler is a modern multi-language compiler project built in **C++20**. It uses a multi-frontend shared intermediate representation (IR) architecture and implements cross-language function-level linking through the `.ploy` domain-specific language.

### Core Capabilities

- **6 language frontends**: C++, Python, Rust, Java, C# (.NET), .ploy
- **3 architecture backends**: x86_64, ARM64, WebAssembly
- **7 toolchain executables**: `polyc`, `polyld`, `polyasm`, `polyopt`, `polyrt`, `polybench`, `polyui` (IDE, requires Qt)
- **Complete compilation pipeline**: Lexer → Parser → Sema → IR Lowering → Optimisation → Code Generation
- **Cross-language OOP**: Full interoperability via `.ploy` (NEW, METHOD, GET, SET, WITH, DELETE, EXTEND)
- **Package manager integration**: pip, conda, uv, pipenv, poetry, cargo, NuGet

## 1.2 Technology Stack

| Component | Technology |
|-----------|-----------|
| Language | C++20 |
| Build System | CMake 3.20+ |
| Build Tool | Ninja (recommended) |
| Test Framework | Catch2 v3 |
| Formatting | fmt library |
| JSON | nlohmann_json |
| Memory Allocator | mimalloc |
| License | GPL v3 |

---

# 2. Prerequisites

## 2.1 Required Software

### Compiler

You need a C++20-capable compiler:

| Platform | Recommended Compiler | Minimum Version |
|----------|---------------------|-----------------|
| Windows | MSVC (Visual Studio 2022+) | v19.30+ |
| Linux | GCC | 12+ |
| macOS | Clang | 15+ |

### Build Tools

| Tool | Version | Notes |
|------|---------|-------|
| CMake | 3.20+ | Build system generator |
| Ninja | Any recent | Recommended build tool (faster than Make) |

### Optional (for cross-language samples)

| Tool | Purpose |
|------|---------|
| Python 3.8+ | Python frontend and samples |
| Rust (rustup) | Rust frontend and samples |
| Java JDK 21+ | Java frontend and samples |
| .NET SDK 9+ | .NET/C# frontend and samples |

## 2.2 Environment Setup Scripts

The project includes automated setup scripts under `tests/samples/`:

### Windows (PowerShell)

```powershell
cd tests\samples
.\setup_env.ps1
```

This script:
- Creates Python virtual environments
- Installs Rust via `rustup` (if not present)
- Installs Java JDK 21 (if not present)
- Installs .NET SDK 9 (if not present)
- Links environments to all sample directories

### Linux/macOS (Bash)

```bash
cd tests/samples
chmod +x setup_env.sh
./setup_env.sh
```

---

# 3. Building from Source

## 3.1 Clone the Repository

```bash
git clone <repository-url>
cd PolyglotCompiler
```

## 3.2 Build on Windows (MSVC + Ninja)

**Important**: You must use the Developer Command Prompt with `-arch=amd64` to avoid x86/x64 linker mismatches.

```powershell
# Step 1: Open Developer Command Prompt
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64

# Step 2: Create build directory and generate build files
mkdir build
cd build
cmake .. -G Ninja

# Step 3: Build all targets
ninja

# Or build a specific target
ninja polyc
ninja unit_tests
```

### VS Code Build Shortcut

If using VS Code with CMake Tools extension, the project supports direct CMake build integration.

## 3.3 Build on Linux/macOS

```bash
# Create build directory
mkdir -p build && cd build

# Generate build files (Ninja recommended)
cmake .. -G Ninja

# Build
ninja

# Or with Make
cmake .. -G "Unix Makefiles"
make -j$(nproc)
```

## 3.4 Build Targets

| Target | Description | Executable |
|--------|-------------|-----------|
| `polyc` | Compiler driver | `polyc` / `polyc.exe` |
| `polyld` | Linker | `polyld` / `polyld.exe` |
| `polyasm` | Assembler | `polyasm` / `polyasm.exe` |
| `polyopt` | Optimiser | `polyopt` / `polyopt.exe` |
| `polyrt` | Runtime tool | `polyrt` / `polyrt.exe` |
| `polybench` | Benchmark suite | `polybench` / `polybench.exe` |
| `polyui` | Desktop IDE (requires Qt) | `polyui` / `polyui.exe` |
| `unit_tests` | Test executable | `unit_tests` / `unit_tests.exe` |

### Building Specific Targets

```bash
# Build only the compiler driver
ninja polyc

# Build only the test executable
ninja unit_tests

# Build everything
ninja
```

## 3.5 CMake Dependencies

The following libraries are **automatically fetched** by CMake (no manual installation needed):

| Library | Purpose |
|---------|---------|
| **Catch2** | Unit testing framework |
| **fmt** | String formatting |
| **nlohmann_json** | JSON parsing and serialisation |
| **mimalloc** | High-performance memory allocator |

These are configured in `Dependencies.cmake` and downloaded to `build/_deps/`.

## 3.6 Build Configurations

```bash
# Debug build (default)
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Debug

# Release build (optimised)
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release

# Release with debug info
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
```

---

# 4. Project Architecture

## 4.1 Directory Structure

```
PolyglotCompiler/
├── frontends/              # Language frontends (6 frontends)
│   ├── common/             #   Shared frontend facilities (Token, Diagnostics)
│   ├── cpp/                #   C++ frontend
│   ├── python/             #   Python frontend
│   ├── rust/               #   Rust frontend
│   ├── java/               #   Java frontend (Java 8/17/21/23)
│   ├── dotnet/             #   .NET frontend (C# .NET 6/7/8/9)
│   └── ploy/               #   .ploy cross-language frontend
├── middle/                 # Middle layer (IR + Optimisation)
│   ├── include/
│   │   ├── ir/             #   IR definitions
│   │   ├── passes/         #   Optimisation pass interfaces
│   │   ├── pgo/            #   Profile-Guided Optimization
│   │   └── lto/            #   Link-Time Optimization
│   └── src/
│       ├── ir/             #   IR builder, context, CFG, SSA
│       ├── passes/         #   Pass manager, optimisations
│       ├── pgo/            #   Profile data
│       └── lto/            #   Link-time optimiser
├── backends/               # Architecture backends (3 backends)
│   ├── common/             #   Shared backend facilities
│   ├── x86_64/             #   x86_64 backend
│   ├── arm64/              #   ARM64 backend
│   └── wasm/               #   WebAssembly backend
├── runtime/                # Runtime system
│   ├── include/            #   GC, FFI, service interfaces
│   └── src/
│       ├── gc/             #   Garbage collectors
│       ├── interop/        #   FFI, marshalling, type mapping
│       ├── libs/           #   Language-specific runtime bridges
│       └── services/       #   Exception, reflection, threading
├── common/                 # Project-wide common facilities
│   ├── include/
│   │   ├── core/           #   Type system, source locations, symbols
│   │   ├── ir/             #   IR node definitions
│   │   ├── debug/          #   DWARF5 debug info
│   │   └── utils/          #   Utilities
│   └── src/
├── tools/                  # Toolchain executables (7 tools)
│   ├── polyc/              #   Compiler driver
│   ├── polyld/             #   Linker
│   ├── polyasm/            #   Assembler
│   ├── polyopt/            #   Optimiser
│   ├── polyrt/             #   Runtime tool
│   ├── polybench/          #   Benchmark suite
│   └── ui/                 #   Desktop IDE (polyui) — Qt-based, syntax highlighting, diagnostics
├── tests/                  # Tests
│   ├── unit/               #   Catch2 unit tests
│   ├── samples/            #   16 sample programs
│   ├── integration/        #   Integration tests
│   └── benchmarks/         #   Performance benchmarks
├── docs/                   # Documentation
│   ├── api/                #   API reference (EN + ZH)
│   ├── specs/              #   Language & IR specs (EN + ZH)
│   ├── tutorial/           #   Tutorials (EN + ZH)
│   ├── realization/        #   Implementation docs
│   └── demand/             #   Requirements
├── CMakeLists.txt          # Root CMake configuration
└── Dependencies.cmake      # External dependency declarations
```

## 4.2 Compilation Pipeline

```
Source Code (.cpp / .py / .rs / .java / .cs / .ploy)
    │
    ▼
┌─────────┐    Tokens    ┌─────────┐    AST    ┌─────────┐    Checked AST    ┌──────────┐    IR
│  Lexer  │─────────────▶│ Parser  │────────▶│  Sema   │─────────────────▶│ Lowering │──────▶
└─────────┘              └─────────┘          └─────────┘                  └──────────┘
                                                                                │
                                                                                ▼
                                                                       ┌──────────────┐
                                                                       │ Shared IR     │
                                                                       │ (SSA + CFG)   │
                                                                       └──────┬───────┘
                                                                              │
                                                                    ┌─────────┼─────────┐
                                                                    ▼         ▼         ▼
                                                                ┌──────┐ ┌──────┐ ┌──────┐
                                                                │ Opt  │ │ SSA  │ │ CFG  │
                                                                │Passes│ │Trans │ │Build │
                                                                └──┬───┘ └──┬───┘ └──┬───┘
                                                                   └────────┼────────┘
                                                                            ▼
                                                                   ┌──────────────────────┐
                                                                   │ Backend              │
                                                                   │ (x86_64/ARM64/WASM)  │
                                                                   └──────────┬───────────┘
                                                                            │
                                                                   ┌────────┼────────┐
                                                                   ▼        ▼        ▼
                                                               ┌──────┐ ┌──────┐ ┌──────┐
                                                               │ISel  │ │RegAll│ │AsmGen│
                                                               └──┬───┘ └──┬───┘ └──┬───┘
                                                                  └────────┼────────┘
                                                                           ▼
                                                                  ┌───────────────┐
                                                                  │ Object File   │
                                                                  └───────────────┘
```

## 4.3 Frontend Architecture

Every frontend (C++, Python, Rust, Java, .NET, .ploy) follows a unified **4-stage pipeline**:

| Stage | Class | Responsibility |
|-------|-------|---------------|
| **Lexer** | `*_lexer.h` | Tokenise source text into a token stream |
| **Parser** | `*_parser.h` | Parse tokens into an Abstract Syntax Tree (AST) |
| **Sema** | `*_sema.h` | Type checking, name resolution, semantic validation |
| **Lowering** | `*_lowering.h` | Transform the annotated AST into shared IR |

Each frontend resides in `frontends/<language>/` with `include/` and `src/` subdirectories.

## 4.4 CMake Library Targets

The CMake build system defines the following library targets:

| Target | Source Directory | Description |
|--------|-----------------|-------------|
| `polyglot_common` | `common/` | Core types, symbols, utilities |
| `frontend_common` | `frontends/common/` | Shared token pool, diagnostics |
| `frontend_cpp` | `frontends/cpp/` | C++ frontend |
| `frontend_python` | `frontends/python/` | Python frontend |
| `frontend_rust` | `frontends/rust/` | Rust frontend |
| `frontend_java` | `frontends/java/` | Java frontend |
| `frontend_dotnet` | `frontends/dotnet/` | .NET frontend |
| `middle_ir` | `middle/` | IR, passes, PGO, LTO |
| `backend_x86_64` | `backends/x86_64/` | x86_64 backend |
| `backend_arm64` | `backends/arm64/` | ARM64 backend |
| `backend_wasm` | `backends/wasm/` | WebAssembly backend |
| `runtime` | `runtime/` | Runtime system |
| `linker_lib` | `tools/polyld/` | Linker library |

---

# 5. Using the Toolchain

## 5.1 polyc — Compiler Driver

The `polyc` driver is the main entry point for compilation. It automatically detects the source language by file extension.

### Basic Usage

```bash
# Compile a .ploy file
polyc sample.ploy -o sample

# Compile a C++ file
polyc hello.cpp -o hello

# Compile a Python file
polyc script.py -o script

# Compile a Java file
polyc Main.java -o main

# Compile a C# file
polyc Program.cs -o program
```

### Language Override

```bash
# Force a specific language frontend
polyc --lang=cpp input_file -o output
polyc --lang=python input_file -o output
polyc --lang=ploy input_file -o output
```

### Emit Intermediate Representations

```bash
# Generate human-readable IR
polyc --emit-ir=output.ir input.ploy

# Generate assembly
polyc --emit-asm=output.asm input.ploy

# Specify target architecture
polyc --target=x86_64 input.ploy -o output
polyc --target=arm64 input.ploy -o output
```

### Output Artifacts

When compiling, `polyc` generates intermediate artifacts in an `aux/` subdirectory:

```
aux/
├── <filename>.ir         # IR text (if --emit-ir)
├── <filename>.ir.bin     # Binary-encoded IR
├── <filename>.asm        # Generated assembly
├── <filename>.asm.bin    # Binary-encoded assembly
├── <filename>.obj        # Object file
└── <filename>.symbols    # Symbol table dump
```

## 5.2 polyld — Linker

The linker handles object file linking and cross-language glue code generation:

```bash
# Link object files
polyld -o output file1.obj file2.obj

# Link with cross-language glue
polyld --polyglot -o output main.obj bridge.obj
```

## 5.3 polyasm — Assembler

Convert assembly files to object files:

```bash
polyasm input.asm -o output.obj
```

## 5.4 polyopt — Optimiser

Apply optimisation passes to IR:

```bash
# Apply all optimisations
polyopt -O2 input.ir -o optimised.ir

# Apply specific passes
polyopt --pass=constant-fold --pass=dce input.ir -o optimised.ir
```

## 5.5 polyrt — Runtime Tool

Manage runtime components:

```bash
# Start runtime services
polyrt --gc-mode=generational --threads=4
```

## 5.6 polybench — Benchmark

Run performance benchmarks:

```bash
polybench --suite=micro
polybench --suite=macro
polybench --all
```

---

# 6. Running Tests

## 6.1 Catch2 Test Framework

The project uses **Catch2 v3** as its testing framework. All unit tests are in `tests/unit/`.

### Run All Tests

```bash
cd build
./unit_tests          # Linux/macOS
unit_tests.exe        # Windows
```

### Run Tests with Tags

```bash
# Run only .ploy frontend tests
./unit_tests "[ploy]"

# Run only Java frontend tests
./unit_tests "[java]"

# Run only .NET frontend tests
./unit_tests "[dotnet]"

# Run multiple tags
./unit_tests "[ploy],[java]"
```

### Test Statistics

The project currently has:

| Category | Tests | Assertions |
|----------|-------|------------|
| .ploy frontend | 207 | 599 |
| Java frontend | 22 | 77 |
| .NET frontend | 24 | 77 |
| **Total** | **253** | **753** |

## 6.2 CTest Integration

You can also run tests through CTest:

```bash
cd build
ctest --output-on-failure
ctest -R "ploy"      # Run tests matching "ploy"
ctest -V              # Verbose output
```

## 6.3 Test File Structure

```
tests/
├── unit/
│   └── frontends/
│       └── ploy/
│           └── ploy_test.cpp    # Main .ploy test file (171+ test cases)
├── samples/                      # 16 sample programs
├── integration/                  # Integration tests
└── benchmarks/                   # Performance benchmarks
```

---

# 7. Working with Sample Programs

## 7.1 Sample Directory Overview

The project includes **16 categorised sample programs** under `tests/samples/`:

| # | Folder | Feature | Languages |
|---|--------|---------|-----------|
| 01 | `01_basic_linking/` | LINK, CALL, IMPORT, EXPORT | C++, Python |
| 02 | `02_type_mapping/` | MAP_TYPE, STRUCT, containers | C++, Python |
| 03 | `03_pipeline/` | PIPELINE, control flow | C++, Python |
| 04 | `04_package_import/` | IMPORT PACKAGE, CONFIG | C++, Python |
| 05 | `05_class_instantiation/` | NEW, METHOD | C++, Python |
| 06 | `06_attribute_access/` | GET, SET | C++, Python |
| 07 | `07_resource_management/` | WITH | C++, Python |
| 08 | `08_delete_extend/` | DELETE, EXTEND | C++, Python |
| 09 | `09_mixed_pipeline/` | All features | C++, Python, Rust |
| 10 | `10_error_handling/` | Error checking | C++, Python |
| 11 | `11_java_interop/` | NEW, METHOD (Java) | Java, Python |
| 12 | `12_dotnet_interop/` | NEW, METHOD (.NET) | C#, Python |
| 13 | `13_generic_containers/` | MAP_TYPE containers | C++, Java, Python |
| 14 | `14_async_pipeline/` | PIPELINE, IF/ELSE | C++, Rust, Python |
| 15 | `15_full_stack/` | All 5 languages | C++, Python, Rust, Java, C# |
| 16 | `16_config_and_venv/` | CONFIG, IMPORT PACKAGE, CONVERT | Python, C# |

## 7.2 Sample Structure

Each sample directory contains:

```
XX_feature_name/
├── feature_name.ploy        # .ploy source file
├── source_file.cpp          # C++ source (if applicable)
├── source_file.py           # Python source (if applicable)
├── source_file.rs           # Rust source (if applicable)
├── SourceFile.java          # Java source (if applicable)
└── SourceFile.cs            # C# source (if applicable)
```

## 7.3 Compiling Samples

```bash
# Compile any sample
polyc tests/samples/01_basic_linking/basic_linking.ploy -o basic_linking

# Compile the mixed pipeline
polyc tests/samples/09_mixed_pipeline/mixed_pipeline.ploy -o ml_pipeline

# Compile the full-stack sample
polyc tests/samples/15_full_stack/full_stack.ploy -o full_stack
```

## 7.4 Learning Path

Recommended order for working through the samples:

1. **Start with basics**: `01_basic_linking` → `02_type_mapping`
2. **Learn control flow**: `03_pipeline` (IF, WHILE, FOR, MATCH)
3. **Package management**: `04_package_import`
4. **OOP features**: `05_class_instantiation` → `06_attribute_access` → `07_resource_management` → `08_delete_extend`
5. **Combined features**: `09_mixed_pipeline`
6. **Error handling**: `10_error_handling`
7. **Multi-language**: `11_java_interop` → `12_dotnet_interop` → `13_generic_containers`
8. **Advanced**: `14_async_pipeline` → `15_full_stack` → `16_config_and_venv`

---

# 8. Development Workflow

## 8.1 Typical Workflow

```
1. Make code changes
2. Build:       ninja -C build
3. Run tests:   build/unit_tests
4. Fix issues:  iterate on 1-3
5. Verify:      build/unit_tests      (all 253 tests pass)
```

## 8.2 Incremental Builds

Ninja supports fast incremental builds — only changed files are recompiled:

```bash
# Rebuild after changes
cd build
ninja
```

## 8.3 Clean Build

```bash
# Full clean rebuild
cd build
ninja clean
ninja
```

Or delete the build directory entirely:

```bash
rm -rf build
mkdir build && cd build
cmake .. -G Ninja
ninja
```

## 8.4 Compile Commands

For IDE integration (IntelliSense, clangd, etc.), `compile_commands.json` is generated in the build directory:

```bash
cmake .. -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

This file is typically located in your active build directory (for example, the
directory where you ran CMake with `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`).

---

# 9. Frontend Development

## 9.1 Frontend Structure

Each frontend follows the same directory layout:

```
frontends/<language>/
├── include/
│   ├── <lang>_ast.h        # AST node definitions (if separate)
│   ├── <lang>_lexer.h      # Lexer interface
│   ├── <lang>_parser.h     # Parser interface
│   ├── <lang>_sema.h       # Semantic analyser interface
│   └── <lang>_lowering.h   # IR lowering interface
└── src/
    ├── lexer/               # Lexer implementation
    │   └── lexer.cpp
    ├── parser/              # Parser implementation
    │   └── parser.cpp
    ├── sema/                # Semantic analysis implementation
    │   └── sema.cpp
    └── lowering/            # IR lowering implementation
        └── lowering.cpp
```

## 9.2 Lexer Development

The lexer converts source text into a stream of tokens:

```cpp
// Example: Tokenising a keyword
Token LexKeyword(const std::string& word) {
    if (word == "FUNC") return Token{TokenKind::kFunc, word, loc_};
    if (word == "LET")  return Token{TokenKind::kLet, word, loc_};
    // ...
}
```

Key considerations:
- Handle all language-specific keywords
- Support string/number/identifier literals
- Track source locations for error reporting

## 9.3 Parser Development

The parser transforms tokens into an AST:

```cpp
// Example: Parsing a function declaration
std::unique_ptr<FuncDeclNode> ParseFuncDecl() {
    Expect(TokenKind::kFunc);
    auto name = Expect(TokenKind::kIdentifier);
    Expect(TokenKind::kLParen);
    auto params = ParseParamList();
    Expect(TokenKind::kRParen);
    Expect(TokenKind::kArrow);
    auto ret_type = ParseType();
    auto body = ParseBlock();
    return std::make_unique<FuncDeclNode>(name, params, ret_type, body);
}
```

## 9.4 Semantic Analysis

The semantic analyser validates the AST and annotates it with type information:

- Name resolution (symbol table lookup)
- Type checking (parameter types, return types)
- Cross-language validation (LINK targets, package versions)
- Error reporting with source locations

## 9.5 IR Lowering

The lowering phase transforms the annotated AST into the shared IR:

```cpp
// Example: Lowering a LET declaration
void LowerLetDecl(const LetDeclNode& node) {
    auto value = LowerExpression(node.initialiser());
    auto alloca = builder_.CreateAlloca(MapType(node.type()));
    builder_.CreateStore(value, alloca);
    symbol_table_.Bind(node.name(), alloca);
}
```

---

# 10. Middle Layer and IR

## 10.1 IR Design

The shared IR uses an SSA-based representation with:

- **Basic blocks** for control flow
- **Instructions** for operations (arithmetic, memory, control flow, calls)
- **Types** that are independent of source languages (i32, f64, void, pointers, etc.)

## 10.2 IR Types

| IR Type | Size | Description |
|---------|------|-------------|
| `i1` | 1 bit | Boolean |
| `i8` | 1 byte | Byte |
| `i16` | 2 bytes | Short integer |
| `i32` | 4 bytes | 32-bit integer |
| `i64` | 8 bytes | 64-bit integer |
| `f32` | 4 bytes | Single-precision float |
| `f64` | 8 bytes | Double-precision float |
| `void` | 0 | No value |
| `T*` | pointer-sized | Pointer to T |

## 10.3 Optimisation Passes

The optimiser supports the following passes:

| Pass | Category | Description |
|------|----------|-------------|
| Constant Folding | Transform | Evaluate constant expressions at compile time |
| Dead Code Elimination | Transform | Remove unreachable/unused code |
| Common Subexpression Elimination | Transform | Reuse previously computed values |
| Function Inlining | Transform | Replace call sites with function bodies |
| Devirtualisation | Transform | Resolve virtual calls to direct calls |
| Loop Optimisation | Transform | LICM, unrolling, vectorisation |
| GVN | Transform | Global Value Numbering |
| SSA Transform | Analysis | Convert to SSA form |
| CFG Build | Analysis | Build control flow graph |

## 10.4 IR Builder API

```cpp
// Create a new function
auto func = module.CreateFunction("my_func", return_type, param_types);

// Create basic blocks
auto entry = func->CreateBlock("entry");
auto then_bb = func->CreateBlock("then");
auto else_bb = func->CreateBlock("else");

// Build instructions
builder.SetInsertPoint(entry);
auto cond = builder.CreateICmpSGT(param, builder.CreateConstInt(0));
builder.CreateCondBr(cond, then_bb, else_bb);
```

---

# 11. Backend Development

## 11.1 Backend Structure

```
backends/<arch>/
├── include/
│   ├── <arch>_target.h       # Target description
│   ├── <arch>_register.h     # Register definitions
│   └── machine_ir.h          # Machine IR definitions
└── src/
    ├── isel/                  # Instruction selection
    ├── regalloc/              # Register allocation
    │   ├── graph_coloring.cpp
    │   └── linear_scan.cpp
    ├── asm_printer/           # Assembly emission
    └── calling_convention/    # Calling convention implementation
```

## 11.2 x86_64 Backend

- **Instruction Selection**: Maps IR instructions to x86_64 instructions
- **Register Allocation**: Graph colouring and linear scan algorithms
- **Calling Convention**: System V ABI (Linux/macOS) and Win64 ABI (Windows)
- **SIMD**: SSE2, AVX, AVX2, AVX-512 support

## 11.3 ARM64 Backend

- **Instruction Selection**: Maps IR instructions to AArch64 instructions
- **Register Allocation**: Graph colouring and linear scan algorithms
- **Calling Convention**: AAPCS64
- **SIMD**: NEON, SVE support

---

# 12. Adding a New Language Frontend

## 12.1 Steps

1. **Create directory structure**:
   ```
   frontends/new_lang/
   ├── include/
   │   ├── new_lang_lexer.h
   │   ├── new_lang_parser.h
   │   ├── new_lang_sema.h
   │   └── new_lang_lowering.h
   └── src/
       ├── lexer/lexer.cpp
       ├── parser/parser.cpp
       ├── sema/sema.cpp
       └── lowering/lowering.cpp
   ```

2. **Add CMake target** in `CMakeLists.txt`:
   ```cmake
   add_library(frontend_new_lang
       frontends/new_lang/src/lexer/lexer.cpp
       frontends/new_lang/src/parser/parser.cpp
       frontends/new_lang/src/sema/sema.cpp
       frontends/new_lang/src/lowering/lowering.cpp
   )
   target_include_directories(frontend_new_lang PUBLIC
       frontends/new_lang/include
   )
   target_link_libraries(frontend_new_lang PUBLIC
       polyglot_common frontend_common middle_ir
   )
   ```

3. **Implement the 4 stages**: Lexer → Parser → Sema → Lowering.

4. **Register with polyc driver**: Add the new language to the driver's language detection and dispatch logic in `tools/polyc/src/driver.cpp`.

5. **Add tests**: Create test cases in `tests/unit/frontends/new_lang/`.

6. **Add samples**: Create sample programs in `tests/samples/`.

## 12.2 Integration Checklist

- [ ] Lexer tokenises all language keywords and operators
- [ ] Parser produces correct AST for all supported constructs
- [ ] Sema validates types, names, and language-specific rules
- [ ] Lowering generates correct IR for all AST nodes
- [ ] polyc driver detects the file extension and routes correctly
- [ ] Unit tests cover all major features
- [ ] Sample programs compile successfully

---

# 13. Debugging and Troubleshooting

## 13.1 Common Build Issues

### x86/x64 Linker Mismatch (Windows)

**Problem**: Linker errors about architecture mismatch.

**Solution**: Always use `-arch=amd64` when opening the Developer Command Prompt:

```powershell
call "...\VsDevCmd.bat" -arch=amd64
```

### CMake Version Too Old

**Problem**: `CMake Error: CMake 3.20 or higher is required.`

**Solution**: Update CMake to 3.20+.

### Ninja Not Found

**Problem**: `CMake Error: Could not find Ninja.`

**Solution**: Install Ninja and ensure it's in your PATH.

## 13.2 Common Test Failures

### Missing Environment

Some samples require language runtimes (Python, Rust, Java, .NET). Run the setup script:

```bash
cd tests/samples
./setup_env.sh      # Linux/macOS
.\setup_env.ps1     # Windows
```

### Stale Build

If tests fail after code changes, do a clean rebuild:

```bash
ninja clean && ninja
```

## 13.3 Debugging Tips

1. **Use `--emit-ir`** to inspect the generated IR:
   ```bash
   polyc --emit-ir=debug.ir input.ploy
   ```

2. **Run specific tests** to isolate failures:
   ```bash
   ./unit_tests "[ploy]" -c "test name"
   ```

3. **Check compile_commands.json** for IntelliSense issues.

4. **Use verbose CTest output**:
   ```bash
   ctest -V --output-on-failure
   ```

---

# 14. Project Configuration Reference

## 14.1 CMakeLists.txt Structure

The root `CMakeLists.txt` defines:

1. Project metadata (name, version, language)
2. C++ standard (C++20)
3. Include `Dependencies.cmake` for external libraries
4. Library targets for each component
5. Executable targets for each tool
6. Test configuration

## 14.2 Dependencies.cmake

Declares external dependencies fetched via `FetchContent`:

```cmake
FetchContent_Declare(catch2 ...)
FetchContent_Declare(fmt ...)
FetchContent_Declare(nlohmann_json ...)
FetchContent_Declare(mimalloc ...)
```

## 14.3 Key CMake Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `CMAKE_BUILD_TYPE` | `Debug` | Build configuration |
| `CMAKE_CXX_STANDARD` | `20` | C++ standard version |
| `CMAKE_EXPORT_COMPILE_COMMANDS` | `ON` | Generate compile_commands.json |

## 14.4 Documentation Structure

| Path | Content |
|------|---------|
| `docs/api/` | API reference (EN + ZH) |
| `docs/specs/` | Language & IR specifications (EN + ZH) |
| `docs/tutorial/` | Tutorials (EN + ZH) |
| `docs/realization/` | Implementation documentation |
| `docs/demand/` | Requirements and task tracking |
| `docs/USER_GUIDE.md` | Complete user guide (EN) |
| `docs/USER_GUIDE_zh.md` | Complete user guide (ZH) |

## 14.5 CI Quality Gates

The project uses GitHub Actions (`.github/workflows/ci.yml`) with five quality gates:

### Docs Lint & Sync
- Checks path references, bilingual sync, heading structure
- Validates version consistency across documentation

### Format Check
- Uses `clang-format-17` with C++20 standard (updated from C++17)
- Configuration in `.clang-format`: `Standard: c++20`

### Static Analysis
- `clang-tidy-17` with bugprone checks
- **Note**: Previously limited to first 50 `.cpp` files; now scans all files
- Output logged and checked for errors via grep

### Sanitizers
- ASan (Address Sanitizer) + UBSan (Undefined Behavior Sanitizer)
- Runs on full test suite

### Code Coverage
- Uses `lcov`/`gcov` for coverage collection

### Test Configuration
- `tests/CMakeLists.txt`: Uses explicit file lists instead of `GLOB_RECURSE` for integration and benchmark tests
