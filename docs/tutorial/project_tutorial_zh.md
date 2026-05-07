# PolyglotCompiler 项目教程

> **版本**：3.0.0  
> **更新日期**：2026-05-07  
> **项目**：PolyglotCompiler 1.45.2  
> **读者对象**：贡献者、集成方与工具链作者

---

## 目录

1. [项目概述](#1-项目概述)
2. [前置条件](#2-前置条件)
3. [从源码构建](#3-从源码构建)
4. [项目架构](#4-项目架构)
5. [使用工具链](#5-使用工具链)
6. [跨目标与容器矩阵](#6-跨目标与容器矩阵)
7. [运行测试](#7-运行测试)
8. [使用样例程序](#8-使用样例程序)
9. [开发工作流](#9-开发工作流)
10. [前端开发](#10-前端开发)
11. [中间层与 IR](#11-中间层与-ir)
12. [后端开发](#12-后端开发)
13. [新增语言前端](#13-新增语言前端)
14. [插件、设置与主题](#14-插件设置与主题)
15. [调试与排错](#15-调试与排错)
16. [项目配置参考](#16-项目配置参考)

---

# 1. 项目概述

## 1.1 PolyglotCompiler 是什么

PolyglotCompiler 是一款使用 **C++20** 实现的自举多语言编译器。它将 **C++**、**Python**、**Rust**、**Java**、**C#（.NET）**、**Go**、**JavaScript**、**Ruby** 与跨语言 DSL **`.ploy`** 编译为统一的 SSA 形式中间表示（IR），并为 **x86_64**、**ARM64**、**WebAssembly** 生成原生代码。链接器（`polyld`）直接生成 ELF、PE32+、Mach-O 与 Wasm，代码生成阶段不依赖任何外部编译器。

### 核心能力

- **9 个一等前端**，全部通过 `FrontendRegistry` 注册。
- **3 个后端**（`backend_x86_64`、`backend_arm64`、`backend_wasm`）共享 `MachineIR` 层与统一的寄存器/调度框架。
- **11 个工具链可执行文件**：`polyc`、`polyld`、`polyasm`、`polyopt`、`polyrt`、`polytopo`、`polybench`、`polyls`、`polydoc`、`polyver`、`polyui`。
- **30 个 CTest 目标**，按模块拆分（按前端、后端、运行时、链接器、LSP、设置、样例回归、基准等）。
- **跨语言互操作**：通过 `.ploy`（`LINK`、`CALL`、`NEW`、`METHOD`、`GET`、`SET`、`WITH`、`DELETE`、`EXTEND`、`IMPORT … PACKAGE`、`MAP_TYPE`、`CONVERT`、`PIPELINE`）。
- **容器矩阵**：`polyc --target=<triple> --container=<auto|elf|pe|macho|wasm>` 覆盖 Linux / Windows / macOS / WASI 上的 x86_64 与 arm64。
- **自举语言服务器**（`polyls`）通过 stdio JSON-RPC 提供服务，被 `polyui` 与任意 LSP 客户端使用。
- **插件系统**：稳定的 C ABI（前端、优化、后端、IDE 面板、Formatter、Linter、Debugger、补全 / 诊断提供者）。
- **设置与主题契约**仿 VS Code（三层 JSON、Schema、热重载、命令面板、键绑定、外置 `.polytheme.json`）。

## 1.2 技术栈

| 组件        | 技术                                                                              |
|-------------|-----------------------------------------------------------------------------------|
| 语言        | C++20                                                                             |
| 构建系统    | CMake ≥ 3.20                                                                      |
| 构建工具    | Ninja（推荐）或 Make                                                              |
| 测试框架    | Catch2 v3                                                                         |
| 格式化      | fmt ≥ 11.2.0                                                                      |
| JSON        | nlohmann/json                                                                     |
| 内存分配器  | mimalloc（仅使用 `mi_*` API；不在 dylib 边界进行全局 malloc 替换）                |
| GUI（可选） | Qt 6（推荐）或 Qt 5.15+                                                            |
| 许可证      | GPL v3                                                                            |

---

# 2. 前置条件

## 2.1 必备软件

| 平台    | 推荐编译器                  | 最低版本 |
|---------|-----------------------------|----------|
| Windows | MSVC（Visual Studio 2022+） | v19.30+  |
| Linux   | GCC                         | 12+      |
| Linux   | Clang                       | 15+      |
| macOS   | Apple Clang                 | 15+      |

| 构建工具 | 版本    | 说明                          |
|----------|---------|-------------------------------|
| CMake    | ≥ 3.20  | 构建系统生成器                |
| Ninja    | 任意新版 | 推荐生成器（比 Make 更快）    |

## 2.2 可选运行时（用于跨语言样例）

| 运行时         | 用途                              |
|----------------|-----------------------------------|
| Python 3.10+   | Python 前端与样例                 |
| Rust（rustup） | Rust 前端与样例                   |
| Java JDK 21+   | Java 前端与样例                   |
| .NET SDK 9+    | .NET（C#）前端与样例              |
| Go 1.22+       | Go 前端与样例                     |
| Node.js 20+    | JavaScript 前端与样例             |
| Ruby 3.2+      | Ruby 前端与样例                   |

`polyver` 在宿主上探测以上工具链并写入工具链数据库，供 `polyc` 启动时查询。安装或升级任何运行时之后运行一次：

```bash
polyver detect            # 一次性探测
polyver detect --json     # 机器可读清单
```

## 2.3 环境配置脚本

`tests/samples/` 提供按样例驱动的跨语言环境脚本：

```powershell
# Windows（PowerShell）
cd tests\samples
.\setup_env.ps1
```

```bash
# Linux / macOS
cd tests/samples
chmod +x setup_env.sh
./setup_env.sh
```

脚本会创建按语言隔离的环境（Python venv、cargo target、JDK、.NET SDK、Go 模块缓存、Node `node_modules`、Bundler `Gemfile.lock`），并把它们链接到每个样例目录。

---

# 3. 从源码构建

## 3.1 克隆

```bash
git clone <repository-url>
cd PolyglotCompiler
```

## 3.2 在 Linux / macOS 构建

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

## 3.3 在 Windows（MSVC + Ninja）构建

```cmd
rem 必须使用 -arch=amd64，避免 x86 ↔ x64 链接失败
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64
cmake -B build -G Ninja
cmake --build build -j
```

## 3.4 构建目标

| 目标                | 描述                                            | 可执行文件                  |
|---------------------|-------------------------------------------------|-----------------------------|
| `polyc`             | 编译器驱动                                       | `polyc[.exe]`               |
| `polyld`            | 链接器（ELF / PE32+ / Mach-O / Wasm）           | `polyld[.exe]`              |
| `polyasm`           | 汇编器                                           | `polyasm[.exe]`             |
| `polyopt`           | 独立 IR 优化器                                   | `polyopt[.exe]`             |
| `polyrt`            | 运行时工具（GC、FFI、async、profiling 驱动）    | `polyrt[.exe]`              |
| `polytopo`          | 拓扑图分析                                       | `polytopo[.exe]`            |
| `polybench`         | 基准套件                                         | `polybench[.exe]`           |
| `polyls`            | 语言服务器（stdio JSON-RPC）                    | `polyls[.exe]`              |
| `polydoc`           | `.ploy` 文档抽取                                 | `polydoc[.exe]`             |
| `polyver`           | 工具链探测与数据库写入                           | `polyver[.exe]`             |
| `polyui`            | Qt 桌面 IDE（未找到 Qt 时静默跳过）              | `polyui[.exe]` / `polyui.app` |
| `unit_tests` …      | 聚合 + 各模块测试二进制（见第 7 章）            | 多个                         |

```bash
cmake --build build --target polyc           # 单个工具
cmake --build build --target test_linker     # 单个测试
cmake --build build                          # 全部
```

## 3.5 CMake 依赖

`Dependencies.cmake` 声明了通过 `FetchContent` 自动拉取到 `build/_deps/` 的库：

| 库               | 用途                                                                                              |
|------------------|---------------------------------------------------------------------------------------------------|
| Catch2           | 单元测试框架                                                                                      |
| fmt              | 格式化 I/O（Apple Clang 21 需要 ≥ 11.2.0）                                                        |
| nlohmann/json    | JSON 解析与序列化                                                                                 |
| mimalloc         | 高性能内存分配器（仅使用 `mi_*` API；跨 dylib 边界禁用全局 malloc 替换）                          |

## 3.6 构建配置

```bash
cmake -B build      -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake -B build-rel  -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake -B build-rwd  -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake -B build-san  -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DPOLYGLOT_ENABLE_ASAN=ON -DPOLYGLOT_ENABLE_UBSAN=ON \
      -DBUILD_SHARED_LIBS=OFF
```

`BUILD_SHARED_LIBS` 默认 `ON`：所有可执行文件共享同一份公共动态库，避免重复链接。

---

# 4. 项目架构

## 4.1 目录结构

```
PolyglotCompiler/
├── frontends/              # 9 个语言前端
│   ├── common/             #   共享 token pool、预处理、诊断
│   ├── cpp/                #   C++ 前端（lexer/parser/sema/lowering/constexpr）
│   ├── python/             #   Python 前端
│   ├── rust/               #   Rust 前端（cargo metadata + crate loader）
│   ├── java/               #   Java 前端（Java 8 / 17 / 21 / 23）
│   ├── dotnet/             #   .NET（C#）前端（.NET 6 / 7 / 8 / 9）
│   ├── go/                 #   Go 前端（go.mod + GOROOT / GOPATH 解析）
│   ├── javascript/         #   JavaScript / TypeScript 前端（Node.js 解析）
│   ├── ruby/               #   Ruby 前端（require / Gemfile / Bundler）
│   └── ploy/               #   .ploy 跨语言前端
├── middle/                 # SSA IR、优化 Pass、PGO、LTO
├── backends/               # 3 个架构后端
│   ├── common/             #   MachineIR、调试信息（DWARF / PDB）、对象发射
│   ├── x86_64/             #   x86_64（isel / regalloc / asm_printer / scheduler）
│   ├── arm64/              #   ARM64（isel / regalloc / asm_printer）
│   └── wasm/               #   WebAssembly（影子栈、WAT / 二进制）
├── runtime/                # 运行时系统
│   ├── include/            #   GC / FFI / async / 线程接口
│   └── src/{gc,interop,libs,services}/
│       └── libs/           #     python_rt、cpp_rt、rust_rt、java_rt、dotnet_rt、
│                           #     go_rt、javascript_rt、ruby_rt
├── common/                 # 类型系统、符号表、DWARF 5、宿主三元组、设置
├── tools/                  # 驱动、链接器、汇编、优化器、运行时工具、拓扑、基准、
│                           # LSP、文档、版本探测、IDE
├── tests/
│   ├── unit/               #   各模块单元测试（Catch2）
│   ├── integration/        #   全管线 + 二进制矩阵
│   ├── benchmarks/         #   微 + 宏基准
│   └── samples/            #   42 个样例（`00_minimal` … `41_grammar_polish`）
├── scripts/                # CI 脚本、打包、样例驱动、文档闸门
├── docs/
│   ├── api/                #   API 参考（双语）
│   ├── specs/              #   语言与 IR 规范、ABI、插件规范
│   ├── realization/        #   实现细节（双语）
│   ├── tutorial/           #   教程（本文件位于此处）
│   └── demand/             #   项目需求日志
├── CMakeLists.txt          # 根 CMake（项目版本号居于此）
├── Dependencies.cmake      # 外部依赖声明
└── VERSION.txt             # 由 CMake 重新生成的纯文本版本号镜像
```

## 4.2 编译管线

```
源代码（.cpp / .py / .rs / .java / .cs / .go / .js / .rb / .ploy）
        │
        ▼
┌─────────┐  Token  ┌─────────┐  AST   ┌─────────┐ 标注 AST  ┌──────────┐  IR
│  Lexer  │───────▶│ Parser  │──────▶│  Sema   │──────────▶│ Lowering │────┐
└─────────┘         └─────────┘        └─────────┘           └──────────┘    │
                                                                              ▼
                                                  ┌──────────────────────────────────┐
                                                  │   共享 SSA IR（CFG + 函数）       │
                                                  └─────────────────┬────────────────┘
                                                                    │
                                                  ┌─────────────────┼─────────────────┐
                                                  ▼                 ▼                 ▼
                                              ┌──────────┐  ┌──────────┐      ┌──────────┐
                                              │ 优化 Pass │ │   PGO    │      │   LTO    │
                                              └────┬─────┘  └────┬─────┘      └────┬─────┘
                                                   └───────────────┴─────────────────┘
                                                                    ▼
                                                  ┌──────────────────────────────────┐
                                                  │  后端（x86_64 / arm64 / wasm）   │
                                                  │  isel → regalloc → asm_printer   │
                                                  └─────────────────┬────────────────┘
                                                                    ▼
                                                  ┌──────────────────────────────────┐
                                                  │  polyld → ELF / PE32+ / Mach-O / │
                                                  │           Wasm + 跨语言粘合代码  │
                                                  └──────────────────────────────────┘
```

## 4.3 前端架构

每个前端遵循同一套 4 阶段管线：

| 阶段     | 头文件             | 职责                                                 |
|----------|--------------------|------------------------------------------------------|
| Lexer    | `*_lexer.h`        | 把源码切分为共享 `Token` 流                           |
| Parser   | `*_parser.h`       | 把 token 解析为该语言的 AST                           |
| Sema     | `*_sema.h`         | 名字解析、类型检查、语义验证                          |
| Lowering | `*_lowering.h`     | 将带类型注解的 AST 降级到共享 SSA IR                  |

所有前端都向 `FrontendRegistry` 注册并共享 `frontend_common` 中的 `SharedTokenPool`（基于 arena 的 lexeme 存储 + 开放寻址标识符内化 + 解析回溯快照/恢复）。

## 4.4 CMake 库目标

| 目标                       | 源码根                    | 描述                                                  |
|----------------------------|---------------------------|-------------------------------------------------------|
| `polyglot_common`          | `common/`                 | 类型系统、符号表、DWARF 5、宿主三元组                 |
| `frontend_common`          | `frontends/common/`       | Token pool、预处理、诊断                              |
| `frontend_cpp` / `_python` / `_rust` / `_java` / `_dotnet` / `_go` / `_javascript` / `_ruby` / `_ploy` | `frontends/<lang>/` | 各语言前端                                |
| `middle_ir`                | `middle/`                 | IR、优化 Pass、PGO、LTO                               |
| `backend_x86_64` / `_arm64` / `_wasm` | `backends/<arch>/` | 各架构后端                                           |
| `runtime`                  | `runtime/`                | GC / FFI / async / 线程                               |
| `linker_lib`               | `tools/polyld/`           | `polyld` 与测试共享的链接器库                         |
| `polyls_core`              | `tools/polyls/`           | LSP 服务器核心（capabilities、navigation、index）     |

---

# 5. 使用工具链

## 5.1 polyc — 编译器驱动

驱动按文件后缀自动选择前端，串接共享优化器与目标后端。

```bash
# 后缀自动识别
polyc sample.ploy   -o sample
polyc hello.cpp     -o hello
polyc script.py     -o script
polyc Main.java     -o main
polyc Program.cs    -o program
polyc app.go        -o app
polyc index.js      -o index
polyc gem.rb        -o gem

# 显式指定语言
polyc --lang=cpp    input_file -o output

# 输出中间产物
polyc --emit-ir=output.ir   input.ploy
polyc --emit-asm=output.s   input.ploy
polyc --emit=call-graph:cg.json input.ploy
polyc --emit-obj=out.o      input.ploy

# 跨目标编译
polyc --target=aarch64-apple-darwin    --container=macho -o app  main.cpp
polyc --target=x86_64-pc-windows-msvc  --container=pe    -o app.exe main.cpp
polyc --target=wasm32-wasi             --container=wasm  -o app.wasm main.cpp

# 设置与诊断
polyc --settings ./.polyglot/settings.json --print-effective-settings
polyc --check broken.ploy            # 在 stdout 输出 LSP 形态的 JSON 诊断
polyc --dump-token-pool              # SharedTokenPool 统计
polyc --progress=json                # 机器可读阶段事件
polyc --clean-cache                  # 清理增量缓存
```

`polyc` 调用链接器时按相对自身可执行路径定位 `polyld`，构建树原地即可工作，无需修改 `PATH`。

## 5.2 polyld — 链接器

```bash
polyld -o program file1.o file2.o
polyld --polyglot -o program main.o bridge.o          # 跨语言粘合
polyld --target=aarch64-apple-darwin --container=macho -o app main.o
```

POSIX 宿主上自动设置 `0755` 权限位。macOS arm64 写出 `LC_SEGMENT_64` 时按 16 KiB 对齐，`__DATA_CONST` 写为 `rw-` 并携带 `SG_READ_ONLY`，这是 dyld 26（Tahoe）应用 chained fixup 所要求的。

## 5.3 polyasm — 汇编器

```bash
polyasm input.s -o output.o
polyasm --target=arm64 input.s -o output.o
```

## 5.4 polyopt — 独立优化器

```bash
polyopt -O0 input.ir -o out.ir            # 仅规范化
polyopt -O3 input.ir -o out.ir
polyopt --pass=constant-fold --pass=dce --pass=gvn input.ir -o out.ir
polyopt --pgo-profile=app.profdata -O2 input.ir -o out.ir
polyopt --lto-summary=app.lto.json input.ir -o out.ir
```

## 5.5 polyrt — 运行时工具

```bash
polyrt --gc-mode=generational --threads=4
polyrt profile --json profile.json --duration-ms 5000 build/app
polyrt async   --json   # 协作式事件循环快照
polyrt async   --run=64 # 推进事件循环 64 个 tick
polyrt calltrace --json calltrace.json
```

## 5.6 polytopo — 拓扑分析

```bash
polytopo build/app.cgjson                     # 默认 ASCII 文本视图
polytopo --view-mode=dot  build/app.cgjson    # Graphviz DOT
polytopo --view-mode=json build/app.cgjson    # JSON（可脚本化）
polytopo --filter-language=python build/app.cgjson
```

## 5.7 polyls — 语言服务器

`polyls` 是普通的 stdio LSP 服务器，遵循 LSP 3.17。`polyui` 通过 `IdeLspBridge` 为每种语言启动一个实例；其他 LSP 客户端（VS Code、Neovim、Helix 等）同样可以驱动它。

```bash
polyls                           # stdio 服务器
polyls --log polyls.log          # 抓取 JSON-RPC 帧用于排查
```

服务器侧测试覆盖：`test_polyls`（服务器 / capabilities）、`test_lsp`（封帧与协商）、`test_problems`（诊断面板契约）、`test_completion_ranker`。

## 5.8 polydoc — 文档抽取

```bash
polydoc tests/samples/01_basic_linking/basic_linking.ploy        # Markdown 输出到 stdout
polydoc --json tests/samples/01_basic_linking/basic_linking.ploy # JSON 输出到 stdout
polydoc -o docs/out.md tests/samples/01_basic_linking/basic_linking.ploy
```

`polydoc` 会扫描每个挂在顶层 `FUNC` / `STRUCT` / `LET` / `VAR` 上的 `///` 文档块。

## 5.9 polyver — 工具链探测

```bash
polyver detect                  # 打印检测到的工具链表
polyver detect --json           # JSON 清单
polyver db --print              # 打印工具链数据库
polyver db --refresh            # 强制重新检测
```

## 5.10 polybench — 基准套件

```bash
polybench --suite=micro
polybench --suite=macro
polybench --all
polybench --fast                # `benchmark_fast` CTest 目标使用的子集
```

## 5.11 polyui — 桌面 IDE

`polyui` 是基于 Qt 的 IDE，要点：

- 实时诊断（通过 `polyls` 的 LSP）。
- 拓扑面板（链接图、按语言过滤、批量操作）。
- Profiler 面板（`Ctrl+Alt+P`）：火焰图、热点、时间线、按语言细分（由 `polybench` + `polyrt` 驱动）。
- Call Analyzer 面板（`Ctrl+Alt+G`）：来自 `polyc --emit=call-graph` 的静态调用图，支持调用方/被调方树以及有界 DFS 路径搜索。
- 问题面板：可按严重度、文件名子串、消息正则过滤。
- 主题管理（`Ctrl+K, Ctrl+T`）、命令面板（`Ctrl+Shift+P`）、键绑定（`keybindings.json`）。

---

# 6. 跨目标与容器矩阵

`polyc --target=<triple>` 与 `polyc --container=<auto|elf|pe|macho|wasm>` 覆盖下表中的每一格。未指定 `--target` 时使用 `common::HostTriple()`；未指定 `--container` 时按第三列的 OS 族自动推导。当 `-o` 后缀与解析的容器不匹配时，驱动会发出 `polyc-warn-W2101` 警告。

| 三元组                       | 容器     | 默认可执行后缀 |
|------------------------------|----------|----------------|
| `x86_64-pc-windows-msvc`     | PE32+    | `.exe`         |
| `aarch64-pc-windows-msvc`    | PE32+    | `.exe`         |
| `x86_64-unknown-linux-gnu`   | ELF      | （无）         |
| `aarch64-unknown-linux-gnu`  | ELF      | （无）         |
| `x86_64-apple-darwin`        | Mach-O   | （无）         |
| `aarch64-apple-darwin`       | Mach-O   | （无）         |
| `wasm32-wasi`                | Wasm     | `.wasm`        |

矩阵在两处被驱动：

- `scripts/ci/run_binary_matrix.{sh,ps1}`：在对应 CI runner 上跑遍每一格。
- `tests/integration/binary_matrix/`：写入器一旦回归就让 `integration_tests` 失败。

macOS arm64 关键点（1.45.2 最近一次修复）：

- 所有 `LC_SEGMENT_64` 按 16 KiB 对齐（`PageSizeForArch(MachOArch::kArm64)`）；4 KiB 对齐会被内核 mapper 静默拒绝并以退出码 137 终止。
- `__DATA_CONST` 写为 `rw-` 并将 `flags = SG_READ_ONLY (0x10)`，dyld 据此在原地应用 chained fixup，并在完成后将段保护改回 `r--`。

---

# 7. 运行测试

## 7.1 CTest 矩阵

历史上的单体 `unit_tests` 已经被拆成 24 个按模块的测试二进制；聚合目标作为兼容形态保留。CTest 矩阵共 30 个目标：

| #  | 目标                       | 范围                                                  |
|----|----------------------------|-------------------------------------------------------|
| 1  | `test_core`                | 公共工具、类型系统、符号表                            |
| 2  | `test_plugins`             | 插件加载、沙箱、版本契约                              |
| 3  | `test_frontend_common`     | Token pool、预处理、诊断                              |
| 4  | `test_frontend_python`     | Python 前端                                           |
| 5  | `test_frontend_cpp`        | C++ 前端                                              |
| 6  | `test_frontend_rust`       | Rust 前端                                             |
| 7  | `test_frontend_ploy`       | `.ploy` 前端                                          |
| 8  | `test_frontend_java`       | Java 前端                                             |
| 9  | `test_frontend_dotnet`     | .NET（C#）前端                                        |
| 10 | `test_frontend_javascript` | JavaScript 前端                                       |
| 11 | `test_frontend_ruby`       | Ruby 前端                                             |
| 12 | `test_frontend_go`         | Go 前端                                               |
| 13 | `test_middle`              | SSA IR、优化 Pass、PGO、LTO                           |
| 14 | `test_backends`            | x86_64 / ARM64 / Wasm 代码生成                        |
| 15 | `test_runtime`             | GC、FFI、marshalling、线程                            |
| 16 | `test_linker`              | `polyld` ELF / PE / Mach-O / Wasm 写入与粘合代码     |
| 17 | `test_topology`            | 拓扑图与链接校验                                      |
| 18 | `test_settings`            | 三层设置、Schema、热重载                              |
| 19 | `test_lsp`                 | LSP 封帧、JSON-RPC、能力协商                          |
| 20 | `test_polyls`              | `polyls` 服务器：补全、跳转、符号索引                 |
| 21 | `test_problems`            | 诊断面板契约                                          |
| 22 | `test_completion_ranker`   | 补全排序启发式                                        |
| 23 | `test_topology_ui`         | 拓扑面板 UI 绑定                                      |
| 24 | `test_e2e`                 | 端到端管线                                            |
| 25 | `unit_tests`               | 聚合兼容二进制                                        |
| 26 | `integration_tests`        | 全管线 + 跨语言互操作                                 |
| 27 | `benchmark_tests`          | 基准套件                                              |
| 28 | `samples_regression`       | 驱动 `tests/samples/**` 经过 `polyc` + `polyld`       |
| 29 | `benchmark_fast`           | CI 烟测使用的 `[fast]` 子集                          |
| 30 | `benchmark_full`           | 完整基准                                              |

## 7.2 常用调用

```bash
cd build
ctest --output-on-failure                       # 全部
ctest -R test_frontend_ploy                     # 单个目标
ctest -L benchmark                              # 按标签
./test_frontend_ploy [parser]                   # 按 Catch2 标签
./unit_tests "[ploy],[python]"                  # 经聚合二进制运行多个标签
```

## 7.3 Sanitizer 与覆盖率构建

```bash
cmake -B build-san -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DPOLYGLOT_ENABLE_ASAN=ON \
  -DPOLYGLOT_ENABLE_UBSAN=ON \
  -DBUILD_SHARED_LIBS=OFF
cmake --build build-san
cd build-san && ctest --output-on-failure -LE benchmark

cmake -B build-cov -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DPOLYGLOT_ENABLE_COVERAGE=ON \
  -DBUILD_SHARED_LIBS=OFF
cmake --build build-cov
cd build-cov && ctest --output-on-failure -LE benchmark
lcov --capture --directory . --output-file coverage.info
```

---

# 8. 使用样例程序

## 8.1 清单

`tests/samples/` 共 **42 个样例**，覆盖从单行最小样例到全栈多语言管线。`samples_regression` 驱动整套样例；每个目录可包含 `expected_output.txt`（按字节固定 stdout）或 `expected_output.skip`（直接归类到 SKIP，不调用 `polyc`）。

| 范围                | 主题                                                                                |
|---------------------|-------------------------------------------------------------------------------------|
| `00_minimal`        | 单行最小样例（按宿主固定 stdout）                                                   |
| `01` … `09`         | 核心 `.ploy` 互操作（LINK、MAP_TYPE、PIPELINE、控制流、OOP、混合）                  |
| `10` … `16`         | 诊断、Java / .NET 互操作、泛型、async、全栈、CONFIG / VENV                           |
| `17` … `30`         | 真实领域（字符串 / 数值 / 文件 / JSON / 图像 / SQL / HTTP …）                        |
| `31` … `41`         | 近期语言特性（带宽类型、类型化句柄、模式匹配、默认参数、动态语言上的 EXTEND、TRY/CATCH、async/await、泛型、可见性/属性、字符串字面量、文法收尾） |

## 8.2 样例布局

```
NN_feature_name/
├── feature_name.ploy         # .ploy 入口
├── source_file.cpp           # 各语言源码
├── source_file.py
├── source_file.rs
├── SourceFile.java
├── SourceFile.cs
├── source_file.go
├── source_file.js
├── source_file.rb
├── expected_output.txt       # 按字节固定的 stdout（可选）
└── expected_output.skip      # 归类到 SKIP（可选）
```

## 8.3 编译单个样例

```bash
polyc tests/samples/01_basic_linking/basic_linking.ploy   -o basic_linking
polyc tests/samples/09_mixed_pipeline/mixed_pipeline.ploy -o mixed_pipeline
polyc tests/samples/15_full_stack/full_stack.ploy         -o full_stack
```

## 8.4 驱动整张矩阵

```bash
# POSIX
scripts/build_all_samples.sh   --polyc build/polyc --polyld build/polyld
# Windows
scripts\build_all_samples.ps1  -Polyc build\polyc.exe -Polyld build\polyld.exe
```

脚本写出 `samples_report.json`（顶层带按 ASCII 排序的 `ok` 数组），`samples_regression_test.cpp` 据此校验脚本汇报的 OK 集合与 per-sample 状态字段汇总的 OK 集合一致。

## 8.5 推荐学习路径

1. `00_minimal` → `01_basic_linking` → `02_type_mapping`
2. 控制流与管线：`03_pipeline`
3. 包管理：`04_package_import`、`16_config_and_venv`
4. OOP 互操作：`05_class_instantiation` → `08_delete_extend`
5. 混合语言：`09_mixed_pipeline` → `15_full_stack`
6. 真实领域：`17_string_processing` … `30_game_loop_demo`
7. 近期语言特性：`33_pattern_matching`、`36_try_catch`、`37_async_await`、`38_generics`、`39_visibility_attrs`、`40_string_literals`

---

# 9. 开发工作流

## 9.1 典型循环

```
1. 修改代码
2. 构建：       cmake --build build -j
3. 测试：       (cd build && ctest --output-on-failure -R <target>)
4. 迭代
5. 验证：       (cd build && ctest --output-on-failure -LE benchmark)
```

## 9.2 增量构建

Ninja 仅重编受影响的文件。`compile_commands.json` 默认生成（`-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`），`clangd` 与 IDE IntelliSense 直接可用。

## 9.3 全量重建

```bash
cmake --build build --target clean && cmake --build build -j
# 或
rm -rf build && cmake -B build -G Ninja && cmake --build build -j
```

## 9.4 推送前的文档与格式闸门

```bash
# 格式 dry-run
find common middle frontends backends runtime tools \
  -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.c' \) \
  -not -path '*/deps/*' -not -path '*/.cache/*' -print0 \
  | xargs -0 clang-format --dry-run --Werror --style=file

# 双语文档同步闸门（核心文档）
python scripts/docs_sync_check.py --ci --scope core

# 路径 / 标题 / 版本 lint
python scripts/docs_lint.py
```

---

# 10. 前端开发

## 10.1 布局

```
frontends/<language>/
├── include/
│   ├── <lang>_ast.h        # AST 节点定义（拆分时使用）
│   ├── <lang>_lexer.h
│   ├── <lang>_parser.h
│   ├── <lang>_sema.h
│   └── <lang>_lowering.h
└── src/
    ├── lexer/
    ├── parser/
    ├── sema/
    └── lowering/
```

## 10.2 Lexer

Lexer 共享 `frontend_common::SharedTokenPool`，整个编译单元内的标识符内化与 lexeme 分配只支付一次：

```cpp
Token Lex(const std::string_view source) {
    auto handle = pool_.Intern(source);              // 开放寻址标识符表
    return Token{TokenKind::kIdentifier, handle, loc_};
}
```

## 10.3 Parser

Parser 可对 token pool 做快照/恢复，支持任意长度回溯：

```cpp
auto snapshot = pool_.Snapshot();
auto maybe_decl = TryParseDecl();
if (!maybe_decl) {
    pool_.Restore(snapshot);
}
```

## 10.4 Sema

Sema 负责名字解析、类型检查、语言特定的校验，并通过共享 `Diagnostics` 输出诊断。错误携带源位置（文件、行、列、长度）与稳定的诊断 id（如 `polyc-err-E3010`）。

## 10.5 Lowering

Lowering 通过 `middle_ir` 的 `IRBuilder` 将带类型注解的 AST 转换为 SSA IR。`LET` 转为 `alloca` + `store`，`CALL` 转为带类型的 call 指令，控制流转为带显式分支的基本块。

---

# 11. 中间层与 IR

## 11.1 IR 设计

- SSA 形式，按 **函数 → 基本块 → 指令** 组织。
- 类型与源语言无关（`i1`、`i8`、`i16`、`i32`、`i64`、`f32`、`f64`、`void`、`T*`、struct、array）。
- 调用都带类型；跨语言调用携带显式 ABI 标签，供链接器粘合代码生成器消费。

## 11.2 优化 Pass

| Pass                  | 类别       | 描述                                            |
|-----------------------|------------|-------------------------------------------------|
| 常量折叠               | Transform  | 编译期求值常量表达式                             |
| 死代码消除             | Transform  | 移除不可达 / 未使用的 IR                          |
| GVN                   | Transform  | 全局值编号                                       |
| 内联                   | Transform  | 在预算内将 call 替换为函数体                     |
| 去虚化                 | Transform  | 把虚调用解析为直接调用                           |
| 循环优化               | Transform  | LICM、展开、向量化                               |
| LCSSA                 | Analysis   | 循环 closed SSA，循环变换的前置                  |
| PGO 重写               | Transform  | 用 profile 数据重写内联 / 布局决策               |
| LTO 摘要               | Analysis   | 为 LTO 链接阶段构造全程序摘要                    |

## 11.3 IR Builder

```cpp
auto* func   = module.CreateFunction("compute", i32_, {i32_, i32_});
auto* entry  = func->CreateBlock("entry");
builder.SetInsertPoint(entry);
auto* sum    = builder.CreateAdd(func->arg(0), func->arg(1));
builder.CreateRet(sum);
```

---

# 12. 后端开发

## 12.1 布局

```
backends/<arch>/
├── include/
│   ├── <arch>_target.h
│   ├── <arch>_register.h
│   └── machine_ir.h
└── src/
    ├── isel/
    ├── regalloc/
    │   ├── graph_coloring.cpp
    │   └── linear_scan.cpp
    ├── asm_printer/
    └── calling_convention/
```

## 12.2 要点

- **x86_64**：System V（Linux / macOS）+ Win64 调用约定；SSE2 / AVX / AVX2 / AVX-512 向量化。
- **ARM64**：AAPCS64；NEON SIMD；macOS arm64 的 Mach-O 段按 16 KiB 对齐。
- **WASM**：WAT 与二进制双输出，影子栈，运行时桥经 WASI imports 暴露。

---

# 13. 新增语言前端

1. 创建 `frontends/<new_lang>/{include,src}` 与四阶段管线文件。
2. 在 `frontends/CMakeLists.txt` 添加 CMake 目标：
    ```cmake
    add_library(frontend_new_lang ...)
    target_include_directories(frontend_new_lang PUBLIC frontends/new_lang/include)
    target_link_libraries(frontend_new_lang PUBLIC polyglot_common frontend_common middle_ir)
    ```
3. 在 `*_frontend.cpp` 向 `FrontendRegistry` 注册：
    ```cpp
    static FrontendRegistration g_registration{
        /*language=*/"new_lang",
        /*extensions=*/{".nlg"},
        /*factory=*/[]() { return std::make_unique<NewLangFrontend>(); }
    };
    ```
4. 在 `tools/polyc/src/driver.cpp` 把后缀路由到新工厂。
5. 添加测试：
    - `tests/unit/frontends/<new_lang>/`：新增 Catch2 标签下的用例。
    - 添加与现有 per-frontend 二进制对齐的 `test_frontend_<new_lang>` CMake 目标。
6. 至少新增一个 `tests/samples/` 样例。
7. 更新 `docs/realization/<new_lang>_frontend.md`（双语）以及 `docs/specs/` 中的语言表。

集成 checklist：

- [ ] Lexer 覆盖全部关键字、运算符与字面量。
- [ ] Parser 与该语言参考文法一致。
- [ ] Sema 强制类型规则并发出语言特定诊断。
- [ ] Lowering 通过 SSA IR 保持可观察语义。
- [ ] `polyc` 驱动按后缀识别该语言。
- [ ] `test_frontend_<new_lang>` 已接入 `tests/CMakeLists.txt`。
- [ ] 至少一个 `tests/samples/` 样例在 `samples_regression` 中跑到 OK。

---

# 14. 插件、设置与主题

## 14.1 插件

插件为共享库（`polyplug_*.so` / `polyplug_*.dylib` / `polyplug_*.dll`），从设置中的搜索路径自动发现或按绝对路径加载。通过稳定的 C ABI 暴露能力：

| 能力              | 说明                                                                |
|-------------------|---------------------------------------------------------------------|
| Language          | 通过 `FrontendRegistry` / `ILanguageFrontend` 添加前端              |
| Optimiser         | 添加自定义优化 Pass                                                 |
| Backend           | 添加新代码生成目标                                                   |
| Tool              | 添加 CLI 工具或管线阶段                                              |
| UI Panel          | 添加 IDE 面板 / 停靠组件                                             |
| Code Action       | 快速修复 / 重构动作                                                  |
| Formatter / Linter / Debugger / Completion / Diagnostic / Template / Topology Proc | 按语言扩展编辑器能力 |

每个插件在元数据声明 `min_host_version`，宿主在加载时强制版本兼容性，拒绝不兼容的插件。回调反复失败时熔断器自动禁用插件（可通过 `SandboxPolicy` 配置）。

## 14.2 设置

三层 JSON，与 VS Code 一致：

1. 默认设置（内置，按 `common/include/settings.schema.json` 校验）。
2. `~/.polyglot/settings.json`（用户级）。
3. `<workspace>/.polyglot/settings.json`（工作区级）。

每个 CLI 工具都通过 `--settings <path>` 与 `--print-effective-settings` 共享同一份文件。`polyui` 在保存时热重载（Schema 拦截无效键）。

## 14.3 主题

外置 `.polytheme.json`（可附 `.qss`）按三层契约发现：内置 qrc / `~/.polyglot/themes/` / `<workspace>/.polyglot/themes/`。内置主题：`polyglot.dark`、`polyglot.light`、`polyglot.hc`、`solarized.light`、`solarized.dark`。CLI：`--theme`、`--list-themes`、`--validate-theme`、`--headless --screenshot`。

---

# 15. 调试与排错

## 15.1 构建问题

| 现象                                                          | 原因 / 解决                                                                                      |
|---------------------------------------------------------------|--------------------------------------------------------------------------------------------------|
| `LNK1112: module machine type 'x86' conflicts …`（Windows）   | MSVC 环境是 x86，必须使用 `VsDevCmd.bat -arch=amd64`。                                           |
| `CMake Error: CMake 3.20 or higher is required`               | 升级 CMake。                                                                                     |
| `Could not find Ninja`                                        | 安装 Ninja 并加入 `PATH`。                                                                       |
| Apple Clang 21 上 `fmt` 编译失败                              | 重新配置以拉取 fmt ≥ 11.2.0。                                                                    |

## 15.2 样例 / 运行时问题

- **缺少按语言的工具链** — 运行 `tests/samples/setup_env.{sh,ps1}` 并重新执行 `polyver detect`。
- **需求大幅变化后构建陈旧** — `cmake --build build --target clean && cmake --build build -j`。
- **macOS arm64 二进制启动即异常** — 确认 `polyld` ≥ 1.45.2（写入器按 16 KiB 对齐段并对 `__DATA_CONST` 标记 `SG_READ_ONLY`）。
- **`execve(2)` 返回 137** — POSIX 宿主必须输出 `0755` 权限位；旧版 `polyld` 输出 `0644`。升级即可。

## 15.3 诊断工具箱

```bash
polyc --emit-ir=debug.ir input.ploy            # 查看 IR
polyc --check input.ploy | jq '.diagnostics[]' # LSP 形态的 JSON 诊断
ctest -V --output-on-failure -R <target>       # 详细 CTest
./test_frontend_ploy "<test name>" -s          # Catch2 详细
polyrt async --json                             # 事件循环快照
polytopo --view-mode=dot build/app.cgjson | dot -Tsvg > app.svg
```

---

# 16. 项目配置参考

## 16.1 `CMakeLists.txt`

根 CMake 文件声明：

1. 项目元数据（名称、**版本**、语言）。
2. C++20 标准。
3. 引入 `Dependencies.cmake`。
4. 各组件库目标。
5. 各工具可执行目标。
6. CTest 配置（per-module + 聚合）。

项目版本号位于 `project(PolyglotCompiler VERSION X.Y.Z LANGUAGES C CXX)`。修改后 CMake 自动重新生成 `common/include/version.h` 与 `VERSION.txt`；C++ 源文件统一通过 `common/include/version.h` 取版本号，禁止硬编码字面量。

## 16.2 `Dependencies.cmake`

通过 `FetchContent_Declare` / `FetchContent_MakeAvailable` 声明外部依赖。版本固定在此处更新，禁止散落到组件 CMake。

## 16.3 关键 CMake 变量

| 变量                                | 默认值          | 说明                                                                       |
|-------------------------------------|-----------------|----------------------------------------------------------------------------|
| `CMAKE_BUILD_TYPE`                  | `Debug`         | 构建配置                                                                   |
| `CMAKE_CXX_STANDARD`                | `20`            | C++ 标准                                                                   |
| `CMAKE_EXPORT_COMPILE_COMMANDS`     | `ON`            | 生成 `compile_commands.json`                                               |
| `BUILD_SHARED_LIBS`                 | `ON`            | 共享库构建公共代码                                                         |
| `POLYGLOT_VERSION_SUFFIX`           | （空）          | 追加到正式版本号的预发布后缀（如 `-pre.1`）                                |
| `POLYGLOT_ENABLE_ASAN` / `_UBSAN`   | `OFF`           | 启用 AddressSanitizer / UndefinedBehaviorSanitizer                         |
| `POLYGLOT_ENABLE_COVERAGE`          | `OFF`           | 启用 lcov / gcov 覆盖率插桩                                                |

## 16.4 文档地图

| 路径                  | 内容                                                            |
|-----------------------|-----------------------------------------------------------------|
| `docs/api/`           | API 参考（双语）                                                |
| `docs/specs/`         | 语言与 IR 规范、运行时 ABI、插件规范、打包                      |
| `docs/realization/`   | 实现细节（双语）                                                |
| `docs/tutorial/`      | 教程（本文件位于此处）                                          |
| `docs/demand/`        | 项目需求日志                                                    |
| `docs/USER_GUIDE.md`  | 完整用户指南（英文）                                            |
| `docs/USER_GUIDE_zh.md` | 完整用户指南（中文）                                          |
| `docs/CHANGELOG.md`   | 发版历史（英文）                                                |
| `docs/CHANGELOG_zh.md` | 发版历史（中文）                                               |

## 16.5 CI 质量闸门

`.github/workflows/ci.yml` 强制以下闸门：

| 闸门            | 工具                                          | 描述                                                                                              |
|-----------------|-----------------------------------------------|---------------------------------------------------------------------------------------------------|
| Docs Lint       | `docs_lint.py` / `docs_sync_check.py`         | 路径引用、核心双语同步、标题结构、版本一致性                                                       |
| Format          | `clang-format-17`                             | 强制 `.clang-format`（`Standard: c++20`）                                                          |
| Static Analysis | `clang-tidy-17`                               | 扫描 `common/middle/frontends/backends/runtime/tools` 下全部 `.cpp`（无 50 文件上限）              |
| Sanitizers      | ASan + UBSan                                  | 跑全部非基准套件（`-LE benchmark`）                                                               |
| Coverage        | `lcov` / `gcov`                               | 非基准测试的行覆盖率                                                                              |
| Benchmark Smoke | `benchmark_fast`                              | `[fast]` Catch2 子集                                                                              |
| Binary Matrix   | `scripts/ci/run_binary_matrix.{sh,ps1}`       | Linux / macOS / Windows 上的 `--target` × `--container` 矩阵                                       |
| Samples Harness | `samples_regression` + `build_all_samples.*`  | 驱动 `tests/samples/**`，`expected_output.txt` 按字节固定 stdout，`expected_output.skip` 跳过      |

---

*由 PolyglotCompiler 团队维护*  
*更新日期：2026-05-07*  
*文档版本：v3.0.0*
