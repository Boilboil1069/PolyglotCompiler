# 语言包导入 — 设计文档

## 1. 概述

本文档描述了 `.ploy` 语言中 `IMPORT ... PACKAGE` 功能的设计和实现，该功能允许直接导入目标语言的原生包（如 Python 的 numpy、Rust 的 serde），用于跨语言管道中。

## 2. 动机

> **为什么需要包导入？** 原始的 `IMPORT` 指令只支持两种形式：
> - `IMPORT "path" AS alias;` — 基于文件路径的导入
> - `IMPORT lang::module;` — 限定模块导入
>
> 这两种形式都假设目标是 PolyglotCompiler 生态系统内的已编译模块。然而，实际的跨语言工作流需要访问目标语言的**第三方包**：
> - Python：numpy、scipy、pandas、torch 等
> - Rust：serde、rayon、tokio 等
>
> 没有包导入功能，开发者需要手动编写包装模块来桥接这些包，这违背了 `.ploy` 自动化跨语言互操作的初衷。

## 3. 语法

```
IMPORT 语言 PACKAGE 包路径 [AS 别名] ';'
```

### 3.1 参数说明

| 参数 | 说明 | 必需 |
|------|------|------|
| `语言` | 目标语言标识符（`python`、`rust`、`cpp`、`c`） | 是 |
| `包路径` | 包名称，支持 `.` 分隔的子包路径 | 是 |
| `别名` | 在 `.ploy` 文件中使用的简短名称 | 否 |

> **别名规则：** 如果不指定 `AS 别名`，则使用包名本身作为标识符。例如 `IMPORT python PACKAGE numpy;` 后可直接使用 `numpy::mean`。

### 3.2 语法示例

```ploy
// 基本包导入
IMPORT python PACKAGE numpy;
// -> 使用 numpy::mean, numpy::array 等

// 带别名的包导入
IMPORT python PACKAGE numpy AS np;
// -> 使用 np::mean, np::array 等（更简洁）

// 子包导入
IMPORT python PACKAGE scipy.optimize AS opt;
// -> 使用 opt::minimize, opt::curve_fit 等

// 深层子包路径
IMPORT python PACKAGE numpy.linalg;
// -> 使用 numpy.linalg::inv, numpy.linalg::det 等

// Rust 包导入
IMPORT rust PACKAGE serde;
// -> 使用 serde::serialize, serde::deserialize 等
```

### 3.3 导入后的使用

导入后，包名（或别名）可以在 `LINK` 指令和 `CALL` 表达式中使用：

```ploy
IMPORT python PACKAGE numpy AS np;

// 在 LINK 中使用 — 声明 C++ 函数调用 numpy 的 mean 函数
LINK(cpp, python, compute, np::mean);

// 在 CALL 中使用 — 直接调用 numpy 的 mean 函数
LET result = CALL(python, np::mean, data);
```

> **完整示例：**
> ```ploy
> IMPORT python PACKAGE numpy AS np;
> IMPORT cpp::math_engine;
>
> MAP_TYPE(cpp::double, python::float);
>
> LINK(cpp, python, math_engine::compute, np::mean) {
>     MAP_TYPE(cpp::double, python::float);
> }
>
> PIPELINE stats {
>     FUNC analyze(data: LIST(f64)) -> f64 {
>         LET avg = CALL(python, np::mean, data);
>         LET std = CALL(python, np::std, data);
>         RETURN avg + std;
>     }
> }
> ```

## 4. 实现细节

### 4.1 词法器改动

新增 `PACKAGE` 作为关键字（在 41 个关键字集合中排第 35 位）。

> **影响范围：** 仅修改了 `lexer.cpp` 中的关键字集合，添加一行 `"PACKAGE"`。

### 4.2 AST 改动

`ImportDecl` 节点新增 `package_name` 字段：

```cpp
struct ImportDecl : public Statement {
    std::string module_path;      // 模块或包路径
    std::string alias;            // 可选别名
    std::string language;         // 语言标识符（如 "python"）
    std::string package_name;     // 包名称（如 "numpy"、"scipy.optimize"）
};
```

> **判断逻辑：** 当 `package_name` 非空时，该导入为包导入。

### 4.3 解析器改动

`ParseImportDecl` 根据 `IMPORT` 后面的下一个 Token 决定解析哪种形式：

| 下一个 Token | 形式 | 示例 |
|-------------|------|------|
| 字符串字面量 | 路径导入 | `IMPORT "path" AS alias;` |
| 标识符 + `PACKAGE` | 包导入 | `IMPORT python PACKAGE numpy AS np;` |
| 标识符 + `::` | 限定导入 | `IMPORT cpp::math;` |

> **包名解析流程：**
> 1. 读取语言标识符（如 `python`）
> 2. 消费 `PACKAGE` 关键字
> 3. 读取包名，支持用 `.` 连接的子包路径
>    - 读取 `numpy` → 检查下一个 Token 是否为 `.`
>    - 如果是 `.`，继续读取 → `numpy.linalg`
>    - 重复直到不再是 `.`
> 4. 可选：读取 `AS alias`
> 5. 消费 `;`

### 4.4 语义分析改动

`AnalyzeImportDecl` 的更新逻辑：

1. **验证语言有效性**：确认语言标识符在支持列表中（`cpp`、`c`、`python`、`rust`、`ploy`、`java`、`dotnet`/`csharp`、`javascript`/`js`/`typescript`/`ts`、`ruby`/`rb`、`go`/`golang`）
2. **确定符号名称**：
   - 如果有 `alias` → 使用别名（如 `np`）
   - 否则如果有 `package_name` → 使用包名（如 `numpy`）
   - 否则 → 使用模块路径
3. **注册符号**：以 `Kind::kImport` 类型和语言标识符注册到符号表

### 4.5 降级改动

> **无需改动。** 现有的 `LowerImportDecl` 创建 `__ploy_module_<lang>_<path>` 外部全局引用。由于 `module_path` 字段在解析时已经被设置为包名，降级逻辑自然兼容包导入。

## 5. 运行时集成

包导入在运行时的解析方式取决于目标语言：

### 5.1 Python 包

```
1. 运行时加载 Python 解释器（通过 libpython）
2. 调用 PyImport_ImportModule("numpy") 导入包
3. FFIRegistry 存储导入的模块对象引用
4. 后续函数调用通过 PyObject_CallMethod 执行
```

> **说明：** 系统中必须已安装目标 Python 包。运行时会使用与编译器配置的相同 Python 环境。

### 5.2 Rust 包

```
1. Rust 包被编译为共享库（.so / .dll / .dylib）
2. 运行时通过 DynamicLibrary 加载共享库
3. 使用标准动态链接解析符号
```

### 5.3 C/C++ 包

```
1. C/C++ 包在编译时链接，或作为共享库加载
2. 头文件路径通过编译器配置指定
```

## 6. 错误处理

| 错误情况 | 诊断消息 | 触发条件 |
|---------|---------|---------|
| 不支持的语言 | `unknown language 'java' in IMPORT` | 语言不在支持列表中 |
| 空包名 | `IMPORT module path is empty` | 未提供包名 |
| 重复导入 | `redefinition of symbol 'np'` | 相同别名使用两次 |

## 7. 已支持语言与发现策略

`IMPORT <lang> PACKAGE ...` 现已扩展到 8 个语言族。每种语言通过一个或多个
包管理器命令探测，将结果写入共享的 `PackageDiscoveryCache`，供 sema
预分析阶段使用。

| 语言标识                                 | 发现命令                                                                | 缓存键前缀        |
|-----------------------------------------|-------------------------------------------------------------------------|------------------|
| `python`                                | `pip list --format=freeze`（含 conda/uv/poetry）                         | `python::`       |
| `rust`                                  | `cargo metadata --format-version 1`                                      | `rust::`         |
| `cpp`、`c`                              | pkg-config / 系统头文件扫描                                              | `cpp::`          |
| `java`                                  | `mvn dependency:list` / `gradle dependencies`                            | `java::`         |
| `dotnet`、`csharp`                      | `dotnet list package` / NuGet 缓存扫描                                   | `dotnet::`       |
| `javascript`、`js`、`typescript`、`ts`  | `npm ls --json` / `yarn list --json` / `pnpm list --json`（+ Node 内置模块） | `javascript::`   |
| `ruby`、`rb`                            | `bundle list`（项目内）/ `gem list --local`（+ Ruby 标准库）             | `ruby::`         |
| `go`、`golang`                          | `go list -m all`（+ Go 标准库路径）                                       | `go::`           |

### 7.1 JavaScript / TypeScript

`PackageIndexer::IndexJavaScript` 依次调用三种包管理器，使得 npm / Yarn /
pnpm 任何一种工作流都能被覆盖。其 JSON 解析采用内联实现（不引入
`nlohmann::json`），便于通过 `MockCommandRunner` 进行单测。包管理器输出
解析完毕后，无条件注入 32 个 Node.js 核心模块（`fs`、`http`、`path` 等），
即使项目没有 `package.json` 也能解析 `IMPORT javascript PACKAGE fs::(...)`。

像 `@types/node` 这样的 scoped 名通过对 yarn 形式的 `<name>@<version>`
按**最右侧**的 `@` 切分得以保留。

### 7.2 Ruby

`IndexRuby` 当存在项目路径时优先调用 `bundle list`，否则回退到
`gem list --local`。解析器同时支持 `name (1.2.3)` 和标准库内置 gem 的
`name (default: 2.6.3)` 两种形式。无条件注入约 80 个 Ruby 标准库名
（`set`、`time`、`json`、`csv` 等）。

### 7.3 Go

`IndexGo` 调用 `go list -m all`，按行解析 `<module-path> v<X.Y.Z>` 格式
（首行为主模块、无版本号，会被跳过）。同时内置约 100 个 Go 标准库路径
（`fmt`、`net/http`、`encoding/json`、`sync`、`context` 等），即便没有
`go.mod` 也能解析标准库导入。

## 8. 命令行接口

`polyc` 为每种语言暴露相应的标志，将值传给 indexer 的 `project_path` 参数
和辅助查找根目录：

| 标志                                          | 用途                                  |
|-----------------------------------------------|---------------------------------------|
| `--python-stubs <dir>`                        | Python typeshed / 存根目录            |
| `--classpath <jars>`、`-cp <jars>`            | Java classpath                        |
| `--reference <dll>`、`-r <dll>`               | .NET 程序集引用                       |
| `--crate-dir <dir>`、`--extern <name=path>`   | Rust crate 源 / `--extern`            |
| `--js-project <dir>`、`--node-modules <dir>`  | JS 项目根 / 额外的 `node_modules`     |
| `--ruby-project <dir>`、`--gem-path <dir>`    | Ruby 项目根 / 额外 GEM_PATH           |
| `--go-project <dir>`、`--go-mod-cache <dir>`  | Go 项目根 / 额外模块缓存              |

`--name=value` 与 `--name value` 两种形式都被接受。这些值存入
`polyc::Settings` 后，再透传到 `PloySemaOptions`，保证 indexer 在合适的
场景下能拿到非空的 `project_path`。

## 9. 未来扩展方向

| 功能 | 语法概念 | 说明 |
|------|---------|------|
| 版本约束 | `IMPORT python PACKAGE numpy >= 1.20;` | 指定包的最低版本要求 |
| 选择性导入 | `IMPORT python PACKAGE numpy USE (array, mean);` | 仅导入特定函数 |
| Lock 文件读取 | 解析 `package-lock.json` / `Gemfile.lock` / `go.sum` | 避免每次都启动外部命令 |
| 虚拟环境支持 | 配置选项 | 指定使用特定的 Python 虚拟环境 |
