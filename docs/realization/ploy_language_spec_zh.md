# .ploy 语言规范 — 多语言链接语言

## 1. 概述

`.ploy` 是为 PolyglotCompiler 项目设计的领域特定语言（DSL）。它作为 **多语言链接描述语言**，使开发者能够表达跨语言函数级别的链接、变量共享、类型映射，以及在异构源语言（C++、Python、Rust 等）之间的控制流编排。

> **为什么需要 .ploy？** 传统的跨语言调用需要手写 FFI 绑定代码，涉及大量重复的类型转换和函数包装。`.ploy` 通过声明式语法自动化了这一过程：开发者只需声明"哪个函数连接哪个函数"，编译器自动生成所有胶水代码。

`.ploy` 文件由专用前端 (`frontend_ploy`) 处理，生成适合中端和链接器使用的 IR，用于产生跨语言粘合代码和互操作存根。

## 2. 设计目标

| 目标 | 说明 |
|------|------|
| **显式跨语言链接** | 提供 `LINK` 指令，将一种语言中的目标函数/变量映射到另一种语言中的源函数/变量。开发者明确指定"谁连接谁"，避免隐式行为。 |
| **类型桥接** | 通过 `MAP_TYPE` 声明如何在语言边界之间进行类型编组。例如 C++ 的 `int` 对应 Python 的 `int`。 |
| **模块导入** | 通过 `IMPORT` 引用来自不同语言的已编译模块，支持 `language::module` 形式。 |
| **包导入** | 通过 `IMPORT ... PACKAGE` 引用目标语言的原生包（如 Python 的 numpy、Rust 的 serde），扩展了互操作范围。 |
| **控制流** | 支持 `IF/ELSE`、`WHILE`、`FOR`、`MATCH`，用于编排复杂的链接逻辑和数据处理流程。 |
| **变量声明** | 支持 `LET`（不可变）和 `VAR`（可变）绑定，用于保存中间计算结果。 |
| **函数定义** | 支持原生 `.ploy` 函数，用于编写胶水逻辑和数据转换。 |
| **管道组合** | `PIPELINE` 块用于串联多语言函数调用，形成多阶段处理流水线。 |
| **自定义类型转换** | `MAP_FUNC` 用于定义复杂类型的转换函数。 |
| **包生态系统访问** | 通过 `IMPORT ... PACKAGE` 使用目标语言的包生态系统（如 Python 的 numpy、scipy）。 |

## 3. 词法结构

### 3.1 关键字

`.ploy` 语言共有 **54 个保留关键字**：

```
LINK        IMPORT      EXPORT      MAP_TYPE    PIPELINE
FUNC        LET         VAR         RETURN      IF
ELSE        WHILE       FOR         IN          MATCH
CASE        DEFAULT     BREAK       CONTINUE    AS
TRUE        FALSE       NULL        AND         OR
NOT         CALL        VOID        INT         FLOAT
STRING      BOOL        ARRAY       STRUCT      PACKAGE
LIST        TUPLE       DICT        OPTION      MAP_FUNC
CONVERT     CONFIG      VENV        CONDA       UV
PIPENV      POETRY      NEW         METHOD      GET
SET         WITH        DELETE      EXTEND
```

> **关键字分类：**
> - **链接相关**：`LINK`、`IMPORT`、`EXPORT`、`MAP_TYPE`、`PACKAGE` — 用于定义跨语言链接关系
> - **程序结构**：`FUNC`、`PIPELINE`、`STRUCT`、`MAP_FUNC` — 用于定义代码组织单元
> - **变量**：`LET`、`VAR` — 用于声明不可变和可变变量
> - **控制流**：`IF`、`ELSE`、`WHILE`、`FOR`、`IN`、`MATCH`、`CASE`、`DEFAULT`、`BREAK`、`CONTINUE`、`RETURN` — 用于控制程序执行流程
> - **运算符关键字**：`AND`、`OR`、`NOT`、`AS` — 逻辑运算和类型别名
> - **字面量关键字**：`TRUE`、`FALSE`、`NULL` — 布尔值和空值
> - **类型关键字**：`VOID`、`INT`、`FLOAT`、`STRING`、`BOOL`、`ARRAY`、`LIST`、`TUPLE`、`DICT`、`OPTION` — 内置类型名称
> - **操作关键字**：`CALL`、`CONVERT`、`NEW`、`METHOD`、`GET`、`SET`、`DELETE` — 跨语言调用、类型转换、类实例化、方法调用、属性访问、对象销毁
> - **OOP 扩展**：`WITH`、`EXTEND` — 资源管理和类继承扩展
> - **包管理器**：`CONFIG`、`VENV`、`CONDA`、`UV`、`PIPENV`、`POETRY` — 包管理器环境配置

### 3.2 标识符

标识符遵循 C 风格规则：以字母或下划线开头，后跟字母、数字或下划线。

> **限定名称说明：**
> - `::` 用于作用域解析，如 `cpp::math::add` 表示 C++ 的 math 模块中的 add 函数
> - `.` 用于包路径分隔，如 `numpy.linalg` 表示 numpy 的 linalg 子包

### 3.3 字面量

| 类型 | 示例 | 说明 |
|------|------|------|
| **整数** | `42`、`0xFF`、`0b1010`、`0o77` | 支持十进制、十六进制、二进制、八进制 |
| **浮点数** | `3.14`、`1.0e-5` | 支持科学记数法 |
| **字符串** | `"hello"`、`"转义 \"引号\""` | 双引号括起，支持转义字符 |
| **布尔值** | `TRUE`、`FALSE` | 逻辑真和假 |
| **空值** | `NULL` | 表示空/无值 |
| **列表** | `[1, 2, 3]` | 方括号括起的有序集合 |
| **元组** | `(1, "hello")` | 圆括号括起的不可变异构集合 |
| **结构体** | `Point { x: 1.0, y: 2.0 }` | 命名字段的结构化数据 |

### 3.4 运算符

```
+  -  *  /  %          （算术运算：加、减、乘、除、取模）
== != < > <= >=        （比较运算：等于、不等于、小于、大于等）
&& || !                （逻辑运算：与、或、非）
=                      （赋值运算）
->                     （箭头：用于函数返回类型声明）
::                     （作用域解析：用于语言::模块::函数的限定）
.                      （成员访问 / 包路径分隔符）
,  ;  :                （分隔符：逗号、分号、冒号）
( ) { } [ ]            （分组：圆括号、花括号、方括号）
```

### 3.5 注释

```ploy
// 单行注释：从 // 到行末
/* 多行注释：
   可以跨越多行 */
```

### 3.6 分号规则

> **这是 .ploy 语法中最重要的规则之一。** 分号的使用遵循以下明确规则：

#### 需要分号的语句（简单语句）：

所有 **非块级语句** 必须以分号结尾：

```ploy
LET x = 42;                                 // 变量声明 — 必须有分号
VAR y = 0;                                  // 可变变量声明 — 必须有分号
RETURN x;                                   // 返回语句 — 必须有分号
BREAK;                                      // 循环中断 — 必须有分号
CONTINUE;                                   // 循环继续 — 必须有分号
x = x + 1;                                  // 表达式语句 — 必须有分号
IMPORT cpp::math;                           // 模块导入 — 必须有分号
IMPORT python PACKAGE numpy AS np;          // 包导入 — 必须有分号
EXPORT f AS "fn";                           // 导出声明 — 必须有分号
LINK(cpp, python, f, g);                    // 简单链接 — 必须有分号
MAP_TYPE(cpp::int, python::int);            // 类型映射 — 必须有分号
```

#### 不需要分号的语句（块级语句）：

所有 **以花括号 `{}` 结尾** 的声明/语句不需要分号：

```ploy
FUNC f() -> void { RETURN; }                 // 函数声明 — 不需要分号
PIPELINE p { }                               // 管道声明 — 不需要分号
IF x > 0 { } ELSE { }                        // 条件语句 — 不需要分号
WHILE x > 0 { }                              // 循环语句 — 不需要分号
FOR i IN items { }                           // 遍历语句 — 不需要分号
MATCH x { CASE 1 => { } }                    // 匹配语句 — 不需要分号
STRUCT S { x: i32; }                         // 结构体声明 — 不需要分号
MAP_FUNC f(x: i32) -> i32 { RETURN x; }      // 映射函数 — 不需要分号
LINK(a, b, c, d) { MAP_TYPE(a::t, b::t); }   // 带体的链接 — 不需要分号
```

> **简记规则：** 如果语句以 `}` 结束，不加分号；否则加分号。

## 4. 语法

### 4.1 顶层声明

```
program         ::= (top_level_decl)*
top_level_decl  ::= link_decl | import_decl | export_decl | map_type_decl
                   | pipeline_decl | func_decl | struct_decl | map_func_decl
                   | var_decl | statement
```

> **说明：** `.ploy` 文件由零个或多个顶层声明组成。声明的顺序不影响语义——所有声明在编译前都会被收集和解析。

### 4.2 LINK 指令

> **这是 .ploy 最核心的指令。** `LINK` 声明跨语言函数级链接关系，告诉编译器"目标语言的某个函数需要调用源语言的某个函数"。

语法使用 **括号化的4参数形式**：

```ploy
LINK(target_language, source_language, target_function, source_function);
```

> **参数说明：**
> - `target_language`：目标语言标识符（`cpp`、`python`、`rust`、`c`、`ploy`）
> - `source_language`：源语言标识符
> - `target_function`：目标端的函数名（可用 `::` 限定，如 `math::add`）
> - `source_function`：源端的函数名

#### 扩展形式 1：带类型映射体

```ploy
// 当链接的函数间需要特定的类型转换规则时使用
LINK(cpp, python, math::process, data::load) {
    MAP_TYPE(cpp::double, python::float);
    MAP_TYPE(cpp::int, python::int);
}
```

> **说明：** 花括号内的 `MAP_TYPE` 指令仅对此 `LINK` 生效，定义参数和返回值的类型转换。

#### 扩展形式 2：变量链接

```ploy
LINK(cpp, python, config_data, py_config) AS VAR;
```

> **说明：** `AS VAR` 表示这是一个全局变量的链接，而不是函数链接。用于在不同语言间共享全局数据。

#### 扩展形式 3：结构体链接

```ploy
LINK(cpp, rust, Point, RustPoint) AS STRUCT {
    MAP_TYPE(cpp::double, rust::f64);
    MAP_TYPE(cpp::int, rust::i32);
}
```

> **说明：** `AS STRUCT` 表示链接的是结构体类型，花括号内定义字段类型的映射关系。

### 4.3 IMPORT 指令

> **IMPORT 用于引入外部模块和包。** 支持三种形式，覆盖不同的导入场景。

#### 形式 1：路径导入

```ploy
IMPORT "path/to/module" AS my_module;
```

> **说明：** 从文件路径导入模块，必须指定别名。适合导入本地的 `.ploy` 文件。

#### 形式 2：限定模块导入

```ploy
IMPORT cpp::math_utils;
IMPORT python::data_loader;
IMPORT rust::validator;
```

> **说明：** 使用 `语言::模块名` 形式导入已编译的模块。编译器会在对应语言的模块搜索路径中查找。

#### 形式 3：包导入

```ploy
IMPORT python PACKAGE numpy AS np;
IMPORT python PACKAGE scipy.optimize AS opt;
IMPORT rust PACKAGE serde;
```

> **说明：** `PACKAGE` 关键字表示导入目标语言的原生包。支持用 `.` 分隔的子包路径（如 `scipy.optimize`）。`AS` 别名可选——如果不指定，则使用包名作为标识符。
>
> **使用场景：** 当需要在跨语言管道中使用 Python 的 numpy 进行矩阵运算，或使用 Rust 的 serde 进行序列化时。

### 4.4 EXPORT 指令

> **EXPORT 将 .ploy 定义的函数暴露给外部使用。**

```ploy
EXPORT function_name;                    // 使用原始名称导出
EXPORT function_name AS "external_name"; // 使用指定的外部名称导出
```

> **说明：** 导出的函数可以被其他语言的代码直接调用。`AS "外部名称"` 允许指定一个对外可见的名称，与内部名称不同。

### 4.5 MAP_TYPE 指令

> **MAP_TYPE 声明两种语言之间的类型映射关系。**

```ploy
MAP_TYPE(source_language::type, target_language::type);
```

示例：

```ploy
MAP_TYPE(cpp::int, python::int);           // C++ int ↔ Python int
MAP_TYPE(cpp::double, python::float);      // C++ double ↔ Python float
MAP_TYPE(cpp::std::string, python::str);   // C++ string ↔ Python str
MAP_TYPE(rust::f64, cpp::double);          // Rust f64 ↔ C++ double
```

> **说明：** 编译器使用这些映射自动生成类型转换（编组/反编组）代码。如果两种类型在内存布局上兼容，编译器会优化为零拷贝转换。

### 4.6 STRUCT 声明

> **STRUCT 定义命名的聚合类型，用于跨语言结构体映射。**

```ploy
STRUCT Point {
    x: f64;
    y: f64;
    label: STRING;
}
```

> **说明：** 结构体的每个字段有名称和类型。字段之间用分号分隔。结构体可用于 `LINK ... AS STRUCT` 中进行跨语言结构映射。

```ploy
STRUCT DataSet {
    name: STRING;
    values: LIST(f64);
    metadata: DICT(STRING, STRING);
}
```

> **说明：** 字段类型可以是容器类型（`LIST`、`DICT` 等），支持嵌套的复杂数据结构。

### 4.7 MAP_FUNC 声明

> **MAP_FUNC 定义自定义的类型转换函数。** 当 `MAP_TYPE` 无法表达的复杂转换逻辑时使用。

```ploy
MAP_FUNC normalize(x: f64) -> f64 {
    IF x < 0.0 {
        RETURN 0.0;
    }
    IF x > 1.0 {
        RETURN 1.0;
    }
    RETURN x;
}
```

> **说明：** `MAP_FUNC` 与普通 `FUNC` 的区别在于：`MAP_FUNC` 会被注册到转换函数表中，在跨语言调用时可被自动调用进行类型转换。

### 4.8 PIPELINE 指令

> **PIPELINE 定义多阶段、多语言处理流水线。**

```ploy
PIPELINE data_analysis {
    FUNC load() -> LIST(f64) {
        LET data = CALL(python, loader::read, "data.csv");
        RETURN data;
    }

    FUNC process(data: LIST(f64)) -> f64 {
        LET result = CALL(cpp, math::compute, data);
        RETURN result;
    }
}
```

> **说明：** PIPELINE 内部包含一系列 FUNC 声明，代表处理流水线的不同阶段。每个阶段可以使用不同语言的函数，编译器自动在阶段间插入类型转换代码。

### 4.9 函数定义

> **FUNC 定义 .ploy 原生函数。**

```ploy
FUNC add(a: i32, b: i32) -> i32 {
    RETURN a + b;
}
```

> **语法详解：**
> - `FUNC` — 关键字，声明函数
> - `add` — 函数名
> - `(a: i32, b: i32)` — 参数列表，每个参数格式为 `名称: 类型`
> - `-> i32` — 返回类型声明
> - `{ ... }` — 函数体

### 4.10 变量声明

> **LET 和 VAR 分别声明不可变和可变变量。**

```ploy
LET x = 42;                              // 不可变变量，类型由字面量推断为 i32
VAR y = 3.14;                            // 可变变量，类型推断为 f64
LET result = CALL(cpp, compute, x);      // 类型由跨语言调用的返回类型推断
```

> **区别：**
> - `LET` 声明的变量不可重新赋值（类似 Rust 的 `let` 或 C++ 的 `const auto`）
> - `VAR` 声明的变量可以重新赋值（类似普通变量）

### 4.11 控制流

#### IF/ELSE — 条件分支

```ploy
IF condition {
    LET a = 1;
} ELSE IF other_condition {
    LET b = 2;
} ELSE {
    LET c = 3;
}
```

> **说明：** 条件表达式不需要括号（与 Go/Rust 类似）。支持 `ELSE IF` 链式条件。

#### WHILE — 条件循环

```ploy
VAR i = 0;
WHILE i < 10 {
    i = i + 1;
}
```

> **说明：** 当条件为真时重复执行循环体。支持 `BREAK` 提前退出和 `CONTINUE` 跳过本次迭代。

#### FOR — 遍历循环

```ploy
FOR item IN collection {
    LET processed = item + 1;
}
```

> **说明：** 遍历集合中的每个元素。`collection` 可以是列表、数组或其他可迭代类型。

#### MATCH — 模式匹配

```ploy
MATCH value {
    CASE 0 => {
        RETURN "zero";
    }
    CASE 1 => {
        RETURN "one";
    }
    DEFAULT => {
        RETURN "other";
    }
}
```

> **说明：** 类似于 C 的 `switch` 或 Rust 的 `match`。每个 `CASE` 后跟 `=>` 和花括号体。`DEFAULT` 分支处理所有未匹配的情况。

### 4.12 CALL 表达式

> **CALL 是执行跨语言函数调用的核心表达式。**

```ploy
CALL(language, function_name, arg1, arg2, ...);
```

> **参数说明：**
> - `language`：目标语言标识符
> - `function_name`：要调用的函数名（可限定）
> - `arg1, arg2, ...`：传递给函数的参数

示例：

```ploy
LET data = CALL(python, loader::read, "input.csv");   // 调用 Python 函数
LET result = CALL(cpp, math::compute, data, 42);       // 调用 C++ 函数
LET valid = CALL(rust, validator::check, result);       // 调用 Rust 函数
```

> **说明：** 编译器根据已注册的 `MAP_TYPE` 规则，自动在参数传递和返回值接收时插入类型转换代码。

### 4.13 CONVERT 表达式

> **CONVERT 用于显式类型转换。**

```ploy
LET x = CONVERT(python_value, i32);
LET items = CONVERT(raw_list, LIST(f64));
```

> **说明：** 将表达式转换为目标类型。编译器会查找注册的转换路径（直接映射、MAP_TYPE 或 MAP_FUNC），如果找不到合法路径则报错。

### 4.14 表达式语法

```
expression      ::= assignment_expr
assignment_expr ::= logical_or ('=' assignment_expr)?
logical_or      ::= logical_and ('||' logical_and)*
logical_and     ::= equality ('&&' equality)*
equality        ::= comparison (('==' | '!=') comparison)*
comparison      ::= addition (('<' | '>' | '<=' | '>=') addition)*
addition        ::= multiplication (('+' | '-') multiplication)*
multiplication  ::= unary (('*' | '/' | '%') unary)*
unary           ::= ('!' | '-' | 'NOT') unary | call_expr
call_expr       ::= primary ('(' arguments? ')')* ('.' identifier)*
primary         ::= identifier | literal | '(' expression ')'
                   | call_directive | list_literal | struct_literal
                   | convert_expr
```

> **优先级从低到高：** 赋值 < 逻辑或 < 逻辑与 < 相等 < 比较 < 加减 < 乘除 < 一元运算 < 调用

## 5. 类型系统

### 5.1 内置原始类型

| .ploy 类型    | C++ 等价 | Python 等价 | Rust 等价 | 说明 |
|-------------|----------|------------|----------|------|
| i32         | int32_t  | int        | i32      | 32位有符号整数 |
| i64         | int64_t  | int        | i64      | 64位有符号整数 |
| f32         | float    | float      | f32      | 32位浮点数 |
| f64         | double   | float      | f64      | 64位浮点数 |
| BOOL        | bool     | bool       | bool     | 布尔值（真/假） |
| STRING / str| std::string | str     | String   | UTF-8 字符串 |
| VOID        | void     | None       | ()       | 无返回值 |
| ptr         | void*    | object     | *mut u8  | 不透明指针类型 |

### 5.2 容器类型

| .ploy 类型        | C++ 等价                     | Python 等价  | Rust 等价              | 说明 |
|-------------------|----------------------------|-------------|------------------------|------|
| LIST(T)           | std::vector\<T\>           | list        | Vec\<T\>               | 动态长度的有序集合 |
| TUPLE(T1, T2)     | std::tuple\<T1, T2\>      | tuple       | (T1, T2)               | 固定大小的异构集合 |
| DICT(K, V)        | std::unordered_map\<K, V\> | dict        | HashMap\<K, V\>        | 键值对映射 |
| OPTION(T)         | std::optional\<T\>         | Optional[T] | Option\<T\>            | 可空包装类型 |

> **容器类型语法注意：** 容器类型使用圆括号 `()` 包裹类型参数，如 `LIST(f64)`、`DICT(STRING, i32)`。

### 5.3 跨语言类型转换

编译器根据 `MAP_TYPE` 指令和内置类型转换表生成编组代码。在安全的情况下，自动生成：

| 转换类型 | 生成策略 |
|---------|---------|
| 数值类型（int → float 等） | 类型转换指令 |
| 字符串 | UTF-8 编码标准化 |
| 数组/列表 | 逐元素复制 + 元素类型转换 |
| 结构体 | 逐字段编组 |
| 字典 | 遍历条目 + 键值转换 |
| 指针/句柄 | ForeignHandle 包装 + 所有权追踪 |

## 6. 编译管道

```
.ploy 源代码
    │
    ▼
┌──────────┐
│  词法器   │  → Token 流（54 个关键字 + 运算符 + 字面量）
│  Lexer   │     识别关键字、标识符、数字、字符串、符号、注释
└──────────┘
    │
    ▼
┌──────────┐
│  解析器   │  → AST（抽象语法树）
│  Parser  │     将 Token 流构建为声明、语句、表达式的树结构
└──────────┘
    │
    ▼
┌──────────┐
│ 语义分析  │  → 类型检查后的 AST、符号解析、链接验证
│   Sema   │     验证类型兼容性、解析标识符、检查链接目标有效性
└──────────┘
    │
    ▼
┌───────────┐
│   降级     │  → 多语言 IR（中间表示）
│ Lowering  │     将 AST 转换为 IR 指令，包含跨语言调用节点和编组代码
└───────────┘
    │
    ▼
┌───────────┐
│  链接器    │  → 粘合代码生成、存根发射
│  Linker   │     为每个 LINK 生成包装函数和 FFI 桥接代码
└───────────┘
```

## 7. 完整示例

```ploy
// ===== 模块和包导入 =====
IMPORT cpp::math_utils;                     // 导入 C++ 的 math_utils 模块
IMPORT python PACKAGE numpy AS np;          // 导入 Python 的 numpy 包，别名 np
IMPORT rust::validator;                     // 导入 Rust 的 validator 模块

// ===== 类型映射 =====
MAP_TYPE(cpp::int, python::int);            // C++ int ↔ Python int
MAP_TYPE(cpp::double, python::float);       // C++ double ↔ Python float
MAP_TYPE(cpp::std::string, python::str);    // C++ string ↔ Python str

// ===== 结构体定义 =====
STRUCT DataPoint {
    timestamp: i64;                         // 时间戳
    value: f64;                             // 数据值
    label: STRING;                          // 标签
}

// ===== 类型转换函数 =====
MAP_FUNC normalize(x: f64) -> f64 {
    IF x < 0.0 {
        RETURN 0.0;                         // 下限截断
    }
    IF x > 1.0 {
        RETURN 1.0;                         // 上限截断
    }
    RETURN x;                               // 已在范围内
}

// ===== 跨语言链接 =====
LINK(cpp, python, math_utils::process, np::compute) {
    MAP_TYPE(cpp::double, python::float);   // 参数类型转换
}

LINK(cpp, rust, Point, RustPoint) AS STRUCT {
    MAP_TYPE(cpp::double, rust::f64);       // 结构体字段类型转换
}

// ===== 处理管道 =====
PIPELINE analyze_data {
    FUNC load() -> LIST(f64) {
        LET raw = CALL(python, np::loadtxt, "input.csv");
        RETURN raw;
    }

    FUNC process(data: LIST(f64)) -> f64 {
        LET result = CALL(cpp, math_utils::process, data);
        RETURN result;
    }

    FUNC validate(value: f64) -> BOOL {
        LET ok = CALL(rust, validator::check, value);
        RETURN ok;
    }
}

// ===== 胶水函数 =====
FUNC transform(input: f64, scale: i32) -> f64 {
    VAR result = input * scale;
    WHILE result > 1000.0 {
        result = result / 2.0;              // 缩放到合理范围
    }
    RETURN result;
}

// ===== 导出 =====
EXPORT analyze_data AS "data_pipeline";     // 导出管道
EXPORT transform AS "transform_fn";         // 导出函数
```
