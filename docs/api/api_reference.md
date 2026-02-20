# PolyglotCompiler API Reference

> **Version**: 2.0.0  
> **Last Updated**: 2026-02-20  

---

## Table of Contents

1. [Core Type System](#1-core-type-system)
2. [Symbol Table](#2-symbol-table)
3. [Frontend Common](#3-frontend-common)
4. [IR Nodes](#4-ir-nodes)
5. [IR Context](#5-ir-context)
6. [IR Utilities](#6-ir-utilities)
7. [Frontend APIs](#7-frontend-apis)
8. [Diagnostics](#8-diagnostics)

---

# 1. Core Type System

**Header**: `common/include/core/types.h`  
**Namespace**: `polyglot::core`

## 1.1 TypeKind Enum

Enumeration of all supported type kinds in the unified type system.

```cpp
enum class TypeKind {
  kInvalid,        // Invalid / unresolved type
  kVoid,           // Void return type
  kBool,           // Boolean type
  kInt,            // Integer type (with optional bit-width / signedness)
  kFloat,          // Floating-point type (with optional bit-width)
  kString,         // String type
  kPointer,        // Pointer type
  kFunction,       // Function type (return + parameters)
  kReference,      // Reference type (C++ / Rust)
  kClass,          // Class type
  kModule,         // Module type
  kAny,            // Dynamic / any type
  kStruct,         // Structure type
  kUnion,          // Union type
  kEnum,           // Enumeration type
  kTuple,          // Tuple type
  kGenericParam,   // Unresolved generic parameter (e.g., T)
  kGenericInstance, // Instantiated generic (e.g., Vec<i32>)
  kArray,          // Fixed-size or dynamic array
  kOptional,       // Optional / nullable type
  kSlice,          // Slice type (Rust-style)
};
```

## 1.2 Type Struct

Unified type representation for all supported source languages.

### Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `kind` | `TypeKind` | `kInvalid` | The type category |
| `name` | `std::string` | `""` | Human-readable type name |
| `language` | `std::string` | `""` | Optional language tag (e.g., `"python"`, `"cpp"`) |
| `type_args` | `std::vector<Type>` | `{}` | Type arguments for generics, tuples, and compound types |
| `lifetime` | `std::string` | `""` | Borrow lifetime annotation (Rust-style) |
| `is_const` | `bool` | `false` | Const qualifier |
| `is_volatile` | `bool` | `false` | Volatile qualifier |
| `is_rvalue_ref` | `bool` | `false` | Rvalue reference qualifier (C++ `&&`) |
| `bit_width` | `int` | `0` | Bit width (8/16/32/64/128) for integers and floats; 0 = unspecified |
| `is_signed` | `bool` | `true` | Sign flag for integer types |
| `array_size` | `size_t` | `0` | Element count for fixed-size arrays; 0 = dynamic |

### Factory Methods

```cpp
static Type Invalid();          // Creates an invalid type marker
static Type Void();             // Creates a void type
static Type Bool();             // Creates a boolean type
static Type Int();              // Creates a default integer type
static Type Float();            // Creates a default float type
static Type String();           // Creates a string type
static Type Any();              // Creates a dynamic/any type

static Type Int(int bits, bool sign);    // Int with bit-width and signedness
static Type Float(int bits);             // Float with bit-width (32 or 64)

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

### Comparison

```cpp
bool operator==(const Type& other) const;
bool operator!=(const Type& other) const;
```

## 1.3 TypeSystem Class

Central type mapping, compatibility checking, and size computation.

### Key Methods

```cpp
// Map a language-specific type name to the unified Type representation.
// Supported languages: "python", "cpp", "rust"
Type MapFromLanguage(const std::string& lang, const std::string& type_name) const;

// Check if 'from' is implicitly convertible to 'to'.
bool IsImplicitlyConvertible(const Type& from, const Type& to) const;

// Compute the size in bytes and alignment for a given Type.
size_t SizeOf(const Type& t) const;
size_t AlignOf(const Type& t) const;
```

## 1.4 TypeUnifier Class

Hindley-Milner style type unifier for resolving generic type parameters.

```cpp
bool Unify(const Type& a, const Type& b);           // Attempt to unify two types
Type Resolve(const Type& t) const;                   // Resolve a type through substitutions
void Reset();                                        // Clear all substitutions
```

## 1.5 TypeRegistry Class

Named type registration and cross-language equivalence tracking.

```cpp
void Register(const std::string& name, const Type& type);
std::optional<Type> Lookup(const std::string& name) const;
void RegisterEquivalence(const std::string& name_a, const std::string& name_b);
bool AreEquivalent(const std::string& name_a, const std::string& name_b) const;
```

---

# 2. Symbol Table

**Header**: `common/include/core/symbols.h`  
**Namespace**: `polyglot::core`

## 2.1 SymbolKind / ScopeKind Enums

```cpp
enum class SymbolKind { kVariable, kFunction, kTypeName, kModule, kParameter, kField };
enum class ScopeKind  { kGlobal, kModule, kFunction, kClass, kBlock, kComprehension };
```

## 2.2 Symbol Struct

| Field | Type | Description |
|-------|------|-------------|
| `name` | `std::string` | Symbol name |
| `type` | `Type` | Associated type |
| `loc` | `SourceLoc` | Source location of declaration |
| `kind` | `SymbolKind` | Kind of symbol |
| `language` | `std::string` | Language origin |
| `scope_id` | `int` | Owning scope identifier |
| `captured` | `bool` | Whether the symbol is captured by a closure |
| `access` | `std::string` | Access modifier (`"public"`, `"protected"`, `"private"`, or empty) |

## 2.3 SymbolTable Class

> **Implementation note**: Internally uses `std::deque<Symbol>` for pointer-stable storage.

### Scope Management

```cpp
int EnterScope(const std::string& name, ScopeKind kind);  // Enter a new scope; returns scope id
void ExitScope();                                           // Exit the current scope
```

### Declaration

```cpp
const Symbol* Declare(const Symbol& symbol);                     // Declare in current scope
const Symbol* DeclareInScope(int scope_id, const Symbol& symbol); // Declare in specific scope
```

### Lookup

```cpp
struct ResolveResult {
  const Symbol* symbol;
  int scope_distance;     // 0 = same scope, 1 = parent, etc.
};

std::optional<ResolveResult> Lookup(const std::string& name) const;

// Overloaded function resolution with type-based scoring
std::optional<ResolveResult> ResolveFunction(
    const std::string& name,
    const std::vector<Type>& arg_types,
    const TypeSystem& types) const;
```

### Accessors

```cpp
int CurrentScope() const;
const std::vector<ScopeInfo>& Scopes() const;
size_t SymbolCount() const;
```

---

# 3. Frontend Common

**Headers**: `frontends/common/include/`  
**Namespace**: `polyglot::frontends`

## 3.1 Token and TokenKind

**Header**: `frontends/common/include/lexer_base.h`

```cpp
enum class TokenKind {
  kEndOfFile,      // End of input
  kIdentifier,     // Identifiers (variable names, etc.)
  kNumber,         // Numeric literals
  kString,         // String literals
  kChar,           // Character literals
  kLifetime,       // Rust lifetime annotations ('a)
  kKeyword,        // Language keywords
  kSymbol,         // Operators and punctuation
  kPreprocessor,   // Preprocessor directives (#include, etc.)
  kComment,        // Comments
  kNewline,        // Significant newlines (Python)
  kIndent,         // Indentation increase (Python)
  kDedent,         // Indentation decrease (Python)
  kUnknown         // Unrecognized token
};

struct Token {
  TokenKind kind;
  std::string lexeme;   // The textual representation
  core::SourceLoc loc;  // Source file location
  bool is_doc;          // Whether this is a doc comment
};
```

## 3.2 LexerBase Class

Abstract base class for all language-specific lexers.

```cpp
class LexerBase {
public:
  virtual ~LexerBase() = default;
  virtual Token NextToken() = 0;

  // Save/restore state for parser lookahead
  struct LexerState { size_t position, line, column; };
  LexerState SaveState() const;
  void RestoreState(const LexerState& state);

protected:
  LexerBase(std::string source, std::string file);

  char Peek() const;       // Look at current character without advancing
  char PeekNext() const;   // Look at next character
  char Get();              // Consume current character and advance
  core::SourceLoc CurrentLoc() const;
  bool Eof() const;
  void SetTabWidth(size_t width);

  std::string source_;
  std::string file_;
  size_t position_, line_, column_, tab_width_;
};
```

## 3.3 ParserBase Class

**Header**: `frontends/common/include/parser_base.h`

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

## 3.4 SemaContext Class

**Header**: `frontends/common/include/sema_context.h`

Shared semantic analysis context used by all frontends.

```cpp
class SemaContext {
public:
  explicit SemaContext(Diagnostics& diagnostics);

  core::SymbolTable& Symbols();
  core::TypeSystem&  Types();
  Diagnostics&       Diags();
};
```

---

# 4. IR Nodes

**Headers**: `middle/include/ir/nodes/`  
**Namespace**: `polyglot::ir`

## 4.1 IRType

**Header**: `middle/include/ir/nodes/types.h`

### IRTypeKind Enum

```cpp
enum class IRTypeKind {
  kInvalid, kI1, kI8, kI16, kI32, kI64,
  kF32, kF64, kVoid, kPointer, kReference,
  kArray, kVector, kStruct, kFunction
};
```

### IRType Factory Methods

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

## 4.2 Value (Base)

```cpp
struct Value {
  virtual ~Value() = default;
  IRType type;
  std::string name;  // SSA name
};
```

## 4.3 Literal and Constant Values

| Class | Description |
|-------|-------------|
| `LiteralExpression` | Integer or floating-point literal |
| `UndefValue` | Uninitialized / undefined placeholder |
| `GlobalValue` | Global variable or constant data |
| `ConstantString` | String constant (null-terminated by default) |
| `ConstantArray` | Array of constant values |
| `ConstantStruct` | Struct with constant fields |

## 4.4 Instructions

All instructions inherit from `Instruction`, which inherits from `Value`.

| Instruction | Fields | Description |
|-------------|--------|-------------|
| `BinaryInstruction` | `Op op` | Arithmetic, bitwise, comparison operations |
| `PhiInstruction` | `incomings` | SSA phi node for merging values from predecessors |
| `AssignInstruction` | — | Pre-SSA variable assignment |
| `CallInstruction` | `callee`, `is_indirect`, `is_tail_call` | Function call |
| `AllocaInstruction` | `no_escape` | Stack allocation |
| `LoadInstruction` | `align` | Memory load |
| `StoreInstruction` | `align` | Memory store |
| `CastInstruction` | `CastKind cast` | Type conversion |
| `GetElementPtrInstruction` | `source_type`, `indices`, `inbounds` | Address computation |
| `MemcpyInstruction` | `align` | Memory copy |
| `MemsetInstruction` | `align` | Memory set |
| `ReturnInstruction` | — | Function return (terminator) |
| `BranchInstruction` | `condition`, `true_target`, `false_target` | Conditional/unconditional branch (terminator) |
| `SwitchInstruction` | `cases`, `default_target` | Multi-way branch (terminator) |

### BinaryInstruction::Op Enum

```cpp
enum class Op {
  // Integer arithmetic
  kAdd, kSub, kMul, kDiv, kSDiv, kUDiv, kSRem, kURem, kRem,
  // Floating-point arithmetic
  kFAdd, kFSub, kFMul, kFDiv, kFRem,
  // Bitwise
  kAnd, kOr, kXor, kShl, kLShr, kAShr,
  // Integer comparison
  kCmpEq, kCmpNe, kCmpUlt, kCmpUle, kCmpUgt, kCmpUge,
  kCmpSlt, kCmpSle, kCmpSgt, kCmpSge, kCmpLt,
  // Floating-point comparison
  kCmpFoe, kCmpFne, kCmpFlt, kCmpFle, kCmpFgt, kCmpFge
};
```

### CastInstruction::CastKind Enum

```cpp
enum class CastKind {
  kZExt, kSExt, kTrunc, kBitcast,
  kFpExt, kFpTrunc, kIntToPtr, kPtrToInt
};
```

## 4.5 BasicBlock and Function

**Header**: `middle/include/ir/nodes/statements.h`

```cpp
struct BasicBlock {
  std::string label;
  std::vector<std::shared_ptr<Instruction>> instructions;
  Function* parent;
  std::vector<BasicBlock*> predecessors;
  std::vector<BasicBlock*> successors;
};

struct Function {
  std::string name;
  IRType return_type;
  std::vector<std::pair<std::string, IRType>> params;
  std::vector<std::shared_ptr<BasicBlock>> blocks;
  bool is_declaration;     // External declaration (no body)
  bool is_vararg;
  std::string calling_conv;
  std::string linkage;     // "external", "internal", "private", etc.
};
```

---

# 5. IR Context

**Header**: `middle/include/ir/ir_context.h`  
**Namespace**: `polyglot::ir`

```cpp
class IRContext {
public:
  explicit IRContext(DataLayout::Arch arch = DataLayout::Arch::kX86_64);

  // Function creation
  std::shared_ptr<Function> CreateFunction(const std::string& name);
  std::shared_ptr<Function> CreateFunction(
      const std::string& name, const IRType& ret,
      const std::vector<std::pair<std::string, IRType>>& params);

  // Global variable creation
  std::shared_ptr<GlobalValue> CreateGlobal(
      const std::string& name, const IRType& type,
      bool is_const = false, const std::string& init = "",
      std::shared_ptr<Value> initializer = nullptr);

  // Convenience accessors
  std::shared_ptr<Function> DefaultFunction();
  std::shared_ptr<BasicBlock> DefaultBlock();
  void AddStatement(const std::shared_ptr<Statement>& stmt);

  // Query
  const std::vector<std::shared_ptr<Function>>& Functions() const;
  const std::vector<std::shared_ptr<GlobalValue>>& Globals() const;
  const DataLayout& Layout() const;
  DataLayout& Layout();

  // Dialect registration
  void RegisterDialectByName(const std::string& name);
  template <typename Dialect> void RegisterDialect();
  const std::vector<std::string>& Dialects() const;
};
```

---

# 6. IR Utilities

## 6.1 IR Printer

**Header**: `common/include/ir/ir_printer.h`  
**Namespace**: `polyglot::ir`

```cpp
void PrintFunction(const Function& func, std::ostream& os);
void PrintModule(const IRContext& ctx, std::ostream& os);
std::string Dump(const Function& func);
```

## 6.2 IR Builder

**Header**: `common/include/ir/ir_builder.h`

Provides a fluent API for constructing IR instructions within a `BasicBlock`.

## 6.3 IR Parser

**Header**: `common/include/ir/ir_parser.h`

Parses textual IR representation back into `IRContext`.

## 6.4 IR Visitor

**Header**: `common/include/ir/ir_visitor.h`

Base visitor class for traversing IR structures (instructions, basic blocks, functions).

---

# 7. Frontend APIs

Each language frontend exposes the same four-phase pipeline:

| Phase | Function | Input | Output |
|-------|----------|-------|--------|
| **Lexer** | `NextToken()` | Source text | `Token` stream |
| **Parser** | `ParseModule()` | Token stream | Language-specific AST |
| **Sema** | `AnalyzeModule()` | AST + `SemaContext` | Validated AST + symbol table |
| **Lowering** | `LowerToIR()` | AST + `IRContext` | IR functions and globals |

## 7.1 C++ Frontend

**Namespace**: `polyglot::frontends::cpp`

| Header | Class/Function |
|--------|---------------|
| `frontends/cpp/include/cpp_lexer.h` | `CppLexer : LexerBase` |
| `frontends/cpp/include/cpp_parser.h` | `CppParser : ParserBase` |
| `frontends/cpp/include/cpp_sema.h` | `void AnalyzeModule(const CppModule&, SemaContext&)` |
| `frontends/cpp/include/cpp_lowering.h` | `void LowerToIR(const CppModule&, ir::IRContext&, Diagnostics&)` |

## 7.2 Python Frontend

**Namespace**: `polyglot::frontends::python`

| Header | Class/Function |
|--------|---------------|
| `frontends/python/include/python_lexer.h` | `PythonLexer : LexerBase` |
| `frontends/python/include/python_parser.h` | `PythonParser : ParserBase` |
| `frontends/python/include/python_sema.h` | `void AnalyzeModule(const PythonModule&, SemaContext&)` |
| `frontends/python/include/python_lowering.h` | `void LowerToIR(const PythonModule&, ir::IRContext&, Diagnostics&)` |

## 7.3 Rust Frontend

**Namespace**: `polyglot::frontends::rust`

| Header | Class/Function |
|--------|---------------|
| `frontends/rust/include/rust_lexer.h` | `RustLexer : LexerBase` |
| `frontends/rust/include/rust_parser.h` | `RustParser : ParserBase` |
| `frontends/rust/include/rust_sema.h` | `void AnalyzeModule(const RustModule&, SemaContext&)` |
| `frontends/rust/include/rust_lowering.h` | `void LowerToIR(const RustModule&, ir::IRContext&, Diagnostics&)` |

## 7.4 Java Frontend

**Namespace**: `polyglot::frontends::java`

| Header | Class/Function |
|--------|---------------|
| `frontends/java/include/java_lexer.h` | `JavaLexer : LexerBase` |
| `frontends/java/include/java_parser.h` | `JavaParser : ParserBase` |
| `frontends/java/include/java_sema.h` | `void AnalyzeModule(const Module&, SemaContext&)` |
| `frontends/java/include/java_lowering.h` | `void LowerToIR(const Module&, ir::IRContext&, Diagnostics&)` |

**Supported versions**: Java 8, 17, 21, 23

## 7.5 .NET Frontend

**Namespace**: `polyglot::frontends::dotnet`

| Header | Class/Function |
|--------|---------------|
| `frontends/dotnet/include/dotnet_lexer.h` | `DotnetLexer : LexerBase` |
| `frontends/dotnet/include/dotnet_parser.h` | `DotnetParser : ParserBase` |
| `frontends/dotnet/include/dotnet_sema.h` | `void AnalyzeModule(const Module&, SemaContext&)` |
| `frontends/dotnet/include/dotnet_lowering.h` | `void LowerToIR(const Module&, ir::IRContext&, Diagnostics&)` |

**Supported versions**: .NET 6, 7, 8, 9

## 7.6 .ploy Frontend

**Namespace**: `polyglot::frontends::ploy`

| Header | Class/Function |
|--------|---------------|
| `frontends/ploy/include/ploy_lexer.h` | `PloyLexer : LexerBase` |
| `frontends/ploy/include/ploy_parser.h` | `PloyParser : ParserBase` |
| `frontends/ploy/include/ploy_sema.h` | `void AnalyzeModule(const PloyModule&, SemaContext&)` |
| `frontends/ploy/include/ploy_lowering.h` | `void LowerToIR(const PloyModule&, ir::IRContext&, Diagnostics&)` |

**Special features**: Cross-language LINK/IMPORT/EXPORT/MAP_TYPE/PIPELINE/NEW/METHOD/GET/SET/WITH/DELETE/EXTEND

---

# 8. Diagnostics

**Header**: `frontends/common/include/diagnostics.h`  
**Namespace**: `polyglot::frontends`

## 8.1 DiagnosticSeverity

```cpp
enum class DiagnosticSeverity {
  kError,    // Fatal — compilation fails
  kWarning,  // Non-fatal — compilation continues
  kNote      // Informational — context for previous diagnostic
};
```

## 8.2 ErrorCode

Structured error codes organized by compilation phase:

| Range | Phase | Examples |
|-------|-------|---------|
| `1xxx` | Lexer | `kUnexpectedCharacter` (1001), `kUnterminatedString` (1002), `kUnterminatedComment` (1003) |
| `2xxx` | Parser | `kUnexpectedToken` (2001), `kMissingSemicolon` (2002), `kMissingClosingBrace` (2003) |
| `3xxx` | Semantic | `kUndefinedSymbol` (3001), `kTypeMismatch` (3003), `kParamCountMismatch` (3004), `kReturnTypeMismatch` (3010) |
| `4xxx` | Lowering | `kLoweringUndefined` (4001), `kUnsupportedOperator` (4002) |
| `5xxx` | Linker | `kUnresolvedSymbol` (5001), `kDuplicateExport` (5002) |

## 8.3 Diagnostic Struct

```cpp
struct Diagnostic {
  core::SourceLoc loc;
  std::string message;
  DiagnosticSeverity severity;
  ErrorCode code;
  std::vector<Diagnostic> related;  // Traceback chain
  std::string suggestion;           // Quick-fix hint for UI
};
```

## 8.4 Diagnostics Container

```cpp
class Diagnostics {
public:
  // Simple error report
  void Report(const core::SourceLoc& loc, const std::string& message);

  // Rich diagnostic with severity, code, suggestion, and related diagnostics
  void Report(const core::SourceLoc& loc, const std::string& message,
              DiagnosticSeverity severity, ErrorCode code = ErrorCode::kUnknown,
              const std::string& suggestion = "",
              const std::vector<Diagnostic>& related = {});

  // Convenience
  void Warning(const core::SourceLoc& loc, const std::string& message,
               ErrorCode code = ErrorCode::kUnknown);
  void Note(const core::SourceLoc& loc, const std::string& message);

  // Query
  bool HasErrors() const;
  bool HasWarnings() const;
  size_t ErrorCount() const;
  const std::vector<Diagnostic>& GetDiagnostics() const;

  // Output
  void PrintAll(std::ostream& os) const;
  void Clear();
};
```

---

# SourceLoc

**Header**: `common/include/core/source_loc.h`  
**Namespace**: `polyglot::core`

```cpp
struct SourceLoc {
  std::string file;
  size_t line;      // 1-based
  size_t column;    // 1-based

  SourceLoc() = default;
  SourceLoc(std::string file_path, size_t line_number, size_t column_number);
};
```
