# 跨语言类实例化 — 实现细节

## 1. 概述

本文档描述了 `.ploy` 语言对跨语言类实例化和方法调用的支持。通过 `NEW` 和 `METHOD` 关键字，
`.ploy` 程序可以创建外部语言的类实例，并调用其方法，实现面向对象语义的跨语言互操作。

### 1.1 动机

> **为什么需要类实例化支持？** 现有的 `.ploy` 语言通过 `CALL` 关键字支持跨语言函数调用，
> 但很多语言的核心 API 是基于类和对象的。例如：
> - Python 的 PyTorch：`model = torch.nn.Linear(784, 10)`，然后 `model.forward(data)`
> - Python 的 scikit-learn：`scaler = StandardScaler()`，然后 `scaler.fit_transform(data)`
> - Rust 的 tokio：`let runtime = tokio::Runtime::new()`，然后 `runtime.spawn(task)`
>
> 没有类实例化支持，开发者无法使用这些面向对象的 API。

### 1.2 设计目标

| 目标 | 说明 |
|------|------|
| **跨语言构造器** | `NEW(language, class, args...)` — 在目标语言中实例化类对象 |
| **跨语言方法调用** | `METHOD(language, object, method, args...)` — 调用对象的方法 |
| **一致的语法风格** | 与现有的 `CALL()` 语法保持一致的括号调用风格 |
| **对象句柄传递** | 对象在 `.ploy` 中作为不透明句柄传递，类型安全由目标语言保证 |
| **成员访问兼容** | 已有的 `.` 语法（`MemberExpression`）与新功能互补 |

## 2. 语言扩展

### 2.1 新增关键字

| 关键字   | 用途                                       | 说明 |
|----------|-------------------------------------------|------|
| `NEW`    | 跨语言类实例化                              | 调用目标语言类的构造器，返回对象句柄 |
| `METHOD` | 跨语言方法调用                              | 在目标语言的对象上调用方法 |
| `GET`    | 跨语言属性访问                              | 读取目标语言对象的属性/字段 |
| `SET`    | 跨语言属性设置                              | 写入目标语言对象的属性/字段 |
| `WITH`   | 跨语言资源管理                              | 上下文管理范围，自动清理资源 |
| `DELETE` | 跨语言对象销毁                              | 显式销毁/释放目标语言对象 |
| `EXTEND` | 跨语言类继承扩展                            | 扩展目标语言类，添加新方法 |

新增后，`.ploy` 语言共有 **54 个保留关键字**。

### 2.2 语法定义

#### NEW — 构造器调用

```
NEW(language, class_name [, arg1, arg2, ...])
```

| 参数 | 说明 | 示例 |
|------|------|------|
| `language` | 目标语言标识符 | `python`、`rust`、`cpp`、`java`、`dotnet` |
| `class_name` | 类名（可用 `::` 限定路径） | `MyClass`、`torch::nn::Linear` |
| `arg1, arg2, ...` | 构造器参数（可选） | `784, 10` |

**示例：**

```ploy
// 实例化 Python 类（无参数）
LET model = NEW(python, sklearn::LinearRegression);

// 实例化 Python 类（带参数）
LET layer = NEW(python, torch::nn::Linear, 784, 10);

// 实例化 Rust 结构体
LET runtime = NEW(rust, tokio::Runtime);

// 使用字符串参数
LET conn = NEW(python, sqlite3::Connection, "database.db");
```

#### METHOD — 方法调用

```
METHOD(language, object, method_name [, arg1, arg2, ...])
```

| 参数 | 说明 | 示例 |
|------|------|------|
| `language` | 目标语言标识符 | `python`、`rust` |
| `object` | 对象表达式（通常是变量） | `model`、`scaler` |
| `method_name` | 方法名（可用 `::` 限定） | `forward`、`fit_transform` |
| `arg1, arg2, ...` | 方法参数（可选） | `data, 42` |

**示例：**

```ploy
// 调用方法（带参数）
LET output = METHOD(python, model, forward, input_data);

// 调用方法（无参数）
LET vocab = METHOD(python, tokenizer, get_vocab);

// 调用限定路径的方法
LET result = METHOD(python, obj, utils::serialize, data);
```

### 2.3 与现有功能的对比

| 功能 | 语法 | 用途 | 返回值 |
|------|------|------|--------|
| `CALL` | `CALL(lang, func, args...)` | 调用跨语言函数 | 函数返回值 |
| `NEW` | `NEW(lang, class, args...)` | 实例化跨语言类 | 对象句柄 |
| `METHOD` | `METHOD(lang, obj, method, args...)` | 调用对象方法 | 方法返回值 |

## 3. 实现细节

### 3.1 AST 节点

新增两个表达式节点：

```
NewExpression
├── language: string          // 目标语言标识符
├── class_name: string        // 类名（可含 :: 限定）
└── args: Expression[]        // 构造器参数

MethodCallExpression
├── language: string          // 目标语言标识符
├── object: Expression        // 接收者对象
├── method_name: string       // 方法名（可含 :: 限定）
└── args: Expression[]        // 方法参数
```

### 3.2 解析器处理

`NEW` 和 `METHOD` 在 `ParsePrimary()` 中被识别，位于 `CALL` 之后：

```
ParsePrimary:
    CALL → ParseCallDirective()
    NEW → ParseNewExpression()
    METHOD → ParseMethodCallDirective()
    CONVERT → ParseConvertExpression()
    ...
```

解析过程：
1. 消费关键字（`NEW` 或 `METHOD`）
2. 期望 `(`
3. 解析语言名称
4. 期望 `,`
5. 对于 `NEW`：解析类名（含 `::` 限定）；对于 `METHOD`：解析对象表达式 + `,` + 方法名
6. 可选：解析剩余参数（`,` 分隔）
7. 期望 `)`

### 3.3 语义分析

- **语言验证**：检查 `language` 是否为有效的语言标识符（`cpp`、`python`、`rust`、`c`、`ploy`）
- **类名验证**：检查 `class_name`（`NEW`）或 `method_name`（`METHOD`）非空
- **参数分析**：递归分析每个参数表达式
- **对象验证**（`METHOD`）：分析接收者对象表达式，确保已定义
- **返回类型**：`NEW` 和 `METHOD` 均返回 `Any` 类型（外部类型无法静态解析）

### 3.4 IR 降级

#### NEW 的 IR 生成

构造器调用被编译为桥接桩函数调用：

```
源代码: LET model = NEW(python, torch::nn::Linear, 784, 10);
IR:     %0 = call __ploy_bridge_ploy_python_torch__nn__Linear____init__(784, 10) -> ptr
```

- 桩函数名称格式：`__ploy_bridge_ploy_<language>_<class>____init__`
- 返回类型：`ptr`（不透明对象指针）
- 生成 `CrossLangCallDescriptor`，`source_function` 设置为 `<class>::__init__`

#### METHOD 的 IR 生成

方法调用被编译为桥接桩函数调用，接收者对象作为第一个参数传递：

```
源代码: LET output = METHOD(python, model, forward, data);
IR:     %1 = call __ploy_bridge_ploy_python_forward(%model, %data) -> i64
```

- 接收者对象自动作为第一个参数插入（类似 Python 的 `self`）
- 生成 `CrossLangCallDescriptor`，`source_param_types` 包含对象类型 + 参数类型

## 4. 完整示例

### 4.1 机器学习推理管道

```ploy
// 导入 Python ML 包
IMPORT python PACKAGE torch;
IMPORT python PACKAGE sklearn;
IMPORT cpp::inference_engine;

// 跨语言类型映射
LINK(cpp, python, run_inference, torch::forward) {
    MAP_TYPE(cpp::float_ptr, python::Tensor);
}

FUNC ml_pipeline() -> INT {
    // 实例化 Python 类
    LET scaler = NEW(python, sklearn::StandardScaler);
    LET model = NEW(python, torch::nn::Sequential);

    // 准备数据
    LET data = [1.0, 2.0, 3.0];

    // 调用方法进行预处理
    LET scaled = METHOD(python, scaler, fit_transform, data);

    // 调用方法进行推理
    LET prediction = METHOD(python, model, forward, scaled);

    // 将结果传递给 C++ 后处理
    LET result = CALL(cpp, inference_engine::postprocess, prediction);

    RETURN 0;
}
```

### 4.2 数据库操作

```ploy
IMPORT python PACKAGE sqlite3;

FUNC query_database() -> INT {
    // 创建数据库连接
    LET conn = NEW(python, sqlite3::Connection, "data.db");

    // 执行查询
    LET cursor = METHOD(python, conn, execute, "SELECT * FROM users");

    // 获取结果
    LET rows = METHOD(python, cursor, fetchall);

    // 关闭连接
    METHOD(python, conn, close);

    RETURN 0;
}
```

### 4.3 在 PIPELINE 中使用

```ploy
IMPORT python PACKAGE torch;

PIPELINE training_pipeline {
    LET model = NEW(python, torch::nn::Linear, 128, 10);
    LET optimizer = NEW(python, torch::optim::SGD, 0.01);
    LET loss = METHOD(python, model, forward, 42);
    RETURN 0;
}
```

## 5. 编译器流水线中的位置

```
┌────────┐   ┌────────┐   ┌────────┐   ┌─────────┐   ┌──────────┐
│ Lexer  │ → │ Parser │ → │  Sema  │ → │Lowering │ → │   IR     │
│  (54   │   │ NEW    │   │ 语言   │   │构造器桩  │   │ 桥接调用  │
│  关键字)│   │ METHOD │   │ 验证   │   │方法桩    │   │ 描述符   │
└────────┘   └────────┘   └────────┘   └─────────┘   └──────────┘
```

## 6. 限制

| 限制 | 说明 | 状态 |
|-----------|-------------|--------|
| **静态类型未知** | `NEW` 和 `METHOD` 返回 `Any` 类型；没有类模式时无法在编译时进行完整类型检查 | 通过 `ForeignClassSchema` 部分解决 |
| **无析构器** | 不支持自动调用析构器/`__del__`；需手动调用 `METHOD(lang, obj, close)` | 仍然适用 |
| **无继承** | `.ploy` 不支持在目标语言中定义子类 | 仍然适用 |
| **无属性赋值** | 之前仅支持方法调用，不支持直接 `obj.attr = value`。**现已解决：** `GET` 和 `SET` 通过 `RegisterClassSchema()` 注册的类模式解析类型 | **已解决** |

## 7. 实现细节（更新）

### 7.1 类模式解析

语义分析器维护 `class_schemas_` 映射（`std::map<std::string, ForeignClassSchema>`），注册已知的外部类布局。处理 `NEW`、`METHOD`、`GET` 或 `SET` 时：

1. 分析器在 `class_schemas_` 中查找类名
2. 如果找到，验证：
   - `NEW` 的构造器参数数量和类型
   - `METHOD` 的方法签名（参数数量、返回类型）
   - `GET`/`SET` 的字段存在性和类型
3. 如果未找到且启用了严格模式，报错；否则发出警告并将表达式默认为 `Any`

这将跨语言 OOP 的类型检查从 IR 降级阶段提前到语义分析阶段。


