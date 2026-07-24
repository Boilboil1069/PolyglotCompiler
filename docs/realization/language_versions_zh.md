# 多语言多版本编译与工具链管理

> 项目版本：**1.3.0** &nbsp;|&nbsp; 状态：**已交付**

PolyglotCompiler 把"源语言"（cpp / python / java / dotnet / rust / go /
javascript / ruby）和"语言版本"（如 `c++20`、`python 3.11`、`java 17`、
`rust 2021`）作为两个正交概念。本文档描述使所有组件就某个翻译单元应使用
的版本达成一致的基础设施。

> 版本选择器、CLI、`polyver`、poly `LANG` pin、外语签名提取和各前端门控
> 已经端到端串通。这里采用 fail-closed 契约：能够识别某个版本，绝不表示
> 可以静默接受或下沉尚未建模的语法。

## 可识别的版本选择器

| 语言        | 可识别的版本                                               | 默认值       | 枚举（`polyglot::frontends`）        |
|-------------|------------------------------------------------------------|--------------|--------------------------------------|
| cpp         | c++98 c++03 c++11 c++14 c++17 c++20 c++23 c++26            | c++23          | `CppDialect`                         |
| python      | 2.7；3.6 到 3.14 的每个 3.x 版本                            | 3.14           | `PythonVersion`                      |
| java        | 8 到 26 的每个 release                                      | 26             | `JavaRelease`                        |
| dotnet (C#) | 7.3 8 9 10 11 12 13 14 preview（target：net6 … net10）      | C# 14 / net10  | `DotnetLangVersion` / `…Framework`   |
| rust        | 2015 2018 2021 2024（edition）                              | 2024           | `RustEdition`                        |
| go          | 1.18 到 1.26 的每个 release                                 | 1.26           | `GoVersion`                          |
| javascript  | es5；es2015 到 es2026 的每个年度版本；esnext                 | es2026         | `EcmaVersion`                        |
| ruby        | 1.9 2.7 3.0 3.1 3.2 3.3 3.4 4.0                            | 4.0            | `RubyVersion`                        |

每个枚举都保留 `kAuto` 成员；在前端内部它会确定性地映射到上表默认值。
项目和工具链探测属于调用方职责：`polyver` 或 UI 必须先把探测结果解析成
显式枚举，再调用前端。

这些条目是版本选择器，不等于所有构造都能下沉。分析阶段必须建立忠实 AST
或明确报错；下沉阶段遇到尚未实现的节点必须报告 `kUnsupportedLowering`，
不得静默丢弃。

## 已审计的现代语言边界

本轮把默认版本推进到各语言当前稳定基线，并为下列现代语法建立了可回归的
版本门控和 AST 契约：

| 语言 | 已保真分析并精确门控的代表性现代边界 |
|------|------------------------------------------|
| C++ | C++11 `nullptr` / lambda / range-for；C++17 结构化绑定 / fold / `if` 初始化语句；C++20 module / concept / `requires` / `<=>` / 指定初始化 / coroutine；C++23 显式对象参数与 `if consteval`。C++26 可作为选择器，但不会把尚未标准化或尚未建模的构造伪装成已支持。 |
| Python | Python 3.8 仅位置参数与赋值表达式、结构化模式匹配、`except*`、PEP 695 类型参数、Python 3.13 类型参数默认值；Python 3.14 template string（独立 AST）与 PEP 758 无括号异常列表。 |
| Rust | 2015/2018/2021/2024 edition 关键字语境；async/await；2024 的 `gen` 保留、guarded-string 保留语法及 unsafe attribute 规则。 |
| Java | Java 9 模块描述符与 private interface method；record、sealed type、switch expression；Java 25 module import、compact source file 与 flexible constructor body。Java 26 默认选择稳定语法，preview-only 构造不会被当作稳定语法静默启用。 |
| C# | C# 8 switch expression、C# 9 `init`，以及 raw string、primary constructor、collection/parameter evolution；C# 14 extension block、`field` backing field、复合赋值运算符、`#:` file directive，以及 null-conditional assignment / unbound `nameof` / partial constructor 与 event。 |
| Go | 泛型与 type set；Go 1.22 integer range、1.23 range-over-function、1.24 generic alias、1.26 `new(expression)` 与 self-referential generic constraint。 |
| JavaScript | ES2015 declaration / class / module / arrow-function 边界、class private field / static block、RegExp `v` flag、ES2025 import attributes；explicit resource management 的 `using` / `await using` 只在 `esnext` 接受。decorator 仍明确报 `kUnsupportedSyntax`。 |
| Ruby | safe navigation、pattern / rightward assignment、argument forwarding 与 block AST；Ruby 3.1 hash value omission、Ruby 3.4 implicit `it`；Ruby 4.0 行首逻辑运算符续行。 |

这里的“完整现代支持”指完整、可检查的**前端边界**：要么产生保真 AST 并
完成分析，要么在最早无法保真的阶段失败。它不表示上述每个节点已经具备原生
IR、运行时 ABI 和 backend 实现；这些节点若进入尚不具备语义保持能力的下沉
路径，会统一报告 `kUnsupportedLowering`。

## 当前实际实现的解析与冲突规则

1. 显式 `polyc` / API 版本参数确定调用方要求；
2. 导入源码上的模块级 poly `LANG` pin 在调用方为 `auto` 时补全版本；
3. 两者都显式时必须相等，否则报告 `kLangVersionMismatch`；
4. 两者都没有时使用确定性的 `kXxxVersionDefault`。

作用域 `WITH LANG` / `@LANG` pin 继续附着在单个 bridge call 描述符上。
未知 CLI selector 属于命令行用法错误，`polyc` 输出错误并以状态码 2 退出；
非法或冲突的 poly pin 报告 `kLangVersionMismatch`。两种情况都不会回落到
另一个 dialect。

`.NET` 还有一层真实的 TFM 默认推导：`net6` 到 `net10` 分别选择 C# 10 到
C# 14。显式 `--cs-lang` 高于所选 TFM 的受支持默认值，或在稳定 TFM 上显式
选择 `preview`，会报告 `kLangVersionMismatch`；只给 `--cs-lang=preview` 而让
TFM 保持 `auto` 时仍可进行 preview 边界分析。当前 TFM 只参与语言版本兼容
检查，不提供对应 .NET reference pack 的 API surface 校验。

外语签名索引目前只投影固定参数签名；default/rest/variadic 元数据与完整重载
解析尚未进入跨语言 ABI 模型。因此重载或短名 alias 碰撞会报告
`kSignatureMismatch` 并原子拒绝整个模块，而不是覆盖旧签名。外语源码缺失或
不可读会报告 `kSignatureMissing`。C++ 签名扫描把 `<...>` 系统头视作外部契约，
但本地 `"..."` 头必须能够解析；宏和条件编译仍按选定 dialect 预处理。

## `polyc` 命令行接口

`polyc` 为每种语言提供一个可选参数，外加一个查询子命令。所有参数均接受
`auto`（默认值），此时选择上表确定性的现代默认版本；别名遵循上游通行写法。

| 参数                          | 别名        | 取值示例                                    |
|-------------------------------|-------------|---------------------------------------------|
| `--std=<dialect>`             | `-std=`     | `c++20`、`c++23`、`20`                      |
| `--python-version=<v>`        | `--py=`     | `3.12`、`3.14`                              |
| `--java-release=<n>`          | `--java=`   | `21`、`26`                                  |
| `--cs-lang=<v>`               | `--csharp=` | `12`、`14`                                  |
| `--target-framework=<tfm>`    | `--tfm=`    | `net8`、`net10`                             |
| `--rust-edition=<y>`          | `--edition=`| `2021`、`2024`                              |
| `--go-version=<v>`            | `--go=`     | `1.22`、`1.26`                              |
| `--ecma=<v>`                  | `--es=`     | `es2024`、`es2026`、`esnext`                |
| `--ruby-version=<v>`          | `--ruby=`   | `3.4`、`4.0`                                |
| `--list-language-versions`    | —           | 打印上述版本矩阵后退出                       |

被选定的版本由 `tools/polyc/src/stage_frontend.cpp` 转发到
`polyglot::frontends::FrontendOptions`，各前端可据此调整行为。

`kUnsupportedSyntax`、`kUnsupportedLowering` 与 `kLangVersionMismatch` 是
不可绕过的边界错误；`--force` 不会把缺失 AST、部分 IR 或方言冲突继续送入
backend / packaging。

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
| python      | `python3.14` … `python3.7`、`python3`、`python`                    |
| java        | `java -version`（按 `(?:openjdk|java) version "?(\d+)` 解析）      |
| dotnet      | `dotnet --list-runtimes`（每个 `Microsoft.NETCore.App` major 一项）|
| rust        | `rustc --version`（`rustc>=1.85` 记录 edition `2024`，否则 `2021`；完整版本存入 `vendor`）|
| go          | `go version`（`go1.X` → `1.X`）                                    |
| javascript  | `node --version`；按发行线保守映射，最高到 `es2026`                |
| ruby        | `ruby --version`                                                   |

## 诊断码

| 编号   | 符号                            | 等级 | 含义                                                                  |
|--------|---------------------------------|------|-----------------------------------------------------------------------|
| `2006` | `kUnsupportedSyntax`            | 错误 | 所选前端无法忠实表示已经识别到的源语言构造。                          |
| `4003` | `kUnsupportedLowering`          | 错误 | 分析成功，但该 AST 节点还没有保持语义的下沉实现。                     |
| `6001` | `kLangVersionMismatch`          | 错误 | 源码语法、pin 或工具链与选定语言版本冲突。                           |
| `6002` | `kLangVersionFallback`          | 警告 | 没有来源给出版本，回落到确定性的稳定版本默认值。                     |
| `6003` | `kToolchainNotFound`            | 错误 | 所请求语言完全没有可用工具链。                                       |

## 实现状态

* **Phase 2 &mdash; poly 语法（已完成）**：模块级 `LANG <name> = "<ver>";`、
  作用域块 `WITH LANG (name=ver, …) { … }`，以及单语句注解
  `@LANG (name=ver) <stmt>` 已在 poly 词法 / 语法 / 语义阶段全链路落地。
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
* **Phase 2 &mdash; 分析 / 下沉边界（已加固）**：各前端按
  `FrontendOptions` 暴露的版本字段门控已经实现的现代语法族。例如 C++
  concepts / modules / spaceship、Python 赋值表达式和 PEP 695 声明、Java
  record / sealed、C# file / global / raw string、Rust async / await 的 edition
  边界、Go 泛型与 range 演进、ECMAScript 可选链，以及 Ruby 的现代参数和
  pattern 形式。源码语法晚于所选 release，以及非法或未知 selector / pin，
  都报告 `ErrorCode::kLangVersionMismatch`。运行时按描述文件里的
  `VERSION` 行选择 ABI bridge 变体；linker（`tools/polyld`）和 linker
  库（`tools/polyld_lib`）均已串通版本字段。已经完成分析、但尚无安全 IR
  表达的节点会报告 `kUnsupportedLowering`，不再被静默丢弃。
* **Phase 2 &mdash; LINK 固定（已完成）**：`LinkEntry::lang_version` 现按
  *目标*（外语）侧解析，而非源（宿主）侧；将 `LINK` 包入 `WITH LANG` /
  `@LANG` 即可让 bridge stub 把版本编入名字，下沉阶段为每个被固定的
  LINK 生成独立描述符。下沉阶段还会递归进入 `WithLangBlock::body` 与
  `LangAnnotation::target`，让被包裹的 LINK / CALL 全部到达描述符流水线。
* **Phase 3（已完成）** &mdash; `polyui` 的 Tool-chains 页面调用
  `polyver list/detect`、poly `LANG` 的语法高亮，以及
  `tests/integration/language_versions/` 下九个语言的集成测试目录（含
  逐调用点双 pin 共存路径）。

项目 VERSION 已升至 `1.3.0`，需求账本条目 `2026-04-27-3` 已追加
`--end -done` 完成标记。
