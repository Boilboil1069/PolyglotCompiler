# 外部包消费

本文档描述 `polyc` 在所有受支持的语言前端中如何发现并加载真实的
第三方包符号。涵盖搜索顺序、CLI 选项、磁盘布局假设，以及在链接
阶段把降级后的 IR 转换为真正外部函数调用的运行时 ABI 桥。

> 状态：本文档跟踪需求 `2026-04-26-03` 的滚动实现进度。
> 阶段 ①–⑥ 已串入各前端与索引器；阶段 ⑦–⑨（链接器 dlopen、
> 运行时 ABI 桥、密闭 E2E 套件）单独跟踪。

## 1. CLI 入口

`tools/polyc/src/driver.cpp` 暴露了一组与具体语言无关的外部包
选项，在 `--help` 中归类为 "External-package options"：

| 选项 | 作用 |
| --- | --- |
| `-I<path>` / `--I=<path>` | C/C++ 用户头文件搜索根 |
| `-isystem <path>` | C/C++ 系统头文件搜索根 |
| `-D<name>[=<value>]` | 定义 C/C++ 预处理宏 |
| `-U<name>` | 取消定义 C/C++ 预处理宏 |
| `--python-stubs=<dir>` | 添加 `.pyi` 存根目录 |
| `--classpath=<paths>` / `-cp` | Java classpath（Windows `;`，其它 `:`） |
| `--reference=<dll>` / `-r` | .NET 程序集引用（`.dll` / `.exe`） |
| `--crate-dir=<dir>` | `cargo metadata` 使用的 Rust cargo 项目根 |
| `--extern <name>=<path>` | Rust extern crate 映射 |
| `--js-project=<dir>` | JavaScript/TypeScript 项目根 |
| `--node-modules=<dir>` | 额外的 node_modules 根 |
| `--ruby-project=<dir>` | Ruby Bundler 项目根 |
| `--gem-path=<dir>` | 额外 gem 搜索路径 |
| `--go-project=<dir>` | 含 `go.mod` 的 Go 模块根 |
| `--go-mod-cache=<dir>` | 额外 Go 模块缓存根 |

每个选项都通过 `DriverSettings` → `FrontendOptions`
（见 `frontends/common/include/language_frontend.h`）流入对应前端
的 `Lower()` 入口，并由 ploy 的 `PackageIndexer` 共同消费。

## 2. C++ — 预处理器接入

`frontends/common/src/preprocessor.cpp` 提供了独立的 C 预处理器，
支持 `#include`、`#define`、函数式宏、include 守卫、
`#if/#ifdef/#elif/#else/#endif` 以及条件表达式求值与
`#pragma once`。驱动会在每个 C/C++ 编译单元前调用它
（见 `stage_frontend.cpp` 与 `compilation_pipeline.cpp`），
随后 parser 看到的就是已展开的文本。`-I` / `-isystem` 给出的
路径同时用于双引号与尖括号 `#include`，尖括号优先 system 路径。
词法器残留的 `kPreprocessor` token 流仅剩 `#line` / `#pragma`
等标记，由 parser 在 `ParseStatement` 顶部统一过滤。

## 3. Python — `.pyi` 存根加载器

`frontends/python/src/pyi_loader.cpp` 实现了对 typeshed 兼容
`.pyi` 子集的缩进感知解析器。搜索顺序为：

1. CLI 顺序中的每个 `--python-stubs=<dir>` 目录
2. 环境变量 `PYTHON_STUBS`（PATH 风格，平台分隔符）
3. `PackageIndexer` 发现的 site-packages 根
4. 编译期内置模块表（`BuiltinModuleExports`）

对 `import foo.bar` 请求，加载器在每个搜索根下探测：

```
<root>/foo/bar.pyi
<root>/foo/bar/__init__.pyi
<root>/foo-stubs/bar.pyi
```

存根体形成 `name -> core::Type` 的导出表，由 `python_sema.cpp`
中的 `PythonSemaOptions` 消费。硬编码的 `BuiltinModuleExports()`
仅作为密闭测试下的确定性回退。

## 4. Java — `.class` / `.jar` 读取器

`frontends/java/src/class_file_reader.cpp` 直接解析 JVM 类文件
格式：常量池、`methods_count` / `fields_count` 表、`Signature`、
`Code`、`Exceptions`、`InnerClasses`，以及 `module-info.class`
的模块可见性。`--classpath` / `-cp` 同时接受目录（扫描 `.class`
树）与 `.jar` 归档（通过项目内置的 mini-zip 读取器读取）。
加载到的类型/方法签名注入 Java sema 的 `SymbolTable`，使
`import java.util.List` 解析到真实的 `List<E>` 形状。

## 5. .NET — ECMA-335 元数据读取器

`frontends/dotnet/src/metadata_reader.cpp` 按 ECMA-335 步进
PE/CLI 格式：`#Strings`、`#US`、`#GUID`、`#Blob` 堆，以及
`TypeDef`、`MethodDef`、`MemberRef`、`AssemblyRef` 表。
`--reference=<dll>` / `-r` 可指定多次；同时也会在
`dotnet --list-runtimes` 给出的目录与 NuGet 全局缓存
（Windows `%USERPROFILE%/.nuget/packages`，其它 `~/.nuget/packages`）
中查找。

## 6. Rust — crate 加载器与 cargo metadata

两套互补机制：

**源码级 crate 加载器**（`frontends/rust/src/crate_loader.cpp`）
复用项目自身的 Rust 词法/语法分析器，遍历外部 crate（或工作区
成员）的 `src/lib.rs` / `src/main.rs`，索引所有 `pub` 项 —
函数、结构体、枚举、trait、类型别名、常量、宏、impl 方法、
以及内联或外置子模块。二进制产物（`.rlib` / `.rmeta`）通过
头签名识别，仅有产物时作为不透明 crate 条目暴露。

**索引器级 cargo 集成**（`frontends/ploy/src/sema/package_indexer.cpp`）
对 `--crate-dir` 给出的 crate 根运行
`cargo metadata --format-version 1 --no-deps`。一个小型专用
JSON 行走器（避免 ploy 前端引入重型依赖）从结果中提取每个包
的 `name`、`version`、`manifest_path`，写入
`PackageInfo.install_path`。若未提供 crate 根，则查询
`cargo install --list` 获取全局安装的二进制 crate。
单元测试通过 `MockCommandRunner` 注入 pip 风格的
`name==version` 输出，该路径被保留为 `ParseFreezeOutput` 短路。

`--extern <name>=<path>` 原样转发给 Rust crate 加载器，使
`use external_crate::Item` 直接解析到磁盘上的产物。

## 7. Ploy 包索引

`PackageIndexer::IndexLanguage` 接收每语言的 `VenvConfig`；
对 Rust 而言 `venv_path` 字段被解释为 cargo 项目根
（即 `--crate-dir` 的值）。`stage_frontend.cpp` 与
`compilation_pipeline.cpp` 都从驱动的
`DriverSettings::rust_crate_dir` 填充该通道，使索引器始终
看到与用户传入的相同值。

得到的 `PackageInfo` 映射通过
`PloySemaOptions::discovery_cache` 流向
`ploy/src/lowering/lowering.cpp` 的 lowering 阶段，安装路径
作为链接期提示与 `IMPORT` 语句一起写出。

## 8. 诊断

每个加载器都按软失败设计：缺失存根、缺失 classpath 条目或
不可达的 cargo 二进制都会降级为非致命诊断并继续尝试下一个
后端。`--strict` 把这些警告升级为错误，使生产构建拒绝占位
符号解析。

## 9. 待办

需求中链接期的另一半 — `polyld` 通过
`dlopen`/`LoadLibrary` + `dlsym` 解析每个降级后的外部符号、
以及 `runtime/src/libs/*_rt.c` 中各语言的运行时桥
（CPython C-API、JNI、hostfxr、Rust cdylib、C/C++ 系统 ABI）—
已随需求 `2026-04-26-04` 落地。需求 `2026-04-26-03` 的密闭
E2E 套件位于
`tests/integration/external_packages/demand_03_test.cpp`，配套
夹具放在 `tests/fixtures/external_packages/{cpp,python,rust,...}`
下；测试不依赖网络，也不依赖宿主机已装的第三方运行时。

## 10. Go / JavaScript / Ruby 外部包（2026-04-27-1）

需求 `2026-04-26-01` 引入的 Go、JavaScript、Ruby 三个前端最初仅完成
词法/语法骨架。本节记录需求 `2026-04-27-1` 给它们补齐的"解析 + 降级 +
ABI 桥接"完整链路。

### 10.1 Go

* 解析器：`frontends/go/include/go_import_resolver.h`。
* 查找顺序：
  1. `<--go-project>/<import-path>`（项目内子包）；
  2. `<--go-project>/vendor/<import-path>`（vendor 模式）；
  3. 项目 `go.mod` 中的 `replace` 指令；
  4. `--go-mod-cache=<dir>` 与自动探测的 `GOPATH/pkg/mod`，遵循
     大写路径段的 `!lower` 转义规则；
  5. `GOROOT/src/<import-path>`（自动从 `GOROOT` 环境或
     `go env GOROOT` 输出探测）。
* 导出抽取复用 `GoLexer` + `GoParser` 扫描所有 `.go` 文件
  （排除 `_test.go`），把首字母大写的 `FuncDecl` / `TypeSpec`
  作为跨包符号注入 SemaContext。
* ABI 桥接：`runtime/src/libs/go_rt.c` 提供
  `__ploy_go_load_pkg` / `__ploy_go_call`。
  缺失宿主运行时仅打印一次诊断并退化为返回 NULL。

### 10.2 JavaScript / TypeScript

* 解析器：`frontends/javascript/include/javascript_import_resolver.h`。
* 解析算法对齐 Node.js：
  1. 相对说明符（`./x`、`../y`）按 `.d.ts → .ts → .mjs → .cjs →
     .js → .json` 顺序探测扩展名，并回落到 `<dir>/index.<ext>`；
  2. 裸说明符沿导入文件目录、项目根、`--node-modules` 给定根
     的 `node_modules` 祖先链向上回溯；
  3. `package.json` 优先读取 `types` / `typings`，其次
     `module`、再次 `main`，最后 `index.*`。
* 导出抽取复用 `JsLexer` + `JsParser`（解析器同时接受
  `.d.ts`，因为 TS 声明在我们消费的范围内是 JS 的语法超集）。
* ABI 桥接：`runtime/src/libs/javascript_rt.c` 提供
  `__ploy_js_require` / `__ploy_js_call`。

### 10.3 Ruby

* 解析器：`frontends/ruby/include/ruby_import_resolver.h`。
* `require` / `load` 查找顺序：
  1. `RUBYLIB` 环境变量中的路径；
  2. `--gem-path=<dir>` 给定的根，既作为 `$LOAD_PATH` 直接条目，
     也作为 Bundler / RubyGems vendor 目录
     （`<root>/<gem>/lib/<feature>.rb`）扫描；
  3. 项目根本身以及 `<project>/lib/`；
  4. 宿主 Ruby 的 `$LOAD_PATH`，通过
     `ruby -e "puts $LOAD_PATH"` 一次性探测（找不到则静默忽略）。
* `require_relative` 相对当前文件目录解析，带 `.rb` 扩展探测。
* `autoload` 在注册时即解析，以便语义分析阶段就能看到对应常量。
* ABI 桥接：`runtime/src/libs/ruby_rt.c` 提供
  `__ploy_ruby_require` / `__ploy_ruby_call`。

### 10.4 CLI 选项

驱动器选项（`--go-project`、`--go-mod-cache`、`--js-project`、
`--node-modules`、`--ruby-project`、`--gem-path`）在需求
`2026-04-26-03` 中已声明。需求 `2026-04-27-1` 将它们贯穿
`FrontendOptions`，并在每个前端的 `Analyze()` / `Lower()`
入口真正消费。

### 10.5 测试

`tests/fixtures/external_packages/{go,javascript,ruby}/` 提供
封闭夹具，由 `tests/integration/external_packages/external_packages_test.cpp`
驱动。每种语言至少包含一个正样本和一个缺失包样本，后者
断言输出干净诊断而非崩溃。
