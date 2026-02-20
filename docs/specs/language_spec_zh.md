# PolyglotCompiler 语言与 IR 规范

> **版本**: 2.0.0  
> **最后更新**: 2026-02-20  

---

## 目录

1. [支持的源语言](#1-支持的源语言)
2. [.ploy 语言规范](#2-ploy-语言规范)
3. [统一 IR 规范](#3-统一-ir-规范)
4. [跨语言调用约定](#4-跨语言调用约定)
5. [类型编组规则](#5-类型编组规则)
6. [二进制输出格式](#6-二进制输出格式)

---

# 1. 支持的源语言

## 1.1 语言矩阵

| 语言 | 前端 | 文件扩展名 | 版本 | 状态 |
|------|------|-----------|------|------|
| C++ | `frontend_cpp` | `.cpp`, `.cxx`, `.cc`, `.h`, `.hpp` | C++11–C++23 | ✅ |
| Python | `frontend_python` | `.py`, `.pyi` | 3.8+ | ✅ |
| Rust | `frontend_rust` | `.rs` | 2018/2021 edition | ✅ |
| Java | `frontend_java` | `.java` | 8, 17, 21, 23 | ✅ |
| C# (.NET) | `frontend_dotnet` | `.cs`, `.vb` | .NET 6, 7, 8, 9 | ✅ |
| .ploy | `frontend_ploy` | `.ploy` | 1.0 | ✅ |

## 1.2 检测规则

`polyc` 驱动程序根据文件扩展名自动检测源语言：

| 扩展名 | 语言 |
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

`.ploy` 是一种领域特定语言，用于表达跨语言函数级链接、面向对象互操作、类型映射和多语言管道编排。

## 2.2 关键字（共 54 个）

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

## 2.3 声明

### LINK — 跨语言函数链接

```ploy
LINK <目标语言>::<模块>::<函数> AS FUNC(<参数类型>) -> <返回类型>;
```

**示例**:
```ploy
LINK cpp::math::add AS FUNC(INT, INT) -> INT;
LINK python::utils::format_string AS FUNC(STRING) -> STRING;
```

### IMPORT — 模块导入

```ploy
IMPORT <语言> MODULE <模块路径>;
```

### IMPORT PACKAGE — 带版本约束的包导入

```ploy
IMPORT <语言> PACKAGE <包名>;
IMPORT <语言> PACKAGE <包名> >= <版本号>;
IMPORT <语言> PACKAGE <包名>::(<符号1>, <符号2>) >= <版本号>;
```

**版本运算符**: `>=`, `<=`, `>`, `<`, `==`, `!=`

### EXPORT — 符号导出

```ploy
EXPORT <符号名>;
EXPORT <符号名> AS <别名>;
```

### MAP_TYPE — 跨语言类型映射

```ploy
MAP_TYPE <ploy类型> = <语言>::<类型名>;
```

### CONFIG — 包管理器配置

```ploy
CONFIG VENV "<路径>";          // Python venv
CONFIG CONDA "<环境名>";       // Conda 环境
CONFIG UV "<项目路径>";         // uv 项目
CONFIG PIPENV "<项目路径>";     // Pipenv 项目
CONFIG POETRY "<项目路径>";     // Poetry 项目
```

## 2.4 函数

```ploy
FUNC <名称>(<参数>: <类型>, ...) -> <返回类型> {
    <函数体>
}
```

## 2.5 变量

```ploy
LET <名称>: <类型> = <表达式>;   // 不可变
VAR <名称>: <类型> = <表达式>;   // 可变
```

## 2.6 控制流

### 条件语句

```ploy
IF (<条件>) { <主体> }
ELSE IF (<条件>) { <主体> }
ELSE { <主体> }
```

### 循环

```ploy
WHILE (<条件>) { <主体> }
FOR (<变量> IN <可迭代对象>) { <主体> }
```

### 模式匹配

```ploy
MATCH (<表达式>) {
    CASE <模式> => { <主体> }
    DEFAULT => { <主体> }
}
```

## 2.7 跨语言面向对象操作

### 对象创建

```ploy
LET obj = NEW(<语言>, <类路径>, <参数...>);
```

### 方法调用

```ploy
LET result = METHOD(<语言>, <对象>, <方法名>, <参数...>);
```

### 属性访问

```ploy
LET value = GET(<语言>, <对象>, <属性>);
SET(<语言>, <对象>, <属性>, <值>);
```

### 资源管理

```ploy
WITH (<语言>, <对象>) {
    // 对象在块退出时自动关闭/释放
}
```

### 对象销毁

```ploy
DELETE(<语言>, <对象>);
```

### 类继承扩展

```ploy
EXTEND(<语言>, <基类>) {
    // 定义扩展行为
}
```

## 2.8 管道

```ploy
PIPELINE <名称> {
    STAGE <阶段名> = CALL(<语言>, <函数>, <参数...>);
    STAGE <阶段名> = CALL(<语言>, <函数>, <上一阶段>);
}
```

## 2.9 类型系统

### 原始类型

| .ploy 类型 | C++ | Python | Rust | Java | C# |
|-----------|-----|--------|------|------|-----|
| `INT` | `int` | `int` | `i32` | `int` | `int` |
| `FLOAT` | `double` | `float` | `f64` | `double` | `double` |
| `STRING` | `std::string` | `str` | `String` | `String` | `string` |
| `BOOL` | `bool` | `bool` | `bool` | `boolean` | `bool` |
| `VOID` | `void` | `None` | `()` | `void` | `void` |

### 容器类型

| .ploy 类型 | C++ | Python | Rust |
|-----------|-----|--------|------|
| `LIST<T>` | `std::vector<T>` | `list[T]` | `Vec<T>` |
| `TUPLE<T...>` | `std::tuple<T...>` | `tuple` | `(T...)` |
| `DICT<K,V>` | `std::unordered_map<K,V>` | `dict[K,V]` | `HashMap<K,V>` |
| `ARRAY<T,N>` | `T[N]` | `list[T]` | `[T; N]` |
| `OPTION<T>` | `std::optional<T>` | `Optional[T]` | `Option<T>` |

## 2.10 运算符

| 类别 | 运算符 |
|------|--------|
| 算术 | `+`, `-`, `*`, `/`, `%` |
| 比较 | `==`, `!=`, `<`, `>`, `<=`, `>=` |
| 逻辑 | `AND`, `OR`, `NOT` |
| 赋值 | `=` |
| 成员访问 | `.`, `::` |

## 2.11 语句终止

所有语句以分号 (`;`) 终止。

---

# 3. 统一 IR 规范

## 3.1 IR 类型系统

IR 使用独立于源语言的底层类型系统：

| IR 类型 | 大小 | 说明 |
|---------|------|------|
| `i1` | 1 位 | 布尔值 |
| `i8` | 1 字节 | 字节 / 字符 |
| `i16` | 2 字节 | 短整数 |
| `i32` | 4 字节 | 32 位整数 |
| `i64` | 8 字节 | 64 位整数 |
| `f32` | 4 字节 | 单精度浮点数 |
| `f64` | 8 字节 | 双精度浮点数 |
| `void` | 0 字节 | 无值 |
| `T*` | 指针大小 | 指向 T 的指针 |
| `T&` | 指针大小 | T 的引用 |
| `[N x T]` | N × sizeof(T) | 固定大小数组 |
| `<N x T>` | N × sizeof(T) | SIMD 向量 |
| `{T1, T2, ...}` | 字段之和 | 结构体类型 |
| `fn(T1, T2) -> R` | — | 函数类型 |

## 3.2 指令

### 算术指令

```
%result = add i64 %a, %b
%result = sub i64 %a, %b
%result = mul i64 %a, %b
%result = sdiv i64 %a, %b
%result = fadd f64 %x, %y
```

### 内存指令

```
%ptr = alloca i64
store i64 %val, i64* %ptr
%loaded = load i64, i64* %ptr
%elem = getelementptr %struct, %struct* %ptr, i64 0, i32 1
```

### 控制流指令

```
br i1 %cond, label %true_bb, label %false_bb
br label %target
ret i64 %val
ret void
switch i64 %val, label %default [i64 0, label %case0; i64 1, label %case1]
```

### 类型转换指令

```
%ext = zext i32 %val to i64       // 零扩展
%trunc = trunc i64 %val to i32    // 截断
%cast = bitcast i64* %ptr to i8*  // 位转换
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

IR 在 SSA 转换过程后使用静态单赋值（SSA）形式：

- 每个变量只被赋值一次
- Phi 节点在控制流汇合点合并值
- 支持高效的优化过程（常量传播、死代码消除等）

---

# 4. 跨语言调用约定

## 4.1 概述

PolyglotCompiler 生成 FFI 粘合代码以桥接不同源语言之间的函数调用。每次跨语言调用经过以下步骤：

1. **参数编组**：将调用方的类型转换为被调用方的类型
2. **函数分发**：通过运行时桥接路由调用
3. **返回值解编组**：将被调用方的返回类型转换回调用方的类型

## 4.2 调用约定表

| 源 → 目标 | 约定 | 桥接 |
|----------|------|------|
| .ploy → C++ | cdecl | 直接 FFI |
| .ploy → Python | CPython C API | `__ploy_python_*` 运行时 |
| .ploy → Rust | Rust ABI (extern "C") | 直接 FFI |
| .ploy → Java | JNI | `__ploy_java_*` 运行时 |
| .ploy → .NET | CoreCLR Hosting | `__ploy_dotnet_*` 运行时 |

## 4.3 运行时桥接函数

| 桥接 | 头文件 | 说明 |
|------|--------|------|
| `__ploy_python_call` | `runtime/include/python_bridge.h` | 调用 Python 函数 |
| `__ploy_python_new` | `runtime/include/python_bridge.h` | 实例化 Python 类 |
| `__ploy_java_call` | `runtime/include/java_rt.h` | 通过 JNI 调用 Java 方法 |
| `__ploy_java_init` | `runtime/include/java_rt.h` | 初始化 JVM |
| `__ploy_dotnet_call` | `runtime/include/dotnet_rt.h` | 通过 CoreCLR 调用 .NET 方法 |
| `__ploy_dotnet_init` | `runtime/include/dotnet_rt.h` | 初始化 .NET 运行时 |

---

# 5. 类型编组规则

## 5.1 原始类型编组

| .ploy → C++ | 规则 |
|-------------|------|
| `INT` → `int` | 直接传递（相同 ABI 表示） |
| `FLOAT` → `double` | 直接传递 |
| `STRING` → `std::string` | 复制字符串数据 |
| `BOOL` → `bool` | 直接传递（i1 ↔ i8） |

| .ploy → Python | 规则 |
|----------------|------|
| `INT` → `int` | 转换为 `PyLong` / 从 `PyLong` 转换 |
| `FLOAT` → `float` | 转换为 `PyFloat` / 从 `PyFloat` 转换 |
| `STRING` → `str` | 转换为 `PyUnicode` / 从 `PyUnicode` 转换 |
| `BOOL` → `bool` | 转换为 `PyBool` / 从 `PyBool` 转换 |

## 5.2 容器类型编组

| .ploy 类型 | 编组策略 |
|-----------|---------|
| `LIST<T>` | 逐元素复制并进行类型转换 |
| `TUPLE<T...>` | 按位置逐元素复制 |
| `DICT<K,V>` | 键值对迭代和转换 |
| `ARRAY<T,N>` | 直接内存复制（相同元素类型）或逐元素转换 |

## 5.3 对象编组

通过 `NEW` 创建的对象使用不透明句柄管理：

- 每个对象由运行时使用唯一句柄 ID 追踪
- 方法调用通过运行时桥接使用句柄进行
- 对象销毁（`DELETE`）释放句柄并调用析构函数/终结器

---

# 6. 二进制输出格式

## 6.1 编译管道

```
源码 → 前端 → IR → 优化 → 后端 → 汇编 → 二进制
```

## 6.2 输出产物

| 文件 | 格式 | 说明 |
|------|------|------|
| `*.ir` | 文本 | 可读的 IR 表示 |
| `*.ir.bin` | 二进制 | 序列化 IR（二进制格式） |
| `*.asm` | 文本 | 目标特定的汇编 |
| `*.asm.bin` | 二进制 | 编码后的汇编 |
| `*.o` / `*.obj` | 目标文件 | 可重定位目标文件 |
| `*.exe` / (无扩展名) | 可执行文件 | 最终链接的二进制 |

## 6.3 辅助目录

编译过程中，`polyc` 在 `aux/` 子目录中生成中间产物：

```
aux/
├── <文件名>.ir         # IR 文本（如指定 --emit-ir）
├── <文件名>.ir.bin     # 二进制编码的 IR
├── <文件名>.asm        # 生成的汇编
├── <文件名>.asm.bin    # 二进制编码的汇编
├── <文件名>.obj        # 目标文件
└── <文件名>.symbols    # 符号表转储
```

## 6.4 目标架构

| 架构 | 指令集 | 状态 |
|------|--------|------|
| x86_64 | SSE2/AVX/AVX2/AVX-512 | ✅ |
| ARM64 (AArch64) | NEON/SVE | ✅ |
| WebAssembly | MVP + SIMD | 🔧 计划中 |
