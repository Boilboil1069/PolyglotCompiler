# PolyglotCompiler 语言�?IR 规范

> **版本**: 3.0.0  
> **最后更�?*: 2026-02-22  

---

## 目录

1. [支持的源语言](#1-支持的源语言)
2. [.ploy 语言规范](#2-ploy-语言规范)
3. [统一 IR 规范](#3-统一-ir-规范)
4. [跨语言调用约定](#4-跨语言调用约定)
5. [类型编组规则](#5-类型编组规则)
6. [二进制输出格式](#6-二进制输出格�?

---

# 1. 支持的源语言

## 1.1 语言矩阵

| 语言 | 前端 | 文件扩展�?| 版本 | 状�?|
|------|------|-----------|------|------|
| C++ | `frontend_cpp` | `.cpp`, `.cxx`, `.cc`, `.h`, `.hpp` | C++11–C++23 | �?|
| Python | `frontend_python` | `.py`, `.pyi` | 3.8+ | �?|
| Rust | `frontend_rust` | `.rs` | 2018/2021 edition | �?|
| Java | `frontend_java` | `.java` | 8, 17, 21, 23 | �?|
| C# (.NET) | `frontend_dotnet` | `.cs`, `.vb` | .NET 6, 7, 8, 9 | �?|
| .ploy | `frontend_ploy` | `.ploy` | 1.0 | �?|

## 1.2 检测规�?

`polyc` 驱动程序根据文件扩展名自动检测源语言�?

| 扩展�?| 语言 |
|--------|------|
| `.cpp`, `.cxx`, `.cc`, `.h`, `.hpp` | C++ |
| `.py`, `.pyi` | Python |
| `.rs` | Rust |
| `.java` | Java |
| `.cs`, `.vb` | C# (.NET) |
| `.ploy` | .ploy |

手动指定：`polyc --lang=<语言> 输入文件`

---

# 2. .ploy 语言规范

## 2.1 概述

`.ploy` 是一种领域特定语言，用于表达跨语言函数级链接、面向对象互操作、类型映射和多语言管道编排�?

## 2.2 关键字（�?56 �?�?大小写不敏感�?

�?`Ploy 1.5.2` 起，所有保留字均按 **大小写不敏感** 方式识别。词法器�?
将每个关键字统一规范化为标准�?UPPER 大写拼写后再交给语法分析器，同时
将用户在源码中实际书写的拼写保留�?token 上（`Token::raw_lexeme`），�?
便诊断信息和"忠于源码"的格式化器仍然可以打印出用户原本写下的字面量�?
**标识�?* 仍然是大小写敏感的——只有关键字集合参与折叠�?

```
LINK        IMPORT      EXPORT      MAP_TYPE    PIPELINE
FUNC        LET         VAR         RETURN      RETURNS*
IF          ELSE        WHILE       FOR         IN
MATCH       CASE        DEFAULT     BREAK       CONTINUE
AS          TRUE        FALSE       NULL        AND
OR          NOT         CALL        VOID        INT
FLOAT       STRING      BOOL        ARRAY       STRUCT
PACKAGE     LIST        TUPLE       DICT        OPTION
MAP_FUNC    CONVERT     CONFIG      VENV        CONDA
UV          PIPENV      POETRY      NEW         METHOD
GET         SET         WITH        DELETE      EXTEND
LANG        PRINTLN     TYPE        CONST       I8
I16         I32         I64         U8          U16
U32         U64         F32         F64         USIZE
ISIZE       TRY         CATCH       FINALLY     THROW
ERROR       ASYNC       AWAIT       WHERE       PUB
PRIVATE
```

`*` `RETURNS` �?`Ploy 1.5.2` 起已 **弃用**。遗留的
`LINK(...) RETURNS Type { ... }` 写法仍然可以解析并产出与之前完全一致的
AST，但语法分析器会发出 `kDeprecatedKeyword` 警告，警告文本中会回显用�?
源码中真实写下的拼写。新代码请改�?LINK 签名上的标准 `-> Type` 箭头�?
声明返回类型�?

> **大小写约定建议�?* 本文档中所有示例继续使�?UPPER 大写的关键字纯粹
> 是出于阅读对齐需要。新写的 `.ploy` 程序推荐遵循通用编程语言常见�?
> 小写约定 —�?`link`、`func`、`var`、`return`、`if`、`else` —�?词法�?
> 会以完全相同的方式接受它们。`If` / `Func` 这样的混合大小写也会被接受，
> 但出于一致性原因不推荐�?

> **标识符冲突提示�?* 由于关键字识别现在是大小写不敏感的，过去仅在
> 大小写上与关键字不同的标识符（`config`、`array`、`get`、`set`�?
> `pipeline`、`new` ……）现在变成了保留字。请把这类标识符迁移到非关键�?
> 名称（例�?`app_config`、`np_array`、`getter`）�?

### 字符串字面量（v1.17.0 起）

Ploy 识别四种字符串字面量形式。四者共享同一 `kString` 词法 token，
并通过既有的 `polyrt_println` 风格驻留路径下沉为无 NUL 的 `(ptr,
len)` 二元组。

| 形式      | 语法                                  | 说明                                |
| --------- | ------------------------------------- | ----------------------------------- |
| 普通      | `"hello\n"`                           | 反斜杠转义（`\n`、`\r`、`\t`、`\\`、`\"`、`\0`、`\xHH`）。 |
| 原始      | `r"C:\path\no\escape"`                | 反斜杠按字面字符处理。              |
| 带填充原始| `r#"contains "quotes""#` / `r##"..."##` | `#` 数量决定闭合引号需要的填充。 |
| 多行      | `"""line1\nline2"""`                  | 换行字符按字面保留。                |
| 模板      | `f"x = {expr}"`                       | 大括号包裹的插值；`{{` / `}}` 表示字面大括号。 |

Sema 要求模板字符串中每个插值表达式具备**可格式化**类型 ——
`Int`、`Float`、`String` 或 `Bool`（同时接受 `Any` / `Unknown`，
以避免未解析的跨语言引用阻塞编译）。整体表达式类型恒为 `String`。

v1.17.0 的下沉层在所有插值都是编译期 `Literal` 时进行立即折叠；
运行时变量插值作为后续工作记录在 `docs/realization/string_literals_zh.md`。

## 2.3 声明

### LINK �?跨语言函数链接

```ploy
LINK <目标语言>::<模块>::<函数> AS FUNC(<参数类型>) -> <返回类型>;
```

**示例**:
```ploy
LINK cpp::math::add AS FUNC(INT, INT) -> INT;
LINK python::utils::format_string AS FUNC(STRING) -> STRING;
```

### IMPORT �?模块导入

```ploy
IMPORT <语言> MODULE <模块路径>;
```

### IMPORT PACKAGE �?带版本约束的包导�?

```ploy
IMPORT <语言> PACKAGE <包名>;
IMPORT <语言> PACKAGE <包名> >= <版本�?;
IMPORT <语言> PACKAGE <包名>::(<符号1>, <符号2>) >= <版本�?;
```

**版本运算�?*: `>=`, `<=`, `>`, `<`, `==`, `!=`

### EXPORT �?符号导出

```ploy
EXPORT <符号�?;
EXPORT <符号�? AS <别名>;
```

### MAP_TYPE �?跨语言类型映射

```ploy
MAP_TYPE <ploy类型> = <语言>::<类型�?;
```

### CONFIG �?包管理器配置

字符串化的规范形式（自 v1.12.0 起）：

```ploy
CONFIG <语言> "<包管理器>" "<路径或环境名>";

// 示例
CONFIG python   "venv"    "/opt/envs/ml";
CONFIG python   "conda"   "ml_env";
CONFIG python   "uv"      "/opt/envs/uv_env";
CONFIG python   "pipenv"  "/proj/myapp";
CONFIG python   "poetry"  "/proj/myapp";
CONFIG rust     "cargo"   ".";
CONFIG javascript "npm"   "./node_modules";
CONFIG java     "maven"   "./pom.xml";
CONFIG dotnet   "nuget"   "./packages";
CONFIG ruby     "bundler" "./Gemfile";
CONFIG go       "gomod"   "./go.mod";
```

旧的关键字形式（已弃用，仍可解析以保持源代码兼容）：

```ploy
CONFIG VENV "<路径>";          // Python venv
CONFIG CONDA "<环境名>";       // Conda 环境
CONFIG UV "<项目路径>";         // uv 项目
CONFIG PIPENV "<项目路径>";     // Pipenv 项目
CONFIG POETRY "<项目路径>";     // Poetry 项目
```

旧形式会触发 `kDeprecatedKeyword` 警告并给出新形式的改写建议。所有
已注册的 `(语言, 包管理器)` 组合由 `frontends/ploy/src/sema/config_registry.cpp`
集中维护，新增包管理器只需在表里加一行，无需改动词法分析器。

## 2.4 函数

```ploy
FUNC <名称>(<参数>: <类型>, ...) -> <返回类型> {
    <函数�?
}
```

## 2.5 变量

```ploy
LET <名称>: <类型> = <表达�?;   // 不可�?
VAR <名称>: <类型> = <表达�?;   // 可变
```

### TYPE —— 类型别名（自 `Ploy 1.7.0` 起）

```ploy
TYPE <别名> = <类型表达式>;
```

`TYPE` 声明把 `别名` 注册为右侧类型表达式的同义符号；别名进入与 struct
声明相同的命名空间，重复定义或屏蔽内置宽度关键字会以 `kRedefinedSymbol`
报错。诊断信息触及别名时会同时打印底层类型，例如 `Pixel (alias of i32)`。

```ploy
TYPE Pixel        = i32;           // 宽度感知的整型别名
TYPE ChannelCount = u32;
TYPE PixelBuffer  = LIST(Pixel);   // 别名透传到泛型实参
```

### CONST —— 编译期常量（自 `Ploy 1.7.0` 起）

```ploy
CONST <名称>: <类型> = <表达式>;
```

`CONST` 声明必须显式标注类型，初始化器由 ploy 语义分析器折叠：支持字面量、
对此前 `CONST` 的引用、一元 `-` / `!` / `NOT` 以及二元的算术、比较、逻辑
运算符。声明类型与折叠结果出现宽度不匹配时发出警告。折叠后的常量会作为
不可变变量注册到符号表，下游各阶段通过常规符号查找即可消费。

```ploy
CONST KMaxRetry: i32 = 5;
CONST KAlias:    i32 = KMaxRetry;   // CONST 引用 CONST
CONST KArea:     i64 = 10 * 20;     // 由 sema 折叠
```

## 2.6 控制�?

### 条件语句

```ploy
IF (<条件>) { <主体> }
ELSE IF (<条件>) { <主体> }
ELSE { <主体> }
```

### 循环

```ploy
WHILE (<条件>) { <主体> }
FOR (<变量> IN <可迭代对�?) { <主体> }
```

### 模式匹配

```ploy
MATCH (<表达�?) {
    CASE <模式> => { <主体> }
    DEFAULT => { <主体> }
}
```

### 错误处理

自 `Ploy 1.13.0` 起，可通过 `TRY` / `CATCH` / `FINALLY` / `THROW`
进行结构化异常处理：

```ploy
TRY {
    <受保护体>
}
CATCH (<绑定>: Error) {
    <处理体>
}
FINALLY {
    <清理体>
}

THROW <表达式>;
```

不带任何 `CATCH` 与 `FINALLY` 的裸 `TRY` 会被拒绝。允许多个 `CATCH`
子句，按声明顺序派发。捕获绑定的类型为内建句柄 `Error`，公开
`message: String`、`source_lang: String`、`stacktrace: List<String>`
三个字段。运行时桥把宿主语言异常（Python `Exception`、C++
`std::exception`、Java `Throwable`、.NET `Exception`、Rust
`Result::Err`）映射到统一的 `Error` 句柄；模型详见
`docs/realization/error_handling_zh.md`。

### 异步 / Await

自 `Ploy 1.14.0` 起，可通过 `ASYNC` / `AWAIT` 使用协作式异步函数：

```ploy
ASYNC FUNC fetch() -> i32 {
    LET v = AWAIT load_value();
    RETURN v;
}
```

`ASYNC FUNC name(...) -> T { ... }` 声明返回值在 ABI 边界被隐式包装
为 `Future<T>` 的异步函数，面向开发者的类型仍为 `T`。
`AWAIT <expr>` 仅允许出现在 `ASYNC FUNC` 体内，其它位置由 sema 拒绝。
运行时桥提供协作式事件循环（默认单线程，可选 work-stealing 
线程池），并将宿主语言的 awaitable（Python `asyncio` 协程、
Rust `Future`、C++20 `std::coroutine`、Java `CompletableFuture`、
.NET `Task<T>`）适配为统一的 `Future<T>` 句柄；模型详见
`docs/realization/async_model_zh.md`。

### 泛型

自 `Ploy 1.15.0` 起，`FUNC` 与 `STRUCT` 声明可携带带可选 trait
bound 的泛型类型参数列表：

```ploy
FUNC max<T: Comparable>(a: T, b: T) -> T { ... }
STRUCT Pair<A, B> { first: A, second: B }
FUNC sum<T>(a: T, b: T) -> T WHERE T: Numeric { ... }
```

参数列表 `<T: Bound1 + Bound2, U>` 紧随声明名；可选的
`WHERE` 子句位于返回类型与函数体之间，并入已有 bound。
内建 trait 注册表为
`Comparable`、`Hashable`、`Numeric`、`Iterable`、`Display`，
sema 拒绝未知 bound。v1.15.0 的下沉路径是类型擦除（每个
参数解析为 `Any`）；按实例化单态化记录为后续工作。模型详见
`docs/realization/generics_zh.md`。

### 可见性与属性

自 `Ploy 1.16.0` 起，顶层 `FUNC`、`ASYNC FUNC` 与 `STRUCT`
声明可携带属性 / 可见性前缀：

```ploy
@inline @hot PUB FUNC fast(a: i32, b: i32) -> i32 { RETURN a + b; }
PRIVATE STRUCT Internal { x: i32 }
```

`PUB` 将符号跨模块边界导出；`PRIVATE` 为默认值，可显式书写。
`EXPORT` 要求目标为 `PUB`（显式 `PRIVATE` 为硬错误；仍携默认
值的符号会被自动升级并发出弃用警告，以保证 v1.16.0 之前的源码
可编译）。属性形式为 `@name` 或 `@name(arg, ...)`；内建注册表为
`inline`、`noinline`、`always_inline`、`hot`、`cold`、`profile`、
`no_profile`、`deprecated`、`link_name`、`target`。未知属性以 sema
警告接受。模型详见 `docs/realization/visibility_attrs_zh.md` 与
`docs/specs/attribute_catalog_zh.md`。

## 2.7 跨语言面向对象操作

### 对象创建

```ploy
LET obj = NEW(<语言>, <类路�?, <参数...>);
```

### 方法调用

```ploy
LET result = METHOD(<语言>, <对象>, <方法�?, <参数...>);
```

### 属性访�?

```ploy
LET value = GET(<语言>, <对象>, <属�?);
SET(<语言>, <对象>, <属�?, <�?);
```

### 资源管理

```ploy
WITH (<语言>, <对象>) {
    // 对象在块退出时自动关闭/释放
}
```

### 对象销�?

```ploy
DELETE(<语言>, <对象>);
```

### 类继承扩�?

```ploy
EXTEND(<语言>, <基类>) {
    // 定义扩展行为
}
```

## 2.8 管道

```ploy
PIPELINE <名称> {
    STAGE <阶段�? = CALL(<语言>, <函数>, <参数...>);
    STAGE <阶段�? = CALL(<语言>, <函数>, <上一阶段>);
}
```

## 2.9 类型系统

### 原始类型

自 `Ploy 1.7.0`（需求 2026-04-28-7）起，原始类型扩展为显式宽度的有/无符号
整型与浮点型关键字。旧式拼写继续可用并按别名解析：

- `INT` ≡ `i64`
- `FLOAT` ≡ `f64`

赋值或初始化时位宽不匹配（例如把 `i64` 表达式赋给 `i32` 槽位）会以
`kTypeMismatch` 等级发出 **警告**；当诊断信息触及用户 `TYPE` 别名时，
会同步打印底层原始类型，例如 `Pixel (alias of i32)`。

| .ploy 关键字 | 底层 `core::Type` | 位宽 | 符号 |
| --- | --- | --- | --- |
| `i8` / `i16` / `i32` / `i64` | `Int(N, true)` | 8 / 16 / 32 / 64 | 有符号 |
| `u8` / `u16` / `u32` / `u64` | `Int(N, false)` | 8 / 16 / 32 / 64 | 无符号 |
| `f32` / `f64` | `Float(N)` | 32 / 64 | 不适用 |
| `usize` / `isize` | 指针宽整数 | 平台原生 | 与名称一致 |
| `INT`（`i64` 旧别名） | `Int(64, true)` | 64 | 有符号 |
| `FLOAT`（`f64` 旧别名） | `Float(64)` | 64 | 不适用 |

| .ploy 类型 | C++ | Python | Rust | Java | C# | Go | JavaScript | Ruby |
|-----------|-----|--------|------|------|-----|------|-----------|------|
| `INT`     | `int`         | `int`   | `i32`     | `int`     | `int`    | `int`     | `number`        | `Integer` |
| `FLOAT`   | `double`      | `float` | `f64`     | `double`  | `double` | `float64` | `number`        | `Float`   |
| `STRING`  | `std::string` | `str`   | `String`  | `String`  | `string` | `string`  | `string`        | `String`  |
| `BOOL`    | `bool`        | `bool`  | `bool`    | `boolean` | `bool`   | `bool`    | `boolean`       | `TrueClass` / `FalseClass` |
| `VOID`    | `void`        | `None`  | `()`      | `void`    | `void`   | （无返回值） | `undefined`   | `nil`     |

### 容器类型

| .ploy 类型 | C++ | Python | Rust | Go | JavaScript | Ruby |
|------------|-----|--------|------|------|-----------|------|
| `LIST<T>`        | `std::vector<T>`           | `list[T]`     | `Vec<T>`        | `[]T`            | `Array`         | `Array`     |
| `TUPLE<T...>`    | `std::tuple<T...>`         | `tuple`       | `(T...)`        | `struct{...}`    | （定�?Array�? | `Array`     |
| `DICT<K,V>`      | `std::unordered_map<K,V>`  | `dict[K,V]`   | `HashMap<K,V>`  | `map[K]V`        | `Object` / `Map`| `Hash`      |
| `ARRAY<T,N>`     | `T[N]`                     | `list[T]`     | `[T; N]`        | `[N]T`           | `Array`         | `Array`     |
| `OPTION<T>`      | `std::optional<T>`         | `Optional[T]` | `Option<T>`     | `*T`（可空指针） | `T \| null`    | `T \| nil` |

> **命名说明（自 v1.18.0 起）。** `LIST<T>` 是连续序列容器，语义等价
> 于 Rust `Vec<T>` 与 C++ `std::vector<T>`，**不是**链表。

> **`NULL` 与 `None`（自 v1.18.0 起）。** `NULL` 仅用于与外部代码的
> 裸指针互操作；需要空 `OPTION<T>` 时请用 `None`。Sema 会拒绝
> `LET x: OPTION<T> = NULL;` 并给出明确诊断。

### 控制流外层括号可选（自 v1.18.0 起）

`IF` / `WHILE` / `FOR` 头部支持可选的外层括号，下列写法解析等价：

```ploy
IF cond { … }            IF (cond) { … }
WHILE cond { … }         WHILE (cond) { … }
FOR i IN xs { … }        FOR (i IN xs) { … }
```

### `IF LET` 解构 `OPTION<T>`（自 v1.18.0 起）

```ploy
IF LET Some(x) = opt { use(x); } ELSE { fallback(); }
IF LET None    = opt { … }
```

`Some` 必须有且仅有一个绑定；`None` 不带绑定。绑定仅在 THEN 体可见，
类型为 `T`（`OPTION` 的内部类型实参）。

### `///` 文档注释（自 v1.18.0 起）

恰好以**三个**斜杠开头的行是文档注释，连续行累计后挂到紧邻的 `FUNC`
/ `STRUCT` / `LET` / `VAR` 声明上。普通 `//` 与 `////`（四斜杠）
仍为普通行注释。抽取工具见 [`docs/api/polydoc_zh.md`](../api/polydoc_zh.md)。

## 2.10 运算�?

| 类别 | 运算�?|
|------|--------|
| 算术 | `+`, `-`, `*`, `/`, `%` |
| 比较 | `==`, `!=`, `<`, `>`, `<=`, `>=` |
| 逻辑 | `&&`, `\|\|`, `!`（推荐写法） �?`AND`, `OR`, `NOT`（自 1.5.1 起为别名�?|
| 赋�?| `=` |
| 成员访问 | `.`, `::` |

> **逻辑运算符别名（Ploy 1.5.2+）�?* 关键字形�?`AND`、`OR`、`NOT`
> 是符号形�?`&&`、`||`、`!` 的精确别名，会产生完全相同的 AST 节点�?
> 两种写法都长期保留，但新代码推荐使用符号形式，以便源文件能够�?
> 自然地与本语言要桥接的 C/C++/Rust/JavaScript 等生态保持一致�?

## 2.11 语句终止

所有语句以分号 (`;`) 终止�?

## 2.12 PRINTLN —— 标准输出语句（自 `Ploy 1.5.3` 起）

```
PRINTLN STRING_LITERAL ';'
```

`PRINTLN` 将单个字符串字面量写入宿主进程的标准输出流。该语句刻意保持
最小：只接受一个字面量——不支持表达式、不支持拼接、不支持格式化占位符。
更丰富的形式将在运行时-IO 管线（demand `2026-04-28-49`）的后续阶段补齐。

语义：

- 字面量内容按双引号之间的字节 **原样** 取用；不会自动追加任何换行符。
  若需输出 CRLF，请在源码中显式写出转义：`PRINTLN "Hello\r\n";`。
- 反斜杠转义序列（`\r`、`\n`、`\t`、`\\`、`\"` 等）**不**由前端解码。
  解码后的字节序列由代码生成阶段统一产出，从而保证解释器、IR 文本回环
  以及发射出的 `.rdata` 段三方对同一份字面量解释完全一致。
- 空字面量（`PRINTLN "";`）是合法的，输出零字节。

`PRINTLN` 关键字遵循与其它 Ploy 关键字一致的大小写不敏感规则
（`println`、`Println`、`PRINTLN` 等价）。

---

# 3. 统一 IR 规范

## 3.1 IR 类型系统

IR 使用独立于源语言的底层类型系统：

| IR 类型 | 大小 | 说明 |
|---------|------|------|
| `i1` | 1 �?| 布尔�?|
| `i8` | 1 字节 | 字节 / 字符 |
| `i16` | 2 字节 | 短整�?|
| `i32` | 4 字节 | 32 位整�?|
| `i64` | 8 字节 | 64 位整�?|
| `f32` | 4 字节 | 单精度浮点数 |
| `f64` | 8 字节 | 双精度浮点数 |
| `void` | 0 字节 | 无�?|
| `T*` | 指针大小 | 指向 T 的指�?|
| `T&` | 指针大小 | T 的引�?|
| `[N x T]` | N × sizeof(T) | 固定大小数组 |
| `<N x T>` | N × sizeof(T) | SIMD 向量 |
| `{T1, T2, ...}` | 字段之和 | 结构体类�?|
| `fn(T1, T2) -> R` | �?| 函数类型 |

## 3.2 指令

### 算术指令

```
%result = add i64 %a, %b
%result = sub i64 %a, %b
%result = mul i64 %a, %b
%result = sdiv i64 %a, %b
%result = fadd f64 %x, %y
%result = fsub f64 %x, %y
%result = fmul f64 %x, %y
%result = fdiv f64 %x, %y
%result = frem f64 %x, %y
```

### 内存指令

```
%ptr = alloca i64
store i64 %val, i64* %ptr
%loaded = load i64, i64* %ptr
%elem = getelementptr %struct, %struct* %ptr, i64 0, i32 1
```

### 控制流指�?

```
br i1 %cond, label %true_bb, label %false_bb
br label %target
ret i64 %val
ret void
switch i64 %val, label %default [i64 0, label %case0; i64 1, label %case1]
```

### 类型转换指令

```
%ext = zext i32 %val to i64       // 零扩�?
%trunc = trunc i64 %val to i32    // 截断
%cast = bitcast i64* %ptr to i8*  // 位转�?
```

### 调用指令

```
%result = call i64 @function_name(i64 %arg1, f64 %arg2)
call void @procedure(i64 %arg)
```

### Phi 指令

```
%merged = phi i64 [%val1, %pred1], [%val2, %pred2]
```

## 3.3 函数定义

```
define i64 @function_name(i64 %param1, f64 %param2) {
entry:
    %result = add i64 %param1, 42
    ret i64 %result
}
```

## 3.4 外部声明

```
declare i64 @external_function(i64, f64)
```

## 3.5 全局变量

```
@global_var = global i64 0
@constant = constant [13 x i8] "Hello, World\00"
```

## 3.6 SSA 形式

IR �?SSA 转换过程后使用静态单赋值（SSA）形式：

- 每个变量只被赋值一�?
- Phi 节点在控制流汇合点合并�?
- 支持高效的优化过程（常量传播、死代码消除等）

---

# 4. 跨语言调用约定

## 4.1 概述

PolyglotCompiler 生成 FFI 粘合代码以桥接不同源语言之间的函数调用。每次跨语言调用经过以下步骤�?

1. **参数编组**：将调用方的类型转换为被调用方的类型
2. **函数分发**：通过运行时桥接路由调�?
3. **返回值解编组**：将被调用方的返回类型转换回调用方的类型

## 4.2 调用约定�?

| �?�?目标 | 约定 | 桥接 |
|----------|------|------|
| .ploy �?C++ | cdecl | 直接 FFI |
| .ploy �?Python | CPython C API | `__ploy_python_*` 运行�?|
| .ploy �?Rust | Rust ABI (extern "C") | 直接 FFI |
| .ploy �?Java | JNI | `__ploy_java_*` 运行�?|
| .ploy �?.NET | CoreCLR Hosting | `__ploy_dotnet_*` 运行�?|

## 4.3 运行时桥接函�?

| 桥接 | 头文�?| 说明 |
|------|--------|------|
| `__ploy_python_call` | `runtime/include/libs/python_rt.h` | 调用 Python 函数 |
| `__ploy_python_new` | `runtime/include/libs/python_rt.h` | 实例�?Python �?|
| `__ploy_java_call` | `runtime/include/libs/java_rt.h` | 通过 JNI 调用 Java 方法 |
| `__ploy_java_init` | `runtime/include/libs/java_rt.h` | 初始�?JVM |
| `__ploy_dotnet_call` | `runtime/include/libs/dotnet_rt.h` | 通过 CoreCLR 调用 .NET 方法 |
| `__ploy_dotnet_init` | `runtime/include/libs/dotnet_rt.h` | 初始�?.NET 运行�?|

---

# 5. 类型编组规则

## 5.1 原始类型编组

| .ploy �?C++ | 规则 |
|-------------|------|
| `INT` �?`int` | 直接传递（相同 ABI 表示�?|
| `FLOAT` �?`double` | 直接传�?|
| `STRING` �?`std::string` | 复制字符串数�?|
| `BOOL` �?`bool` | 直接传递（i1 �?i8�?|

| .ploy �?Python | 规则 |
|----------------|------|
| `INT` �?`int` | 转换�?`PyLong` / �?`PyLong` 转换 |
| `FLOAT` �?`float` | 转换�?`PyFloat` / �?`PyFloat` 转换 |
| `STRING` �?`str` | 转换�?`PyUnicode` / �?`PyUnicode` 转换 |
| `BOOL` �?`bool` | 转换�?`PyBool` / �?`PyBool` 转换 |

## 5.2 容器类型编组

| .ploy 类型 | 编组策略 |
|-----------|---------|
| `LIST<T>` | 逐元素复制并进行类型转换 |
| `TUPLE<T...>` | 按位置逐元素复�?|
| `DICT<K,V>` | 键值对迭代和转�?|
| `ARRAY<T,N>` | 直接内存复制（相同元素类型）或逐元素转�?|

## 5.3 对象编组

通过 `NEW` 创建的对象使用不透明句柄管理�?

- 每个对象由运行时使用唯一句柄 ID 追踪
- 方法调用通过运行时桥接使用句柄进�?
- 对象销毁（`DELETE`）释放句柄并调用析构函数/终结�?

---

# 6. 二进制输出格�?

## 6.1 编译管道

```
源码 �?前端 �?IR �?优化 �?后端 �?汇编 �?二进�?
```

## 6.2 输出产物

| 文件 | 格式 | 说明 |
|------|------|------|
| `*.ir` | 文本 | 可读�?IR 表示 |
| `*.ir.bin` | 二进�?| 序列�?IR（二进制格式�?|
| `*.asm` | 文本 | 目标特定的汇�?|
| `*.asm.bin` | 二进�?| 编码后的汇编 |
| `*.o` / `*.obj` | 目标文件 | 可重定位目标文件 |
| `*.exe` / (无扩展名) | 可执行文�?| 最终链接的二进�?|

## 6.3 辅助目录

编译过程中，`polyc` �?`aux/` 子目录中生成中间产物�?

```
aux/
├── <文件�?.ir         # IR 文本（如指定 --emit-ir�?
├── <文件�?.ir.bin     # 二进制编码的 IR
├── <文件�?.asm        # 生成的汇�?
├── <文件�?.asm.bin    # 二进制编码的汇编
├── <文件�?.obj        # 目标文件
└── <文件�?.symbols    # 符号表转�?
```

## 6.4 目标架构

| 架构 | 指令�?| 状�?|
|------|--------|------|
| x86_64 | SSE2/AVX/AVX2/AVX-512 | �?|
| ARM64 (AArch64) | NEON/SVE | �?|
| WebAssembly | MVP + SIMD | �?|
