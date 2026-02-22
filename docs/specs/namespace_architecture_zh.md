# PolyglotCompiler — 命名空间架构、接口与依赖分析

> 文档生成日期：2026-02-22  
> 分析范围：全部头文件与源文件，排除 `build/`、`deps/`、`_deps/` 目录

---

## 目录

1. [命名空间总览](#1-命名空间总览)
2. [命名空间层级架构图](#2-命名空间层级架构图)
3. [模块间依赖关系图](#3-模块间依赖关系图)
4. [各命名空间详细接口清单](#4-各命名空间详细接口清单)
5. [未使用代码分析](#5-未使用代码分析)

---

## 1. 命名空间总览

项目共包含 **23 个命名空间**（含子命名空间），覆盖 **~150 个类/结构体**、**~50 个枚举**、**~100 个自由函数**。

| 命名空间 | 职责 | 主要头文件目录 |
|----------|------|---------------|
| `polyglot::core` | 类型系统、符号表、编译器配置 | `common/include/core/` |
| `polyglot::utils` | 字符串池、Arena 分配器、日志 | `common/include/utils/` |
| `polyglot::debug` | DWARF5 调试信息、调试信息构建器 | `common/include/debug/` |
| `polyglot::ir` | 中间表示（IR 节点、CFG、SSA） | `middle/include/ir/` |
| `polyglot::ir::passes` | IR 层优化 pass | `middle/include/ir/passes/` |
| `polyglot::ir::dialects` | 语言方言扩展（预留） | `middle/include/ir/dialects/` |
| `polyglot::passes::analysis` | 分析 pass（CFG、别名分析） | `middle/include/passes/analysis/` |
| `polyglot::passes::transform` | 变换 pass（常量折叠、DCE、GVN、循环优化等） | `middle/include/passes/transform/` |
| `polyglot::passes` | 去虚拟化 pass | `middle/include/passes/` |
| `polyglot::pgo` | Profile-Guided Optimization | `middle/include/pgo/` |
| `polyglot::lto` | Link-Time Optimization | `middle/include/lto/` |
| `polyglot::frontends` | 前端公共基础（Lexer、Parser、诊断） | `frontends/common/include/` |
| `polyglot::cpp` | C++ 前端（AST、Lexer、Parser、Sema、Lowering） | `frontends/cpp/` |
| `polyglot::python` | Python 前端（AST、Lexer、Parser、Sema、Lowering） | `frontends/python/` |
| `polyglot::rust` | Rust 前端（AST、Lexer、Parser、Sema、Lowering） | `frontends/rust/` |
| `polyglot::java` | Java 前端（AST、Lexer、Parser、Sema、Lowering） | `frontends/java/` |
| `polyglot::dotnet` | .NET/C# 前端（AST、Lexer、Parser、Sema、Lowering） | `frontends/dotnet/` |
| `polyglot::ploy` | .ploy 前端（AST、Lexer、Parser、Sema、Lowering） | `frontends/ploy/` |
| `polyglot::backends` | 后端公共（目标文件、调试发射） | `backends/common/include/` |
| `polyglot::backends::x86_64` | x86-64 后端（ISel、RegAlloc、指令调度） | `backends/x86_64/include/` |
| `polyglot::backends::arm64` | ARM64 后端（ISel、RegAlloc） | `backends/arm64/include/` |
| `polyglot::backends::wasm` | WebAssembly 后端（WASM 代码生成） | `backends/wasm/include/` |
| `polyglot::runtime::gc` | 运行时（GC、内存管理） | `runtime/include/gc/` |
| `polyglot::runtime::interop` | FFI 互操作（类型映射、调用约定、内存） | `runtime/include/interop/` |
| `polyglot::runtime::services` | 线程、异常处理、反射 | `runtime/include/services/` |
| `polyglot::linker` | 链接器（ELF/Mach-O/COFF 解析、符号解析） | `tools/polyld/include/` |

---

## 2. 命名空间层级架构图

```
polyglot
├── core                           # 核心类型与符号
│   ├── Type / TypeKind            # 类型定义（21 种 TypeKind）
│   ├── TypeSystem                 # 类型系统（映射、兼容性、转换）
│   ├── TypeUnifier                # 类型统一/推断
│   ├── TypeRegistry               # 类型注册表
│   ├── Symbol / SymbolTable       # 符号与符号表
│   ├── CompilerConfig             # 编译器配置
│   └── SourceLoc                  # 源代码位置
│
├── utils                          # 工具库
│   ├── StringPool                 # 字符串池（去重）
│   ├── Logger                     # 日志工具
│   ├── Arena                      # 内存 Arena 分配器
│   └── Hash                       # 哈希工具
│
├── debug                          # 调试信息
│   ├── dwarf (子空间)             # DWARF5 常量 & Tag/Attribute 枚举
│   ├── DIE / LocationExpression   # DWARF 调试信息条目
│   ├── LineNumberProgram          # 行号程序
│   ├── DWARF5Emitter              # DWARF5 Section 发射
│   ├── DebugInfoBuilder           # 高层调试信息构建
│   ├── DebugInfoValidator         # 调试信息验证
│   └── DebugInfoPrinter           # 调试信息打印
│
├── ir                             # 中间表示
│   ├── IRType                     # IR 类型系统
│   ├── Value / Literal / ...      # IR 值节点
│   ├── Instruction (+ 20 子类)   # IR 指令节点
│   ├── BasicBlock / Function      # 控制流图
│   ├── IRContext                  # IR 上下文
│   ├── IRBuilder                  # IR 构建器
│   ├── DataLayout                 # 数据布局
│   ├── AnalysisCache              # 分析缓存
│   ├── TemplateInstantiator       # 模板实例化
│   ├── ClassLayout                # 类布局 & VTable
│   ├── IRVisitor                  # IR 访问者基类
│   ├── IRPrinter                  # IR 打印器
│   ├── IRParser                   # IR 解析器
│   ├── SSAConstructor             # SSA 构造
│   ├── IRVerifier                 # IR 验证器
│   ├── passes/                    # IR 优化 pass
│   │   ├── ConstantFold
│   │   ├── DeadCodeElim
│   │   ├── CommonSubexprElim
│   │   ├── Inline
│   │   ├── StrengthReduce
│   │   ├── GVN
│   │   ├── LICM
│   │   └── Vectorize
│   └── dialects/                  # 语言方言扩展
│       ├── high_level
│       ├── mid_level
│       └── low_level
│
├── passes
│   ├── analysis/                  # 分析
│   │   ├── 支配树分析
│   │   └── 别名分析
│   ├── transform/                 # 变换
│   │   ├── ConstantFolding
│   │   ├── DeadCodeElimination
│   │   ├── CommonSubexprElimination
│   │   ├── Inlining
│   │   ├── GVNPass / PREPass / AliasAnalysisPass
│   │   ├── LoopAnalysis / LoopUnrolling / LICM / LoopFusion
│   │   ├── LoopStrengthReduction
│   │   ├── DevirtualizationPass
│   │   └── Advanced (Tail Call, Tiling, AutoVec 等)
│   └── DevirtualizationPass      # 顶层去虚拟化
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
├── frontends                      # 前端公共
│   ├── TokenKind / Token          # 词法单元
│   ├── LexerBase                  # 词法分析基类
│   ├── ParserBase                 # 语法分析基类
│   ├── Diagnostics                # 诊断系统
│   ├── DiagnosticStore            # 诊断存储
│   ├── SemaContext                # 语义分析上下文
│   ├── Preprocessor               # 预处理器
│   └── TokenPool                  # Token 池
│
├── cpp                            # C++ 前端
│   ├── AST (~45 节点类型)
│   ├── CppLexer
│   ├── CppParser
│   ├── RunCppSema()
│   ├── LowerCppToIR()
│   ├── ConstexprValue / ConstexprEvaluator
│   └── ConstexprAnalyzer
│
├── python                         # Python 前端
│   ├── AST (~50 节点类型)
│   ├── PythonLexer
│   ├── PythonParser
│   ├── RunPythonSema()
│   └── LowerPythonToIR()
│
├── rust                           # Rust 前端
│   ├── AST (~60 节点类型)
│   ├── RustLexer
│   ├── RustParser
│   ├── RunRustSema()
│   └── LowerRustToIR()
│
├── java                           # Java 前端
│   ├── AST
│   ├── JavaLexer
│   ├── JavaParser
│   ├── RunJavaSema()
│   └── LowerJavaToIR()
│
├── dotnet                         # .NET/C# 前端
│   ├── AST
│   ├── DotnetLexer
│   ├── DotnetParser
│   ├── RunDotnetSema()
│   └── LowerDotnetToIR()
│
├── ploy                           # .ploy 前端
│   ├── AST (~60 节点类型)
│   ├── PloyLexer
│   ├── PloyParser
│   ├── PloySema
│   └── PloyLowering
│
├── backends                       # 后端公共
│   ├── TargetMachine (abstract)
│   ├── ObjectFileBuilder / ELFBuilder / MachOBuilder
│   ├── DebugInfoBuilder (backends版)
│   ├── DebugEmitter
│   ├── DWARF5Generator (backends版)
│   ├── Relocation
│   ├── x86_64/                    # x86-64 后端
│   │   ├── X86Target (: TargetMachine)
│   │   ├── Register / MachineInstr / MachineBlock
│   │   ├── ISel / RegAlloc
│   │   ├── InstructionScheduler / SoftwarePipeliner
│   │   ├── MicroArchOptimizer / RegisterRenamer
│   │   ├── CacheOptimizer / BranchOptimizer
│   │   └── CallingConvention [匿名namespace, 内部]
│   ├── arm64/                     # ARM64 后端
│   │   ├── ARM64Target (: TargetMachine)
│   │   ├── Register / MachineInstr / MachineBlock
│   │   ├── ISel / RegAlloc
│   │   └── CallingConvention [匿名namespace, 内部]
│   └── wasm/                      # WebAssembly 后端
│       └── WASMTarget (: TargetMachine)
│
├── runtime                        # 运行时
│   ├── gc/                        # 垃圾收集
│   │   ├── GarbageCollector (abstract)
│   │   ├── Heap / RootHandle
│   │   ├── GC 策略 (MarkSweep/Generational/Copying/Incremental)
│   │   └── C linkage API (poly_alloc, poly_gc 等)
│   ├── interop/                   # FFI 互操作
│   │   ├── TypeMapping / Marshalling
│   │   ├── CallingConvention (runtime层)
│   │   ├── ManagedBuffer / Memory
│   │   ├── ObjectLifecycle / OwnershipTracker
│   │   ├── ContainerMarshal
│   │   └── FFI (HandleTable, DynamicLibrary, FFIRegistry)
│   └── services/                  # 运行时服务
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
│       └── exception.h (兼容 shim)
│
└── linker                         # 链接器
    ├── ObjectFile / LinkerSymbol / Relocation
    ├── Archive / Segment / OutputSection
    ├── LinkerConfig / LinkStats
    ├── Linker
    └── LinkerScriptParser
```

---

## 3. 模块间依赖关系图

```
                            ┌──────────────────┐
                            │   tools/polyc    │  编译器驱动
                            │    (driver.cpp)  │
                            └────────┬─────────┘
                                     │ 调用
          ┌──────────┬───────────────┼───────────────┬──────────┐
          │          │               │               │          │
 ┌────────▼────┐ ┌───▼──────┐ ┌─────▼─────┐ ┌──────▼───┐ ┌────▼───────┐
 │frontend_cpp │ │frontend_ │ │frontend_  │ │frontend_ │ │frontend_   │
 │ CppLexer    │ │python    │ │rust       │ │java      │ │dotnet      │
 │ CppParser   │ │PythonLex │ │RustLexer  │ │JavaLexer │ │DotnetLexer │
 │ RunCppSema()│ │PythonPar │ │RustParser │ │JavaParse │ │DotnetParse │
 │LowerCppTo   │ │RunPython │ │RunRustSe  │ │RunJavaSe │ │RunDotnetSe │
 │  IR()       │ │ Sema()   │ │  ma()     │ │  ma()    │ │  ma()      │
 └────────┬────┘ └───┬──────┘ └──────┬────┘ └────────┬─┘ └──────┬─────┘
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
                             │ 依赖           │
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
        ┌──────────┐ ┌──────────┐ ┌──────────┐                 │
        │middle_ir │ │ middle   │ │middle_ir │                 │
        │IRBuilder │ │ passes   │ │PGO / LTO │                 │
        │IRContext │ │(优化)     │ │Template  │                │
        │DataLayout│ │          │ │ClassLay  │                 │
        └────┬─────┘ └─────┬────┘ └─────┬────┘                 │
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
                     │  GC / FFI     │
                     │  Threading    │
                     │  Reflection   │
                     └───────────────┘

        ┌────────────┐  ┌────────────┐  ┌────────────┐  ┌──────────────┐
        │ tools/     │  │ tools/     │  │ tools/     │  │ tools/       │
        │ polyasm    │  │ polyld     │  │ polyopt    │  │ polybench    │
        │ (汇编器)    │  │ (链接器)    │  │ (优化器)    │  │ (基准测试)    │
        └────────────┘  └────────────┘  └────────────┘  └──────────────┘
```

### CMake 目标依赖关系

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
       └── linker_lib       │
                            │
polyc ──────► frontend_python, frontend_cpp, frontend_rust,
              frontend_java, frontend_dotnet, frontend_ploy,
              backend_x86_64, backend_arm64, backend_wasm,
              middle_ir, runtime

unit_tests ──► 全部库 + Catch2::Catch2WithMain
```

---

## 4. 各命名空间详细接口清单

### 4.1 `polyglot::core`

| 类/结构体 | 关键接口 | 被调用位置 |
|-----------|---------|-----------|
| `Type` (struct) | `Int()`, `Float()`, `Array()`, `Optional()`, `Slice()`, `Tuple()`, `GenericInstance()`, `IsNumeric()`, `IsInteger()`, `ToString()` 等 | 全部前端 sema/lowering，middle_ir，测试 |
| `TypeSystem` | `MapFromLanguage()`, `CanImplicitlyConvert()`, `IsCompatible()`, `SizeOf()`, `AlignOf()`, `CommonType()` | 前端 sema/lowering，middle_ir |
| `TypeUnifier` | `Unify()`, `Apply()`, `AddTraitConstraint()` | ⚠️ 仅测试 |
| `TypeRegistry` | `Register()`, `Find()`, `RegisterEquivalence()` | ⚠️ 仅测试 |
| `SymbolTable` | `EnterScope()`, `ExitScope()`, `Declare()`, `Lookup()`, `ResolveFunction()` | 全部前端 sema |
| `CompilerConfig` | `SetOption()`, `GetOption()` | ❌ 无任何引用 |
| `SourceLoc` | `file`, `line`, `column` | 全项目广泛使用 |

### 4.2 `polyglot::utils`

| 类 | 关键接口 | 被调用位置 |
|----|---------|-----------|
| `StringPool` | `Intern()`, `Size()` | ❌ 无任何引用 |
| `Logger` | `Log()` | ❌ 无任何引用 |
| `Arena` | `Allocate()`, `Reset()` | ❌ 无任何引用 |

### 4.3 `polyglot::debug`

| 类 | 关键接口 | 被调用位置 |
|----|---------|-----------|
| `DIE` | `SetAttribute()`, `AddChild()`, `Encode()` | dwarf5.cpp 内部 |
| `LocationExpression` | `AddOp()`, `Encode()` | dwarf5.cpp 内部 |
| `DWARF5Emitter` | `SetCompileUnit()`, `AddBaseType()`, `GenerateSections()` | dwarf5.cpp 内部 |
| `DebugInfoBuilder` (debug版) | `CreateFunction()`, `CreateCompileUnit()`, `GenerateDWARF()` | debug_emitter.cpp，测试 |
| `DebugInfoValidator` | `Validate()` | ❌ 无任何引用 |
| `DebugInfoPrinter` | `Print()`, `PrintTypes()` 等 | ❌ 无任何引用 |

### 4.4 `polyglot::ir`

| 类 | 关键接口 | 被调用位置 |
|----|---------|-----------|
| `IRContext` | `CreateFunction()`, `CreateGlobal()` | 全部前端 lowering，polyopt |
| `IRBuilder` | `MakeLiteral()`, `MakeBinary()`, `MakeCall()` 等 | 全部前端 lowering |
| `DataLayout` | `SizeOf()`, `AlignOf()`, `PointerSize()` | middle_ir passes，backends |
| `AnalysisCache` | `GetCFG()`, `GetDomTree()`, `GetLoops()` | passes 内部 |
| `TemplateInstantiator` | `InstantiateClass()`, `InstantiateFunction()` | C++ 前端 sema |
| `ClassLayout` | `RegisterClass()`, `GetLayout()` | C++ 前端 lowering |
| `InvokeInstruction` | (异常处理指令) | C++ lowering, lto, gvn |
| `LandingPadInstruction` | (异常处理指令) | C++ lowering |
| `ResumeInstruction` | (异常处理指令) | 声明存在，⚠️ 仅 C++ lowering 间接 |
| `VectorInstruction` | (SIMD 向量指令) | advanced_optimizations, x86 isel |
| `IRVisitor` | `Visit()` | ❌ 仅声明 (`common/include/ir/ir_visitor.h`) |

### 4.5 `polyglot::passes::transform`

| 类/函数 | 被调用位置 |
|---------|-----------|
| `ConstantFolding()` | ⚠️ 仅测试 |
| `DeadCodeElimination()` | ⚠️ 仅测试 |
| `CommonSubexprElimination()` | ⚠️ 仅测试 |
| `Inlining()` | ⚠️ 仅测试 |
| `GVNPass` | ⚠️ 仅测试 |
| `PREPass` | ⚠️ 仅测试 |
| `AliasAnalysisPass` | ⚠️ 仅测试 |
| `LoopAnalysis` | ⚠️ 仅测试 + 同文件内部 |
| `LoopUnrollingPass` | ⚠️ 仅测试 |
| `LICMPass` | ⚠️ 仅测试 |
| `LoopFusionPass` | ⚠️ 仅测试 |
| `LoopStrengthReductionPass` | ⚠️ 仅测试 |
| `DevirtualizationPass` | ⚠️ 仅测试 |
| `TailCallOptimization()` | ⚠️ 仅测试 |
| `LoopTiling()` | ⚠️ 仅测试 |
| 其他 advanced_optimizations 函数 | ⚠️ 仅测试 |

### 4.6 `polyglot::pgo`

| 类 | 被调用位置 |
|----|-----------|
| `ProfileData` | ⚠️ 仅测试 |
| `RuntimeProfiler` | ⚠️ 仅测试 |
| `FunctionProfile` | ⚠️ 仅测试 |

### 4.7 `polyglot::lto`

| 类 | 被调用位置 |
|----|-----------|
| `LTOModule` / `ModuleIndex` | ⚠️ 仅测试 + LTO 内部 |
| `CrossModuleInliner` | ⚠️ 仅测试 + LTO 内部 |
| `DeadSymbolEliminator` | ⚠️ 仅测试 + LTO 内部 |
| `InterproceduralConstantPropagation` | ⚠️ 仅测试 + LTO 内部 |
| `CrossModuleDevirtualizer` | ⚠️ 仅测试 + LTO 内部 |
| `CrossModuleGVN` | ⚠️ 仅测试 + LTO 内部 |
| `LTOOptimizer` | ⚠️ 仅测试 |
| `LTOLinker` | ⚠️ 仅测试 |

### 4.8 `polyglot::backends`

| 类 | 被调用位置 |
|----|-----------|
| `TargetMachine` (abstract) | x86_64/arm64/wasm target 实现 |
| `ObjectFileBuilder` / `ELFBuilder` / `MachOBuilder` | ❌ 仅自身定义文件 |
| `DebugInfoBuilder` (backends版) | debug_emitter.cpp，测试 |
| `DebugEmitter` | ⚠️ 仅测试 |
| `DWARF5Generator` (backends版) | debug_emitter.cpp 内部 |
| `X86Target` / `ARM64Target` / `WASMTarget` | polyc driver, polyasm |
| `InstructionScheduler` | ⚠️ 仅测试 |
| `SoftwarePipeliner` | ⚠️ 仅测试 |
| `MicroArchOptimizer` | ⚠️ 仅测试 |
| `RegisterRenamer` | ⚠️ 仅测试 |
| `CacheOptimizer` | ⚠️ 仅测试 |
| `BranchOptimizer` | ⚠️ 仅测试 |
| `calling_convention (x86_64)` | ❌ 匿名 namespace，无外部调用 |
| `calling_convention (arm64)` | ❌ 匿名 namespace，无外部调用 |

### 4.9 `polyglot::runtime`

| 类 | 被调用位置 |
|----|-----------|
| `Heap` / `RootHandle` | ⚠️ 仅测试 |
| GC 策略类 (MarkSweep/Generational/Copying/Incremental) | ⚠️ 仅测试 (通过 CreateGC) |
| C linkage API (`poly_alloc` 等) | ⚠️ 仅测试 + 桩实现 |
| `FFIRegistry` | ⚠️ 仅测试 |
| `DynamicLibrary` | ⚠️ 仅测试 |
| `HandleTable` | ⚠️ 仅测试 |
| `ObjectLifecycle` / `OwnershipTracker` | ⚠️ 仅测试 |
| `ContainerMarshal` | ⚠️ 仅测试 |
| `TaskScheduler` | ⚠️ 仅测试 |
| `WorkStealingScheduler` | ⚠️ 仅测试 |
| `CoroutineScheduler` | ⚠️ 仅测试 |
| `Future` / `Promise` | ⚠️ 仅测试 |
| `Atomic` | ⚠️ 仅测试 |
| `ThreadProfiler` | ❌ 无任何引用（声明但未实现） |
| `ReflectionRegistry` | ⚠️ 仅测试 |
| `TokenPool` | ❌ 仅声明，cpp 为空桩 |

### 4.10 `polyglot::linker`

| 类 | 被调用位置 |
|----|-----------|
| `Linker` | polyld driver, 测试 |
| `LinkerScriptParser` | ⚠️ 仅测试（linker 内部未调用） |

---

## 5. 未使用代码分析

### 5.1 🔴 完全未使用（从未被任何生产代码或测试引用）

| # | 项目 | 位置 | 说明 |
|---|------|------|------|
| 1 | `CompilerConfig` | `common/include/core/config.h` | 类声明但无任何文件 include 此头文件 |
| 2 | `StringPool` | `common/include/utils/string_pool.h` | 声明但零引用 |
| 3 | `Logger` | `common/include/utils/logging.h` | 声明但零引用 |
| 4 | `Arena` | `common/include/utils/arena.h` | 声明但零引用 |
| 5 | `DebugInfoValidator` | `common/include/debug/debug_info_builder.h` | 声明但零引用 |
| 6 | `DebugInfoPrinter` | `common/include/debug/debug_info_builder.h` | 声明但零引用 |
| 7 | `IRVisitor` | `common/include/ir/ir_visitor.h` | 抽象基类，无子类实现 |
| 8 | `TokenPool` | `frontends/common/include/token_pool.h` | 声明但 cpp 为空桩，零引用 |
| 9 | `ThreadProfiler` | `runtime/include/services/threading.h` | 仅静态方法声明，无实现 |
| 10 | `ObjectFileBuilder`/`ELFBuilder`/`MachOBuilder` | `backends/common/include/object_file.h` | 声明+定义但无外部调用 |
| 11 | `calling_convention (x86_64)` | `backends/x86_64/src/calling_convention.cpp` | 匿名 namespace 内全部代码无外部调用 |
| 12 | `calling_convention (arm64)` | `backends/arm64/src/calling_convention.cpp` | 匿名 namespace 内全部代码无外部调用 |

### 5.2 🟡 仅测试使用（有完整实现和测试，但未集成到编译流水线）

| # | 项目 | 说明 |
|---|------|------|
| 1 | `TypeUnifier` | 类型统一器，仅在 type_system_test.cpp 中测试 |
| 2 | `TypeRegistry` | 类型注册表，仅在 type_system_test.cpp 中测试 |
| 3 | `TypeSystem::AreLayoutCompatible` | 仅测试调用 |
| 4 | `TypeSystem::IsWidening/IsNarrowing` | 仅测试调用 |
| 5 | `TypeSystem::ConversionRank` | 仅测试调用 |
| 6 | `TypeSystem::RegisterAlias/ResolveAlias/HasAlias` | 仅测试调用 |
| 7 | `SymbolKindToString/ScopeKindToString/FormatSymbol/FormatScope` | 辅助函数，仅测试调用 |
| 8 | **全部 passes::transform 优化** | 17+ 优化 pass/函数均仅测试使用 |
| 9 | **全部 PGO 类** | ProfileData, RuntimeProfiler 仅测试使用 |
| 10 | **全部 LTO 类** | LTOModule/Optimizer/Linker 等仅测试使用 |
| 11 | **全部后端优化类** | InstructionScheduler, SoftwarePipeliner, MicroArchOptimizer 等仅测试使用 |
| 12 | **全部运行时服务** | GC Heap, FFI, Threading (TaskScheduler/WorkStealing/Coroutine 等), Reflection 仅测试使用 |
| 13 | `DebugEmitter` | EmitDWARF/EmitPDB/EmitSourceMap 仅测试使用 |
| 14 | `LinkerScriptParser` | 声明+定义，仅测试使用，Linker 主流程未调用 |

### 5.3 📊 统计摘要

| 类别 | 数量 | 占比 |
|------|------|------|
| 完全未使用 | **12 项** | ~10% |
| 仅测试使用 | **40+ 项** | ~33% |
| 生产代码使用 | **~70 项** | ~57% |

---

*本文档基于对 98 个头文件和 80 个源文件的静态分析生成。标记为"仅测试使用"的组件本身是完整可用的，拥有完整实现和测试覆盖——它们只是尚未集成到编译器的主编译流水线中。*
