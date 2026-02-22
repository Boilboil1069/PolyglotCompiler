# PolyglotCompiler — Namespace Architecture, Interface & Dependency Analysis

> Document Date: 2026-02-22  
> Analysis Scope: All header and source files, excluding `build/`, `deps/`, `_deps/` directories

---

## Table of Contents

1. [Namespace Overview](#1-namespace-overview)
2. [Namespace Hierarchy Diagram](#2-namespace-hierarchy-diagram)
3. [Inter-Module Dependency Graph](#3-inter-module-dependency-graph)
4. [Detailed Interface Inventory by Namespace](#4-detailed-interface-inventory-by-namespace)
5. [Unused Code Analysis](#5-unused-code-analysis)

---

## 1. Namespace Overview

The project contains **23 namespaces** (including sub-namespaces), covering **~150 classes/structs**, **~50 enumerations**, and **~100 free functions**.

| Namespace | Responsibility | Primary Header Directory |
|-----------|---------------|--------------------------|
| `polyglot::core` | Type system, symbol table, compiler configuration | `common/include/core/` |
| `polyglot::utils` | String pool, Arena allocator, logging | `common/include/utils/` |
| `polyglot::debug` | DWARF5 debug info, debug info builder | `common/include/debug/` |
| `polyglot::ir` | Intermediate Representation (IR nodes, CFG, SSA) | `middle/include/ir/` |
| `polyglot::ir::passes` | IR-level optimization passes | `middle/include/ir/passes/` |
| `polyglot::ir::dialects` | Language dialect extensions (reserved) | `middle/include/ir/dialects/` |
| `polyglot::passes::analysis` | Analysis passes (CFG, alias analysis) | `middle/include/passes/analysis/` |
| `polyglot::passes::transform` | Transform passes (constant folding, DCE, GVN, loop optimizations, etc.) | `middle/include/passes/transform/` |
| `polyglot::passes` | Devirtualization pass | `middle/include/passes/` |
| `polyglot::pgo` | Profile-Guided Optimization | `middle/include/pgo/` |
| `polyglot::lto` | Link-Time Optimization | `middle/include/lto/` |
| `polyglot::frontends` | Frontend common base (Lexer, Parser, Diagnostics) | `frontends/common/include/` |
| `polyglot::cpp` | C++ frontend (AST, Lexer, Parser, Sema, Lowering) | `frontends/cpp/` |
| `polyglot::python` | Python frontend (AST, Lexer, Parser, Sema, Lowering) | `frontends/python/` |
| `polyglot::rust` | Rust frontend (AST, Lexer, Parser, Sema, Lowering) | `frontends/rust/` |
| `polyglot::java` | Java frontend (AST, Lexer, Parser, Sema, Lowering) | `frontends/java/` |
| `polyglot::dotnet` | .NET/C# frontend (AST, Lexer, Parser, Sema, Lowering) | `frontends/dotnet/` |
| `polyglot::ploy` | .ploy frontend (AST, Lexer, Parser, Sema, Lowering) | `frontends/ploy/` |
| `polyglot::backends` | Backend common (target file, debug emission) | `backends/common/include/` |
| `polyglot::backends::x86_64` | x86-64 backend (ISel, RegAlloc, instruction scheduling) | `backends/x86_64/include/` |
| `polyglot::backends::arm64` | ARM64 backend (ISel, RegAlloc) | `backends/arm64/include/` |
| `polyglot::backends::wasm` | WebAssembly backend (WASM code generation) | `backends/wasm/include/` |
| `polyglot::runtime::gc` | Runtime (GC, memory management) | `runtime/include/gc/` |
| `polyglot::runtime::interop` | FFI interop (type mapping, calling conventions, memory) | `runtime/include/interop/` |
| `polyglot::runtime::services` | Threading, exception handling, reflection | `runtime/include/services/` |
| `polyglot::linker` | Linker (ELF/Mach-O/COFF parsing, symbol resolution) | `tools/polyld/include/` |

---

## 2. Namespace Hierarchy Diagram

```
polyglot
├── core                           # Core types and symbols
│   ├── Type / TypeKind            # Type definitions (21 TypeKind variants)
│   ├── TypeSystem                 # Type system (mapping, compatibility, conversion)
│   ├── TypeUnifier                # Type unification / inference
│   ├── TypeRegistry               # Type registry
│   ├── Symbol / SymbolTable       # Symbols and symbol table
│   ├── CompilerConfig             # Compiler configuration
│   └── SourceLoc                  # Source code location
│
├── utils                          # Utility library
│   ├── StringPool                 # String pool (deduplication)
│   ├── Logger                     # Logging utility
│   ├── Arena                      # Memory Arena allocator
│   └── Hash                       # Hash utility
│
├── debug                          # Debug information
│   ├── dwarf (sub-namespace)      # DWARF5 constants & Tag/Attribute enums
│   ├── DIE / LocationExpression   # DWARF debug info entries
│   ├── LineNumberProgram          # Line number program
│   ├── DWARF5Emitter              # DWARF5 section emission
│   ├── DebugInfoBuilder           # High-level debug info builder
│   ├── DebugInfoValidator         # Debug info validation
│   └── DebugInfoPrinter           # Debug info printing
│
├── ir                             # Intermediate Representation
│   ├── IRType                     # IR type system
│   ├── Value / Literal / ...      # IR value nodes
│   ├── Instruction (+ 20 subclasses) # IR instruction nodes
│   ├── BasicBlock / Function      # Control flow graph
│   ├── IRContext                  # IR context
│   ├── IRBuilder                  # IR builder
│   ├── DataLayout                 # Data layout
│   ├── AnalysisCache              # Analysis cache
│   ├── TemplateInstantiator       # Template instantiation
│   ├── ClassLayout                # Class layout & VTable
│   ├── IRVisitor                  # IR visitor base
│   ├── IRPrinter                  # IR printer
│   ├── IRParser                   # IR parser
│   ├── SSAConstructor             # SSA construction
│   ├── IRVerifier                 # IR verifier
│   ├── passes/                    # IR optimization passes
│   │   ├── ConstantFold
│   │   ├── DeadCodeElim
│   │   ├── CommonSubexprElim
│   │   ├── Inline
│   │   ├── StrengthReduce
│   │   ├── GVN
│   │   ├── LICM
│   │   └── Vectorize
│   └── dialects/                  # Language dialect extensions
│       ├── high_level
│       ├── mid_level
│       └── low_level
│
├── passes
│   ├── analysis/                  # Analysis
│   │   ├── Dominance analysis
│   │   └── Alias analysis
│   ├── transform/                 # Transforms
│   │   ├── ConstantFolding
│   │   ├── DeadCodeElimination
│   │   ├── CommonSubexprElimination
│   │   ├── Inlining
│   │   ├── GVNPass / PREPass / AliasAnalysisPass
│   │   ├── LoopAnalysis / LoopUnrolling / LICM / LoopFusion
│   │   ├── LoopStrengthReduction
│   │   ├── DevirtualizationPass
│   │   └── Advanced (Tail Call, Tiling, AutoVec, etc.)
│   └── DevirtualizationPass      # Top-level devirtualization
│
├── pgo                            # Profile-Guided Optimization
│   ├── FunctionProfile
│   ├── ProfileData
│   └── RuntimeProfiler
│
├── lto                            # Link-Time Optimization
│   ├── LTOModule / ModuleIndex
│   ├── CrossModuleInliner
│   ├── DeadSymbolEliminator
│   ├── InterproceduralConstantPropagation
│   ├── CrossModuleDevirtualizer
│   ├── CrossModuleGVN
│   ├── LTOOptimizer
│   └── LTOLinker
│
├── frontends                      # Frontend common
│   ├── TokenKind / Token          # Lexical tokens
│   ├── LexerBase                  # Lexer base class
│   ├── ParserBase                 # Parser base class
│   ├── Diagnostics                # Diagnostics system
│   ├── DiagnosticStore            # Diagnostics storage
│   ├── SemaContext                # Semantic analysis context
│   ├── Preprocessor               # Preprocessor
│   └── TokenPool                  # Token pool
│
├── cpp                            # C++ frontend
│   ├── AST (~45 node types)
│   ├── CppLexer
│   ├── CppParser
│   ├── RunCppSema()
│   ├── LowerCppToIR()
│   ├── ConstexprValue / ConstexprEvaluator
│   └── ConstexprAnalyzer
│
├── python                         # Python frontend
│   ├── AST (~50 node types)
│   ├── PythonLexer
│   ├── PythonParser
│   ├── RunPythonSema()
│   └── LowerPythonToIR()
│
├── rust                           # Rust frontend
│   ├── AST (~60 node types)
│   ├── RustLexer
│   ├── RustParser
│   ├── RunRustSema()
│   └── LowerRustToIR()
│
├── java                           # Java frontend
│   ├── AST
│   ├── JavaLexer
│   ├── JavaParser
│   ├── RunJavaSema()
│   └── LowerJavaToIR()
│
├── dotnet                         # .NET/C# frontend
│   ├── AST
│   ├── DotnetLexer
│   ├── DotnetParser
│   ├── RunDotnetSema()
│   └── LowerDotnetToIR()
│
├── ploy                           # .ploy frontend
│   ├── AST (~60 node types)
│   ├── PloyLexer
│   ├── PloyParser
│   ├── PloySema
│   └── PloyLowering
│
├── backends                       # Backend common
│   ├── TargetMachine (abstract)
│   ├── ObjectFileBuilder / ELFBuilder / MachOBuilder
│   ├── DebugInfoBuilder (backends version)
│   ├── DebugEmitter
│   ├── DWARF5Generator (backends version)
│   ├── Relocation
│   ├── x86_64/                    # x86-64 backend
│   │   ├── X86Target (: TargetMachine)
│   │   ├── Register / MachineInstr / MachineBlock
│   │   ├── ISel / RegAlloc
│   │   ├── InstructionScheduler / SoftwarePipeliner
│   │   ├── MicroArchOptimizer / RegisterRenamer
│   │   ├── CacheOptimizer / BranchOptimizer
│   │   └── CallingConvention [anonymous namespace, internal]
│   ├── arm64/                     # ARM64 backend
│   │   ├── ARM64Target (: TargetMachine)
│   │   ├── Register / MachineInstr / MachineBlock
│   │   ├── ISel / RegAlloc
│   │   └── CallingConvention [anonymous namespace, internal]
│   └── wasm/                      # WebAssembly backend
│       └── WASMTarget (: TargetMachine)
│
├── runtime                        # Runtime
│   ├── gc/                        # Garbage collection
│   │   ├── GarbageCollector (abstract)
│   │   ├── Heap / RootHandle
│   │   ├── GC strategies (MarkSweep/Generational/Copying/Incremental)
│   │   └── C linkage API (poly_alloc, poly_gc, etc.)
│   ├── interop/                   # FFI interop
│   │   ├── TypeMapping / Marshalling
│   │   ├── CallingConvention (runtime layer)
│   │   ├── ManagedBuffer / Memory
│   │   ├── ObjectLifecycle / OwnershipTracker
│   │   ├── ContainerMarshal
│   │   └── FFI (HandleTable, DynamicLibrary, FFIRegistry)
│   └── services/                  # Runtime services
│       ├── ThreadLocal / Thread / ThreadPool
│       ├── TaskScheduler / WorkStealingScheduler
│       ├── RWLock / Barrier / Semaphore
│       ├── ConcurrentQueue / ConcurrentStack
│       ├── Coroutine / CoroutineScheduler
│       ├── Future / Promise
│       ├── Atomic
│       ├── ThreadProfiler
│       ├── ReflectionRegistry
│       ├── RuntimeError / exception handling
│       └── exception.h (compatibility shim)
│
└── linker                         # Linker
    ├── ObjectFile / LinkerSymbol / Relocation
    ├── Archive / Segment / OutputSection
    ├── LinkerConfig / LinkStats
    ├── Linker
    └── LinkerScriptParser
```

---

## 3. Inter-Module Dependency Graph

```
                            ┌──────────────────┐
                            │   tools/polyc    │  Compiler driver
                            │    (driver.cpp)  │
                            └────────┬─────────┘
                                     │ invokes
          ┌──────────┬───────────────┼───────────────┬──────────┐
          │          │               │               │          │
 ┌────────▼────┐ ┌───▼──────┐ ┌─────▼─────┐ ┌──────▼───┐ ┌────▼───────┐
 │frontend_cpp │ │frontend_ │ │frontend_  │ │frontend_ │ │frontend_   │
 │ CppLexer    │ │python    │ │rust       │ │java      │ │dotnet      │
 │ CppParser   │ │PythonLex │ │RustLexer  │ │JavaLexer │ │DotnetLexer │
 │ RunCppSema()│ │PythonPar │ │RustParser │ │JavaParse │ │DotnetParse │
 │LowerCppTo  │ │RunPython │ │RunRustSe  │ │RunJavaSe │ │RunDotnetSe │
 │  IR()       │ │ Sema()   │ │  ma()     │ │  ma()    │ │  ma()      │
 └────────┬────┘ └───┬──────┘ └─────┬─────┘ └──────┬───┘ └────┬───────┘
          │          │               │               │          │
          └──────────┴───────┬───────┴───────┬───────┴──────────┘
                             │               │
                    ┌────────▼────────┐  ┌───▼────────────┐
                    │frontend_common  │  │ frontend_ploy  │
                    │LexerBase,Parser │  │ PloyLexer      │
                    │Base,Diagnostics │  │ PloyParser     │
                    │Preprocessor     │  │ PloySema       │
                    └────────┬────────┘  │ PloyLowering   │
                             │           └───┬────────────┘
                             │ depend        │
                             ▼               │
        ┌────────────────────────────────────┼─────────────────┐
        │            polyglot_common         │                 │
        │  core: Type, TypeSystem, SymTable  │                 │
        │  utils: StringPool, Logger, Arena  │                 │
        │  debug: DWARF5, DebugInfoBuilder   │                 │
        │  backends/common: DebugEmitter     │                 │
        └──────────────────┬─────────────────┘                 │
                           │                                   │
               ┌───────────┼───────────┐                       │
               ▼           ▼           ▼                       │
        ┌──────────┐ ┌──────────┐ ┌──────────┐                │
        │middle_ir │ │ middle   │ │middle_ir │                │
        │IRBuilder │ │ passes   │ │PGO / LTO │                │
        │IRContext │ │(optimize)│ │Template  │                │
        │DataLayout│ │          │ │ClassLay  │                │
        └────┬─────┘ └────┬─────┘ └────┬─────┘                │
             │             │            │                      │
             └─────────────┼────────────┘                      │
                           ▼                                   │
               ┌───────────┴──────────────┬────────────────────┘
               │                          │
        ┌──────▼──────┐  ┌───────▼───────┐ ┌──────────────┐
        │backend_x86  │  │backend_arm64  │ │ backend_wasm │
        │ X86Target   │  │ ARM64Target   │ │ WASMTarget   │
        │ ISel/RegAll │  │ ISel/RegAlloc │ │              │
        │ Scheduler   │  │               │ │              │
        └──────┬──────┘  └───────┬───────┘ └──────┬───────┘
               │                 │                │
               └─────────────┬──┴────────────────┘
                             ▼
                     ┌───────────────┐
                     │    runtime    │
                     │  GC / FFI    │
                     │  Threading   │
                     │  Reflection  │
                     └───────────────┘

        ┌────────────┐  ┌────────────┐  ┌────────────┐  ┌──────────────┐
        │ tools/     │  │ tools/     │  │ tools/     │  │ tools/       │
        │ polyasm    │  │ polyld     │  │ polyopt    │  │ polybench    │
        │ (assembler)│  │ (linker)   │  │ (optimizer)│  │ (benchmark)  │
        └────────────┘  └────────────┘  └────────────┘  └──────────────┘
```

### CMake Target Dependencies

```
polyglot_common ◄──── frontend_common ◄──── frontend_python
       ▲                    ▲                frontend_cpp
       │                    │                frontend_rust
       │                    │                frontend_java
       │                    │                frontend_dotnet
       │                    │                frontend_ploy
       ├── middle_ir        │
       ├── backend_x86_64   │
       ├── backend_arm64    │
       ├── backend_wasm     │
       │                    │
       └── linker_lib       │
                            │
polyc ──────► frontend_python, frontend_cpp, frontend_rust,
              frontend_java, frontend_dotnet, frontend_ploy,
              backend_x86_64, backend_arm64, backend_wasm,
              middle_ir, runtime

unit_tests ──► all libraries + Catch2::Catch2WithMain
```

---

## 4. Detailed Interface Inventory by Namespace

### 4.1 `polyglot::core`

| Class/Struct | Key Interfaces | Called From |
|-------------|---------------|------------|
| `Type` (struct) | `Int()`, `Float()`, `Array()`, `Optional()`, `Slice()`, `Tuple()`, `GenericInstance()`, `IsNumeric()`, `IsInteger()`, `ToString()`, etc. | All frontend sema/lowering, middle_ir, tests |
| `TypeSystem` | `MapFromLanguage()`, `CanImplicitlyConvert()`, `IsCompatible()`, `SizeOf()`, `AlignOf()`, `CommonType()` | Frontend sema/lowering, middle_ir |
| `TypeUnifier` | `Unify()`, `Apply()`, `AddTraitConstraint()` | ⚠️ Tests only |
| `TypeRegistry` | `Register()`, `Find()`, `RegisterEquivalence()` | ⚠️ Tests only |
| `SymbolTable` | `EnterScope()`, `ExitScope()`, `Declare()`, `Lookup()`, `ResolveFunction()` | All frontend sema |
| `CompilerConfig` | `SetOption()`, `GetOption()` | ❌ No references |
| `SourceLoc` | `file`, `line`, `column` | Widely used across project |

### 4.2 `polyglot::utils`

| Class | Key Interfaces | Called From |
|-------|---------------|------------|
| `StringPool` | `Intern()`, `Size()` | ❌ No references |
| `Logger` | `Log()` | ❌ No references |
| `Arena` | `Allocate()`, `Reset()` | ❌ No references |

### 4.3 `polyglot::debug`

| Class | Key Interfaces | Called From |
|-------|---------------|------------|
| `DIE` | `SetAttribute()`, `AddChild()`, `Encode()` | Internal to dwarf5.cpp |
| `LocationExpression` | `AddOp()`, `Encode()` | Internal to dwarf5.cpp |
| `DWARF5Emitter` | `SetCompileUnit()`, `AddBaseType()`, `GenerateSections()` | Internal to dwarf5.cpp |
| `DebugInfoBuilder` (debug ver.) | `CreateFunction()`, `CreateCompileUnit()`, `GenerateDWARF()` | debug_emitter.cpp, tests |
| `DebugInfoValidator` | `Validate()` | ❌ No references |
| `DebugInfoPrinter` | `Print()`, `PrintTypes()`, etc. | ❌ No references |

### 4.4 `polyglot::ir`

| Class | Key Interfaces | Called From |
|-------|---------------|------------|
| `IRContext` | `CreateFunction()`, `CreateGlobal()` | All frontend lowering, polyopt |
| `IRBuilder` | `MakeLiteral()`, `MakeBinary()`, `MakeCall()`, etc. | All frontend lowering |
| `DataLayout` | `SizeOf()`, `AlignOf()`, `PointerSize()` | middle_ir passes, backends |
| `AnalysisCache` | `GetCFG()`, `GetDomTree()`, `GetLoops()` | Internal to passes |
| `TemplateInstantiator` | `InstantiateClass()`, `InstantiateFunction()` | C++ frontend sema |
| `ClassLayout` | `RegisterClass()`, `GetLayout()` | C++ frontend lowering |
| `InvokeInstruction` | (Exception handling instruction) | C++ lowering, lto, gvn |
| `LandingPadInstruction` | (Exception handling instruction) | C++ lowering |
| `ResumeInstruction` | (Exception handling instruction) | Declared, ⚠️ C++ lowering indirect |
| `VectorInstruction` | (SIMD vector instruction) | advanced_optimizations, x86 isel |
| `IRVisitor` | `Visit()` | ❌ Declaration only (`common/include/ir/ir_visitor.h`) |

### 4.5 `polyglot::passes::transform`

| Class/Function | Called From |
|---------------|------------|
| `ConstantFolding()` | ⚠️ Tests only |
| `DeadCodeElimination()` | ⚠️ Tests only |
| `CommonSubexprElimination()` | ⚠️ Tests only |
| `Inlining()` | ⚠️ Tests only |
| `GVNPass` | ⚠️ Tests only |
| `PREPass` | ⚠️ Tests only |
| `AliasAnalysisPass` | ⚠️ Tests only |
| `LoopAnalysis` | ⚠️ Tests only + internal |
| `LoopUnrollingPass` | ⚠️ Tests only |
| `LICMPass` | ⚠️ Tests only |
| `LoopFusionPass` | ⚠️ Tests only |
| `LoopStrengthReductionPass` | ⚠️ Tests only |
| `DevirtualizationPass` | ⚠️ Tests only |
| `TailCallOptimization()` | ⚠️ Tests only |
| `LoopTiling()` | ⚠️ Tests only |
| Other advanced_optimizations functions | ⚠️ Tests only |

### 4.6 `polyglot::pgo`

| Class | Called From |
|-------|------------|
| `ProfileData` | ⚠️ Tests only |
| `RuntimeProfiler` | ⚠️ Tests only |
| `FunctionProfile` | ⚠️ Tests only |

### 4.7 `polyglot::lto`

| Class | Called From |
|-------|------------|
| `LTOModule` / `ModuleIndex` | ⚠️ Tests only + LTO internal |
| `CrossModuleInliner` | ⚠️ Tests only + LTO internal |
| `DeadSymbolEliminator` | ⚠️ Tests only + LTO internal |
| `InterproceduralConstantPropagation` | ⚠️ Tests only + LTO internal |
| `CrossModuleDevirtualizer` | ⚠️ Tests only + LTO internal |
| `CrossModuleGVN` | ⚠️ Tests only + LTO internal |
| `LTOOptimizer` | ⚠️ Tests only |
| `LTOLinker` | ⚠️ Tests only |

### 4.8 `polyglot::backends`

| Class | Called From |
|-------|------------|
| `TargetMachine` (abstract) | x86_64/arm64/wasm target implementations |
| `ObjectFileBuilder` / `ELFBuilder` / `MachOBuilder` | ❌ Self-definition only |
| `DebugInfoBuilder` (backends ver.) | debug_emitter.cpp, tests |
| `DebugEmitter` | ⚠️ Tests only |
| `DWARF5Generator` (backends ver.) | Internal to debug_emitter.cpp |
| `X86Target` / `ARM64Target` / `WASMTarget` | polyc driver, polyasm |
| `InstructionScheduler` | ⚠️ Tests only |
| `SoftwarePipeliner` | ⚠️ Tests only |
| `MicroArchOptimizer` | ⚠️ Tests only |
| `RegisterRenamer` | ⚠️ Tests only |
| `CacheOptimizer` | ⚠️ Tests only |
| `BranchOptimizer` | ⚠️ Tests only |
| calling_convention (x86_64) | ❌ Anonymous namespace, no external calls |
| calling_convention (arm64) | ❌ Anonymous namespace, no external calls |

### 4.9 `polyglot::runtime`

| Class | Called From |
|-------|------------|
| `Heap` / `RootHandle` | ⚠️ Tests only |
| GC strategy classes (MarkSweep/Generational/Copying/Incremental) | ⚠️ Tests only (via CreateGC) |
| C linkage API (`poly_alloc`, etc.) | ⚠️ Tests only + stub implementation |
| `FFIRegistry` | ⚠️ Tests only |
| `DynamicLibrary` | ⚠️ Tests only |
| `HandleTable` | ⚠️ Tests only |
| `ObjectLifecycle` / `OwnershipTracker` | ⚠️ Tests only |
| `ContainerMarshal` | ⚠️ Tests only |
| `TaskScheduler` | ⚠️ Tests only |
| `WorkStealingScheduler` | ⚠️ Tests only |
| `CoroutineScheduler` | ⚠️ Tests only |
| `Future` / `Promise` | ⚠️ Tests only |
| `Atomic` | ⚠️ Tests only |
| `ThreadProfiler` | ❌ No references (declared but not implemented) |
| `ReflectionRegistry` | ⚠️ Tests only |
| `TokenPool` | ❌ Declaration only, cpp is an empty stub |

### 4.10 `polyglot::linker`

| Class | Called From |
|-------|------------|
| `Linker` | polyld driver, tests |
| `LinkerScriptParser` | ⚠️ Tests only (not called from linker main flow) |

---

## 5. Unused Code Analysis

### 5.1 🔴 Completely Unused (never referenced by any production code or tests)

| # | Item | Location | Notes |
|---|------|----------|-------|
| 1 | `CompilerConfig` | `common/include/core/config.h` | Class declared but no file includes this header |
| 2 | `StringPool` | `common/include/utils/string_pool.h` | Declared with zero references |
| 3 | `Logger` | `common/include/utils/logging.h` | Declared with zero references |
| 4 | `Arena` | `common/include/utils/arena.h` | Declared with zero references |
| 5 | `DebugInfoValidator` | `common/include/debug/debug_info_builder.h` | Declared with zero references |
| 6 | `DebugInfoPrinter` | `common/include/debug/debug_info_builder.h` | Declared with zero references |
| 7 | `IRVisitor` | `common/include/ir/ir_visitor.h` | Abstract base class, no subclass implementation |
| 8 | `TokenPool` | `frontends/common/include/token_pool.h` | Declared but cpp is empty stub, zero references |
| 9 | `ThreadProfiler` | `runtime/include/services/threading.h` | Static methods declared only, no implementation |
| 10 | `ObjectFileBuilder`/`ELFBuilder`/`MachOBuilder` | `backends/common/include/object_file.h` | Declared + defined but no external calls |
| 11 | calling_convention (x86_64) | `backends/x86_64/src/calling_convention.cpp` | All code in anonymous namespace with no external calls |
| 12 | calling_convention (arm64) | `backends/arm64/src/calling_convention.cpp` | All code in anonymous namespace with no external calls |

### 5.2 🟡 Tests Only (complete implementation and tests, but not integrated into compile pipeline)

| # | Item | Notes |
|---|------|-------|
| 1 | `TypeUnifier` | Type unifier, tested only in type_system_test.cpp |
| 2 | `TypeRegistry` | Type registry, tested only in type_system_test.cpp |
| 3 | `TypeSystem::AreLayoutCompatible` | Test calls only |
| 4 | `TypeSystem::IsWidening/IsNarrowing` | Test calls only |
| 5 | `TypeSystem::ConversionRank` | Test calls only |
| 6 | `TypeSystem::RegisterAlias/ResolveAlias/HasAlias` | Test calls only |
| 7 | `SymbolKindToString/ScopeKindToString/FormatSymbol/FormatScope` | Helper functions, test calls only |
| 8 | **All passes::transform optimizations** | 17+ optimization passes/functions, tests only |
| 9 | **All PGO classes** | ProfileData, RuntimeProfiler, tests only |
| 10 | **All LTO classes** | LTOModule/Optimizer/Linker etc., tests only |
| 11 | **All backend optimization classes** | InstructionScheduler, SoftwarePipeliner, MicroArchOptimizer, etc., tests only |
| 12 | **All runtime services** | GC Heap, FFI, Threading (TaskScheduler/WorkStealing/Coroutine, etc.), Reflection, tests only |
| 13 | `DebugEmitter` | EmitDWARF/EmitPDB/EmitSourceMap, tests only |
| 14 | `LinkerScriptParser` | Declared + defined, tests only, not called from Linker main flow |

### 5.3 📊 Statistical Summary

| Category | Count | Proportion |
|----------|-------|-----------|
| Completely unused | **12 items** | ~10% |
| Tests only | **40+ items** | ~33% |
| Production code usage | **~70 items** | ~57% |

---

*This document is based on static analysis of 98 header files and 80 source files. Components marked "tests only" are fully functional with complete implementations and test coverage — they have simply not yet been integrated into the compiler's main compilation pipeline.*
