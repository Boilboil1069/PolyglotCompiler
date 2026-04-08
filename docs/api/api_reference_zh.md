# PolyglotCompiler API 参考手册

> **版本**: 3.0.0  
> **最后更新**: 2026-02-22  

---

## 目录

1. [核心类型系统](#1-核心类型系统)
2. [符号表](#2-符号表)
3. [前端公共组件](#3-前端公共组件)
4. [IR 节点](#4-ir-节点)
5. [IR 上下文](#5-ir-上下文)
6. [IR 工具](#6-ir-工具)
7. [前端 API](#7-前端-api)
8. [诊断系统](#8-诊断系统)

---

# 1. 核心类型系统

**头文件**: `common/include/core/types.h`  
**命名空间**: `polyglot::core`

## 1.1 TypeKind 枚举

统一类型系统中所有支持的类型种类。

```cpp
enum class TypeKind {
  kInvalid,        // 无效 / 未解析的类型
  kVoid,           // 空返回类型
  kBool,           // 布尔类型
  kInt,            // 整数类型（可附带位宽 / 符号信息）
  kFloat,          // 浮点类型（可附带位宽）
  kString,         // 字符串类型
  kPointer,        // 指针类型
  kFunction,       // 函数类型（返回类型 + 参数列表）
  kReference,      // 引用类型（C++ / Rust）
  kClass,          // 类类型
  kModule,         // 模块类型
  kAny,            // 动态 / any 类型
  kStruct,         // 结构体类型
  kUnion,          // 联合体类型
  kEnum,           // 枚举类型
  kTuple,          // 元组类型
  kGenericParam,   // 未解析的泛型参数（如 T）
  kGenericInstance, // 已实例化的泛型（如 Vec<i32>）
  kArray,          // 固定大小或动态数组
  kOptional,       // 可选 / 可空类型
  kSlice,          // 切片类型（Rust 风格）
};
```

## 1.2 Type 结构体

用于所有支持的源语言的统一类型表示。

### 字段

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `kind` | `TypeKind` | `kInvalid` | 类型种类 |
| `name` | `std::string` | `""` | 可读的类型名称 |
| `language` | `std::string` | `""` | 可选的语言标记（如 `"python"`、`"cpp"`） |
| `type_args` | `std::vector<Type>` | `{}` | 泛型、元组和复合类型的类型参数 |
| `lifetime` | `std::string` | `""` | 借用生命周期标注（Rust 风格） |
| `is_const` | `bool` | `false` | const 限定符 |
| `is_volatile` | `bool` | `false` | volatile 限定符 |
| `is_rvalue_ref` | `bool` | `false` | 右值引用限定符（C++ `&&`） |
| `bit_width` | `int` | `0` | 位宽（8/16/32/64/128）；0 表示未指定 |
| `is_signed` | `bool` | `true` | 整数类型的符号标志 |
| `array_size` | `size_t` | `0` | 固定大小数组的元素数；0 表示动态 |

### 工厂方法

```cpp
static Type Invalid();          // 创建无效类型标记
static Type Void();             // 创建 void 类型
static Type Bool();             // 创建布尔类型
static Type Int();              // 创建默认整数类型
static Type Float();            // 创建默认浮点类型
static Type String();           // 创建字符串类型
static Type Any();              // 创建动态/any 类型

static Type Int(int bits, bool sign);    // 指定位宽和符号的整数
static Type Float(int bits);             // 指定位宽的浮点数（32 或 64）

static Type Pointer(const Type& pointee);
static Type Reference(const Type& pointee);
static Type RValueRef(const Type& pointee);
static Type Function(const Type& ret, const std::vector<Type>& params);
static Type Tuple(const std::vector<Type>& elems);
static Type GenericParam(const std::string& name);
static Type GenericInstance(const std::string& name, const std::vector<Type>& args);
static Type Struct(const std::string& name, const std::string& lang = "");
static Type Array(const Type& elem, size_t size = 0);
static Type Optional(const Type& inner);
static Type Slice(const Type& elem);
static Type Class(const std::string& name, const std::string& lang = "");
```

### 比较运算

```cpp
bool operator==(const Type& other) const;
bool operator!=(const Type& other) const;
```

## 1.3 TypeSystem 类

中心化的类型映射、兼容性检查和大小计算。

### 主要方法

```cpp
// 将特定语言的类型名称映射为统一的 Type 表示
// 支持的语言: "python", "cpp", "rust", "java", "dotnet", "ploy"
Type MapFromLanguage(const std::string& lang, const std::string& type_name) const;

// 检查 from 类型是否可以隐式转换为 to 类型
bool IsImplicitlyConvertible(const Type& from, const Type& to) const;

// 计算给定类型的字节大小和对齐要求
size_t SizeOf(const Type& t) const;
size_t AlignOf(const Type& t) const;
```

## 1.4 TypeUnifier 类

Hindley-Milner 风格的类型统一器，用于解析泛型类型参数。

```cpp
bool Unify(const Type& a, const Type& b);           // 尝试统一两个类型
Type Resolve(const Type& t) const;                   // 通过替换解析类型
void Reset();                                        // 清空所有替换
```

## 1.5 TypeRegistry 类

命名类型注册和跨语言等价性追踪。

```cpp
void Register(const std::string& name, const Type& type);
std::optional<Type> Lookup(const std::string& name) const;
void RegisterEquivalence(const std::string& name_a, const std::string& name_b);
bool AreEquivalent(const std::string& name_a, const std::string& name_b) const;
```

---

# 2. 符号表

**头文件**: `common/include/core/symbols.h`  
**命名空间**: `polyglot::core`

## 2.1 SymbolKind / ScopeKind 枚举

```cpp
enum class SymbolKind { kVariable, kFunction, kTypeName, kModule, kParameter, kField };
enum class ScopeKind  { kGlobal, kModule, kFunction, kClass, kBlock, kComprehension };
```

## 2.2 Symbol 结构体

| 字段 | 类型 | 说明 |
|------|------|------|
| `name` | `std::string` | 符号名称 |
| `type` | `Type` | 关联类型 |
| `loc` | `SourceLoc` | 声明位置 |
| `kind` | `SymbolKind` | 符号种类 |
| `language` | `std::string` | 来源语言 |
| `scope_id` | `int` | 所属作用域标识符 |
| `captured` | `bool` | 是否被闭包捕获 |
| `access` | `std::string` | 访问修饰符（`"public"`、`"protected"`、`"private"` 或空） |

## 2.3 SymbolTable 类

> **实现说明**: 内部使用 `std::deque<Symbol>` 来保证指针稳定性。

### 作用域管理

```cpp
int EnterScope(const std::string& name, ScopeKind kind);  // 进入新作用域；返回作用域 id
void ExitScope();                                           // 退出当前作用域
```

### 声明

```cpp
const Symbol* Declare(const Symbol& symbol);                     // 在当前作用域声明
const Symbol* DeclareInScope(int scope_id, const Symbol& symbol); // 在指定作用域声明
```

### 查找

```cpp
struct ResolveResult {
  const Symbol* symbol;
  int scope_distance;     // 0 = 同一作用域，1 = 父作用域，以此类推
};

std::optional<ResolveResult> Lookup(const std::string& name) const;

// 基于类型评分的重载函数解析
std::optional<ResolveResult> ResolveFunction(
    const std::string& name,
    const std::vector<Type>& arg_types,
    const TypeSystem& types) const;
```

### 访问器

```cpp
int CurrentScope() const;
const std::vector<ScopeInfo>& Scopes() const;
size_t SymbolCount() const;
```

---

# 3. 前端公共组件

**头文件**: `frontends/common/include/`  
**命名空间**: `polyglot::frontends`

## 3.1 Token 和 TokenKind

**头文件**: `frontends/common/include/lexer_base.h`

```cpp
enum class TokenKind {
  kEndOfFile,      // 输入结束
  kIdentifier,     // 标识符（变量名等）
  kNumber,         // 数字字面量
  kString,         // 字符串字面量
  kChar,           // 字符字面量
  kLifetime,       // Rust 生命周期标注（'a）
  kKeyword,        // 语言关键字
  kSymbol,         // 运算符和标点
  kPreprocessor,   // 预处理指令（#include 等）
  kComment,        // 注释
  kNewline,        // 有意义的换行符（Python）
  kIndent,         // 缩进增加（Python）
  kDedent,         // 缩进减少（Python）
  kUnknown         // 无法识别的词法单元
};

struct Token {
  TokenKind kind;
  std::string lexeme;   // 文本表示
  core::SourceLoc loc;  // 源文件位置
  bool is_doc;          // 是否为文档注释
};
```

## 3.2 LexerBase 类

所有语言特定词法分析器的抽象基类。

```cpp
class LexerBase {
public:
  virtual ~LexerBase() = default;
  virtual Token NextToken() = 0;

  // 保存/恢复状态用于解析器前瞻
  struct LexerState { size_t position, line, column; };
  LexerState SaveState() const;
  void RestoreState(const LexerState& state);

protected:
  LexerBase(std::string source, std::string file);

  char Peek() const;       // 查看当前字符但不推进
  char PeekNext() const;   // 查看下一个字符
  char Get();              // 消耗当前字符并推进
  core::SourceLoc CurrentLoc() const;
  bool Eof() const;
  void SetTabWidth(size_t width);

  std::string source_;     // 源码文本
  std::string file_;       // 文件路径
  size_t position_, line_, column_, tab_width_;
};
```

## 3.3 ParserBase 类

**头文件**: `frontends/common/include/parser_base.h`

```cpp
class ParserBase {
public:
  explicit ParserBase(Diagnostics& diagnostics);
  virtual ~ParserBase() = default;
  virtual void ParseModule() = 0;

protected:
  Diagnostics& diagnostics_;
};
```

## 3.4 SemaContext 类

**头文件**: `frontends/common/include/sema_context.h`

所有前端共用的语义分析上下文。

```cpp
class SemaContext {
public:
  explicit SemaContext(Diagnostics& diagnostics);

  core::SymbolTable& Symbols();  // 符号表访问
  core::TypeSystem&  Types();    // 类型系统访问
  Diagnostics&       Diags();    // 诊断信息访问
};
```

---

# 4. IR 节点

**头文件**: `middle/include/ir/nodes/`  
**命名空间**: `polyglot::ir`

## 4.1 IRType

**头文件**: `middle/include/ir/nodes/types.h`

### IRTypeKind 枚举

```cpp
enum class IRTypeKind {
  kInvalid, kI1, kI8, kI16, kI32, kI64,
  kF32, kF64, kVoid, kPointer, kReference,
  kArray, kVector, kStruct, kFunction
};
```

### IRType 工厂方法

```cpp
static IRType Invalid();
static IRType I1();
static IRType I8(bool is_signed = true);
static IRType I16(bool is_signed = true);
static IRType I32(bool is_signed = true);
static IRType I64(bool is_signed = true);
static IRType F32();
static IRType F64();
static IRType Void();

static IRType Pointer(const IRType& pointee);
static IRType Reference(const IRType& pointee);
static IRType Array(const IRType& elem, size_t n);
static IRType Vector(const IRType& elem, size_t lanes);
static IRType Struct(std::string name, std::vector<IRType> fields);
static IRType Function(const IRType& ret, const std::vector<IRType>& params);
```

## 4.2 Value（基类）

```cpp
struct Value {
  virtual ~Value() = default;
  IRType type;         // IR 类型
  std::string name;    // SSA 名称
};
```

## 4.3 字面量和常量值

| 类 | 说明 |
|----|------|
| `LiteralExpression` | 整数或浮点数字面量 |
| `UndefValue` | 未初始化 / 未定义占位符 |
| `GlobalValue` | 全局变量或常量数据 |
| `ConstantString` | 字符串常量（默认以空字符结尾） |
| `ConstantArray` | 常量值数组 |
| `ConstantStruct` | 具有常量字段的结构体 |

## 4.4 指令

所有指令继承自 `Instruction`，而 `Instruction` 继承自 `Value`。

| 指令 | 关键字段 | 说明 |
|------|---------|------|
| `BinaryInstruction` | `Op op` | 算术、位运算、比较操作 |
| `PhiInstruction` | `incomings` | SSA phi 节点，用于合并前驱块的值 |
| `AssignInstruction` | — | SSA 转换前的变量赋值 |
| `CallInstruction` | `callee`, `is_indirect`, `is_tail_call` | 函数调用 |
| `AllocaInstruction` | `no_escape` | 栈分配 |
| `LoadInstruction` | `align` | 内存加载 |
| `StoreInstruction` | `align` | 内存存储 |
| `CastInstruction` | `CastKind cast` | 类型转换 |
| `GetElementPtrInstruction` | `source_type`, `indices`, `inbounds` | 地址计算 |
| `MemcpyInstruction` | `align` | 内存复制 |
| `MemsetInstruction` | `align` | 内存填充 |
| `ReturnInstruction` | — | 函数返回（终结指令） |
| `BranchInstruction` | `condition`, `true_target`, `false_target` | 条件/无条件跳转（终结指令） |
| `SwitchInstruction` | `cases`, `default_target` | 多路分支（终结指令） |

### BinaryInstruction::Op 枚举

```cpp
enum class Op {
  // 整数算术
  kAdd, kSub, kMul, kDiv, kSDiv, kUDiv, kSRem, kURem, kRem,
  // 浮点算术
  kFAdd, kFSub, kFMul, kFDiv, kFRem,
  // 位运算
  kAnd, kOr, kXor, kShl, kLShr, kAShr,
  // 整数比较
  kCmpEq, kCmpNe, kCmpUlt, kCmpUle, kCmpUgt, kCmpUge,
  kCmpSlt, kCmpSle, kCmpSgt, kCmpSge, kCmpLt,
  // 浮点比较
  kCmpFoe, kCmpFne, kCmpFlt, kCmpFle, kCmpFgt, kCmpFge
};
```

### CastInstruction::CastKind 枚举

```cpp
enum class CastKind {
  kZExt,      // 零扩展
  kSExt,      // 符号扩展
  kTrunc,     // 截断
  kBitcast,   // 位转换
  kFpExt,     // 浮点扩展
  kFpTrunc,   // 浮点截断
  kIntToPtr,  // 整数到指针
  kPtrToInt   // 指针到整数
};
```

## 4.5 BasicBlock 和 Function

**头文件**: `middle/include/ir/nodes/statements.h`

```cpp
struct BasicBlock {
  std::string label;                                    // 基本块标签
  std::vector<std::shared_ptr<Instruction>> instructions; // 指令列表
  Function* parent;                                     // 所属函数
  std::vector<BasicBlock*> predecessors;                 // 前驱块
  std::vector<BasicBlock*> successors;                   // 后继块
};

struct Function {
  std::string name;                                     // 函数名
  IRType return_type;                                   // 返回类型
  std::vector<std::pair<std::string, IRType>> params;   // 参数列表
  std::vector<std::shared_ptr<BasicBlock>> blocks;      // 基本块列表
  bool is_declaration;                                  // 是否为外部声明（无函数体）
  bool is_vararg;                                       // 是否为可变参数
  std::string calling_conv;                             // 调用约定
  std::string linkage;                                  // 链接属性
};
```

---

# 5. IR 上下文

**头文件**: `middle/include/ir/ir_context.h`  
**命名空间**: `polyglot::ir`

```cpp
class IRContext {
public:
  explicit IRContext(DataLayout::Arch arch = DataLayout::Arch::kX86_64);

  // 函数创建
  std::shared_ptr<Function> CreateFunction(const std::string& name);
  std::shared_ptr<Function> CreateFunction(
      const std::string& name, const IRType& ret,
      const std::vector<std::pair<std::string, IRType>>& params);

  // 全局变量创建
  std::shared_ptr<GlobalValue> CreateGlobal(
      const std::string& name, const IRType& type,
      bool is_const = false, const std::string& init = "",
      std::shared_ptr<Value> initializer = nullptr);

  // 便捷访问器
  std::shared_ptr<Function> DefaultFunction();
  std::shared_ptr<BasicBlock> DefaultBlock();
  void AddStatement(const std::shared_ptr<Statement>& stmt);

  // 查询
  const std::vector<std::shared_ptr<Function>>& Functions() const;
  const std::vector<std::shared_ptr<GlobalValue>>& Globals() const;
  const DataLayout& Layout() const;
  DataLayout& Layout();

  // 方言注册
  void RegisterDialectByName(const std::string& name);
  template <typename Dialect> void RegisterDialect();
  const std::vector<std::string>& Dialects() const;
};
```

---

# 6. IR 工具

## 6.1 IR 打印器

**头文件**: `common/include/ir/ir_printer.h`  
**命名空间**: `polyglot::ir`

```cpp
void PrintFunction(const Function& func, std::ostream& os);  // 打印单个函数的 IR
void PrintModule(const IRContext& ctx, std::ostream& os);     // 打印整个模块的 IR
std::string Dump(const Function& func);                       // 将函数 IR 转储为字符串
```

## 6.2 IR 构建器

**头文件**: `common/include/ir/ir_builder.h`

提供流畅的 API，用于在 `BasicBlock` 中构建 IR 指令。

## 6.3 IR 解析器

**头文件**: `common/include/ir/ir_parser.h`

将文本 IR 表示解析回 `IRContext`。

## 6.4 IR 访问器

**头文件**: `common/include/ir/ir_visitor.h`

用于遍历 IR 结构（指令、基本块、函数）的基础访问器类。

---

# 7. 前端 API

每个语言前端都暴露相同的四阶段管道：

| 阶段 | 函数 | 输入 | 输出 |
|------|------|------|------|
| **词法分析** | `NextToken()` | 源码文本 | `Token` 流 |
| **语法分析** | `ParseModule()` | Token 流 | 语言特定的 AST |
| **语义分析** | `AnalyzeModule()` | AST + `SemaContext` | 经过验证的 AST + 符号表 |
| **IR 降级** | `LowerToIR()` | AST + `IRContext` | IR 函数和全局变量 |

## 7.1 C++ 前端

**命名空间**: `polyglot::frontends::cpp`

| 头文件 | 类 / 函数 |
|--------|----------|
| `frontends/cpp/include/cpp_lexer.h` | `CppLexer : LexerBase` |
| `frontends/cpp/include/cpp_parser.h` | `CppParser : ParserBase` |
| `frontends/cpp/include/cpp_sema.h` | `void AnalyzeModule(const CppModule&, SemaContext&)` |
| `frontends/cpp/include/cpp_lowering.h` | `void LowerToIR(const CppModule&, ir::IRContext&, Diagnostics&)` |

## 7.2 Python 前端

**命名空间**: `polyglot::frontends::python`

| 头文件 | 类 / 函数 |
|--------|----------|
| `frontends/python/include/python_lexer.h` | `PythonLexer : LexerBase` |
| `frontends/python/include/python_parser.h` | `PythonParser : ParserBase` |
| `frontends/python/include/python_sema.h` | `void AnalyzeModule(const PythonModule&, SemaContext&)` |
| `frontends/python/include/python_lowering.h` | `void LowerToIR(const PythonModule&, ir::IRContext&, Diagnostics&)` |

## 7.3 Rust 前端

**命名空间**: `polyglot::frontends::rust`

| 头文件 | 类 / 函数 |
|--------|----------|
| `frontends/rust/include/rust_lexer.h` | `RustLexer : LexerBase` |
| `frontends/rust/include/rust_parser.h` | `RustParser : ParserBase` |
| `frontends/rust/include/rust_sema.h` | `void AnalyzeModule(const RustModule&, SemaContext&)` |
| `frontends/rust/include/rust_lowering.h` | `void LowerToIR(const RustModule&, ir::IRContext&, Diagnostics&)` |

## 7.4 Java 前端

**命名空间**: `polyglot::frontends::java`

| 头文件 | 类 / 函数 |
|--------|----------|
| `frontends/java/include/java_lexer.h` | `JavaLexer : LexerBase` |
| `frontends/java/include/java_parser.h` | `JavaParser : ParserBase` |
| `frontends/java/include/java_sema.h` | `void AnalyzeModule(const Module&, SemaContext&)` |
| `frontends/java/include/java_lowering.h` | `void LowerToIR(const Module&, ir::IRContext&, Diagnostics&)` |

**支持的版本**: Java 8, 17, 21, 23

## 7.5 .NET 前端

**命名空间**: `polyglot::frontends::dotnet`

| 头文件 | 类 / 函数 |
|--------|----------|
| `frontends/dotnet/include/dotnet_lexer.h` | `DotnetLexer : LexerBase` |
| `frontends/dotnet/include/dotnet_parser.h` | `DotnetParser : ParserBase` |
| `frontends/dotnet/include/dotnet_sema.h` | `void AnalyzeModule(const Module&, SemaContext&)` |
| `frontends/dotnet/include/dotnet_lowering.h` | `void LowerToIR(const Module&, ir::IRContext&, Diagnostics&)` |

**支持的版本**: .NET 6, 7, 8, 9

## 7.6 .ploy 前端

**命名空间**: `polyglot::frontends::ploy`

| 头文件 | 类 / 函数 |
|--------|----------|
| `frontends/ploy/include/ploy_lexer.h` | `PloyLexer : LexerBase` |
| `frontends/ploy/include/ploy_parser.h` | `PloyParser : ParserBase` |
| `frontends/ploy/include/ploy_sema.h` | `void AnalyzeModule(const PloyModule&, SemaContext&)` |
| `frontends/ploy/include/ploy_lowering.h` | `void LowerToIR(const PloyModule&, ir::IRContext&, Diagnostics&)` |

**特殊功能**: 跨语言 LINK/IMPORT/EXPORT/MAP_TYPE/PIPELINE/NEW/METHOD/GET/SET/WITH/DELETE/EXTEND

---

# 8. 诊断系统

**头文件**: `frontends/common/include/diagnostics.h`  
**命名空间**: `polyglot::frontends`

## 8.1 DiagnosticSeverity

```cpp
enum class DiagnosticSeverity {
  kError,    // 致命错误 — 编译失败
  kWarning,  // 非致命问题 — 编译继续
  kNote      // 信息性说明 — 为前一个诊断提供上下文
};
```

## 8.2 ErrorCode

按编译阶段分类的结构化错误码：

| 范围 | 阶段 | 示例 |
|------|------|------|
| `1xxx` | 词法分析 | `kUnexpectedCharacter` (1001), `kUnterminatedString` (1002), `kUnterminatedComment` (1003) |
| `2xxx` | 语法分析 | `kUnexpectedToken` (2001), `kMissingSemicolon` (2002), `kMissingClosingBrace` (2003) |
| `3xxx` | 语义分析 | `kUndefinedSymbol` (3001), `kTypeMismatch` (3003), `kParamCountMismatch` (3004), `kReturnTypeMismatch` (3010) |
| `4xxx` | IR 降级 | `kLoweringUndefined` (4001), `kUnsupportedOperator` (4002) |
| `5xxx` | 链接器 | `kUnresolvedSymbol` (5001), `kDuplicateExport` (5002) |

## 8.3 Diagnostic 结构体

```cpp
struct Diagnostic {
  core::SourceLoc loc;               // 错误位置
  std::string message;               // 错误信息
  DiagnosticSeverity severity;       // 严重级别
  ErrorCode code;                    // 错误码
  std::vector<Diagnostic> related;   // 追踪链
  std::string suggestion;            // UI 快速修复提示
};
```

## 8.4 Diagnostics 容器

```cpp
class Diagnostics {
public:
  // 简单错误报告
  void Report(const core::SourceLoc& loc, const std::string& message);

  // 带严重级别、错误码、修复建议和关联诊断的丰富报告
  void Report(const core::SourceLoc& loc, const std::string& message,
              DiagnosticSeverity severity, ErrorCode code = ErrorCode::kUnknown,
              const std::string& suggestion = "",
              const std::vector<Diagnostic>& related = {});

  // 便捷方法
  void Warning(const core::SourceLoc& loc, const std::string& message,
               ErrorCode code = ErrorCode::kUnknown);
  void Note(const core::SourceLoc& loc, const std::string& message);

  // 查询
  bool HasErrors() const;
  bool HasWarnings() const;
  size_t ErrorCount() const;
  const std::vector<Diagnostic>& GetDiagnostics() const;

  // 输出
  void PrintAll(std::ostream& os) const;
  void Clear();
};
```

---

# 9. 链接器 ABI 验证

**头文件**: `tools/polyld/include/polyglot_linker.h`
**命名空间**: `polyglot::linker`

## 9.1 ABIDescriptor

描述跨语言符号的调用约定和参数布局。

```cpp
struct ABIDescriptor {
    enum class Convention {
        kSysV_AMD64,    // Linux/macOS x86_64
        kWin64,         // Windows x86_64
        kAAPCS64,       // ARM64
        kWasm           // WebAssembly
    };

    Convention convention;
    std::vector<ArgClass> arg_classes;  // 每个参数的类别（整数、SSE、内存）
    size_t shadow_space;                // Win64 shadow space（字节）
    size_t stack_alignment;             // 要求的栈对齐
};
```

## 9.2 ABI 验证

```cpp
// 验证源符号和目标符号是否具有兼容的 ABI
bool ValidateABICompatibility(const ABIDescriptor& source,
                               const ABIDescriptor& target);

// 获取给定语言中符号的 ABI 描述符
ABIDescriptor GetABIDescriptor(const std::string& language,
                                const std::string& symbol);
```

---

# 10. 语义分析类模式

**头文件**: `frontends/ploy/include/ploy_sema.h`
**命名空间**: `polyglot::ploy`

## 10.1 ForeignClassSchema

描述外部类的布局，用于编译时类型检查。

```cpp
struct ForeignClassSchema {
    std::string language;
    std::string class_name;
    std::vector<Field> fields;           // 字段名 + 类型
    std::vector<Method> methods;         // 方法名 + 签名
    std::vector<Constructor> constructors; // 可用构造器
    bool has_destructor;
    bool has_context_manager;            // __enter__/__exit__
};

struct Field {
    std::string name;
    core::Type type;
    Access access;  // public/protected/private
};

struct Method {
    std::string name;
    std::vector<core::Type> param_types;
    core::Type return_type;
    bool is_static;
};
```

## 10.2 PloySema 模式 API

```cpp
class PloySema {
public:
    // 注册从外部源发现的类模式
    void RegisterClassSchema(const ForeignClassSchema& schema);

    // 按完全限定名查找类模式
    std::optional<ForeignClassSchema> LookupClassSchema(
        const std::string& language,
        const std::string& class_name) const;

    // 将函数签名标记为已验证
    void MarkValidated(FunctionSignature& sig);

private:
    std::unordered_map<std::string, ForeignClassSchema> class_schemas_;
};
```

---

# SourceLoc

**头文件**: `common/include/core/source_loc.h`  
**命名空间**: `polyglot::core`

```cpp
struct SourceLoc {
  std::string file;     // 文件路径
  size_t line;          // 行号（从 1 开始）
  size_t column;        // 列号（从 1 开始）

  SourceLoc() = default;
  SourceLoc(std::string file_path, size_t line_number, size_t column_number);
};
```
