# 多语言多版本编译与工具链管理

> 项目版本：**1.3.0** &nbsp;|&nbsp; 状态：**已交付**

PolyglotCompiler 把"源语言"（cpp / python / java / dotnet / rust / go /
javascript / ruby）和"语言版本"（如 `c++20`、`python 3.11`、`java 17`、
`rust 2021`）作为两个正交概念。本文档描述使所有组件就某个翻译单元应使用
的版本达成一致的基础设施。

> 本里程碑（Phase 1）交付：类型体系、CLI 入口、`polyver` 工具链管理器、
> 三个新诊断码以及通过 `polyc` 的转发链路。各前端的版本门控、ploy `LANG`
> 语法、运行时 ABI 选择、UI 工具链页和集成测试集分别在 Phase 2 与 Phase 3
> 中跟踪。

## 支持的版本矩阵

| 语言        | 可识别的版本                                               | 默认值       | 枚举（`polyglot::frontends`）        |
|-------------|------------------------------------------------------------|--------------|--------------------------------------|
| cpp         | c++98 c++03 c++11 c++14 c++17 c++20 c++23 c++26            | c++20        | `CppDialect`                         |
| python      | 2.7 3.6 3.8 3.10 3.11 3.12 3.13                            | 3.11         | `PythonVersion`                      |
| java        | 8 11 17 21 23                                              | 17           | `JavaRelease`                        |
| dotnet (C#) | 7.3 8 9 10 11 12（target：net6 net7 net8 net9）            | C# 11 / net8 | `DotnetLangVersion` / `…Framework`   |
| rust        | 2015 2018 2021 2024（edition）                             | 2021         | `RustEdition`                        |
| go          | 1.18 1.20 1.21 1.22 1.23                                   | 1.21         | `GoVersion`                          |
| javascript  | es5 es2015 es2017 es2020 es2022 es2023 esnext              | es2022       | `EcmaVersion`                        |
| ruby        | 1.9 2.7 3.0 3.2 3.3                                        | 3.2          | `RubyVersion`                        |

每个枚举都保留 `kAuto` 成员；输入 `auto`（或不写该参数）即让前端按下文的
推断顺序自行选择版本。

## 推断顺序

对一个翻译单元，最终生效的版本按下列顺序确定：

1. **显式调用注解**（Phase 2 — ploy 的 `@LANG(version)` / `WITH LANG`）；
2. **文件级 pragma**（前端各自约定，例如 C++ `#pragma poly std=c++23`）；
3. **项目固定** &mdash; `<项目根>/.polyglot/toolchains.lock` 中的条目
   （由 `polyver use` 写入）；
4. **`polyc` 命令行参数**（见下表）；
5. **用户目录默认** &mdash; `~/.polyglot/toolchains.json` 中该语言的
   `"default": true` 条目；
6. **工具链探测** &mdash; `polyver detect` 在该语言下发现的最高版本；
7. **保守回落** &mdash; `frontends/common/include/language_versions.h` 中
   定义的 `kXxxVersionDefault`。

如果 1–6 之间的来源相互冲突，编译器报错 `E_LANG_VERSION_MISMATCH`
（`6001`）；若必须从第 6 步回落到第 7 步，则发出警告
`W_LANG_VERSION_FALLBACK`（`6002`）；若所请求语言根本找不到工具链，则
报硬错误 `E_TOOLCHAIN_NOT_FOUND`（`6003`）。

## `polyc` 命令行接口

`polyc` 为每种语言提供一个可选参数，外加一个查询子命令。所有参数均接受
`auto`（默认值）以保留推断逻辑；别名遵循上游通行写法。

| 参数                          | 别名        | 取值示例                                    |
|-------------------------------|-------------|---------------------------------------------|
| `--std=<dialect>`             | `-std=`     | `c++20`、`c++23`、`20`                      |
| `--python-version=<v>`        | `--py=`     | `3.11`、`3.13`                              |
| `--java-release=<n>`          | `--java=`   | `17`、`21`                                  |
| `--cs-lang=<v>`               | `--csharp=` | `11`、`12`                                  |
| `--target-framework=<tfm>`    | `--tfm=`    | `net8`、`net9`                              |
| `--rust-edition=<y>`          | `--edition=`| `2021`、`2024`                              |
| `--go-version=<v>`            | `--go=`     | `1.21`、`1.22`                              |
| `--ecma=<v>`                  | `--es=`     | `es2022`、`esnext`                          |
| `--ruby-version=<v>`          | `--ruby=`   | `3.2`                                       |
| `--list-language-versions`    | —           | 打印上述版本矩阵后退出                       |

被选定的版本由 `tools/polyc/src/stage_frontend.cpp` 转发到
`polyglot::frontends::FrontendOptions`，各前端可据此调整行为。

## `polyver` &mdash; 工具链管理器

`polyver` 是一个独立的可执行程序（位于 `tools/polyver/`），用于发现宿主上
的工具链、把它们持久化到 JSON 目录中，并允许某个项目固定默认版本。

```text
polyver list [<lang>]                列出已发现的工具链
polyver detect                       探测宿主并刷新 ~/.polyglot/toolchains.json
polyver use   <lang> <version>       为当前项目固定默认值（写入 .polyglot/toolchains.lock）
polyver path  <lang> <version>       打印工具链可执行文件的绝对路径
polyver --version                    打印 polyver 版本
polyver --help                       打印帮助信息
```

### 目录文件位置

* **用户目录** &mdash; `~/.polyglot/toolchains.json`，由 `polyver detect`
  写入；重新探测时，已被标记为 `default: true` 的条目仍会保留。
* **项目锁** &mdash; `<项目根>/.polyglot/toolchains.lock`，由 `polyver use`
  写入；查找工具链时优先于用户目录。"项目根"是从当前目录向上查找最近的
  含 `.polyglot/` 子目录的祖先；若一个都没有，`polyver use` 会在当前目录
  自动建立 `.polyglot/`。

### JSON Schema（`polyglot.toolchains.v1`）

```json
{
  "schema": "polyglot.toolchains.v1",
  "generated_by": "polyver",
  "toolchains": [
    { "language": "cpp",    "version": "c++20", "path": "/usr/bin/g++",      "vendor": "gcc 11.4.0", "default": true },
    { "language": "python", "version": "3.11",  "path": "/usr/bin/python3",  "vendor": "CPython 3.11.6" },
    { "language": "rust",   "version": "2021",  "path": "~/.cargo/bin/rustc","vendor": "rustc 1.78.0" }
  ]
}
```

项目锁文件采用同一 schema。

### 探测策略

`polyver detect` 在宿主 `PATH` 上探测下列可执行文件：

| 语言        | 探测到的可执行文件 / 版本判定                                      |
|-------------|--------------------------------------------------------------------|
| cpp         | `clang++`、`g++`、`cl`（MSVC）。`gcc>=10`/`msvc>=19` → `c++20`，`gcc>=13` → `c++23` |
| python      | `python3.13` … `python3.6`、`python3`、`python`、`python2`         |
| java        | `java -version`（按 `(?:openjdk|java) version "?(\d+)` 解析）      |
| dotnet      | `dotnet --list-runtimes`（每个 `Microsoft.NETCore.App` major 一项）|
| rust        | `rustc --version`（统一记录 edition `2021`，完整版本存入 `vendor`）|
| go          | `go version`（`go1.X` → `1.X`）                                    |
| javascript  | `node --version`；按 major 映射 `es2020/es2022/es2023`             |
| ruby        | `ruby --version`                                                   |

## 诊断码

| 编号   | 符号                            | 等级 | 含义                                                                  |
|--------|---------------------------------|------|-----------------------------------------------------------------------|
| `6001` | `kLangVersionMismatch`          | 错误 | pragma / 项目固定 / 命令行要求版本 *X*，而当前工具链只提供 *Y*。     |
| `6002` | `kLangVersionFallback`          | 警告 | 没有来源给出版本，回落到了保守默认值。                               |
| `6003` | `kToolchainNotFound`            | 错误 | 所请求语言完全没有可用工具链。                                       |

## 后续路线（仍是 WIP）

* **Phase 2 &mdash; ploy 语法（已完成）**：模块级 `LANG <name> = "<ver>";`、
  作用域块 `WITH LANG (name=ver, …) { … }`，以及单语句注解
  `@LANG (name=ver) <stmt>` 已在 ploy 词法 / 语法 / 语义阶段全链路落地。
  语义阶段维护一个 pin 栈：模块层 pragma 写入栈底，`WITH LANG` /
  `@LANG` push / pop 内层 frame，`AnalyzeCrossLangCall` /
  `AnalyzeNewExpression` / `AnalyzeMethodCallExpression` /
  `AnalyzeGetAttrExpression` / `AnalyzeSetAttrExpression` /
  `AnalyzeDeleteExpression` / `AnalyzeWithStatement` /
  `AnalyzeExtendDecl` / `AnalyzeLinkDecl` 在拍板时调用
  `ResolveLangVersion(language)` 写入相应 AST 节点的 `lang_version_pin`
  字段。下沉阶段把它复制进 `CrossLangCallDescriptor::lang_version`，
  bridge 阶段把它写成 `.paux` 描述文件里的 `VERSION <lang> <ver>` 行（紧
  跟在对应 `CALL` 行之后），polyld 在 `LoadDescriptorFile` 中向回查找
  最近的同语言 CALL 并附加版本。覆盖测试位于
  `tests/unit/frontends/ploy/lang_version_pin_test.cpp`。
* **Phase 2 &mdash; 运行时 / 后端门控（已完成）**：各前端按
  `FrontendOptions` 暴露的版本字段开启 / 关闭语法。C++ 在 C++20+ 才允许
  concepts / consteval；Python 在 3.8+ 才允许 walrus；Java 在 17+ 才允许
  `record`；.NET 在 C# 11+ 才允许 `file class`；Rust 在 edition 2021+ 才
  允许 `let ... else`；JavaScript 在 ES2020+ 才允许可选链 `?.`。所有违反
  都以 `ErrorCode::kLangVersionMismatch` 形式上报。运行时按描述文件里的
  `VERSION` 行选择 ABI bridge 变体；linker（`tools/polyld`）和 linker
  库（`tools/polyld_lib`）均已串通版本字段。
* **Phase 2 &mdash; LINK 固定（已完成）**：`LinkEntry::lang_version` 现按
  *目标*（外语）侧解析，而非源（宿主）侧；将 `LINK` 包入 `WITH LANG` /
  `@LANG` 即可让 bridge stub 把版本编入名字，下沉阶段为每个被固定的
  LINK 生成独立描述符。下沉阶段还会递归进入 `WithLangBlock::body` 与
  `LangAnnotation::target`，让被包裹的 LINK / CALL 全部到达描述符流水线。
* **Phase 3（已完成）** &mdash; `polyui` 的 Tool-chains 页面调用
  `polyver list/detect`、ploy `LANG` 的语法高亮，以及
  `tests/integration/language_versions/` 下九个语言的集成测试目录（含
  逐调用点双 pin 共存路径）。

项目 VERSION 已升至 `1.3.0`，需求账本条目 `2026-04-27-3` 已追加
`--end -done` 完成标记。
