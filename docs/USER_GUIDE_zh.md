# PolyglotCompiler 用户指南

> 一个功能完整的多语言编译器项目  
> 支持 C++、Python、Rust、Java、C# (.NET)、JavaScript、Ruby、Go → x86_64/ARM64/WebAssembly  
> 含 .ploy 跨语言链接前端

**版本**: v1.2.0  
**最后更新**: 2026-04-28

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

- ✅ **多语言支持**: C++、Python、Rust、Java、.NET (C#)、JavaScript、Ruby、Go 的完整编译前端
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
│       ├── libs/           #   base.c, base_gc_bridge.cpp, python_rt.c, cpp_rt.c, rust_rt.c, java_rt.c, dotnet_rt.c, javascript_rt.c, ruby_rt.c, go_rt.c
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
│   ├── unit/               # 单元测试（Catch2 框架）— 909 个测试用例
│   │   └── frontends/ploy/ #   ploy_test.cpp（283 测试用例）
│   ├── samples/            # 示例程序（16 个分类目录，含 .ploy/.cpp/.py/.rs/.java/.cs）
│   ├── integration/        # 集成测试（编译管道/互操作/性能）— 92 个测试用例
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

### 关键字列表（56 个，自 Ploy 1.5.2 起大小写不敏感）

```
// 声明关键字
LINK      IMPORT    EXPORT    MAP_TYPE   PIPELINE   FUNC      CONFIG
LANG

// 变量与类型
LET       VAR       STRUCT    VOID       INT        FLOAT     STRING
BOOL      ARRAY     LIST      TUPLE      DICT       OPTION

// 控制流
RETURN    RETURNS*  IF        ELSE       WHILE      FOR        IN
MATCH     CASE      DEFAULT   BREAK      CONTINUE

// 运算符
AS        AND       OR        NOT        CALL       CONVERT   MAP_FUNC

// 面向对象操作
NEW       METHOD    GET       SET        WITH       DELETE    EXTEND

// 值
TRUE      FALSE     NULL      PACKAGE

// 包管理器
VENV      CONDA     UV        PIPENV     POETRY
```

> **大小写不敏感（Ploy 1.5.2+）。** 上表中所有保留字均按大小写不敏感
> 识别——`link`、`Link`、`LINK`、`LiNk` 都映射到同一个 `LINK` token。
> 词法器会把 `Token::lexeme` 规范化为表中所示的 UPPER 拼写；用户在
> 源代码里真正写下的拼写则保留在 `Token::raw_lexeme` 字段，并通过
> `Token::SourceText()` 暴露。**标识符仍然大小写敏感**——只有关键字
> 集合参与折叠。过去仅在大小写上与关键字不同的标识符（`config`、
> `array`、`get`、`pipeline`、…）现在变为保留字，请改名（例如
> `app_cfg`、`np_array`、`getter`、`run_pipeline`）。
>
> `*` `RETURNS` 仍然为了向后兼容而被解析，但语法分析器会在该 token
> 处发出非致命的 `kDeprecatedKeyword` 警告（ErrorCode = 3024）。新代码
> 请使用 LINK 签名上的 `-> Type` 箭头来声明返回类型。逻辑运算符
> `AND` / `OR` / `NOT` 是 `&&` / `||` / `!` 的精确别名；新代码推荐使用
> 符号形式。

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

**重要说明：** PolyglotCompiler 使用自己的前端（`frontend_cpp`、`frontend_python`、`frontend_rust`、`frontend_java`、`frontend_dotnet`、`frontend_ploy`）编译所有语言源码到统一 IR，然后通过三重后端（x86_64/ARM64/WebAssembly）生成目标代码。编译阶段**不依赖**外部编译器（MSVC/GCC/rustc/CPython/javac/dotnet）。`polyc` 驱动程序 (`driver.cpp`) 在最终链接阶段会调用系统链接器（`polyld` 或 `clang`）将目标文件链接为可执行文件。`polyld` 会自动相对于 `polyc` 所在目录解析，无需加入系统 PATH。

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
| `--lang=<cpp\|python\|rust\|java\|dotnet\|javascript\|ruby\|go\|ploy>` | 源语言（省略时根据扩展名自动检测） |
| `--arch=<x86_64\|arm64\|wasm>` | 目标架构（默认：宿主检测 — Apple Silicon / AArch64 为 `arm64`，其他为 `x86_64`） |
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
| `--progress=json` | 向标准输出发送机器可读 JSON 进度事件（stage_start、stage_end、complete） |
| `--clean-cache` | 清除增量编译缓存并退出 |
| `--dump-token-pool` | 把前端 TokenPool 统计（tokens、arena 字节、唯一标识符、intern 命中/未命中）写入 `<aux>/<stem>.pool_stats.json` |
| `--print-targets[=text\|json]` | 打印当前进程注册的所有后端（triple、别名、能力矩阵）后退出。`text` 为默认；`json` 输出稳定的 JSON 快照。 |
| `--print-target-info=<triple>[:json]` | 打印单个后端的信息（支持别名查询）后退出。追加 `:json` 切换到 JSON 形式。别名未注册时退出码为 `2`，并向 stderr 写入"available backends"诊断。 |
| `-h` / `--help` | 显示帮助信息 |

#### 查看可用后端

```text
$ polyc --print-targets
arm64-unknown-elf
  description: ARM64 (AArch64) backend
  aliases: arm64, aarch64, armv8, aarch64-apple-darwin, aarch64-linux-gnu, aarch64-pc-windows-msvc
  capabilities: object=yes assembly=yes bitcode=no debug-info=yes pic=yes
  register allocators: linear-scan, graph-coloring
wasm32-unknown-unknown
  ...
x86_64-unknown-elf
  ...

$ polyc --print-target-info=amd64
x86_64-unknown-elf
  description: x86_64 backend (System V / Win64)
  aliases: x86_64, x86-64, amd64, x64, x86_64-pc-windows-msvc, x86_64-apple-darwin, x86_64-linux-gnu
  capabilities: object=yes assembly=yes bitcode=no debug-info=yes pic=yes
  register allocators: linear-scan, graph-coloring

$ polyc --print-targets=json | jq '.[].triple'
"arm64-unknown-elf"
"wasm32-unknown-unknown"
"x86_64-unknown-elf"
```

查询大小写不敏感，并接受上一条命令列出的所有别名 —— `--arch=AMD64`、`--arch=x86-64`、
`--arch=x86_64-linux-gnu` 都命中同一个后端。该列表是 `--arch` 接受值的唯一权威来源；
未来子需求新增的后端（例如 2026-04-28-2e 引入的 RISC-V）会自动出现在此列表中，文档无需手工同步。

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
| `build_profile.bin` | 二进制构建性能分析：各阶段计时数据（编译成功后生成） |

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

从 `polyc` 调用时，`polyld` 会自动从 `polyc` 同级目录中查找（兄弟路径解析）。`polyld` 内部包含 `PolyglotLinker`，它消费 `.ploy` 前端的 IR，生成跨语言粘合代码：

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
| **语法高亮** | 使用编译器前端分词器实现精确的、语言感知的高亮，支持全部受支持语言和全部主题。文本颜色通过 `QPalette` 设置，确保 `QSyntaxHighlighter` 的逐字符格式始终优先生效；主题切换后自动触发 `rehighlight()`。Ploy 文件采用三层配色方案：跨语言指令关键字（`LINK`、`IMPORT`、`EXPORT`、`MAP_TYPE`、`PIPELINE`、`CALL`、`NEW`、`METHOD`、`GET`、`SET`、`WITH`、`DELETE`、`EXTEND`、`CONVERT`、`MAP_FUNC`、`CONFIG`）显示为**紫色/品红色**；原始类型关键字（`INT`、`FLOAT`、`STRING`、`BOOL`、`VOID`、`ARRAY`、`LIST`、`TUPLE`、`DICT`、`OPTION`、`STRUCT`）显示为**青色**；语言限定符标识符（`cpp`、`python`、`rust`、`java`、`csharp`、`dotnet`）显示为**黄色**；控制流关键字（`IF`、`WHILE`、`FOR`、`MATCH`、`RETURN` 等）显示为**蓝色** |
| **实时诊断** | 调用完整编译管道（词法 → 语法 → 语义）实时报告错误和警告 |
| **文件浏览器** | 树形项目导航器，按支持的源文件扩展名过滤，支持右键上下文菜单 |
| **多标签编辑器** | 支持多文件编辑，含新建、打开、保存、关闭标签功能 |
| **输出面板** | 三标签输出区域：编译输出、错误表格（点击跳转源码）、日志 |
| **内置终端** | 嵌入式 Shell（Windows 为 PowerShell，Linux/macOS 为 bash/zsh），支持 ANSI 颜色、命令历史和多实例 |
| **代码导航** | 双击诊断表格中的错误行，跳转到对应源码位置 |
| **括号匹配** | 高亮光标处的匹配括号 / 圆括号 / 花括号 |
| **自动缩进** | 新行自动保持当前缩进级别 |
| **缩放** | `Ctrl+加号` / `Ctrl+减号` 调整编辑器字体大小 |
| **暗色主题** | 通过 ThemeManager 统一管理主题，内置 4 套配色方案（暗色、亮色、Monokai、Solarized Dark），所有面板统一切换 |
| **外部主题系统** | VS Code 风格的外部主题 — `.polytheme.json` 文件（可附带同名 `.qss` 兜底），按 3 层目录契约发现：内置 qrc（`:/polyglot/themes/`）、用户级 `~/.polyglot/themes/`、工作区级 `<ws>/.polyglot/themes/`。开箱即用 5 套内置主题（`polyglot.dark`、`polyglot.light`、`polyglot.hc`、`solarized.light`、`solarized.dark`）。使用 `Ctrl+K, Ctrl+T`（或 *视图 → 主题管理器…*）打开 **主题管理器** 对话框，可安装 / 卸载 / 预览 / 导出主题。开发者命令：`workbench.action.selectTheme`、`workbench.action.openColorTheme`、`workbench.action.generateColorTheme`、`editor.action.inspectTMScopes`、`workbench.action.inspectColorTheme`。命令行参数：`--theme <id|path>`、`--list-themes`、`--validate-theme <path>`、`--headless --screenshot <out.png>`。完整规范见 [主题系统](realization/theme_system_zh.md) |
| **编译与运行** | 一键编译并运行当前文件（`Ctrl+R`），自动查找输出二进制文件并通过 QProcess 启动，stdout/stderr 实时输出至日志面板；支持停止运行中的进程（`Ctrl+Shift+R`） |
| **设置对话框** | 7 类偏好设置（外观、编辑器、编译器、环境、构建、调试、键绑定），通过 `QSettings` 持久化存储。外观页包含 *显示工具栏*、*显示状态栏* 和 *显示资源管理器* 开关（默认均为**开启**） |
| **自定义快捷键** | 在设置对话框的键绑定页中使用 QKeySequenceEdit 编辑快捷键，支持应用、重置，自定义快捷键通过 QSettings 持久化存储并在启动时自动加载 |
| **设置联动** | 设置中的 CMake 路径、构建目录、调试器路径等配置自动同步至构建面板和调试面板，修改后即时生效 |
| **Git 集成** | 内置 Git 面板，支持状态查看、暫存、提交、分支管理、推送/拉取、差异查看、日志历史和储藏功能 |
| **构建系统** | CMake 集成面板，支持配置/构建/清理、生成器和构建类型选择、目标发现和错误解析 |
| **调试器** | 集成调试面板，支持 lldb 和 gdb，包括断点、单步执行、完整的调用栈帧解析（模块、函数、文件、行号）、变量类型/值解析、监视表达式实时求值与结果更新、监视右键菜单（删除/全部删除/求值）、入口断点选项和调试控制台 |
| **模板创建** | 通过 *文件 → 从模板新建*（`Ctrl+Shift+N`）从内置语言模板创建新文件。模板包括：Ploy 链接器、C++ hello-world / 类骨架、Python 脚本 / 类、Rust hello-world / 结构体、Java hello-world / 类、C# hello-world / 类 |
| **拓扑图动态连线** | 拓扑面板中的连接线随节点移动实时跟随 — 拖拽节点时所有关联的贝塞尔曲线路径即时重新计算 |

### 快捷键

| 快捷键 | 操作 |
|--------|------|
| `Ctrl+N` | 新建文件 |
| `Ctrl+Shift+N` | 从模板新建文件 |
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

### 资源管理器右键菜单

在文件浏览器中右键点击任意项目，可访问以下上下文敏感菜单操作：

| 操作 | 说明 |
|------|------|
| **Open** | 在编辑器中打开选中的文件（仅限文件） |
| **New File...** | 在选中目录中创建新文件并打开 |
| **New Folder...** | 在选中目录中创建新子文件夹 |
| **Rename...** | 重命名选中的文件或文件夹（F2） |
| **Delete** | 删除选中的文件或文件夹（需确认） |
| **Copy Path** | 将绝对路径复制到剪贴板 |
| **Copy Relative Path** | 将相对于项目根目录的路径复制到剪贴板 |
| **Reveal in File Explorer** | 在操作系统文件管理器中打开所在文件夹 |
| **Open in Terminal** | 在选中目录打开一个新的内置终端会话 |
| **Generate Topology Graph** | 为选中的 `.ploy` 文件生成并展示函数 I/O 拓扑图（仅 `.ploy` 文件可用） |

> **说明：** *Generate Topology Graph* 操作仅对 `.ploy` 文件可见。它会打开拓扑面板（或将其置于前台），并使用完整的拓扑分析管道（词法 → 语法 → 语义 → 拓扑分析器）构建可视化拓扑图。

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

### 拓扑面板

IDE 包含一个用于 `.ploy` 文件的交互式拓扑可视化面板，类似于 Simulink 的模块图视图。

**功能特性：**

| 功能 | 说明 |
|------|------|
| **布局算法** | 提供 8 种可选布局：*分层 (DAG)*（**默认**，全静态分层绘制，通过 Kahn 拓扑序 + 最长路径分层计算）、*力导向*（动画版 Fruchterman–Reingold 模拟，含模拟退火）、*从上到下网格*、*从左到右网格*、*环形*、*同心环（按度数）*、*螺旋*（阿基米德螺线）、*BFS 树*（以最高度数节点为根）。除力导向外的所有布局均为确定性、无动画；用户的选择持久化在 `QSettings` 的 `topology/layout_mode` 中，主面板与所有钻入子窗口共享。布局计算完成后同步更新所有 `TopoEdgeItem` 贝塞尔路径。 |
| **交互式边创建** | 从输出端口拖拽到输入端口（或反向）创建新边。自环和重复边会被拒绝并显示诊断信息。新建的边会自动以 `LINK` 声明写回当前 `.ploy` 文件。 |
| **交互式边删除** | 右键点击边即可从图中删除。对应的 `LINK`/`CALL` 语句会自动从 `.ploy` 源文件中移除。 |
| **文件实时重载** | 通过 `QFileSystemWatcher` 监控已加载的 `.ploy` 文件；文件在磁盘上变更后，经 200 毫秒防抖后自动重建拓扑，状态栏显示"Reloaded"。 |
| **调试执行高亮** | 调试器暂停在某个源码位置时，对应的拓扑节点以亮黄色脉冲动画高亮显示（边框透明度在 40% 到 100% 之间脉动），视图自动滚动居中。支持通过 `HighlightExecutingNode(uint64_t node_id)` 直接按节点 ID 高亮。调试会话结束或新会话开始时通过 `ClearExecutionHighlight()` 清除。 |
| **端口悬停提示** | 将鼠标悬停在任意输入或输出端口上，可查看包含端口名称、类型、方向、父节点名称、所属语言与连接状态（`✔ Valid`、`⚠ Implicit Convert`、`✘ Incompatible`、`? Unknown` 或 `Not connected`）的详细提示。悬停时端口视觉放大。 |
| **验证覆盖** | 点击 **Validate** 运行拓扑验证器；错误节点以红色高亮，诊断信息显示在面板中。 |
| **导出** | 通过工具栏按钮将当前拓扑导出为 DOT、JSON 或 PNG 格式。 |
| **生成 .ploy** | 点击工具栏中的 **Generate .ploy** 按钮，从当前拓扑图自动生成合法的 `.ploy` 源代码。生成的文件写入同目录下的 `<basename>_generated.ploy` 并在编辑器中打开。保存前会验证生成代码的解析/语义正确性。 |

**使用方法：**
1. 在资源管理器中右键点击 `.ploy` 文件并选择 **Generate Topology Graph**，或在打开 `.ploy` 文件时按 `Ctrl+Shift+T`。
2. 使用布局选择器在 *分层 (DAG)*（默认静态）、*力导向*、*从上到下网格*、*从左到右网格*、*环形*、*同心环（按度数）*、*螺旋* 与 *BFS 树* 之间切换。
3. 在端口之间拖拽可交互式创建边；右键点击边可删除。
4. 点击 **Validate** 运行所有边的类型兼容性验证。
5. 启动调试会话 — 拓扑面板将自动高亮当前活跃节点。
6. 点击 **Generate .ploy** 从拓扑图自动生成 `.ploy` 源代码（双向互通）。

**CLI 代码生成：**

`polytopo` 命令行工具也支持通过 `generate` 子命令进行代码生成：

```bash
# 首先，将拓扑图导出为 JSON：
polytopo my_project.ploy --format json --output graph.json

# 然后，从 JSON 图生成 .ploy 源码：
polytopo generate graph.json -o generated.ploy
```

CLI 和 UI 共享相同的 `GeneratePloySrc` 逻辑，确保输出一致。

**双向同步：**

拓扑面板与代码编辑器保持双向实时同步：

- **编辑器 → 拓扑**：在编辑器中保存 `.ploy` 文件后，拓扑面板在 200 毫秒内（防抖）自动重新解析并重建图形。状态栏显示"Reloaded"。
- **拓扑 → 编辑器**：在拓扑面板中通过拖拽创建或右键删除边时，对应的 `LINK`/`CALL` 语句会自动写入（或从）`.ploy` 源文件中移除。编辑器重新加载文件并以黄色高亮显示受影响的行 2 秒钟。

此双向协议确保可视拓扑图与文本 `.ploy` 源代码始终保持同步。实现细节请参阅 `docs/realization/topology_tool_zh.md`。

### Markdown 渲染查看器

IDE 将 Markdown 文档（`.md`、`.markdown`、`.mdown`、`.mkd`）以排版后的格式呈现，而非原始源码。这是在 IDE 内阅读项目自带文档（`README.md`、`docs/USER_GUIDE_zh.md`、`docs/specs/*.md` 等）的推荐方式。

**功能特性：**

| 特性 | 说明 |
|------|------|
| **原生渲染** | 使用 Qt 内置的 `QTextDocument::setMarkdown()` 渲染（CommonMark + GitHub Flavoured Markdown 子集：表格、围栏代码块、任务列表、删除线）。无需任何第三方库。 |
| **预览 ↔ 源码切换** | 内嵌工具栏提供 **Preview / Source / Reload** 按钮。按 `Ctrl+Shift+M`（视图 → 切换 Markdown 预览）即可在渲染视图与原始源码视图之间切换。 |
| **主题感知** | 查看器自动采用当前主题的编辑器背景色、前景色与选中色，使浅色 / 深色主题与 IDE 其它部分保持一致。主题切换时实时更新。 |
| **资源解析** | 相对路径的 `<img src="...">` 与超链接相对于源文件目录解析，因此文档中嵌入的截图能正确显示。 |
| **外部链接** | `http(s)://` 与 `mailto:` 链接在系统默认浏览器中打开。文档内的 `#anchor` 锚点在查看器内跳转。本地跨文档链接（`./other.md`）会在新标签页中打开。 |
| **UTF-8 安全** | 文件统一以 UTF-8 读取，与系统区域设置无关，因此中文 / 日文 / emoji 内容均能正确呈现。 |

**使用步骤：**

1. 通过 **文件 → 打开**、Explorer 面板，或在已渲染的另一份文档中点击 Markdown 链接打开任意 `.md` 文件。IDE 自动识别扩展名，将其在 Markdown 查看器标签页中打开，而非代码编辑器。
2. 使用内嵌工具栏的 **Preview** / **Source** 按钮（或 `Ctrl+Shift+M`）在渲染与原始视图间切换。**Reload** 按钮重新从磁盘读取文件。
3. 点击渲染视图中的任意链接进行跳转（文档内锚点 → 滚动定位，外部 URL → 浏览器，本地文件 → 新标签页）。

> Markdown 查看器在设计上是只读的。如需编辑 Markdown 文件，请右键 → *Open With → Code Editor*（计划中），或临时改名为非 Markdown 扩展名后再打开。

### 设置（VS Code 风格 JSON）

> 所有偏好以单一 JSON 文件存储 —— 与 VS Code 完全一致。
> 原 `QSettings` 存储在首次启动时自动迁移。

**三层覆盖（优先级由低到高）：**

| 层级       | 位置                                                                       |
|------------|----------------------------------------------------------------------------|
| 默认       | 内嵌资源（只读）                                                            |
| 用户       | `%APPDATA%\PolyglotCompiler\settings.json`（Windows） / `~/.config/PolyglotCompiler/settings.json`（Linux） / `~/Library/Application Support/PolyglotCompiler/settings.json`（macOS） |
| 工程       | `<workspace>/.polyglot/settings.json`                                      |

**编辑方式：**

- **表单视图** —— `文件 → 设置`（或在命令面板执行
  `Preferences: Open Settings`）。表单由 `settings_schema.json` 自动生成，
  每行显示来源徽标（`(default)` / `(user)` / `(workspace)`）。
- **JSON 视图** —— 表单顶部按钮直接在编辑器标签页中打开
  `settings.json`（用户/工程/默认）。保存文件触发 200 ms 防抖热重载，
  无需重启。
- **工程级覆盖** —— 通过命令 *Open Workspace Settings (JSON)* 创建
  `.polyglot/settings.json`（不存在时自动生成）。

**命令面板：** 按 `Ctrl+Shift+P` 打开命令面板，执行任意已注册命令
（`Preferences: Open Settings (JSON)`、`Preferences: Open Keyboard Shortcuts (JSON)`、
`File: Save / Save As / Open / New Untitled`、
`Markdown: Toggle Preview` 等）。

**自定义快捷键** 保存在 `settings.json` 同级目录的 `keybindings.json`，
使用与 VS Code 一致的 schema：

```json
[
  { "key": "ctrl+shift+m", "command": "editor.action.toggleMarkdownPreview" },
  { "key": "ctrl+k ctrl+s", "command": "workbench.action.openKeybindings" }
]
```

**CLI 工具**（`polyc`、`polyld`、`polyrt`、`polytopo`、`polybench`）读取
同一份 `settings.json`，并支持两个额外标志：

| 标志                              | 作用                                                  |
|-----------------------------------|-------------------------------------------------------|
| `--settings <path>`               | 用显式 JSON 路径覆盖用户层。                           |
| `--print-effective-settings`      | 把合并后的生效 JSON 打印到 stdout 并退出 0。          |

完整键参考与实现细节见 `docs/realization/settings_system_zh.md`。

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
| `unit_tests` | `tests/unit/` | `[ploy]`, `[gc]`, `[opt]` 等 | 所有模块的单元测试 — **909 个用例** |
| `integration_tests` | `tests/integration/` | `[integration]` | 端到端编译管道、互操作、性能压力 — **92 个用例** |
| `benchmark_tests` | `tests/benchmarks/` | `[benchmark]` | 微基准和宏基准性能测试 — **18 个用例** |

### 测试套件汇总

| 测试套件 | 标签 | 测试用例数 | 覆盖内容 |
|---------|------|-----------|---------|
| .ploy 前端 | `[ploy]` | 283 | 词法/语法/语义/IR/集成/包管理/OOP互操作/错误检查 |
| Python 前端 | `[python]` | 127 | 25+ 高级特性，类型注解，async，推导式 |
| Rust 前端 | `[rust]` | 46 | 借用检查、生命周期、闭包、Traits |
| E2E 管道 | `[e2e]` | 56 | 从源码到目标码的完整管道 |
| FFI / 互操作 | `[ffi]` | 39 | FFI 绑定、编组、类型映射、所有权跟踪 |
| 链接器 | `[linker]` | 36 | 符号解析、ELF/MachO/COFF、跨语言粘合 |
| .NET 前端 | `[dotnet]` | 24 | .NET 6/7/8/9 特性 |
| Java 前端 | `[java]` | 22 | Java 8/17/21/23 特性 |
| 拓扑分析 | `[topology]` | 22 | 图、分析器、验证器、打印器、UI 面板、钻入 |
| 后端 | `[backend]` | 20 | 指令选择、寄存器分配、调度器、WASM |
| GC 算法 | `[gc]` | 20 | 4 种 GC 算法（标记清除、分代、拷贝、增量） |
| 预处理器 | `[preprocessor]` | 18 | 对象宏/函数宏、指令、Token池 |
| 优化 Passes | `[opt]` | 17 | 常量折叠、DCE、CSE、GVN、内联、去虚拟化 |
| 线程服务 | `[threading]` | 16 | 线程池、同步原语、协程 |
| LTO | `[lto]` | 14 | 链接时优化、跨模块优化 |
| PGO | `[pgo]` | 13 | 剖面引导优化 |
| 严格模式语义 | `[sema][strict]` | 10 | 跨语言调用类型严格性、ReportStrictDiag |
| C++ 前端 | `[cpp]` | 10 | OOP、模板、RTTI、异常、constexpr |
| DWARF5 调试 | `[dwarf5]` | 7 | DWARF 5 调试信息生成 |
| 调试信息 | `[debug]` | 4 | PDB、源码映射、调试发射器 |
| 集成测试 | `[integration]` | 92 | 完整管道/跨语言互操作/性能压力/目标文件格式 |
| 基准测试 | `[benchmark]` | 18 | 微基准(词法/语法/语义/lowering)/宏基准(扩展/OOP/管道) |

## 10.2 .ploy 测试详细分类

| 类别 | 标签 | 数量 | 覆盖内容 |
|------|------|------|---------|
| 词法分析 | `[ploy][lexer]` | 17 | 关键字(56，大小写不敏感)、标识符、数字、字符串、运算符 |
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

`tests/integration/` 目录包含 106 个集成测试，分为 6 个类别：

```
tests/integration/
├── compile_tests/
│   └── compile_pipeline_test.cpp       # 41 个测试：完整编译管道
├── interop_tests/
│   └── interop_test.cpp                # 15 个测试：跨语言互操作
├── performance/
│   └── perf_test.cpp                   # 11 个测试：性能压力
├── e2e/
│   └── polyc_e2e_test.cpp              # 12 个测试：polyc CLI 端到端
├── external_packages/
│   ├── external_packages_test.cpp      #  8 个测试：外部包导入解析
│   └── demand_03_test.cpp              #  6 个测试：需求驱动的外部导入
└── object_format_test.cpp              # 13 个测试：COFF / ELF / Mach-O 写入
```

### 集成测试类别

| 类别 | 标签 | 测试数 | 覆盖内容 |
|------|------|--------|---------|
| 编译管道 | `[integration][compile]` | 41 | LINK+CALL、STRUCT、PIPELINE、IF/ELSE/WHILE/FOR、NEW/METHOD、GET/SET、WITH、DELETE、EXTEND、MATCH、ML管道、多函数PIPELINE |
| 跨语言互操作 | `[integration][interop]` | 15 | LINK链、NEW创建、METHOD链、生命周期(NEW→METHOD→DELETE)、多语言对象、GET/SET、WITH/嵌套WITH、EXTEND、组合OOP、三语言 |
| 性能压力 | `[integration][perf]` | 11 | 50/100函数、10/20层嵌套、50/100 CALL、20/50阶段管道、复杂混合程序、词法/语法吞吐 |
| 端到端 | `[integration][e2e]` | 12 | polyc CLI 真实编译流程，从源码到二进制 |
| 外部包 | `[integration][external]` | 14 | 八种语言的外部包导入解析（cpp/python/rust/java/dotnet/go/javascript/ruby） |
| 目标文件格式 | `[integration][objfmt]` | 13 | COFF / ELF / Mach-O 写入与回读 |

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
| `POLYGLOT_CAP_COMPLETION` | `1 << 11` | 编辑器补全提供器 |
| `POLYGLOT_CAP_DIAGNOSTIC` | `1 << 12` | 编辑器诊断提供器 |
| `POLYGLOT_CAP_TEMPLATE` | `1 << 13` | 文件模板提供器 |
| `POLYGLOT_CAP_TOPOLOGY_PROC` | `1 << 14` | 拓扑图后处理器 |

### 版本约束

插件在 `PolyglotPluginInfo` 中声明 `min_host_version`（如 `"1.0.0"`）。宿主在加载时检查此约束，拒绝需要更高版本宿主的插件。格式有误的版本字符串视为"无约束"。

### 冲突检测

`PluginManager::DetectConflicts()` 扫描所有活跃插件中的独占能力冲突——例如两个为同一语言注册格式化器的插件，或两个具有相同 `language_name` 的语言提供器。检测到的冲突以 `std::vector<PluginConflict>` 返回。

### 沙箱策略与熔断器

宿主对插件回调执行**沙箱策略**：

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `call_timeout_ms` | 5000 | 每次回调的最大毫秒数 |
| `memory_limit_bytes` | 0（无限制） | 插件内存限制（预留供未来实施） |
| `max_consecutive_failures` | 3 | 触发熔断前的连续失败次数 |

当插件超过失败阈值时，**熔断器**自动打开——插件被禁用，直到通过 `ResetCircuitBreaker()` 或 IDE 设置中的"重置熔断器"按钮手动重置。

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

完整的发布历史已迁移到独立文档。
请参阅 [`CHANGELOG.md`](CHANGELOG.md)（英文） /
[`CHANGELOG_zh.md`](CHANGELOG_zh.md)（中文）查阅自
v0.1.0 (2026-01-15) 起的全部版本条目。

---

<!-- BEGIN:version_footer_zh -->
*本文档由 PolyglotCompiler 团队维护*  
*最后更新: 2026-04-28*  
*文档版本: v1.2.0*
<!-- END:version_footer_zh -->
