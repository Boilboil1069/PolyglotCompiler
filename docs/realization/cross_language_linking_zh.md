# 跨语言函数级别链接 — 实现细节

## 1. 架构概述

跨语言函数级别链接系统实现为一个新的前端 (`frontend_ploy`)，它通过标准编译器管道处理 `.ploy` 文件：词法器 → 解析器 → 语义分析 → 降级。降级阶段生成包含跨语言调用节点的多语言 IR，然后由链接器将其解析为具体的粘合代码。

> **核心思想：** 开发者在 `.ploy` 文件中声明"哪个语言的哪个函数连接另一个语言的哪个函数"，编译器自动生成所有的 FFI 绑定、类型转换和调用约定适配代码。

### 1.1 组件映射

```
frontends/ploy/
├── include/
│   ├── ploy_lexer.h          # 词法器（继承 LexerBase）
│   ├── ploy_ast.h             # AST 节点定义
│   ├── ploy_parser.h          # 解析器（继承 ParserBase）
│   ├── ploy_sema.h            # 语义分析器（含 ForeignClassSchema、class_schemas_）
│   └── ploy_lowering.h        # IR 降级器
└── src/
    ├── lexer/
    │   └── lexer.cpp          # 词法器实现：54 个关键字、运算符、字面量、注释
    ├── parser/
    │   └── parser.cpp         # 解析器实现：递归下降，约 1380 行
    ├── sema/
    │   └── sema.cpp           # 语义分析：参数数量、返回类型、ABI schema 验证
    └── lowering/
        └── lowering.cpp       # 降级实现：WITH 语句结构化异常安全

runtime/                        # 运行时支持
└── include/interop/
    └── container_marshal.h    # 容器编组头文件
└── src/interop/
    └── container_marshal.cpp  # 容器编组实现

linker（扩展）:                  # 链接器扩展
└── tools/polyld/
    ├── include/
    │   └── polyglot_linker.h  # 跨语言链接解析器（ABIDescriptor、ValidateABICompatibility）
    └── src/
        └── polyglot_linker.cpp # 链接解析与粘合代码生成
```

> **文件职责一览：**
> - `lexer.cpp`：将源代码字符流分解为 Token 流（关键字、标识符、数字、字符串、符号）
> - `parser.cpp`：将 Token 流构建为抽象语法树（AST）
> - `sema.cpp`：对 AST 进行语义检查（类型检查、符号解析、链接验证）
> - `lowering.cpp`：将检查后的 AST 转换为 IR（中间表示）指令

### 1.2 集成点

| 模块 | 使用内容 | 说明 |
|------|---------|------|
| `polyglot_common` | `core::Type`、`core::TypeSystem`、`core::SourceLoc` | 统一的类型系统和源代码位置追踪 |
| `frontend_common` | `LexerBase`、`ParserBase`、`Diagnostics`、`Token` | 前端基础设施，提供 Token 定义和错误报告 |
| `middle_ir` | `ir::IRContext`、`ir::Function`、`ir::IRBuilder` | IR 构建工具，生成函数、基本块和指令 |
| `runtime::interop` | `FFIRegistry`、`TypeMapping`、`ContainerMarshal` | 运行时跨语言调用支持 |
| `tools/polyld` | `ABIDescriptor`、`ValidateABICompatibility()` | 链接阶段 ABI 兼容性验证，在 `ResolveSymbolPair()` 中调用 |

## 2. 词法器设计

`PloyLexer` 扩展了 `frontends::LexerBase`，将 `.ploy` 源码标记化为标准 `Token` 流。它识别所有 41 个 `.ploy` 关键字、运算符、字面量和注释。

> **词法器特性：**
> - 支持 `//` 单行注释和 `/* */` 多行注释
> - 支持十六进制 (`0xFF`)、二进制 (`0b1010`)、八进制 (`0o77`) 整数字面量
> - 支持科学记数法浮点数 (`1.0e-5`)
> - 支持字符串转义 (`\"`, `\\`, `\n` 等)
> - `::` 和 `->` 作为多字符符号被识别为单个 Token

### 2.1 Token 分类

| Token 类型          | 示例                                         | 说明 |
|--------------------|----------------------------------------------|------|
| `kKeyword`         | LINK, IMPORT, FUNC, IF, PACKAGE, MAP_FUNC    | 41 个保留关键字 |
| `kIdentifier`      | my_func, data_loader, math_utils             | 用户定义的名称 |
| `kNumber`          | 42, 3.14, 0xFF                               | 整数和浮点数 |
| `kString`          | "hello", "path/to/file"                      | 字符串字面量 |
| `kSymbol`          | (, ), {, }, ;, ::, ->, ==, !=, [, ] 等       | 运算符和标点 |
| `kComment`         | // 行注释, /* 块注释 */                        | 注释（被解析器忽略） |
| `kEndOfFile`       | 输入结束                                      | 标记文件末尾 |

## 3. AST 设计

AST 遵循与 C++ 和 Python 前端相同的模式，使用 `AstNode`、`Statement` 和 `Expression` 基类型。

### 3.1 基础 AST 节点

| 节点类型 | 语法形式 | 字段 | 说明 |
|---------|---------|------|------|
| `LinkDecl` | `LINK(a, b, c, d);` | target_lang, source_lang, target_func, source_func, body, link_kind | 跨语言函数/变量/结构体链接 |
| `ImportDecl` | `IMPORT ...;` | module_path, alias, language, package_name | 模块/包导入 |
| `ExportDecl` | `EXPORT ...;` | symbol_name, external_name | 函数导出 |
| `MapTypeDecl` | `MAP_TYPE(a, b);` | source_type, target_type | 类型映射 |
| `PipelineDecl` | `PIPELINE name { ... }` | name, stages | 多阶段处理管道 |
| `FuncDecl` | `FUNC name(...) -> type { ... }` | name, params, return_type, body | 函数定义 |
| `StructDecl` | `STRUCT name { ... }` | name, fields | 结构体定义 |
| `MapFuncDecl` | `MAP_FUNC name(...) -> type { ... }` | name, params, return_type, body | 类型转换函数 |
| `CallExpr` | `CALL(lang, func, args...)` | language, function, arguments | 跨语言调用 |
| `VarDecl` | `LET x = expr;` | name, type, initializer, is_mutable | 变量声明 |

### 3.2 复杂类型 AST 节点

| 节点类型 | 语法形式 | 说明 |
|---------|---------|------|
| `ListLiteral` | `[expr1, expr2, ...]` | 列表字面量，存储表达式数组 |
| `TupleLiteral` | `(expr1, expr2)` | 元组字面量，存储表达式数组 |
| `StructLiteral` | `Name { f1: v1, f2: v2 }` | 结构体字面量，存储字段名-值对 |
| `ConvertExpression` | `CONVERT(expr, type)` | 类型转换表达式 |

## 4. 解析器设计

解析器是递归下降解析器，约 1380 行代码，处理所有 `.ploy` 语法构造。

### 4.1 LINK 解析

> **LINK 是最复杂的解析构造。** 解析器需要处理四种变体：

```
LINK '(' target ',' source ',' target_func ',' source_func ')'
    ( ';'                                        // 变体1：简单链接
    | '{' (MAP_TYPE(a, b) ';')* '}'              // 变体2：带类型映射体
    | 'AS' 'VAR' ';'                             // 变体3：变量链接
    | 'AS' 'STRUCT' '{' (MAP_TYPE ';')* '}'      // 变体4：结构体链接
    )
```

> **解析流程：**
> 1. 消费 `LINK` 关键字和 `(`
> 2. 解析4个逗号分隔的参数（每个可以是 `language::module::function` 形式）
> 3. 消费 `)`
> 4. 根据下一个 Token 判断变体：`;` → 简单链接、`{` → 带体链接、`AS` → 变量/结构体链接

### 4.2 IMPORT 解析

> **IMPORT 有三种形式：**

| 形式 | 语法 | 判断条件 |
|------|------|---------|
| 路径导入 | `IMPORT "path" AS alias;` | 下一个 Token 是字符串 |
| 限定导入 | `IMPORT lang::module;` | 下一个 Token 是标识符后跟 `::` |
| 包导入 | `IMPORT lang PACKAGE pkg [AS alias];` | 标识符后跟 `PACKAGE` 关键字 |

> **包导入的特殊处理：** 包名支持 `.` 分隔的路径（如 `numpy.linalg`），解析器会将整个路径连接为一个字符串。

### 4.3 MAP_TYPE 解析

```
MAP_TYPE '(' qualified_type ',' qualified_type ')' ';'
```

> **说明：** 两个参数都是限定类型名（如 `cpp::int`、`python::float`），用逗号分隔，整体以分号结尾。

## 5. 语义分析

语义分析阶段执行以下验证和处理：

| 步骤 | 内容 | 说明 |
|------|------|------|
| 1. 符号解析 | 解析所有标识符到其声明 | 包括跨模块引用和包引用 |
| 2. 语言验证 | 验证 LINK/IMPORT 的语言标识符 | 支持：`cpp`、`python`、`rust`、`c`、`ploy` |
| 3. 类型检查 | 验证链接函数签名兼容性 | 考虑 MAP_TYPE 定义的转换 |
| 4. 链接验证 | 验证 LINK 目标/源的有效性 | 存在 MAP_TYPE 时设置 `param_count_known` 和 `validated` 标志 |
| 5. 类型映射验证 | 验证 MAP_TYPE 转换的有效性 | 确保类型可以安全转换 |
| 6. 控制流验证 | 验证 BREAK/CONTINUE 在循环内 | 检查所有路径返回值 |
| 7. 结构体验证 | 验证字段不重复、类型有效 | 注册到符号表作为类型 |
| 8. MAP_FUNC 验证 | 验证参数和返回类型 | 注册到转换函数表 |
| 9. 包导入验证 | 验证 PACKAGE 的语言有效 | 确保语言标识符合法 |
| 10. 类模式验证 | `NEW`/`METHOD`/`GET`/`SET` 从 `ForeignClassSchema` 解析类型 | 匹配时检查参数数量、字段类型、返回类型；严格模式对未解析类型报错 |

## 6. 降级设计

降级阶段将经过类型检查的 AST 转换为多语言 IR：

| AST 节点 | IR 产物 | 说明 |
|---------|--------|------|
| `LINK` 指令 | `CrossLanguageCallStub` | 包含源/目标语言、函数符号、编组描述符 |
| `CALL` 表达式 | `ir::CallInstruction` + 元数据 | 带有目标语言信息的调用指令 |
| `PIPELINE` 块 | 调用序列 + 类型转换 | 阶段间自动插入转换代码 |
| 控制流 | `ir::Branch`/`ir::CondBranch` | 使用 IRBuilder 生成标准块 |
| `IMPORT` 声明 | `__ploy_module_<lang>_<path>` 全局引用 | 外部模块的引用符号 |
| `STRUCT` 声明 | IR 上下文中的结构体元数据 | 不生成代码，仅注册类型 |
| `MAP_FUNC` 声明 | `__ploy_mapfunc_<name>` IR 函数 | 包含转换逻辑的完整函数 |
| 容器字面量 | 运行时分配 + 元素存储 | 调用 `__ploy_rt_list_create` 等 |
| `CONVERT` 表达式 | `__ploy_convert_<type>` 调用 | 运行时类型转换 |
| `WITH` 语句 | body/finally/exit 三区块结构 | 结构化异常安全：确保即使异常传播也调用 `__exit__` |

## 7. 跨语言链接解析

`PolyglotLinker` 扩展了现有的 `Linker`：

### 7.1 粘合代码生成

对于每个 `LINK` 指令，链接器生成一个**包装函数**：

```
1. 接收目标语言格式的参数
2. 将参数从目标类型编组为源类型（使用 MAP_TYPE 规则）
3. 通过 FFI 调用源语言的函数
4. 将返回值从源类型编组回目标类型
5. 返回给调用者
```

> **示例：** 对于 `LINK(cpp, python, math::add, utils::get)`，链接器生成一个 C++ 可调用的包装函数，内部调用 Python 的 `utils.get()` 并自动转换参数和返回值类型。

### 7.2 类型编组

| 转换类型               | 策略                                          |
|-----------------------|-----------------------------------------------|
| int → int（相同大小）   | 直接复制（零开销）                               |
| int → float           | 类型转换指令                                    |
| string → string       | UTF-8 编码标准化                                |
| array → array         | 逐元素复制 + 元素类型转换                         |
| struct → struct       | 逐字段编组                                      |
| pointer → handle      | ForeignHandle 包装 + 所有权追踪                  |
| list → list           | 通过 RuntimeList 中间表示转换                     |
| dict → dict           | 通过 RuntimeDict 中间表示转换                     |
| tuple → tuple         | 通过 RuntimeTuple 中间表示转换                    |

### 7.3 符号解析

链接器通过以下步骤解析跨语言符号：
1. 从所有源语言加载目标文件
2. 解混淆（demangle）符号以找到目标函数
3. 生成连接不同调用约定的桥接符号

### 7.4 ABI 兼容性验证

链接阶段新增：`ResolveSymbolPair()` 在生成粘合代码前调用 `ValidateABICompatibility()`。验证内容包括：

| 检查项 | 说明 |
|-------|------|
| **参数数量** | 源函数和目标函数必须声明相同数量的参数 |
| **指针兼容性** | 双方在每个参数位置上必须一致（指针 vs 值类型） |
| **调用约定** | `ABIDescriptor` 根据平台和语言解析 SysV AMD64 / Win64 / AAPCS64 |
| **返回大小** | 非 void 返回值在编组前验证大小兼容性 |

`ABIDescriptor` 由 `GetABIDescriptor(language, symbol)` 返回，包含约定标签、参数类别和目标平台的 shadow-space 需求。

## 8. 运行时支持

运行时跨语言调用经过以下组件：

| 组件 | 类 | 说明 |
|------|---|------|
| FFI 注册表 | `runtime::interop::FFIRegistry` | 外部函数的中央查找表，管理所有注册的跨语言函数 |
| 所有权追踪器 | `runtime::interop::OwnershipTracker` | 确保安全的跨语言资源管理，防止内存泄漏和悬空引用 |
| 动态库加载器 | `runtime::interop::DynamicLibrary` | 在运行时加载已编译的语言模块（.so/.dll/.dylib） |
| 容器编组器 | `runtime::interop::ContainerMarshal` | 在不同语言的容器类型之间进行转换 |

### 8.1 运行时容器操作

| 函数                              | 说明                      |
|----------------------------------|---------------------------|
| `__ploy_rt_list_create`          | 创建运行时列表              |
| `__ploy_rt_list_push`           | 向列表追加元素              |
| `__ploy_rt_list_get`            | 按索引获取列表元素           |
| `__ploy_rt_list_len`            | 获取列表长度                |
| `__ploy_rt_list_free`           | 释放列表                   |
| `__ploy_rt_tuple_create`        | 创建运行时元组              |
| `__ploy_rt_tuple_get`           | 按索引获取元组元素           |
| `__ploy_rt_tuple_free`          | 释放元组                   |
| `__ploy_rt_dict_create`         | 创建运行时字典              |
| `__ploy_rt_dict_insert`         | 向字典插入键值对            |
| `__ploy_rt_dict_lookup`         | 按键查找字典值              |
| `__ploy_rt_dict_free`           | 释放字典                   |

### 8.2 跨语言容器转换

| 函数                                    | 说明                          |
|----------------------------------------|-------------------------------|
| `__ploy_rt_convert_list_to_pylist`     | RuntimeList → Python list     |
| `__ploy_rt_convert_pylist_to_list`     | Python list → RuntimeList     |
| `__ploy_rt_convert_dict_to_pydict`     | RuntimeDict → Python dict     |
| `__ploy_rt_convert_pydict_to_dict`     | Python dict → RuntimeDict     |
| `__ploy_rt_convert_vec_to_list`        | Rust Vec → RuntimeList        |
| `__ploy_rt_convert_list_to_vec`        | RuntimeList → Rust Vec        |

## 9. 错误处理

所有错误通过 `frontends::Diagnostics` 报告，包含精确的源代码位置信息：

| 阶段 | 错误类型 | 示例 |
|------|---------|------|
| 词法器 | 无效 Token、未终止字符串 | `error: unterminated string literal` |
| 解析器 | 语法错误 | `error: expected ';' after LINK declaration` |
| 语义分析 | 类型不匹配 | `error: unknown language 'java' in IMPORT` |
| 语义分析 | 未定义引用 | `error: EXPORT references undefined symbol 'foo'` |
| 语义分析 | 重复定义 | `error: redefinition of symbol 'x'` |
| 链接器 | 未解析符号 | `error: unresolved cross-language symbol` |
