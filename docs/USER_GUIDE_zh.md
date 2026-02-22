# PolyglotCompiler 用户指南

> 一个功能完整的多语言编译器项目  
> 支持 C++、Python、Rust、Java、C# (.NET) → x86_64/ARM64/WebAssembly  
> 含 .ploy 跨语言链接前端

**版本**: v5.0  
**最后更新**: 2026-02-22

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
14. [附录](#14-附录)

---

# 1. 项目概述

## 1.1 项目简介

PolyglotCompiler 是一个现代化的多语言编译器项目，采用多前端共享中间表示（IR）的架构设计，并通过 `.ploy` 语言实现跨语言函数级别链接和面向对象互操作。

**核心目标：**

- ✅ **多语言支持**: C++、Python、Rust、Java、.NET (C#) 的完整编译前端
- ✅ **三重后端架构**: x86_64 (SSE/AVX)、ARM64 (NEON)、WebAssembly 后端
- ✅ **完整工具链**: 编译器（polyc）、链接器（polyld）、优化器（polyopt）、汇编器（polyasm）、运行时工具（polyrt）、基准测试（polybench）
- ✅ **跨语言链接**: 通过 `.ploy` 声明式语法实现函数级跨语言互操作
- ✅ **跨语言 OOP**: 通过 `NEW` / `METHOD` / `GET` / `SET` / `WITH` / `DELETE` / `EXTEND` 关键字支持类实例化、方法调用、属性访问、资源管理、对象销毁和类继承扩展
- ✅ **包管理集成**: 支持 pip/conda/uv/pipenv/poetry/cargo/pkg-config/NuGet/Maven/Gradle
- ✅ **生产级质量**: 完整实现而非最小原型

## 1.2 核心特性一览

### 语言支持

| 语言 | 前端 | IR Lowering | 高级特性 | 状态 |
|------|------|-------------|----------|------|
| **C++** | ✅ | ✅ | OOP/模板/RTTI/异常/constexpr/SIMD | **完整** |
| **Python** | ✅ | ✅ | 类型注解/推导/装饰器/生成器/async/25+高级特性 | **完整** |
| **Rust** | ✅ | ✅ | 借用检查/闭包/生命周期/Traits/28+高级特性 | **完整** |
| **Java** | ✅ | ✅ | Java 8/17/21/23/记录类/密封类/模式匹配/文本块/Switch表达式 | **完整** |
| **.NET (C#)** | ✅ | ✅ | .NET 6/7/8/9/记录类/顶级语句/主构造器/文件范围命名空间/可空引用类型 | **完整** |
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
| 基准测试 | 性能评估套件 | `polybench` |

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
- `.ploy` 前端独特之处在于它的 IR 被 PolyglotLinker 消费，生成跨语言粘合代码

## 3.2 目录结构

```
PolyglotCompiler/
├── frontends/              # 前端（6 个语言前端）
│   ├── common/             # 通用前端设施（Token、Diagnostics、预处理器）
│   │   └── src/            #   token_pool.cpp, preprocessor.cpp
│   ├── cpp/                # C++ 前端
│   │   ├── include/        #   cpp_lexer.h, cpp_parser.h, cpp_sema.h, cpp_lowering.h
│   │   └── src/            #   lexer/, parser/, sema/, lowering/, constexpr/
│   ├── python/             # Python 前端
│   │   ├── include/        #   python_lexer.h, python_parser.h, python_sema.h, python_lowering.h
│   │   └── src/            #   lexer/, parser/, sema/, lowering/
│   ├── rust/               # Rust 前端
│   │   ├── include/        #   rust_lexer.h, rust_parser.h, rust_sema.h, rust_lowering.h
│   │   └── src/            #   lexer/, parser/, sema/, lowering/
│   ├── java/               # Java 前端 (Java 8/17/21/23)
│   │   ├── include/        #   java_ast.h, java_lexer.h, java_parser.h, java_sema.h, java_lowering.h
│   │   └── src/            #   lexer/, parser/, sema/, lowering/
│   ├── dotnet/             # .NET 前端 (C# .NET 6/7/8/9)
│   │   ├── include/        #   dotnet_ast.h, dotnet_lexer.h, dotnet_parser.h, dotnet_sema.h, dotnet_lowering.h
│   │   └── src/            #   lexer/, parser/, sema/, lowering/
│   └── ploy/               # .ploy 跨语言链接前端
│       ├── include/        #   ploy_ast.h, ploy_lexer.h, ploy_parser.h, ploy_sema.h, ploy_lowering.h
│       └── src/            #   lexer/, parser/, sema/, lowering/
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
├── tools/                  # 工具链（6 个可执行文件）
│   ├── polyc/              # 编译器驱动 (driver.cpp ~1412 行)
│   ├── polyld/             # 链接器 (linker.cpp + polyglot_linker.cpp ~522 行)
│   ├── polyasm/            # 汇编器 (assembler.cpp)
│   ├── polyopt/            # 优化器 (optimizer.cpp)
│   ├── polyrt/             # 运行时工具 (polyrt.cpp)
│   ├── polybench/          # 基准测试 (benchmark_suite.cpp)
│   └── ui/                 # UI 工具
├── tests/                  # 测试
│   ├── unit/               # 单元测试（Catch2 框架）— 734 个测试用例
│   │   └── frontends/ploy/ #   ploy_test.cpp（207 测试用例）
│   ├── samples/            # 示例程序（16 个分类目录，含 .ploy/.cpp/.py/.rs/.java/.cs）
│   ├── integration/        # 集成测试（编译管道/互操作/性能）— 50 个测试用例
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

**各前端规模概览：**

| 前端 | 源码路径 | 核心特性 |
|------|----------|---------|
| C++ | `frontends/cpp/` (5 编译单元) | OOP、模板、RTTI、异常、constexpr、SIMD |
| Python | `frontends/python/` (4 编译单元) | 类型注解、推导、25+高级特性 |
| Rust | `frontends/rust/` (4 编译单元) | 借用检查、生命周期、闭包、28+高级特性 |
| Java | `frontends/java/` (4 编译单元) | Java 8/17/21/23、记录类、密封类、模式匹配、Switch表达式、文本块 |
| .NET (C#) | `frontends/dotnet/` (4 编译单元) | .NET 6/7/8/9、记录类、顶级语句、主构造器、可空引用类型 |
| .ploy | `frontends/ploy/` (4 编译单元) | LINK、IMPORT、PIPELINE、CONFIG、包管理 |

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

**IR 生成：**
1. 求值资源表达式
2. 调用 `__enter__` 桥接桩 → 将结果绑定到变量名
3. 执行语句体
4. 调用 `__exit__` 桥接桩进行清理
5. 记录两个跨语言描述符：一个用于 `__enter__`，一个用于 `__exit__`

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
2. **触发发现** — 遇到 `IMPORT PACKAGE` 时执行对应的发现命令（每种语言每编译单元仅执行一次）
3. **缓存结果** — 发现的包和版本缓存在 `discovered_packages_` 映射中
4. **版本验证** — 对版本约束进行 6 种运算符验证
5. **符号验证** — 验证选择性导入的符号无重复

### 平台适配

- **Windows**: 虚拟环境 Python 位于 `<venv_path>\Scripts\python.exe`
- **Unix/macOS**: 虚拟环境 Python 位于 `<venv_path>/bin/python`

## 4.4 混合编译方式

> **核心思想：PolyglotCompiler 自身编译所有语言，通过 `.ploy` 描述跨语言连接关系。**

```
┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐
│  C++ 源代码  │  │ Python 源代码│  │  Rust 源代码 │  │  Java 源代码 │  │  C# 源代码  │
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
                                       │  统一      │
                                       │  二进制    │
                                       └───────────┘
```

**重要说明：** PolyglotCompiler 使用自己的前端（`frontend_cpp`、`frontend_python`、`frontend_rust`、`frontend_java`、`frontend_dotnet`、`frontend_ploy`）编译所有语言源码到统一 IR，然后通过三重后端（x86_64/ARM64/WebAssembly）生成目标代码。**不依赖**外部编译器（MSVC/GCC/rustc/CPython/javac/dotnet）。`polyc` 驱动程序 (`driver.cpp`) 中仅在最终链接阶段可选择调用系统链接器（`polyld` 或 `clang`）将目标文件链接为可执行文件。

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
| 容器类型编组 | ✅ | list, tuple, dict, optional |
| 结构体映射 | ✅ | 跨语言结构体字段转换 |
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

完整的 Java 前端，支持 Java 8、17、21 和 23 的特性：

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

完整的 C# (.NET) 前端，支持 .NET 6、7、8 和 9 的特性：

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
| `-h` / `--help` | 显示帮助信息 |

### 语言自动检测

`polyc` 根据文件扩展名自动检测源语言：

| 扩展名 | 语言 |
|--------|------|
| `.ploy` | ploy |
| `.py` | python |
| `.cpp`, `.cc`, `.cxx`, `.c` | cpp |
| `.rs` | rust |

### 进度输出

默认情况下，`polyc` 向 stderr 打印详细进度信息：

```
========================================
 PolyglotCompiler v4.3  (polyc)
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

实现:
- `backends/x86_64/src/calling_convention.cpp`
- `backends/arm64/src/calling_convention.cpp`
- `runtime/src/interop/calling_convention.cpp` (跨语言调用约定适配)

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
| `unit_tests` | `tests/unit/` | `[ploy]`, `[gc]`, `[opt]` 等 | 所有模块的单元测试 — **734 个用例** |
| `integration_tests` | `tests/integration/` | `[integration]` | 端到端编译管道、互操作、性能压力 — **50 个用例** |
| `benchmark_tests` | `tests/benchmarks/` | `[benchmark]` | 微基准和宏基准性能测试 — **18 个用例** |

### 测试套件汇总

| 测试套件 | 标签 | 测试用例数 | 覆盖内容 |
|---------|------|-----------|---------|
| .ploy 前端 | `[ploy]` | 207 | 词法/语法/语义/IR/集成/包管理/OOP互操作/错误检查 |
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
| 集成测试 | `[integration]` | 50 | 完整管道/跨语言互操作/性能压力 |
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

`tests/integration/` 目录包含 50 个集成测试，分为 3 个类别：

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
| `frontend_ploy` | 静态库 | frontend_common, middle_ir (4 编译单元) |
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

# 13. 附录

## 13.1 术语表

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

## 13.2 .ploy 关键字速查表

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

## 13.3 版本运算符

| 运算符 | 含义 | 示例 |
|--------|------|------|
| `>=` | 大于等于 | `>= 1.20` |
| `<=` | 小于等于 | `<= 2.0` |
| `==` | 精确等于 | `== 1.10.0` |
| `>` | 严格大于 | `> 1.0` |
| `<` | 严格小于 | `< 3.0` |
| `~=` | 兼容版本 (PEP 440) | `~= 1.20` → `>= 1.20, < 2.0` |

## 13.4 参考资料

- "Compilers: Principles, Techniques, and Tools" (龙书)
- "Engineering a Compiler" (鲸书)
- LLVM: https://llvm.org/
- Rust Compiler: https://github.com/rust-lang/rust
- PEP 440: https://peps.python.org/pep-0440/

## 13.5 更新日志

### v5.0 (2026-02-22)
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
- ✅ 总计：802 个测试用例，3 个测试套件（734 单元 + 50 集成 + 18 基准）

### v4.3 (2026-02-20)
- ✅ 新增跨语言对象销毁 `DELETE` 关键字
- ✅ 新增跨语言类继承扩展 `EXTEND` 关键字
- ✅ 增强诊断基础设施：严重性级别、错误代码（1xxx-5xxx）、溯源链、建议
- ✅ 新增语义分析中的参数数量不匹配检查
- ✅ 新增语义分析中的类型不匹配检查
- ✅ 所有错误报告升级为结构化错误代码
- ✅ AST / 词法 / 语法 / 语义 / IR Lowering 全链路实现（DELETE/EXTEND）
- ✅ 36 个新测试用例，总计 207 测试用例、598 断言
- ✅ 关键字数量 52 → 54

### v4.2 (2026-02-20)
- ✅ 新增跨语言属性访问 `GET` 和属性赋值 `SET` 关键字
- ✅ 新增自动资源管理 `WITH` 关键字
- ✅ 新增带限定类型的类型注解支持
- ✅ 新增通过 `MAP_TYPE` 进行接口映射
- ✅ AST / 词法 / 语法 / 语义 / IR Lowering 全链路实现（GET/SET/WITH）
- ✅ 31 个新测试用例，总计 171 测试用例、523 断言
- ✅ 中英双语用户指南（USER_GUIDE.md / USER_GUIDE_zh.md）
- ✅ 关键字数量 49 → 52

### v4.1 (2026-02-20)
- ✅ 新增跨语言类实例化 `NEW` 和方法调用 `METHOD` 关键字
- ✅ AST / 词法 / 语法 / 语义 / IR Lowering 全链路实现
- ✅ 22 个新测试用例，总计 140 测试用例、443 断言
- ✅ 中英双语功能文档 (`class_instantiation.md` / `class_instantiation_zh.md`)
- ✅ 关键字数量 47 → 49

### v4.0 (2026-02-19)
- ✅ 新增 .ploy 跨语言链接前端完整章节
- ✅ 多包管理器支持: CONFIG CONDA / UV / PIPENV / POETRY
- ✅ 版本约束验证（6 种运算符）
- ✅ 选择性导入
- ✅ 117+ 测试用例，361+ 断言
- ✅ 全面审查并重写完整指南（v3.0 → v4.0）
- ✅ 精确说明编译流程（PolyglotCompiler 自身前端编译，不依赖外部编译器）

### v3.0 (2026-02-01)
- ✅ 第 11-15 章（实现分析/测试/高级优化/成就总结）
- ✅ 33+ 优化 passes、4 种 GC 算法
- ✅ PGO/LTO/DWARF5 支持

### v2.0 (2026-01-29)
- ✅ 循环优化、GVN、constexpr、DWARF 5

### v1.0 (2026-01-15)
- ✅ 基础编译链：3 种前端、2 种后端、基础优化

---

*本文档由 PolyglotCompiler 团队维护*  
*最后更新: 2026-02-22*  
*文档版本: v5.0*