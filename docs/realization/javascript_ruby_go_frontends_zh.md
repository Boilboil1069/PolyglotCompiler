# JavaScript / Ruby / Go 前端

> 需求：`2026-04-26-01` —— *"前端增加更多语言：js，Ruby，Go三种语言。"*

本文档描述这三个前端在 PolyglotCompiler 现有流水线中的实现方式。

## 1. 总览

三个前端的目录结构与已有的 Java / Rust / .NET 前端完全一致：

| 阶段        | 文件                                              |
| ----------- | ------------------------------------------------- |
| AST 节点    | `frontends/<lang>/include/<lang>_ast.h`           |
| 词法分析    | `<lang>_lexer.h` + `src/lexer/lexer.cpp`          |
| 语法分析    | `<lang>_parser.h` + `src/parser/parser.cpp`       |
| 语义分析    | `<lang>_sema.h`   + `src/sema/sema.cpp`           |
| IR 下降     | `<lang>_lowering.h` + `src/lowering/lowering.cpp` |
| 前端封装    | `<lang>_frontend.h` + `src/<lang>_frontend.cpp`   |

均通过 `REGISTER_FRONTEND(...)` 在静态初始化阶段自动注册，`polyc`、`polyui`
以及测试可执行文件无需手工挂接。

## 2. JavaScript (ECMAScript)

* **库目标：** `frontend_javascript`
* **显示名：** `JavaScript`
* **别名：** `javascript`、`js`、`node`、`es`、`ecmascript`
* **扩展名：** `.js`、`.mjs`、`.cjs`

### 词法
支持模板字符串与 `${...}` 插值、正则字面量（基于上下文区分 `/` 与正则）、
十进制/十六进制 `0x`/八进制 `0o`/二进制 `0b` 数字与 `BigInt` 后缀 `n`。换行
作为 token 提供给解析器，便于实现 ASI（自动分号插入）。

### 语法
覆盖函数声明、箭头函数、类（`extends`、`static`、getter/setter、私有 `#`
字段）、解构赋值、扩展/收集运算符、默认参数、`import` / `export`、以及现代
语句（`for-of`、`for-in`、`try/catch/finally`、`switch`、带标签的
`break`）。

### 语义
区分 `var`、`let`、`const` 作用域，校验 `break`/`continue` 的位置，并对暂时
性死区中的提前使用给出诊断。

### IR 下降
每个函数/方法映射为 IR 的 `Function`。闭包通过将自由变量装箱进合成的
环境记录来实现，使现有 SSA 基础设施可以像处理普通分配一样分析它们。

## 3. Ruby

* **库目标：** `frontend_ruby`
* **显示名：** `Ruby`
* **别名：** `ruby`、`rb`
* **扩展名：** `.rb`、`.rbw`

### 词法
识别标准 Ruby 关键字（`def`、`class`、`module`、`do`、`end` …）、方法名
后缀 `?`/`!`、符号字面量（`:foo`），以及单引号/双引号字符串。Heredoc 与
`%w()` 数组目前先归约为普通字符串以便 parser 能继续推进。

### 语法
顶层是一个 `Module`，包含类、模块、方法、顶层语句及常量赋值。表达式遵循
Ruby 经典优先级，无括号方法调用（`puts "hi"`）通过单 token 前瞻接受。

### 语义
处理作用域与 arity 检查，确保 `break`/`next`/`redo` 仅出现在迭代器/循环
中，并对同一作用域中的重复顶层常量与方法定义报错。

### IR 下降
每个方法成为一个 IR `Function`；实例方法以 `Receiver.method` 形式发射，方
便拓扑工具按所属类聚合显示。块 (`do |x| … end`) 被下降为匿名 IR 函数加上
一个闭包记录。

## 4. Go

* **库目标：** `frontend_go`
* **显示名：** `Go`
* **别名：** `go`、`golang`
* **扩展名：** `.go`

### 词法
完整实现 Go 规范的 *自动分号插入* 规则：当上一 token 可结束语句（标识符、
字面量、`)`、`]`、`}`、`++`、`--`、`return`、`break`、`continue`、
`fallthrough`）且下一字符为换行时，词法器自动产生隐式 `;`。运算符按 3 字符
（`<<=`、`>>=`、`&^=`、`...`）→ 2 字符（`<-`、`:=`、`==`、`!=` …）→ 1 字符
顺序匹配。支持反引号原始字符串与 rune 字面量。

### 语法
完整覆盖：

* 包声明与 import（单条与分组）
* 顶层 `var` / `const` / `type` 块
* 方法接收者 `func (r T) M(...)`
* 多返回值签名与变长 `...T` 参数
* 复合字面量 `T{...}`、类型断言 `x.(T)`、切片 `a[lo:hi:max]`、通道
  `<-ch` / `ch <- v`、`go` / `defer`
* 语句：带初始化的 `if`、三段式/cond-only/`range` 形式 `for`、`switch`
  （表达式与类型）、`select`

### 语义
校验 `break` / `continue` / `fallthrough` 位置，检测顶层重名，并验证 import
与方法接收者格式。

### IR 下降
`ToIRType` 将 Go 基础类型集合映射到 IR 类型（`int8/16/32/64`、`uint*`、
`float32/64`、`bool`、`string`、`byte`、`rune`、`error`），复合类型分别走
`core::Type::Slice`、`Array`、手动构造的 `kPointer` 与 `kStruct` 记录。方法
以 `Recv.Method` 形式发射，并对非 `main` 包以包名作前缀，与 Java 包的现有
约定一致。

## 5. 构建与测试集成

* `frontends/CMakeLists.txt` 声明三个新库。
* `tools/CMakeLists.txt` 让 `polyc_lib`、`polybench`、`polyui` 链接它们，
  并加入 macOS bundle 的 `_POLYUI_DYLIB_TARGETS` 列表。
* `tests/CMakeLists.txt` 提供独立测试目标 `test_frontend_javascript`、
  `test_frontend_ruby`、`test_frontend_go`，并注册到 CTest。
* 聚合目标 `unit_tests` 同步纳入新源文件。

## 6. UI / 驱动集成

* `tools/polyc/src/driver.cpp` 扩展 `--lang` 帮助行，加入
  `javascript|ruby|go`（扩展名检测仍由注册表自动驱动）。
* `tools/ui/common/src/mainwindow.cpp` 在语言下拉框、扩展名映射、
  `DetectLanguage` 中加入三种语言。
* `tools/ui/common/src/settings_dialog.cpp` 同步扩大 *Default Language*
  下拉框。
* `tools/ui/common/src/file_browser.cpp` 在工作区文件过滤中追加
  `*.js`、`*.mjs`、`*.cjs`、`*.rb`、`*.go`。
* `tools/ui/common/src/compiler_service.cpp` 增补关键字表（用于补全）以及
  基于正则的工作区符号索引：JS 的 `function`/`class`，Ruby 的 `def`/`class`/
  `module`，Go 的 `func`/`type`。

## 7. 测试策略

每种语言在 `tests/unit/frontends/<lang>/<lang>_test.cpp` 提供一组 Catch2
用例，覆盖：

* 词法关键字 & 字面量识别
* 解析器对若干代表性程序的接受
* 至少一条负向用例（畸形输入 → 诊断）
* 完整下降流程：产出非空 IR 并包含期望的函数符号

所有断言均观测真实程序性质，绝不出现 `REQUIRE(true)` 之类的占位符。
