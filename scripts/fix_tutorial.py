import pathlib

path = pathlib.Path(r'd:\Others\PolyglotCompiler\docs\tutorial\ploy_language_tutorial_zh.md')

content = r"""# .ploy 语言教程

> **版本**: 2.0.0
> **最后更新**: 2026-04-09
> **项目**: PolyglotCompiler

---

## 目录

1. [简介](#1-简介)
2. [快速入门](#2-快速入门)
3. [模块与包导入](#3-模块与包导入)
4. [跨语言函数链接](#4-跨语言函数链接)
5. [类型映射](#5-类型映射)
6. [变量与表达式](#6-变量与表达式)
7. [函数](#7-函数)
8. [控制流](#8-控制流)
9. [结构体与自定义类型](#9-结构体与自定义类型)
10. [跨语言调用](#10-跨语言调用)
11. [面向对象互操作](#11-面向对象互操作)
12. [管道](#12-管道)
13. [环境配置](#13-环境配置)
14. [类型转换](#14-类型转换)
15. [符号导出](#15-符号导出)
16. [错误处理与诊断](#16-错误处理与诊断)
17. [完整示例](#17-完整示例)
18. [关键字参考](#18-关键字参考)
19. [最佳实践](#19-最佳实践)

---

# 1. 简介

## 1.1 什么是 .ploy？

`.ploy` 是 PolyglotCompiler 项目专用的**领域特定语言**（DSL，Domain-Specific Language）。它的核心目标是充当"跨语言胶水"——用一套统一的声明式语法，描述不同编程语言的函数、类和数据如何跨越语言边界互相调用和传递。

换句话说，你不需要手写复杂的 JNI 调用、Cython 绑定或 P/Invoke 代码。你只需要在 `.ploy` 文件里声明："我要把这个 C++ 函数的结果传给这个 Python 函数"，编译器会自动生成所有胶水代码。

`.ploy` **不是**通用编程语言，它不负责实现业务逻辑，而是负责**描述和编排**跨语言的调用关系。

## 1.2 .ploy 能做什么？

| 能力 | 说明 |
|------|------|
| **链接函数** | 声明两种语言函数之间的绑定关系，让编译器生成桥接代码 |
| **实例化类** | 在一种语言里创建另一种语言的类实例，并持有其句柄 |
| **访问和修改属性** | 通过 `GET`/`SET` 读写外部对象的字段 |
| **自动资源管理** | 用 `WITH` 块确保跨语言资源（文件、连接等）被正确释放 |
| **编排多阶段管道** | 用 `PIPELINE` 将多种语言的处理阶段组合成一个可复用的工作流 |
| **管理包依赖** | 支持 pip/conda/uv/pipenv/poetry/cargo/NuGet 等生态系统 |
| **类型映射** | 告诉编译器不同语言的类型之间如何互相转换 |

## 1.3 支持的语言

目前支持以下五种语言，在 `.ploy` 文件中用对应的**标识符**引用它们：

| 语言 | 编译器前端模块 | .ploy 中的标识符 |
|------|--------------|----------------|
| C++ | `frontend_cpp` | `cpp` |
| Python | `frontend_python` | `python` |
| Rust | `frontend_rust` | `rust` |
| Java | `frontend_java` | `java` |
| C#（.NET）| `frontend_dotnet` | `dotnet` |

## 1.4 运行原理概述

当你编写一个 `.ploy` 文件并用 `polyc` 编译时，编译流程如下：

```
.ploy 源文件
    ↓ 词法分析（识别关键字和符号）
    ↓ 语法分析（构建 AST）
    ↓ 语义分析（检查类型、模块引用等）
    ↓ IR 生成（生成中间表示）
    ↓ PolyglotLinker（生成跨语言胶水代码）
    → 可执行文件 / 共享库
```

`.ploy` 语义分析阶段会检查所有跨语言调用的类型是否匹配、所有模块是否已导入、CONFIG 配置是否正确，在编译期就捕获大多数错误。

## 1.5 文件扩展名

所有 `.ploy` 源文件使用 `.ploy` 扩展名：

```
my_project.ploy
pipeline.ploy
config_and_venv.ploy
```

---

# 2. 快速入门

## 2.1 第一个 .ploy 文件

我们从一个最小的跨语言示例开始，感受 `.ploy` 的整体结构。假设你有一个 C++ 模块负责数学运算，一个 Python 模块负责格式化输出，你想把两者串联起来。

创建一个名为 `hello.ploy` 的文件：

```ploy
// hello.ploy — 一个最小的跨语言示例

// 第一步：从 C++ 和 Python 分别导入需要用到的模块
// IMPORT 告诉编译器在哪里找函数定义，类似于 C++ 的 #include
IMPORT cpp::math_ops;
IMPORT python::string_utils;

// 第二步：声明跨语言链接关系
// 这里告诉编译器：math_ops::add（C++）和 string_utils::format_result（Python）
// 会在同一个调用链中出现，编译器需要生成它们之间的类型转换胶水代码
LINK(cpp, python, math_ops::add, string_utils::format_result) {
    MAP_TYPE(cpp::int, python::int);  // C++ int 对应 Python int
}

// 第三步：定义一个跨语言函数
// FUNC 的语法和大多数语言类似，参数和返回值用 .ploy 内置类型声明
FUNC greet(a: INT, b: INT) -> STRING {
    // CALL(语言, 函数, 参数...) 是跨语言调用的标准方式
    LET sum = CALL(cpp, math_ops::add, a, b);
    // sum 此时是一个 INT，传给 Python 时会自动转换为 Python int
    LET message = CALL(python, string_utils::format_result, sum);
    RETURN message;
}

// 第四步：把这个函数以 "polyglot_greet" 的名字暴露给外部使用
EXPORT greet AS "polyglot_greet";
```

**这段代码做了什么？**
编译器读取它之后，会：
1. 检查 `cpp::math_ops` 和 `python::string_utils` 模块是否存在；
2. 验证 `math_ops::add` 接受两个 `int` 参数，`format_result` 接受一个 `int` 并返回字符串；
3. 生成 C++ ↔ Python 的类型转换桥接代码；
4. 生成一个名为 `polyglot_greet` 的 C ABI 导出符号，供主程序调用。

## 2.2 编译

使用 `polyc` 驱动程序编译 `.ploy` 文件：

```bash
polyc hello.ploy -o hello
```

> **说明**：`polyc` 会自动识别 `.ploy` 扩展名并调用 .ploy 前端，整个流程是：
> 词法分析 → 语法分析 → 语义分析（类型检查）→ IR 生成 → 跨语言链接器 → 输出二进制。

常用选项：

| 选项 | 说明 |
|------|------|
| `-o <name>` | 指定输出文件名 |
| `--verbose` | 打印详细编译日志，方便调试 |
| `--emit-ir` | 仅输出中间 IR，不进行链接 |
| `--check-only` | 仅做语法和类型检查，不生成代码 |

## 2.3 基本程序结构

一个完整的 `.ploy` 文件通常按以下顺序组织：

```ploy
// ① 环境配置（可选）
CONFIG VENV python "/path/to/venv";

// ② 包导入（可选）
IMPORT python PACKAGE numpy >= 1.20 AS np;

// ③ 模块导入（必需）
IMPORT cpp::module_name;
IMPORT python::module_name;

// ④ 跨语言链接（需要时）
LINK(cpp, python, func_a, func_b) {
    MAP_TYPE(cpp::int, python::int);
}

// ⑤ 全局类型映射（需要时）
MAP_TYPE(cpp::double, python::float);

// ⑥ 结构体定义（可选）
STRUCT Config {
    width: INT;
    height: INT;
}

// ⑦ 函数和管道（核心逻辑）
FUNC main_func() -> INT { ... }
PIPELINE my_pipeline { ... }

// ⑧ 导出（可选）
EXPORT main_func;
EXPORT my_pipeline;
```

## 2.4 注释

`.ploy` 只支持 C/C++ 风格的**单行注释**，以 `//` 开头，到行尾结束：

```ploy
// 这是一条独立注释
FUNC foo() -> INT {
    LET x = 42;  // 行内注释
    RETURN x;
}
```

> **注意**：目前不支持 `/* */` 块注释，也不支持 `#` 注释。

## 2.5 语句终止符

所有语句以分号（`;`）结尾。漏写分号是初学者最常见的语法错误：

```ploy
LET x = 10;          // ✅ 正确
VAR y: FLOAT = 3.14; // ✅ 正确
RETURN x             // ❌ 错误：缺少分号
```

块结构（`{ }` 包裹的函数体、LINK 体等）本身不需要在 `}` 后加分号。

---

# 3. 模块与包导入

## 3.1 为什么要 IMPORT？

在 `.ploy` 里，你不能凭空调用一个 C++ 函数或实例化一个 Java 类——必须先用 `IMPORT` 告诉编译器去哪里找它们的定义。`IMPORT` 的作用相当于：
- C++ 的 `#include`（引入头文件/模块）
- Python 的 `import`（引入模块作用域）
- Java 的 `import`（引入类路径）

不同的是，`.ploy` 的 `IMPORT` 同时指定了**语言**和**模块名**，因为你可能同时使用多种语言的模块。

## 3.2 模块导入

语法：`IMPORT <语言>::<模块名>;`

```ploy
IMPORT cpp::math_utils;         // 对应 math_utils.cpp
IMPORT python::data_processing; // 对应 data_processing.py
IMPORT rust::serde;
IMPORT java::SortEngine;
IMPORT dotnet::Transformer;
```

编译器会根据项目构建配置找到对应的源文件或已编译库，提取其中的函数/类签名，记入符号表。只有导入了的模块，才能在后续的 `LINK`、`CALL`、`NEW` 等指令中引用。

> **注意**：模块名大小写敏感，要与实际源文件名/类名保持一致。

## 3.3 包导入 — `IMPORT PACKAGE`

除了项目自身的模块，还可以引用来自各语言生态的外部包（如 numpy、serde、NuGet 包等）。

### 基本用法

```ploy
IMPORT python PACKAGE numpy;
IMPORT python PACKAGE numpy >= 1.20;
IMPORT python PACKAGE scipy == 1.11;
IMPORT rust PACKAGE serde >= 1.0;

// 带别名
IMPORT python PACKAGE numpy >= 1.20 AS np;
IMPORT python PACKAGE pandas >= 2.0 AS pd;
```

### 选择性导入

```ploy
// 只从 numpy 导入指定符号
IMPORT python PACKAGE numpy::(array, mean, std);
IMPORT python PACKAGE torch::(tensor, no_grad) >= 2.0;
IMPORT python PACKAGE numpy.linalg::(solve, inv);
```

> **注意**：选择性导入 `::(...)` 和别名 `AS` **不能同时使用**。

### 版本约束运算符

| 运算符 | 语义 | 示例 |
|--------|------|------|
| `>=` | 大于或等于 | `>= 1.20` |
| `<=` | 小于或等于 | `<= 2.0` |
| `==` | 精确版本 | `== 1.10.0` |
| `>` | 严格大于 | `> 1.0` |
| `<` | 严格小于 | `< 3.0` |
| `!=` | 排除某版本 | `!= 2.0` |
| `~=` | 兼容版本（PEP 440）| `~= 1.20` |

### 工作机制

编译器遇到 `IMPORT python PACKAGE numpy >= 1.20 AS np` 时，会：
1. 查找 CONFIG 声明的虚拟环境，确认 numpy 已安装且版本满足要求；
2. 若未安装，调用对应包管理器（pip/conda/uv 等）自动安装；
3. 将 numpy 的公开 API 注册到符号表，后续可用 `np::` 前缀引用。

---

# 4. 跨语言函数链接

## 4.1 为什么需要 LINK？

当你要在 `.ploy` 函数里用 `CALL` 跨语言调用时，编译器需要提前知道：
- **调用路径**：从哪种语言的哪个函数，传给哪种语言的哪个函数；
- **类型转换规则**：源语言的参数/返回值怎样转换成目标语言能理解的形式。

`LINK` 指令正是用来声明这两件事的。没有 `LINK`，编译器无法验证类型安全，也无法生成正确的桥接代码。

## 4.2 LINK 基本语法

```
LINK(<源语言>, <目标语言>, <源函数>, <目标函数>) {
    MAP_TYPE(<源语言>::<类型A>, <目标语言>::<类型B>);
}
```

## 4.3 示例：链接 C++ 到 Python

```ploy
LINK(cpp, python, math_ops::add, string_utils::format_result) {
    MAP_TYPE(cpp::int, python::int);
}
```

编译器会自动生成类似如下的桥接函数（伪代码示意）：

```c
PyObject* bridge_add_to_format(int a, int b) {
    int cpp_result = math_ops_add(a, b);
    PyObject* py_int = PyLong_FromLong(cpp_result); // cpp::int → python::int
    return string_utils_format_result(py_int);
}
```

## 4.4 示例：链接 Java 到 Python

```ploy
LINK(java, python, SortEngine::sortedKeys, collection_utils::invert_dict) {
    MAP_TYPE(java::ArrayList_String, python::list);
    MAP_TYPE(java::HashMap_String_Integer, python::dict);
}
```

## 4.5 示例：链接 Python 到 .NET

```ploy
LINK(python, dotnet, data_science::to_json, Transformer::ParseJson) {
    MAP_TYPE(python::str, dotnet::string);
}
```

## 4.6 LINK 的作用范围

一个 `LINK` 声明的类型映射规则**仅在该 LINK 调用链中生效**。如果需要在整个文件范围内复用某个类型映射，应使用全局 `MAP_TYPE`（见第 5 章）。

---

# 5. 类型映射

## 5.1 为什么需要类型映射？

不同的编程语言有不同的类型系统和内存布局：
- C++ 的 `int` 是 32 位整数，Python 的 `int` 是任意精度对象；
- Java 的 `ArrayList<String>` 和 Python 的 `list` 在内存中完全不同；
- Rust 的 `Vec<f64>` 和 .NET 的 `List<double>` 也是如此。

`MAP_TYPE` 告诉编译器：当数据跨越某两种语言的边界时，应该调用哪种转换逻辑。没有 `MAP_TYPE`，编译器会拒绝任何涉及这两种类型的跨语言调用。

## 5.2 MAP_TYPE 语法

```ploy
MAP_TYPE(cpp::int, python::int);
MAP_TYPE(cpp::double, python::float);
MAP_TYPE(cpp::std::string, python::str);

// 容器类型（用 _ 连接元素类型）
MAP_TYPE(cpp::std::vector_int, java::ArrayList_Integer);
MAP_TYPE(java::HashMap_String_Integer, python::dict);
MAP_TYPE(rust::Vec_f64, python::list);
MAP_TYPE(dotnet::List_double, python::list);
```

## 5.3 基本类型速查表

`.ploy` 提供一套与平台无关的**内置类型**：

| .ploy 类型 | C++ | Python | Rust | Java | C# |
|-----------|-----|--------|------|------|-----|
| `INT` | `int` | `int` | `i32` | `int` | `int` |
| `FLOAT` | `double` | `float` | `f64` | `double` | `double` |
| `STRING` | `std::string` | `str` | `String` | `String` | `string` |
| `BOOL` | `bool` | `bool` | `bool` | `boolean` | `bool` |
| `VOID` | `void` | `None` | `()` | `void` | `void` |

## 5.4 容器类型速查表

| .ploy 类型 | C++ | Python | Rust |
|-----------|-----|--------|------|
| `LIST<T>` | `std::vector<T>` | `list[T]` | `Vec<T>` |
| `TUPLE<T...>` | `std::tuple<T...>` | `tuple` | `(T...)` |
| `DICT<K,V>` | `std::unordered_map<K,V>` | `dict[K,V]` | `HashMap<K,V>` |
| `ARRAY<T,N>` | `T[N]` | `list[T]` | `[T; N]` |
| `OPTION<T>` | `std::optional<T>` | `Optional[T]` | `Option<T>` |

## 5.5 MAP_TYPE 的有效位置

- **全局**（文件顶层）：对整个文件生效；
- **LINK 体内**：仅对该 LINK 声明的调用链生效（优先级高于全局声明）；
- **FUNC / PIPELINE 内部**：不支持。

---

# 6. 变量与表达式

## 6.1 LET — 不可变绑定

`LET` 声明一个**不可变**局部变量，一旦绑定就不能再赋新值：

```ploy
LET x = 42;
LET name: STRING = "PolyglotCompiler";
LET pi: FLOAT = 3.14159;
LET flag: BOOL = TRUE;

// CALL 的返回值通常用 LET 绑定
LET result = CALL(cpp, math_ops::add, 10, 20);
// result = 999; ← 编译错误：LET 不可重新赋值
```

## 6.2 VAR — 可变变量

`VAR` 声明一个**可变**局部变量，可以被重新赋值：

```ploy
VAR counter = 0;
VAR total: FLOAT = 0.0;

counter = counter + 1;   // ✅ 允许重新赋值
total = total + 3.14;

// 重新赋值时不需要再写 VAR
VAR buffer: STRING = "";
buffer = buffer + "hello";
```

## 6.3 算术运算符

| 运算符 | 含义 | 备注 |
|--------|------|------|
| `+` | 加法 / 字符串拼接 | |
| `-` | 减法 / 取负 | |
| `*` | 乘法 | |
| `/` | 除法 | INT 做整数除法，FLOAT 做浮点除法 |
| `%` | 取模 | 仅 INT |

## 6.4 比较运算符

| 运算符 | 含义 |
|--------|------|
| `==` | 等于 |
| `!=` | 不等于 |
| `<` | 小于 |
| `>` | 大于 |
| `<=` | 小于等于 |
| `>=` | 大于等于 |

## 6.5 逻辑运算符

| 运算符 | 含义 | 短路求值 |
|--------|------|---------|
| `AND` | 逻辑与 | ✅ |
| `OR` | 逻辑或 | ✅ |
| `NOT` | 逻辑非 | — |

## 6.6 字面量类型

```ploy
LET a = 42;             // INT
LET b = -10;
LET c = 3.14;           // FLOAT
LET d = 1.5e10;         // 科学记数法
LET s = "Hello!";       // STRING（双引号，支持 \n \t 转义）
LET t = TRUE;           // BOOL
LET f = FALSE;
LET n = NULL;           // 用于 OPTION 类型

LET nums = [1, 2, 3];                  // LIST<INT>
LET pair = (42, "answer");             // TUPLE<INT, STRING>
LET mapping = {"a": 1, "b": 2};       // DICT<STRING, INT>
```

---

# 7. 函数

## 7.1 FUNC — 函数声明

函数是 `.ploy` 中**复用跨语言逻辑**的基本单元。必须显式指定参数类型和返回类型：

```ploy
FUNC add(a: INT, b: INT) -> INT {
    RETURN a + b;
}

FUNC greet(name: STRING) -> STRING {
    LET message = "Hello, " + name;
    RETURN message;
}

FUNC sum_array(data: LIST<FLOAT>) -> FLOAT {
    LET result = CALL(cpp, math_ops::sum, data);
    RETURN result;
}
```

`.ploy` 的 `FUNC` 会被编译成语言无关的 IR 函数。用 `EXPORT` 导出后，会生成遵循 C ABI 的导出符号，可被任何语言通过 FFI 调用。

## 7.2 RETURN 语句

`RETURN` 终止函数执行并返回值：

```ploy
FUNC max(a: INT, b: INT) -> INT {
    IF a > b {
        RETURN a;    // 提前返回
    }
    RETURN b;
}
```

每条分支路径都必须有 `RETURN`（非 VOID 函数）。如果编译器发现某条路径没有返回值，会报错。

## 7.3 VOID 函数

返回类型为 `VOID` 的函数不需要 `RETURN` 语句：

```ploy
FUNC log_result(value: INT) -> VOID {
    CALL(python, logger::info, value);
}

FUNC process(data: LIST<INT>) -> VOID {
    IF data == NULL {
        RETURN;    // 提前退出
    }
    CALL(cpp, processor::run, data);
}
```

## 7.4 函数参数的类型规则

- 参数类型**必须**使用 `.ploy` 内置类型；
- 不能直接用 `cpp::int` 等语言特定类型作为参数类型；
- 容器类型参数使用泛型语法：`LIST<INT>`、`DICT<STRING, INT>` 等。

---

# 8. 控制流

控制流语句让 `.ploy` 函数能够做条件判断、循环和模式匹配。所有控制流结构都以 `{` `}` 包裹代码块，**不使用括号包裹条件**。

## 8.1 IF / ELSE — 条件分支

```ploy
IF prediction > threshold {
    RETURN 1;
} ELSE {
    RETURN 0;
}
```

多分支用 `ELSE IF` 链接：

```ploy
IF score >= 90 {
    LET grade = "A";
} ELSE IF score >= 80 {
    LET grade = "B";
} ELSE IF score >= 70 {
    LET grade = "C";
} ELSE {
    LET grade = "F";
}
```

> **注意**：`IF` 的条件必须是 BOOL 表达式。不能把 INT 直接当条件，应写 `IF x != 0 { }`。

## 8.2 WHILE — 条件循环

当条件为 `TRUE` 时反复执行，每次执行前检查条件：

```ploy
VAR i = 0;
WHILE i < 10 {
    CALL(python, process::step, i);
    i = i + 1;
}
```

## 8.3 FOR — 迭代循环

`FOR` 用于遍历一个**范围**或**集合**：

```ploy
// 遍历整数范围 [0, 10)
FOR i IN 0..10 {
    CALL(cpp, engine::tick, i);
}

// 遍历集合中的每个元素
FOR item IN collection {
    CALL(python, processor::handle, item);
}
```

`FOR` 循环中的迭代变量是**只读**的，不能对其赋值。

## 8.4 MATCH — 模式匹配

`MATCH` 根据一个值的不同情况执行不同的分支（仅支持等值匹配）：

```ploy
MATCH mode {
    CASE 0 {
        RETURN CALL(python, ml_model::predict, data);
    }
    CASE 1 {
        CALL(cpp, image_processor::enhance, data, 100);
        RETURN CALL(python, ml_model::predict, data);
    }
    CASE 2 {
        CALL(cpp, image_processor::threshold, data, 100, 0.5);
        RETURN CALL(python, ml_model::predict, data);
    }
    DEFAULT {
        RETURN 0.0;
    }
}
```

> **注意**：每个 `CASE` 块执行完后会**自动退出** MATCH，不需要 `break`（无 C 的 fallthrough）。

## 8.5 BREAK 和 CONTINUE

在循环体内控制执行流：

```ploy
VAR current = CALL(python, ml_model::predict, data);
VAR iteration = 0;

WHILE iteration < max_iter {
    IF current > target {
        BREAK;    // 提前退出循环
    }
    CALL(cpp, image_processor::enhance, data, 100);
    current = CALL(python, ml_model::predict, data);
    iteration = iteration + 1;
}

FOR i IN 0..100 {
    IF i % 2 == 0 {
        CONTINUE;  // 跳过偶数，进入下一次迭代
    }
    CALL(python, process::handle_odd, i);
}
```

---

# 9. 结构体与自定义类型

## 9.1 为什么需要 STRUCT？

在跨语言编程中，经常需要把**一组相关字段**打包成一个复合数据结构，然后在不同语言之间传递。`.ploy` 的 `STRUCT` 允许你在语言中立的层面定义复合类型，编译器会自动生成各语言的等价结构体定义，并在跨语言传递时做正确的内存布局转换。

## 9.2 STRUCT — 结构体定义

```ploy
STRUCT PipelineConfig {
    width: INT;
    height: INT;
    channels: INT;
    learning_rate: FLOAT;
    epochs: INT;
}

STRUCT Experiment {
    name: STRING;
    epochs: INT;
    learning_rate: FLOAT;
    converged: BOOL;
}
```

**字段类型约束**：只能使用 `.ploy` 内置类型（`INT`、`FLOAT`、`STRING`、`BOOL`）和容器类型。

## 9.3 创建结构体实例

```ploy
LET config = PipelineConfig {
    width: 1920,
    height: 1080,
    channels: 3,
    learning_rate: 0.001,
    epochs: 100
};

VAR result: Experiment = Experiment {
    name: "exp_01",
    epochs: 50,
    learning_rate: 0.01,
    converged: FALSE
};
```

## 9.4 成员访问

```ploy
LET w = config.width;
LET lr = config.learning_rate;

CALL(cpp, renderer::resize, config.width, config.height);

// 修改可变结构体字段（只有 VAR 声明的实例才可修改）
result.converged = TRUE;
result.epochs = result.epochs + 1;
```

---

# 10. 跨语言调用

## 10.1 CALL 的工作原理

`CALL` 是 `.ploy` 中最核心的操作，执行**跨语言函数调用**：
1. 编译器查找已注册的桥接函数（由 `LINK` 声明生成）；
2. 在调用点插入类型转换代码（根据 `MAP_TYPE` 规则）；
3. 通过运行时 ABI（C 调用约定）调用目标语言的函数；
4. 将返回值转换回 `.ploy` 内置类型。

## 10.2 CALL 语法

```ploy
// CALL(<语言>, <模块>::<函数>, <参数...>)
LET sum       = CALL(cpp,    math_ops::add,               10, 20);
LET formatted = CALL(python, string_utils::format_result, sum);
LET data      = CALL(rust,   data_loader::load_batch,     "data.csv", 64);
LET sorted    = CALL(java,   SortEngine::sortInts,        numbers);
```

**前提**：模块已 `IMPORT`，涉及的类型已有 `MAP_TYPE` 或 `LINK` 声明。

## 10.3 跨语言调用链

```ploy
FUNC process_data(input: LIST<FLOAT>) -> STRING {
    LET enhanced   = CALL(cpp,    image_processor::enhance,    input, 100);
    LET prediction = CALL(python, ml_model::predict,           enhanced);
    LET compressed = CALL(rust,   data_loader::compress,       prediction);
    LET report     = CALL(python, string_utils::format_result, compressed);
    RETURN report;
}
```

---

# 11. 面向对象互操作

`.ploy` 提供七个关键字支持跨语言面向对象操作：

| 关键字 | 作用 |
|--------|------|
| `NEW` | 创建外部语言的类实例 |
| `METHOD` | 调用实例的方法 |
| `GET` | 读取实例的属性 |
| `SET` | 设置实例的属性 |
| `WITH` | 自动资源管理（块结束自动析构） |
| `DELETE` | 手动释放实例 |
| `EXTEND` | 声明继承外部语言的类 |

## 11.1 NEW — 创建实例

```ploy
LET mat         = NEW(cpp,    matrix::Matrix,     3, 3, 1.0);
LET model       = NEW(python, model::LinearModel, 3, 1);
LET parser      = NEW(rust,   serde::json::Parser);
LET engine      = NEW(java,   SortEngine);
LET transformer = NEW(dotnet, Transformer);
```

`NEW` 返回一个**不透明对象句柄**（内部类型 `Any`），只能通过 `METHOD`/`GET`/`SET`/`DELETE` 操作。

## 11.2 METHOD — 调用方法

```ploy
METHOD(cpp, mat, set, 0, 0, 5.0);
LET val  = METHOD(cpp,    mat,   get,     0, 0);
LET norm = METHOD(cpp,    mat,   norm);
LET pred = METHOD(python, model, forward, [1.0, 2.0, 3.0]);
```

### 链式调用（PyTorch 训练循环）

```ploy
LET nn_model  = NEW(python, torch::nn::Linear, 784, 10);
LET optimizer = NEW(python, torch::optim::Adam,
                    METHOD(python, nn_model, parameters), 0.001);
METHOD(python, optimizer, zero_grad);
LET loss = METHOD(python, nn_model, compute_loss, predictions, labels);
METHOD(python, loss, backward);
METHOD(python, optimizer, step);
```

## 11.3 GET — 读取属性

```ploy
LET rows   = GET(cpp,    mat,   rows);
LET weight = GET(python, model, weight);
```

## 11.4 SET — 修改属性

```ploy
SET(cpp,    mat,   rows,          5);
SET(python, model, learning_rate, 0.001);
```

## 11.5 WITH — 自动资源管理

`WITH` 块结束后，无论是否出错，都自动释放资源：

```ploy
WITH conn = NEW(python, database::Connection, "postgresql://localhost/mydb") {
    LET result = METHOD(python, conn, query,    "SELECT * FROM users");
    LET users  = METHOD(python, conn, fetchall);
}
// conn 在 WITH 块结束后自动关闭

WITH file    = NEW(rust, std::fs::File,   "output.bin"),
     encoder = NEW(cpp,  codec::Encoder,  1920, 1080) {
    METHOD(rust, file, write, METHOD(cpp, encoder, encode, raw_data));
}
```

## 11.6 DELETE — 手动释放

```ploy
LET mat = NEW(cpp, matrix::Matrix, 100, 100, 0.0);
DELETE(cpp, mat);   // 调用 C++ 析构函数，释放内存
```

> **最佳实践**：优先用 `WITH`，只有需要跨代码块共享对象生命周期时才手动 `DELETE`。

## 11.7 EXTEND — 跨语言继承

```ploy
EXTEND python::torch.nn.Module AS MyModel {
    FUNC forward(x: LIST<FLOAT>) -> LIST<FLOAT> {
        LET hidden = CALL(cpp, activations::relu, x);
        RETURN CALL(python, layers::linear, hidden);
    }
}
```

`MyModel` 可被 Python 代码当作标准 `nn.Module` 使用，同时其 `forward` 由 `.ploy` 定义并调用 C++。

---

# 12. 管道

## 12.1 什么是 PIPELINE？

`PIPELINE` 是 `.ploy` 中用于**编排多阶段、跨语言工作流**的顶层声明。你可以把它理解为一个"命名的工作流容器"，里面包含多个 `FUNC`，每个 `FUNC` 代表工作流的一个阶段，各阶段可以使用不同的语言。

与独立 `FUNC` 的区别在于：
- `PIPELINE` 内部的函数逻辑上相关联；
- 整个 `PIPELINE` 可以作为一个整体 `EXPORT`；
- 明确表达了这些阶段是**有序协作**的意图。

## 12.2 PIPELINE 基本语法

```
PIPELINE <管道名> {
    FUNC <阶段1>(...) -> <类型> { ... }
    FUNC <阶段2>(...) -> <类型> { ... }
}
```

## 12.3 图像分类管道示例

```ploy
PIPELINE image_classification {

    // 第 1 阶段：C++ 预处理——高斯模糊 + 增强
    FUNC preprocess(input: LIST<FLOAT>, size: INT) -> LIST<FLOAT> {
        CALL(cpp, image_processor::gaussian_blur, input, size, 1.5);
        CALL(cpp, image_processor::enhance, input, size);
        RETURN input;
    }

    // 第 2 阶段：Python ML 分类
    FUNC classify(data: LIST<FLOAT>, threshold: FLOAT) -> INT {
        LET prediction = CALL(python, ml_model::predict, data);
        IF prediction > threshold {
            RETURN 1;
        } ELSE {
            RETURN 0;
        }
    }

    // 第 3 阶段：批处理
    FUNC process_batch(batch_size: INT) -> INT {
        VAR total_positive = 0;
        FOR i IN 0..batch_size {
            LET sample = [1.0, 2.0, 3.0];
            LET result = CALL(python, ml_model::classify, sample, 0.5);
            IF result == 1 {
                total_positive = total_positive + 1;
            }
        }
        RETURN total_positive;
    }

    // 第 4 阶段：模式分发
    FUNC dispatch(mode: INT, data: LIST<FLOAT>) -> FLOAT {
        MATCH mode {
            CASE 0 {
                RETURN CALL(python, ml_model::predict, data);
            }
            CASE 1 {
                CALL(cpp, image_processor::enhance, data, 100);
                RETURN CALL(python, ml_model::predict, data);
            }
            DEFAULT {
                RETURN 0.0;
            }
        }
    }
}
```

## 12.4 包含 OOP 的管道

```ploy
PIPELINE training_pipeline {
    FUNC train(epochs: INT) -> FLOAT {
        LET model  = NEW(python, model::LinearModel, 4, 2);
        LET loader = NEW(python, model::DataLoader,  data, 1);

        VAR total_loss = 0.0;
        VAR epoch = 0;

        WHILE epoch < epochs {
            LET batch = METHOD(python, loader, next_batch);
            LET mat   = NEW(cpp, matrix::Matrix, 1, 4, 0.0);
            LET loss  = METHOD(python, model, train_step,
                               [1.0, 2.0, 3.0, 4.0], [1.0, 0.0]);
            total_loss = total_loss + loss;
            epoch = epoch + 1;
        }

        METHOD(python, loader, reset);
        RETURN total_loss;
    }
}
```

---

# 13. 环境配置

## 13.1 为什么需要 CONFIG？

在跨语言项目里，Python/Java/.NET 等语言往往依赖特定的虚拟环境。`CONFIG` 用来声明"这种语言应该用哪个环境"，编译器和运行时会按照这里的配置初始化相应的运行时环境。

## 13.2 CONFIG 语法

```ploy
// venv（标准虚拟环境）
CONFIG VENV python "env/python";
CONFIG VENV python "/opt/ml-env";

// Conda 环境（指定环境名称）
CONFIG CONDA python "ml_env";

// uv 管理的环境
CONFIG UV python "D:/venvs/uv_env";

// Pipenv 项目（指向含 Pipfile 的目录）
CONFIG PIPENV python "C:/projects/myapp";

// Poetry 项目（指向含 pyproject.toml 的目录）
CONFIG POETRY python "C:/projects/poetry_app";

// .NET 环境
CONFIG VENV dotnet "env/dotnet";
```

### 重要规则

1. **每种语言只能有一个 CONFIG**：重复声明同一种语言会报编译错误。
2. **CONFIG 必须在 IMPORT PACKAGE 之前**：编译器解析 `IMPORT PACKAGE` 时会查找 CONFIG 声明的环境。
3. **路径可以是相对路径**：相对于 `.ploy` 文件所在目录。

## 13.3 完整配置示例

```ploy
// 第一步：声明环境（必须在 IMPORT PACKAGE 之前）
CONFIG VENV python "env/python";
CONFIG VENV dotnet "env/dotnet";

// 第二步：导入外部包
IMPORT python PACKAGE numpy >= 1.24 AS np;
IMPORT python PACKAGE pandas >= 2.0 AS pd;
IMPORT python PACKAGE scipy >= 1.11 AS sp;
IMPORT dotnet PACKAGE Newtonsoft.Json >= 13.0 AS njson;
```

---

# 14. 类型转换

## 14.1 自动转换 vs 显式转换

`MAP_TYPE` 声明的类型映射是**自动**发生的——编译器在调用点自动插入转换代码。

但有时自动映射不够用，比如：
- 需要在函数内部临时把一种语言的类型转换为另一种；
- 目标函数期望的类型没有对应的 `MAP_TYPE` 声明；
- 需要基本类型之间的数值转换（INT → FLOAT）。

这时就需要 `CONVERT` 显式转换。

## 14.2 CONVERT — 显式类型转换

```ploy
// CONVERT(<表达式>, <目标类型>)

// Python list → .NET List<double>
LET py_list    = CALL(python, data_science::random_matrix, 1, 5);
LET dotnet_arr = CONVERT(py_list, dotnet::List_double);

// .NET List<double> → Python list
LET py_flat = CONVERT(flat, python::list);

// INT → FLOAT
LET int_val: INT = 42;
LET float_val = CONVERT(int_val, FLOAT);
```

**何时用 CONVERT 而非 MAP_TYPE？**
- `MAP_TYPE`：声明两种类型在**跨语言调用边界**上的等价关系，是"全局规则"；
- `CONVERT`：在函数体内部做**临时的、点对点**的类型转换，是"局部操作"。

## 14.3 MAP_FUNC — 自定义转换函数

```ploy
MAP_FUNC convert_to_float(x: INT) -> FLOAT {
    LET result = CONVERT(x, FLOAT);
    RETURN result;
}

MAP_FUNC score_to_grade(score: INT) -> STRING {
    IF score >= 90 { RETURN "A"; }
    ELSE IF score >= 80 { RETURN "B"; }
    ELSE IF score >= 70 { RETURN "C"; }
    ELSE { RETURN "F"; }
}
```

---

# 15. 符号导出

## 15.1 为什么要 EXPORT？

`.ploy` 编译后会生成共享库或可执行文件。默认情况下，`.ploy` 内部定义的函数和管道是**内部符号**，不对外可见。`EXPORT` 让你明确控制哪些符号暴露给外部。

## 15.2 EXPORT 语法

```ploy
// 用原始名称导出
EXPORT compute;
EXPORT my_pipeline;

// 用别名导出（遵循 C ABI 命名约定）
EXPORT compute_and_format AS "polyglot_compute";
EXPORT distance_report    AS "polyglot_distance";
EXPORT image_classification AS "image_pipeline";
```

## 15.3 PIPELINE 的导出

导出整个管道时，管道内所有 `FUNC` 都会被导出：

```ploy
PIPELINE image_classification {
    FUNC preprocess(...) { ... }
    FUNC classify(...) { ... }
}

EXPORT image_classification AS "img_pipe";
// 外部可以调用 img_pipe.preprocess 和 img_pipe.classify
```

---

# 16. 错误处理与诊断

`.ploy` 编译器会在**编译期**捕获大多数错误，并给出清晰的诊断信息。

## 16.1 错误信息格式

每条编译器错误信息包括：
- **错误码**（如 `E3010`）：可用于查阅文档；
- **错误描述**：说明出了什么问题；
- **位置**（文件名:行号:列号）：准确指向出错位置；
- **建议**（suggestion）：告诉你怎么修复。

## 16.2 常见错误：参数数量不匹配

```ploy
FUNC param_count_error() -> FLOAT {
    LET result = CALL(cpp, bad_functions::compute, 42); // ❌ 只传了 1 个参数
    RETURN result;
}
```

```
Error [E3010]: Parameter count mismatch in call to 'compute'
  --> error_handling.ploy:44:9
   | Expected 3 argument(s), got 1
   = suggestion: Check the function signature for 'compute'
```

## 16.3 常见错误：类型不匹配

```ploy
FUNC type_mismatch_error() -> FLOAT {
    LET result = CALL(python, bad_functions::process, "hello", 42); // ❌ 类型错误
    RETURN result;
}
```

```
Error [E3011]: Type mismatch for parameter 1 in call to 'process'
  --> error_handling.ploy:58:9
   | Expected 'INT', got 'STRING'
   = suggestion: Consider using CONVERT to convert the argument type
```

## 16.4 常见错误：未导入的模块

```ploy
LET x = CALL(cpp, nonexistent::foo, 1);  // ❌ nonexistent 未被导入
```

## 16.5 常见错误：重复 CONFIG

```ploy
CONFIG VENV python "env1";
CONFIG VENV python "env2";   // ❌ python 的 CONFIG 重复定义
```

## 16.6 常见错误：LET 变量被重新赋值

```ploy
LET x = 10;
x = 20;   // ❌ LET 变量不可修改，应使用 VAR
```

> **参见**：[示例 10: error_handling](../../tests/samples/10_error_handling/)

---

# 17. 完整示例

## 17.1 基本跨语言链接

> 源码：[示例 01: basic_linking](../../tests/samples/01_basic_linking/)

```ploy
IMPORT cpp::math_ops;
IMPORT python::string_utils;

LINK(cpp, python, math_ops::add, string_utils::format_result) {
    MAP_TYPE(cpp::int, python::int);
}

MAP_TYPE(cpp::int, python::int);
MAP_TYPE(cpp::double, python::float);

FUNC compute_and_format(a: INT, b: INT) -> STRING {
    LET sum       = CALL(cpp,    math_ops::add,               a, b);
    LET formatted = CALL(python, string_utils::format_result, sum);
    RETURN formatted;
}

EXPORT compute_and_format AS "polyglot_compute";
```

**运行逻辑**：
1. 调用 C++ 的 `math_ops::add(a, b)` 得到整数结果；
2. 桥接代码把 C++ `int` 转换为 Python `int`；
3. 调用 Python 的 `format_result` 得到字符串；
4. 返回字符串。

## 17.2 ML 训练管道

> 源码：[示例 05: class_instantiation](../../tests/samples/05_class_instantiation/)

```ploy
IMPORT cpp::matrix;
IMPORT python::model;

LINK(cpp, python, matrix::Matrix, model::LinearModel) {
    MAP_TYPE(cpp::double, python::float);
    MAP_TYPE(cpp::int,    python::int);
}

PIPELINE training_pipeline {
    FUNC train(epochs: INT) -> FLOAT {
        LET model  = NEW(python, model::LinearModel, 4, 2);
        LET loader = NEW(python, model::DataLoader,  data, 1);

        VAR total_loss = 0.0;
        VAR epoch = 0;

        WHILE epoch < epochs {
            LET batch = METHOD(python, loader, next_batch);
            LET mat   = NEW(cpp, matrix::Matrix, 1, 4, 0.0);
            LET loss  = METHOD(python, model, train_step,
                               [1.0, 2.0, 3.0, 4.0], [1.0, 0.0]);
            total_loss = total_loss + loss;
            epoch = epoch + 1;
        }

        METHOD(python, loader, reset);
        RETURN total_loss;
    }
}

EXPORT training_pipeline AS "train_pipeline";
```

## 17.3 五语言全栈

> 源码：[示例 15: full_stack](../../tests/samples/15_full_stack/)

此示例展示了所有五种支持的语言在单个管道中协同工作：
- **C++**：高性能图像/矩阵处理；
- **Python**：机器学习推理（PyTorch/NumPy）；
- **Rust**：高效数据加载和压缩；
- **Java**：排序和集合处理；
- **.NET (C#)**：JSON 序列化/反序列化。

## 17.4 环境配置与包管理

> 源码：[示例 16: config_and_venv](../../tests/samples/16_config_and_venv/)

```ploy
CONFIG VENV python "env/python";
CONFIG VENV dotnet "env/dotnet";

IMPORT python PACKAGE numpy >= 1.24 AS np;
IMPORT python PACKAGE pandas >= 2.0 AS pd;
IMPORT dotnet PACKAGE Newtonsoft.Json >= 13.0 AS njson;

IMPORT dotnet::Transformer;
IMPORT python::data_science;

FUNC json_round_trip() -> STRING {
    LET json_str    = CALL(python,  data_science::to_json,       "ResNet-50");
    LET transformer = NEW(dotnet,   Transformer);
    LET parsed      = METHOD(dotnet, transformer, ParseJson,     json_str);
    LET enriched    = METHOD(dotnet, transformer, Enrich,        parsed, "validated", "true");
    LET back        = METHOD(dotnet, transformer, ToJsonString,  enriched);
    RETURN back;
}

FUNC explicit_conversion() -> FLOAT {
    LET py_list     = CALL(python,  data_science::random_matrix, 1, 5);
    LET dotnet_arr  = CONVERT(py_list, dotnet::List_double);
    LET transformer = NEW(dotnet,   Transformer);
    LET flat        = METHOD(dotnet, transformer, Flatten,       dotnet_arr);
    LET py_flat     = CONVERT(flat, python::list);
    LET avg         = CALL(python,  data_science::mean,          py_flat);
    RETURN avg;
}

EXPORT json_round_trip;
EXPORT explicit_conversion;
```

---

# 18. 关键字参考

`.ploy` 共有 **54 个关键字**，按功能分类如下：

## 18.1 声明类关键字

| 关键字 | 用途 | 示例 |
|--------|------|------|
| `LINK` | 声明跨语言函数链接，生成桥接代码 | `LINK(cpp, python, f1, f2) { }` |
| `IMPORT` | 导入模块或外部包 | `IMPORT cpp::math;` |
| `EXPORT` | 导出符号供外部访问 | `EXPORT f AS "name";` |
| `MAP_TYPE` | 声明两种语言类型的等价关系 | `MAP_TYPE(cpp::int, python::int);` |
| `PIPELINE` | 声明多阶段跨语言工作流 | `PIPELINE p { FUNC ... }` |
| `FUNC` | 定义函数 | `FUNC f(x: INT) -> INT { }` |
| `CONFIG` | 配置语言运行时环境 | `CONFIG VENV python "env";` |

## 18.2 变量类关键字

| 关键字 | 用途 | 示例 |
|--------|------|------|
| `LET` | 声明不可变变量 | `LET x = 42;` |
| `VAR` | 声明可变变量 | `VAR count = 0;` |

## 18.3 类型关键字

| 关键字 | 对应类型 |
|--------|---------|
| `VOID` | 无返回值 |
| `INT` | 整数（对应各语言的 int/i32） |
| `FLOAT` | 浮点数（对应 double/f64） |
| `STRING` | 字符串（对应 std::string/str） |
| `BOOL` | 布尔值 |
| `ARRAY` | 固定长度数组 |
| `LIST` | 动态列表 |
| `TUPLE` | 元组 |
| `DICT` | 字典/哈希表 |
| `OPTION` | 可选类型（可能为空） |

## 18.4 控制流关键字

| 关键字 | 用途 |
|--------|------|
| `RETURN` | 从函数返回值 |
| `IF` | 条件分支开始 |
| `ELSE` | IF 的替代分支 |
| `WHILE` | 条件循环 |
| `FOR` | 迭代循环 |
| `IN` | 配合 FOR 使用（`FOR x IN ...`） |
| `MATCH` | 值匹配分发 |
| `CASE` | MATCH 的一个分支 |
| `DEFAULT` | MATCH 的默认分支 |
| `BREAK` | 退出循环 |
| `CONTINUE` | 跳到下一次迭代 |

## 18.5 运算符关键字

| 关键字 | 用途 |
|--------|------|
| `AS` | 起别名（EXPORT/IMPORT 中） |
| `AND` | 逻辑与 |
| `OR` | 逻辑或 |
| `NOT` | 逻辑非 |
| `CALL` | 跨语言函数调用 |
| `CONVERT` | 显式类型转换 |
| `MAP_FUNC` | 定义自定义类型转换函数 |

## 18.6 OOP 关键字

| 关键字 | 用途 |
|--------|------|
| `NEW` | 创建外部语言类实例 |
| `METHOD` | 调用实例方法 |
| `GET` | 读取实例属性 |
| `SET` | 设置实例属性 |
| `WITH` | 自动资源管理（自动析构） |
| `DELETE` | 手动释放实例 |
| `EXTEND` | 声明继承外部语言类 |

## 18.7 值关键字

| 关键字 | 含义 |
|--------|------|
| `TRUE` | 布尔真 |
| `FALSE` | 布尔假 |
| `NULL` | 空值（用于 OPTION 类型） |

## 18.8 包管理关键字

| 关键字 | 用途 |
|--------|------|
| `PACKAGE` | 外部包导入（与 IMPORT 配合） |
| `VENV` | 配置 venv/virtualenv 环境 |
| `CONDA` | 配置 Conda 环境 |
| `UV` | 配置 uv 管理的环境 |
| `PIPENV` | 配置 Pipenv 项目 |
| `POETRY` | 配置 Poetry 项目 |

---

# 19. 最佳实践

## 19.1 文件组织顺序

```
CONFIG（环境配置）
  ↓
IMPORT PACKAGE（包导入）
  ↓
IMPORT <lang>::<module>（模块导入）
  ↓
MAP_TYPE（全局类型映射）
  ↓
LINK（跨语言链接声明）
  ↓
STRUCT（数据结构定义）
  ↓
FUNC / PIPELINE（核心逻辑）
  ↓
EXPORT（对外暴露）
```

## 19.2 变量使用原则

- **优先用 `LET`**：`CALL` 的返回值只需绑定一次，用 `LET` 防止意外修改；
- **只有需要修改时才用 `VAR`**：如循环计数器、累加器等。

## 19.3 跨语言调用原则

1. **为每个语言边界声明类型映射**：缺少 `MAP_TYPE` 会导致编译错误；
2. **函数调用用 `CALL`，方法调用用 `METHOD`**：二者不能互换；
3. **自动映射不够时用 `CONVERT`**：不要用不当的 `MAP_TYPE` 强行绕过类型检查。

## 19.4 对象生命周期管理

1. **优先用 `WITH`**：对于需要关闭/释放的资源，`WITH` 保证即使出错也正确释放；
2. **不用 `WITH` 时，记得 `DELETE`**：忘记 `DELETE` 会导致外部对象泄漏；
3. **`DELETE` 后不要再访问对象**：编译器不能检测悬空句柄，运行时会崩溃。

## 19.5 语言选择策略

| 任务类型 | 推荐语言 | 原因 |
|----------|---------|------|
| 高性能数值计算 | C++ / Rust | 无 GC，内存直接控制 |
| 机器学习推理 | Python | PyTorch/TF 等框架 API 最完善 |
| 数据加载/文件 IO | Rust | 性能好，内存安全 |
| 企业应用逻辑 | Java / .NET | 生态完善，易于集成 |
| JSON/序列化 | .NET / Python | 库支持丰富 |
| 排序/集合操作 | Java | Collections Framework 成熟 |

## 19.6 管道设计原则

1. **每个阶段（FUNC）只做一件事**：便于测试、替换和复用；
2. **阶段之间用标准 `.ploy` 类型传递数据**；
3. **命名要表达意图**：`preprocess`、`classify`、`postprocess` 远比 `stage1`、`func_a` 更易读。

## 19.7 示例程序索引

| 示例 | 主要演示内容 |
|------|------------|
| [01_basic_linking](../../tests/samples/01_basic_linking/) | LINK、CALL、IMPORT、EXPORT 基础用法 |
| [02_type_mapping](../../tests/samples/02_type_mapping/) | MAP_TYPE 与复杂类型及 STRUCT |
| [03_pipeline](../../tests/samples/03_pipeline/) | PIPELINE + IF/WHILE/FOR/MATCH |
| [04_package_import](../../tests/samples/04_package_import/) | IMPORT PACKAGE + 版本约束 |
| [05_class_instantiation](../../tests/samples/05_class_instantiation/) | NEW + METHOD OOP 模式 |
| [06_attribute_access](../../tests/samples/06_attribute_access/) | GET 和 SET |
| [07_resource_management](../../tests/samples/07_resource_management/) | WITH 自动资源管理 |
| [08_delete_extend](../../tests/samples/08_delete_extend/) | DELETE 和 EXTEND |
| [09_mixed_pipeline](../../tests/samples/09_mixed_pipeline/) | 所有特性组合的 ML 管道 |
| [10_error_handling](../../tests/samples/10_error_handling/) | 错误场景和诊断信息 |
| [11_java_interop](../../tests/samples/11_java_interop/) | Java 互操作 |
| [12_dotnet_interop](../../tests/samples/12_dotnet_interop/) | .NET 互操作 |
| [13_generic_containers](../../tests/samples/13_generic_containers/) | 泛型容器类型 |
| [14_async_pipeline](../../tests/samples/14_async_pipeline/) | 多阶段信号处理管道 |
| [15_full_stack](../../tests/samples/15_full_stack/) | 五语言全栈演示 |
| [16_config_and_venv](../../tests/samples/16_config_and_venv/) | CONFIG + IMPORT PACKAGE + CONVERT |
"""

path.write_text(content, encoding='utf-8')
print(f"Done. Lines: {content.count(chr(10))}, Replacement chars: {content.count(chr(0xFFFD))}")
