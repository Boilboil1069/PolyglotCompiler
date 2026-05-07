# `.ploy` 语言教程

> **文档版本**：3.0.0  
> **更新日期**：2026-05-07  
> **项目**：PolyglotCompiler 1.45.2  
> **读者对象**：编写 `.ploy` 粘合代码的开发者、库作者，以及任何通过 `polyc` 工具链组装多语言管线的人。

---

## 目录

1. [`.ploy` 是什么？](#1-ploy-是什么)
2. [Hello, polyglot world](#2-hello-polyglot-world)
3. [词法结构](#3-词法结构)
4. [模块导入](#4-模块导入)
5. [类型系统](#5-类型系统)
6. [变量与常量](#6-变量与常量)
7. [运算符与表达式](#7-运算符与表达式)
8. [控制流](#8-控制流)
9. [函数](#9-函数)
10. [可见性与属性（v1.16）](#10-可见性与属性v116)
11. [泛型（v1.15）](#11-泛型v115)
12. [结构化异常（v1.13）](#12-结构化异常v113)
13. [协作式 async / await（v1.14）](#13-协作式-async--awaitv114)
14. [扩展字符串字面量（v1.17）](#14-扩展字符串字面量v117)
15. [STRUCT、OPTION、模式匹配](#15-structoption模式匹配)
16. [面向对象构造](#16-面向对象构造)
17. [跨语言链接](#17-跨语言链接)
18. [类型映射与转换](#18-类型映射与转换)
19. [管线组合](#19-管线组合)
20. [诊断码](#20-诊断码)
21. [文档注释与 `polydoc`（v1.18）](#21-文档注释与-polydocv118)
22. [样例矩阵导览](#22-样例矩阵导览)
23. [关键字参考](#23-关键字参考)

---

# 1. `.ploy` 是什么？

`.ploy` 是 PolyglotCompiler 的核心跨语言 DSL。一个 `.ploy` 源文件做三件事：

1. 声明所支持的 9 种宿主语言（C++、Python、Rust、Java、.NET / C#、Go、JavaScript、Ruby 以及 `.ploy` 自身）中的 **模块**。
2. 在这些语言之间声明 **带类型的桥接**（`LINK`、`MAP_TYPE`、`CONVERT`）。
3. 用 `.ploy` 自身的语句 / 表达式语言承载 **跨语言业务逻辑**，通过 `CALL`、`NEW`、`METHOD`、`GET`、`SET`、`WITH`、`DELETE`、`EXTEND`、`PIPELINE` 派发到宿主语言代码。

`.ploy` 文件由 `frontend_ploy` 库解析，降级到与其它前端共享的 SSA IR，由 `middle_ir` 优化，再由三个后端之一（`backend_x86_64`、`backend_arm64`、`backend_wasm`）发射。

---

# 2. Hello, polyglot world

```ploy
// hello.ploy — 自包含最小程序

FUNC main() -> i32 {
    PRINTLN "Hello, polyglot world!";
    RETURN 0;
}
```

编译并运行：

```bash
polyc hello.ploy -o hello
./hello
```

复用 C++ 算术与 Python 格式化的双语版本：

```ploy
// hello_polyglot.ploy
IMPORT cpp::math_ops;
IMPORT python::string_utils;

LINK(python, cpp, string_utils::format_result, math_ops::abs_val) RETURNS python::str {
    MAP_TYPE(python::int, cpp::int);
}

FUNC main() -> i32 {
    LET formatted = CALL(python, string_utils::format_result, -42);
    PRINTLN formatted;
    RETURN 0;
}
```

---

# 3. 词法结构

## 3.1 注释

```ploy
// 行注释。
/* 块注释。 */
/// polydoc 抽取的文档注释（v1.18 起）。
```

## 3.2 标识符

`[A-Za-z_][A-Za-z0-9_]*`，区分大小写。标识符由 `frontend_common::SharedTokenPool` 内化。

## 3.3 关键字（54 个）

完整列表见第 23 节。约定关键字使用大写；解析器**严格区分大小写**（`func` 是标识符，`FUNC` 才是关键字）。

## 3.4 字面量

| 形式               | 示例                                           | 说明                                                       |
|--------------------|------------------------------------------------|------------------------------------------------------------|
| 整数               | `42`、`0xff`、`0o17`、`0b1010`、`1_000_000`    | 允许下划线分隔。                                            |
| 浮点               | `3.14`、`2.5e-3`、`1_000.5`                    |                                                            |
| 布尔               | `TRUE`、`FALSE`                                |                                                            |
| 空                 | `NULL`                                         |                                                            |
| 字符串（普通）     | `"hello\nworld"`                               | 标准转义（`\n`、`\t`、`\\`、`\"`、`\xNN`、`\uNNNN`）。       |
| 字符串（原始）     | `r"C:\path"`、`r#"contains "quotes""#`         | 不做转义；`#` 填充以容纳内嵌引号。                          |
| 字符串（多行）     | `"""line1\nline2"""`                           | 换行原样保留。                                              |
| 字符串（模板）     | `f"answer = {42}, pi = {3.14}"`                | 大括号内为插值表达式。                                      |
| 字符               | `'A'`、`'\n'`                                  |                                                            |

## 3.5 空白与语句终止符

语句以 `;` 结尾。块用 `{ ... }`。换行被视作普通空白，没有缩进规则。

---

# 4. 模块导入

```ploy
IMPORT cpp::math_ops;            // C++ 翻译单元
IMPORT python::string_utils;     // Python 模块
IMPORT rust::data;               // Rust crate
IMPORT java::com.example.Util;   // Java 全限定名
IMPORT dotnet::App.Util;         // .NET 命名空间
IMPORT go::pkg.util;             // Go 包
IMPORT javascript::./util.js;    // JS 文件（Node 解析）
IMPORT ruby::util;               // Ruby（require / Bundler）

IMPORT python::numpy PACKAGE "numpy" VERSION ">=1.21";
IMPORT java::org.json.JSONObject PACKAGE "org.json:json:20231013";
IMPORT rust::serde PACKAGE "serde" VERSION "1";
IMPORT dotnet::Newtonsoft.Json PACKAGE "Newtonsoft.Json" VERSION "13.0.*";
```

按语言的源文件定位规则记录在 `docs/specs/ploy_language.md`。驱动通过与 `polyver detect` 写入相同的包管理粘合层来解析模块路径。

---

# 5. 类型系统

## 5.1 原生类型（大小写不敏感的同义词）

| `.ploy`             | C++           | Python  | Rust  | Java     | .NET     | Go      | JS / Ruby             |
|---------------------|---------------|---------|-------|----------|----------|---------|------------------------|
| `INT` / `i32`       | `int`         | `int`   | `i32` | `int`    | `int`    | `int32` | `Number` / `Integer`   |
| `LONG` / `i64`      | `int64_t`     | `int`   | `i64` | `long`   | `long`   | `int64` | `BigInt` / `Integer`   |
| `i8`、`i16`、`u8` … | 定宽整数      | `int`   | 定宽  | 定宽     | 定宽     | 定宽    | 定宽                   |
| `FLOAT` / `f32`     | `float`       | `float` | `f32` | `float`  | `float`  | `float32` | `Number` / `Float`   |
| `DOUBLE` / `f64`    | `double`      | `float` | `f64` | `double` | `double` | `float64` | `Number` / `Float`   |
| `BOOL`              | `bool`        | `bool`  | `bool`| `boolean`| `bool`   | `bool`  | `Boolean` / `TrueClass`|
| `STRING`            | `std::string` | `str`   | `String` | `String`| `string`| `string`| `String`              |
| `CHAR`              | `char`        | `str[1]`| `char` | `char`  | `char`   | `rune`  | n/a                    |
| `VOID`              | `void`        | `None`  | `()`  | `void`   | `void`   | n/a     | `null`                 |

## 5.2 复合类型

| 形式              | 示例                          | 描述                                           |
|-------------------|-------------------------------|------------------------------------------------|
| `LIST<T>`         | `LIST<INT>`                   | 动态数组。                                      |
| `MAP<K, V>`       | `MAP<STRING, INT>`            | 哈希表。                                        |
| `SET<T>`          | `SET<STRING>`                 | 哈希集合。                                      |
| `OPTION<T>`       | `OPTION<INT>`                 | Some / None ADT，`IF LET` 解构。               |
| `RESULT<T, E>`    | `RESULT<INT, STRING>`         | Ok / Err ADT。                                  |
| `STRUCT`          | `STRUCT Point { x: i32, y: i32 }` | 命名记录。                                  |
| `T*`              | `INT*`                        | 指针（按宿主 ABI）。                            |

## 5.3 类型修饰符

| 修饰符     | 含义                                                     |
|------------|----------------------------------------------------------|
| `CONST`    | 只读绑定。                                                |
| `MUTABLE`  | 可变绑定（`LET VAR` 默认）。                              |
| `PUB`      | 跨模块 / 链接边界可见（v1.16）。                          |
| `PRIVATE`  | 仅当前 `.ploy` 翻译单元内可见。                           |

---

# 6. 变量与常量

```ploy
LET x = 42;                      // 不可变，类型推导
LET y: i64 = 100;                // 不可变，显式类型
LET VAR counter: i32 = 0;        // 可变
LET PI: DOUBLE = 3.14159265;     // 文件域常量

VAR scratch: STRING = "tmp";     // 顶层声明的 LET VAR 简写
```

`LET` 必须初始化。`LET VAR` 缺少初始化器是解析错误（`polyc-err-E2001`）。

---

# 7. 运算符与表达式

| 类别       | 运算符                                                |
|------------|-------------------------------------------------------|
| 算术       | `+`、`-`、`*`、`/`、`%`、一元 `-`                     |
| 比较       | `==`、`!=`、`<`、`<=`、`>`、`>=`                      |
| 逻辑       | `AND`、`OR`、`NOT`、`&&`、`||`、`!`                   |
| 位         | `&`、`|`、`^`、`~`、`<<`、`>>`                        |
| 赋值       | `=`、`+=`、`-=`、`*=`、`/=`、`%=`                     |
| 成员       | `.`、`[]`                                             |
| 指针       | `->`、一元 `&`、一元 `*`                              |
| 区间       | `1..10`（半开）、`1..=10`（闭区间）                   |

优先级遵循 `docs/specs/ploy_grammar.md` §6 中的标准 C / Rust 混合规则。

---

# 8. 控制流

## 8.1 IF / ELSE — 外层括号可选（v1.18）

```ploy
IF x > 0 {
    PRINTLN "positive";
} ELSE IF x < 0 {
    PRINTLN "negative";
} ELSE {
    PRINTLN "zero";
}

IF (x > 0) { PRINTLN "still ok with parens"; }
```

## 8.2 IF LET 解构（v1.18）

```ploy
FUNC head_or_zero(opt: OPTION<i32>) -> i32 {
    IF LET Some(x) = opt {
        RETURN x;
    } ELSE {
        RETURN 0;
    }
}
```

## 8.3 WHILE / DO WHILE / FOR

```ploy
LET VAR i: i32 = 0;
WHILE i < 10 {
    PRINTLN i;
    i = i + 1;
}

FOR x IN xs {
    PRINTLN x;
}

FOR i IN 0..10 {
    PRINTLN i;
}
```

## 8.4 MATCH（模式匹配，v1.10 起）

```ploy
MATCH value {
    1            => PRINTLN "one";
    2 | 3        => PRINTLN "two or three";
    n IF n > 10  => PRINTLN "big";
    _            => PRINTLN "other";
}
```

## 8.5 BREAK / CONTINUE / RETURN

```ploy
WHILE TRUE {
    IF done { BREAK; }
    IF skip { CONTINUE; }
}
RETURN 0;
```

---

# 9. 函数

```ploy
FUNC add(a: i32, b: i32) -> i32 {
    RETURN a + b;
}

// 默认参数（v1.10）
FUNC greet(name: STRING = "world") -> STRING {
    RETURN f"hello, {name}";
}

// 多返回值通过 STRUCT 或 RESULT。
FUNC parse(s: STRING) -> RESULT<i32, STRING> {
    IF s == "0" { RETURN Ok(0); }
    RETURN Err("not a digit");
}
```

`.ploy` 程序入口为 `FUNC main() -> i32`。驱动也接受 `FUNC main()`（返回 unit，被视作退出码 0）。

---

# 10. 可见性与属性（v1.16）

```ploy
PUB STRUCT Point { x: i32, y: i32 }   // 跨 LINK 边界可见
PRIVATE FUNC inner_helper() -> i32 { RETURN 7; }

@inline @hot
PUB FUNC fast_path(a: i32, b: i32) -> i32 { RETURN a + b; }

@deprecated("use fast_path")
PUB FUNC slow_path(a: i32, b: i32) -> i32 { RETURN a + b; }

@link_name("ploy_run")
PUB FUNC run() -> i32 { RETURN fast_path(inner_helper(), 1); }

EXPORT run AS "ploy_run";   // 必须 PUB；导出 PRIVATE 触发 polyc-err-E2410
```

内置属性：

| 属性                   | 作用                                                            |
|------------------------|-----------------------------------------------------------------|
| `@inline`              | 提示内联 Pass 主动内联。                                         |
| `@hot`                 | 放入热段；影响 PGO 与代码布局。                                   |
| `@cold`                | 放入冷段。                                                        |
| `@deprecated("msg")`   | 在每个调用点发出 `polyc-warn-W2401` 警告，`msg` 为说明。          |
| `@link_name("sym")`    | 覆盖外部可见的符号名。                                            |
| `@target("feature,…")` | 约束后端特性需求（如 `avx2`、`neon`）。                           |
| `@no_mangle`           | 禁用名字重整（与裸 C ABI 互操作）。                               |

未知属性触发 `polyc-err-E2402`。完整目录见 `docs/specs/ploy_attributes.md`。

---

# 11. 泛型（v1.15）

```ploy
STRUCT Pair<A, B> {
    first: A,
    second: B
}

FUNC max<T: Comparable>(a: T, b: T) -> T {
    IF a > b { RETURN a; } ELSE { RETURN b; }
}

FUNC identity<T>(x: T) -> T { RETURN x; }

FUNC sum<T>(a: T, b: T) -> T WHERE T: Numeric {
    RETURN a + b;
}
```

约束既可写在内联（`T: Comparable`），也可写在尾随 `WHERE` 子句中。运行时当前走 `docs/realization/generics.md` 描述的 **类型擦除 MVP** 路径；按实例化进行的完整单态化作为后续工作跟踪。

内置 trait 约束：`Comparable`、`Numeric`、`Hashable`、`Display`、`Clone`、`Send`。

---

# 12. 结构化异常（v1.13）

```ploy
FUNC main() {
    TRY {
        THROW "boom";
    }
    CATCH (e: Error) {
        PRINTLN f"caught: {e.message}";
    }
    FINALLY {
        PRINTLN "cleanup";
    }
}
```

- `THROW <expr>` 抛出一个值；非 `Error` 值由运行时桥包装为统一 `Error` 句柄。
- 多个 `CATCH` 子句按静态类型自上而下匹配。
- `FINALLY` 始终执行，包括控制经 `RETURN`、`BREAK` 或重抛离开块时。

跨语言反向拦截 Python / C++ / Java / .NET / Rust 异常的能力实现于运行时数据面；统一各宿主模型的 IR 级派发器作为后续工作跟踪。

---

# 13. 协作式 async / await（v1.14）

```ploy
ASYNC FUNC fetch_one() -> i32 { RETURN 1; }
ASYNC FUNC fetch_two() -> i32 { RETURN 2; }

ASYNC FUNC pipeline_demo() -> i32 {
    LET a = AWAIT fetch_one();
    LET b = AWAIT fetch_two();
    RETURN a + b;
}
```

- `ASYNC FUNC name(...) -> T` 声明返回值被隐式包装为 `Future<T>` 的函数。
- `AWAIT <expr>` 挂起当前 ASYNC 帧，直到所等待的 future 完成。
- 桥接的运行时 ABI（`__ploy_rt_async_*`）实现于 `runtime/services/async_bridge.{h,cpp}` 与 `runtime/services/event_loop.{h,cpp}`。

观察协作循环：

```bash
polyrt async --json        # 快照
polyrt async --run=64      # 推进 64 个 tick
```

运行时 async 桥连接 Python `asyncio`、Rust `Future`、C++20 协程、Java `CompletableFuture`、.NET `Task<T>`。

---

# 14. 扩展字符串字面量（v1.17）

```ploy
LET path     = r"C:\projects\polyglot";                        // 原始 — 无转义
LET sql      = r#"SELECT "name", "age" FROM users"#;           // 原始 + # 填充
LET haiku    = """An old silent pond
A frog jumps in
splash, silence again""";                                      // 多行
LET greeting = f"answer = {42}, pi = {3.14}";                   // 模板 / 插值
```

模板字符串可插入任何实现 `Display` 的表达式。混合形式（`fr"..."`、`rf"..."` 等）**不**在表面文法之内 — 用拼接，或将 `f"..."` 与 `r"..."` 子串组合。

---

# 15. STRUCT、OPTION、模式匹配

```ploy
STRUCT Point { x: i32, y: i32 }

LET p: Point = Point { x: 1, y: 2 };
PRINTLN p.x;
PRINTLN p.y;

FUNC find(xs: LIST<i32>, key: i32) -> OPTION<i32> {
    FOR (i, x) IN xs.enumerate() {
        IF x == key { RETURN Some(i); }
    }
    RETURN None;
}

MATCH find(xs, 7) {
    Some(i) => PRINTLN f"index = {i}";
    None    => PRINTLN "not found";
}
```

---

# 16. 面向对象构造

`.ploy` 提供宿主无关的对象模型，使跨语言调用形态一致 — 无论接收者来自 C++、Python、Java、.NET、Rust 等。

| 操作                                | 语法                                                    |
|-------------------------------------|---------------------------------------------------------|
| 构造宿主对象                         | `LET p = NEW(python, Person, "Alice", 30);`             |
| 调用实例方法                         | `LET name = METHOD(p, "get_name");`                     |
| 读取字段 / 属性                      | `LET age = GET(p, "age");`                              |
| 写入字段 / 属性                      | `SET(p, "age", 31);`                                    |
| 显式生命周期借用                     | `WITH(p) { ... }`                                       |
| 提前释放宿主句柄                     | `DELETE(p);`                                            |
| 用 `.ploy` 实现扩展宿主类             | `EXTEND python::Animal AS Cat { ... }`                  |

参见样例 `05_class_instantiation` … `08_delete_extend`。

---

# 17. 跨语言链接

`LINK` 声明把目标可调用绑定到源可调用：

```ploy
LINK(cpp, python, math_ops::add, string_utils::concat) RETURNS cpp::int {
    MAP_TYPE(cpp::int, python::str);
    MAP_TYPE(cpp::int, python::str);
}
```

形式为：

```
LINK(<目标语言>, <源语言>, <目标可调用>, <源可调用>) RETURNS <目标返回类型> {
    MAP_TYPE(<目标参数类型>, <源参数类型>);
    ... // 每个参数恰好一条 MAP_TYPE
}
```

Sema 强制：

- 双侧元数必须一致（`polyc-err-E3104`）；
- 每个参数恰好一条 `MAP_TYPE`（`polyc-err-E3105`）；
- `RETURNS` 类型在目标语言必须可 marshalling（`polyc-err-E3106`）。

链接声明完成后，`CALL` 通过运行时 marshalling 层调用桥接的可调用：

```ploy
LET sum = CALL(cpp, math_ops::add, 1, 2);
```

marshalling 由 `runtime/src/interop/` 生成，记录在按目标的调用图中（`polyc --emit=call-graph:cg.json`）。

---

# 18. 类型映射与转换

文件级全局类型等价用 `MAP_TYPE` 声明：

```ploy
MAP_TYPE(cpp::int,    python::int);
MAP_TYPE(cpp::double, python::float);
MAP_TYPE(rust::String, java::String);
```

自定义转换器用 `CONVERT` 注册：

```ploy
CONVERT(cpp::std::vector<int>, python::list) USING py_list_from_vec;
CONVERT(rust::Vec<u8>,          dotnet::byte[]) USING dotnet_bytes_from_vec;
```

转换器符号必须解析为对链接目标可见的运行时辅助函数（`polyrt` 把符号写入 marshalling 表）。

---

# 19. 管线组合

`PIPELINE` 串接一组跨语言调用，把上一阶段输出穿入下一阶段的第一个参数：

```ploy
PIPELINE preprocess(text: STRING) -> STRING {
    text
    | python::nlp::tokenize
    | rust::filter::lowercase
    | cpp::compress::deflate
    | dotnet::Cipher::encrypt
}
```

Sema 校验相邻阶段共享 `MAP_TYPE` 或已注册 `CONVERT`。

---

# 20. 诊断码

每条 `.ploy` 诊断都带稳定 id，形如 `polyc-(err|warn)-<E####|W####>`。常见者：

| 编码                  | 含义                                                          |
|-----------------------|---------------------------------------------------------------|
| `polyc-err-E2001`     | `LET` / `LET VAR` 缺少初始化器。                               |
| `polyc-err-E2102`     | 未知标识符。                                                   |
| `polyc-err-E2410`     | `EXPORT` 一个 `PRIVATE` 声明。                                 |
| `polyc-err-E2402`     | 未知 `@属性`。                                                 |
| `polyc-err-E3104`     | LINK 元数不匹配。                                              |
| `polyc-err-E3105`     | MAP_TYPE 数量不匹配。                                          |
| `polyc-err-E3106`     | `RETURNS` 类型不可 marshalling。                              |
| `polyc-warn-W2101`    | `--container` 与解析的输出后缀不一致。                         |
| `polyc-warn-W2401`    | 调用了 `@deprecated` API。                                     |
| `polyc-warn-W2501`    | `IMPORT` 未使用。                                              |

运行 `polyc --check file.ploy` 可获得 LSP 形态的 JSON 诊断；同一份 payload 驱动 IDE 问题面板与 `polyls`。完整表见 `docs/specs/ploy_diagnostics.md`。

---

# 21. 文档注释与 `polydoc`（v1.18）

`///` 开头的行被 `polydoc` 工具采集，需挂在顶层 `FUNC`、`STRUCT`、`LET`、`VAR` 之上：

```ploy
/// 返回 `n` 的绝对值。
/// i32::MIN 的回绕行为遵循宿主后端。
FUNC abs(n: i32) -> i32 {
    IF n < 0 { RETURN -n; }
    RETURN n;
}

/// 放弃前的最大重试次数。
LET MAX_RETRY: i32 = 5;
```

```bash
polydoc file.ploy                # Markdown 输出到 stdout
polydoc --json file.ploy         # 机器可读
polydoc -o api.md file.ploy
```

同一份 doc payload 经 `polyls` 的 hover / signature-help 响应展现。

---

# 22. 样例矩阵导览

| 范围             | 主题                                                                  |
|------------------|-----------------------------------------------------------------------|
| `00_minimal`     | 单行最小样例，stdout 按字节固定。                                      |
| `01` … `09`      | 核心 `.ploy` 互操作 — LINK、MAP_TYPE、PIPELINE、控制流、OOP。           |
| `10` … `16`      | 诊断、Java / .NET 互操作、泛型容器、async 管线、全栈、CONFIG / VENV。  |
| `17` … `30`      | 真实领域 — 字符串、数值、文件 I/O、JSON、图像、SQL、HTTP、并发、事件循环、插件、ML、分析、游戏循环。 |
| `31`、`32`       | 显式宽度整型、类型化句柄。                                             |
| `33`、`34`       | 模式匹配、默认参数。                                                   |
| `35`             | 在动态语言上 EXTEND。                                                  |
| `36`             | TRY / CATCH / FINALLY / THROW（v1.13）。                                |
| `37`             | ASYNC / AWAIT（v1.14）。                                                |
| `38`             | 带约束的泛型（v1.15）。                                                 |
| `39`             | 可见性与属性（v1.16）。                                                 |
| `40`             | 扩展字符串字面量（v1.17）。                                             |
| `41`             | 文法收尾 — 可选外层括号、IF LET、`///` 文档注释。                       |

驱动整张矩阵：

```bash
scripts/build_all_samples.sh   --polyc build/polyc --polyld build/polyld
scripts\build_all_samples.ps1  -Polyc build\polyc.exe -Polyld build\polyld.exe
```

脚本写出 `samples_report.json`（顶层带按 ASCII 排序的 `ok` 数组），由 `samples_regression_test.cpp` 与 per-sample 状态字段交叉校验。

---

# 23. 关键字参考

`.ploy` 当前共 54 个关键字（截至 1.45.2）：

```
AND, AS, ASYNC, AWAIT, BREAK, CALL, CASE, CATCH, CONST, CONTINUE,
CONVERT, DELETE, DO, DOUBLE, ELSE, EXPORT, EXTEND, FALSE, FINALLY,
FLOAT, FOR, FUNC, GET, IF, IMPORT, IN, INT, LET, LINK, LIST, LONG,
MAP, MAP_TYPE, MATCH, METHOD, NEW, NOT, NULL, OPTION, OR, PACKAGE,
PIPELINE, PRIVATE, PUB, RESULT, RETURN, RETURNS, SET, STRING, STRUCT,
THROW, TRUE, TRY, USING, VAR, VOID, WHERE, WHILE, WITH
```

类型标识符（`i8`、`i16`、`i32`、`i64`、`u8`、`u16`、`u32`、`u64`、`f32`、`f64`、`Error`、`Future`、`Some`、`None`、`Ok`、`Err`、`Comparable`、`Numeric`、`Hashable`、`Display`、`Clone`、`Send`）是保留标识符但不是关键字；重定义它们是硬错误。

---

*由 PolyglotCompiler 团队维护*  
*更新日期：2026-05-07*  
*文档版本：v3.0.0*
