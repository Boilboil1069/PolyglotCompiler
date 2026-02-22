# .ploy 语言教程

> **版本**: 2.0.0
> **最后更新**: 2026-02-22
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

`.ploy` 是 PolyglotCompiler 项目专用的**领域特定语言**（DSL）。它提供了声明式语法，用于表达不同编程语言之间的**函数级跨语言链接**和**面向对象互操作**，支持 C++、Python、Rust、Java 和 C#（.NET）。

与通用编程语言不同，`.ploy` 充当**"跨语言胶水"**——它描述函数、类和数据如何在不同语言运行时之间流转。

## 1.2 .ploy 能做什么？

- **链接函数** — 将不同语言的函数绑定在一起（例如从 Python 调用 C++ 函数）
- **实例化类** — 在一种语言中创建另一种语言的类实例
- **访问和修改属性** — 读写外部对象的属性
- **自动资源管理** — 跨语言边界的自动资源清理
- **编排多阶段管道** — 将跨多种语言运行时的工作流组织为可复用的管道
- **管理包依赖** — 支持 pip/conda/uv/pipenv/poetry/cargo/NuGet 等
- **类型映射** — 在不同语言的类型系统之间进行转换

## 1.3 支持的语言

| 语言 | 前端 | 标识符 |
|------|------|--------|
| C++ | `frontend_cpp` | `cpp` |
| Python | `frontend_python` | `python` |
| Rust | `frontend_rust` | `rust` |
| Java | `frontend_java` | `java` |
| C#（.NET）| `frontend_dotnet` | `dotnet` |

## 1.4 文件扩展名

所有 `.ploy` 源文件使用 `.ploy` 扩展名：

```
my_project.ploy
pipeline.ploy
config_and_venv.ploy
```
-
---

# 2. 快速入门

## 2.1 第一个 .ploy 文件

创建一个名为 `hello.ploy` 的文件：

```ploy
// hello.ploy — 一个最小的跨语言示例

// 从 C++ 和 Python 导入模块
IMPORT cpp::math_ops;
IMPORT python::string_utils;

// 将 C++ 函数链接到 Python 函数
LINK(cpp, python, math_ops::add, string_utils::format_result) {
    MAP_TYPE(cpp::int, python::int);
}

// 定义一个跨语言函数
FUNC greet(a: INT, b: INT) -> STRING {
    LET sum = CALL(cpp, math_ops::add, a, b);
    LET message = CALL(python, string_utils::format_result, sum);
    RETURN message;
}

// 导出函数供外部使用
EXPORT greet AS "polyglot_greet";
```

## 2.2 编译

使用 `polyc` 驱动程序编译 `.ploy` 文件：

```bash
polyc hello.ploy -o hello
```

驱动程序自动检测 `.ploy` 扩展名，并将文件路由到 .ploy 前端（词法分析 → 语法分析 → 语义分析 → IR 生成）。

## 2.3 基本程序结构

典型的 `.ploy` 文件按以下顺序组织：

```ploy
// 1. 环境配置（可选）
CONFIG VENV python "/path/to/venv";

// 2. 包导入（可选）
IMPORT python PACKAGE numpy >= 1.20 AS np;

// 3. 模块导入
IMPORT cpp::module_name;
IMPORT python::module_name;

// 4. 跨语言链接
LINK(cpp, python, func_a, func_b) {
    MAP_TYPE(cpp::int, python::int);
}

// 5. 类型映射
MAP_TYPE(cpp::double, python::float);

// 6. 结构体定义（可选）
STRUCT Config {
    width: INT;
    height: INT;
}

// 7. 函数和管道
FUNC main_func() -> INT { ... }
PIPELINE my_pipeline { ... }

// 8. 导出
EXPORT main_func;
EXPORT my_pipeline;
```

## 2.4 注释

`.ploy` 支持以 `//` 开头的单行注释：

```ploy
// 这是一条注释
FUNC foo() -> INT {
    LET x = 42;  // 行内注释
    RETURN x;
}
```

## 2.5 语句终止

所有语句以分号（`;`）结尾：

```ploy
LET x = 10;
VAR y: FLOAT = 3.14;
RETURN x;
```

---

# 3. 模块与包导入

## 3.1 模块导入 — IMPORT

使用 `IMPORT` 将其他语言的模块引入作用域：

```ploy
// 限定模块导入：<语言>::<模块名>
IMPORT cpp::math_utils;
IMPORT python::data_processing;
IMPORT rust::serde;
IMPORT java::SortEngine;
IMPORT dotnet::Transformer;
```

这些模块对应项目中的实际源文件（例如 `math_utils.cpp`、`data_processing.py`）。

## 3.2 包导入 — IMPORT PACKAGE

从各语言的包生态系统中导入外部包：

```ploy
// 基本包导入
IMPORT python PACKAGE numpy;

// 带版本约束
IMPORT python PACKAGE numpy >= 1.20;
IMPORT python PACKAGE scipy == 1.11;
IMPORT rust PACKAGE serde >= 1.0;

// 带别名
IMPORT python PACKAGE numpy >= 1.20 AS np;
IMPORT python PACKAGE pandas >= 2.0 AS pd;

// 选择性导入（指定符号）
IMPORT python PACKAGE numpy::(array, mean, std);
IMPORT python PACKAGE torch::(tensor, no_grad) >= 2.0;

// 子模块选择性导入
IMPORT python PACKAGE numpy.linalg::(solve, inv);
```

### 版本运算符

| 运算符 | 含义 | 示例 |
|--------|------|------|
| `>=` | 大于或等于 | `>= 1.20` |
| `<=` | 小于或等于 | `<= 2.0` |
| `==` | 精确匹配 | `== 1.10.0` |
| `>` | 严格大于 | `> 1.0` |
| `<` | 严格小于 | `< 3.0` |
| `!=` | 不等于 | `!= 2.0` |
| `~=` | 兼容版本（PEP 440）| `~= 1.20` |

### 规则

1. 选择性导入 `::()` 和 `AS` 别名**不能同时使用**。
2. 版本约束是可选的。
3. 每个编译单元中，每种语言最多一个 CONFIG。

---

# 4. 跨语言函数链接

## 4.1 LINK — 声明跨语言绑定

`LINK` 指令告诉 PolyglotLinker 生成**胶水代码**，在两种语言之间桥接函数调用：

```ploy
// 基本 LINK 语法
LINK(source_lang, target_lang, source_function, target_function) {
    MAP_TYPE(source_lang::type, target_lang::type);
}
```

### 示例：链接 C++ 到 Python

```ploy
// 将 C++ 数学输出链接到 Python 格式化
LINK(cpp, python, math_ops::add, string_utils::format_result) {
    MAP_TYPE(cpp::int, python::int);
}
```

### 示例：链接 Java 到 Python

```ploy
LINK(java, python, SortEngine::sortedKeys, collection_utils::invert_dict) {
    MAP_TYPE(java::ArrayList_String, python::list);
    MAP_TYPE(java::HashMap_String_Integer, python::dict);
}
```

### 示例：链接 Python 到 .NET

```ploy
LINK(python, dotnet, data_science::to_json, Transformer::ParseJson) {
    MAP_TYPE(python::str, dotnet::string);
}
```

## 4.2 LINK 体

LINK 块的主体包含 `MAP_TYPE` 声明，指定在边界处如何进行类型转换：

```ploy
LINK(cpp, rust, image_processor::edge_detect, data_loader::merge_chunks) {
    MAP_TYPE(cpp::std::vector_double, rust::Vec_f64);
    MAP_TYPE(cpp::int, rust::usize);
}
```

---

# 5. 类型映射

## 5.1 MAP_TYPE — 声明类型等价关系

`MAP_TYPE` 告诉编译器如何在特定语言类型之间进行数据编排：

```ploy
// 基本类型映射
MAP_TYPE(cpp::int, python::int);
MAP_TYPE(cpp::double, python::float);
MAP_TYPE(cpp::std::string, python::str);

// 容器类型映射
MAP_TYPE(cpp::std::vector_int, java::ArrayList_Integer);
MAP_TYPE(java::HashMap_String_Integer, python::dict);
MAP_TYPE(rust::Vec_f64, python::list);
MAP_TYPE(dotnet::List_double, python::list);
```

## 5.2 基本类型映射

.ploy 类型系统提供平台无关的基本类型：

| .ploy 类型 | C++ | Python | Rust | Java | C# |
|-----------|-----|--------|------|------|-----|
| `INT` | `int` | `int` | `i32` | `int` | `int` |
| `FLOAT` | `double` | `float` | `f64` | `double` | `double` |
| `STRING` | `std::string` | `str` | `String` | `String` | `string` |
| `BOOL` | `bool` | `bool` | `bool` | `boolean` | `bool` |
| `VOID` | `void` | `None` | `()` | `void` | `void` |

## 5.3 容器类型映射

| .ploy 类型 | C++ | Python | Rust |
|-----------|-----|--------|------|
| `LIST<T>` | `std::vector<T>` | `list[T]` | `Vec<T>` |
| `TUPLE<T...>` | `std::tuple<T...>` | `tuple` | `(T...)` |
| `DICT<K,V>` | `std::unordered_map<K,V>` | `dict[K,V]` | `HashMap<K,V>` |
| `ARRAY<T,N>` | `T[N]` | `list[T]` | `[T; N]` |
| `OPTION<T>` | `std::optional<T>` | `Optional[T]` | `Option<T>` |

---

# 6. 变量与表达式

## 6.1 LET — 不可变变量

`LET` 声明一个**不可变**变量，其值不能被重新赋值：

```ploy
LET x = 42;
LET name: STRING = "PolyglotCompiler";
LET pi: FLOAT = 3.14159;
LET flag: BOOL = TRUE;
```

## 6.2 VAR — 可变变量

`VAR` 声明一个**可变**变量，其值可以被重新赋值：

```ploy
VAR counter = 0;
VAR total: FLOAT = 0.0;

counter = counter + 1;   // 允许重新赋值
total = total + 3.14;
```

## 6.3 表达式

### 算术运算符

| 运算符 | 含义 | 示例 |
|--------|------|------|
| `+` | 加法 | `a + b` |
| `-` | 减法 | `a - b` |
| `*` | 乘法 | `a * b` |
| `/` | 除法 | `a / b` |
| `%` | 取模 | `a % b` |

### 比较运算符

| 运算符 | 含义 | 示例 |
|--------|------|------|
| `==` | 等于 | `a == b` |
| `!=` | 不等于 | `a != b` |
| `<` | 小于 | `a < b` |
| `>` | 大于 | `a > b` |
| `<=` | 小于等于 | `a <= b` |
| `>=` | 大于等于 | `a >= b` |

### 逻辑运算符

| 运算符 | 含义 | 示例 |
|--------|------|------|
| `AND` | 逻辑与 | `a AND b` |
| `OR` | 逻辑或 | `a OR b` |
| `NOT` | 逻辑非 | `NOT a` |

### 字面量

```ploy
// 整数字面量
LET a = 42;
LET b = -10;

// 浮点字面量
LET c = 3.14;
LET d = -0.5;

// 字符串字面量
LET s = "Hello, World!";

// 布尔字面量
LET t = TRUE;
LET f = FALSE;

// 空值
LET n = NULL;

// 容器字面量
LET list = [1, 2, 3];
LET tuple = (1, "hello", 3.14);
LET dict = {"key1": 10, "key2": 20};
```

---

# 7. 函数

## 7.1 FUNC — 函数声明

使用类型化参数和返回类型定义函数：

```ploy
FUNC add(a: INT, b: INT) -> INT {
    RETURN a + b;
}

FUNC greet(name: STRING) -> STRING {
    LET message = "Hello, " + name;
    RETURN message;
}

FUNC process(data: ARRAY[FLOAT], size: INT) -> FLOAT {
    LET result = CALL(cpp, math_ops::sum, data, size);
    RETURN result;
}
```

## 7.2 RETURN

每个非 void 函数必须使用 `RETURN` 返回值：

```ploy
FUNC compute(x: INT) -> INT {
    IF x > 0 {
        RETURN x * 2;
    } ELSE {
        RETURN 0;
    }
}
```

## 7.3 Void 函数

不返回值的函数使用 `VOID` 作为返回类型：

```ploy
FUNC log_message(msg: STRING) -> VOID {
    CALL(python, logger::info, msg);
}
```

---

# 8. 控制流

## 8.1 IF / ELSE

条件分支：

```ploy
IF prediction > threshold {
    RETURN 1;
} ELSE {
    RETURN 0;
}
```

使用 `ELSE IF`：

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

## 8.2 WHILE — 循环

```ploy
VAR i = 0;
WHILE i < 10 {
    CALL(python, process::step, i);
    i = i + 1;
}
```

## 8.3 FOR — 迭代

遍历范围或集合：

```ploy
// 基于范围的循环
FOR i IN 0..10 {
    CALL(cpp, engine::tick, i);
}

// 基于集合的循环
FOR item IN collection {
    CALL(python, processor::handle, item);
}
```

## 8.4 MATCH — 模式匹配

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

## 8.5 BREAK 和 CONTINUE

控制循环执行：

```ploy
VAR current = CALL(python, ml_model::predict, data);
VAR iteration = 0;

WHILE iteration < max_iter {
    // 如果达到目标则提前退出
    IF current > target {
        BREAK;
    }

    CALL(cpp, image_processor::enhance, data, 100);
    current = CALL(python, ml_model::predict, data);
    iteration = iteration + 1;
}
```

```ploy
FOR i IN 0..100 {
    IF i % 2 == 0 {
        CONTINUE;  // 跳过偶数
    }
    CALL(python, process::handle_odd, i);
}
```

---

# 9. 结构体与自定义类型

## 9.1 STRUCT — 结构体定义

定义复合数据类型：

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

## 9.2 结构体字面量

使用字面量语法创建结构体实例：

```ploy
LET config = PipelineConfig {
    width: 1920,
    height: 1080,
    channels: 3,
    learning_rate: 0.001,
    epochs: 100
};
```

## 9.3 成员访问

使用点运算符访问结构体字段：

```ploy
LET w = config.width;
LET lr = config.learning_rate;
```

---

# 10. 跨语言调用

## 10.1 CALL — 调用其他语言的函数

`CALL` 关键字从特定语言运行时执行函数：

```ploy
// 语法：CALL(语言, 模块::函数, 参数1, 参数2, ...)
LET sum = CALL(cpp, math_ops::add, 10, 20);
LET formatted = CALL(python, string_utils::format_result, sum);
LET data = CALL(rust, data_loader::load_batch, "data.csv", 64);
LET sorted = CALL(java, SortEngine::sortInts, numbers);
```

### 跨语言调用链

可以链式调用不同语言的函数，将一种语言的输出作为另一种语言的输入：

```ploy
FUNC process_data(input: ARRAY[FLOAT]) -> STRING {
    // 第 1 步：C++ 预处理
    LET enhanced = CALL(cpp, image_processor::enhance, input, 100);
    
    // 第 2 步：Python ML 推理
    LET prediction = CALL(python, ml_model::predict, enhanced);
    
    // 第 3 步：Rust 后处理
    LET compressed = CALL(rust, data_loader::compress, prediction);
    
    // 第 4 步：Python 格式化
    LET report = CALL(python, string_utils::format_result, compressed);
    
    RETURN report;
}
```

---

# 11. 面向对象互操作

`.ploy` 提供七个关键字实现完整的跨语言面向对象互操作：`NEW`、`METHOD`、`GET`、`SET`、`WITH`、`DELETE` 和 `EXTEND`。

## 11.1 NEW — 类实例化

从任何受支持的语言创建类实例：

```ploy
// 语法：NEW(语言, 模块::类名, 参数1, 参数2, ...)
LET mat = NEW(cpp, matrix::Matrix, 3, 3, 1.0);
LET model = NEW(python, model::LinearModel, 3, 1);
LET parser = NEW(rust, serde::json::Parser);
LET engine = NEW(java, SortEngine);
LET transformer = NEW(dotnet, Transformer);
```

返回的值是一个**不透明句柄**（类型为 `Any`），可以传递给 `METHOD`、`GET`、`SET`、`WITH` 和 `DELETE`。

## 11.2 METHOD — 方法调用

调用外部对象的方法：

```ploy
// 语法：METHOD(语言, 对象, 方法名, 参数1, 参数2, ...)
METHOD(cpp, mat, set, 0, 0, 5.0);
METHOD(cpp, mat, set, 1, 1, 10.0);
LET val = METHOD(cpp, mat, get, 0, 0);
LET norm = METHOD(cpp, mat, norm);

LET prediction = METHOD(python, model, forward, [1.0, 2.0, 3.0]);
LET params = METHOD(python, model, parameters);
```

### 链式方法调用

```ploy
LET model = NEW(python, torch::nn::Linear, 784, 10);
LET optimizer = NEW(python, torch::optim::Adam,
                    METHOD(python, model, parameters), 0.001);
METHOD(python, optimizer, zero_grad);
LET loss = METHOD(python, model, compute_loss, predictions, labels);
METHOD(python, loss, backward);
METHOD(python, optimizer, step);
```

## 11.3 GET — 读取属性

从外部对象读取属性：

```ploy
// 语法：GET(语言, 对象, 属性名)
LET app = GET(python, settings_obj, app_name);
LET debug = GET(python, settings_obj, debug);
LET val = GET(cpp, sensor, value);
LET active = GET(cpp, sensor, active);
```

## 11.4 SET — 写入属性

向外部对象的属性写入值：

```ploy
// 语法：SET(语言, 对象, 属性名, 值)
SET(python, counter, count, 100);
SET(python, counter, step, 5);
SET(cpp, sensor, value, 99.9);
SET(cpp, sensor, active, FALSE);
```

## 11.5 WITH — 资源管理

自动资源管理，类似于 Python 的 `with` 语句：

```ploy
// 语法：WITH(语言, 资源表达式) AS 变量 { 主体 }
LET f = NEW(python, open, "data.csv");
WITH(python, f) AS handle {
    LET data = METHOD(python, handle, read);
    LET lines = METHOD(python, data, splitlines);
}
// handle 在块结束后自动关闭
```

## 11.6 DELETE — 对象销毁

显式销毁外部对象：

```ploy
// 语法：DELETE(语言, 对象)
LET obj = NEW(cpp, engine::GameObject, "Player", 0.0, 0.0, 0.0);
LET renderer = NEW(cpp, engine::Renderer, 1920, 1080, "OpenGL");

// 使用对象 ...
METHOD(cpp, obj, move, 10.0, 5.0, 0.0);

// 显式销毁
DELETE(cpp, renderer);  // 调用 C++ 析构函数
DELETE(cpp, obj);
DELETE(python, body);   // 释放 Python 引用
```

## 11.7 EXTEND — 类扩展

扩展来自另一种语言的类：

```ploy
// 语法：EXTEND(语言, 基类) { 主体 }
EXTEND(python, base_classes::Component) {
    FUNC custom_update(dt: FLOAT) -> VOID {
        CALL(python, base_classes::Component::update, dt);
    }
}
```

---

# 12. 管道

## 12.1 PIPELINE — 多阶段工作流

管道将多阶段、跨语言的工作流组织为单个可复用的单元：

```ploy
PIPELINE image_classification {

    // 第 1 阶段：C++ 预处理
    FUNC preprocess(input: ARRAY[FLOAT], size: INT) -> ARRAY[FLOAT] {
        CALL(cpp, image_processor::gaussian_blur, input, size, 1.5);
        CALL(cpp, image_processor::enhance, input, size);
        RETURN input;
    }

    // 第 2 阶段：Python 分类
    FUNC classify(data: ARRAY[FLOAT], threshold: FLOAT) -> INT {
        LET prediction = CALL(python, ml_model::predict, data);
        IF prediction > threshold {
            RETURN 1;
        } ELSE {
            RETURN 0;
        }
    }

    // 第 3 阶段：FOR 循环批处理
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

    // 第 4 阶段：MATCH 分发
    FUNC dispatch(mode: INT, data: ARRAY[FLOAT]) -> FLOAT {
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

## 12.2 带 OOP 的管道

管道可以使用 `NEW`、`METHOD` 及所有 OOP 特性：

```ploy
PIPELINE training_pipeline {
    FUNC train(epochs: INT) -> FLOAT {
        LET model = NEW(python, model::LinearModel, 4, 2);
        LET loader = NEW(python, model::DataLoader, data, 1);

        VAR total_loss = 0.0;
        VAR epoch = 0;

        WHILE epoch < epochs {
            LET batch = METHOD(python, loader, next_batch);
            LET mat = NEW(cpp, matrix::Matrix, 1, 4, 0.0);
            LET loss = METHOD(python, model, train_step,
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

## 13.1 CONFIG — 包管理器设置

配置用于包解析的包管理器环境：

```ploy
// Python 虚拟环境（venv）
CONFIG VENV python "env/python";
CONFIG VENV python "/opt/ml-env";

// Conda 环境
CONFIG CONDA python "ml_env";

// uv 管理的环境
CONFIG UV python "D:/venvs/uv_env";

// Pipenv 项目
CONFIG PIPENV python "C:/projects/myapp";

// Poetry 项目
CONFIG POETRY python "C:/projects/poetry_app";

// .NET 环境
CONFIG VENV dotnet "env/dotnet";
```

### 规则

1. 每个编译单元中，**每种语言一个 CONFIG** — 重复将导致编译时错误。
2. **CONFIG 必须在 IMPORT PACKAGE 之前出现** — 即在依赖它的包导入之前。
3. 语言参数可选，默认为 `python`。

## 13.2 实际示例

```ploy
// 设置 Python 环境
CONFIG VENV python "env/python";

// 从配置的环境中导入版本化的包
IMPORT python PACKAGE numpy >= 1.24 AS np;
IMPORT python PACKAGE pandas >= 2.0 AS pd;
IMPORT python PACKAGE scipy >= 1.11 AS sp;

// 设置 .NET 环境
CONFIG VENV dotnet "env/dotnet";

// 导入 .NET 包
IMPORT dotnet PACKAGE Newtonsoft.Json >= 13.0 AS njson;
```

> 参见 [示例 16: config_and_venv](../../tests/samples/16_config_and_venv/) 获取完整示例。

---

# 14. 类型转换

## 14.1 CONVERT — 显式类型编排

当自动类型映射不足时，使用 `CONVERT` 显式编排不同语言类型之间的数据：

```ploy
// 语法：CONVERT(表达式, 目标类型)

// 将 Python list 转换为 .NET List<double>
LET py_list = CALL(python, data_science::random_matrix, 1, 5);
LET dotnet_arr = CONVERT(py_list, dotnet::List_double);

// 转换回来：.NET List<double> 到 Python list
LET py_flat = CONVERT(flat, python::list);

// 转换基本类型
LET float_val = CONVERT(int_val, FLOAT);
```

## 14.2 MAP_FUNC — 映射函数

定义自定义转换函数：

```ploy
MAP_FUNC convert_to_float(x: INT) -> FLOAT {
    LET result = CONVERT(x, FLOAT);
    RETURN result;
}
```

---

# 15. 符号导出

## 15.1 EXPORT — 使符号可用

使用 `EXPORT` 使函数和管道可从 `.ploy` 模块外部访问：

```ploy
// 使用原始名称导出
EXPORT compute;
EXPORT my_pipeline;

// 使用别名导出
EXPORT compute_and_format AS "polyglot_compute";
EXPORT distance_report AS "polyglot_distance";
EXPORT image_classification AS "image_pipeline";
```

---

# 16. 错误处理与诊断

`.ploy` 语义分析器在检测到问题时会生成详细的错误信息。以下是常见错误场景及其诊断：

## 16.1 参数数量不匹配

```ploy
// 错误：cpp::compute 期望 3 个参数，但只传入了 1 个
FUNC param_count_error() -> FLOAT {
    LET result = CALL(cpp, bad_functions::compute, 42);
    RETURN result;
}
```

```
Error [E3010]: Parameter count mismatch in call to 'compute'
  --> error_handling.ploy:44:9
   | Expected 3 argument(s), got 1
   = suggestion: Check the function signature for 'compute'
```

## 16.2 类型不匹配

```ploy
// 错误：python::process 期望 (INT, INT)，但传入了 (STRING, INT)
FUNC type_mismatch_error() -> FLOAT {
    LET result = CALL(python, bad_functions::process, "hello", 42);
    RETURN result;
}
```

```
Error [E3011]: Type mismatch for parameter 1 in call to 'process'
  --> error_handling.ploy:58:9
   | Expected 'INT', got 'STRING'
   = suggestion: Consider using CONVERT to convert the argument type
```

## 16.3 未定义模块

```ploy
// 错误：模块 'nonexistent' 未被导入
LET x = CALL(cpp, nonexistent::foo, 1);
```

## 16.4 重复 CONFIG

```ploy
CONFIG VENV python "env1";
CONFIG VENV python "env2";  // 错误：语言 'python' 的 CONFIG 重复
```

> 参见 [示例 10: error_handling](../../tests/samples/10_error_handling/) 获取完整的错误场景目录。

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
    LET sum = CALL(cpp, math_ops::add, a, b);
    LET formatted = CALL(python, string_utils::format_result, sum);
    RETURN formatted;
}

EXPORT compute_and_format AS "polyglot_compute";
```

## 17.2 ML 训练管道

> 源码：[示例 05: class_instantiation](../../tests/samples/05_class_instantiation/)

```ploy
IMPORT cpp::matrix;
IMPORT python::model;

LINK(cpp, python, matrix::Matrix, model::LinearModel) {
    MAP_TYPE(cpp::double, python::float);
    MAP_TYPE(cpp::int, python::int);
}

PIPELINE training_pipeline {
    FUNC train(epochs: INT) -> FLOAT {
        LET model = NEW(python, model::LinearModel, 4, 2);
        LET loader = NEW(python, model::DataLoader, data, 1);

        VAR total_loss = 0.0;
        VAR epoch = 0;

        WHILE epoch < epochs {
            LET batch = METHOD(python, loader, next_batch);
            LET mat = NEW(cpp, matrix::Matrix, 1, 4, 0.0);
            LET loss = METHOD(python, model, train_step,
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

此示例展示了所有五种支持的语言在单个管道中协同工作：C++ 处理计算、Python 处理机器学习、Rust 处理数据加载、Java 处理排序、C# 处理 JSON 序列化。

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

STRUCT Experiment {
    name: STRING;
    epochs: INT;
    learning_rate: FLOAT;
    converged: BOOL;
}

FUNC json_round_trip() -> STRING {
    LET json_str = CALL(python, data_science::to_json, "ResNet-50");
    LET transformer = NEW(dotnet, Transformer);
    LET parsed = METHOD(dotnet, transformer, ParseJson, json_str);
    LET enriched = METHOD(dotnet, transformer, Enrich, parsed, "validated", "true");
    LET back = METHOD(dotnet, transformer, ToJsonString, enriched);
    RETURN back;
}

FUNC explicit_conversion() -> FLOAT {
    LET py_list = CALL(python, data_science::random_matrix, 1, 5);
    LET dotnet_arr = CONVERT(py_list, dotnet::List_double);
    LET transformer = NEW(dotnet, Transformer);
    LET flat = METHOD(dotnet, transformer, Flatten, dotnet_arr);
    LET py_flat = CONVERT(flat, python::list);
    LET avg = CALL(python, data_science::mean, py_flat);
    RETURN avg;
}

EXPORT json_round_trip;
EXPORT explicit_conversion;
```

---

# 18. 关键字参考

全部 **54 个关键字**：

| 关键字 | 类别 | 描述 |
|--------|------|------|
| `LINK` | 声明 | 跨语言函数链接 |
| `IMPORT` | 声明 | 模块或包导入 |
| `EXPORT` | 声明 | 符号导出 |
| `MAP_TYPE` | 声明 | 跨语言类型映射 |
| `PIPELINE` | 声明 | 多阶段管道声明 |
| `FUNC` | 声明 | 函数定义 |
| `CONFIG` | 声明 | 包管理器配置 |
| `LET` | 变量 | 不可变变量声明 |
| `VAR` | 变量 | 可变变量声明 |
| `STRUCT` | 类型 | 结构体类型定义 |
| `VOID` | 类型 | 空返回类型 |
| `INT` | 类型 | 整数类型 |
| `FLOAT` | 类型 | 浮点类型 |
| `STRING` | 类型 | 字符串类型 |
| `BOOL` | 类型 | 布尔类型 |
| `ARRAY` | 类型 | 数组类型 |
| `LIST` | 类型 | 列表容器类型 |
| `TUPLE` | 类型 | 元组容器类型 |
| `DICT` | 类型 | 字典容器类型 |
| `OPTION` | 类型 | 可选类型 |
| `RETURN` | 控制流 | 从函数返回值 |
| `IF` | 控制流 | 条件分支 |
| `ELSE` | 控制流 | 替代分支 |
| `WHILE` | 控制流 | 循环 |
| `FOR` | 控制流 | 迭代 |
| `IN` | 控制流 | 与 FOR 和 MATCH 配合使用 |
| `MATCH` | 控制流 | 模式匹配 |
| `CASE` | 控制流 | 匹配分支 |
| `DEFAULT` | 控制流 | 默认匹配分支 |
| `BREAK` | 控制流 | 跳出循环 |
| `CONTINUE` | 控制流 | 跳到下一次迭代 |
| `AS` | 运算符 | 别名/类型转换运算符 |
| `AND` | 运算符 | 逻辑与 |
| `OR` | 运算符 | 逻辑或 |
| `NOT` | 运算符 | 逻辑非 |
| `CALL` | 运算符 | 跨语言函数调用 |
| `CONVERT` | 运算符 | 显式类型转换 |
| `MAP_FUNC` | 运算符 | 映射函数 |
| `NEW` | OOP | 跨语言类实例化 |
| `METHOD` | OOP | 跨语言方法调用 |
| `GET` | OOP | 读取外部对象属性 |
| `SET` | OOP | 写入外部对象属性 |
| `WITH` | OOP | 自动资源管理 |
| `DELETE` | OOP | 对象销毁 |
| `EXTEND` | OOP | 类扩展 |
| `TRUE` | 值 | 布尔真字面量 |
| `FALSE` | 值 | 布尔假字面量 |
| `NULL` | 值 | 空值字面量 |
| `PACKAGE` | 包 | 包导入关键字 |
| `VENV` | 包管理器 | venv/virtualenv 环境 |
| `CONDA` | 包管理器 | Conda 环境 |
| `UV` | 包管理器 | uv 管理的环境 |
| `PIPENV` | 包管理器 | Pipenv 项目 |
| `POETRY` | 包管理器 | Poetry 项目 |

---

# 19. 最佳实践

## 19.1 文件组织

1. **CONFIG 放在最前面** — 环境配置必须出现在依赖它的 `IMPORT PACKAGE` 之前。
2. **集中 IMPORT 语句** — 将所有导入放在一起，模块导入在前，包导入在后。
3. **LINK 块在函数之前定义** — 链接声明提供胶水代码上下文。
4. **MAP_TYPE 放在 LINK 块之后** — 全局类型映射补充每个链接的映射。
5. **导出放在最后** — EXPORT 应该是文件的最后一个部分。

## 19.2 跨语言调用

1. **函数调用使用 CALL** — `CALL(lang, module::func, args...)`。
2. **方法调用使用 METHOD** — `METHOD(lang, obj, method, args...)`。
3. **为每个边界声明类型映射** — 始终显式映射类型。
4. **自动映射不足时使用 CONVERT** — 进行显式编排。

## 19.3 对象生命周期

1. **使用完毕后 DELETE** — 显式销毁对象以释放资源。
2. **作用域资源使用 WITH** — 文件、连接、锁等。
3. **用 LET 保存对象句柄** — 不可变性防止意外重新赋值。

## 19.4 管道设计

1. **每个阶段一个职责** — PIPELINE 中的每个 FUNC 应只做一件事。
2. **使用有意义的名称** — `preprocess`、`classify`、`postprocess` 比 `stage1`、`stage2` 更清晰。
3. **策略性地混合语言** — C++/Rust 用于性能、Python 用于 ML/数据科学、Java/.NET 用于企业逻辑。

## 19.5 示例程序

参考示例程序了解实际模式：

| 示例 | 学习内容 |
|------|----------|
| [01_basic_linking](../../tests/samples/01_basic_linking/) | LINK、CALL、IMPORT、EXPORT 基础 |
| [02_type_mapping](../../tests/samples/02_type_mapping/) | MAP_TYPE 与复杂类型及 STRUCT |
| [03_pipeline](../../tests/samples/03_pipeline/) | PIPELINE 与 IF/WHILE/FOR/MATCH |
| [04_package_import](../../tests/samples/04_package_import/) | IMPORT PACKAGE 与版本约束 |
| [05_class_instantiation](../../tests/samples/05_class_instantiation/) | NEW 和 METHOD 用于 OOP |
| [06_attribute_access](../../tests/samples/06_attribute_access/) | GET 和 SET |
| [07_resource_management](../../tests/samples/07_resource_management/) | WITH 用于作用域资源 |
| [08_delete_extend](../../tests/samples/08_delete_extend/) | DELETE 和 EXTEND |
| [09_mixed_pipeline](../../tests/samples/09_mixed_pipeline/) | 所有特性结合的 ML 管道 |
| [10_error_handling](../../tests/samples/10_error_handling/) | 错误场景和诊断 |
| [11_java_interop](../../tests/samples/11_java_interop/) | Java 跨语言互操作 |
| [12_dotnet_interop](../../tests/samples/12_dotnet_interop/) | .NET 跨语言互操作 |
| [13_generic_containers](../../tests/samples/13_generic_containers/) | 泛型容器类型互操作 |
| [14_async_pipeline](../../tests/samples/14_async_pipeline/) | 多阶段信号处理管道 |
| [15_full_stack](../../tests/samples/15_full_stack/) | 五语言全栈演示 |
| [16_config_and_venv](../../tests/samples/16_config_and_venv/) | CONFIG、IMPORT PACKAGE、CONVERT |
