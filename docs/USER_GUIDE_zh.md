# PolyglotCompiler 用户指南

> 一个功能完整的多语言编译器项目  
> 支持 C++、Python、Rust、Java、C# (.NET) → x86_64/ARM64/WebAssembly  
> 含 .ploy 跨语言链接前端

**版本**: v1.0.0  
**最后更新**: 2026-03-15

---

## 目录

1. [项目概述](#1-项目概述)
2. [快速开始](#2-快速开始)
3. [架构设计](#3-架构设计)
4. [.ploy 跨语言链接前端](#4-ploy-跨语言链接前端)
5. [已实现功能](#5-已实现功能)
6. [使用指南](#6-使用指南)
7. [IR 设计规范](#7-ir-设计规范)
8. [优化系统](#8-优化系统)
9. [运行时系统](#9-运行时系统)
10. [测试体系](#10-测试体系)
11. [构建与集成](#11-构建与集成)
12. [开发指南](#12-开发指南)
13. [插件系统](#13-插件系统)
14. [附录](#14-附录)

---

# 1. 项目概述

## 1.1 项目简介

PolyglotCompiler 是一个现代化的多语言编译器项目，采用多前端共享中间表示（IR）的架构设计，并通过 `.ploy` 语言实现跨语言函数级别链接和面向对象互操作。

**核心目标：**

- ✅ **多语言支持**: C++、Python、Rust、Java、.NET (C#) 的完整编译前端
- ✅ **三重后端架构**: x86_64 (SSE/AVX)、ARM64 (NEON)、WebAssembly 后端
- ✅ **完整工具链**: 编译器（polyc）、链接器（polyld）、优化器（polyopt）、汇编器（polyasm）、运行时工具（polyrt）、基准测试（polybench）、IDE（polyui）
- ✅ **跨语言链接**: 通过 `.ploy` 声明式语法实现函数级跨语言互操作
- ✅ **跨语言 OOP**: 通过 `NEW` / `METHOD` / `GET` / `SET` / `WITH` / `DELETE` / `EXTEND` 关键字支持类实例化、方法调用、属性访问、资源管理、对象销毁和类继承扩展
- ✅ **包管理集成**: 支持 pip/conda/uv/pipenv/poetry/cargo/pkg-config/NuGet/Maven/Gradle
- ✅ **运行时内存策略**: `DICT` 运行时已实现基于负载因子的扩容与 rehash，类扩展注册改为线程安全动态注册表
- ✅ **功能完整实现**: 全面实现，所有前端均可运行；Java 和 .NET 测试覆盖仍在持续扩展

## 1.2 核心特性一览

### 语言支持

| 语言 | 前端 | IR Lowering | 高级特性 | 状态 |
|------|------|-------------|----------|------|
| **C++** | ✅ | ✅ | OOP/模板/RTTI/异常/constexpr/SIMD | **完整** |
| **Python** | ✅ | ✅ | 类型注解/推导/装饰器/生成器/async/25+高级特性 | **完整** |
| **Rust** | ✅ | ✅ | 借用检查/闭包/生命周期/Traits/28+高级特性 | **完整** |
| **Java** | ✅ | ✅ | Java 8/17/21/23/记录类/密封类/模式匹配/文本块/Switch表达式 | **核心完整** |
| **.NET (C#)** | ✅ | ✅ | .NET 6/7/8/9/记录类/顶级语句/主构造器/文件范围命名空间/可空引用类型 | **核心完整** |
| **.ploy** | ✅ | ✅ | 跨语言链接/管道/包管理/多包管理器/类实例化/方法调用/属性访问/资源管理/对象销毁/类继承 | **完整** |

### 平台支持

| 架构 | 指令选择 | 寄存器分配 | 调用约定 | 状态 |
|------|----------|------------|----------|------|
| **x86_64** | ✅ | ✅ 图着色/线性扫描 | ✅ SysV ABI | **完整** |
| **ARM64** | ✅ | ✅ 图着色/线性扫描 | ✅ AAPCS64 | **完整** |
| **WebAssembly** | ✅ | ✅ 堆栈机 | ✅ WASM ABI | **完整** |

### 工具链

| 工具 | 功能 | 可执行文件 |
|------|------|-----------|
| 编译器驱动 | 源码 → IR → 目标代码 | `polyc` |
| 链接器 | 目标文件链接 + 跨语言粘合 | `polyld` |
| 汇编器 | 汇编 → 目标文件 | `polyasm` |
| 优化器 | IR 优化 Pass | `polyopt` |
| 运行时工具 | GC/FFI/线程管理 | `polyrt` |
| 拓扑分析器 | 函数 I/O 拓扑可视化、链接验证、图导出（文本/DOT/JSON） | `polytopo` |
| 基准测试 | 性能评估套件 | `polybench` |
| IDE | 基于 Qt 的桌面集成开发环境，支持语法高亮、诊断、拓扑面板、文件浏览 | `polyui` |

---

# 2. 快速开始

## 2.1 环境要求

- **CMake** 3.20+
- **C++20** 兼容编译器（MSVC 2022+/GCC 12+/Clang 15+）
- **Ninja** 或 **Make** 构建工具（推荐 Ninja）

**CMake 自动拉取的依赖：**
- **fmt**: 格式化库
- **nlohmann_json**: JSON 库
- **Catch2**: 测试框架
- **mimalloc**: 高性能内存分配器

## 2.2 编译项目

```bash
# 克隆项目
git clone https://github.com/Boilboil1069/PolyglotCompiler
cd PolyglotCompiler

# 构建（Linux/macOS）
mkdir -p build && cd build
cmake .. -G Ninja
ninja

# 构建（Windows + MSVC）
# 注意：必须使用 -arch=amd64 避免 x86/x64 链接器不匹配
call "...\VsDevCmd.bat" -arch=amd64
mkdir build && cd build
cmake .. -G Ninja
ninja

# 运行全部测试
./unit_tests         # Linux/macOS
unit_tests.exe       # Windows
```

## 2.3 第一个 C++ 程序

```cpp
// hello.cpp
int main() {
    return 42;
}
```

```bash
polyc --lang=cpp --emit-ir=hello.ir hello.cpp    # 生成 IR
polyc --lang=cpp -o hello.o hello.cpp             # 生成目标文件
```

## 2.4 第一个跨语言管道

```ploy
// pipeline.ploy — 使用 Python ML + C++ 后处理
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

# 3. 架构设计

## 3.1 整体架构

```
┌─────────────────────────────────────────────────────────────┐
│                  Source Code                                │
│       C++ / Python / Rust / Java / C# / .ploy               │
└─────────────────────┬───────────────────────────────────────┘
                      │
  ┌───────┬───────────┼──────────┬───────────┬───────────┐
  │       │           │          │           │           │
  ▼       ▼           ▼          ▼           ▼           ▼
┌────┐ ┌──────┐ ┌────────┐ ┌────────┐ ┌────────┐ ┌──────────┐
│C++ │ │Python│ │  Rust  │ │  Java  │ │ .NET   │ │  .ploy   │
│ FE │ │  FE  │ │   FE   │ │   FE   │ │   FE   │ │   FE     │
└──┬─┘ └──┬───┘ └───┬────┘ └───┬────┘ └───┬────┘ └────┬─────┘
   │      │         │          │          │           │
   └──────┼─────────┼──────────┼──────────┘        ┌──┘
          │         │          │                   │
          ▼         ▼          ▼                   ▼
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
     ┌──────────────────────────────┐
     │         Backend              │
     │ (x86_64 / ARM64 / WASM)      │
     └────────────┬────────────│────┘
                  │
        ┌─────────┼─────────┐
        │         │         │
        ▼         ▼         ▼
    ┌───────┐ ┌────────┐ ┌──────┐
    │ISelect│ │RegAlloc│ │AsmGen│
    └───┬───┘ └──┬─────┘ └───┬──┘
        └────────┼───────────┘
                 │
         ┌───────────────┐
         │ Object File   │
         │ (ELF/Mach-O/  │
         │  POBJ/WASM)   │
         └───────────────┘
```

**关键设计原则：**
- 所有语言共享统一的 IR 中间表示
- 每个前端独立实现完整的 Lexer → Parser → Sema → Lowering 流水线
- 所有前端通过 `ILanguageFrontend` 接口注册到中央 **FrontendRegistry**（前端注册中心），实现 CLI、IDE、测试和插件的统一分发
- `.ploy` 前端独特之处在于它的 IR 被 PolyglotLinker 消费，生成跨语言粘合代码

## 3.2 目录结构

```
PolyglotCompiler/
├── frontends/              # 前端（6 个语言前端）
│   ├── common/             # 通用前端设施（Token、Diagnostics、预处理器、注册中心）
│   │   ├── include/        #   language_frontend.h（ILanguageFrontend 接口）
│   │   │                   #   frontend_registry.h（FrontendRegistry 单例）
│   │   │                   #   diagnostics.h, lexer_base.h, sema_context.h
│   │   └── src/            #   frontend_registry.cpp, token_pool.cpp, preprocessor.cpp
│   ├── cpp/                # C++ 前端
│   │   ├── include/        #   cpp_frontend.h, cpp_lexer.h, cpp_parser.h, cpp_sema.h, cpp_lowering.h
│   │   └── src/            #   cpp_frontend.cpp, lexer/, parser/, sema/, lowering/, constexpr/
│   ├── python/             # Python 前端
│   │   ├── include/        #   python_frontend.h, python_lexer.h, python_parser.h, python_sema.h, python_lowering.h
│   │   └── src/            #   python_frontend.cpp, lexer/, parser/, sema/, lowering/
│   ├── rust/               # Rust 前端
│   │   ├── include/        #   rust_frontend.h, rust_lexer.h, rust_parser.h, rust_sema.h, rust_lowering.h
│   │   └── src/            #   rust_frontend.cpp, lexer/, parser/, sema/, lowering/
│   ├── java/               # Java 前端 (Java 8/17/21/23)
│   │   ├── include/        #   java_frontend.h, java_ast.h, java_lexer.h, java_parser.h, java_sema.h, java_lowering.h
│   │   └── src/            #   java_frontend.cpp, lexer/, parser/, sema/, lowering/
│   ├── dotnet/             # .NET 前端 (C# .NET 6/7/8/9)
│   │   ├── include/        #   dotnet_frontend.h, dotnet_ast.h, dotnet_lexer.h, dotnet_parser.h, dotnet_sema.h, dotnet_lowering.h
│   │   └── src/            #   dotnet_frontend.cpp, lexer/, parser/, sema/, lowering/
│   └── ploy/               # .ploy 跨语言链接前端
│       ├── include/        #   ploy_frontend.h, ploy_ast.h, ploy_lexer.h, ploy_parser.h, ploy_sema.h, ploy_lowering.h
│       └── src/            #   ploy_frontend.cpp, lexer/, parser/, sema/, lowering/
├── middle/                 # 中间层
│   ├── include/
│   │   ├── ir/             #   IR 定义（cfg.h, ssa.h, verifier.h, analysis.h, data_layout.h 等）
│   │   ├── passes/         #   优化 Pass 接口（analysis/, transform/）
│   │   ├── pgo/            #   Profile-Guided Optimization
│   │   └── lto/            #   Link-Time Optimization
│   └── src/
│       ├── ir/             #   builder, ir_context, cfg, ssa, verifier, printer, parser, template_instantiator, opt
│       ├── passes/         #   pass_manager, optimizations/（constant_fold, dead_code_elim, common_subexpr,
│       │                   #     inlining, devirtualization, loop_optimization, gvn, advanced_optimizations）
│       ├── pgo/            #   profile_data.cpp
│       └── lto/            #   link_time_optimizer.cpp
├── backends/               # 后端（3 个架构后端）
│   ├── common/             # 通用后端设施
│   │   └── src/            #   debug_info.cpp, debug_emitter.cpp, dwarf_builder.cpp, object_file.cpp
│   ├── x86_64/             # x86_64 后端
│   │   ├── include/        #   x86_target.h, x86_register.h, machine_ir.h, instruction_scheduler.h
│   │   └── src/            #   isel/, regalloc/（graph_coloring, linear_scan）, asm_printer/, optimizations, calling_convention
│   ├── arm64/              # ARM64 后端
│   │   ├── include/        #   arm64_target.h, arm64_register.h, machine_ir.h
│   │   └── src/            #   isel/, regalloc/（graph_coloring, linear_scan）, asm_printer/, calling_convention
│   └── wasm/               # WebAssembly 后端
│       ├── include/        #   wasm_target.h
│       └── src/            #   wasm_target.cpp
├── runtime/                # 运行时
│   ├── include/            # GC、FFI、服务接口
│   └── src/
│       ├── gc/             #   mark_sweep, generational, copying, incremental, gc_strategy, runtime
│       ├── interop/        #   ffi, memory, marshalling, type_mapping, calling_convention, container_marshal
│       ├── libs/           #   base.c, base_gc_bridge.cpp, python_rt.c, cpp_rt.c, rust_rt.c, java_rt.c, dotnet_rt.c
│       └── services/       #   exception, reflection, threading
├── common/                 # 项目公共设施
│   ├── include/
│   │   ├── core/           #   type_system.h, source_loc.h, symbol_table.h
│   │   ├── ir/             #   ir_node.h
│   │   ├── debug/          #   dwarf5.h
│   │   └── utils/          #   工具类
│   └── src/
│       ├── core/           #   type_system.cpp, symbol_table.cpp
│       └── debug/          #   dwarf5.cpp
├── tools/                  # 工具链（7 个可执行文件）
│   ├── polyc/              # 编译器驱动 (driver.cpp ~1773 行)
│   ├── polyld/             # 链接器 (linker.cpp + polyglot_linker.cpp ~3790 行)
│   ├── polyasm/            # 汇编器 (assembler.cpp)
│   ├── polyopt/            # 优化器 (optimizer.cpp)
│   ├── polyrt/             # 运行时工具 (polyrt.cpp)
│   ├── polybench/          # 基准测试 (benchmark_suite.cpp)
│   └── ui/                 # IDE 工具 (polyui) — 按平台分离的源码布局
│       ├── common/         #   跨平台共享代码（mainwindow、code_editor、syntax_highlighter、file_browser、output_panel、compiler_service、terminal_widget）
│       ├── windows/        #   Windows 专用入口点 (main.cpp)
│       ├── linux/          #   Linux 专用入口点 (main.cpp)
│       └── macos/          #   macOS 专用入口点 (main.cpp)
├── tests/                  # 测试
│   ├── unit/               # 单元测试（Catch2 框架）— 743 个测试用例
│   │   └── frontends/ploy/ #   ploy_test.cpp（216 测试用例）
│   ├── samples/            # 示例程序（16 个分类目录，含 .ploy/.cpp/.py/.rs/.java/.cs）
│   ├── integration/        # 集成测试（编译管道/互操作/性能）— 52 个测试用例
│   └── benchmarks/         # 基准测试（微基准/宏基准）— 18 个测试用例
└── docs/                   # 文档
    ├── api/                # API 参考（中英双语）
    ├── specs/              # 语言与 IR 规范
    ├── realization/        # 实现文档（8 主题 × 2 语言 = 16 文件）
    ├── tutorial/           # 教程（ploy 语言 + 项目教程，中英双语）
    ├── demand/             # 需求文档
    ├── USER_GUIDE.md       # 完整指南（英文版）
    └── USER_GUIDE_zh.md    # 本文档（中文版）
```

## 3.3 前端设计

每个前端遵循统一的 4 阶段流水线：

```
Source Code
    │
    ▼
┌─────────┐    Token Stream    ┌─────────┐   AST    ┌─────────┐   Annotated AST    ┌──────────┐ IR Module
│  Lexer  │──────────────────▶ │ Parser  │────────▶ │  Sema   │───────────────────▶│ Lowering │──────────▶
└─────────┘                    └─────────┘          └─────────┘                    └──────────┘
```

### 3.3.1 统一前端注册中心

所有语言前端通过 **FrontendRegistry**（前端注册中心）进行统一管理——这是一个单例注册中心，取代了原有的硬编码 if/else 分发链。CLI（`polyc`）、IDE（`polyui`）、测试和插件都共享同一分发机制。

**架构图：**

```
                    ┌──────────────────────────┐
                    │    FrontendRegistry       │
                    │    （单例注册中心）         │
                    ├──────────────────────────┤
                    │  Register(frontend)       │
                    │  GetFrontend(名称/别名)    │
                    │  GetFrontendByExtension() │
                    │  DetectLanguage(路径)      │
                    │  SupportedLanguages()     │
                    │  AllFrontends()           │
                    └───────────┬──────────────┘
                                │
         ┌──────────┬───────────┼───────────┬──────────┬──────────┐
         │          │           │           │          │          │
         ▼          ▼           ▼           ▼          ▼          ▼
     ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐
     │  Ploy  │ │  C++   │ │ Python │ │  Rust  │ │  Java  │ │ .NET   │
     │ 前端   │ │  前端  │ │  前端   │ │  前端  │ │  前端  │ │  前端  │
     └────────┘ └────────┘ └────────┘ └────────┘ └────────┘ └────────┘
         │          │           │           │          │          │
         └──────────┴───────────┴───────────┴──────────┴──────────┘
                                │
                    ┌───────────────────────┐
                    │  ILanguageFrontend     │
                    │  （抽象接口）           │
                    ├───────────────────────┤
                    │  Name() -> 规范名称     │
                    │  DisplayName() -> 显示名│
                    │  Extensions() -> 扩展名 │
                    │  Aliases() -> 别名列表  │
                    │  Tokenize(源码)        │
                    │  Analyze(源码, 诊断)    │
                    │  Lower(源码, IR上下文)  │
                    │  NeedsPreprocessing()  │
                    └───────────────────────┘
```

**核心特性：**

- **自动注册**：每个前端适配器通过 `REGISTER_FRONTEND()` 宏在静态初始化阶段自动注册，无需手动接线。
- **多键查找**：支持通过规范名称（如 `"cpp"`）、别名（如 `"c"`、`"c++"`、`"csharp"`）或文件扩展名（如 `".py"`、`".rs"`）查找前端。
- **语言检测**：`DetectLanguage(file_path)` 可根据文件扩展名自动推断语言类型。
- **线程安全**：所有注册中心操作均受互斥锁保护。
- **可扩展**：第三方插件可在运行时通过相同接口注册新的语言前端。

**支持的标识符：**

| 前端 | 规范名称 | 别名 | 文件扩展名 |
|------|---------|------|-----------|
| C++ | `cpp` | `c`, `c++` | `.cpp`, `.cc`, `.cxx`, `.c`, `.hpp`, `.h` |
| Python | `python` | `py` | `.py` |
| Rust | `rust` | `rs` | `.rs` |
| Java | `java` | — | `.java` |
| .NET/C# | `dotnet` | `csharp`, `c#` | `.cs`, `.vb` |
| .ploy | `ploy` | — | `.ploy`, `.poly` |

**源文件说明：**

| 文件 | 说明 |
|------|------|
| `frontends/common/include/language_frontend.h` | `ILanguageFrontend` 抽象接口及 `FrontendOptions`/`FrontendResult` 类型 |
| `frontends/common/include/frontend_registry.h` | `FrontendRegistry` 单例及 `REGISTER_FRONTEND()` 宏 |
| `frontends/common/src/frontend_registry.cpp` | 注册中心实现（查找、枚举、清空） |
| `frontends/<lang>/include/<lang>_frontend.h` | 各语言适配器头文件 |
| `frontends/<lang>/src/<lang>_frontend.cpp` | 各语言适配器实现（含自动注册） |

### 3.3.2 前端流水线阶段

**各前端规模概览：**

| 前端 | 源码路径 | 核心特性 |
|------|----------|---------|
| C++ | `frontends/cpp/` (5 编译单元) | OOP、模板、RTTI、异常、constexpr、SIMD |
| Python | `frontends/python/` (4 编译单元) | 类型注解、推导、25+高级特性 |
| Rust | `frontends/rust/` (4 编译单元) | 借用检查、生命周期、闭包、28+高级特性 |
| Java | `frontends/java/` (4 编译单元) | Java 8/17/21/23、记录类、密封类、模式匹配、Switch表达式、文本块 |
| .NET (C#) | `frontends/dotnet/` (4 编译单元) | .NET 6/7/8/9、记录类、顶级语句、主构造器、可空引用类型 |
| .ploy | `frontends/ploy/` (6 编译单元) | LINK、IMPORT、PIPELINE、CONFIG、包管理、OOP 互操作 |

---

# 4. .ploy 跨语言链接前端

## 4.1 语言概述

`.ploy` 是一种声明式领域特定语言（DSL），用于描述不同编程语言之间的函数级别链接关系和面向对象互操作。它不是通用编程语言，而是"跨语言粘合剂"。

### 设计目标

1. **声明式链接** — 用简洁的语法描述跨语言函数调用关系
2. **面向对象互操作** — 通过 `NEW` / `METHOD` / `GET` / `SET` / `WITH` 支持跨语言类实例化、方法调用、属性访问和资源管理
3. **类型映射** — 自动处理不同语言之间的类型转换
4. **包管理集成** — 直接引用各语言生态的包（numpy, rayon, opencv 等）
5. **管道编排** — 将多阶段跨语言工作流组织为可复用管道

### 前端组成

| 组件 | 头文件 | 实现 | 行数 | 职责 |
|------|--------|------|------|------|
| 词法分析器 | `ploy_lexer.h` | `lexer.cpp` | ~295 | 54 个关键字、运算符、字面量 |
| 语法分析器 | `ploy_parser.h` | `parser.cpp` | ~2020 | 声明/语句/表达式解析 |
| 语义分析器 | `ploy_sema.h` | `sema.cpp` | ~1950 | 类型检查、包发现、版本验证、错误检查 |
| IR 生成器 | `ploy_lowering.h` | `lowering.cpp` | ~1530 | AST → IR 转换 |

### 关键字列表（54 个）

```
// 声明关键字
LINK      IMPORT    EXPORT    MAP_TYPE   PIPELINE   FUNC      CONFIG

// 变量与类型
LET       VAR       STRUCT    VOID       INT        FLOAT     STRING
BOOL      ARRAY     LIST      TUPLE      DICT       OPTION

// 控制流
RETURN    IF        ELSE      WHILE      FOR        IN        MATCH
CASE      DEFAULT   BREAK     CONTINUE

// 运算符
AS        AND       OR        NOT        CALL       CONVERT   MAP_FUNC

// 面向对象操作
NEW       METHOD    GET       SET        WITH       DELETE    EXTEND

// 值
TRUE      FALSE     NULL      PACKAGE

// 包管理器
VENV      CONDA     UV        PIPENV     POETRY
```

## 4.2 核心语法

### 4.2.1 LINK — 跨语言函数链接

```ploy
// 基本语法
LINK(target_language, source_language, target_function, source_function);

// 带类型映射的链接
LINK(cpp, python, process_data, np::compute) {
    MAP_TYPE(cpp::std::vector_double, python::list);
    MAP_TYPE(cpp::int, python::int);
}

// 带结构体映射
LINK(cpp, rust, transform, serde::parse) {
    MAP_TYPE(cpp::MyStruct, rust::MyStruct);
}
```

**语义：** LINK 声明告诉 PolyglotLinker 生成粘合代码，将 `source_function` 的输出转换为 `target_function` 可接受的输入。

### 4.2.2 IMPORT — 模块与包导入

```ploy
// ---- 限定模块导入 ----
IMPORT cpp::math_utils;
IMPORT rust::serde;

// ---- 包导入（使用 PACKAGE 关键字）----
IMPORT python PACKAGE numpy;

// 带版本约束
IMPORT python PACKAGE numpy >= 1.20;
IMPORT python PACKAGE scipy == 1.10.0;
IMPORT rust PACKAGE serde >= 1.0;

// 带别名
IMPORT python PACKAGE numpy >= 1.20 AS np;

// 选择性导入（仅导入特定符号）
IMPORT python PACKAGE numpy::(array, mean, std);

// 从子模块选择性导入
IMPORT python PACKAGE numpy.linalg::(solve, inv);

// 选择性导入 + 版本约束
IMPORT python PACKAGE numpy::(array, mean) >= 1.20;

// 选择性导入 + 版本约束
IMPORT python PACKAGE torch::(tensor, no_grad) >= 2.0;

// 整包导入 + 别名（不带选择性导入）
IMPORT python PACKAGE torch >= 2.0 AS pt;
```

> **限制：** 选择性导入 `::()` 与 `AS` 别名**不能同时使用**。
> 例如 `torch::(tensor, no_grad) AS pt` 是非法的 —— `pt` 无法确定指代 `tensor` 还是 `no_grad`。

**语法解析顺序：**

```
IMPORT <语言> PACKAGE <包名>[::(<符号列表>)] [<版本运算符> <版本号>] [AS <别名>];
```

> 注意：选择性导入 `::()` 紧跟包名，版本约束和别名在其后。选择性导入与别名互斥。

**版本运算符：**

| 运算符 | 含义 | 示例 |
|--------|------|------|
| `>=` | 大于等于 | `>= 1.20` |
| `<=` | 小于等于 | `<= 2.0` |
| `==` | 精确等于 | `== 1.10.0` |
| `>` | 严格大于 | `> 1.0` |
| `<` | 严格小于 | `< 3.0` |
| `~=` | 兼容版本 (PEP 440) | `~= 1.20` → `>= 1.20, < 2.0` |

### 4.2.3 CONFIG — 包管理器配置

为包发现配置特定的包管理器环境。

```ploy
// ---- pip / venv / virtualenv ----
CONFIG VENV python "/opt/ml-env";
CONFIG VENV "/home/user/.virtualenvs/ml";     // 默认语言 python

// ---- Conda 环境 ----
CONFIG CONDA python "ml_env";                 // Conda 环境名
CONFIG CONDA "data_science";                  // 默认语言 python

// ---- uv 管理的虚拟环境 ----
CONFIG UV python "D:/venvs/uv_env";

// ---- Pipenv 项目 ----
CONFIG PIPENV python "C:/projects/myapp";

// ---- Poetry 项目 ----
CONFIG POETRY python "C:/projects/poetry_app";
```

**规则：**
- 每种语言每个编译单元只允许一个 CONFIG
- 同一语言重复配置会产生编译时错误
- CONFIG 必须出现在使用它的 IMPORT 之前
- 语言参数可省略，默认为 `python`

### 4.2.4 PIPELINE — 多阶段管道

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

### 4.2.5 EXPORT — 符号导出

```ploy
EXPORT image_pipeline AS "classify_images";   // 带别名导出
EXPORT compute;                                // 使用原始名称
```

### 4.2.6 类型系统

| 类别 | 类型 | 语法 |
|------|------|------|
| 原始类型 | 整数、浮点、布尔、字符串、空 | `INT`, `FLOAT`, `BOOL`, `STRING`, `VOID` |
| 容器类型 | 列表、元组、字典、可选 | `LIST(T)`, `TUPLE(T1, T2)`, `DICT(K, V)`, `OPTION(T)` |
| 结构体 | 用户定义复合类型 | `STRUCT Point { x: f64, y: f64 }` |
| 数组 | 定长数组 | `ARRAY(INT)` |
| 函数类型 | 函数签名 | `(INT, FLOAT) -> BOOL` |
| 限定类型 | 语言特定类型 | `cpp::int`, `python::str`, `rust::Vec` |

**跨语言类型检查：**

当 `LINK` 声明包含 `MAP_TYPE` 条目时，编译器会在调用点启用完整的参数数量和类型验证：

- **参数数量：** `CALL`、`METHOD` 和 `NEW` 表达式会根据注册的签名进行验证。参数数量不匹配会产生编译时错误，并回溯到声明位置。
- **参数类型：** 每个参数类型都会与预期的参数类型进行检查。不兼容的类型会产生错误。
- **返回类型：** 当跨语言调用结果赋值给带类型的变量时（`LET x: INT = CALL(...)`），返回类型会与声明的类型进行验证。
- **严格模式：** 在严格模式（`--strict`）下，缺失可调用签名/ABI 描述以及无法消除的 `Any` 回退会在语义分析阶段直接作为错误处理。
- **链接阶段 ABI 闸门：** `polyld` 会将参数/返回 ABI 不匹配（数量、尺寸、传参方式、类型名、MAP_TYPE 数量漂移）视为硬失败，不再以 warning 放行。

**类型化对象句柄：**

当类已知时（例如通过 `LINK` 或 `EXTEND`），`NEW` 表达式返回类型化的指针句柄，而不是不透明的 `void*`。这使得后续对该对象的 `METHOD`、`GET` 和 `SET` 操作能够进行类型检查。

### 4.2.7 控制流

```ploy
// IF / ELSE
IF condition {
    ...
} ELSE IF other {
    ...
} ELSE {
    ...
}

// WHILE 循环
WHILE condition {
    ...
}

// FOR .. IN 范围循环
FOR item IN collection {
    ...
}

FOR i IN 0..10 {
    ...
}

// MATCH 模式匹配
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

### 4.2.8 变量声明与表达式

```ploy
// 变量声明
LET x = 42;                        // 不可变
VAR y: FLOAT = 3.14;               // 可变，带类型注解
LET z: LIST(INT) = [1, 2, 3];      // 容器字面量

// 算术表达式
LET sum = a + b * c;

// 逻辑表达式
IF x > 0 AND y < 10 { ... }

// 跨语言调用
LET result = CALL(python, np::mean, data);

// 类型转换
LET converted = CONVERT(value, FLOAT);

// 跨语言类实例化
LET model = NEW(python, torch::nn::Linear, 784, 10);

// 跨语言方法调用
LET output = METHOD(python, model, forward, input_data);

// 属性访问
LET weight = GET(python, model, weight);
SET(python, model, training, FALSE);

// 资源管理
WITH(python, open_file) AS handle {
    LET data = METHOD(python, handle, read);
}

// 带限定类型的类型注解
LET model: python::nn::Module = NEW(python, torch::nn::Linear, 784, 10);

// 容器字面量
LET list = [1, 2, 3];
LET tuple = (1, "hello", 3.14);
LET dict = {"key1": 10, "key2": 20};
LET point = Point { x: 1.0, y: 2.0 };     // 结构体字面量
```

### 4.2.9 MAP_FUNC — 映射函数

```ploy
MAP_FUNC convert(x: INT) -> FLOAT {
    LET result = CONVERT(x, FLOAT);
    RETURN result;
}
```

### 4.2.10 NEW — 跨语言类实例化

`NEW` 关键字用于在 `.ploy` 中创建其他语言的类实例（对象）。返回一个不透明句柄（Any 类型），可传递给 `METHOD`、`CALL` 或 `LINK`。

```ploy
// 基本语法
NEW(language, class_name, arg1, arg2, ...)

// 示例：创建 Python 类实例
IMPORT python PACKAGE torch >= 2.0;
LET model = NEW(python, torch::nn::Linear, 784, 10);

// 无参数构造
LET empty_list = NEW(python, list);

// 字符串参数
LET conn = NEW(python, sqlite3::Connection, "data.db");

// Rust 结构体构造
IMPORT rust PACKAGE serde;
LET parser = NEW(rust, serde::json::Parser);
```

**语义规则：**
- 第一个参数必须是已知语言标识符（`python`、`cpp`、`rust`）
- 第二个参数是类名，支持 `::` 限定路径
- 后续参数作为构造函数参数传入
- 返回 `Any` 类型（不透明对象句柄）

**IR 生成：** 构造函数桥接桩命名为 `__ploy_bridge_ploy_<lang>_<class>____init__`，返回类型为 `Pointer(Void)`。

### 4.2.11 METHOD — 跨语言方法调用

`METHOD` 关键字用于在 `.ploy` 中调用其他语言对象的方法。接收者对象作为第一个参数传入生成的桩函数。

```ploy
// 基本语法
METHOD(language, object_expression, method_name, arg1, arg2, ...)

// 示例：调用 Python 对象方法
LET model = NEW(python, torch::nn::Linear, 784, 10);
LET output = METHOD(python, model, forward, input_data);

// 无额外参数
LET params = METHOD(python, model, parameters);

// 链式调用
LET optimizer = NEW(python, torch::optim::Adam, params, 0.001);
METHOD(python, optimizer, zero_grad);
LET loss = METHOD(python, model, compute_loss, predictions, labels);
METHOD(python, loss, backward);
METHOD(python, optimizer, step);
```

**语义规则：**
- 第一个参数必须是已知语言标识符
- 第二个参数是对象表达式（通常是 `NEW` 返回的变量）
- 第三个参数是方法名，支持 `::` 限定路径
- 后续参数作为方法参数传入
- 返回 `Any` 类型

**IR 生成：** 方法桥接桩命名为 `__ploy_bridge_ploy_<lang>_<method>`，接收者对象作为第一个参数插入。

**完整示例 — PyTorch 训练循环：**

```ploy
LINK(cpp, python, train_model, torch_train);
IMPORT python PACKAGE torch >= 2.0;
IMPORT python PACKAGE numpy >= 1.20 AS np;

PIPELINE ml_pipeline {
    FUNC train(data: LIST(f64), epochs: INT) -> LIST(f64) {
        LET model = NEW(python, torch::nn::Linear, 784, 10);
        LET optimizer = NEW(python, torch::optim::SGD,
                            METHOD(python, model, parameters), 0.01);
        LET loss = METHOD(python, model, forward, data);
        METHOD(python, loss, backward);
        METHOD(python, optimizer, step);
        RETURN CALL(python, np::array, loss);
    }
}

EXPORT ml_pipeline AS "train_ml";
```

### 4.2.12 GET — 跨语言属性访问

`GET` 关键字用于读取其他语言对象的属性值。生成 `__getattr__` 桥接桩函数。

```ploy
// 基本语法
GET(language, object_expression, attribute_name)

// 示例：读取 Python 对象属性
LET model = NEW(python, torch::nn::Linear, 784, 10);
LET weight = GET(python, model, weight);
LET bias = GET(python, model, bias);

// 读取 Rust 结构体字段
LET value = GET(rust, config, max_retries);

// 在表达式中使用
IF GET(python, model, training) {
    METHOD(python, model, eval);
}
```

**语义规则：**
- 第一个参数必须是已知语言标识符（`python`、`cpp`、`rust`）
- 第二个参数是对象表达式
- 第三个参数是属性名（必须是有效标识符）
- 返回 `Any` 类型

**IR 生成：** 属性访问桥接桩命名为 `__ploy_bridge_ploy_<lang>___getattr__<attr_name>`。

### 4.2.13 SET — 跨语言属性赋值

`SET` 关键字用于向其他语言对象的属性写入值。生成 `__setattr__` 桥接桩函数。

```ploy
// 基本语法
SET(language, object_expression, attribute_name, value)

// 示例：写入 Python 对象属性
LET model = NEW(python, torch::nn::Linear, 784, 10);
SET(python, model, training, FALSE);
SET(python, model, learning_rate, 0.001);

// 设置 Rust 结构体字段
SET(rust, config, max_retries, 5);

// 值可以是任意表达式
SET(python, model, weight, CALL(python, np::zeros, 784));
```

**语义规则：**
- 第一个参数必须是已知语言标识符
- 第二个参数是对象表达式
- 第三个参数是属性名（必须是有效标识符）
- 第四个参数是值表达式
- 返回 `Any` 类型（赋值后的值会被传递，支持链式操作）

**IR 生成：** 属性赋值桥接桩命名为 `__ploy_bridge_ploy_<lang>___setattr__<attr_name>`，对象和值作为参数传入。

### 4.2.14 WITH — 跨语言资源管理

`WITH` 关键字提供自动资源管理，类似于 Python 的 `with` 语句。生成 `__enter__` 和 `__exit__` 桥接桩函数，确保资源被正确清理。

```ploy
// 基本语法
WITH(language, resource_expression) AS variable_name {
    // 语句体 — variable_name 绑定为 __enter__ 的返回值
}

// 示例：文件处理
LET f = NEW(python, open, "data.csv");
WITH(python, f) AS handle {
    LET data = METHOD(python, handle, read);
    LET lines = METHOD(python, data, splitlines);
}

// 数据库连接
WITH(python, NEW(python, sqlite3::connect, "app.db")) AS db {
    LET cursor = METHOD(python, db, cursor);
    METHOD(python, cursor, execute, "SELECT * FROM users");
    LET rows = METHOD(python, cursor, fetchall);
}

// 多个 WITH 块
WITH(python, resource1) AS r1 {
    METHOD(python, r1, process);
}
WITH(python, resource2) AS r2 {
    METHOD(python, r2, process);
}
```

**语义规则：**
- 第一个参数必须是已知语言标识符
- 第二个参数是资源表达式（任何求值为具有 `__enter__`/`__exit__` 的对象的表达式）
- `AS` 关键字后跟一个变量名，在语句体作用域内绑定
- 语句体是由 `{ }` 包围的语句块
- 绑定的变量在语句体作用域内可用
- 返回 `Any` 类型
- 上下文管理器 ABI 契约被强制校验：`__enter__(self)` 与 `__exit__(self, exc_type, exc_val, exc_tb)`。

**IR 生成（异常安全）：**
1. 求值资源表达式
2. 调用 `__enter__` 桥接桩 → 将结果绑定到变量名
3. 在带 unwind 边的受保护区域执行语句体
4. 正常路径调用 `__exit__(resource, null, null, null)`
5. 异常路径调用 `__exit__(resource, exc_type, exc_val, exc_tb)`
6. 清理后继续传播异常
7. 记录两个跨语言描述符：一个用于 `__enter__`，一个用于 `__exit__`

这确保即使语句体遇到错误或提前返回，`__exit__` 也始终被调用，类似于 Python 的 `with` 语句或 C++ 的 RAII 模式。

### 4.2.15 DELETE — 跨语言对象销毁

`DELETE` 关键字提供显式的跨语言对象销毁：

```ploy
DELETE(python, obj);
DELETE(cpp, ptr);
DELETE(rust, handle);
```

**语法：**
```
DELETE ( language , expression )
```

**语义规则：**
- 第一个参数必须是已知语言标识符（`python`、`cpp`、`rust`）
- 第二个参数是求值为待销毁对象的表达式
- 对象必须是通过 `NEW` 创建或从跨语言调用获取的
- 返回 `Void` 类型（析构调用不产生值）

**IR 生成：**
- **Python**: 生成 `__ploy_py_del` 桥接桩调用（执行 `del` / 引用释放）
- **C++**: 生成 `__ploy_cpp_delete` 桥接桩调用（执行 `delete` / 析构函数）
- **Rust**: 生成 `__ploy_rust_drop` 桥接桩调用（执行 `drop()`）
- 为运行时链接器记录一个 `CrossLangCallDescriptor`

**示例 — 训练后清理：**
```ploy
LET model = NEW(python, torch::nn::Linear, 784, 10);
// ... 使用 model ...
DELETE(python, model);
```

### 4.2.16 EXTEND — 跨语言类继承扩展

`EXTEND` 关键字支持跨语言类继承扩展：

```ploy
EXTEND(python, torch::nn::Module) AS MyModel {
    FUNC forward(x: LIST(f64)) -> LIST(f64) {
        RETURN CALL(python, self::linear, x);
    }
}
```

**语法：**
```
EXTEND ( language , base_class ) AS DerivedName {
    FUNC method1 ( params ) -> ReturnType { body }
    FUNC method2 ( params ) -> ReturnType { body }
    ...
}
```

**语义规则：**
- 第一个参数必须是已知语言标识符
- 第二个参数是要继承的基类（支持命名空间限定，例如 `torch::nn::Module`）
- `AS` 关键字后跟派生类名
- 语句体包含一个或多个 `FUNC` 声明（方法重写/添加）
- 派生类名被注册到符号表中作为类型
- 每个方法的签名都会被验证（参数数量和类型）

**IR 生成：**
1. 对于每个方法，通过 `ir_ctx_.CreateFunction()` 创建桥接函数 `__ploy_extend_DerivedName_method`
2. 方法体被降低到桥接函数中
3. 生成 `__ploy_extend_register` 调用，传入类元数据
4. 为运行时链接器记录一个 `CrossLangCallDescriptor`

**示例 — 从 .ploy 扩展 Python 类：**
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

### 4.2.17 错误检查

`.ploy` 语义分析器提供全面的错误检查：

**参数数量不匹配：**
```
Error [E3010]: Parameter count mismatch in call to 'process'
  --> pipeline.ploy:15:9
   | Expected 3 argument(s), got 1
   = suggestion: Check the function signature for 'process'
```

**类型不匹配：**
```
Error [E3011]: Type mismatch for parameter 1 in call to 'compute'
  --> pipeline.ploy:22:5
   | Expected 'INT', got 'STRING'
   = suggestion: Consider using CONVERT to convert the argument type
```

**未定义符号：**
```
Error [E3001]: Undefined variable 'unknown_var'
  --> pipeline.ploy:8:13
```

**错误代码范围：**

| 范围 | 类别 | 示例 |
|------|------|------|
| 1xxx | 词法分析 | 非法字符、未终止的字符串 |
| 2xxx | 语法分析 | 意外的 token、缺少分隔符 |
| 3xxx | 语义分析 | 未定义变量、类型不匹配、参数数量不匹配 |
| 4xxx | IR 降低 | 不支持的目标、代码生成失败 |
| 5xxx | 链接器 | 未解析符号、重复定义 |

所有错误报告包含源位置、错误代码和可选建议。支持溯源链（traceback chains）用于追溯源自多个相关位置的错误。

## 4.3 包管理器自动发现

`.ploy` 前端在语义分析阶段自动发现已安装的包。

### 发现机制

| 包管理器 | 发现命令 | CONFIG 语法 |
|---------|---------|-------------|
| pip (venv/virtualenv) | `<venv>/python -m pip list --format=freeze` | `CONFIG VENV "path";` |
| Conda | `conda list -n <env_name> --export` | `CONFIG CONDA "env_name";` |
| uv | `<venv>/python -m pip list --format=freeze` 或 `uv pip list --format=freeze` | `CONFIG UV "path";` |
| Pipenv | `pipenv run pip list --format=freeze` | `CONFIG PIPENV "project_path";` |
| Poetry | `poetry run pip list --format=freeze` | `CONFIG POETRY "project_path";` |
| Cargo (Rust) | `cargo install --list` | 自动 |
| pkg-config (C/C++) | `pkg-config --list-all` | 自动 |

### 工作流程

1. **解析 CONFIG** — 确定包管理器类型和环境路径
2. **包索引阶段** — 语义分析之前，`PackageIndexer` 类以独立的预编译阶段运行包管理器命令（`pip list`、`cargo install --list`、`pkg-config`、`mvn`、`dotnet`），具备逐命令超时（默认10秒）和重试机制
3. **缓存结果** — 发现的包和版本缓存在会话级 `PackageDiscoveryCache` 中（键为 `language|manager|env_path`）；可跨多个 `PloySema` 编译复用共享缓存以避免重复外部命令调用
4. **语义分析读取缓存** — `PloySema` 读取预填充的缓存条目，但自身不再执行外部命令（`enable_package_discovery` 默认为 `false`）
5. **版本验证** — 对版本约束进行 6 种运算符验证
6. **符号验证** — 验证选择性导入的符号无重复

### 包索引阶段

`PackageIndexer` 类将缓慢的、有副作用的环境探测与快速的、确定性的语义分析分离：

```cpp
#include "frontends/ploy/include/package_indexer.h"

auto cache  = std::make_shared<PackageDiscoveryCache>();
auto runner = std::make_shared<DefaultCommandRunner>(std::chrono::seconds{10});

PackageIndexerOptions idx_opts;
idx_opts.command_timeout = std::chrono::seconds{10};
idx_opts.verbose = true;

PackageIndexer indexer(cache, runner, idx_opts);
indexer.BuildIndex({"python", "rust", "java"});

// 将预填充的缓存传入 sema
PloySemaOptions sema_opts;
sema_opts.enable_package_discovery = false;   // sema 不执行外部命令
sema_opts.discovery_cache = cache;            // 由索引器预填充
PloySema sema(diagnostics, sema_opts);
```

CLI（`polyc`）通过以下标志控制：

| 标志 | 说明 |
|------|------|
| `--package-index` | 在语义分析前执行显式包索引阶段（默认启用） |
| `--no-package-index` | 跳过包索引阶段以加速编译 |
| `--pkg-timeout=<ms>` | 包索引的逐命令超时（默认 10000 毫秒） |

### 带超时的命令执行器

`DefaultCommandRunner` 现已支持可配置的逐命令超时：

```cpp
auto runner = std::make_shared<DefaultCommandRunner>(std::chrono::seconds{10});
CommandResult result = runner->RunWithResult("pip list --format=freeze");

if (result.Ok()) {
    // result.stdout_output 包含命令输出
} else if (result.timed_out) {
    // 命令超过了超时时间
} else {
    // result.exit_code 指示失败原因
}
```

### 发现开关

可通过 `PloySemaOptions::enable_package_discovery` 切换发现功能：

```cpp
PloySemaOptions opts;
opts.enable_package_discovery = false;   // 默认值 — sema 不执行外部命令
PloySema sema(diagnostics, opts);
```

禁用后（默认行为），`IMPORT PACKAGE` 语句仍会被记录，但不会执行任何 `_popen`/`popen` 调用。调用者应改用 `PackageIndexer` 作为预编译阶段。

### 缓存策略

`PackageDiscoveryCache` 是线程安全（互斥锁保护）的会话级缓存。规范键格式为：

```
language + "|" + manager_kind + "|" + env_path
```

例如：`"python|pip|/home/user/venv"`。当同一会话中编译多个 `.ploy` 文件时，调用者可通过 `PloySemaOptions::discovery_cache` 注入共享缓存，避免重复运行 `pip list` / `cargo install --list` 等命令。

### 命令执行抽象

所有外部命令执行通过 `ICommandRunner` 接口（`command_runner.h`）路由。默认实现 `DefaultCommandRunner` 使用 `_popen`/`popen`。测试可注入 `MockCommandRunner` 以验证发现行为而无需生成真实进程。

### 平台适配

- **Windows**: 虚拟环境 Python 位于 `<venv_path>\Scripts\python.exe`
- **Unix/macOS**: 虚拟环境 Python 位于 `<venv_path>/bin/python`

## 4.4 混合编译方式

> **核心思想：PolyglotCompiler 自身编译所有语言，通过 `.ploy` 描述跨语言连接关系。**

```
┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐
│  C++ 源代码  │  │ Python 源代码│  │  Rust 源代码 │  │  Java 源代码 │  │  C# 源代码   │
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
                                       │  目标文件  │
                                       │ / 可执行   │
                                       └───────────┘
```

**重要说明：** PolyglotCompiler 使用自己的前端（`frontend_cpp`、`frontend_python`、`frontend_rust`、`frontend_java`、`frontend_dotnet`、`frontend_ploy`）编译所有语言源码到统一 IR，然后通过三重后端（x86_64/ARM64/WebAssembly）生成目标代码。编译阶段**不依赖**外部编译器（MSVC/GCC/rustc/CPython/javac/dotnet）。`polyc` 驱动程序 (`driver.cpp`) 在最终链接阶段会调用系统链接器（`polyld` 或 `clang`）将目标文件链接为可执行文件。

### 能力矩阵

| 功能 | 状态 | 说明 |
|------|------|------|
| C++ ↔ Python 函数调用 | ✅ | 通过 FFI 粘合代码 + 类型编组 |
| C++ ↔ Rust 函数调用 | ✅ | 通过 C ABI extern 函数 |
| Python ↔ Rust 函数调用 | ✅ | 通过 FFI 桥接 |
| Java ↔ C++ 函数调用 | ✅ | 通过 JNI 桥接 + `__ploy_java_*` 运行时 |
| Java ↔ Python 函数调用 | ✅ | 通过 JNI ↔ CPython C API 桥接 |
| .NET ↔ C++ 函数调用 | ✅ | 通过 CoreCLR 宿主 + `__ploy_dotnet_*` 运行时 |
| .NET ↔ Python 函数调用 | ✅ | 通过 CoreCLR ↔ CPython C API 桥接 |
| Java ↔ .NET 互操作 | ✅ | 通过 IR 层统一 + 双向运行时桥接 |
| 原始类型编组 | ✅ | int, float, bool, string, void |
| 容器类型编组 | ✅ | list, tuple, dict, optional（基本类型） |
| 结构体映射 | ✅ | 跨语言结构体字段转换（扁平结构体） |
| 包导入 + 版本约束 | ✅ | `IMPORT python PACKAGE numpy >= 1.20;` |
| 选择性导入 | ✅ | `IMPORT python PACKAGE numpy::(array, mean);` |
| 多包管理器支持 | ✅ | pip/conda/uv/pipenv/poetry/cargo/pkg-config/NuGet/Maven/Gradle |
| 多阶段管道 | ✅ | PIPELINE + 跨语言 CALL |
| 控制流编排 | ✅ | IF/WHILE/FOR/MATCH |
| 跨语言类实例化 | ✅ | `NEW(python, torch::nn::Linear, 784, 10)` |
| 跨语言方法调用 | ✅ | `METHOD(java, service, processRequest, data)` |
| 跨语言属性访问 | ✅ | `GET(dotnet, config, ConnectionString)` |
| 跨语言属性赋值 | ✅ | `SET(python, model, training, FALSE)` |
| 自动资源管理 | ✅ | `WITH(python, f) AS handle { ... }` |
| 类继承扩展 | ✅ | `EXTEND(java, BaseService) { ... }` |
| 对象销毁 | ✅ | `DELETE(dotnet, dbConnection)` |
| 类型注解 | ✅ | `LET model: python::nn::Module = NEW(...)` |
| 接口映射 | ✅ | `MAP_TYPE(python::nn::Module, cpp::NeuralNet)` |

### 架构约束

| 约束 | 解释 |
|------|------|
| **函数/对象级粒度** | 不能在单个函数体内混合不同语言的代码，但支持跨语言函数调用、类实例化、方法调用、属性访问和资源管理 |
| **运行时开销** | 跨语言调用涉及参数编组 |
| **内存模型差异** | 每种语言管理自己的内存，所有权通过 OwnershipTracker 追踪 |
| **WASM alloca** | 栈分配通过影子栈模型降级（可变 i32 全局变量，初始值 65536，向下增长）；线性内存布局与原生后端不同 |
| **链接器严格模式** | 未解析的跨语言符号视为硬错误——链接器不会为未解析的符号对生成粘合存根 |
| **语义分析严格模式** | 启用后，`.ploy` 语义分析对无类型参数、`Any` 回退和缺少注释发出警告；默认为宽松模式 |

---

# 5. 已实现功能

## 5.1 C++ 前端 (`frontend_cpp`)

### 基础语法 ✅
- 函数声明和定义、递归、函数重载
- 控制流：if-else、while、for、switch
- 变量声明、作用域、名字查找

### 面向对象 ✅
- 类和对象、构造函数/析构函数
- 单继承和多继承、虚继承（菱形继承）
- 访问控制（public/protected/private）
- 虚函数和多态

### 运算符重载 ✅
- 算术、比较、逻辑、位运算
- 下标运算符 `[]`、赋值运算符

### 模板 ✅
- 函数模板和类模板
- 模板参数推导、特化和偏特化
- 实例化缓存 (`template_instantiator.cpp`)

### RTTI ✅
- `typeid` 运算符、`dynamic_cast`、`static_cast`

### 异常处理 ✅
- try-catch-throw、多重 catch 子句
- IR 支持: invoke/landingpad/resume

### 浮点运算 ✅
- fadd, fsub, fmul, fdiv, frem; 浮点比较; fpext, fptrunc, fptosi

### constexpr ✅
- 编译期函数执行、constexpr 变量
- 实现: `frontends/cpp/src/constexpr/cpp_constexpr.cpp`

### SIMD 向量化 ✅
- 循环向量化: SSE/AVX (x86_64)、NEON (ARM64)

## 5.2 Python 前端 (`frontend_python`) ✅

- 类型注解函数
- 控制流（if/while/for/match）
- 类和对象
- 25+ 高级特性（装饰器、生成器、async/await、推导式、匹配语句等）

## 5.3 Rust 前端 (`frontend_rust`) ✅

- 函数、结构体
- 借用检查器和生命周期
- 闭包
- 28+ 高级特性（Traits、枚举、模式匹配、智能指针等）

## 5.4 .ploy 前端 (`frontend_ploy`) ✅

详见 [第 4 章](#4-ploy-跨语言链接前端)。完整功能列表：

- 54 个关键字的词法分析
- LINK/IMPORT/EXPORT/MAP_TYPE/PIPELINE/FUNC/STRUCT/CONFIG 声明解析
- 版本约束验证（6 种运算符）
- 选择性导入与符号验证
- 5 种包管理器配置（VENV/CONDA/UV/PIPENV/POETRY）
- 自动包发现
- 跨语言类实例化（NEW）和方法调用（METHOD）
- 跨语言属性访问（GET）和属性赋值（SET）
- 自动资源管理（WITH）
- 跨语言对象销毁（DELETE）和类继承扩展（EXTEND）
- 全面错误检查：参数数量不匹配 / 类型不匹配 / 溯源链
- 类型注解与限定类型
- 接口映射（MAP_TYPE）
- 完整的类型系统（原始类型 + 容器类型 + 结构体 + 函数类型）
- 控制流（IF/ELSE/WHILE/FOR/MATCH/BREAK/CONTINUE）
- 结构体字面量（带前瞻消歧，使用 LexerBase SaveState/RestoreState）
- IR Lowering（函数/管道/链接/表达式/控制流/OOP互操作/对象销毁/类继承）

## 5.5 Java 前端 (`frontend_java`) ✅

Java 前端，支持 Java 8、17、21 和 23 的核心特性（测试覆盖持续扩展中）：

### 词法分析器
- 完整的 Java 关键字和运算符词法分析
- 字符串/字符/数字/注解字面量
- 单行和多行注释处理

### 解析器
- 类、接口、枚举、记录类（Java 16+）
- 密封类和 permits（Java 17+）
- 模式匹配 instanceof（Java 16+）
- Switch 表达式（Java 14+）
- 文本块（Java 13+）
- 方法、构造器、字段、泛型
- 导入和包声明
- Lambda 表达式和方法引用
- try-with-resources（Java 7+）

### 语义分析
- 类型解析和作用域管理
- 方法重载检测
- 记录类组件的隐式访问器
- 密封类的许可验证
- 构造器和方法体分析
- 二元表达式的类型推导

### IR 降级
- 类 → 结构体降级，生成构造器（`<init>`）
- 方法降级，名称修饰（`ClassName::method`）
- 枚举 → 整型常量降级
- 记录类 → 带组件访问器的结构体
- 接口 → 虚表生成
- `System.out.println` → `__ploy_java_print` 运行时桥接

### 运行时桥接 (`java_rt`)
- JNI 兼容的桥接 API
- 版本感知的初始化（Java 8/17/21/23）
- 对象生命周期管理

## 5.6 .NET 前端 (`frontend_dotnet`) ✅

C# (.NET) 前端，支持 .NET 6、7、8 和 9 的核心特性（测试覆盖持续扩展中）：

### 词法分析器
- 完整的 C# 关键字和运算符词法分析（包括 `??`、`?.`、`=>`）
- 常规和逐字字符串字面量
- 数值字面量（十六进制、二进制、十进制）
- XML 文档注释处理

### 解析器
- 类、结构体、接口、枚举、记录类（.NET 5+）
- 命名空间和文件范围命名空间（C# 10）
- 顶级语句（C# 9+）
- 主构造器（C# 12）
- Using 指令（包括全局 using，C# 10）
- 委托
- 属性及 get/set 访问器
- 析构器/终结器
- 方法、构造器、字段、泛型
- 可空引用类型
- 模式匹配和 switch 表达式

### 语义分析
- 类型解析和作用域管理
- 命名空间范围的符号查找
- 属性和事件分析
- 构造器和方法体分析
- Using 指令注册
- `var` 声明的类型推导

### IR 降级
- 类/结构体 → 结构体降级，生成构造器（`.ctor`）
- 方法降级，名称修饰（`ClassName::Method`）
- 枚举 → 整型常量降级
- 命名空间 → 作用域前缀
- 顶级语句 → `<Program>::Main` 入口点
- `Console.WriteLine` → `__ploy_dotnet_print` 运行时桥接

### 运行时桥接 (`dotnet_rt`)
- CoreCLR 兼容的桥接 API
- 版本感知的初始化（.NET 6/7/8/9）
- 垃圾回收对象互操作

---

# 6. 使用指南

## 6.1 编译器驱动 (`polyc`)

```bash
# 基本用法 — 根据文件扩展名自动检测语言
polyc <输入文件> -o <输出>

# 显式指定语言
polyc --lang=ploy input.ploy -o output

# 完整用法
polyc [选项] <输入文件>
```

### 选项

| 选项 | 说明 |
|------|------|
| `--lang=<cpp\|python\|rust\|java\|dotnet\|ploy>` | 源语言（省略时根据扩展名自动检测） |
| `--arch=<x86_64\|arm64\|wasm>` | 目标架构（默认：x86_64） |
| `-O<0\|1\|2\|3>` | 优化级别 |
| `--emit-ir=<文件>` | 输出 IR 文本 |
| `--emit-asm=<文件>` | 输出汇编 |
| `-o <文件>` | 输出目标文件/可执行文件 |
| `--debug` | 生成调试信息 (DWARF 5) |
| `--regalloc=<linear-scan\|graph-coloring>` | 寄存器分配器选择 |
| `--obj-format=<elf\|macho\|coff\|pobj>` | 目标文件格式（按操作系统自动检测：Windows 为 `coff`，Linux 为 `elf`，macOS 为 `macho`） |
| `--quiet` / `-q` | 禁止进度输出 |
| `--no-aux` | 禁止辅助文件生成 |
| `--force` | 遇到错误继续编译 |
| `--strict` | 严格模式：拒绝占位类型，禁止降级桩 |
| `--permissive` | 宽松模式：允许占位类型（覆盖 Release 默认设置） |
| `-h` / `--help` | 显示帮助信息 |

> **注意：** Release 构建默认启用严格模式（通过 `POLYC_DEFAULT_STRICT`）。
> 使用 `--permissive` 覆盖。`--strict` 和 `--force` 互斥。

### 编译模式

`polyc` 支持两种编译模式，控制对占位类型和未解析跨语言类型信息的处理方式：

| 模式 | 占位类型处理 | `--force` 桩 | 默认启用场景 |
|------|-------------|--------------|-------------|
| **严格模式** | 错误 | 禁止 | Release 构建 |
| **宽松模式** | 警告 | 允许 | Debug 构建 |

**严格模式**（`--strict` 或 Release 默认）：
- 语义分析将占位类型回退（如无类型参数默认为 `Any`）报告为**错误**
- IR 验证器拒绝包含未解析占位 `I64` 返回类型的函数
- 降级阶段拒绝返回类型无法解析的跨语言调用
- 禁止通过 `--force` 生成降级桩
- `--strict` 和 `--force` 互斥

**宽松模式**（`--permissive` 或 Debug 默认）：
- 占位类型回退报告为**警告**
- 验证器接受 I64 占位返回类型
- `--force` 可为不完整编译生成降级桩

### 语言自动检测

`polyc` 根据文件扩展名自动检测源语言：

| 扩展名 | 语言 |
|--------|------|
| `.ploy` | ploy |
| `.py` | python |
| `.cpp`, `.cc`, `.cxx`, `.c` | cpp |
| `.rs` | rust |
| `.java` | java |
| `.cs` | dotnet |

### 进度输出

默认情况下，`polyc` 向 stderr 打印详细进度信息：

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

使用 `--quiet` 禁止进度输出。

### 辅助文件

默认情况下，`polyc` 在源文件所在目录的 `aux/` 子目录中生成中间文件。所有辅助文件使用 **PAUX 二进制容器格式**（文件头：`"PAUX"` 魔数 + 版本号 + 段数量），防止中间数据以明文形式暴露。

| 文件 | 内容 |
|------|------|
| `<stem>.tokens.paux` | 词法分析器 token 输出（类型、位置、文本）— 二进制 |
| `<stem>.ast.paux` | AST 摘要（声明数量、位置）— 二进制 |
| `<stem>.symbols.paux` | 符号表条目（类型、种类）— 二进制 |
| `<stem>.ir.paux` | IR 文本表示 — 二进制 |
| `<stem>.descriptors.paux` | 跨语言调用描述符 — 二进制 |
| `<stem>.asm.paux` | 生成的汇编代码 — 二进制 |
| `<stem>.obj` / `<stem>.o` | COFF/ELF/Mach-O 目标文件（平台相关） |
| `<stem>_<lang>.lib.pobj` | 按语言分离的桥接库（如 `_cpp.lib.pobj`、`_python.lib.pobj`） |

#### PAUX 二进制格式

```
偏移量  大小  字段
0       4     魔数: "PAUX"
4       2     版本: 1
6       2     段数量: N
8       8     保留（零填充）
16      ...   段数据: 每段包含:
                uint16 名称长度 + 名称字节 + uint32 数据长度 + 数据字节
```

#### 按语言分离的桥接库

`.ploy` 文件中引用的每种语言都会在 `aux/` 中生成独立的桥接库目标文件。例如，一个链接 C++ 和 Python 函数的 `.ploy` 文件会产生：
- `<stem>_cpp.lib.pobj` — 包含 C++ 互操作桥接符号
- `<stem>_python.lib.pobj` — 包含 Python 互操作桥接符号

这样可以实现按语言的分离编译和链接，而不是单体输出。

### 二进制目标文件输出

在 **compile** 模式（默认）下，`polyc` 现在会在中间文件之外额外生成二进制目标文件：
- **Windows**: COFF `.obj` 文件（与 MSVC `link.exe` 兼容）
- **Linux**: ELF `.o` 文件
- **macOS**: Mach-O `.o` 文件

在 **link** 模式下，`polyc` 还会调用系统链接器生成可执行文件。

使用 `--no-aux` 禁止辅助文件生成。`aux/` 目录通过 `.gitignore` 自动排除。

### 优化级别

| 级别 | 说明 | 启用的优化 |
|------|------|-----------|
| -O0 | 无优化 | 无 |
| -O1 | 基础优化 | 常量折叠、DCE |
| -O2 | 标准优化 | +CSE、内联、GVN |
| -O3 | 激进优化 | +去虚化、向量化、循环优化、高级优化 |

## 6.2 链接器 (`polyld`)

```bash
polyld -o program file1.o file2.o file3.o
polyld -static -o program main.o -lmylib      # 静态链接
polyld -shared -o libmylib.so obj1.o obj2.o    # 共享库
```

`polyld` 内部包含 `PolyglotLinker`，它消费 `.ploy` 前端的 IR，生成跨语言粘合代码：

- `CrossLangCallDescriptor` — 描述跨语言调用的类型映射
- `LinkEntry` — 描述 LINK 声明中的源/目标函数对
- `ResolveLinks()` — 解析所有链接条目，生成粘合代码
- 容器类型检测: `IsListType()`, `IsDictType()`, `IsTupleType()`, `IsStructType()`

## 6.3 PGO (Profile-Guided Optimization)

```bash
# 1. 生成插桩版本
polyc -fprofile-generate input.cpp -o app.instrumented

# 2. 运行收集 profile
./app.instrumented < typical_input.txt

# 3. 使用 profile 优化
polyc -fprofile-use=default.profdata input.cpp -o app.optimized
```

实现: `middle/src/pgo/profile_data.cpp`

## 6.4 LTO (Link-Time Optimization)

```bash
# Thin LTO（推荐）
polyc -flto=thin -c file1.cpp -o file1.o
polyc -flto=thin file1.o file2.o -o app
```

实现: `middle/src/lto/link_time_optimizer.cpp`

## 6.5 .ploy 示例文件

项目在 `tests/samples/` 中包含 12 个 `.ploy` 示例：

| 文件 | 内容 |
|------|------|
| `basic_linking.ploy` | 基础 LINK + MAP_TYPE |
| `complex_types.ploy` | 结构体、容器类型映射 |
| `container_marshalling.ploy` | LIST/TUPLE/DICT 编组 |
| `advanced_pipeline.ploy` | 多阶段 PIPELINE |
| `multi_language_pipeline.ploy` | 三语言管道 |
| `pipeline_control_flow.ploy` | IF/WHILE/FOR/MATCH |
| `mixed_compilation.ploy` | 混合编译示例 |
| `error_handling.ploy` | 错误处理 |
| `package_import.ploy` | 包导入 + 版本约束 + 选择性导入 + CONFIG |
| `cross_lang_class_instantiation.ploy` | 跨语言类实例化混合调用 |
| `java_interop.ploy` | Java 互操作（字符串处理 + 跨语言管道） |
| `dotnet_interop.ploy` | .NET 互操作（数据服务 + 统计分析管道） |

## 6.6 示例环境配置

`tests/samples/` 目录包含环境配置脚本，用于创建运行示例所需的本地 Python 和 Rust 环境。

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

脚本将创建：
- **Python 虚拟环境**：位于 `tests/samples/env/python/`，预装 numpy、torch、typing-extensions
- **Rust 工具链**：位于 `tests/samples/env/rust/`，使用独立的 RUSTUP_HOME/CARGO_HOME

`env/` 目录通过 `.gitignore` 自动排除，不会被 git 同步。

## 6.7 IDE (`polyui`)

`polyui` 是 PolyglotCompiler 的基于 Qt 的桌面集成开发环境（IDE）。它提供了语法高亮、实时诊断和项目文件浏览等功能。

### 前置条件

- **Qt 6**（推荐）或 **Qt 5.15+**（需要 Widgets 模块）
- CMake 自动在以下路径发现 Qt：
  - `D:\Qt`（Windows）
  - `deps/qt/`（项目本地，所有平台 — 通过 `aqtinstall` 安装）
  - 系统路径（如 Homebrew、apt）
- 可通过 `-DQT_ROOT=<path>` 覆盖

**快速安装（未安装 Qt 时）：**

```bash
# macOS / Linux
./tools/ui/setup_qt.sh

# Windows (PowerShell)
.\tools\ui\setup_qt.ps1
```

此命令将预编译的 Qt 6.10.2 二进制文件下载到 `deps/qt/` 目录（已被 git 忽略）。

### 构建

```bash
cmake --build build --target polyui
```

如果未找到 Qt，`polyui` 目标将被静默跳过，不影响其他目标的构建。

在 **Windows** 上，链接完成后会自动调用 `windeployqt` 将所需的 Qt DLL（如 `Qt6Cored.dll`、`Qt6Guid.dll`、`Qt6Widgetsd.dll`）和平台插件复制到构建目录。这意味着可以直接双击 `polyui.exe` 运行，无需手动配置 DLL。

在 **macOS** 上，会自动调用 `macdeployqt` 将 Qt 框架打包到 `.app` 目录中。应用程序以原生 macOS Bundle 形式构建，支持 Retina 显示。

在 **Linux** 上，构建生成标准 ELF 可执行文件。请确保目标系统上可用 Qt 运行时库（通常通过发行版的包管理器安装）。

### 按平台分离的源码布局

`polyui` 的源代码按平台组织：

```
tools/ui/
├── common/          # 跨平台共享代码（所有平台均编译）
│   ├── src/         #   mainwindow.cpp、code_editor.cpp、syntax_highlighter.cpp、
│   │                #   file_browser.cpp、output_panel.cpp、compiler_service.cpp、
│   │                #   settings_dialog.cpp、git_panel.cpp、build_panel.cpp、
│   │                #   debug_panel.cpp、theme_manager.cpp
│   └── include/     #   对应的头文件（含 theme_manager.h）
├── windows/         # Windows 专用入口点 (main.cpp)
├── linux/           # Linux 专用入口点 (main.cpp)
└── macos/           # macOS 专用入口点 (main.cpp)
```

CMake 会根据当前操作系统自动选择正确的平台专用 `main.cpp`。每个平台的入口点包含特定于操作系统的初始化逻辑（如 Windows 上的 `windeployqt`、Linux 上的 `xcb` 平台默认值、macOS 上的 `MACOSX_BUNDLE`）。

### 启动

```bash
# 打开 IDE
./build/polyui

# 直接打开项目文件夹
./build/polyui --folder /path/to/project
```

### 功能特性

| 功能 | 说明 |
|------|------|
| **语法高亮** | 使用编译器前端分词器实现精确的、语言感知的高亮，支持全部 6 种语言 |
| **实时诊断** | 调用完整编译管道（词法 → 语法 → 语义）实时报告错误和警告 |
| **文件浏览器** | 树形项目导航器，按支持的源文件扩展名过滤 |
| **多标签编辑器** | 支持多文件编辑，含新建、打开、保存、关闭标签功能 |
| **输出面板** | 三标签输出区域：编译输出、错误表格（点击跳转源码）、日志 |
| **内置终端** | 嵌入式 Shell（Windows 为 PowerShell，Linux/macOS 为 bash/zsh），支持 ANSI 颜色、命令历史和多实例 |
| **代码导航** | 双击诊断表格中的错误行，跳转到对应源码位置 |
| **括号匹配** | 高亮光标处的匹配括号 / 圆括号 / 花括号 |
| **自动缩进** | 新行自动保持当前缩进级别 |
| **缩放** | `Ctrl+加号` / `Ctrl+减号` 调整编辑器字体大小 |
| **暗色主题** | 通过 ThemeManager 统一管理主题，内置 4 套配色方案（暗色、亮色、Monokai、Solarized Dark），所有面板统一切换 |
| **编译与运行** | 一键编译并运行当前文件（`Ctrl+R`），自动查找输出二进制文件并通过 QProcess 启动，stdout/stderr 实时输出至日志面板；支持停止运行中的进程（`Ctrl+Shift+R`） |
| **设置对话框** | 7 类偏好设置（外观、编辑器、编译器、环境、构建、调试、键绑定），通过 `QSettings` 持久化存储 |
| **自定义快捷键** | 在设置对话框的键绑定页中使用 QKeySequenceEdit 编辑快捷键，支持应用、重置，自定义快捷键通过 QSettings 持久化存储并在启动时自动加载 |
| **设置联动** | 设置中的 CMake 路径、构建目录、调试器路径等配置自动同步至构建面板和调试面板，修改后即时生效 |
| **Git 集成** | 内置 Git 面板，支持状态查看、暫存、提交、分支管理、推送/拉取、差异查看、日志历史和储藏功能 |
| **构建系统** | CMake 集成面板，支持配置/构建/清理、生成器和构建类型选择、目标发现和错误解析 |
| **调试器** | 集成调试面板，支持 lldb 和 gdb，包括断点、单步执行、完整的调用栈帧解析（模块、函数、文件、行号）、变量类型/值解析、监视表达式实时求值与结果更新、监视右键菜单（删除/全部删除/求值）、入口断点选项和调试控制台 |

### 快捷键

| 快捷键 | 操作 |
|--------|------|
| `Ctrl+N` | 新建文件 |
| `Ctrl+O` | 打开文件 |
| `Ctrl+S` | 保存文件 |
| `Ctrl+Shift+S` | 另存为 |
| `Ctrl+W` | 关闭当前标签 |
| `Ctrl+Z` | 撤销 |
| `Ctrl+Y` | 重做 |
| `Ctrl+X` / `Ctrl+C` / `Ctrl+V` | 剪切 / 复制 / 粘贴 |
| `Ctrl+F` | 查找 |
| `Ctrl+B` | 编译当前文件 |
| `Ctrl+R` | 编译并运行当前文件 |
| `Ctrl+Shift+R` | 停止运行中的进程 |
| `Ctrl+Shift+B` | 分析当前文件（仅诊断） |
| `` Ctrl+` `` | 切换内置终端 |
| `` Ctrl+Shift+` `` | 打开新终端实例 |
| `Ctrl+加号` / `Ctrl+减号` | 放大 / 缩小 |
| `Ctrl+,` | 打开设置对话框 |
| `F9` | 开始调试 |
| `F10` | 单步跳过 |
| `F11` | 单步进入 |
| `Shift+F11` | 单步跳出 |

### 支持的语言

IDE 使用与 `polyc` 相同的前端分词器，确保以下语言的精确高亮和诊断：

- C++（`.cpp`、`.h`、`.hpp`、`.cxx`）
- Python（`.py`）
- Rust（`.rs`）
- Java（`.java`）
- C# / .NET（`.cs`）
- Ploy（`.ploy`）

### 内置终端

IDE 内置了终端面板，无需离开编辑器即可进行交互式命令行操作。

**功能特性：**
- **平台感知 Shell 检测**：Windows 自动启动 PowerShell，macOS 启动 zsh，Linux 启动 bash（或 `$SHELL`）
- **多终端实例**：可创建任意数量的独立终端会话，每个运行在单独的标签页中
- **ANSI 颜色支持**：解析并渲染基本 SGR 转义码，显示彩色输出
- **命令历史**：通过上/下方向键浏览历史命令（最多 500 条）
- **快捷键**：`Ctrl+C` 发送中断、`Ctrl+L` 清屏、`` Ctrl+` `` 切换面板
- **工作目录同步**：新终端自动打开到文件浏览器中显示的项目根目录

**使用方法：**
1. 按 `` Ctrl+` `` 切换终端面板，或使用 **终端 → 切换终端** 菜单
2. 按 `` Ctrl+Shift+` `` 打开新的终端实例
3. 使用 **终端** 菜单执行清空、重启和新建终端操作
4. 通过标签页关闭按钮关闭单个终端

### 设置

通过 **文件 → 设置** 或 `Ctrl+,` 打开设置对话框。偏好设置分为 7 个类别：

| 类别 | 选项 |
|------|------|
| **外观** | 主题、UI 字体和大小 |
| **编辑器** | 字体、大小、Tab 宽度、自动换行、显示空白字符、行号 |
| **编译器** | 默认优化级别、目标架构、C++ 标准、额外标志 |
| **环境** | 项目根目录、CMake 路径、调试器路径、PATH 覆盖 |
| **构建** | 默认生成器、构建类型、并行任务数、CMake 额外参数 |
| **调试** | 首选调试器（lldb/gdb/自动）、入口停止、异常断点 |
| **键绑定** | 查看和参考所有键盘快捷键 |

所有设置通过 `QSettings`（"PolyglotCompiler"/"IDE"）持久化，点击 **应用** 或 **确定** 后立即生效。

### Git 集成

Git 面板（通过 **视图 → Git 面板** 切换）提供不离开 IDE 的版本控制功能。

**功能特性：**
- **状态视图**：彩色编码的树形视图，显示已暫存、未暫存和未跟踪文件（绿色/黄色/灰色图标）
- **暫存操作**：通过工具栏按钮和右键菜单暫存/取消暫存单个文件或所有文件
- **提交**：输入提交消息，支持追加修改（amend）；从工具栏提交
- **分支管理**：从分支选择器创建、删除、合并和切换分支
- **远程操作**：推送、拉取和获取异步运行，输出区域显示进度反馈
- **差异查看器**：在输出文本区域查看文件差异；双击文件显示其差异
- **日志历史**：浏览提交历史，包括哈希、作者、日期和提交消息
- **储藏**：通过工具栏操作储藏和弹出工作目录更改

### 构建系统

构建面板（通过 **视图 → 构建面板** 切换）集成了基于 CMake 的项目构建功能。

#### 模块化 CMake 结构

项目采用模块化的 `add_subdirectory()` 布局，将原有的顶层单体 `CMakeLists.txt` 拆分为按目录组织的构建文件：

```
CMakeLists.txt          ← 项目设置、编译选项、依赖项、add_subdirectory() 调用
├── common/CMakeLists.txt      ← polyglot_common 库
├── middle/CMakeLists.txt      ← middle_ir（IR、优化遍、PGO、LTO）
├── frontends/CMakeLists.txt   ← frontend_common + 所有语言前端
├── backends/CMakeLists.txt    ← backend_x86_64、backend_arm64、backend_wasm
├── runtime/CMakeLists.txt     ← runtime 库（GC、FFI、互操作、服务）
├── tools/CMakeLists.txt       ← polyc、polyasm、polyld、polyopt、polyrt、polybench、polyui
└── tests/CMakeLists.txt       ← unit_tests、integration_tests、benchmark_tests
```

单元测试源文件按模块显式列出（不使用 `GLOB_RECURSE`），实现精确的增量重编粒度。

**功能特性：**
- **配置**：使用所选生成器（Makefiles、Ninja、Xcode 等）和构建类型（Debug、Release、RelWithDebInfo、MinSizeRel）运行 `cmake`
- **构建 / 重新构建**：构建整个项目或通过 `cmake --build --target help` 发现的单个目标
- **清理**：删除所有构建产物
- **错误解析**：解析编译器输出中的 GCC/Clang 和 MSVC 风格错误消息；匹配的错误显示在错误树中，发出 `BuildErrorFound` 信号打开源码位置
- **进度条**：跟踪 CMake 基于百分比的构建输出
- **路径发现**：自动搜索 `/opt/homebrew/bin`、`/usr/local/bin`、`/usr/bin` 和 `$PATH` 中的 `cmake` 可执行文件

**使用方法：**
1. 通过浏览按钮或直接输入路径设置源目录和构建目录
2. 选择生成器和构建类型，然后点击 **配置**
3. 选择构建目标（或“all”），然后点击 **构建**
4. 错误显示在下方树中；双击跳转到源码位置

### 调试

调试面板（通过 **视图 → 调试面板** 切换）提供通过 lldb 或 gdb 的交互式调试功能。

**功能特性：**
- **调试器自动检测**：macOS 优先使用 lldb，Linux 优先使用 gdb；可在设置中配置
- **断点**：按文件和行号切换断点，支持条件断点，可全部移除；断点显示在专用树中
- **执行控制**：启动（`F9`）、停止、暂停、继续、单步跳过（`F10`）、单步进入（`F11`）、单步跳出（`Shift+F11`）、运行到光标
- **调用栈**：查看当前调用栈，包括帧号、函数名、文件和行号
- **变量检查**：查看局部变量的名称、值和类型
- **监视表达式**：添加任意表达式在当前调试上下文中求值
- **调试控制台**：发送原始调试器命令并查看响应
- **源码导航**：调试器停止时，IDE 自动打开对应源文件并通过 `DebugLocationChanged` 信号高亮当前行

**使用方法：**
1. 在面板顶部设置可执行文件路径和可选的程序参数
2. 从断点树中切换断点（添加 / 全部移除）
3. 点击 **启动** 或按 `F9` 开始调试
4. 使用工具栏按钮或键盘快捷键单步执行代码
5. 在侧边面板中检查变量和调用栈；根据需要添加监视表达式
6. 使用控制台标签发送原始调试器命令

---

# 7. IR 设计规范

## 7.1 概述

PolyglotCompiler 的中间表示（IR）是完整的 SSA 形式、显式控制流、显式内存模型的中间语言。

核心实现文件:
- `middle/src/ir/builder.cpp` — IR 构建器
- `middle/src/ir/ir_context.cpp` — IR 上下文管理
- `middle/src/ir/cfg.cpp` — 控制流图
- `middle/src/ir/ssa.cpp` — SSA 转换
- `middle/src/ir/analysis.cpp` — IR 分析
- `middle/src/ir/verifier.cpp` — IR 验证器
- `middle/src/ir/printer.cpp` — IR 文本输出
- `middle/src/ir/parser.cpp` — IR 文本解析
- `middle/src/ir/data_layout.cpp` — 数据布局
- `middle/src/ir/template_instantiator.cpp` — 模板实例化

## 7.2 类型系统

### 标量类型
- **整数**: `i1, i8, i16, i32, i64`
- **浮点**: `f32, f64`
- **其他**: `void`

### 聚合类型
- **指针**: 单一指向类型
- **数组**: `[N x T]`
- **向量**: `<N x T>` (用于 SIMD)
- **结构体**: `{T0, T1, ...}`
- **函数**: `(ret, params...)`

## 7.3 核心指令集

### 算术 / 逻辑
```
add, sub, mul, sdiv/udiv, srem/urem     # 整数运算
and, or, xor, shl, lshr, ashr           # 位运算
icmp (eq, ne, slt, sle, sgt, sge,       # 整数比较
      ult, ule, ugt, uge)
fadd, fsub, fmul, fdiv, frem              # 浮点运算
fcmp (foe, fne, flt, fle, fgt, fge)     # 浮点比较
```

### 内存操作
```
alloca      # 栈分配
load        # 加载
store       # 存储
gep         # 地址计算 (GetElementPtr)
```

### 控制流
```
ret         # 返回
br          # 无条件跳转
cbr         # 条件跳转
switch      # 多路分支
```

### 调用与异常
```
call        # 直接/间接调用
invoke      # 可能抛异常的调用
landingpad  # 异常着陆点
resume      # 异常恢复
```

### SSA
```
phi         # SSA 合并节点
```

### 类型转换
```
sext, zext, trunc           # 整数扩展/截断
fpext, fptrunc              # 浮点扩展/截断
fptosi, sitofp              # 浮点 ↔ 整数
bitcast                     # 位转换
```

## 7.4 调用约定

| 约定 | 整数参数 | 浮点参数 | 返回值 | 栈对齐 |
|------|---------|---------|--------|--------|
| x86_64 SysV | RDI, RSI, RDX, RCX, R8, R9 | XMM0-7 | RAX/XMM0 | 16 字节 |
| ARM64 AAPCS64 | X0-X7 | V0-V7 | X0/V0 | 16 字节 |

### 跨语言 ABI 验证

编译器在多个阶段执行 ABI 兼容性检查：

1. **语义分析阶段（编译时）：** 当 `LINK` 声明包含 `MAP_TYPE` 条目时，语义分析器为目标函数和源函数构建 `ABISignature` 描述符。交叉验证内容包括：
   - 参数数量匹配
   - 参数大小兼容性（字节级）
   - 指针与值传递约定匹配
   - 返回类型大小兼容性

2. **链接阶段（链接时）：** 多语言链接器（`polyld`）在生成胶水桩之前验证 `CrossLangSymbol` 描述符：
   - 源符号和目标符号之间的参数数量
   - 每个参数的大小和传递约定
   - 返回类型大小兼容性
   - MAP_TYPE 条目数量与实际符号参数数量

3. **描述符文件验证：** 加载 `.paux` 描述符文件时，链接器验证：
   - 语言标识符是否在已知集合中
   - 符号名称非空
   - 重复条目检测
   - 描述符格式正确性

### 语言到调用约定映射

| 语言 | 约定（Linux/macOS） | 约定（Windows） |
|------|-------------------|----------------|
| C/C++/Rust | System V AMD64 | Microsoft x64 |
| Python | Python C API (System V / x64) | Python C API (x64) |
| Java | JNI (System V / x64) | JNI (x64) |
| .NET/C# | P/Invoke (System V / x64) | P/Invoke (x64) |
| .ploy | System V AMD64 | Microsoft x64 |

实现：
- `frontends/ploy/include/ploy_sema.h`（ABISignature、ABIParamDesc）
- `frontends/ploy/src/sema/sema.cpp`（BuildABISignature、ValidateCompatibility）
- `tools/polyld/src/polyglot_linker.cpp`（链接器级 ABI 验证）
- `backends/x86_64/src/calling_convention.cpp`
- `backends/arm64/src/calling_convention.cpp`
- `runtime/src/interop/calling_convention.cpp`（跨语言调用约定适配）

## 7.5 IR 验证规则

- 每个基本块恰好一个终结指令
- 操作数必须通过类型检查
- 定义必须支配使用（SSA 特性）
- Phi 节点的输入类型必须匹配结果类型
- 函数参数数量和类型必须匹配声明

---

# 8. 优化系统

## 8.1 Pass 管理器

```cpp
// middle/src/passes/pass_manager.cpp
class PassManager {
    std::vector<std::unique_ptr<Pass>> passes_;
public:
    void AddPass(std::unique_ptr<Pass> pass);
    void Run();
};
```

## 8.2 优化 Pass 列表

### 核心 Transform Passes（7 个实现文件）

| Pass | 文件 | 功能 |
|------|------|------|
| `ConstantFold` | `constant_fold.cpp` | 常量折叠 |
| `DeadCodeElim` | `dead_code_elim.cpp` | 死代码消除 |
| `CommonSubExpr` | `common_subexpr.cpp` | 公共子表达式消除 (CSE) |
| `Inlining` | `inlining.cpp` | 函数内联 |
| `Devirtualization` | `devirtualization.cpp` | 虚函数去虚化 |
| `LoopOptimization` | `loop_optimization.cpp` | 循环优化（展开/LICM/融合/分裂/分块/交换） |
| `GVN` | `gvn.cpp` | 全局值编号 |

### Analysis Passes（2 个）

| Pass | 文件 | 功能 |
|------|------|------|
| `AliasAnalysis` | `alias.h` | 别名分析 |
| `DominanceAnalysis` | `dominance.h` | 支配关系分析 |

### 高级优化（`advanced_optimizations.cpp` 中）

包含 20+ 高级优化：

- **循环**: 尾调用优化、循环谓词化、软件流水线
- **数据流**: 强度削减、归纳变量消除、SCCP
- **内存**: 逃逸分析、标量替换、死存储消除
- **并行**: 自动向量化
- **其他**: 部分求值、代码沉降/提升、跳转线程化、预取插入、分支预测优化、内存布局优化

### 后端优化

| Pass | 文件 | 功能 |
|------|------|------|
| 指令调度 | `backends/x86_64/src/asm_printer/scheduler.cpp` | 指令重排以减少停顿 |
| x86_64 优化 | `backends/x86_64/src/optimizations.cpp` | 窥孔优化、指令融合 |

## 8.3 PGO 支持

- 实现: `middle/src/pgo/profile_data.cpp`
- 运行时性能计数器
- Profile 数据持久化
- 基于调用频率的内联决策
- 热代码布局优化

## 8.4 LTO 支持

- 实现: `middle/src/lto/link_time_optimizer.cpp`
- 跨模块优化
- 全局死代码消除
- Thin LTO（更快，内存占用更少）

---

# 9. 运行时系统

## 9.1 垃圾回收（4 种算法）

| GC 类型 | 文件 | 特点 | 适用场景 |
|---------|------|------|---------|
| 标记-清除 | `mark_sweep.cpp` | 通用 | 一般应用 |
| 分代 GC | `generational.cpp` | 利用对象年龄分布 | 大部分应用（推荐） |
| 复制式 GC | `copying.cpp` | 半空间收集、减少碎片 | 短生命周期对象 |
| 增量式 GC | `incremental.cpp` | 三色标记、低停顿 | 低延迟要求 |

GC 策略选择: `gc_strategy.cpp`

## 9.2 FFI 互操作

| 组件 | 文件 | 功能 |
|------|------|------|
| FFI 绑定 | `interop/ffi.cpp` | FFI 绑定、所有权追踪、动态库加载 |
| 类型编组 | `interop/marshalling.cpp` | 整数/浮点/字符串编组 |
| 容器编组 | `interop/container_marshal.cpp` | list/tuple/dict/optional 编组 |
| 类型映射 | `interop/type_mapping.cpp` | 跨语言类型映射注册表 |
| 调用约定 | `interop/calling_convention.cpp` | 跨语言调用约定适配 |
| 内存管理 | `interop/memory.cpp` | 跨语言内存管理 |

## 9.3 语言运行时

| 组件 | 文件 | 功能 |
|------|------|------|
| 基础运行时 | `libs/base.c` | 基础运行时设施 |
| GC 桥接 | `libs/base_gc_bridge.cpp` | GC ↔ 运行时桥接 |
| Python RT | `libs/python_rt.c` | Python 运行时支持 |
| C++ RT | `libs/cpp_rt.c` | C++ 运行时支持 |
| Rust RT | `libs/rust_rt.c` | Rust 运行时支持 |

## 9.4 服务

| 服务 | 文件 | 功能 |
|------|------|------|
| 异常处理 | `services/exception.cpp` | 跨语言异常处理 |
| 反射 | `services/reflection.cpp` | 运行时类型信息 |
| 线程 | `services/threading.cpp` | 线程池、任务调度、同步原语、协程 |

## 9.5 调试信息

- 完整 DWARF 5 支持 (`common/src/debug/dwarf5.cpp`)
- 统一 DWARF 构建器 (`backends/common/src/dwarf_builder.cpp`)
- 调试信息发射器 (`backends/common/src/debug_info.cpp`, `debug_emitter.cpp`)
- PDB 支持：MSF 块布局、TPI 类型记录、RFC 4122 v4 GUID 生成
- CFA（调用帧地址）寄存器保存规则和对齐
- ELF 目标文件含 SHT_RELA 重定位节
- Mach-O 目标文件含 LC_SEGMENT_64、LC_SYMTAB、nlist_64 符号表
- 变量位置跟踪（优化后仍可调试）
- 内联函数调试
- 分离调试信息支持
- JSON 源码映射发射

---

# 10. 测试体系

## 10.1 测试概览

项目使用 **Catch2** 测试框架。共有三个测试可执行文件：

| 可执行文件 | 源码目录 | 标签 | 说明 |
|-----------|---------|------|------|
| `unit_tests` | `tests/unit/` | `[ploy]`, `[gc]`, `[opt]` 等 | 所有模块的单元测试 — **743 个用例** |
| `integration_tests` | `tests/integration/` | `[integration]` | 端到端编译管道、互操作、性能压力 — **52 个用例** |
| `benchmark_tests` | `tests/benchmarks/` | `[benchmark]` | 微基准和宏基准性能测试 — **18 个用例** |

### 测试套件汇总

| 测试套件 | 标签 | 测试用例数 | 覆盖内容 |
|---------|------|-----------|---------|
| .ploy 前端 | `[ploy]` | 216 | 词法/语法/语义/IR/集成/包管理/OOP互操作/错误检查 |
| Python 前端 | `[python]` | 127 | 25+ 高级特性，类型注解，async，推导式 |
| Rust 前端 | `[rust]` | 46 | 借用检查、生命周期、闭包、Traits |
| 链接器 | `[linker]` | 36 | 符号解析、ELF/MachO/COFF、跨语言粘合 |
| FFI / 互操作 | `[ffi]` | 39 | FFI 绑定、编组、类型映射、所有权跟踪 |
| E2E 管道 | `[e2e]` | 29 | 从源码到目标码的完整管道 |
| Java 前端 | `[java]` | 22 | Java 8/17/21/23 特性 |
| .NET 前端 | `[dotnet]` | 24 | .NET 6/7/8/9 特性 |
| GC 算法 | `[gc]` | 20 | 4 种 GC 算法（标记清除、分代、拷贝、增量） |
| 预处理器 | `[preprocessor]` | 18 | 对象宏/函数宏、指令、Token池 |
| 优化 Passes | `[opt]` | 17 | 常量折叠、DCE、CSE、GVN、内联、去虚拟化 |
| 线程服务 | `[threading]` | 16 | 线程池、同步原语、协程 |
| LTO | `[lto]` | 14 | 链接时优化、跨模块优化 |
| PGO | `[pgo]` | 13 | 剖面引导优化 |
| 后端 | `[backend]` | 12 | 指令选择、寄存器分配、调度器 |
| C++ 前端 | `[cpp]` | 10 | OOP、模板、RTTI、异常、constexpr |
| DWARF5 调试 | `[dwarf5]` | 7 | DWARF 5 调试信息生成 |
| 调试信息 | `[debug]` | 4 | PDB、源码映射、调试发射器 |
| 集成测试 | `[integration]` | 52 | 完整管道/跨语言互操作/性能压力 |
| 基准测试 | `[benchmark]` | 18 | 微基准(词法/语法/语义/lowering)/宏基准(扩展/OOP/管道) |

## 10.2 .ploy 测试详细分类

| 类别 | 标签 | 数量 | 覆盖内容 |
|------|------|------|---------|
| 词法分析 | `[ploy][lexer]` | 17 | 关键字(54)、标识符、数字、字符串、运算符 |
| 语法分析 | `[ploy][parser]` | 40 | LINK/IMPORT/EXPORT/FUNC/PIPELINE/STRUCT/CONFIG/NEW/METHOD/GET/SET/WITH/DELETE/EXTEND |
| 语义分析 | `[ploy][sema]` | 40 | 类型检查、作用域、版本验证、包发现、OOP互操作、错误检查 |
| IR 生成 | `[ploy][lowering]` | 29 | 函数/管道/链接/表达式/控制流/OOP互操作/DELETE/EXTEND |
| 集成测试 | `[ploy][integration]` | 20 | 完整管道端到端 |
| 诊断系统 | `[ploy][diagnostics]` | 6 | 错误代码、建议、溯源链、格式化 |
| 错误检查 | `[ploy][error]` | 10 | 参数数量不匹配、类型不匹配、错误代码验证 |
| 版本约束 | `[ploy][version]` | 5+ | 6 种版本运算符 |
| 选择性导入 | `[ploy][selective]` | 7 | 单/多符号、版本组合、别名 |
| CONFIG VENV | `[ploy][venv]` | 6 | 解析/验证/重复检测/无效语言 |
| 多包管理器 | `[ploy][pkgmgr]` | 17 | CONDA/UV/PIPENV/POETRY 解析、验证、集成 |

## 10.3 运行测试

```bash
# 运行所有单元测试
./unit_tests

# 运行 .ploy 测试
./unit_tests [ploy]

# 运行特定标签
./unit_tests [ploy][pkgmgr]    # 包管理器测试
./unit_tests [ploy][lexer]      # 词法分析测试
./unit_tests [ploy][sema]       # 语义分析测试

# 运行集成测试
./integration_tests [integration]
./integration_tests [compile]   # 仅编译管道测试
./integration_tests [interop]   # 仅互操作测试
./integration_tests [perf]      # 仅性能压力测试

# 运行基准测试
./benchmark_tests [benchmark]
./benchmark_tests [micro]       # 仅微基准测试
./benchmark_tests [macro]       # 仅宏基准测试

# 通过 CTest 档位运行基准测试（设置 POLYBENCH_MODE 环境变量）：
ctest -L fast                   # 快速 smoke 运行（1 次预热，少量迭代）
ctest -L full                   # 完整统计运行（5 次预热，大量迭代）
ctest -L benchmark              # 默认档位（3 次预热，适量迭代）
```

> **基准测试推荐构建类型**：`Release` 或 `RelWithDebInfo`。Debug 构建包含断言和 sanitiser 开销，会扭曲测量结果。

```bash
# 详细输出
./unit_tests [ploy] -r compact

# Windows 注意：需要 <nul 防止 pip 命令挂起
unit_tests.exe [ploy] -r compact 2>&1 <nul
integration_tests.exe [integration] -r compact 2>&1 <nul
benchmark_tests.exe [benchmark] -r compact 2>&1 <nul
```

## 10.4 示例程序

`tests/samples/` 目录包含 16 个分类示例目录，每个目录含 `.ploy`、`.cpp`、`.py`、`.rs`、`.java` 和/或 `.cs` 源文件：

```
tests/samples/
├── README.md                           # 示例总览
├── 01_basic_linking/                   # 基本 LINK + CALL 互操作
├── 02_type_mapping/                    # 结构体和容器类型映射
├── 03_pipeline/                        # 多阶段 PIPELINE
├── 04_package_import/                  # IMPORT 与版本约束 + CONFIG
├── 05_class_instantiation/             # NEW/METHOD 跨语言 OOP
├── 06_attribute_access/                # GET/SET 属性访问
├── 07_resource_management/             # WITH 资源管理
├── 08_delete_extend/                   # DELETE/EXTEND 对象生命周期
├── 09_mixed_pipeline/                  # 组合 ML 管道(C++/Python/Rust)
├── 10_error_handling/                  # 错误场景和诊断
├── 11_java_interop/                    # Java 互操作（NEW/METHOD）
├── 12_dotnet_interop/                  # .NET 互操作（NEW/METHOD）
├── 13_generic_containers/              # 泛型容器互操作
├── 14_async_pipeline/                  # 异步多阶段信号处理
├── 15_full_stack/                      # 五语言全栈
└── 16_config_and_venv/                 # 环境配置、包版本
```

## 10.5 集成测试

`tests/integration/` 目录包含 52 个集成测试，分为 3 个类别：

```
tests/integration/
├── compile_tests/
│   └── compile_pipeline_test.cpp       # 17 个测试：完整编译管道
├── interop_tests/
│   └── interop_test.cpp                # 14 个测试：跨语言互操作
└── performance/
    └── perf_test.cpp                   # 14 个测试：性能压力
```

### 集成测试类别

| 类别 | 标签 | 测试数 | 覆盖内容 |
|------|------|--------|---------|
| 编译管道 | `[integration][compile]` | 17 | LINK+CALL、STRUCT、PIPELINE、IF/ELSE/WHILE/FOR、NEW/METHOD、GET/SET、WITH、DELETE、EXTEND、MATCH、ML管道、多函数PIPELINE |
| 跨语言互操作 | `[integration][interop]` | 14 | LINK链、NEW创建、METHOD链、生命周期(NEW→METHOD→DELETE)、多语言对象、GET/SET、WITH/嵌套WITH、EXTEND、组合OOP、三语言 |
| 性能压力 | `[integration][perf]` | 14 | 50/100函数、10/20层嵌套、50/100 CALL、20/50阶段管道、复杂混合程序、词法/语法吞吐 |

## 10.6 基准测试

`tests/benchmarks/` 目录包含 18 个基准测试，分为 2 个类别：

```
tests/benchmarks/
├── micro/
│   └── micro_bench.cpp                 # 8 个测试：逐阶段基准
└── macro/
    └── macro_bench.cpp                 # 10 个测试：全管道吞吐量
```

### 微基准测试

测量各编译器阶段的吞吐量，包含预热和统计报告：

| 测试 | 说明 |
|------|------|
| 词法分析 小/中/大 | 3 种程序规模的 Token 吞吐量 |
| 语法分析 小/中/大 | AST 构建吞吐量 |
| 语义分析 中 | 语义分析吞吐量 |
| Lowering 中 | IR 生成吞吐量 |

### 宏基准测试

测量完整管道 (lex → parse → sema → lower → IR print) 性能：

| 测试 | 说明 |
|------|------|
| Pipeline 10/50/100/200 函数 | 函数数量增长下的吞吐量扩展 |
| 扩展线性检查 | 验证次二次扩展 (2x 输入 < 5x 时间) |
| OOP 10/25 类 | EXTEND + NEW + METHOD + DELETE 性能 |
| Pipeline 10×5 / 20×10 | 多阶段管道，每阶段含多个 CALL |
| IR 大小增长 | 验证 IR 输出大小随程序大小线性增长 |

---

## 10.7 CI 质量闸门

CI 流水线（`.github/workflows/ci.yml`）在每次 push 和 PR 时强制执行以下质量闸门：

| 闸门 | 工具 | 说明 |
|------|------|------|
| **文档检查** | `docs_lint.py` / `docs_sync_check.py --scope core` | 路径引用、核心双语文档同步（`USER_GUIDE` / API）、标题结构、版本一致性 |
| **格式检查** | `clang-format-17` | 强制所有 C/C++ 源文件遵循 `.clang-format` 风格 |
| **静态分析** | `clang-tidy-17` | 覆盖 `common/middle/frontends/backends/runtime/tools` 下全部项目 `.cpp` 源文件（不再限制前 50 个） |
| **消毒器** | ASan + UBSan | 在非 benchmark 测试集上运行 AddressSanitizer 和 UndefinedBehaviorSanitizer（`-LE benchmark`） |
| **代码覆盖率** | lcov / gcov | 基于非 benchmark 测试集收集行覆盖率；报告作为 CI 产物上传 |
| **基准冒烟测试** | Catch2 `[fast]` | 以 fast 模式运行基准测试，捕获性能回退 |

### CMake 质量选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `POLYGLOT_ENABLE_ASAN` | `OFF` | 启用 AddressSanitizer（仅 GCC/Clang） |
| `POLYGLOT_ENABLE_UBSAN` | `OFF` | 启用 UndefinedBehaviorSanitizer（仅 GCC/Clang） |
| `POLYGLOT_ENABLE_COVERAGE` | `OFF` | 启用代码覆盖率（gcov/llvm-cov，仅 GCC/Clang） |

### 本地运行质量检查

```bash
# 格式检查（dry-run，需要 clang-format 17+）
find common middle frontends backends runtime tools \
    -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.c' \) \
    -not -path '*/deps/*' -not -path '*/.cache/*' -print0 \
    | xargs -0 clang-format --dry-run --Werror --style=file

# 文档同步闸门（核心双语文档）
python scripts/docs_sync_check.py --ci --scope core

# 消毒器构建
cmake -B build-san -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DPOLYGLOT_ENABLE_ASAN=ON \
  -DPOLYGLOT_ENABLE_UBSAN=ON \
  -DBUILD_SHARED_LIBS=OFF
cmake --build build-san
cd build-san && ctest --output-on-failure -LE benchmark

# 覆盖率构建
cmake -B build-cov -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DPOLYGLOT_ENABLE_COVERAGE=ON \
  -DBUILD_SHARED_LIBS=OFF
cmake --build build-cov
cd build-cov && ctest --output-on-failure -LE benchmark
lcov --capture --directory . --output-file coverage.info
```

---

# 11. 构建与集成

## 11.1 CMake 构建目标

| 目标 | 类型 | 主要依赖 |
|------|------|---------|
| `polyglot_common` | 静态库 | fmt, nlohmann_json (7 编译单元，含 dwarf_builder) |
| `frontend_common` | 静态库 | polyglot_common |
| `frontend_cpp` | 静态库 | frontend_common (5 编译单元: lexer/parser/sema/lowering/constexpr) |
| `frontend_python` | 静态库 | frontend_common (4 编译单元) |
| `frontend_rust` | 静态库 | frontend_common (4 编译单元) |
| `frontend_java` | 静态库 | frontend_common (4 编译单元: lexer/parser/sema/lowering) |
| `frontend_dotnet` | 静态库 | frontend_common (4 编译单元: lexer/parser/sema/lowering) |
| `frontend_ploy` | 静态库 | frontend_common, middle_ir (6 编译单元) |
| `middle_ir` | 静态库 | polyglot_common (15 编译单元) |
| `backend_x86_64` | 静态库 | polyglot_common (7 编译单元) |
| `backend_arm64` | 静态库 | polyglot_common (6 编译单元) |
| `backend_wasm` | 静态库 | polyglot_common, middle_ir (1 编译单元) |
| `runtime` | 静态库 | — (23 编译单元，含 java_rt/dotnet_rt/object_lifecycle) |
| `linker_lib` | 对象库 | polyglot_common, frontend_ploy |
| `polyc` | 可执行文件 | 所有前端 + 后端 + IR + 运行时 |
| `polyld` | 可执行文件 | polyglot_common, frontend_ploy |
| `polyasm` | 可执行文件 | 后端 + IR |
| `polyopt` | 可执行文件 | middle_ir + 后端 |
| `polyrt` | 可执行文件 | runtime |
| `polybench` | 可执行文件 | 全部 |
| `polyui` | 可执行文件 | 全部前端 + Qt6::Widgets（可选，需要 Qt） |
| `unit_tests` | 可执行文件 | 全部 + Catch2 |
| `integration_tests` | 可执行文件 | 全部 + Catch2 |
| `benchmark_tests` | 可执行文件 | 全部 + Catch2 |

## 11.2 依赖管理

依赖通过 `Dependencies.cmake` 使用 `FetchContent` 自动拉取：

| 依赖 | 用途 |
|------|------|
| fmt | 格式化输出 |
| nlohmann_json | JSON 处理 |
| Catch2 | 单元测试框架 |
| mimalloc | 高性能内存分配 |

## 11.3 已知问题与修复

| 问题 | 原因 | 修复 |
|------|------|------|
| x86/x64 链接器不匹配 | VsDevCmd 默认 x86 | 使用 `-arch=amd64` |
| 结构体字面量解析歧义 | `Ident {` 可能是结构体或块 | LexerBase `SaveState()/RestoreState()` 前瞻 |
| sema 类型名大小写 | `INT` vs `int` vs `i32` | `ResolveType` 支持多种大小写变体 |
| 测试 pip 命令挂起 | Windows stdin 等待 | 测试命令使用 `<nul` 重定向 |

---

# 12. 开发指南

## 12.1 添加新的 .ploy 关键字

1. 在 `ploy_lexer.h` 的 `TokenKind` 枚举中添加新 token
2. 在 `lexer.cpp` 的关键字映射表中添加字符串 → token 映射
3. 在 `parser.cpp` 中添加对应的解析逻辑
4. 在 `sema.cpp` 中添加语义检查
5. 在 `lowering.cpp` 中添加 IR 生成
6. 在 `ploy_test.cpp` 中添加测试（词法 + 语法 + 语义 + IR + 集成）

## 12.2 添加新的包管理器

1. 在 `ploy_ast.h` 的 `VenvConfigDecl::ManagerKind` 枚举中添加新值
2. 在 `lexer.cpp` 中添加对应关键字
3. 在 `parser.cpp` 的 `ParseConfigDecl` 中添加新分支
4. 在 `ploy_sema.h` 中添加 `DiscoverPythonPackagesViaXxx()` 方法声明
5. 在 `sema.cpp` 中实现发现逻辑，在 `DiscoverPythonPackages` 中添加 dispatch
6. 添加测试用例

## 12.3 代码风格

- **C++20** 标准
- 英文代码注释
- 使用 `namespace polyglot::ploy` (或 `polyglot::python` 等)
- 类名: `PascalCase`
- 方法名: `PascalCase` (如 `ParseFuncDecl`)
- 变量名: `snake_case`
- 成员变量: `trailing_underscore_`
- 使用 `core::SourceLoc` 定位错误（注意：使用 `file` 字段而非 `filename`）
- 使用 `core::Type` 类型系统（注意：`core::Type::Any()` 而非 `core::Type::Function`）

## 12.4 文档规范

- 所有文档均提供中英双语版本
- 中文文件名后缀 `_zh`，英文无后缀
- 文档存放在 `docs/realization/` 目录

---

# 13. 插件系统

PolyglotCompiler 支持通过稳定的 **C ABI** 接口实现动态插件扩展。插件为共享库，在加载时向宿主注册自身，可扩展编译器或 IDE 的语言前端、优化 Pass、后端、Linter、格式化器、代码操作、语法主题等功能。

## 13.1 架构

```
┌─────────────────────────────────────────────────────────┐
│               PolyglotCompiler 宿主                      │
│  ┌────────────────────┐  ┌────────────────┐  ┌─────────┐│
│  │ PluginManager      │  │ Host Services  │  │ polyui  ││
│  │ (发现、加载、卸载) │  │ (日志、诊断、  │  │ (设置   ││
│  │                    │  │  设置、打开文件)│  │  界面)  ││
│  └────────┬───────────┘  └───────┬────────┘  └─────────┘│
│           │                      │                      │
└───────────┼──────────────────────┼──────────────────────┘
            │                      │     C ABI 边界
      ┌─────┴──────────────────────┴─────┐
      │  插件 (.so/.dll/.dylib)          │
      │  polyplug_<name>                 │
      │  polyglot_plugin_info()          │
      │  polyglot_plugin_init()          │
      │  polyglot_plugin_shutdown()      │
      └──────────────────────────────────┘
```

- **C ABI**：所有导出函数使用 `extern "C"` 链接，确保跨编译器和平台的二进制兼容性。
- **命名约定**：插件共享库必须命名为 `polyplug_<name>`（如 `polyplug_myformatter.so`）。
- **发现机制**：`PluginManager` 扫描搜索路径中匹配的文件，通过 `dlopen` / `LoadLibrary` 加载。

## 13.2 能力标志

每个插件在 `PolyglotPluginInfo` 中声明能力位掩码：

| 标志 | 值 | 说明 |
|------|------|------|
| `POLYGLOT_CAP_LANGUAGE` | `1 << 0` | 语言前端 |
| `POLYGLOT_CAP_OPTIMIZER` | `1 << 1` | 优化 Pass |
| `POLYGLOT_CAP_BACKEND` | `1 << 2` | 代码生成后端 |
| `POLYGLOT_CAP_TOOL` | `1 << 3` | CLI 工具 / 管道阶段 |
| `POLYGLOT_CAP_UI_PANEL` | `1 << 4` | IDE 面板 / 停靠窗口 |
| `POLYGLOT_CAP_SYNTAX_THEME` | `1 << 5` | 语法高亮主题 |
| `POLYGLOT_CAP_FILE_TYPE` | `1 << 6` | 文件类型注册 |
| `POLYGLOT_CAP_CODE_ACTION` | `1 << 7` | 快速修复 / 重构操作 |
| `POLYGLOT_CAP_FORMATTER` | `1 << 8` | 代码格式化器 |
| `POLYGLOT_CAP_LINTER` | `1 << 9` | 代码检查器 |
| `POLYGLOT_CAP_DEBUGGER` | `1 << 10` | 调试器集成 |

## 13.3 必须导出的函数

每个插件必须导出以下三个函数：

```c
// 返回静态插件元数据（名称、版本、作者、能力）。
PolyglotPluginInfo polyglot_plugin_info(void);

// 初始化插件。宿主传入 PolyglotHostServices 结构体，
// 包含日志、诊断、设置等函数指针。
// 返回 0 表示成功，非零表示失败。
int polyglot_plugin_init(PolyglotHostServices services);

// 卸载前清理资源。
void polyglot_plugin_shutdown(void);
```

## 13.4 可选导出函数

插件可根据声明的能力选择性导出提供者函数：

```c
// 语言前端 — 返回 PolyglotLanguageProvider 结构体。
PolyglotLanguageProvider polyglot_plugin_get_language(void);

// 优化 Pass — 返回 PolyglotOptimizerPass 结构体。
PolyglotOptimizerPass polyglot_plugin_get_optimizer(void);

// 代码操作 — 返回 PolyglotCodeAction 结构体。
PolyglotCodeAction polyglot_plugin_get_code_action(void);

// 格式化器 — 返回 PolyglotFormatter 结构体。
PolyglotFormatter polyglot_plugin_get_formatter(void);

// 检查器 — 返回 PolyglotLinter 结构体。
PolyglotLinter polyglot_plugin_get_linter(void);
```

## 13.5 宿主服务

宿主通过 `PolyglotHostServices` 向插件提供以下服务：

| 服务 | 签名 | 用途 |
|------|------|------|
| `log` | `void (*)(void*, int, const char*)` | 按严重级别记录日志 |
| `emit_diagnostic` | `void (*)(void*, const PolyglotDiagnostic*)` | 发出编译诊断信息 |
| `get_setting` | `int (*)(void*, const char*, char*, int)` | 按键读取宿主设置 |
| `set_setting` | `void (*)(void*, const char*, const char*)` | 写入宿主设置 |
| `open_file` | `void (*)(void*, const char*, int)` | 请求 IDE 打开文件到指定行 |

## 13.6 插件文件位置

插件从以下搜索路径发现：

| 平台 | 系统路径 | 用户路径 |
|------|---------|----------|
| **macOS** | `<app>/plugins/` | `~/Library/Application Support/PolyglotCompiler/plugins/` |
| **Linux** | `<app>/plugins/` | `~/.local/share/PolyglotCompiler/plugins/` |
| **Windows** | `<app>\plugins\` | `%APPDATA%\PolyglotCompiler\plugins\` |

也可以在 `polyui` 的 **设置 → 插件** 页面手动加载插件。

## 13.7 快速示例（C 语言）

```c
#include "plugins/plugin_api.h"

static PolyglotHostServices host;

PolyglotPluginInfo polyglot_plugin_info(void) {
    PolyglotPluginInfo info = {0};
    info.api_version    = POLYGLOT_PLUGIN_API_VERSION;
    info.name           = "我的检查器";
    info.version        = "1.0.0";
    info.author         = "开发者";
    info.description    = "一个示例检查器插件";
    info.capabilities   = POLYGLOT_CAP_LINTER;
    return info;
}

int polyglot_plugin_init(PolyglotHostServices services) {
    host = services;
    host.log(host.context, 0, "检查器插件已加载");
    return 0;
}

void polyglot_plugin_shutdown(void) {
    host.log(host.context, 0, "检查器插件已卸载");
}
```

完整规范请参阅 [`docs/specs/plugin_specification_zh.md`](specs/plugin_specification_zh.md)。

---

# 14. 附录

## 14.1 术语表

| 术语 | 英文 | 解释 |
|------|------|------|
| AST | Abstract Syntax Tree | 抽象语法树 |
| IR | Intermediate Representation | 中间表示 |
| SSA | Static Single Assignment | 静态单赋值 |
| CFG | Control Flow Graph | 控制流图 |
| DCE | Dead Code Elimination | 死代码消除 |
| CSE | Common Subexpression Elimination | 公共子表达式消除 |
| GVN | Global Value Numbering | 全局值编号 |
| RTTI | Run-Time Type Information | 运行时类型信息 |
| FFI | Foreign Function Interface | 外部函数接口 |
| PGO | Profile-Guided Optimization | 基于性能剖析的优化 |
| LTO | Link-Time Optimization | 链接时优化 |
| LICM | Loop-Invariant Code Motion | 循环不变量外提 |
| DSL | Domain-Specific Language | 领域特定语言 |
| POBJ | Polyglot Object | PolyglotCompiler 自定义目标文件格式 |

## 14.2 .ploy 关键字速查表

| 关键字 | 用途 | 示例 |
|--------|------|------|
| `LINK` | 跨语言函数链接 | `LINK(cpp, python, f, g);` |
| `IMPORT` | 模块/包导入 | `IMPORT python PACKAGE numpy >= 1.20;` |
| `EXPORT` | 符号导出 | `EXPORT func AS "name";` |
| `MAP_TYPE` | 类型映射 | `MAP_TYPE(cpp::int, python::int);` |
| `PIPELINE` | 多阶段管道 | `PIPELINE name { ... }` |
| `FUNC` | 函数声明 | `FUNC f(x: INT) -> INT { ... }` |
| `LET` / `VAR` | 变量声明 | `LET x = 42;` / `VAR y = 0;` |
| `CALL` | 跨语言调用 | `CALL(python, np::mean, data)` |
| `STRUCT` | 结构体定义 | `STRUCT Point { x: f64, y: f64 }` |
| `MAP_FUNC` | 映射函数 | `MAP_FUNC convert(x: INT) -> FLOAT { ... }` |
| `CONVERT` | 类型转换 | `CONVERT(value, FLOAT)` |
| `NEW` | 跨语言类实例化 | `NEW(python, torch::nn::Linear, 784, 10)` |
| `METHOD` | 跨语言方法调用 | `METHOD(python, model, forward, data)` |
| `GET` | 跨语言属性访问 | `GET(python, model, weight)` |
| `SET` | 跨语言属性赋值 | `SET(python, model, training, FALSE)` |
| `WITH` | 自动资源管理 | `WITH(python, f) AS handle { ... }` |
| `DELETE` | 跨语言对象销毁 | `DELETE(python, obj)` |
| `EXTEND` | 跨语言类继承扩展 | `EXTEND(python, Base) AS Derived { ... }` |
| `CONFIG` | 环境配置 | `CONFIG CONDA "env";` |
| `VENV` | pip/venv 环境 | `CONFIG VENV python "/path";` |
| `CONDA` | Conda 环境 | `CONFIG CONDA "env_name";` |
| `UV` | uv 管理环境 | `CONFIG UV python "/path";` |
| `PIPENV` | Pipenv 项目 | `CONFIG PIPENV "project_path";` |
| `POETRY` | Poetry 项目 | `CONFIG POETRY "project_path";` |

## 14.3 版本运算符

| 运算符 | 含义 | 示例 |
|--------|------|------|
| `>=` | 大于等于 | `>= 1.20` |
| `<=` | 小于等于 | `<= 2.0` |
| `==` | 精确等于 | `== 1.10.0` |
| `>` | 严格大于 | `> 1.0` |
| `<` | 严格小于 | `< 3.0` |
| `~=` | 兼容版本 (PEP 440) | `~= 1.20` → `>= 1.20, < 2.0` |

## 14.4 参考资料

- "Compilers: Principles, Techniques, and Tools" (龙书)
- "Engineering a Compiler" (鲸书)
- LLVM: https://llvm.org/
- Rust Compiler: https://github.com/rust-lang/rust
- PEP 440: https://peps.python.org/pep-0440/

## 14.5 发布打包

PolyglotCompiler 提供各平台的打包脚本用于构建发布版本：

| 平台 | 脚本 | 输出 |
|------|------|------|
| Windows | `scripts/package_windows.ps1` | 免安装 `.zip` + NSIS 安装程序 `.exe` |
| Linux | `scripts/package_linux.sh` | 免安装 `.tar.gz` |
| macOS | `scripts/package_macos.sh` | 免安装 `.tar.gz`（含 `.app` 包） |

```bash
# Windows (PowerShell)
.\scripts\package_windows.ps1

# Linux
./scripts/package_linux.sh

# macOS
./scripts/package_macos.sh
```

详细说明、前置要求和版本管理请参见 `docs/specs/release_packaging_zh.md`。

## 14.6 更新日志

### v1.0.6 (2026-03-19)

**CI 质量闸门（2026-03-17-8）**
- ✅ 新增 `.clang-tidy` 配置：bugprone、cppcoreguidelines、modernize、performance、readability 检查，含项目特定排除项
- ✅ 新增 CMake 选项：`POLYGLOT_ENABLE_ASAN`、`POLYGLOT_ENABLE_UBSAN`、`POLYGLOT_ENABLE_COVERAGE`，用于消毒器和覆盖率构建
- ✅ CI `format-check` 任务：通过 `clang-format-17` 强制所有 C/C++ 源文件遵循 `.clang-format` 风格
- ✅ CI `clang-tidy` 任务：使用 `clang-tidy-17` 扫描全部项目 `.cpp` 源文件（不再限制前 50 个）
- ✅ CI `sanitizers` 任务：在 ASan + UBSan 下运行非 benchmark 测试集（`ctest -LE benchmark`），降低噪声
- ✅ CI `coverage` 任务：通过 lcov/gcov 从非 benchmark 测试集收集覆盖率，上传过滤后的报告作为产物
- ✅ CI `benchmark-smoke` 任务：以 fast 模式运行基准测试，捕获性能回退
- ✅ CI 并发控制：取消同一分支/PR 的进行中运行
- ✅ 平台构建现在依赖 `format-check` 闸门先通过
- ✅ 文档更新：README、USER_GUIDE（中英文）添加质量闸门表格和本地使用说明

**跨语言链接闭环（2026-03-19-2）**
- ✅ `polyc` 现在在调用 `ResolveLinks()` 之前自动从 LINK 声明和 CALL 描述符合成 `CrossLangSymbol` 条目，解决了链接器有描述符但无符号表可解析的问题
- ✅ 当 sema 已知函数签名可用时，自动填充参数描述符
- ✅ 编译模型文档已更新，反映自动化符号注册流程

**收紧降级路径（2026-03-19-3）**
- ✅ `lowering.cpp`：无签名信息的 LINK 桩函数在严格模式（默认）下现在报错，而非静默回退到单个 opaque i64 参数
- ✅ `driver.cpp`：最小 main 合成在严格模式下被阻止；仅在宽松模式 + `--force` 下允许，并输出明确的 DEGRADED BUILD 警告
- ✅ `driver.cpp`：优化后 IR 验证失败在严格模式下现在是硬错误；之前仅在 verbose 模式下静默记录
- ✅ 后端空 section 桩注入已限制在 `--force` + 非严格模式下；添加了一致的 DEGRADED BUILD 消息

**管线重构（2026-03-19-4）**
- ✅ 将 `PassManager` 从内部 `.cpp` 类提升为公共头文件 `middle/include/passes/pass_manager.h`
- ✅ `PassManager` 支持 O0/O1/O2/O3 级别，具有命名 `PassEntry` 阶段、自定义 pass 注入和逐函数 verbose 日志
- ✅ `driver.cpp` 优化管线（约 40 行内联 pass 调用）替换为 `PassManager::Build()` + `RunOnModule()` — 仅 5 行调用
- ✅ Pass 管线现在可检查、可扩展，CLI、UI、测试和插件均可复用

**测试解耦（2026-03-19-5）**
- ✅ 将单体 `unit_tests` 拆分为 14 个按模块的测试二进制（`test_core`、`test_plugins`、`test_frontend_python`、`test_frontend_cpp`、`test_frontend_rust`、`test_frontend_ploy`、`test_frontend_java`、`test_frontend_dotnet`、`test_frontend_common`、`test_middle`、`test_backends`、`test_runtime`、`test_linker`、`test_e2e`）
- ✅ 每个模块二进制仅链接实际需要的库，减少链接开销并隔离 dylib 故障
- ✅ CTest 现有 19 个测试入口（14 个按模块 + 合并 `unit_tests` + `integration_tests` + 3 个基准目标），带标签（`unit`、`frontend`、`middle`、`backend`、`runtime`、`linker`、`e2e`、`benchmark`）
- ✅ 保留合并 `unit_tests` 目标以保持向后兼容
- ✅ 顶层 `CMakeLists.txt` 添加 `enable_testing()` 使按模块测试可从构建根目录发现

**模块边界修正（2026-03-19-6）**
- ✅ 将 `backends/common/` 源码（debug_info、debug_emitter、dwarf_builder、object_file）从 `polyglot_common` 移出，创建独立的 `backend_common` 库
- ✅ `backend_x86_64`、`backend_arm64`、`backend_wasm` 现在依赖 `backend_common` 而非直接依赖 `polyglot_common` 编入的后端源码
- ✅ 消除了 common → backends 的层级反转（正确方向：backends → common）
- ✅ `polyld` CLI 逻辑合并：将 `linker.cpp` 中 170 行重复的 `main` 函数（含 `--ploy-desc`、`--aux-dir`、`--allow-adhoc-link` 等选项）合并到 `main.cpp`，删除 `linker.cpp` 中的重复入口

### v1.0.5 (2026-03-17)

**文档单源化与自动校验（2026-03-17-7）**
- ✅ 修复路径引用：README.md、USER_GUIDE.md、USER_GUIDE_zh.md、`setup_qt.sh`、`setup_qt.ps1` 现在正确引用 `tools/ui/setup_qt.*`（原为 `scripts/setup_qt.*`）
- ✅ 修复死链接：USER_GUIDE 中 `plugin_specification.md` / `plugin_specification_zh.md` 链接现在正确解析
- ✅ 修复 `language_spec.md` / `language_spec_zh.md`：运行时桥接头文件路径更新为 `runtime/include/libs/*.h`
- ✅ 修复 `project_tutorial.md` / `project_tutorial_zh.md`：驱动程序路径更新为 `tools/polyc/src/driver.cpp`
- ✅ 新增 `scripts/docs_lint.py`：全面的文档校验工具，包含 9 类检查：
  - DL001：路径引用验证（反引号引用的路径必须存在于仓库中）
  - DL002/DL003：双语配对 — 每个 `*.md` 必须有 `*_zh.md` 对应文件
  - DL004：标题结构同步 — 中英文标题各级数量必须一致
  - DL005：死链接检测 — 相对 Markdown 链接必须可解析
  - DL006：版本一致性 — 各文档中的版本字符串必须一致
  - DL007：孤立文档检测 — 未被 README 或 USER_GUIDE 引用的文档
  - DL008：已发布文档中的 TODO/FIXME 标记
  - DL009：占位文本检测
- ✅ 新增 `scripts/docs_generate.py`：单源文档生成器，使用模板标记（`<!-- BEGIN:section_name -->` / `<!-- END:section_name -->`）；支持 `--apply` / `--check` 模式；从 `docs/_variables.json` 更新测试徽章、Qt 路径、依赖表、版本页脚
- ✅ 新增 `scripts/docs_sync_check.py`：双语同步检查器，比较中英文文档的标题结构和内容长度差异
- ✅ 新增 `scripts/check_include_deps.py`：头文件依赖层级约束检查器（middle→common/ir、backends→frontends、frontends→backends）
- ✅ 新增 `docs/_variables.json`：文档变量单一数据源，包含版本号、测试计数、路径、依赖信息、工具名称 — 消除手动多文件更新
- ✅ 在 README.md 中插入模板标记（`test_badge`、`qt_setup_en`、`dependencies_table`、`version_footer_en`）和 USER_GUIDE 页脚（`version_footer_en`、`version_footer_zh`）
- ✅ CI 集成：在 `.github/workflows/ci.yml` 中添加 `docs-lint` 作业，在每次 push/PR 时运行 `docs_lint.py --ci`、`docs_sync_check.py --ci` 和 `docs_generate.py --check`
- ✅ 测试徽章从 813 → 808 更新（准确计数）；版本页脚更新为 v1.0.4 / 2026-03-17
- ✅ 808 单元测试全部通过（34085 断言）；51/52 集成测试通过

### v1.0.4 (2026-03-17)

**插件系统与 UI 可扩展性（2026-03-17-6）**
- ✅ 插件宿主回调完整实现：`emit_diagnostic` 将诊断转发到 IDE 输出面板并触发 `DIAGNOSTIC` 事件；`open_file` 委托给已注册的 `OpenFileCallback`（连接到 IDE 标签管理器）；`register_file_type` 填充中央文件类型注册表
- ✅ 事件订阅系统：插件可通过 `subscribe_event` / `unsubscribe_event` 宿主服务订阅 8 种事件类型（`FILE_OPENED`、`FILE_SAVED`、`FILE_CLOSED`、`BUILD_STARTED`、`BUILD_FINISHED`、`DIAGNOSTIC`、`WORKSPACE_CHANGED`、`THEME_CHANGED`）
- ✅ `PluginManager::FireEvent()`：线程安全分发 — 在锁内快照订阅者，在锁外投递事件以避免重入死锁
- ✅ 文件类型注册表：`RegisterFileType()` / `GetLanguageForExtension()` / `GetRegisteredFileTypes()`
- ✅ 菜单贡献系统：`RegisterMenuItem()` / `UnregisterMenuItem()` / `GetMenuContributions()` / `ExecuteMenuAction()`
- ✅ 新增 `ActionManager` 类：从 MainWindow 提取的集中化动作/快捷键管理
- ✅ 新增 `PanelManager` 类：管理底部面板标签页，支持插件贡献面板
- ✅ MainWindow 重构：面板切换/显示逻辑委托给 `PanelManager`；快捷键管理委托给 `ActionManager`
- ✅ 12 个新单元测试：事件订阅/取消/分发、文件类型注册、菜单贡献等
- ✅ 测试计数：796 → 808 测试用例；断言：34056 → 34085 — 全部通过

### v1.0.3 (2026-03-17)

**测试质量提升（2026-03-17-5）**
- ✅ 将 `lto_test.cpp` 中的 `REQUIRE(true)` 替换为优化器统计行为断言（空模块所有统计为 0，模块经历所有优化阶段后仍存在）
- ✅ 将 `gc_algorithms_test.cpp` 中的 `REQUIRE(true)` 替换为 GC 统计验证：多周期测试 `collections >= 10`、`total_allocations >= 500`；增量回收 `total_allocations >= 200`
- ✅ 将注释掉的性能基准测试替换为真实吞吐量断言（`total_allocations >= 10000`、`collections >= 1`）
- ✅ 将 `java_test.cpp` 和 `dotnet_test.cpp` 中的 `SUCCEED()` 替换为语义分析行为检查（验证诊断信息为类型映射相关，而非崩溃）
- ✅ 将 `threading_services_test.cpp` 中的 `REQUIRE(true)` 替换为基于原子变量的屏障阶段跟踪和大工作负载精确求和验证
- ✅ 增强 Java/DotNet 枚举和结构体降级测试，添加正向诊断状态断言
- ✅ 新增 `compilation_behavior_test.cpp`：15 个行为级和失败路径测试用例：
  - C++ 单函数/多函数 IR 输出正确性
  - C++ 条件语句产生多个基本块
  - x86_64 汇编包含函数标签和 `ret` 指令
  - x86_64 目标代码包含 `.text` 节且有非零字节和函数符号
  - ARM64 汇编输出包含函数标签
  - IR 验证通过格式正确的 IR；检测到格式错误（空函数）的 IR
  - Ploy `LINK` 产生正确的跨语言调用描述符
  - Ploy `FUNC` 产生具名 IR 函数
  - 失败路径：解析错误诊断、未定义变量语义错误、降级无效代码返回失败
- ✅ 测试用例数：781 → 796；断言数：3985 → 34056 — 全部通过

### v1.0.2 (2026-03-17)

**包发现重构**
- ✅ 新增 `CommandResult` 结构体，提供结构化的命令执行结果（标准输出、退出码、超时标志）
- ✅ `ICommandRunner` 新增 `RunWithResult()` 接口，`Run()` 保留并委托至 `RunWithResult()`
- ✅ `DefaultCommandRunner` 构造函数支持可配置默认超时（基于 `std::async` + `std::future::wait_for`）
- ✅ 新增 `PackageIndexer` 类——在语义分析之前运行的显式包索引阶段
- ✅ `PackageIndexer` 支持超时、重试、进度回调和统计信息收集
- ✅ `polyc` 驱动程序新增 Phase 2.5：根据 AST 导入声明扫描所需语言并运行包索引
- ✅ 新增 CLI 标志：`--no-package-index`（跳过索引）、`--pkg-timeout=<ms>`（设置超时）
- ✅ `PloySemaOptions::enable_package_discovery` 默认值改为 `false`，由预阶段缓存替代
- ✅ 新增 10+ 个测试用例覆盖 `CommandResult`、`PackageIndexer`、超时模拟及预阶段→语义分析流程

**构建系统模块化**
- ✅ 顶层 `CMakeLists.txt` 从约 690 行精简为约 80 行
- ✅ 新增 7 个子目录 `CMakeLists.txt`：`common/`、`frontends/`、`middle/`、`backends/`、`runtime/`、`tools/`、`tests/`
- ✅ 单元测试采用显式逐模块源文件列表（不再使用 `GLOB_RECURSE`）
- ✅ 集成测试和基准测试保留 `GLOB_RECURSE` 以便快速添加
- ✅ Qt6 检测与部署逻辑迁移至 `tools/CMakeLists.txt`
- ✅ 全部 781 个测试用例 / 3985 条断言通过

### v1.0.1 (2026-03-17)
- ✅ 两种显式编译模式：**严格**和**宽松**（`--strict` / `--permissive` CLI 标志）
- ✅ Release 构建默认启用严格模式（通过 `POLYC_DEFAULT_STRICT` 编译定义）
- ✅ 严格模式：语义分析将占位类型回退报告为错误而非警告
- ✅ 严格模式：IR 验证器拒绝包含未解析占位 I64 返回类型的函数
- ✅ 严格模式：降级阶段拒绝返回类型未知的跨语言调用
- ✅ 严格模式：禁止通过 `--force` 生成降级桩；`--strict` 与 `--force` 互斥
- ✅ 宽松模式：所有占位类型回退以警告形式可见（此前为静默处理）
- ✅ `ReportStrictDiag()` 辅助方法：根据模式将诊断路由为错误（严格）或警告（宽松）
- ✅ `IRType::is_placeholder` 标志区分真实 I64 与未解析回退 I64

### v1.0.0 (2026-03-15)
- ✅ 项目版本统一为 **1.0.0**（所有原 v5.x/v4.x 版本号重命名为 v0.5.x/v0.4.x）
- ✅ 三平台发布打包脚本（`scripts/package_windows.ps1`、`scripts/package_linux.sh`、`scripts/package_macos.sh`）
- ✅ Windows：免安装 ZIP 压缩包 + NSIS 安装程序（`scripts/installer.nsi`），支持 PATH 注册和开始菜单快捷方式
- ✅ Linux：免安装 `.tar.gz`，自动打包 Qt 库和启动包装脚本
- ✅ macOS：免安装 `.tar.gz`，集成 `macdeployqt` 生成 `.app` 包
- ✅ 打包文档（`docs/specs/release_packaging.md` / `_zh.md`）
- ✅ 版本号已更新至 CMakeLists.txt、所有工具源文件、README.md 和 USER_GUIDE（中英文）

### v0.5.5 (2026-03-15)
- ✅ 统一主题系统：通过 `ThemeManager` 单例管理 — 4 套内置配色方案（暗色、亮色、Monokai、Solarized Dark）；所有面板（主窗口、构建、调试、Git、设置）从统一来源应用样式
- ✅ 编译与运行（`Ctrl+R`）：基于 QProcess 的工作流，编译当前文件、定位输出二进制文件、启动并将 stdout/stderr 实时输出至日志面板
- ✅ 停止（`Ctrl+Shift+R`）：终止运行中的进程并取消活跃构建
- ✅ 调试面板：完整的调用栈帧解析，同时支持 lldb 和 GDB/MI 输出（模块、函数、文件、行号）；变量类型/值提取；监视表达式求值并实时更新结果
- ✅ 调试面板：监视右键菜单（删除 / 全部删除 / 求值）连接至 `OnRemoveWatch` 和 `OnEvaluateWatch`
- ✅ 调试面板：支持从设置中配置调试器路径和入口断点选项
- ✅ 自定义快捷键：在设置 → 键绑定页面中通过 `QKeySequenceEdit` 编辑；28 个默认操作；自定义快捷键通过 `QSettings` 持久化并在启动时加载
- ✅ 设置联动：CMake 路径、构建目录、调试器路径自动同步至 `BuildPanel` 和 `DebugPanel`
- ✅ 新增源文件 `theme_manager.cpp` / `theme_manager.h` 至 `tools/ui/common/`

### v0.5.4 (2026-03-11)
- ✅ 基于 Qt 的桌面 IDE（`polyui`）：语法高亮、实时诊断、文件浏览器、多标签编辑器、输出面板、括号匹配、暗色主题
- ✅ IDE 使用编译器前端分词器实现精确的、语言感知的高亮，支持全部 6 种语言
- ✅ IDE 快捷键：Ctrl+B 编译、Ctrl+Shift+B 分析、Ctrl+N/O/S/W 文件管理
- ✅ CMake Qt6/Qt5 自动发现：优先使用 `D:\Qt` 下的独立 Qt 安装，不再引用 Anaconda 自带的 Qt；可通过 `-DQT_ROOT=<path>` 覆盖
- ✅ 默认 `ninja` 构建现在生成所有可执行文件（polyc、polyld、polyasm、polyopt、polyrt、polybench、polyui）
- ✅ `polyui` 源码按平台分离：共享代码位于 `tools/ui/common/`，平台专用入口点位于 `tools/ui/windows/`、`tools/ui/linux/`、`tools/ui/macos/`；CMake 根据操作系统自动选择正确的 `main.cpp`
- ✅ macOS 支持：`MACOSX_BUNDLE` + `macdeployqt` 后编译部署；Linux 支持：`xcb` 平台默认值、`.desktop` 集成
- ✅ IDE 内置终端：嵌入式 Shell（PowerShell/bash/zsh），ANSI 颜色解析，命令历史，多实例，`` Ctrl+` `` 切换
- ✅ 清理旧目录 `tools/ui/ployui_windows/`、`tools/ui/src/`、`tools/ui/include/`
- ✅ 全项目文档审查与统计数据刷新 — 293 个源文件，91,457 行代码
- ✅ 测试增长：743 单元（原 734）+ 52 集成（原 50）+ 18 基准 = **813 总计**（原 802）
- ✅ Ploy 前端扩展至 6 个编译单元（新增 `command_runner.cpp`、`package_discovery_cache.cpp`）
- ✅ 更新所有文档（README、USER_GUIDE 中英文、教程）至最新统计数据

### v1.0.1 (2026-04-09)
- ✅ 在 `tools/polyc/src/compilation_pipeline.cpp` 落地 `.ploy` 六阶段编译主链实现
- ✅ 阶段拆分明确并以结构化数据传递：`frontend -> semantic db -> marshal plan -> bridge generation -> backend -> packaging`
- ✅ 新增管道编排执行器（`CompilationPipeline::RunAll()` 及逐阶段运行函数），并记录阶段耗时
- ✅ `polyc` 驱动已将 `.ploy` 主路径切换为 staged pipeline 执行
- ✅ staged 语义阶段接入 `PackageIndexer` + 共享 `PackageDiscoveryCache` 的包索引流程
- ✅ bridge generation 阶段接入 `PolyglotLinker`，并在后端发射前注入已解析桥接桩
- ✅ packaging 阶段支持确定性 `.pobj` 输出，并在 `link` 模式可选调用 `polyld` 完成链接
- ✅ `polyui` 的 Windows 可执行文件现通过 CMake 资源脚本（`tools/ui/windows/polyui.rc`）嵌入 `tools/ui/common/resources/icon.ico`，生成的 `polyui.exe` 默认带项目图标
- ✅ `polyui` 现已显式设置运行时标题栏/窗口图标：Windows 使用内嵌 `:/icons/icon.ico`，Linux/macOS 使用内嵌 `:/icons/icon.png`

### v0.5.2 (2026-02-22)
- ✅ 新增 `PloySemaOptions` 配置结构，支持 `enable_package_discovery` 开关
- ✅ `PloySema` 构造函数接收 options，保持默认行为向后兼容
- ✅ 会话级 `PackageDiscoveryCache` — 线程安全，键为 `language|manager|env_path`
- ✅ `ICommandRunner` 抽象 — 外部命令执行与 `_popen` 解耦；支持测试 Mock
- ✅ `DiscoverPackages` 缓存优先流程 — 命中→合并缓存结果；未命中→执行+存储
- ✅ 基准测试套件禁用包发现，隔离编译器本体性能
- ✅ Lowering 微基准计时修正 — 解析/语义分析排除在计时窗口外
- ✅ 基准测试 fast/full 档位通过 `POLYBENCH_MODE` 环境变量控制（`fast`/`full`/默认）
- ✅ CTest: `benchmark_fast` 和 `benchmark_full` 目标及标签
- ✅ 发现子系统单元测试：禁用无命令、重复分析仅一次、键隔离、缓存往返

### v0.5.1 (2026-02-22)
- ✅ 链接器强失败模式 — 未解析符号为硬错误；不再为未解析对生成占位存根
- ✅ 链接器主流程：存在跨语言条目但解析失败时直接报错终止
- ✅ `polyc` 和 `polyasm` 现支持 `--arch=wasm` WebAssembly 目标
- ✅ WASM 后端：通过名称→索引映射实现真实函数调用索引解析（移除硬编码索引）
- ✅ WASM 后端：alloca 降级为影子栈模型（可变 i32 全局变量，从 65536 向下增长）
- ✅ WASM 后端：不支持的 IR 指令发射 `unreachable` + 诊断错误（不再静默 NOP）
- ✅ `.ploy` 语义分析严格模式（`SetStrictMode(true)`）— 对无类型参数、`Any` 回退、缺少注释发出警告
- ✅ `.ploy` lowering 在 I64 回退前先查询语义符号表；使用 `KnownSignatures` 获取返回/参数类型
- ✅ 调试发射器：FDE 现包含正确的 CFA 指令（push rbp / mov rbp,rsp 帧建立）
- ✅ 调试发射器：PDB TPI 流发射指针类型的 LF_POINTER 类型记录
- ✅ Mach-O 目标文件：计算并写入每节 `reloff`/`nreloc`；发射 relocation_info 条目
- ✅ Rust 和 Python 前端 lowering：未知类型 → I64 回退时发出诊断警告
- ✅ Rust advanced_features_test.cpp：所有 `REQUIRE(true)` 替换为行为性解析 + AST 断言
- ✅ E2E 测试：新增 ARM64 目标代码发射测试和 WASM 多函数二进制 smoke 测试

### v0.5.0 (2026-02-22)
- ✅ 全面项目文档更新 — 所有文档刷新以反映当前状态
- ✅ 新增 WebAssembly (WASM) 后端 (`backends/wasm/`)
- ✅ 统一 DWARF 构建器编入 `polyglot_common` 库
- ✅ PDB 发射：MSF 块布局、TPI 类型记录 (LF_ARGLIST/LF_PROCEDURE)、RFC 4122 v4 GUID
- ✅ CFA 初始化含寄存器保存规则和 NOP 对齐
- ✅ ELF：e_machine 架构切换 (x86_64/ARM64)，完整 SHT_RELA 重定位节
- ✅ Mach-O：完整 LC_SEGMENT_64、LC_SYMTAB、nlist_64 符号表、段映射
- ✅ IR 解析器/打印器对称：`fadd/fsub/fmul/fdiv/frem` 解析 + global/const 声明
- ✅ 预处理器测试覆盖：18 个宏、指令、Token池 专用测试
- ✅ 调试测试 Windows 兼容：`TmpPath()` 使用 `std::filesystem::temp_directory_path()`
- ✅ 跨语言链接主链路完全连通：polyc → PolyglotLinker → 粘合代码 → 二进制
- ✅ 所有 6 个前端真实 IR lowering（无回退桩）
- ✅ E2E 测试启用：29 个端到端测试
- ✅ 链接器完整：COFF/PE、ELF、Mach-O 全部实现
- ✅ polyopt：读取 IR 文件并运行优化管道
- ✅ polybench：完整基准套件（编译 + E2E）
- ✅ polyrt：FFI 子命令、真实 GC/线程统计
- ✅ 教程文档新增 (`docs/tutorial/`)
- ✅ 16 个示例程序（原 12 个）
- ✅ 总计：813 个测试用例，3 个测试套件（743 单元 + 52 集成 + 18 基准）

### v0.4.3 (2026-02-20)
- ✅ 新增跨语言对象销毁 `DELETE` 关键字
- ✅ 新增跨语言类继承扩展 `EXTEND` 关键字
- ✅ 增强诊断基础设施：严重性级别、错误代码（1xxx-5xxx）、溯源链、建议
- ✅ 新增语义分析中的参数数量不匹配检查
- ✅ 新增语义分析中的类型不匹配检查
- ✅ 所有错误报告升级为结构化错误代码
- ✅ AST / 词法 / 语法 / 语义 / IR Lowering 全链路实现（DELETE/EXTEND）
- ✅ 36 个新测试用例，总计 207 测试用例、598 断言
- ✅ 关键字数量 52 → 54

### v0.4.2 (2026-02-20)
- ✅ 新增跨语言属性访问 `GET` 和属性赋值 `SET` 关键字
- ✅ 新增自动资源管理 `WITH` 关键字
- ✅ 新增带限定类型的类型注解支持
- ✅ 新增通过 `MAP_TYPE` 进行接口映射
- ✅ AST / 词法 / 语法 / 语义 / IR Lowering 全链路实现（GET/SET/WITH）
- ✅ 31 个新测试用例，总计 171 测试用例、523 断言
- ✅ 中英双语用户指南（USER_GUIDE.md / USER_GUIDE_zh.md）
- ✅ 关键字数量 49 → 52

### v0.4.1 (2026-02-20)
- ✅ 新增跨语言类实例化 `NEW` 和方法调用 `METHOD` 关键字
- ✅ AST / 词法 / 语法 / 语义 / IR Lowering 全链路实现
- ✅ 22 个新测试用例，总计 140 测试用例、443 断言
- ✅ 中英双语功能文档 (`class_instantiation.md` / `class_instantiation_zh.md`)
- ✅ 关键字数量 47 → 49

### v0.5.3 (2026-02-22)
- ✅ 解除 `common` ↔ `middle` 循环依赖 — 规范 IR 头文件移至 `middle/include/ir/`；`common/include/ir/` 保留转发垫片保持向后兼容
- ✅ 新增 CI include-lint 脚本（`scripts/check_include_deps.py`）强制层级约束：`middle/` 不可包含 `common/include/ir/`，后端不可包含前端
- ✅ 统一调试信息建模 — 新增 `common/include/debug/debug_info_adapter.h`，通过 `ConvertToBackendDebugInfo()` 桥接 `polyglot::debug`（丰富 DWARF 模型）与 `polyglot::backends`（扁平发射模型）
- ✅ 新增优化管线与开关矩阵文档（`docs/specs/optimization_pipeline.md` / `_zh.md`）
- ✅ 新增运行时 C ABI 参考文档（`docs/specs/runtime_abi.md` / `_zh.md`）
- ✅ 统一工具层命名空间：`polyc` 和 `polybench` 现使用 `namespace polyglot::tools`，与 `polyasm`/`polyopt`/`polyrt` 一致

### v0.4.0 (2026-02-19)
- ✅ 新增 .ploy 跨语言链接前端完整章节
- ✅ 多包管理器支持: CONFIG CONDA / UV / PIPENV / POETRY
- ✅ 版本约束验证（6 种运算符）
- ✅ 选择性导入
- ✅ 117+ 测试用例，361+ 断言
- ✅ 全面审查并重写完整指南（v0.3.0 → v0.4.0）
- ✅ 精确说明编译流程（PolyglotCompiler 自身前端编译，不依赖外部编译器）

### v0.3.0 (2026-02-01)
- ✅ 第 11-15 章（实现分析/测试/高级优化/成就总结）
- ✅ 33+ 优化 passes、4 种 GC 算法
- ✅ PGO/LTO/DWARF5 支持

### v0.2.0 (2026-01-29)
- ✅ 循环优化、GVN、constexpr、DWARF 5

### v0.1.0 (2026-01-15)
- ✅ 基础编译链：3 种前端、2 种后端、基础优化

---

<!-- BEGIN:version_footer_zh -->
*本文档由 PolyglotCompiler 团队维护*  
*最后更新: 2026-03-19*  
*文档版本: v1.0.0*
<!-- END:version_footer_zh -->