# PolyglotCompiler 项目教程

> **版本**: 2.0.0
> **最后更新**: 2026-02-22
> **项目**: PolyglotCompiler  

---

## 目录

1. [项目概述](#1-项目概述)
2. [环境要求](#2-环境要求)
3. [从源码构建](#3-从源码构建)
4. [项目架构](#4-项目架构)
5. [使用工具链](#5-使用工具链)
6. [运行测试](#6-运行测试)
7. [使用示例程序](#7-使用示例程序)
8. [开发工作流](#8-开发工作流)
9. [前端开发](#9-前端开发)
10. [中间层与 IR](#10-中间层与-ir)
11. [后端开发](#11-后端开发)
12. [添加新语言前端](#12-添加新语言前端)
13. [调试与故障排除](#13-调试与故障排除)
14. [项目配置参考](#14-项目配置参考)

---

# 1. 项目概述

## 1.1 什么是 PolyglotCompiler？

PolyglotCompiler 是一个使用 **C++20** 构建的现代多语言编译器项目。它采用多前端共享中间表示（IR）架构，并通过 `.ploy` 领域特定语言实现跨语言函数级链接。

### 核心能力

- **6 个语言前端**：C++、Python、Rust、Java、C#（.NET）、.ploy
- **3 个架构后端**：x86_64、ARM64、WebAssembly
- **7 个工具链可执行文件**：`polyc`、`polyld`、`polyasm`、`polyopt`、`polyrt`、`polybench`、`polyui`（IDE，需要 Qt）
- **完整编译流水线**：词法分析 → 语法分析 → 语义分析 → IR 生成 → 优化 → 代码生成
- **跨语言 OOP**：通过 `.ploy` 实现完整互操作（NEW、METHOD、GET、SET、WITH、DELETE、EXTEND）
- **包管理器集成**：支持 pip、conda、uv、pipenv、poetry、cargo、NuGet

## 1.2 技术栈

| 组件 | 技术 |
|------|------|
| 语言 | C++20 |
| 构建系统 | CMake 3.20+ |
| 构建工具 | Ninja（推荐）|
| 测试框架 | Catch2 v3 |
| 格式化 | fmt 库 |
| JSON | nlohmann_json |
| 内存分配器 | mimalloc |
| 许可证 | GPL v3 |

---

# 2. 环境要求

## 2.1 必需软件

### 编译器

需要一个支持 C++20 的编译器：

| 平台 | 推荐编译器 | 最低版本 |
|------|-----------|---------|
| Windows | MSVC（Visual Studio 2022+）| v19.30+ |
| Linux | GCC | 12+ |
| macOS | Clang | 15+ |

### 构建工具

| 工具 | 版本 | 说明 |
|------|------|------|
| CMake | 3.20+ | 构建系统生成器 |
| Ninja | 任何近期版本 | 推荐的构建工具（比 Make 更快）|

### 可选（用于跨语言示例）

| 工具 | 用途 |
|------|------|
| Python 3.8+ | Python 前端和示例 |
| Rust（rustup）| Rust 前端和示例 |
| Java JDK 21+ | Java 前端和示例 |
| .NET SDK 9+ | .NET/C# 前端和示例 |

## 2.2 环境设置脚本

项目在 `tests/samples/` 下提供了自动化设置脚本：

### Windows（PowerShell）

```powershell
cd tests\samples
.\setup_env.ps1
```

此脚本将：
- 创建 Python 虚拟环境
- 安装 Rust（通过 `rustup`，如未安装）
- 安装 Java JDK 21（如未安装）
- 安装 .NET SDK 9（如未安装）
- 将环境链接到所有示例目录

### Linux/macOS（Bash）

```bash
cd tests/samples
chmod +x setup_env.sh
./setup_env.sh
```

---

# 3. 从源码构建

## 3.1 克隆仓库

```bash
git clone <repository-url>
cd PolyglotCompiler
```

## 3.2 在 Windows 上构建（MSVC + Ninja）

**重要**：必须使用带 `-arch=amd64` 的开发者命令提示符，以避免 x86/x64 链接器不匹配。

```powershell
# 步骤 1：打开开发者命令提示符
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64

# 步骤 2：创建构建目录并生成构建文件
mkdir build
cd build
cmake .. -G Ninja

# 步骤 3：构建所有目标
ninja

# 或构建特定目标
ninja polyc
ninja unit_tests
```

### VS Code 构建快捷方式

如果使用 VS Code 配合 CMake Tools 扩展，项目支持直接的 CMake 构建集成。

## 3.3 在 Linux/macOS 上构建

```bash
# 创建构建目录
mkdir -p build && cd build

# 生成构建文件（推荐使用 Ninja）
cmake .. -G Ninja

# 构建
ninja

# 或使用 Make
cmake .. -G "Unix Makefiles"
make -j$(nproc)
```

## 3.4 构建目标

| 目标 | 描述 | 可执行文件 |
|------|------|-----------|
| `polyc` | 编译器驱动程序 | `polyc` / `polyc.exe` |
| `polyld` | 链接器 | `polyld` / `polyld.exe` |
| `polyasm` | 汇编器 | `polyasm` / `polyasm.exe` |
| `polyopt` | 优化器 | `polyopt` / `polyopt.exe` |
| `polyrt` | 运行时工具 | `polyrt` / `polyrt.exe` |
| `polybench` | 基准测试套件 | `polybench` / `polybench.exe` |
| `polyui` | 桌面 IDE（需要 Qt） | `polyui` / `polyui.exe` |
| `unit_tests` | 测试可执行文件 | `unit_tests` / `unit_tests.exe` |

### 构建特定目标

```bash
# 仅构建编译器驱动程序
ninja polyc

# 仅构建测试可执行文件
ninja unit_tests

# 构建所有
ninja
```

## 3.5 CMake 依赖

以下库由 CMake **自动获取**（无需手动安装）：

| 库 | 用途 |
|----|------|
| **Catch2** | 单元测试框架 |
| **fmt** | 字符串格式化 |
| **nlohmann_json** | JSON 解析和序列化 |
| **mimalloc** | 高性能内存分配器 |

这些在 `Dependencies.cmake` 中配置，下载到 `build/_deps/`。

## 3.6 构建配置

```bash
# Debug 构建（默认）
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Debug

# Release 构建（已优化）
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release

# 带调试信息的 Release
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
```

---

# 4. 项目架构

## 4.1 目录结构

```
PolyglotCompiler/
├── frontends/              # 语言前端（6 个前端）
│   ├── common/             #   共享前端设施（Token、Diagnostics）
│   ├── cpp/                #   C++ 前端
│   ├── python/             #   Python 前端
│   ├── rust/               #   Rust 前端
│   ├── java/               #   Java 前端（Java 8/17/21/23）
│   ├── dotnet/             #   .NET 前端（C# .NET 6/7/8/9）
│   └── ploy/               #   .ploy 跨语言前端
├── middle/                 # 中间层（IR + 优化）
│   ├── include/
│   │   ├── ir/             #   IR 定义
│   │   ├── passes/         #   优化遍接口
│   │   ├── pgo/            #   配置文件引导优化
│   │   └── lto/            #   链接时优化
│   └── src/
│       ├── ir/             #   IR 构建器、上下文、CFG、SSA
│       ├── passes/         #   遍管理器、优化
│       ├── pgo/            #   配置文件数据
│       └── lto/            #   链接时优化器
├── backends/               # 架构后端（3 个后端）
│   ├── common/             #   共享后端设施
│   ├── x86_64/             #   x86_64 后端
│   ├── arm64/              #   ARM64 后端
│   └── wasm/               #   WebAssembly 后端
├── runtime/                # 运行时系统
│   ├── include/            #   GC、FFI、服务接口
│   └── src/
│       ├── gc/             #   垃圾回收器
│       ├── interop/        #   FFI、编排、类型映射
│       ├── libs/           #   特定语言的运行时桥接
│       └── services/       #   异常、反射、线程
├── common/                 # 项目级通用设施
│   ├── include/
│   │   ├── core/           #   类型系统、源位置、符号
│   │   ├── ir/             #   IR 节点定义
│   │   ├── debug/          #   DWARF5 调试信息
│   │   └── utils/          #   工具
│   └── src/
├── tools/                  # 工具链可执行文件（7 个工具）
│   ├── polyc/              #   编译器驱动程序
│   ├── polyld/             #   链接器
│   ├── polyasm/            #   汇编器
│   ├── polyopt/            #   优化器
│   ├── polyrt/             #   运行时工具
│   ├── polybench/          #   基准测试套件
│   └── ui/                 #   桌面 IDE（polyui）— 基于 Qt，语法高亮、诊断
├── tests/                  # 测试
│   ├── unit/               #   Catch2 单元测试
│   ├── samples/            #   16 个示例程序
│   ├── integration/        #   集成测试
│   └── benchmarks/         #   性能基准测试
├── docs/                   # 文档
│   ├── api/                #   API 参考（中英文）
│   ├── specs/              #   语言和 IR 规范（中英文）
│   ├── tutorial/           #   教程（中英文）
│   ├── realization/        #   实现文档
│   └── demand/             #   需求文档
├── CMakeLists.txt          # 根 CMake 配置
└── Dependencies.cmake      # 外部依赖声明
```

## 4.2 编译流水线

```
源代码 (.cpp / .py / .rs / .java / .cs / .ploy)
    │
    ▼
┌─────────┐   Token 流   ┌─────────┐    AST    ┌─────────┐   标注后的 AST   ┌──────────┐    IR
│ 词法分析 │────────────▶│ 语法分析 │────────▶│ 语义分析 │──────────────▶│ IR 生成  │──────▶
└─────────┘              └─────────┘          └─────────┘                └──────────┘
                                                                              │
                                                                              ▼
                                                                     ┌──────────────┐
                                                                     │  共享 IR      │
                                                                     │ (SSA + CFG)   │
                                                                     └──────┬───────┘
                                                                            │
                                                                  ┌─────────┼─────────┐
                                                                  ▼         ▼         ▼
                                                              ┌──────┐ ┌──────┐ ┌──────┐
                                                              │ 优化 │ │ SSA  │ │ CFG  │
                                                              │ 遍   │ │ 转换 │ │ 构建 │
                                                              └──┬───┘ └──┬───┘ └──┬───┘
                                                                 └────────┼────────┘
                                                                          ▼
                                                                 ┌──────────────────────┐
                                                                 │ 后端                 │
                                                                 │ (x86_64/ARM64/WASM)  │
                                                                 └──────────┬───────────┘
                                                                          │
                                                                 ┌────────┼────────┐
                                                                 ▼        ▼        ▼
                                                             ┌──────┐ ┌──────┐ ┌──────┐
                                                             │指令选│ │寄存器│ │汇编  │
                                                             │ 择   │ │ 分配 │ │ 生成 │
                                                             └──┬───┘ └──┬───┘ └──┬───┘
                                                                └────────┼────────┘
                                                                         ▼
                                                                ┌───────────────┐
                                                                │ 目标文件      │
                                                                └───────────────┘
```

## 4.3 前端架构

每个前端（C++、Python、Rust、Java、.NET、.ploy）遵循统一的 **4 阶段流水线**：

| 阶段 | 类 | 职责 |
|------|-----|------|
| **词法分析** | `*_lexer.h` | 将源文本分词为 Token 流 |
| **语法分析** | `*_parser.h` | 将 Token 解析为抽象语法树（AST）|
| **语义分析** | `*_sema.h` | 类型检查、名称解析、语义验证 |
| **IR 生成** | `*_lowering.h` | 将标注后的 AST 转换为共享 IR |

每个前端位于 `frontends/<语言>/`，包含 `include/` 和 `src/` 子目录。

## 4.4 CMake 库目标

CMake 构建系统定义了以下库目标：

| 目标 | 源目录 | 描述 |
|------|--------|------|
| `polyglot_common` | `common/` | 核心类型、符号、工具 |
| `frontend_common` | `frontends/common/` | 共享 Token 池、诊断 |
| `frontend_cpp` | `frontends/cpp/` | C++ 前端 |
| `frontend_python` | `frontends/python/` | Python 前端 |
| `frontend_rust` | `frontends/rust/` | Rust 前端 |
| `frontend_java` | `frontends/java/` | Java 前端 |
| `frontend_dotnet` | `frontends/dotnet/` | .NET 前端 |
| `middle_ir` | `middle/` | IR、优化遍、PGO、LTO |
| `backend_x86_64` | `backends/x86_64/` | x86_64 后端 |
| `backend_arm64` | `backends/arm64/` | ARM64 后端 |
| `backend_wasm` | `backends/wasm/` | WebAssembly 后端 |
| `runtime` | `runtime/` | 运行时系统 |
| `linker_lib` | `tools/polyld/` | 链接器库 |

---

# 5. 使用工具链

## 5.1 polyc — 编译器驱动程序

`polyc` 驱动程序是编译的主要入口点。它通过文件扩展名自动检测源语言。

### 基本用法

```bash
# 编译 .ploy 文件
polyc sample.ploy -o sample

# 编译 C++ 文件
polyc hello.cpp -o hello

# 编译 Python 文件
polyc script.py -o script

# 编译 Java 文件
polyc Main.java -o main

# 编译 C# 文件
polyc Program.cs -o program
```

### 语言覆盖

```bash
# 强制使用特定的语言前端
polyc --lang=cpp input_file -o output
polyc --lang=python input_file -o output
polyc --lang=ploy input_file -o output
```

### 输出中间表示

```bash
# 生成可读的 IR
polyc --emit-ir=output.ir input.ploy

# 生成汇编
polyc --emit-asm=output.asm input.ploy

# 指定目标架构
polyc --target=x86_64 input.ploy -o output
polyc --target=arm64 input.ploy -o output
```

### 输出产物

编译时，`polyc` 在 `aux/` 子目录中生成中间产物：

```
aux/
├── <filename>.ir         # IR 文本（如使用 --emit-ir）
├── <filename>.ir.bin     # 二进制编码 IR
├── <filename>.asm        # 生成的汇编
├── <filename>.asm.bin    # 二进制编码汇编
├── <filename>.obj        # 目标文件
└── <filename>.symbols    # 符号表转储
```

## 5.2 polyld — 链接器

链接器处理目标文件链接和跨语言胶水代码生成：

```bash
# 链接目标文件
polyld -o output file1.obj file2.obj

# 使用跨语言胶水链接
polyld --polyglot -o output main.obj bridge.obj
```

## 5.3 polyasm — 汇编器

将汇编文件转换为目标文件：

```bash
polyasm input.asm -o output.obj
```

## 5.4 polyopt — 优化器

对 IR 应用优化遍：

```bash
# 应用所有优化
polyopt -O2 input.ir -o optimised.ir

# 应用特定遍
polyopt --pass=constant-fold --pass=dce input.ir -o optimised.ir
```

## 5.5 polyrt — 运行时工具

管理运行时组件：

```bash
# 启动运行时服务
polyrt --gc-mode=generational --threads=4
```

## 5.6 polybench — 基准测试

运行性能基准测试：

```bash
polybench --suite=micro
polybench --suite=macro
polybench --all
```

---

# 6. 运行测试

## 6.1 Catch2 测试框架

项目使用 **Catch2 v3** 作为测试框架。所有单元测试位于 `tests/unit/`。

### 运行所有测试

```bash
cd build
./unit_tests          # Linux/macOS
unit_tests.exe        # Windows
```

### 使用标签运行测试

```bash
# 仅运行 .ploy 前端测试
./unit_tests "[ploy]"

# 仅运行 Java 前端测试
./unit_tests "[java]"

# 仅运行 .NET 前端测试
./unit_tests "[dotnet]"

# 运行多个标签
./unit_tests "[ploy],[java]"
```

### 测试统计

项目当前有：

| 类别 | 测试数 | 断言数 |
|------|--------|--------|
| .ploy 前端 | 207 | 599 |
| Java 前端 | 22 | 77 |
| .NET 前端 | 24 | 77 |
| **总计** | **253** | **753** |

## 6.2 CTest 集成

也可以通过 CTest 运行测试：

```bash
cd build
ctest --output-on-failure
ctest -R "ploy"      # 运行匹配 "ploy" 的测试
ctest -V              # 详细输出
```

## 6.3 测试文件结构

```
tests/
├── unit/
│   └── frontends/
│       └── ploy/
│           └── ploy_test.cpp    # 主 .ploy 测试文件（171+ 测试用例）
├── samples/                      # 16 个示例程序
├── integration/                  # 集成测试
└── benchmarks/                   # 性能基准测试
```

---

# 7. 使用示例程序

## 7.1 示例目录概览

项目在 `tests/samples/` 下包含 **16 个分类示例程序**：

| # | 文件夹 | 特性 | 语言 |
|---|--------|------|------|
| 01 | `01_basic_linking/` | LINK、CALL、IMPORT、EXPORT | C++、Python |
| 02 | `02_type_mapping/` | MAP_TYPE、STRUCT、容器 | C++、Python |
| 03 | `03_pipeline/` | PIPELINE、控制流 | C++、Python |
| 04 | `04_package_import/` | IMPORT PACKAGE、CONFIG | C++、Python |
| 05 | `05_class_instantiation/` | NEW、METHOD | C++、Python |
| 06 | `06_attribute_access/` | GET、SET | C++、Python |
| 07 | `07_resource_management/` | WITH | C++、Python |
| 08 | `08_delete_extend/` | DELETE、EXTEND | C++、Python |
| 09 | `09_mixed_pipeline/` | 所有特性 | C++、Python、Rust |
| 10 | `10_error_handling/` | 错误检查 | C++、Python |
| 11 | `11_java_interop/` | NEW、METHOD（Java）| Java、Python |
| 12 | `12_dotnet_interop/` | NEW、METHOD（.NET）| C#、Python |
| 13 | `13_generic_containers/` | MAP_TYPE 容器 | C++、Java、Python |
| 14 | `14_async_pipeline/` | PIPELINE、IF/ELSE | C++、Rust、Python |
| 15 | `15_full_stack/` | 全部 5 种语言 | C++、Python、Rust、Java、C# |
| 16 | `16_config_and_venv/` | CONFIG、IMPORT PACKAGE、CONVERT | Python、C# |

## 7.2 示例结构

每个示例目录包含：

```
XX_feature_name/
├── feature_name.ploy        # .ploy 源文件
├── source_file.cpp          # C++ 源文件（如适用）
├── source_file.py           # Python 源文件（如适用）
├── source_file.rs           # Rust 源文件（如适用）
├── SourceFile.java          # Java 源文件（如适用）
└── SourceFile.cs            # C# 源文件（如适用）
```

## 7.3 编译示例

```bash
# 编译任意示例
polyc tests/samples/01_basic_linking/basic_linking.ploy -o basic_linking

# 编译混合管道
polyc tests/samples/09_mixed_pipeline/mixed_pipeline.ploy -o ml_pipeline

# 编译全栈示例
polyc tests/samples/15_full_stack/full_stack.ploy -o full_stack
```

## 7.4 学习路径

建议按以下顺序学习示例：

1. **从基础开始**：`01_basic_linking` → `02_type_mapping`
2. **学习控制流**：`03_pipeline`（IF、WHILE、FOR、MATCH）
3. **包管理**：`04_package_import`
4. **OOP 特性**：`05_class_instantiation` → `06_attribute_access` → `07_resource_management` → `08_delete_extend`
5. **组合特性**：`09_mixed_pipeline`
6. **错误处理**：`10_error_handling`
7. **多语言**：`11_java_interop` → `12_dotnet_interop` → `13_generic_containers`
8. **高级**：`14_async_pipeline` → `15_full_stack` → `16_config_and_venv`

---

# 8. 开发工作流

## 8.1 典型工作流

```
1. 修改代码
2. 构建：       ninja -C build
3. 运行测试：   build/unit_tests
4. 修复问题：   在 1-3 之间迭代
5. 验证：       build/unit_tests      （全部 253 个测试通过）
```

## 8.2 增量构建

Ninja 支持快速增量构建 — 仅重新编译更改的文件：

```bash
# 更改后重新构建
cd build
ninja
```

## 8.3 全量清理构建

```bash
# 完全清理重建
cd build
ninja clean
ninja
```

或完全删除构建目录：

```bash
rm -rf build
mkdir build && cd build
cmake .. -G Ninja
ninja
```

## 8.4 编译命令

用于 IDE 集成（IntelliSense、clangd 等），`compile_commands.json` 在构建目录中生成：

```bash
cmake .. -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

此文件通常位于 `build/compile_commands.json`。

---

# 9. 前端开发

## 9.1 前端结构

每个前端遵循相同的目录布局：

```
frontends/<语言>/
├── include/
│   ├── <lang>_ast.h        # AST 节点定义（如独立文件）
│   ├── <lang>_lexer.h      # 词法分析器接口
│   ├── <lang>_parser.h     # 语法分析器接口
│   ├── <lang>_sema.h       # 语义分析器接口
│   └── <lang>_lowering.h   # IR 生成接口
└── src/
    ├── lexer/               # 词法分析器实现
    │   └── lexer.cpp
    ├── parser/              # 语法分析器实现
    │   └── parser.cpp
    ├── sema/                # 语义分析实现
    │   └── sema.cpp
    └── lowering/            # IR 生成实现
        └── lowering.cpp
```

## 9.2 词法分析器开发

词法分析器将源文本转换为 Token 流：

```cpp
// Example: Tokenising a keyword
Token LexKeyword(const std::string& word) {
    if (word == "FUNC") return Token{TokenKind::kFunc, word, loc_};
    if (word == "LET")  return Token{TokenKind::kLet, word, loc_};
    // ...
}
```

关键注意事项：
- 处理所有语言特定的关键字
- 支持字符串/数字/标识符字面量
- 跟踪源位置以用于错误报告

## 9.3 语法分析器开发

语法分析器将 Token 转换为 AST：

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

## 9.4 语义分析

语义分析器验证 AST 并用类型信息标注：

- 名称解析（符号表查找）
- 类型检查（参数类型、返回类型）
- 跨语言验证（LINK 目标、包版本）
- 带源位置的错误报告

## 9.5 IR 生成

IR 生成阶段将标注后的 AST 转换为共享 IR：

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

# 10. 中间层与 IR

## 10.1 IR 设计

共享 IR 使用基于 SSA 的表示，包含：

- **基本块** 用于控制流
- **指令** 用于操作（算术、内存、控制流、调用）
- **类型** 独立于源语言（i32、f64、void、指针等）

## 10.2 IR 类型

| IR 类型 | 大小 | 描述 |
|---------|------|------|
| `i1` | 1 位 | 布尔 |
| `i8` | 1 字节 | 字节 |
| `i16` | 2 字节 | 短整数 |
| `i32` | 4 字节 | 32 位整数 |
| `i64` | 8 字节 | 64 位整数 |
| `f32` | 4 字节 | 单精度浮点 |
| `f64` | 8 字节 | 双精度浮点 |
| `void` | 0 | 无值 |
| `T*` | 指针大小 | 指向 T 的指针 |

## 10.3 优化遍

优化器支持以下优化遍：

| 遍 | 类别 | 描述 |
|-----|------|------|
| 常量折叠 | 转换 | 在编译时求值常量表达式 |
| 死代码消除 | 转换 | 移除不可达/未使用的代码 |
| 公共子表达式消除 | 转换 | 重用之前计算的值 |
| 函数内联 | 转换 | 用函数体替换调用点 |
| 去虚拟化 | 转换 | 将虚拟调用解析为直接调用 |
| 循环优化 | 转换 | LICM、展开、向量化 |
| GVN | 转换 | 全局值编号 |
| SSA 转换 | 分析 | 转换为 SSA 形式 |
| CFG 构建 | 分析 | 构建控制流图 |

## 10.4 IR 构建器 API

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

# 11. 后端开发

## 11.1 后端结构

```
backends/<架构>/
├── include/
│   ├── <arch>_target.h       # 目标描述
│   ├── <arch>_register.h     # 寄存器定义
│   └── machine_ir.h          # 机器 IR 定义
└── src/
    ├── isel/                  # 指令选择
    ├── regalloc/              # 寄存器分配
    │   ├── graph_coloring.cpp
    │   └── linear_scan.cpp
    ├── asm_printer/           # 汇编输出
    └── calling_convention/    # 调用约定实现
```

## 11.2 x86_64 后端

- **指令选择**：将 IR 指令映射到 x86_64 指令
- **寄存器分配**：图着色和线性扫描算法
- **调用约定**：System V ABI（Linux/macOS）和 Win64 ABI（Windows）
- **SIMD**：SSE2、AVX、AVX2、AVX-512 支持

## 11.3 ARM64 后端

- **指令选择**：将 IR 指令映射到 AArch64 指令
- **寄存器分配**：图着色和线性扫描算法
- **调用约定**：AAPCS64
- **SIMD**：NEON、SVE 支持

---

# 12. 添加新语言前端

## 12.1 步骤

1. **创建目录结构**：
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

2. **在 `CMakeLists.txt` 中添加 CMake 目标**：
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

3. **实现 4 个阶段**：词法分析 → 语法分析 → 语义分析 → IR 生成。

4. **注册到 polyc 驱动程序**：在 `tools/polyc/driver.cpp` 中添加新语言的检测和分发逻辑。

5. **添加测试**：在 `tests/unit/frontends/new_lang/` 中创建测试用例。

6. **添加示例**：在 `tests/samples/` 中创建示例程序。

## 12.2 集成检查清单

- [ ] 词法分析器能正确分词所有语言关键字和运算符
- [ ] 语法分析器为所有支持的构造生成正确的 AST
- [ ] 语义分析器验证类型、名称和语言特定规则
- [ ] IR 生成器为所有 AST 节点生成正确的 IR
- [ ] polyc 驱动程序检测文件扩展名并正确路由
- [ ] 单元测试覆盖所有主要特性
- [ ] 示例程序编译成功

---

# 13. 调试与故障排除

## 13.1 常见构建问题

### x86/x64 链接器不匹配（Windows）

**问题**：关于架构不匹配的链接器错误。

**解决方案**：打开开发者命令提示符时始终使用 `-arch=amd64`：

```powershell
call "...\VsDevCmd.bat" -arch=amd64
```

### CMake 版本过旧

**问题**：`CMake Error: CMake 3.20 or higher is required.`

**解决方案**：将 CMake 更新到 3.20+。

### 找不到 Ninja

**问题**：`CMake Error: Could not find Ninja.`

**解决方案**：安装 Ninja 并确保它在 PATH 中。

## 13.2 常见测试失败

### 缺少环境

部分示例需要语言运行时（Python、Rust、Java、.NET）。运行设置脚本：

```bash
cd tests/samples
./setup_env.sh      # Linux/macOS
.\setup_env.ps1     # Windows
```

### 过期的构建

如果代码更改后测试失败，进行全量清理构建：

```bash
ninja clean && ninja
```

## 13.3 调试技巧

1. **使用 `--emit-ir`** 检查生成的 IR：
   ```bash
   polyc --emit-ir=debug.ir input.ploy
   ```

2. **运行特定测试** 以隔离失败：
   ```bash
   ./unit_tests "[ploy]" -c "test name"
   ```

3. **检查 compile_commands.json** 以解决 IntelliSense 问题。

4. **使用详细的 CTest 输出**：
   ```bash
   ctest -V --output-on-failure
   ```

---

# 14. 项目配置参考

## 14.1 CMakeLists.txt 结构

根 `CMakeLists.txt` 定义了：

1. 项目元数据（名称、版本、语言）
2. C++ 标准（C++20）
3. 引入 `Dependencies.cmake` 获取外部库
4. 每个组件的库目标
5. 每个工具的可执行文件目标
6. 测试配置

## 14.2 Dependencies.cmake

通过 `FetchContent` 声明外部依赖：

```cmake
FetchContent_Declare(catch2 ...)
FetchContent_Declare(fmt ...)
FetchContent_Declare(nlohmann_json ...)
FetchContent_Declare(mimalloc ...)
```

## 14.3 关键 CMake 变量

| 变量 | 默认值 | 描述 |
|------|--------|------|
| `CMAKE_BUILD_TYPE` | `Debug` | 构建配置 |
| `CMAKE_CXX_STANDARD` | `20` | C++ 标准版本 |
| `CMAKE_EXPORT_COMPILE_COMMANDS` | `ON` | 生成 compile_commands.json |

## 14.4 文档结构

| 路径 | 内容 |
|------|------|
| `docs/api/` | API 参考（中英文）|
| `docs/specs/` | 语言和 IR 规范（中英文）|
| `docs/tutorial/` | 教程（中英文）|
| `docs/realization/` | 实现文档 |
| `docs/demand/` | 需求和任务跟踪 |
| `docs/USER_GUIDE.md` | 完整用户指南（英文）|
| `docs/USER_GUIDE_zh.md` | 完整用户指南（中文）|
