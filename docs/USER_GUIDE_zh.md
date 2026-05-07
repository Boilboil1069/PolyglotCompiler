# PolyglotCompiler 用户指南

> **文档版本**：4.0.0  
> **最后更新**：2026-05-07  
> **项目**：PolyglotCompiler 1.45.2  
> **配套文档**：[USER_GUIDE.md](USER_GUIDE.md)

PolyglotCompiler 完整动手指南——一条多语言编译器工具链：吞入
C++、Python、Rust、Java、C#/.NET、Go、JavaScript、Ruby 以及自研的
**Ploy** 胶水语言，统一降到同一份中间表示（IR），并为
**x86_64**、**ARM64** 与 **WebAssembly** 生成原生代码。配套 IDE
（`polyui`）提供基于 LSP 的多语言编辑器、调试器、性能分析器、调用关系
分析器、包管理视图与测试浏览器。

---

## 目录

1. [简介](#1-简介)
2. [快速开始](#2-快速开始)
3. [架构总览](#3-架构总览)
4. [Ploy 胶水语言](#4-ploy-胶水语言)
5. [语言前端](#5-语言前端)
6. [工具与命令行驱动](#6-工具与命令行驱动)
7. [统一 IR](#7-统一-ir)
8. [中端优化](#8-中端优化)
9. [运行时](#9-运行时)
10. [后端](#10-后端)
11. [链接器与目标格式](#11-链接器与目标格式)
12. [IDE（`polyui`）与语言服务器](#12-idepolyui-与语言服务器)
13. [诊断、性能分析与调用分析](#13-诊断性能分析与调用分析)
14. [测试、示例与持续集成](#14-测试示例与持续集成)
15. [构建系统与依赖](#15-构建系统与依赖)
16. [扩展编译器](#16-扩展编译器)
17. [插件 SDK](#17-插件-sdk)
18. [附录](#18-附录)

---

## 1. 简介

### 1.1 PolyglotCompiler 是什么

PolyglotCompiler 把多语言混合的源码树编译成一份链接产物。一个程序
可以同时引入 C++ 图像滤镜、Rust 序列化器、Python 机器学习模型与
Go HTTP 客户端，由 `.ploy` 驱动文件粘合，最终产出 x86_64、ARM64 或
WebAssembly 单一可执行件。整个编译器围绕三条核心保证构建：

1. **统一 IR。** 每种支持语言降到同一份 SSA、三地址形式的 IR；优化、
   调试信息、代码生成只写一次，全部前端共享。
2. **跨语言一等公民调用。** `bridge_call` IR 操作经由统一的 FFI
   编排层（`runtime::ffi::BridgeFrame`）跨越语言边界，链接器会校验
   每个 bridge 调用点是否存在已注册的对端。
3. **生产级工具。** 仓库内同时提供 11 个 CLI 驱动与一个 Qt 6 IDE，
   保证 “编辑→编译→调试→性能分析→打包” 的端到端开发循环每次提交
   都被 CI 校验。

### 1.2 能力矩阵速览

| 领域       | 1.45.2 状态                                                                                                  |
|------------|--------------------------------------------------------------------------------------------------------------|
| 前端       | C++、Python、Rust、Java、.NET（C#）、Go、JavaScript、Ruby、Ploy，共 9 个。                                    |
| 后端       | x86_64（System V + Win64 + macOS Mach-O）、ARM64（AAPCS64，Linux ELF + macOS Mach-O）、WebAssembly MVP+SIMD。 |
| 工具驱动   | `polyc`、`polyld`、`polyasm`、`polyopt`、`polyrt`、`polybench`、`polytopo`、`polyls`、`polydoc`、`polyver`、`polyui`，共 11 个。 |
| 垃圾回收器 | 标记—清扫、三色、分代、引用计数（4 种算法，运行时可选）。                                                     |
| 测试面     | 30 个 CTest 目标，覆盖单元、集成、端到端、基准与工具。                                                        |
| 示例集     | 42 个编号示例（`00_minimal` … `41_grammar_polish`），外加 `01_basic_linking_v2` 变体。                       |
| IDE        | Qt 6 polyui，集成 LSP、DAP、性能分析、调用分析、SCM、测试浏览器、包管理视图。                                  |
| 双语文档   | `docs/` 下 EN+ZH 同步，由 `docs_lint`、`docs_sync` CI 目标守门。                                              |

### 1.3 阅读顺序

新手请先把 [快速开始](#2-快速开始) 全章跑一遍，再深入各语言前端章节。
编译器开发者可直接跳到 [统一 IR](#7-统一-ir) 与 [中端优化](#8-中端优化)。
工具用户可直接看第 12–14 章。插件作者可直接跳到 [第 17 章](#17-插件-sdk)。

### 1.4 约定

* `polyc`、`polyld` 等等宽字体名字均指 `build/` 下的二进制。
* 命令行片段除非另行说明，工作目录默认为仓库根。
* macOS / Linux 路径使用 `/`；Windows 使用 PowerShell 的 `;` 与反引号续行。
* `<…>` 表占位符；`[…]` 表可选参数。
* 项目内代码注释统一英文；本指南遵循同一约定。

### 1.5 各类索引

| 你想……                              | 翻到                                                                            |
|-------------------------------------|---------------------------------------------------------------------------------|
| 第一次构建项目                      | [§ 2.2](#22-克隆与构建)                                                          |
| 编译单个 C++ 文件                   | [§ 2.3](#23-第一个-c-程序)                                                       |
| 把 C++ 接进 Python 流水线           | [§ 2.4](#24-第一个跨语言流水线)                                                   |
| 学习 Ploy 文法                      | [第 4 章](#4-ploy-胶水语言) 与 [tutorial/ploy_language_tutorial_zh.md](tutorial/ploy_language_tutorial_zh.md) |
| 写一个新优化 pass                   | [§ 8.6](#86-编写-pass) 与 [§ 16.4](#164-新增中端-pass)                            |
| 接入全新源语言                      | [§ 16.5](#165-新增前端) 与 [§ 17.3](#173-必选导出)                                 |
| 性能分析长时间运行的程序            | [§ 13.3](#133-性能分析器) 与 [tutorial/profiling_quickstart_zh.md](tutorial/profiling_quickstart_zh.md) |
| 发布构建                            | [§ 18.3](#183-发布打包)                                                          |

---

## 2. 快速开始

### 2.1 先决条件

| 平台          | 工具链                                                                                          |
|---------------|-------------------------------------------------------------------------------------------------|
| macOS 13+     | Xcode 15 命令行工具、CMake 3.27+、Ninja 1.11+、Python 3.11+、Qt 6.6+（`brew install qt`）。     |
| Ubuntu 22.04+ | `clang-17` 或 `gcc-13`、CMake 3.27+、Ninja 1.11+、Python 3.11+、`libssl-dev`、`qt6-base-dev`。   |
| Windows 11    | Visual Studio 2022（17.8+）或 LLVM 17 + Ninja、CMake 3.27+、Python 3.11+、Qt 6.6+ MSVC kit。     |

可选但能让所有前端完整工作：

| 前端       | 可选工具链                                                              |
|------------|-------------------------------------------------------------------------|
| Python     | CPython 3.11+，可选 `conda`、`uv`、`poetry` 用于环境发现。              |
| Rust       | Rust 1.78（`rustup default stable`）。                                  |
| Java       | JDK 21（Temurin、Microsoft Build of OpenJDK 或 Liberica）。             |
| .NET       | .NET 8 SDK。                                                            |
| Go         | Go 1.22+。                                                              |
| JavaScript | Node.js 20+，可选 `pnpm` 或 `yarn`。                                    |
| Ruby       | Ruby 3.3，含 `Gemfile` 项目需 `bundler`。                               |
| 交叉编译   | `lld` 17+、`wasi-sdk` 22（`wasm32-wasi`）、目标 sysroot 放置于 `~/sdk`。 |

### 2.2 克隆与构建

```sh
git clone https://example.invalid/polyglot/PolyglotCompiler.git
cd PolyglotCompiler
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
```

首次构建会按 [Dependencies.cmake](Dependencies.cmake) 拉取第三方依赖
并产出 [§ 1.2](#12-能力矩阵速览) 列出的所有工具。在拥有热依赖缓存的
现代笔记本上预计耗时 4–8 分钟。

#### 2.2.1 常用 CMake 选项

| 选项                           | 默认值              | 说明                                                                |
|--------------------------------|---------------------|---------------------------------------------------------------------|
| `CMAKE_BUILD_TYPE`             | `Debug`             | 日常开发推荐 `RelWithDebInfo`。                                     |
| `POLY_BUILD_IDE`               | `ON`                | 设为 `OFF` 进行无 Qt 的无头/CI 构建。                                |
| `POLY_BUILD_TESTS`             | `ON`                | 关闭后跳过 30 个 CTest 目标。                                       |
| `POLY_BUILD_BENCHMARKS`        | `ON`                | 关闭后跳过 `benchmark_*` JSON 生产。                                |
| `POLY_BUILD_SAMPLES`           | `ON`                | 关闭后跳过 `samples_smoke`。                                        |
| `POLY_SANITIZE`                | `OFF`               | `address`、`undefined`、`thread`，或 `address;undefined`。           |
| `POLY_COVERAGE`                | `OFF`               | 加 `--coverage` 与 `-fprofile-instr-generate`。                      |
| `POLY_ENABLE_LTO`              | `OFF`               | 工具链自身使用 thin LTO 构建。                                       |
| `POLY_TARGET_TRIPLES`          | host                | 分号分隔的额外交叉编译三元组。                                       |
| `POLY_DEFAULT_GC`              | `tricolor`          | `msweep` / `tricolor` / `generational` / `refcount`。               |

#### 2.2.2 无头/CI 构建

```sh
cmake -S . -B build-ci -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DPOLY_BUILD_IDE=OFF \
      -DPOLY_BUILD_BENCHMARKS=OFF
cmake --build build-ci -j
ctest --test-dir build-ci -j --output-on-failure
```

#### 2.2.3 单目标构建

```sh
cmake --build build --target polyc
cmake --build build --target test_runtime
cmake --build build --target polyui polyls
```

### 2.3 第一个 C++ 程序

```sh
echo 'int main() { return 42; }' > hello.cpp
build/polyc hello.cpp -o hello
./hello; echo $?     # 输出 42
```

`polyc` 自动按扩展名识别语言，分派到 C++ 前端，降到统一 IR，运行
默认 O2 优化，最后为宿主三元组发出原生可执行文件。

逐阶段产出查看：

```sh
build/polyc hello.cpp --emit=ir:hello.ir          # 文本 IR
build/polyc hello.cpp --emit=asm:hello.s          # 后端汇编
build/polyc hello.cpp --emit=obj:hello.o          # 目标文件
build/polyc hello.cpp --print-passes              # 仅打印 pass 流水线
build/polyc hello.cpp -o hello --opt=O0 -g        # 调试构建
```

### 2.4 第一个跨语言流水线

`tests/samples/09_mixed_pipeline/mixed_pipeline.ploy` 把 C++ 图像
滤镜与 Python 分类器粘在一起：

```ploy
LINK cpp::filter::sharpen   AS sharpen(image: bytes) -> bytes;
LINK python::ml::classify   AS classify(image: bytes) -> string;

PIPELINE main(path: string) -> string {
    LET raw     = io::read_file(path);
    LET sharper = sharpen(raw);
    RETURN classify(sharper);
}
```

构建并运行：

```sh
build/polyc tests/samples/09_mixed_pipeline/mixed_pipeline.ploy \
            -o build/mixed_pipeline
build/mixed_pipeline tests/samples/09_mixed_pipeline/sample.png
```

驱动通过 **包管理器自动发现**（[§ 4.4](#44-包管理器自动发现)）找到
同目录下的 `*.cpp` 与 `*.py`，分别交给对应前端，再链接 IR 模块得到
单个可执行件。

### 2.5 第一个 WebAssembly 构建

```sh
build/polyc hello.cpp --target=wasm32-wasi -o hello.wasm
wasmtime hello.wasm; echo $?
```

wasm 后端写出符合 WASM 1.0 的模块，并启用 SIMD-128 指令；详见
[第 10 章](#10-后端)。

### 2.6 从零写一个 Ploy 程序

```ploy
FN factorial(n: i64) -> i64 {
    IF n <= 1 { RETURN 1; }
    RETURN n * factorial(n - 1);
}

FN main() -> i32 {
    PRINT(factorial(10));
    RETURN 0;
}
```

```sh
build/polyc fact.ploy -o fact && ./fact
3628800
```

### 2.7 跑测试

```sh
ctest --test-dir build -j --output-on-failure
ctest --test-dir build -R "frontend"
ctest --test-dir build -R "samples_smoke" -V
```

### 2.8 启动 IDE

```sh
build/polyui &
```

在欢迎页打开 `tests/samples/09_mixed_pipeline/`；LSP 面板会记录
`polyls`、`pyright` 与 `clangd` 的 `initialize` 协商。详见
[第 12 章](#12-idepolyui-与语言服务器)。

---

## 3. 架构总览

### 3.1 顶层流水线

```
                          ┌─────────────┐
   .cpp .py .rs .java .cs │   前端集    │   ──►  统一 IR（文本或二进制）
   .go .js .rb .ploy      └─────┬───────┘
                                │
                                ▼
                       ┌────────────────┐
                       │      中端      │   pass 管理器 + 分析
                       └────────┬───────┘
                                │
                                ▼
                       ┌────────────────┐
                       │   后端选择     │   x86_64 / arm64 / wasm
                       └────────┬───────┘
                                │
                                ▼
                       ┌────────────────┐
                       │  polyld 链接   │   ELF / Mach-O / PE / WASM
                       └────────┬───────┘
                                │
                                ▼
                          可执行 / 库 / wasm 模块
```

### 3.2 目录布局

| 路径               | 内容                                                                    |
|--------------------|-------------------------------------------------------------------------|
| `frontends/`       | 每种源语言一个子目录，分别构建为静态库。                                |
| `middle/`          | IR 模块、pass 管理器、各优化 pass、校验器。                             |
| `backends/`        | `x86_64/`、`arm64/`、`wasm/` 加 `common/` 寄存器分配与指令选择。        |
| `runtime/`         | GC、FFI bridge、各语言运行时、性能分析钩子、调试信息。                  |
| `tools/`           | 11 个工具驱动（`polyc` … `polyui`）。                                   |
| `tests/`           | `unit/`、`integration/`、`e2e/`、`benchmark/`、`samples/`（42 编号）。  |
| `docs/`            | 本指南、教程、规范、实现笔记与需求日志。                                |
| `scripts/`         | 仓库自动化（docs lint、sync check、打包）。                             |
| `common/`          | 通用工具（诊断、文件 I/O、JSON、CLI）。                                 |
| `deps/`            | 入仓或锁定的第三方源码。                                                |

### 3.3 模块所有权

每个子系统在 `include/<subsystem>/` 下持有自己的公共头文件，并导出
一个 CMake 目标。模块之间只能通过这些公共头交互；构建会拒绝越界访问
内部布局。

| 子系统      | 公共头根目录                     | CMake 目标            |
|-------------|----------------------------------|------------------------|
| common      | `common/include/poly/common/`    | `poly_common`          |
| frontends   | `frontends/<lang>/include/`      | `frontend_<lang>`      |
| middle      | `middle/include/poly/middle/`    | `poly_middle`          |
| backends    | `backends/<arch>/include/`       | `backend_<arch>`       |
| runtime     | `runtime/include/poly/runtime/`  | `poly_runtime`         |
| tools       | `tools/<name>/include/`          | 每目录一个可执行       |

### 3.4 `polyc` 内部数据流

```
argv  →  CLI 解析  →  driver
                            │
                            ├─►  源码发现   （按扩展名）
                            │       └─► 包管理器探测 (4.3)
                            ├─►  前端分派
                            │       └─► IR 模块装配
                            ├─►  中端 pass 管理器
                            ├─►  后端降级
                            ├─►  进程内调用 polyld
                            └─►  产物写出
```

### 3.5 并发模型

工具链内部：

* 各前端降级运行在全局任务调度器（`runtime::tasks::Pool`）。
* pass 管理器并行化函数级 pass；模块级与循环级 pass 串行执行。
* `polyld` 按输出 section 并行写目标文件。
* IDE 使用一个 Qt 主线程加上一个工作池，复用运行时的任务调度器。

### 3.6 遥测与可观测性

每个长任务工具都暴露 `--trace=<sink>`，输出 Chrome trace 兼容的 JSON
流。IDE 在 **Compile Pipeline Inspector** 面板里直接消费同一格式。
遥测默认关闭，详见
[realization/telemetry_zh.md](realization/telemetry_zh.md)。

---

## 4. Ploy 胶水语言

完整参考：[tutorial/ploy_language_tutorial_zh.md](tutorial/ploy_language_tutorial_zh.md)。

### 4.1 为什么需要 Ploy

Ploy 用来粘合异构代码单元。一份 `.ploy` 文件声明导入、类型映射、
转换函数与流水线，而不再实现宿主语言。它刻意保持精简：整个文法
只有 54 个关键字。

### 4.2 词法与语法基线

* 语句以 `;` 结束（形式文法要求；内置 `polyc --format` 强制执行）。
* 块定界符 `{` 与 `}`。
* 标识符 ASCII 或全 UTF-8（NFC 归一化）。
* 注释：`// 行` `/* 块 */` `/// 文档`。
* 字符串字面量扩展：`r"…"`、`b"…"`、`f"…"`、`rb"…"`、`f"""…"""`（v1.17）。

### 4.3 核心构造

| 关键字            | 用途                                                          |
|-------------------|---------------------------------------------------------------|
| `LET` / `VAR`     | 不可变 / 可变绑定。                                           |
| `FN`              | 函数定义。                                                    |
| `STRUCT`          | 聚合类型。                                                    |
| `OPTION` / `MATCH`| 和类型与穷尽模式匹配。                                        |
| `LINK`            | 引入宿主符号，使用 `from="<lang>:<spec>"`。                   |
| `IMPORT … PACKAGE`| 引入宿主包；支持版本与选择性导入。                            |
| `MAP_TYPE`        | 双向类型桥接。                                                |
| `CONVERT`         | 用户定义转换函数。                                            |
| `PIPELINE`        | 可组合数据流链。                                              |
| `TRY` / `CATCH`   | 结构化错误处理（v1.13）。                                     |
| `ASYNC` / `AWAIT` | 异步函数与等待点（v1.14）。                                   |
| `GENERIC`         | 受约束泛型（v1.15）。                                         |
| `PUBLIC` / `PRIVATE` / `INTERNAL` | 可见性（v1.16）。                            |
| `CLASS` / `NEW`   | 桥接宿主语言类实例化（v1.20）。                               |

完整 54 关键字列表见语言教程第 23 节。

### 4.4 包管理器自动发现

`polyc` 调用一份 `.ploy` 驱动文件时，会遍历同级目录识别下表清单，
然后请求对应前端引入项目：

| 清单                                  | 前端         | 备注                                          |
|---------------------------------------|--------------|-----------------------------------------------|
| `CMakeLists.txt`                      | C++          | 通过 configure 时导出的编译标志。             |
| `pyproject.toml`、`requirements.txt`  | Python       | 同时识别 conda / uv / poetry / venv。        |
| `Cargo.toml`                          | Rust         | 支持 workspace。                              |
| `pom.xml`、`build.gradle[.kts]`       | Java         | 用 Maven 与 Gradle 输出作为 classpath。       |
| `*.csproj`、`*.sln`                   | .NET         | NuGet restore 委托给 `dotnet`。               |
| `go.mod`                              | Go           | 仅支持无 `GOPATH` 的模块模式。                |
| `package.json`                        | JavaScript   | 用 npm / pnpm / yarn 锁文件解析。             |
| `Gemfile`                             | Ruby         | Bundler 解析；运行时为 `ruby`。               |
| `.ploy.toml`                          | Ploy         | 项目级 Ploy 设置。                            |

### 4.5 IMPORT 包语法

```ploy
IMPORT python PACKAGE numpy >= 1.20;                  // 版本约束
IMPORT python PACKAGE numpy::(array, mean);           // 选择性导入
IMPORT python PACKAGE numpy >= 1.20 AS np;            // 别名
IMPORT rust   PACKAGE serde::(Serialize, Deserialize);
IMPORT cpp    PACKAGE eigen >= 3.4;
IMPORT java   PACKAGE org.json::(JSONObject) AS json;
```

包别名不可与目标含糊的选择性导入并存。IDE 在编辑期以
`polyc-err-E0612` 标记此类问题。

### 4.6 跨语言调用

```ploy
LINK cpp::graphics::draw_point  AS draw(p: ptr<u8>) -> void;
LINK python::numpy::mean        AS mean(xs: list<f64>) -> f64;
LINK rust::serde_json::to_string AS to_json<T>(value: T) -> string;

FN demo() -> void {
    LET avg = mean([1.0, 2.0, 3.0, 4.0]);
    PRINT(avg);
}
```

`LINK` 形式降为 `bridge_call` IR 操作；类型编排规则见
[realization/bridge_marshalling.md](realization/bridge_marshalling.md)。

### 4.7 跨语言类实例化

```ploy
LINK cpp::geometry::Point AS Point CLASS {
    NEW(x: f64, y: f64) -> Point;
    FN  norm(self: Point) -> f64;
};

FN distance() -> f64 {
    LET a = NEW Point(0.0, 0.0);
    LET b = NEW Point(3.0, 4.0);
    RETURN b.norm() - a.norm();
}
```

`CLASS` 块声明桥接形态；每个方法降为 `bridge_call`，第一个参数为
装箱后的 receiver。

### 4.8 编译模型

```
.ploy → ploy 前端 ──┐
.cpp  → cpp  前端 ──┤
.py   → py   前端 ──┼──► 统一 IR ──► 中端 ──► 后端 ──► polyld ──► 产物
.rs   → rust 前端 ──┘
```

跨语言调用降为 **bridge stub**，参数走运行时 FFI 层；`polyc
--emit=call-graph` 输出的调用图把 bridge 边单独标识，IDE 可以用
对比色渲染。

### 4.9 诊断标识符

Ploy 诊断与其他前端共用统一目录，标识符格式
`polyc-(err|warn)-<E####|W####>`。常见条目：

| Id                | 含义                                                        |
|-------------------|-------------------------------------------------------------|
| `polyc-err-E0101` | 非预期 token / 解析失败。                                    |
| `polyc-err-E0210` | `LINK` 签名类型不匹配。                                      |
| `polyc-err-E0311` | `IMPORT … PACKAGE` 中包名未知。                              |
| `polyc-err-E0405` | 异步函数在非 `ASYNC` 上下文里被 await。                     |
| `polyc-err-E0612` | 选择性导入与别名组合含糊。                                  |
| `polyc-warn-W0701`| 未使用的 `LINK` 声明。                                       |
| `polyc-warn-W0903`| bridge 调用缺少对端符号。                                    |

完整目录见 [specs/ploy_diagnostics.md](specs/ploy_diagnostics.md)。

---

## 5. 语言前端

每个前端是 `frontends/<lang>/` 下的静态库，对外暴露
`Lower(Source) → IRModule` 一个入口，并各自维护自己的语言特化诊断，
统一映射到 `polyc-(err|warn)-E####`。

| 前端              | 库目标             | 测试目标                   | 亮点                                                                  |
|-------------------|--------------------|----------------------------|-----------------------------------------------------------------------|
| `frontend_cpp`    | `frontend_cpp`     | `test_frontend_cpp`        | C++20 子集；约束折叠的模板；默认关闭 RTTI。                           |
| `frontend_python` | `frontend_python`  | `test_frontend_python`     | Python 3.11 子集；类型提示尽量降为 IR 类型。                          |
| `frontend_rust`   | `frontend_rust`    | `test_frontend_rust`       | Rust 2024 子集；所有权降为作用域受限的 RC 句柄。                      |
| `frontend_java`   | `frontend_java`    | `test_frontend_java`       | Java 21 子集；sealed class 与 record。                                |
| `frontend_dotnet` | `frontend_dotnet`  | `test_frontend_dotnet`     | C# 12 子集；值类型 struct；`async`/`await` 降为协程。                 |
| `frontend_go`     | `frontend_go`      | `test_frontend_go`         | Go 1.22 子集；goroutine 降到运行时任务调度器。                        |
| `frontend_javascript` | `frontend_javascript` | `test_frontend_javascript` | ES2023 子集；NaN 标记的值；可选 Number → i64 收窄。               |
| `frontend_ruby`   | `frontend_ruby`    | `test_frontend_ruby`       | Ruby 3.3 子集；block 降为 closure。                                   |
| `frontend_ploy`   | `frontend_ploy`    | `test_frontend_ploy`       | 胶水语言；完整文法、泛型、异步、错误处理。                            |
| common            | `frontend_common`  | `test_frontend_common`     | 共享诊断、源码映射与查询机制。                                        |

“前端完整” 的判定：（a）解析文档化子集，（b）IR 通过校验器，
（c）按目录发出诊断，（d）通过 `test_frontend_<lang>` 目标。

### 5.1 C++ 前端

支持 C++20，少数实用限制：

* `<thread>` 与 `<atomic>` 降到运行时任务原语。
* RTTI 默认关闭；按翻译单元用 `// poly: rtti on` 打开。
* 异常展开使用平台原生 unwinder；wasm 上走 JS trap 路径。
* 模板按需实例化；约束折叠在实例化前完成。

`.ploy` 流水线里被引入的示例：

```cpp
// frontends/cpp/examples/sharpen.cpp
#include <vector>
#include <cstdint>

extern "C" std::vector<uint8_t> sharpen(const std::vector<uint8_t> &raw) {
    // Apply a 3x3 sharpening kernel; returns a new buffer.
    std::vector<uint8_t> out(raw.size());
    // ... kernel implementation ...
    return out;
}
```

### 5.2 Python 前端

支持 Python 3.11 文法。可解析的类型提示按其推导出明确 IR 类型；
未标注参数降为 `box<value>`。

```python
# frontends/python/examples/classify.py
from __future__ import annotations
import numpy as np

def classify(image: bytes) -> str:
    arr = np.frombuffer(image, dtype=np.uint8)
    return "cat" if arr.mean() > 127 else "dog"
```

前端通过包管理器探测识别 `numpy`；与之配对的 `IMPORT python PACKAGE
numpy` 声明确保链接期合法。

### 5.3 Rust 前端

Rust 2024 子集；所有权降为作用域受限的 RC 句柄，让 IR 校验器跨语言
统一建模生命周期。`Result<T, E>` 降为 `option<variant<T, E>>`。

### 5.4 Java 前端

Java 21 子集；sealed class 与 record 直接降为 IR `variant`/`struct`。
泛型采用类型擦除并附带一张并行的 reified 表用于跨语言调用。

### 5.5 .NET 前端

C# 12 子集；值类型保持为 IR `struct`。`async`/`await` 降到运行时
协程机制。可空引用类型语义被保留。

### 5.6 Go 前端

Go 1.22 子集；goroutine 调度到运行时任务池，channel 降为运行时
原语。通过注释指令支持 build tag。

### 5.7 JavaScript 前端

ES2023 子集；类型推导无法证明更窄类型时使用 NaN 标记。出现类型反馈或
显式 `| 0` 模式时执行 Number → i64 收窄。

### 5.8 Ruby 前端

Ruby 3.3 子集；block 降为 closure；常见 DSL 模式
（`define_method`、`attr_accessor`）在降级期被识别折叠。

### 5.9 Ploy 前端

参考前端；完整文法 54 关键字。负责包发现、bridge 声明、转换、
流水线、异步与泛型。该前端同时驱动 `polyc --format`。

### 5.10 前端编写约束

* 公共降级 API 接受 `SourceBuffer`，返回 `IRModule`；不允许全局状态。
* 诊断使用 `frontend_common` 的 `Diagnostic`。
* 每个前端必须提供 `tests/unit/frontend_<lang>/` 目标。
* bridge stub 注册经由 `runtime::ffi::Registry`。

---

## 6. 工具与命令行驱动

### 6.1 `polyc` —— 编译器驱动

```
polyc [options] <inputs…> [-o <output>]
  --target=<triple>            x86_64 / aarch64 / wasm32 + os/abi
  --emit=<kind>[:<path>]       ir、asm、obj、call-graph、profile-symbols
  --opt=<level>                O0 | O1 | O2 | O3 | Os | Oz
  --check                      只跑前端 + 校验器，输出 JSON 诊断
  --profile-instrument         插入 __ploy_rt_call_enter/exit 钩子
  --pgo=<mode>                 instrument | use=<profdata>
  --lto=<mode>                 thin | full
  --gc=<algo>                  msweep | tricolor | generational | refcount
  --link=<spec>                透传给 polyld
  --print-passes               打印 pass 流水线后退出
  --format                     对输入运行内置格式化
  --jobs=<N>                   并行前端任务数（默认硬件线程数）
  --trace=<path>               写出整次运行的 Chrome trace JSON
```

示例：

```sh
polyc main.ploy -o main                                          # 默认 O2
polyc main.ploy --opt=O3 --lto=thin -o main                      # 发布构建
polyc main.ploy --target=aarch64-apple-darwin -o main.arm64
polyc --check main.ploy | jq '.diagnostics[].message'
polyc --emit=ir:main.ir --emit=asm:main.s main.ploy
polyc --pgo=instrument main.ploy -o main_inst
polyc --pgo=use=main.profdata main.ploy -o main_pgo
```

退出码：

| 码  | 含义                                       |
|-----|--------------------------------------------|
| 0   | 成功。                                     |
| 1   | 输出错误诊断，不写产物。                   |
| 2   | 用法 / I/O 失败。                          |
| 3   | 内部错误（带堆栈）。                       |

### 6.2 `polyld` —— 链接器

`polyld` 接受 IR 与原生目标输入，跨语言解析符号，写出 ELF / Mach-O
/ PE / WASM 容器。

```
polyld [options] <inputs…> -o <output>
  --target=<triple>     输出三元组
  --shared              产出动态库
  --static              产出静态库
  --entry=<symbol>      覆盖入口
  --map=<path>          写链接 map
  --gc-sections         去除未引用 section
  --lto=<mode>          thin | full
  --bridge-table=<path> 输出 bridge 调用表 JSON
  --verify              死代码剥离后再次跑 IR 校验器
```

macOS arm64 发射器对所有 segment 使用 16 KiB 对齐，并对
`__DATA_CONST` 打上 `SG_READ_ONLY` (`0x10`) 标志，以满足 dyld 26
的 chained-fixup 要求。

### 6.3 `polyasm` —— 汇编器 / 反汇编器

实现完整后端 ISA 表，独立用于手写 fixture，也被 IDE 的二进制查看器
用于反汇编渲染。

```
polyasm <input>                  反汇编（自动识别格式）
polyasm --asm    <input>         产出文本汇编
polyasm --obj    <input>         产出可重定位目标
polyasm --ir     <input>         往返文本 IR ↔ 二进制 IR
polyasm --target=<triple>        覆盖宿主三元组
polyasm --print-isa              打印当前目标 ISA 表
```

### 6.4 `polyopt` —— IR 优化器

包裹中端 pass 管理器的独立驱动，便于 `opt` 风格实验：

```sh
polyopt -O3 -print-after-all in.ir
polyopt -passes='mem2reg,instcombine,gvn' in.ir -o out.ir
polyopt -opt-bisect-limit=42 -print-pass-list in.ir
polyopt -analyse=dominators -print-analysis in.ir
```

### 6.5 `polyrt` —— 运行时 CLI

无 IDE 时使用运行时服务的入口：

```
polyrt profile   --json out.json --duration-ms 5000 <program>
polyrt profile   --stream out.ndjson <program>
polyrt calltrace --json calltrace.json
polyrt gc-stats  --algo=tricolor <program>
polyrt heap-snapshot --out heap.json <program>
polyrt schedule-stats --interval-ms 100 <program>
```

### 6.6 `polybench` —— 基准测试驱动

驱动八个基准套件，写出 JSON 报告（`build/benchmark_*.json`），
仪表盘据此渲染。

```
polybench --suite=compilation   --out build/benchmark_compilation.json
polybench --suite=e2e           --out build/benchmark_e2e.json
polybench --suite=gc            --out build/benchmark_gc.json
polybench --suite=link_times    --out build/benchmark_link_times.json
polybench --suite=optimizations --out build/benchmark_optimizations.json
polybench --suite=comparison    --out build/benchmark_comparison.json
polybench --all                 # 跑全部套件
polybench --baseline=<path>     # 与基线 JSON 对比
```

### 6.7 `polytopo` —— 调用图拓扑查看器

调用分析面板的无头版：

```
polytopo build/mixed.cgjson                   # ASCII 摘要
polytopo --view-mode=dot  build/mixed.cgjson  # Graphviz DOT
polytopo --view-mode=json build/mixed.cgjson  # 机器可读
polytopo --filter-language=python build/mixed.cgjson
polytopo --filter-bridge build/mixed.cgjson   # 仅 bridge 边
polytopo --root=main --depth=4 build/mixed.cgjson
polytopo build/mixed.cgjson | dot -Tsvg > mixed.svg
```

### 6.8 `polyls` —— 语言服务器

LSP 3.17 stdio 服务器，被 `polyui` 与任何第三方编辑器消费。

```
polyls                                  # 通过 stdio 走 LSP
polyls --log polyls.log                 # 记录每个帧
polyls --capabilities                   # 打印支持能力
polyls --root=<path>                    # 覆盖工作区根
```

端到端流程参见 [tutorial/lsp_quickstart_zh.md](tutorial/lsp_quickstart_zh.md)。

### 6.9 `polydoc` —— 文档抽取器

解析每种前端的 `///` 文档注释，写出 HTML / Markdown 包；同时驱动
IDE 中的悬浮摘要。

```
polydoc --in src/ --out docs/api/   --format=md
polydoc --in src/ --out docs/api/   --format=html
polydoc --check  src/               # 检查注释质量
```

### 6.10 `polyver` —— 版本与清单工具

报告编译器、运行时、bridge ABI 版本；按构建清单校验磁盘产物。

```
polyver --version
polyver --abi
polyver --check-manifest build/manifest.json
polyver --emit-manifest  build/manifest.json
```

### 6.11 `polyui` —— IDE 外壳

承载所有交互面板的 Qt 6 桌面应用：编辑器、LSP、DAP、性能分析、
调用分析、测试浏览器、SCM、包管理、通知、书签。

```
polyui                              # 启动并恢复上次会话
polyui <workspace>                  # 在指定工作区启动
polyui --reset-settings             # 丢弃用户设置
polyui --safe-mode                  # 跳过插件与上次会话
```

详见 [第 12 章](#12-idepolyui-与语言服务器)。

---

## 7. 统一 IR

### 7.1 概览

IR 是有类型、SSA、三地址形式，控制流边显式表达。提供两种序列化：
文本 `.ir`（可经 `polyasm --ir` 往返）与二进制 `.ir.bin`（增量缓存
消费）。

### 7.2 类型系统

| 类别       | 类型                                                                |
|------------|---------------------------------------------------------------------|
| 整数       | `i1`、`i8`、`i16`、`i32`、`i64`、`i128`，含有/无符号变体。           |
| 浮点       | `f16`、`f32`、`f64`、`f128`。                                        |
| 向量       | `<N x T>` SIMD 向量（wasm SIMD 与 x86_64 AVX 路径使用）。             |
| 聚合       | `struct {…}`、`array<T, N>`、`tuple<…>`。                            |
| 引用       | `ptr<T, addrspace>`、`ref<T>`（GC 跟踪）、`weak<T>`。                |
| 和         | `option<T>`、`variant<T0, T1, …>`。                                  |
| 函数       | `fn(args…) -> T`、`async fn(args…) -> T`。                          |
| 装箱       | `box<T>` 用于跨语言传值。                                            |

### 7.3 核心指令

| 类别       | 示例                                                                |
|------------|---------------------------------------------------------------------|
| 算术       | `add`、`sub`、`mul`、`sdiv/udiv`、`srem/urem`、`fadd…`               |
| 位运算     | `and`、`or`、`xor`、`shl`、`lshr`、`ashr`、`popcnt`、`clz`           |
| 内存       | `alloc`、`load`、`store`、`gep`、`memcpy`、`memset`                  |
| 控制       | `br`、`cbr`、`switch`、`ret`、`unreachable`、`landingpad`            |
| 调用       | `call`、`tailcall`、`invoke`、`bridge_call`（跨语言）                |
| GC         | `gc.alloc`、`gc.barrier.write`、`gc.safepoint`                       |
| 异步       | `async.suspend`、`async.resume`、`async.await`                       |
| SIMD       | `vec.add`、`vec.mul`、`vec.shuffle`、`vec.broadcast`                 |
| 转换       | `trunc`、`sext`、`zext`、`bitcast`、`fptosi`、`sitofp`               |

### 7.4 文本形式

```
fn main() -> i32 {
entry:
    %0 = call @factorial(i64 10)
    %1 = call @print_i64(i64 %0)
    ret i32 0
}

fn factorial(i64 %n) -> i64 {
entry:
    %cond = icmp.sle i64 %n, 1
    cbr i1 %cond, base, recurse
base:
    ret i64 1
recurse:
    %n1   = sub i64 %n, 1
    %r    = call @factorial(i64 %n1)
    %res  = mul i64 %n, %r
    ret i64 %res
}
```

### 7.5 调用约定

* **System V AMD64**：Linux/macOS x86_64。
* **Win64**：Windows x86_64。
* **AAPCS64**：Linux/macOS aarch64。
* **wasm-32**：多值返回。
* **Bridge ABI**：跨语言调用通过 `runtime::ffi::BridgeFrame` 编排，
  让被调用语言运行时仍然看到惯例值。

### 7.6 校验器规则

校验器（`middle/verifier`）会拒绝以下 IR 模块：

* 在定义之前使用 SSA 值；
* 引用未声明块或函数；
* 在单个 `gep` 中混用指针地址空间；
* 跨语言调用未走 `bridge_call`；
* 同一路径上两次 GC 分配之间漏写 `gc.safepoint`；
* 在非 `async fn` 中出现 `async.suspend`。

校验失败时编译以 `polyc-err-E0801` 中止。

### 7.7 二进制序列化

`.ir.bin` 采用长度前缀、内容寻址格式：

```
magic:    "POLI"  (4 bytes)
version:  u32     (current = 4)
strtab:   compressed string pool
typetab:  type interning table
fns:      one record per function (header + blocks + insts)
metadata: source map + DI + bridge index
crc32:    over everything above
```

格式向后兼容：旧版 `polyc` 遇到更新模块以 `polyc-err-E0810` 拒绝
而非误解码。

---

## 8. 中端优化

### 8.1 Pass 管理器

带依赖感知的调度器，区分三种 pass：**模块**、**函数**、**循环**。
各分析跨 pass 缓存结果，破坏保留集时自动失效。

### 8.2 默认 `-O2` 流水线

`mem2reg → instcombine → simplifycfg → gvn → licm → loop-unroll →
inliner → tailcall → dse → adce → late-instcombine → simplifycfg`。

`-O3` 增加 `loop-vectorize`、`slp-vectorize` 与第二轮 inliner；
`-Os/-Oz` 用尺寸感知 inliner 替换默认实现，并启用 `merge-functions`
与 `cold-cc`。

### 8.3 Pass 目录

| Pass               | 类别     | 说明                                                  |
|--------------------|----------|-------------------------------------------------------|
| `mem2reg`          | 函数     | alloca 提升为 SSA 寄存器。                            |
| `instcombine`      | 函数     | 窥孔简化。                                            |
| `simplifycfg`      | 函数     | 合并基本块、折叠平凡分支。                            |
| `gvn`              | 函数     | 全局值编号 + PRE。                                    |
| `licm`             | 循环     | 循环不变量外提。                                      |
| `loop-unroll`      | 循环     | 启发式 + pragma 展开。                                |
| `loop-vectorize`   | 循环     | 感知 SLP 的向量化器；使用 `<N x T>`。                 |
| `inliner`          | 模块     | 成本模型 inliner；感知 PGO。                          |
| `tailcall`         | 函数     | 标记可尾调用；后端降级。                              |
| `dse`              | 函数     | 死存储消除。                                          |
| `adce`             | 函数     | 激进死代码消除。                                      |
| `merge-functions`  | 模块     | 合并相同函数体。                                      |
| `cold-cc`          | 模块     | 冷路径使用 cold 调用约定。                            |
| `bridge-fuse`      | 模块     | 合并相邻同语言 `bridge_call`。                        |
| `gc-strip`         | 模块     | 去除可证明无用的 GC barrier。                         |

### 8.4 PGO

```sh
polyc --pgo=instrument hello.cpp -o hello_inst
./hello_inst   # 写出 default.profraw
llvm-profdata merge -o default.profdata default.profraw
polyc --pgo=use=default.profdata hello.cpp -o hello_pgo
```

PGO 数据被 inliner、分支概率分析与 wasm 后端的冷热分离消费。

### 8.5 LTO

`polyc --lto=thin` 与 `--lto=full` 启用整程序优化。Thin LTO 保留每
模块摘要并并行代码生成。

### 8.6 编写 Pass

```cpp
// middle/passes/example_pass.cpp
#include "poly/middle/pass.h"

namespace poly::middle {

class ExamplePass : public FunctionPass {
public:
    static constexpr std::string_view ID = "example";
    bool Run(Function &fn, FunctionAnalyses &analyses) override {
        bool changed = false;
        for (auto &block : fn) {
            for (auto it = block.begin(); it != block.end(); ) {
                if (IsRedundant(*it)) {
                    it = block.Erase(it);
                    changed = true;
                } else {
                    ++it;
                }
            }
        }
        return changed;
    }
    void GetAnalysisUsage(AnalysisUsage &au) const override {
        au.AddRequired<DominatorTree>();
        au.SetPreservesCFG();
    }
};

REGISTER_PASS(ExamplePass);

}  // namespace poly::middle
```

之后在 `middle/pipeline_defaults.cpp` 把 pass id 加进所需流水线层级，
并在 [realization/middle_passes.md](realization/middle_passes.md) 中
写文档。

### 8.7 检查流水线

```
polyopt -print-passes              # 列出 pass id
polyopt -print-after=instcombine   # 打印 pass 后的 IR
polyopt -opt-bisect-limit=42       # 二分定位回归
polyopt -time-passes               # 打印各 pass 墙钟时间
```

---

## 9. 运行时

### 9.1 垃圾回收器

| 算法           | 头文件                                | 说明                                       |
|----------------|---------------------------------------|--------------------------------------------|
| 标记—清扫     | `runtime/gc/mark_sweep.h`             | Stop-the-world，内存开销低。               |
| 三色          | `runtime/gc/tricolor.h`               | 默认；并发标记；停顿更短。                 |
| 分代          | `runtime/gc/generational.h`           | 新/老分代，写屏障。                        |
| 引用计数      | `runtime/gc/refcount.h`               | 配套环检测器。                             |

可在构建时（`--gc=<algo>`）或运行时（`POLY_GC` 环境变量）选择；
统计：`polyrt gc-stats`。

#### 9.1.1 调优开关

| 环境变量              | 含义                                                  |
|-----------------------|-------------------------------------------------------|
| `POLY_GC`             | 进程启动时选择算法。                                  |
| `POLY_GC_HEAP_INIT`   | 初始堆大小（如 `64m`）。                              |
| `POLY_GC_HEAP_MAX`    | 硬上限；超出则 OOM。                                  |
| `POLY_GC_PAUSE_GOAL_MS` | 三色与分代算法的停顿时间目标。                       |
| `POLY_GC_LOG`         | `none` / `summary` / `verbose`。                      |
| `POLY_GC_TRACE`       | GC 事件 Chrome trace JSON 输出路径。                  |

### 9.2 FFI bridge

`runtime/ffi/` 实现 `BridgeFrame`，所有跨语言调用共用的编排记录。
每种语言运行时为其原生值表示注册装箱/拆箱函数对。

```cpp
// runtime/ffi/bridge_frame.h
struct BridgeFrame {
    LanguageId   from;
    LanguageId   to;
    Span<Value>  args;
    Value        result;
    Diagnostic  *diag;   // out parameter
};
```

bridge 调用点示例：

```
%boxed = bridge_call @python::ml::classify, box<bytes> %img
```

### 9.3 各语言运行时

`runtime/lang/{cpp,py,rust,java,dotnet,go,js,ruby,ploy}/` 提供前端在
运行时所需的最小机制：异常 unwinder、异步调度器、值装箱、内建辅助。
各运行时暴露 `Init(Host *host)` 与 `Shutdown()`，由主程序的开头/末尾
调用。

### 9.4 服务

* **性能分析钩子**（`__ploy_rt_call_enter/exit`）—— 由
  `__ploy_rt_call_trace_enable` 切换。
* **任务调度器** —— work-stealing 池，被 `async`/`await`、Go 前端的
  goroutine、并行 pass 共用；用 `POLY_TASKS_THREADS` 配置。
* **遥测** —— 选择性开启的计数器，经 `polytelemetry` 路由；默认关闭。
  详见 [realization/telemetry_zh.md](realization/telemetry_zh.md)。
* **stdout 流水线** —— IDE 控制台所用的 stdout/stderr 多路捕获，详见
  [realization/runtime_stdout_pipeline.md](realization/runtime_stdout_pipeline.md)。

### 9.5 调试信息

ELF/Mach-O 上为 DWARF 5，PE 上为 CodeView，WebAssembly 上同时输出
`name` 段与 DWARF 段。Ploy 行指令拥有源码映射，调试器可在原始
`.ploy` 文件单步，即便执行点位于宿主语言栈帧。

### 9.6 异常

每种运行时实现 `Throw(Value)` 与 `Catch(Type)`，IR 通过
`landingpad` 与 `invoke` 表达。跨语言异常以装箱 `Value` 形式传播；
接收语言的运行时决定是按原生重新抛出还是返回错误结果。

---

## 10. 后端

### 10.1 x86_64

目标 System V AMD64（Linux + macOS）与 Win64（Windows）。机会式
启用 AVX2，仅在 `-mavx512f` 下启用 AVX-512。复用通用寄存器分配器
与指令选择器。

#### 10.1.1 寄存器类

| 类别 | 寄存器                                                         |
|------|----------------------------------------------------------------|
| GPR  | rax、rcx、rdx、rbx、rsi、rdi、rsp、rbp、r8…r15。               |
| XMM  | xmm0…xmm15（AVX-512 下扩展为 xmm16…xmm31）。                   |
| K    | k0…k7（mask 寄存器，AVX-512）。                                |

### 10.2 ARM64

AAPCS64 ABI；Linux ELF 与 macOS Mach-O 容器。macOS Mach-O 目标使用
16 KiB 段对齐，并为 `__DATA_CONST` 打 `SG_READ_ONLY (0x10)`，以满足
dyld 26 的 chained-fixup 约束——缺少该标志会被 macOS 26 arm64 上的
loader 拒绝。

#### 10.2.1 寄存器类

| 类别 | 寄存器                |
|------|------------------------|
| X    | x0…x30、sp。           |
| V    | v0…v31（NEON）。       |

### 10.3 WebAssembly

目标 MVP 加 SIMD-128、bulk memory、reference-types 提案。产出符合
WASM 1.0 的模块，并附带 `.wasm.map` 源码映射。兼容 `wasmtime`、
`wasmer` 与浏览器。

特性开关：

| 开关             | 效果                                                       |
|------------------|------------------------------------------------------------|
| `--simd`         | 启用 SIMD-128 opcode。                                     |
| `--threads`      | 启用共享内存 + 原子。                                      |
| `--bulk-memory`  | 启用 `memory.copy` / `memory.fill`。                       |
| `--reference-types` | 启用 `externref` 与 table.grow。                       |
| `--gc`           | 启用 GC 提案 opcode（预览）。                              |

### 10.4 交叉编译

```sh
polyc hello.cpp --target=aarch64-linux-gnu      -o hello.aarch64
polyc hello.cpp --target=x86_64-pc-windows-msvc -o hello.exe
polyc hello.cpp --target=wasm32-wasi            -o hello.wasm
polyc hello.cpp --target=aarch64-apple-darwin   -o hello.macho
```

非宿主三元组需配置 sysroot；详见
[realization/cross_compilation.md](realization/cross_compilation.md)。

---

## 11. 链接器与目标格式

### 11.1 容器

| 容器     | 平台                       | 备注                                                              |
|----------|----------------------------|-------------------------------------------------------------------|
| ELF      | Linux x86_64 / aarch64     | 标准重定位 + GNU 风格 `.note.GNU-stack`。                          |
| Mach-O   | macOS x86_64 / arm64       | arm64 上 16 KiB 对齐；`__DATA_CONST` 上打 `SG_READ_ONLY`。         |
| PE/COFF  | Windows x86_64             | 由 `pe_smoke` 校验；默认 `/SUBSYSTEM:CONSOLE`。                    |
| WASM     | wasm32-wasi / wasm32-unknown | imports 按模块名分组；SIMD opcode 由 `--simd` 控制。              |

### 11.2 符号解析

`polyld` 维护一张按 mangled name + 语言键索引的全局符号表。跨语言
调用走 bridge ABI；链接器校验每个 `bridge_call` 在目标语言运行时里
都有已注册对端。

mangling 方案：

```
_PLY<lang>_<module>_<func>_<argtypes>
```

其中 `<lang>` 为 `c`、`p`、`r`、`j`、`n`、`g`、`s`、`b`、`y`，对应
9 种支持语言。完整文法见
[specs/symbol_mangling.md](specs/symbol_mangling.md)。

### 11.3 增量缓存

`build/.cache/` 存放按内容哈希索引的 `.ir.bin` 与 `.obj`。默认策略：
源码哈希、目标三元组、优化等级、pass 流水线全部相同时复用缓存条目。
LRU 淘汰，上限由 `POLY_CACHE_MAX_BYTES`（默认 4 GiB）控制。

### 11.4 Bridge 调用表

`polyld --bridge-table=<path>` 写出每个 bridge 调用点、源语言、目标
语言与目标符号的 JSON 清单。IDE 调用分析面板消费同一文件。

### 11.5 链接脚本

`polyld --script=<path>` 支持小型链接脚本子集：

```
SECTIONS {
    .text   : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }
    .bss    : { *(.bss*) }
}
```

---

## 12. IDE（`polyui`）与语言服务器

### 12.1 组成

* `polyui` —— Qt 6 外壳。
* `polyls` —— Ploy 的 stdio LSP 服务器，并按语言分派第三方服务器。
* `IdeLspBridge` —— 将编辑器事件翻译为 LSP 消息的 IDE 端适配器，
  以 200 ms 节流。
* `IdeDapBridge` —— 适配 `lldb-dap`、`debugpy`、`delve`、
  `netcoredbg` 与 node-debug2 的 DAP 适配器。

### 12.2 默认 LSP 服务器映射

| 语言         | 默认 `command`                          |
|--------------|-----------------------------------------|
| ploy         | `polyls`                                |
| cpp          | `clangd`                                |
| python       | `pyright-langserver --stdio`            |
| rust         | `rust-analyzer`                         |
| java         | `jdtls`                                 |
| dotnet       | `csharp-ls`                             |
| go           | `gopls`                                 |
| javascript   | `typescript-language-server --stdio`    |
| ruby         | `solargraph stdio`                      |

可在 **设置 → 语言服务器** 覆盖。

### 12.3 `polyui` 内部工作流

1. 打开文件 → `IdeLspBridge` 启动对应服务器并发送 `initialize` →
   `initialized` → `didOpen`。
2. 200 ms 节流窗口内的编辑合并为单条 `didChange`。
3. 入站 `publishDiagnostics` 在沟里 / 问题面板渲染。
4. `Ctrl+Click` → `textDocument/definition`；`Shift+F12` →
   `references`；`F2` → `rename`。
5. 退出时对每个活跃服务器发送 `shutdown` + `exit`。

### 12.4 检查通信

底部 **LSP** 停靠面板（`Ctrl+Alt+L` 切换）记录双向所有消息；
`polyls --log polyls.log` 把同样的帧落盘。

### 12.5 面板一览

| 面板                | 切换             | 用途                                          |
|---------------------|------------------|-----------------------------------------------|
| 问题                | `Ctrl+Shift+M`   | 跨服务器聚合诊断。                            |
| 性能分析            | `Ctrl+Alt+P`     | 火焰、热点、时间轴、语言。                    |
| 调用分析            | `Ctrl+Alt+G`     | 静态调用图 + 运行时叠加。                     |
| 测试浏览器          | `Ctrl+Alt+T`     | CTest 树 + 内联运行/调试。                    |
| 包管理              | `Ctrl+Alt+M`     | 各语言依赖图 + 漏洞扫描。                     |
| 通知                | 铃铛图标         | 持久化通知。                                  |
| 书签                | `Ctrl+Alt+K`     | 工作区行级书签。                              |
| TODO/FIXME 索引     | `Ctrl+Alt+J`     | 后台扫描的标签索引。                          |
| 编译流水线          | `Ctrl+Alt+I`     | `polyc` 运行的 Chrome trace 视图。            |
| IR 查看器           | `Ctrl+Alt+R`     | 文本 IR 并排 + diff。                          |
| 汇编查看器          | `Ctrl+Alt+A`     | 后端汇编与源码映射。                          |
| 拓扑实时            | `Ctrl+Alt+Y`     | 当前示例拓扑实时面板。                        |
| Bridge 面板         | `Ctrl+Alt+B`     | 跨语言 bridge 调用检查。                      |
| 编排视图            | `Ctrl+Alt+W`     | FFI 值编排单步。                              |
| 测试覆盖率          | `Ctrl+Alt+V`     | 编辑器沟里覆盖率叠加。                        |

### 12.6 编辑器特性

* 多光标、列选择、多 tab 编辑。
* Quick Open（`Ctrl+P`）、Symbol Search（`Ctrl+T`）、全局文本搜索
  （`Ctrl+Shift+F`）。
* 折叠、格式化（`Shift+Alt+F`）、EditorConfig 支持。
* 各 LSP 服务器提供的语义高亮。
* 内联诊断、code action（`Ctrl+.`）、重构（重命名、抽取、内联）。
* 括号配对染色。

### 12.7 SCM

内置 git 客户端：diff 视图、blame 沟、三方合并解决器、PR review
（GitHub + GitLab）。认证委托给操作系统钥匙串。

### 12.8 调试（DAP）

`F5` 启动当前 Run/Debug profile；DAP bridge 按文件语言挑选适配器。
支持断点、watch、step in/out、条件断点、热重载（Python、.NET、JS、
Java）以及内联值显示。

### 12.9 任务与 Run/Debug 选择器

`Ctrl+Shift+B` 选择构建任务；`F5` 选择调试 profile。Profile 存放于
`.polyui/launch.json`，遵循 VS Code DAP 形态，原有片段可直接复用。

### 12.10 测试浏览器

每个 CTest 目标的树视图，外加各语言测试发现器（pytest、gtest、junit、
xunit、mocha、rspec）。支持内联运行/调试、覆盖率叠加、历史。详见
[realization/test_explorer_zh.md](realization/test_explorer_zh.md)。

### 12.11 包管理视图

每语言的依赖图，叠加基于 OSV 数据的漏洞扫描。更新与 lock 文件编辑
作为 code action 暴露。

### 12.12 跨语言导航

对 `LINK` 目标 `Ctrl+Click` 直接跳到宿主语言源码。**Bridge 面板**
（`Ctrl+Alt+B`）汇总工作区所有 bridge 调用。

### 12.13 编译流水线检查器

可视化 `polyc --trace` 写出的 Chrome trace JSON。每个前端、pass、
后端阶段显示为彩色 span。

### 12.14 示例 / 教程浏览器

欢迎页暴露 `tests/samples/` 下所有示例与 `docs/tutorial/` 下的所有
教程。**拓扑实时** 面板渲染当前示例的 bridge 图。

### 12.15 远程开发

支持 SSH、WSL、容器与 dev container 后端；IDE 把文件操作与进程启动
转发到远端，同时把 LSP / DAP 适配器留在本地。配置见
[realization/remote_dev_zh.md](realization/remote_dev_zh.md)。

### 12.16 AI 助手

聊天、行内建议、重构，以及把请求锁在本地模型的隐私模式。提供方通过
插件 SDK 可插拔。

### 12.17 协作

PR review、行内评论、issue 浏览，以及 Live Share 风格的协作编辑。

### 12.18 扩展、市场与工作区

插件从 `<config>/polyui/plugins/` 加载；市场从
`https://marketplace.example.invalid/polyui/` 拉取。工作区是含
`.polyui/` 目录的文件夹，固定设置、launch、扩展。

### 12.19 Shell 特性

欢迎页、可定制状态栏（含 9 个内置位 `branch`、`problems`、`language`、
`language_server`、`encoding`、`eol`、`indent`、`package_manager`、
`profiler`）、最近文件（`Ctrl+R`）、最近工作区（`Ctrl+E`）、完整
会话恢复。详见 [tutorial/shell_zh.md](tutorial/shell_zh.md)。

### 12.20 国际化、可访问性、遥测

UI 字符串通过 `lupdate` 抽取；默认随附英文与简体中文。可访问性包括
高对比度主题、纯键盘导航与屏幕阅读器提示。遥测选择性开启，每类事件
单独文档化。

### 12.21 文件类型查看器

图像查看器（PNG/JPEG/WebP/GIF/SVG/BMP）、对 ≥ 1 GiB 文件分块 I/O 的
hex 查看器、覆盖 ELF/PE/Mach-O/WASM 的二进制检查器，以及含 SQL 控制台
的 SQLite 客户端。详见 [tutorial/viewers_zh.md](tutorial/viewers_zh.md)。

### 12.22 键盘快捷键速查

| 操作                    | 快捷键            |
|-------------------------|-------------------|
| Quick Open              | `Ctrl+P`          |
| Symbol search           | `Ctrl+T`          |
| 全局文本搜索            | `Ctrl+Shift+F`    |
| 格式化文档              | `Shift+Alt+F`     |
| Code action             | `Ctrl+.`          |
| 重命名符号              | `F2`              |
| 跳转定义                | `F12`             |
| 查找引用                | `Shift+F12`       |
| 切换问题面板            | `Ctrl+Shift+M`    |
| 切换 LSP 面板           | `Ctrl+Alt+L`      |
| 切换性能分析            | `Ctrl+Alt+P`      |
| 切换调用分析            | `Ctrl+Alt+G`      |
| 切换测试浏览器          | `Ctrl+Alt+T`      |
| 切换包管理              | `Ctrl+Alt+M`      |
| 切换书签                | `Ctrl+Alt+K`      |
| 切换 TODO 索引          | `Ctrl+Alt+J`      |
| 最近文件                | `Ctrl+R`          |
| 最近工作区              | `Ctrl+E`          |
| 运行 / 调试             | `F5`              |
| 构建任务                | `Ctrl+Shift+B`    |
| 单步跳过                | `F10`             |
| 单步进入                | `F11`             |

---

## 13. 诊断、性能分析与调用分析

### 13.1 诊断标识符

每条诊断都有稳定的 `polyc-(err|warn)-<E####|W####>` 标识，完整目录
见 [specs/ploy_diagnostics.md](specs/ploy_diagnostics.md)。

严重度映射：

| 严重度  | LSP DiagnosticSeverity | 行为                                                |
|---------|------------------------|-----------------------------------------------------|
| Error   | 1                      | 中止编译；沟里红标。                                |
| Warning | 2                      | 编译继续；沟里黄标。                                |
| Info    | 3                      | 信息提示。                                          |
| Hint    | 4                      | 编辑器提示（如未使用变量）。                        |

### 13.2 问题面板

跨所有语言服务器聚合的实时诊断。可按严重度、文件子串或正则过滤；
双击跳转。后台扫描器对超过 2 000 文件的工作区按每 50 ms 50 文件
分批处理。快速入门：
[tutorial/problems_panel_quickstart_zh.md](tutorial/problems_panel_quickstart_zh.md)。

### 13.3 性能分析器

工作流：

```sh
polyc --profile-instrument main.ploy -o build/main \
      --emit=call-graph:build/main.cgjson \
      --emit=profile-symbols:build/main.symjson
polyrt profile --json build/main.profile.json --duration-ms 2000 build/main
```

在性能分析面板里打开 `build/main.profile.json`。Tab：

* **Flame** —— 按调用栈的火焰图。
* **Hotspots** —— 按 self-time 的 top-N 符号。
* **Timeline** —— 每线程 Gantt，含 GC 与 bridge 事件。
* **Languages** —— 按宿主语言聚合 self-time，外加用于 FFI 开销的
  虚拟 `bridge` 语言。

NDJSON 流式变体（`--stream`）驱动 IDE 实时尾追。快速入门：
[tutorial/profiling_quickstart_zh.md](tutorial/profiling_quickstart_zh.md)。

### 13.4 调用分析

加载 `--emit=call-graph` 产出的 `*.cgjson`。特性：

* BFS-最长路径列布局。
* 语言对过滤列表。
* 任意两函数间有界 DFS 路径搜索。
* 性能分析活跃时叠加运行时调用计数。
* 双击节点跳转源码。

无头等价：`polytopo`。快速入门：
[tutorial/call_analyzer_quickstart_zh.md](tutorial/call_analyzer_quickstart_zh.md)。

### 13.5 Bridge 检查器

Bridge 面板列出每个跨语言调用点、源/目标符号、所用编排规则与采样
后的值预览。FFI 签名不匹配时调试此面板很有用。

### 13.6 堆快照

`polyrt heap-snapshot --out heap.json <program>` 把 GC 堆导出为
JSON 树，与 IDE 堆查看器兼容。该查看器支持两个快照对 diff 找泄漏。

---

## 14. 测试、示例与持续集成

### 14.1 CTest 目标（共 30 个）

```
unit_tests             test_core              test_middle
test_backends          test_runtime           test_linker
test_lsp               test_polyls            test_completion_ranker
test_plugins           test_problems          test_settings
test_topology          test_topology_ui       test_e2e
integration_tests      benchmark_tests        pe_smoke
test_frontend_common
test_frontend_cpp      test_frontend_python   test_frontend_rust
test_frontend_java     test_frontend_dotnet   test_frontend_go
test_frontend_javascript test_frontend_ruby   test_frontend_ploy
samples_smoke          docs_lint              docs_sync
```

### 14.2 跑测试

```sh
ctest --test-dir build -j --output-on-failure
ctest --test-dir build -R "frontend"            # 按名子集
ctest --test-dir build -R "benchmark" -V        # 详细
ctest --test-dir build -L "fast"                # 按 label
ctest --test-dir build --rerun-failed           # 仅重跑失败
ctest --test-dir build -T memcheck              # 在 valgrind 下
```

### 14.3 示例之旅（42 编号 + 变体）

`tests/samples/00_minimal` … `tests/samples/41_grammar_polish` 构成
语言与工具的渐进之旅。每个示例都有自己的 `README` 并被
`samples_smoke` 目标覆盖。

| 示例                                  | 演示                                          |
|---------------------------------------|-----------------------------------------------|
| `00_minimal`                          | 单 `.ploy` 文件，无宿主导入。                 |
| `01_basic_linking` / `_v2`            | 通过 `LINK` 跨语言链接。                      |
| `02_struct_types`                     | 聚合类型与模式匹配。                          |
| `03_generic_functions`                | 受约束泛型。                                  |
| `04_error_handling`                   | `TRY` / `CATCH` 与传播。                      |
| `05_async_basics`                     | `ASYNC` / `AWAIT` 基础。                      |
| `06_pipelines`                        | `PIPELINE` 语法。                             |
| `07_io_and_files`                     | 标准 I/O bridge。                             |
| `08_collections`                      | 列表 / map / set 跨语言桥接。                 |
| `09_mixed_pipeline`                   | C++ + Python 流水线驱动。                     |
| `10_rust_serde`                       | Rust serde 从 Ploy 桥接。                     |
| `11_java_records`                     | Java record 映射到 IR `struct`。              |
| `12_dotnet_async`                     | C# async 降到运行时调度器。                   |
| `13_go_concurrency`                   | goroutine 与 channel。                        |
| `14_javascript_promises`              | JS Promise 与 Ploy `ASYNC` 互通。             |
| `15_async_await`                      | 异步函数与任务调度器。                        |
| `16_ruby_blocks`                      | Ruby block 降为 closure。                     |
| `17_optional_match`                   | `OPTION` / `MATCH` 穷尽。                     |
| `18_extended_strings`                 | `r"…"`、`b"…"`、`f"…"` 字面量。               |
| `19_visibility`                       | `PUBLIC` / `PRIVATE` / `INTERNAL`。           |
| `20_class_bridge`                     | 跨语言类实例化。                              |
| `21_polydoc`                          | 文档注释抽取。                                |
| `22_database_access`                  | SQLite 客户端 + SQL 控制台。                  |
| `23_http_client`                      | 跨语言 HTTP 客户端。                          |
| `24_grpc_service`                     | gRPC 服务端到端。                             |
| `25_simd_kernels`                     | x86_64 / arm64 上的 SIMD 内联。               |
| `26_pgo_demo`                         | PGO instrument + use 循环。                   |
| `27_lto_demo`                         | thin / full LTO。                             |
| `28_gc_tuning`                        | GC 算法对比。                                 |
| `29_plugin_panel`                     | 插件 SDK 示例。                               |
| `30_wasm_build`                       | WebAssembly 端到端构建。                      |
| `31_wasm_simd`                        | WASM SIMD-128。                               |
| `32_remote_dev`                       | 远程开发容器。                                |
| `33_dap_walkthrough`                  | 跨语言 DAP 调试。                             |
| `34_test_explorer`                    | 测试浏览器集成。                              |
| `35_coverage`                         | 覆盖率构建与覆盖率面板。                      |
| `36_profiler_streaming`               | 实时 profile 流。                             |
| `37_call_analyzer_paths`              | 调用分析面板路径搜索。                        |
| `38_heap_snapshot`                    | 堆快照与 diff。                               |
| `39_telemetry_optin`                  | 遥测选择性开启流程。                          |
| `40_release_packaging`                | 端到端发布打包。                              |
| `41_grammar_polish`                   | 最新文法精修回归。                            |

### 14.4 基准

`build/benchmark_*.json` —— `compilation`、`e2e`、`gc`、`link_times`、
`optimizations`、`comparison`。由 `polybench` 驱动；CI 按回归阈值守门。
结果在 IDE 基准仪表盘渲染。

### 14.5 CI 质量门

| 门                          | 行为                                                       |
|-----------------------------|------------------------------------------------------------|
| 构建矩阵                    | macOS arm64、Ubuntu x86_64、Windows x86_64。               |
| `ctest` 全量                | 30 个目标必须全部通过。                                    |
| Sanitizer 构建              | `unit_tests` 与 `test_runtime` 在 ASan + UBSan 下运行。    |
| TSan 构建                   | `test_runtime` 与 `samples_smoke` 在 TSan 下每周一次。     |
| 覆盖率                      | `unit_tests` 覆盖率构建；阈值 ≥ 80 %。                     |
| `docs_lint` / `docs_sync`   | 双语结构与路径检查。                                       |
| `samples_smoke`             | 全部 42 示例可构建并运行。                                 |
| 基准回归                    | `benchmark_comparison.json` 偏离基线 ≤ 5 %。              |

### 14.6 本地预检脚本

```sh
scripts/preflight.sh
```

在一分钟内跑完 `clang-format --dry-run`、`clang-tidy`、
`docs_lint.py`、`docs_sync_check.py` 与一组快速 CTest 目标。

---

## 15. 构建系统与依赖

### 15.1 顶层 CMake 目标

```sh
cmake --build build --target polyc polyld polyui polyls   # 核心工具
cmake --build build --target unit_tests                   # 单 CTest 目标
cmake --build build --target docs                         # 文档包
cmake --build build --target package                      # 二进制发布
cmake --build build --target install                      # 本地安装
cmake --build build --target benchmark_tests              # 基准可执行
```

### 15.2 依赖清单

第三方库声明在 [Dependencies.cmake](Dependencies.cmake)。通过
`FetchContent_Declare` 拉取；按 tag + SHA 锁定。任意一项漂移 CI 都会
失败。主要依赖：

| 库               | 用途                                                            |
|------------------|-----------------------------------------------------------------|
| `spdlog`         | 内部日志（日志字符串只能为英文）。                              |
| `nlohmann/json`  | 诊断、profile 流、插件清单的 JSON I/O。                          |
| `Catch2`         | 单元测试框架。                                                  |
| `Qt6`            | IDE 外壳。                                                      |
| `lldb`           | 原生调试 DAP 适配器。                                           |
| `zstd`           | `.ir.bin` 与增量缓存的压缩。                                    |
| `re2`            | 问题面板里诊断消息正则匹配。                                    |

### 15.3 Sanitizer / 覆盖率构建

```sh
cmake -S . -B build-asan -G Ninja -DPOLY_SANITIZE=address
cmake -S . -B build-ubsan -G Ninja -DPOLY_SANITIZE=undefined
cmake -S . -B build-tsan -G Ninja -DPOLY_SANITIZE=thread
cmake -S . -B build-cov  -G Ninja -DPOLY_COVERAGE=ON
```

### 15.4 容器镜像

`docker/` 提供可复现的 Ubuntu 22.04 与 Alpine 3.19 镜像，CI 使用。
每个镜像都预装宿主工具链与各前端所需的可选语言 SDK。

```sh
docker build -t polyglot/ubuntu-ci -f docker/ubuntu-ci.Dockerfile .
docker run --rm -it -v "$PWD":/work polyglot/ubuntu-ci \
       cmake -S /work -B /work/build-docker -G Ninja
```

### 15.5 版本

项目版本的唯一真源是根 [CMakeLists.txt](CMakeLists.txt)
（`project(PolyglotCompiler VERSION 1.45.2)`）。所有版本变更必须
触动该行；`scripts/bump_version.py` 强制此规则。

```sh
python scripts/bump_version.py --part=patch   # 1.45.2 → 1.45.3
python scripts/bump_version.py --part=minor   # 1.45.2 → 1.46.0
python scripts/bump_version.py --part=major   # 1.45.2 → 2.0.0
```

该脚本同时更新根 `CMakeLists.txt`、内嵌于 `polyver` 的版本与双语
变更日志头。

### 15.6 安装布局

`cmake --install build --prefix /opt/polyglot` 产出：

```
/opt/polyglot/
├── bin/    polyc, polyld, polyasm, polyopt, polyrt, polybench,
│           polytopo, polyls, polydoc, polyver, polyui
├── lib/    libpoly_runtime.* libpoly_middle.* …
├── include/poly/...
├── share/polyglot/samples/
└── share/doc/polyglot/  (HTML 渲染的 docs/)
```

---

## 16. 扩展编译器

### 16.1 新增 Ploy 关键字

1. 扩展 `frontends/ploy/lexer.cpp` 的词法表。
2. 在 `frontends/ploy/parser.cpp` 添加文法产生式。
3. 在 `frontends/ploy/lower.cpp` 把它降到现有 IR。
4. 在
   [tutorial/ploy_language_tutorial.md](tutorial/ploy_language_tutorial.md)
   及 ZH 对应文档记录关键字。
5. 在 `tests/samples/` 增加示例。
6. 在 `tests/unit/frontend_ploy/` 增加前端测试。

### 16.2 新增包管理器

1. 在 `frontends/common/package_discovery.cpp` 实现检测。
2. 在 `tests/integration/package_managers/` 加入 fixture 项目。
3. 同步本指南 [§ 4.4](#44-包管理器自动发现) 与 ZH 对应内容。
4. 若需新增 SDK，更新 CI fixture 镜像。

### 16.3 新增后端特性开关

1. 在对应 `backends/<arch>/features.cpp` 表中加入开关。
2. 接通代码生成分支。
3. 加入触发该特性的 e2e 测试。
4. 在 [第 10 章](#10-后端) 中记录。

### 16.4 新增中端 Pass

代码骨架见 [§ 8.6](#86-编写-pass)。然后：

1. 在 `middle/pipeline_defaults.cpp` 注册 pass id。
2. 在 `tests/unit/middle/passes/` 增加单元测试。
3. 在 [§ 8.3](#83-pass-目录) 与
   [realization/middle_passes.md](realization/middle_passes.md)
   记录 pass。
4. 若改动默认 O2 流水线，更新基准基线。

### 16.5 新增前端

1. 创建 `frontends/<lang>/`，标准布局
   （`include/`、`src/`、`tests/`、`examples/`）。
2. 实现 `Lower(SourceBuffer) → IRModule`。
3. 在 `frontends/common/extension_table.cpp` 注册扩展名。
4. 增加 `test_frontend_<lang>` CTest 目标。
5. 在 `tests/samples/` 至少加入一个示例。
6. 同步本指南第 5、6 章及 ZH 对应内容。

### 16.6 新增目标三元组

1. 在 `backends/<arch>/triple_<os>.cpp` 实现新 ABI。
2. 在 `scripts/sysroot/` 增加 sysroot 配置脚本。
3. 在 `tests/e2e/cross/` 增加交叉编译 e2e 测试。

### 16.7 代码风格

* C++：`.clang-format` 配置；clang-tidy 强制 modernize-* 与
  bugprone-*。
* Ploy：内置 `polyc --format` 是格式化基准。
* 所有源内注释一律英文。
* 不允许在注释中出现 “minimal”、“stub”、“placeholder” 等用语。
* 公共 API 必须有 `///` 文档注释，由 `polydoc` 解析。

### 16.8 文档规则

* 每篇英文文档都有同目录的 `_zh` 对应文档。
* 双语修改必须同 commit；CI 跑 `scripts/docs_sync_check.py`。
* 标题与结构两侧镜像一致。
* 注释或文档中不出现需求文档标识符。
* 版本变更同步根 `CMakeLists.txt`。

### 16.9 提交变更

1. fork；建立主题分支。
2. 跑 `scripts/preflight.sh`。
3. 提 PR；CI 跑全矩阵。
4. 处理 review 意见；按要求 squash。
5. 维护者在所有门绿后以 `Squash & merge` 合入。

---

## 17. 插件 SDK

### 17.1 架构

插件是动态库，`polyui` 从 `<config>/polyui/plugins/`，`polyc` 从
`<config>/polyc/plugins/` 发现。每个插件导出一个小而稳定的 C ABI 面，
并在前置位置声明能力开关，以便宿主对未知扩展做沙箱限制。

### 17.2 能力开关

| 开关                         | 授予                                                      |
|------------------------------|-----------------------------------------------------------|
| `POLY_CAP_FRONTEND`          | 注册新源语言前端。                                        |
| `POLY_CAP_BACKEND`           | 注册新代码生成后端。                                      |
| `POLY_CAP_PASS`              | 注册中端优化 pass。                                       |
| `POLY_CAP_PANEL`             | 给 `polyui` 加 UI 面板。                                  |
| `POLY_CAP_LSP_PROVIDER`      | 提供某语言 LSP 服务器。                                   |
| `POLY_CAP_DAP_PROVIDER`      | 提供 DAP 调试器。                                         |
| `POLY_CAP_PACKAGE_MANAGER`   | 注册包管理器检测器。                                      |
| `POLY_CAP_AI_PROVIDER`       | 提供 AI 助手后端。                                        |
| `POLY_CAP_VIEWER`            | 注册文件类型查看器。                                      |

### 17.3 必选导出

```c
const PolyPluginManifest *poly_plugin_manifest(void);
int  poly_plugin_init(PolyHostServices *host);
void poly_plugin_shutdown(void);
```

### 17.4 可选导出

```c
int  poly_plugin_register_frontend(PolyFrontendRegistry *r);
int  poly_plugin_register_backend(PolyBackendRegistry *r);
int  poly_plugin_register_passes(PolyPassRegistry *r);
int  poly_plugin_register_panels(PolyPanelRegistry *r);
int  poly_plugin_register_lsp(PolyLspRegistry *r);
int  poly_plugin_register_dap(PolyDapRegistry *r);
int  poly_plugin_register_pkg(PolyPackageRegistry *r);
int  poly_plugin_register_ai(PolyAiRegistry *r);
int  poly_plugin_register_viewer(PolyViewerRegistry *r);
```

### 17.5 宿主服务

插件接收 `PolyHostServices`，提供日志、设置访问、文件 I/O、遥测与
事件总线。插件不得直接链接宿主内部。

```c
typedef struct PolyHostServices {
    int  api_version;
    void (*log)(int level, const char *msg);
    int  (*get_setting)(const char *key, char *out, size_t out_size);
    int  (*set_setting)(const char *key, const char *value);
    int  (*read_file)(const char *path, void **buf, size_t *len);
    int  (*emit_event)(const char *topic, const char *payload);
    int  (*subscribe)(const char *topic, void (*cb)(const char *));
} PolyHostServices;
```

### 17.6 插件文件位置

| 操作系统 | 位置                                          |
|----------|-----------------------------------------------|
| Linux    | `~/.config/polyui/plugins/`                   |
| macOS    | `~/Library/Application Support/polyui/plugins/` |
| Windows  | `%APPDATA%\polyui\plugins\`                   |

### 17.7 简单示例

```c
#include "polyplugin.h"

static const PolyPluginManifest g_manifest = {
    .api_version = POLY_PLUGIN_API_VERSION,
    .name        = "hello-plugin",
    .version     = "1.0.0",
    .capabilities = POLY_CAP_PANEL,
};

const PolyPluginManifest *poly_plugin_manifest(void) { return &g_manifest; }

int poly_plugin_init(PolyHostServices *host) {
    host->log(POLY_LOG_INFO, "hello-plugin loaded");
    return POLY_OK;
}

void poly_plugin_shutdown(void) {}
```

构建：

```sh
cc -shared -fPIC -I /opt/polyglot/include hello.c \
   -o ~/.config/polyui/plugins/hello.so
```

### 17.8 插件测试

`tests/unit/plugins/` 中的宿主夹具会从 fixture 目录加载插件，
覆盖每个注册的能力，并检查干净退出。

---

## 18. 附录

### 18.1 词汇表

| 术语            | 定义                                                                  |
|-----------------|-----------------------------------------------------------------------|
| Bridge call     | 经 `runtime::ffi::BridgeFrame` 降级的跨语言调用。                     |
| CTest target    | 注册到 CTest 的逻辑测试可执行（本项目共 30 个）。                     |
| Driver          | `build/` 下任意一个工具二进制（共 11 个）。                           |
| Frontend        | 把某种源语言降到统一 IR 的静态库。                                    |
| Pipeline        | Ploy 中的 `PIPELINE` 链；亦指优化 pass 序列。                         |
| Sample          | `tests/samples/` 下的编号项目。                                       |
| Triple          | 传给 `--target=` 的 `arch-vendor-os-abi` 字符串。                     |
| Sysroot         | 交叉编译时使用的工具链根。                                            |
| Bridge ABI      | `bridge_call` 使用的编排契约。                                        |
| Safepoint       | GC 可在此运行的 IR 位置。                                             |

### 18.2 工具与章节交叉索引

| 工具         | 章节                                     |
|--------------|------------------------------------------|
| `polyc`      | [6.1](#61-polyc--编译器驱动)             |
| `polyld`     | [6.2](#62-polyld--链接器)、[11](#11-链接器与目标格式) |
| `polyasm`    | [6.3](#63-polyasm--汇编器--反汇编器)     |
| `polyopt`    | [6.4](#64-polyopt--ir-优化器)、[8](#8-中端优化) |
| `polyrt`     | [6.5](#65-polyrt--运行时-cli)、[9](#9-运行时)、[13.3](#133-性能分析器) |
| `polybench`  | [6.6](#66-polybench--基准测试驱动)、[14.4](#144-基准) |
| `polytopo`   | [6.7](#67-polytopo--调用图拓扑查看器)、[13.4](#134-调用分析) |
| `polyls`     | [6.8](#68-polyls--语言服务器)、[12](#12-idepolyui-与语言服务器) |
| `polydoc`    | [6.9](#69-polydoc--文档抽取器)            |
| `polyver`    | [6.10](#610-polyver--版本与清单工具)     |
| `polyui`     | [6.11](#611-polyui--ide-外壳)、[12](#12-idepolyui-与语言服务器) |

### 18.3 发布打包

```sh
# Linux
cmake --build build --target package
# macOS —— 产出可公证的 .pkg
cmake --build build --target package_macos
# Windows（PowerShell）
cmake --build build --target package_windows
```

输出包落到 `build/release/`。每个包包含：

* 全部 11 个工具二进制。
* 运行时动态库。
* `docs/` 的 HTML 渲染。
* 42 个编号示例。
* 经签名的清单，可由 `polyver --check-manifest` 校验。

### 18.4 环境变量参考

| 变量                        | 用途                                                    |
|-----------------------------|---------------------------------------------------------|
| `POLY_GC`                   | 选择运行时 GC 算法。                                    |
| `POLY_GC_HEAP_INIT`         | GC 初始堆。                                             |
| `POLY_GC_HEAP_MAX`          | GC 堆上限。                                             |
| `POLY_GC_PAUSE_GOAL_MS`     | GC 停顿目标。                                           |
| `POLY_TASKS_THREADS`        | 任务池规模。                                            |
| `POLY_CACHE_DIR`            | 覆盖增量缓存目录。                                      |
| `POLY_CACHE_MAX_BYTES`      | 缓存 LRU 上限。                                         |
| `POLY_LOG_LEVEL`            | `error` / `warn` / `info` / `debug` / `trace`。         |
| `POLY_TELEMETRY`            | `off`（默认） / `on`。                                  |
| `POLY_PROFILE_INTERVAL_MS`  | 性能分析采样周期。                                      |
| `POLYLS_LOG`                | `polyls` 帧日志路径。                                   |
| `POLYUI_PLUGIN_DIR`         | 覆盖插件发现目录。                                      |

### 18.5 配置文件位置

| 项                | 位置                                                |
|-------------------|-----------------------------------------------------|
| 用户设置          | `~/.config/polyui/settings.json`                    |
| 工作区设置        | `<workspace>/.polyui/settings.json`                 |
| Launch profiles   | `<workspace>/.polyui/launch.json`                   |
| 插件              | `~/.config/polyui/plugins/`                         |
| LSP 覆盖          | `~/.config/polyui/lsp.json`                         |
| 缓存              | `~/.cache/polyglot/`                                |

### 18.6 参考

* [tutorial/project_tutorial_zh.md](tutorial/project_tutorial_zh.md) ——
  新人导览。
* [tutorial/ploy_language_tutorial_zh.md](tutorial/ploy_language_tutorial_zh.md)
  —— Ploy 语言参考。
* [specs/](specs/) —— 机器可读的 schema（调用图、profile 流、诊断）。
* [realization/](realization/) —— 各子系统设计笔记。
* [api/](api/) —— 公共 ABI 文档（`polyls`、插件 SDK、运行时）。

### 18.7 常见问题

**Q. 为什么自研 IR 而不是用 LLVM？**
A. PolyglotCompiler 需要 IR 中的一等 `bridge_call`、GC barrier 与
异步原语。曾原型化基于 LLVM，但把它们假装成 intrinsic 让后端做太多
变通，所以放弃。

**Q. 可以只用某一个前端吗？**
A. 可以。configure 时设置 `POLY_BUILD_FRONTENDS="cpp;ploy"` 即可
舍弃其他前端。

**Q. macOS arm64 二进制可公证吗？**
A. 可以。Mach-O 发射器输出 16 KiB 对齐 segment，并对 `__DATA_CONST`
打 `SG_READ_ONLY (0x10)`，满足公证强制的 dyld 26 chained-fixup 要求。

**Q. IDE 在 Wayland 上能用吗？**
A. 可以；Linux 上默认使用 Qt 6.6 原生 Wayland。X11 通过 XWayland 仍
可用。

**Q. 插件 ABI 稳定吗？**
A. 仅在不兼容变更时增大 ABI 版本。插件宿主拒绝加载用不同
`POLY_PLUGIN_API_VERSION` 编译的插件。

### 18.8 变更日志

精简变更日志见 [VERSION.txt](../VERSION.txt) 与
`scripts/release_notes.py` 生成的发布说明。当前版本为
**PolyglotCompiler 1.45.2**。

### 18.9 许可证

PolyglotCompiler 按 [LICENSE](../LICENSE) 分发。第三方组件保留各自
许可证，详见 [Dependencies.cmake](Dependencies.cmake)。
