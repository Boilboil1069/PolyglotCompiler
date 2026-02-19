# 复杂参数类型扩展 — 实现细节

## 1. 概述

本文档描述了 `.ploy` 语言的扩展，以支持跨语言函数级链接中的复杂参数类型。目标是实现容器类型（列表、元组、字典）、结构类型（结构体）和可选类型在语言边界之间的无缝编组。

### 1.1 动机

> **为什么需要复杂类型支持？** 初始的 `.ploy` 语言仅支持基本类型（`INT`、`FLOAT`、`BOOL`、`STRING`、`VOID`）。但实际的跨语言互操作中，函数的参数和返回值经常是容器类型。例如：
> - Python 函数经常返回 `list`、`dict`、`tuple`
> - C++ 函数使用 `std::vector`、`std::map`、`struct`
> - Rust 函数使用 `Vec`、`HashMap`、`Option`
>
> 没有复杂类型支持，开发者无法在 `.ploy` 中表达这些真实世界的函数签名。

### 1.2 设计目标

| 目标 | 说明 |
|------|------|
| **统一容器类型语法** | `LIST(T)`、`TUPLE(T1, T2, ...)`、`DICT(K, V)`、`OPTION(T)` — 使用圆括号包裹类型参数 |
| **结构体定义** | 通过 `STRUCT` 声明定义命名聚合类型，用于跨语言结构映射 |
| **转换函数** | `MAP_FUNC` 声明用于定义自定义的类型转换逻辑 |
| **显式转换** | `CONVERT(expr, type)` 用于在无法自动推断时进行手动类型转换 |
| **容器字面量** | `[1, 2, 3]` 表示列表，`(1, "hello")` 表示元组，`Name { f: v }` 表示结构体 |

## 2. 语言扩展

### 2.1 新增关键字

| 关键字       | 用途                         | 说明 |
|-------------|------------------------------|------|
| `LIST`      | 列表/向量容器类型              | 对应 C++ 的 `std::vector`、Python 的 `list`、Rust 的 `Vec` |
| `TUPLE`     | 元组容器类型                   | 固定大小的异构集合，对应各语言的元组类型 |
| `DICT`      | 字典/映射容器类型              | 键值对映射，对应 `std::unordered_map`、`dict`、`HashMap` |
| `OPTION`    | 可选/可空容器类型              | 可以为空的包装类型，对应 `std::optional`、`Optional`、`Option` |
| `MAP_FUNC`  | 自定义类型映射函数声明          | 用于定义复杂的类型转换逻辑，超越简单的 `MAP_TYPE` 映射 |
| `CONVERT`   | 显式类型转换表达式              | 将表达式转换为指定类型，类似于强制类型转换 |
| `PACKAGE`   | 语言原生包导入                 | 导入目标语言的第三方包（如 Python 的 numpy） |

### 2.2 容器类型语法

> **语法约定：** 容器类型使用 **圆括号 `()`** 包裹类型参数（区别于某些语言使用方括号或尖括号）。

```ploy
// 基本容器类型
LIST(i32)                          // 32位整数列表
TUPLE(i32, STRING, f64)            // 异构元组：整数、字符串、浮点数
DICT(STRING, i32)                  // 字典：字符串键、整数值
OPTION(i32)                        // 可选整数：可以为 NULL

// 嵌套容器类型
LIST(LIST(f64))                    // 嵌套列表（二维数组）
DICT(STRING, LIST(f64))            // 字典，值为浮点数列表
OPTION(LIST(i32))                  // 可选的整数列表
```

> **与各语言对应关系示例：**
> - `LIST(f64)` → C++: `std::vector<double>` | Python: `list[float]` | Rust: `Vec<f64>`
> - `DICT(STRING, i32)` → C++: `std::unordered_map<std::string, int>` | Python: `dict[str, int]` | Rust: `HashMap<String, i32>`

### 2.3 结构体定义

> **STRUCT 声明了命名的聚合类型。** 结构体中的每个字段有名称和类型，用分号分隔。

```ploy
// 简单结构体
STRUCT Point {
    x: f64;                // x 坐标
    y: f64;                // y 坐标
    label: STRING;         // 标签
}
```

> **说明：** 结构体可以包含容器类型字段：

```ploy
// 带复杂字段的结构体
STRUCT DataSet {
    name: STRING;                      // 数据集名称
    values: LIST(f64);                 // 浮点数值列表
    metadata: DICT(STRING, STRING);    // 键值对元数据
}
```

> **使用场景：** 结构体主要用于 `LINK ... AS STRUCT` 进行跨语言结构映射，以及作为函数参数和返回类型。

### 2.4 MAP_FUNC 声明

> **MAP_FUNC 定义自定义转换函数。** 当简单的 `MAP_TYPE` 声明无法表达复杂的转换逻辑时使用。

```ploy
// 数值归一化转换
MAP_FUNC normalize(x: f64) -> f64 {
    IF x < 0.0 {
        RETURN 0.0;        // 下限截断
    }
    IF x > 1.0 {
        RETURN 1.0;        // 上限截断
    }
    RETURN x;              // 已在 [0, 1] 范围内
}
```

> **MAP_FUNC vs FUNC 的区别：**
> - `MAP_FUNC` 会被注册到转换函数表中，在跨语言调用时可被**自动**调用进行类型转换
> - `FUNC` 是普通函数，需要**手动**调用

```ploy
// 容器元素转换
MAP_FUNC to_list(x: f64) -> LIST(f64) {
    LET result = [x];
    RETURN result;
}
```

### 2.5 CONVERT 表达式

> **CONVERT 用于显式类型转换。** 编译器会查找合适的转换路径。

```ploy
LET x = CONVERT(python_value, i32);            // 转换为整数
LET items = CONVERT(raw_list, LIST(f64));       // 转换为浮点数列表
LET table = CONVERT(cpp_map, DICT(STRING, i32)); // 转换为字典
```

> **转换路径查找顺序：**
> 1. 直接 `MAP_TYPE` 映射
> 2. 已注册的 `MAP_FUNC` 转换函数
> 3. 内置转换（如 `int` → `float`）
> 4. 如果都找不到 → 编译错误

### 2.6 容器字面量

```ploy
LET numbers = [1, 2, 3, 4, 5];                          // 列表字面量
LET pair = (1, "hello");                                  // 元组字面量
LET origin = Point { x: 0.0, y: 0.0, label: "origin" };  // 结构体字面量
```

> **说明：** 字面量的类型由元素类型自动推断。列表要求所有元素类型兼容，元组允许每个元素类型不同。

## 3. 扩展类型映射表

| .ploy 类型           | C++ 等价                     | Python 等价      | Rust 等价              | 说明 |
|---------------------|----------------------------|-----------------|------------------------|------|
| i32                 | int32_t                    | int             | i32                    | 32位整数 |
| i64                 | int64_t                    | int             | i64                    | 64位整数 |
| f32                 | float                      | float           | f32                    | 32位浮点 |
| f64                 | double                     | float           | f64                    | 64位浮点 |
| BOOL                | bool                       | bool            | bool                   | 布尔 |
| STRING / str        | std::string                | str             | String                 | 字符串 |
| VOID                | void                       | None            | ()                     | 无返回值 |
| ptr                 | void*                      | object          | *mut u8                | 指针 |
| LIST(T)             | std::vector\<T\>           | list            | Vec\<T\>               | 列表 |
| TUPLE(T1,T2,...)    | std::tuple\<T1,T2,...\>    | tuple           | (T1, T2, ...)          | 元组 |
| DICT(K,V)           | std::unordered_map\<K,V\>  | dict            | HashMap\<K,V\>         | 字典 |
| OPTION(T)           | std::optional\<T\>         | Optional / None | Option\<T\>            | 可选 |
| STRUCT Name         | struct Name                | class/namedtuple| struct Name            | 结构体 |

## 4. AST 扩展

### 4.1 类型表示

> **设计决策：** 无需新增 `TypeNode` 子类。现有的 `ParameterizedType` 节点通过 `name` 字段区分不同的容器类型：

| 条件 | 解析结果 | 示例 |
|------|---------|------|
| `name == "LIST"`，1 个类型参数 | 列表类型 | `LIST(f64)` |
| `name == "TUPLE"`，N 个类型参数 | 元组类型 | `TUPLE(i32, STRING)` |
| `name == "DICT"`，2 个类型参数 | 字典类型 | `DICT(STRING, i32)` |
| `name == "OPTION"`，1 个类型参数 | 可选类型 | `OPTION(f64)` |

### 4.2 新增表达式节点

| 节点 | 字段 | 语法 | 说明 |
|------|------|------|------|
| `ListLiteral` | elements: vector\<Expression\> | `[a, b, c]` | 方括号括起的元素列表 |
| `TupleLiteral` | elements: vector\<Expression\> | `(a, b)` | 圆括号括起的元素列表 |
| `StructLiteral` | struct_name, fields | `Name { f: v }` | 结构体名 + 字段赋值 |
| `ConvertExpression` | expr, target_type | `CONVERT(e, T)` | 表达式 + 目标类型 |

### 4.3 新增语句节点

| 节点 | 字段 | 语法 | 说明 |
|------|------|------|------|
| `StructDecl` | name, fields (名称+类型) | `STRUCT S { ... }` | 结构体类型定义 |
| `MapFuncDecl` | name, params, return_type, body | `MAP_FUNC f(...) { }` | 转换函数定义 |

## 5. 语义分析扩展

### 5.1 类型解析

> **ResolveType 方法的扩展逻辑：**

| 输入 | 输出 | 说明 |
|------|------|------|
| `LIST(T)` | `core::Type::Array(T)` | 动态数组，与 ARRAY 共享底层表示 |
| `TUPLE(T1,T2,...)` | `core::Type::Tuple({T1, T2, ...})` | 固定大小异构集合 |
| `DICT(K,V)` | `core::Type::GenericInstance("dict", {K, V})` | 参数化泛型类型 |
| `OPTION(T)` | `core::Type::Optional(T)` | 可空包装类型 |
| `STRUCT Name` | `core::Type::Struct(Name)` | 命名结构类型 |

### 5.2 验证规则

| 验证项 | 规则 | 错误示例 |
|--------|------|---------|
| 结构体字段 | 不允许重复字段名 | `STRUCT S { x: i32; x: f64; }` → 错误 |
| 结构体字段类型 | 所有类型必须有效 | `STRUCT S { x: INVALID; }` → 错误 |
| MAP_FUNC 签名 | 参数和返回类型有效 | `MAP_FUNC f(x: ???) -> i32 { }` → 错误 |
| CONVERT 源类型 | 源表达式类型必须已知 | 类型推断失败 → 错误 |
| CONVERT 目标类型 | 必须存在转换路径 | 无 MAP_TYPE 或 MAP_FUNC → 错误 |
| 列表字面量 | 所有元素类型兼容 | `[1, "hello"]` → 类型不兼容 |
| 结构体字面量 | 字段匹配定义 | 缺少必需字段 → 错误 |

## 6. IR 降级扩展

### 6.1 容器类型 IR 表示

> **容器在 IR 中的表示方式：**

| 容器类型 | IR 表示 | 说明 |
|---------|--------|------|
| `LIST(T)` | `ptr`（指向 RuntimeList 的指针） | 运行时管理的动态数组 |
| `TUPLE(T1,T2,...)` | `struct { T1, T2, ... }` | 直接展开为结构体 |
| `DICT(K,V)` | `ptr`（指向 RuntimeDict 的指针） | 运行时管理的哈希表 |
| `OPTION(T)` | `struct { i1 has_value, T value }` | 带判别值的结构体 |
| `STRUCT Name` | `struct { fields... }` | 直接映射为 IR 结构体 |

### 6.2 降级规则

| 构造 | IR 产物 | 说明 |
|------|--------|------|
| 列表字面量 `[a, b]` | `__ploy_rt_list_create` + `__ploy_rt_list_push` | 先创建空列表，再逐个添加元素 |
| 元组字面量 `(a, b)` | 分配结构体 + 存储元素 | 直接在栈上分配 |
| 结构体字面量 | 分配结构体 + 存储字段 | 用 GEP 定位字段并赋值 |
| `CONVERT(e, T)` | 调用 `__ploy_convert_<type>` 或 MAP_FUNC | 根据转换路径选择策略 |
| `MAP_FUNC` 声明 | 生成 `__ploy_mapfunc_<name>` IR 函数 | 完整的函数实现 |

## 7. 运行时容器编组

### 7.1 数据结构

> **运行时使用三种核心数据结构来管理容器：**

#### RuntimeList — 动态数组

```
偏移量    字段           说明
+0:      count         当前元素数量
+8:      capacity      分配的容量
+16:     elem_size     每个元素的字节大小
+24:     data          指向元素数组的指针
```

> **说明：** 类似于 C++ 的 `std::vector`，当 count == capacity 时自动扩容（2倍增长策略）。

#### RuntimeTuple — 固定大小异构集合

```
偏移量    字段           说明
+0:      num_elements  元素数量
+8:      offsets[N]    每个元素的字节偏移量
+?:      data...       紧凑打包的元素数据
```

> **说明：** 由于元组元素类型不同，每个元素大小可能不同，因此需要 offsets 数组来定位各元素。

#### RuntimeDict — 哈希表

```
偏移量    字段           说明
+0:      count         条目数量
+8:      bucket_count  桶数量
+16:     key_size      键的字节大小
+24:     value_size    值的字节大小
+32:     buckets       指向哈希桶数组的指针
```

> **说明：** 使用 FNV-1a 哈希算法和链表法解决冲突。负载因子超过 0.75 时自动扩容。

### 7.2 运行时函数

```cpp
// 列表操作
void *__ploy_rt_list_create(size_t elem_size, size_t initial_capacity);
void  __ploy_rt_list_push(void *list, const void *elem);
void *__ploy_rt_list_get(void *list, size_t index);
size_t __ploy_rt_list_len(void *list);
void  __ploy_rt_list_free(void *list);

// 元组操作
void *__ploy_rt_tuple_create(size_t num_elements, const size_t *elem_sizes);
void *__ploy_rt_tuple_get(void *tuple, size_t index);
void  __ploy_rt_tuple_free(void *tuple);

// 字典操作
void *__ploy_rt_dict_create(size_t key_size, size_t value_size);
void  __ploy_rt_dict_insert(void *dict, const void *key, const void *value);
void *__ploy_rt_dict_lookup(void *dict, const void *key);
size_t __ploy_rt_dict_len(void *dict);
void  __ploy_rt_dict_free(void *dict);
```

### 7.3 跨语言容器转换

| 函数 | 转换方向 | 说明 |
|------|---------|------|
| `__ploy_rt_convert_list_to_pylist` | RuntimeList → Python list | 遍历列表，通过 Python C API 创建 PyList |
| `__ploy_rt_convert_pylist_to_list` | Python list → RuntimeList | 遍历 PyList，逐元素转换并存入 RuntimeList |
| `__ploy_rt_convert_dict_to_pydict` | RuntimeDict → Python dict | 遍历条目，通过 Python C API 创建 PyDict |
| `__ploy_rt_convert_pydict_to_dict` | Python dict → RuntimeDict | 遍历 PyDict，逐条目转换并插入 RuntimeDict |
| `__ploy_rt_convert_vec_to_list` | Rust Vec → RuntimeList | 提取 Rust Vec 内存布局，填充 RuntimeList |
| `__ploy_rt_convert_list_to_vec` | RuntimeList → Rust Vec | 从 RuntimeList 创建 Rust Vec |

## 8. 链接器扩展

### 8.1 容器编组策略

> **链接器为每种容器类型的跨语言传递生成特定的编组代码：**

| 转换                           | 策略                                              |
|-------------------------------|---------------------------------------------------|
| `LIST(T)` → `python::list`   | 遍历 RuntimeList + 通过 Python C API 转换每个元素     |
| `LIST(T)` → `rust::Vec<T>`   | 如果内存布局兼容则零拷贝，否则逐元素遍历               |
| `TUPLE(...)` → `python::tuple`| 通过 Python C API 将元素打包到 Python 元组            |
| `DICT(K,V)` → `python::dict` | 遍历条目 + 通过 Python C API 转换键值                 |
| `STRUCT` → `STRUCT`          | 逐字段转换，递归编组嵌套类型                          |
| `OPTION(T)` → `rust::Option<T>` | 检查判别值 + 有值时转换内部值                       |

> **性能优化：** 当源和目标的内存布局兼容时（例如 C++ `std::vector<double>` 和 Rust `Vec<f64>` 使用相同的内存表示），链接器会生成零拷贝转换代码，直接传递底层指针。

## 9. 使用示例

### 9.1 跨语言列表处理

```ploy
IMPORT python PACKAGE numpy AS np;
IMPORT cpp::math_engine;

MAP_TYPE(cpp::double, python::float);

LINK(cpp, python, math_engine::compute_stats, np::mean) {
    MAP_TYPE(python::list, cpp::std::vector_double);
}

PIPELINE analyze {
    FUNC load() -> LIST(f64) {
        LET data = CALL(python, np::loadtxt, "input.csv");
        RETURN data;
    }

    FUNC compute(data: LIST(f64)) -> f64 {
        LET result = CALL(cpp, math_engine::compute_stats, data);
        RETURN result;
    }
}
```

> **说明：** 此示例展示了使用 Python numpy 加载数据，然后传递给 C++ 进行统计计算。`LIST(f64)` 在 Python 端是 `list[float]`，在 C++ 端是 `std::vector<double>`，编组器自动处理转换。

### 9.2 跨语言结构体映射

```ploy
STRUCT CppPoint {
    x: f64;
    y: f64;
}

MAP_FUNC to_complex(x: f64) -> TUPLE(f64, f64) {
    RETURN (x, 0.0);
}

LINK(cpp, python, geometry::distance, point_gen::make_point) AS STRUCT {
    MAP_TYPE(cpp::double, python::float);
}
```

> **说明：** `LINK ... AS STRUCT` 告诉编译器这是结构体类型的链接。`MAP_FUNC to_complex` 定义了如何将实数转换为复数的元组表示。

### 9.3 可选类型

```ploy
FUNC safe_divide(a: f64, b: f64) -> OPTION(f64) {
    IF b == 0.0 {
        RETURN NULL;       // 除数为零，返回空值
    }
    RETURN a / b;          // 正常除法
}

PIPELINE safe_compute {
    FUNC run() -> f64 {
        LET result = safe_divide(10.0, 0.0);
        MATCH result {
            CASE NULL => {
                RETURN -1.0;           // 处理空值情况
            }
            DEFAULT => {
                RETURN CONVERT(result, f64);  // 提取实际值
            }
        }
    }
}
```

> **说明：** `OPTION(f64)` 表示一个可能为空的浮点数。使用 `MATCH` 语句安全地处理空值情况，避免运行时空指针错误。
