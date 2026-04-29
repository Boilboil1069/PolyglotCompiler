# PolyglotCompiler Language & IR Specifications

> **Version**: 3.0.0  
> **Last Updated**: 2026-02-22  

---

## Table of Contents

1. [Supported Source Languages](#1-supported-source-languages)
2. [.ploy Language Specification](#2-ploy-language-specification)
3. [Unified IR Specification](#3-unified-ir-specification)
4. [Cross-Language Calling Convention](#4-cross-language-calling-convention)
5. [Type Marshalling Rules](#5-type-marshalling-rules)
6. [Binary Output Format](#6-binary-output-format)

---

# 1. Supported Source Languages

## 1.1 Language Matrix

| Language | Frontend | File Extensions | Versions | Status |
|----------|----------|-----------------|----------|--------|
| C++ | `frontend_cpp` | `.cpp`, `.cxx`, `.cc`, `.h`, `.hpp` | C++11–C++23 | �?|
| Python | `frontend_python` | `.py`, `.pyi` | 3.8+ | �?|
| Rust | `frontend_rust` | `.rs` | 2018/2021 edition | �?|
| Java | `frontend_java` | `.java` | 8, 17, 21, 23 | �?|
| C# (.NET) | `frontend_dotnet` | `.cs`, `.vb` | .NET 6, 7, 8, 9 | �?|
| .ploy | `frontend_ploy` | `.ploy` | 1.0 | �?|

## 1.2 Detection Rules

The `polyc` driver automatically detects the source language by file extension:

| Extension | Language |
|-----------|----------|
| `.cpp`, `.cxx`, `.cc`, `.h`, `.hpp` | C++ |
| `.py`, `.pyi` | Python |
| `.rs` | Rust |
| `.java` | Java |
| `.cs`, `.vb` | C# (.NET) |
| `.ploy` | .ploy |

Manual override: `polyc --lang=<language> input_file`

---

# 2. .ploy Language Specification

## 2.1 Overview

`.ploy` is a domain-specific language for expressing cross-language function-level linking, object-oriented interop, type mapping, and pipeline orchestration between heterogeneous source languages.

## 2.2 Keywords (57 total — case-insensitive)

Since `Ploy 1.5.2`, all reserved words are recognised **case-insensitively**.
The lexer normalises every keyword to its canonical UPPER-case spelling
before passing it to the parser, while the original source spelling is
retained on the token (`Token::raw_lexeme`) so that diagnostics and
source-faithful formatters keep printing what the user actually typed.
Identifiers remain *case-sensitive* — only the keyword set is folded.

```
LINK        IMPORT      EXPORT      MAP_TYPE    PIPELINE
FUNC        LET         VAR         RETURN      RETURNS*
IF          ELSE        WHILE       FOR         IN
MATCH       CASE        DEFAULT     BREAK       CONTINUE
AS          TRUE        FALSE       NULL        AND
OR          NOT         CALL        VOID        INT
FLOAT       STRING      BOOL        ARRAY       STRUCT
PACKAGE     LIST        TUPLE       DICT        OPTION
MAP_FUNC    CONVERT     CONFIG      VENV        CONDA
UV          PIPENV      POETRY      NEW         METHOD
GET         SET         WITH        DELETE      EXTEND
LANG        PRINTLN     TYPE        CONST       I8
I16         I32         I64         U8          U16
U32         U64         F32         F64         USIZE
ISIZE
```

`*` `RETURNS` is **deprecated** since `Ploy 1.5.2`.  The legacy
`LINK(...) RETURNS Type { ... }` syntax is still parsed and still produces
the same AST, but the parser emits a `kDeprecatedKeyword` warning that
echoes the user's source spelling.  New code should declare the return
type via the canonical `-> Type` arrow on the LINK signature instead.

> **Casing recommendation.**  All examples in this document use UPPER-case
> for keywords purely for legibility.  New `.ploy` programs are encouraged
> to follow the canonical lower-case convention common to general-purpose
> programming languages �?`link`, `func`, `var`, `return`, `if`, `else` �?
> which the lexer accepts identically.  Mixed-case spellings such as
> `If` / `Func` are also accepted but discouraged for consistency.

> **Identifier collisions.**  Because keyword recognition is now case
> -insensitive, identifiers that previously differed from a keyword only
> by case (`config`, `array`, `get`, `set`, `pipeline`, `new`, �? are now
> reserved.  Migrate such identifiers to non-keyword names (for example
> `app_config`, `np_array`, `getter`).

## 2.3 Declarations

### LINK �?Cross-Language Function Linking

```ploy
LINK <target_lang>::<module>::<function> AS FUNC(<param_types>) -> <return_type>;
```

**Example**:
```ploy
LINK cpp::math::add AS FUNC(INT, INT) -> INT;
LINK python::utils::format_string AS FUNC(STRING) -> STRING;
```

### IMPORT �?Module Import

```ploy
IMPORT <language> MODULE <module_path>;
```

### IMPORT PACKAGE �?Package Import with Version Constraints

```ploy
IMPORT <language> PACKAGE <package_name>;
IMPORT <language> PACKAGE <package_name> >= <version>;
IMPORT <language> PACKAGE <package_name>::(<symbol1>, <symbol2>) >= <version>;
```

**Version operators**: `>=`, `<=`, `>`, `<`, `==`, `!=`

### EXPORT �?Symbol Export

```ploy
EXPORT <symbol_name>;
EXPORT <symbol_name> AS <alias>;
```

### MAP_TYPE �?Cross-Language Type Mapping

```ploy
MAP_TYPE <ploy_type> = <lang>::<type_name>;
```

### CONFIG �?Package Manager Configuration

```ploy
CONFIG VENV "<path>";
CONFIG CONDA "<environment_name>";
CONFIG UV "<project_path>";
CONFIG PIPENV "<project_path>";
CONFIG POETRY "<project_path>";
```

## 2.4 Functions

```ploy
FUNC <name>(<param>: <type>, ...) -> <return_type> {
    <body>
}
```

## 2.5 Variables

```ploy
LET <name>: <type> = <expression>;   // Immutable
VAR <name>: <type> = <expression>;   // Mutable
```

### TYPE — Type Aliases (since Ploy 1.7.0)

```ploy
TYPE <alias_name> = <type_expression>;
```

A `TYPE` declaration registers `alias_name` as a synonym for the resolved
type expression.  Aliases participate in name lookup the same way struct
declarations do; redefining or shadowing a primitive keyword is rejected
with `kRedefinedSymbol`.  Examples:

```ploy
TYPE Pixel        = i32;           // width-aware integer
TYPE ChannelCount = u32;
TYPE PixelBuffer  = LIST(Pixel);   // alias propagates into generics
```

### CONST — Compile-time Constants (since Ploy 1.7.0)

```ploy
CONST <name>: <type> = <expression>;
```

`CONST` declarations require an explicit type annotation; the initializer
is folded by the ploy semantic analyser, which supports literals,
references to previously declared `CONST`s, unary `-` / `!` / `NOT`, and
the binary arithmetic, comparison, and logical operators.  Width-mismatch
between the declared type and the folded value emits a warning.  The
folded constant is registered as an immutable variable so the rest of
the pipeline can consume it via the standard symbol-table lookup path.

```ploy
CONST KMaxRetry: i32 = 5;
CONST KAlias:    i32 = KMaxRetry;   // CONST referencing CONST
CONST KArea:     i64 = 10 * 20;     // folded by sema
```

## 2.6 Control Flow

### Conditional

```ploy
IF (<condition>) { <body> }
ELSE IF (<condition>) { <body> }
ELSE { <body> }
```

### Loops

```ploy
WHILE (<condition>) { <body> }
FOR (<var> IN <iterable>) { <body> }
```

### Pattern Matching

```ploy
MATCH (<expression>) {
    CASE <pattern> => { <body> }
    DEFAULT => { <body> }
}
```

## 2.7 Cross-Language OOP

### Object Creation

```ploy
LET obj = NEW(<language>, <class_path>, <args...>);
```

### Method Invocation

```ploy
LET result = METHOD(<language>, <object>, <method_name>, <args...>);
```

### Property Access

```ploy
LET value = GET(<language>, <object>, <property>);
SET(<language>, <object>, <property>, <value>);
```

### Resource Management

```ploy
WITH (<language>, <object>) {
    // Object is automatically closed/disposed at block exit
}
```

### Object Destruction

```ploy
DELETE(<language>, <object>);
```

### Class Extension

```ploy
EXTEND(<language>, <base_class>) {
    // Define additional behavior
}
```

## 2.8 Pipeline

```ploy
PIPELINE <name> {
    STAGE <stage_name> = CALL(<language>, <function>, <args...>);
    STAGE <stage_name> = CALL(<language>, <function>, <prev_stage>);
}
```

## 2.9 Type System

### Primitive Types

Since `Ploy 1.7.0` (demand 2026-04-28-7) the primitive set is widened with
explicit-width signed/unsigned integer and floating-point keywords.  The
legacy spellings continue to work and now resolve as aliases:

- `INT` ≡ `i64`
- `FLOAT` ≡ `f64`

Width-mismatched assignments and initializers (e.g. assigning an `i64`
expression to an `i32` slot) emit a `kTypeMismatch`-class **warning**;
diagnostics that touch a user `TYPE` alias render the underlying
primitive too, e.g. `Pixel (alias of i32)`.

| .ploy keyword                  | Underlying `core::Type`        | Width | Sign     |
| ------------------------------ | ------------------------------ | ----- | -------- |
| `i8` / `i16` / `i32` / `i64`   | `Int(N, true)`                 | 8 / 16 / 32 / 64 | signed   |
| `u8` / `u16` / `u32` / `u64`   | `Int(N, false)`                | 8 / 16 / 32 / 64 | unsigned |
| `f32` / `f64`                  | `Float(N)`                     | 32 / 64 | n/a   |
| `usize` / `isize`              | pointer-width integer          | platform | as named |
| `INT` (legacy alias of `i64`)  | `Int(64, true)`                | 64    | signed   |
| `FLOAT` (legacy alias of `f64`)| `Float(64)`                    | 64    | n/a      |

| .ploy Type | C++ | Python | Rust | Java | C# | Go | JavaScript | Ruby |
|-----------|-----|--------|------|------|-----|------|-----------|------|
| `INT`     | `int`         | `int`   | `i32`     | `int`     | `int`    | `int`     | `number`        | `Integer` |
| `FLOAT`   | `double`      | `float` | `f64`     | `double`  | `double` | `float64` | `number`        | `Float`   |
| `STRING`  | `std::string` | `str`   | `String`  | `String`  | `string` | `string`  | `string`        | `String`  |
| `BOOL`    | `bool`        | `bool`  | `bool`    | `boolean` | `bool`   | `bool`    | `boolean`       | `TrueClass` / `FalseClass` |
| `VOID`    | `void`        | `None`  | `()`      | `void`    | `void`   | (no return) | `undefined`   | `nil`     |

### Container Types

| .ploy Type | C++ | Python | Rust | Go | JavaScript | Ruby |
|------------|-----|--------|------|------|-----------|------|
| `LIST<T>`        | `std::vector<T>`           | `list[T]`     | `Vec<T>`        | `[]T`            | `Array`         | `Array`     |
| `TUPLE<T...>`    | `std::tuple<T...>`         | `tuple`       | `(T...)`        | `struct{...}`    | (positional Array) | `Array`  |
| `DICT<K,V>`      | `std::unordered_map<K,V>`  | `dict[K,V]`   | `HashMap<K,V>`  | `map[K]V`        | `Object` / `Map`| `Hash`      |
| `ARRAY<T,N>`     | `T[N]`                     | `list[T]`     | `[T; N]`        | `[N]T`           | `Array`         | `Array`     |
| `OPTION<T>`      | `std::optional<T>`         | `Optional[T]` | `Option<T>`     | `*T` (nullable)  | `T \| null`    | `T \| nil` |

## 2.10 Operators

| Category | Operators |
|----------|----------|
| Arithmetic | `+`, `-`, `*`, `/`, `%` |
| Comparison | `==`, `!=`, `<`, `>`, `<=`, `>=` |
| Logical | `&&`, `\|\|`, `!` (preferred) �?`AND`, `OR`, `NOT` (alias since 1.5.1) |
| Assignment | `=` |
| Member Access | `.`, `::` |

> **Logical operator aliases (Ploy 1.5.2+).**  The keyword forms `AND`,
> `OR`, `NOT` are accepted as exact aliases of the symbolic `&&`, `||`,
> `!` operators and produce identical AST nodes.  Both forms are
> permanent, but the symbolic spellings are preferred for new code so
> that source files mix more naturally with the C/C++/Rust/JavaScript
> idioms used by the cross-language ecosystem this language targets.

## 2.11 Statement Termination

All statements are terminated with a semicolon (`;`).

## 2.12 PRINTLN — Standard-Output Statement (since `Ploy 1.5.3`)

```
PRINTLN STRING_LITERAL ';'
```

`PRINTLN` writes a single string literal to the host process's standard
output stream. It is intentionally minimal: only one literal is accepted —
no expressions, no concatenation, no formatting placeholders. Richer
forms will be added once the runtime-IO pipeline (demand `2026-04-28-49`)
reaches its later stages.

Semantics:

- The literal's content is taken **verbatim** between the quotes; no line
  terminator is appended automatically. To emit a CRLF, write the escapes
  inside the source: `PRINTLN "Hello\r\n";`.
- Backslash escape sequences (`\r`, `\n`, `\t`, `\\`, `\"`, …) are *not*
  decoded by the front-end. The decoded byte sequence is produced by the
  codegen stage, ensuring a single canonical interpretation across the
  interpreter, IR text round-trip and the emitted `.rdata` payload.
- Empty literals (`PRINTLN "";`) are legal and emit zero bytes.

The `PRINTLN` keyword obeys the same case-insensitive rule as every other
Ploy keyword (`println`, `Println`, `PRINTLN` are equivalent).

---

# 3. Unified IR Specification

## 3.1 IR Type System

The IR uses a low-level type system independent of source languages:

| IR Type | Size | Description |
|---------|------|-------------|
| `i1` | 1 bit | Boolean |
| `i8` | 1 byte | Byte / char |
| `i16` | 2 bytes | Short integer |
| `i32` | 4 bytes | 32-bit integer |
| `i64` | 8 bytes | 64-bit integer |
| `f32` | 4 bytes | Single-precision float |
| `f64` | 8 bytes | Double-precision float |
| `void` | 0 bytes | No value |
| `T*` | pointer-sized | Pointer to T |
| `T&` | pointer-sized | Reference to T |
| `[N x T]` | N × sizeof(T) | Fixed-size array |
| `<N x T>` | N × sizeof(T) | SIMD vector |
| `{T1, T2, ...}` | sum of fields | Struct type |
| `fn(T1, T2) -> R` | �?| Function type |

## 3.2 Instructions

### Arithmetic

```
%result = add i64 %a, %b
%result = sub i64 %a, %b
%result = mul i64 %a, %b
%result = sdiv i64 %a, %b
%result = fadd f64 %x, %y
%result = fsub f64 %x, %y
%result = fmul f64 %x, %y
%result = fdiv f64 %x, %y
%result = frem f64 %x, %y
```

### Memory

```
%ptr = alloca i64
store i64 %val, i64* %ptr
%loaded = load i64, i64* %ptr
%elem = getelementptr %struct, %struct* %ptr, i64 0, i32 1
```

### Control Flow

```
br i1 %cond, label %true_bb, label %false_bb
br label %target
ret i64 %val
ret void
switch i64 %val, label %default [i64 0, label %case0; i64 1, label %case1]
```

### Casts

```
%ext = zext i32 %val to i64
%trunc = trunc i64 %val to i32
%cast = bitcast i64* %ptr to i8*
```

### Calls

```
%result = call i64 @function_name(i64 %arg1, f64 %arg2)
call void @procedure(i64 %arg)
```

### Phi

```
%merged = phi i64 [%val1, %pred1], [%val2, %pred2]
```

## 3.3 Function Definition

```
define i64 @function_name(i64 %param1, f64 %param2) {
entry:
    %result = add i64 %param1, 42
    ret i64 %result
}
```

## 3.4 External Declaration

```
declare i64 @external_function(i64, f64)
```

## 3.5 Global Variables

```
@global_var = global i64 0
@constant = constant [13 x i8] "Hello, World\00"
```

## 3.6 SSA Form

The IR uses Static Single Assignment (SSA) form after the SSA transformation pass:

- Every variable is assigned exactly once
- Phi nodes merge values at control flow join points
- Enables efficient optimization passes (constant propagation, dead code elimination, etc.)

---

# 4. Cross-Language Calling Convention

## 4.1 Overview

PolyglotCompiler generates FFI glue code to bridge function calls between different source languages. Each cross-language call goes through the following steps:

1. **Argument marshalling**: Convert caller's types to callee's types
2. **Function dispatch**: Route the call through the runtime bridge
3. **Return value unmarshalling**: Convert callee's return type back to caller's type

## 4.2 Calling Convention Table

| Source �?Target | Convention | Bridge |
|----------------|------------|--------|
| .ploy �?C++ | cdecl | Direct FFI |
| .ploy �?Python | CPython C API | `__ploy_python_*` runtime |
| .ploy �?Rust | Rust ABI (extern "C") | Direct FFI |
| .ploy �?Java | JNI | `__ploy_java_*` runtime |
| .ploy �?.NET | CoreCLR Hosting | `__ploy_dotnet_*` runtime |

## 4.3 Runtime Bridge Functions

| Bridge | Header | Description |
|--------|--------|-------------|
| `__ploy_python_call` | `runtime/include/libs/python_rt.h` | Invoke Python function |
| `__ploy_python_new` | `runtime/include/libs/python_rt.h` | Instantiate Python class |
| `__ploy_java_call` | `runtime/include/libs/java_rt.h` | Invoke Java method via JNI |
| `__ploy_java_init` | `runtime/include/libs/java_rt.h` | Initialize JVM |
| `__ploy_dotnet_call` | `runtime/include/libs/dotnet_rt.h` | Invoke .NET method via CoreCLR |
| `__ploy_dotnet_init` | `runtime/include/libs/dotnet_rt.h` | Initialize .NET runtime |

---

# 5. Type Marshalling Rules

## 5.1 Primitive Marshalling

| .ploy �?C++ | Rule |
|-------------|------|
| `INT` �?`int` | Direct (same ABI representation) |
| `FLOAT` �?`double` | Direct |
| `STRING` �?`std::string` | Copy string data |
| `BOOL` �?`bool` | Direct (i1 �?i8) |

| .ploy �?Python | Rule |
|----------------|------|
| `INT` �?`int` | Convert to/from `PyLong` |
| `FLOAT` �?`float` | Convert to/from `PyFloat` |
| `STRING` �?`str` | Convert to/from `PyUnicode` |
| `BOOL` �?`bool` | Convert to/from `PyBool` |

## 5.2 Container Marshalling

| .ploy Type | Marshalling Strategy |
|-----------|---------------------|
| `LIST<T>` | Element-wise copy with type conversion |
| `TUPLE<T...>` | Positional element copy |
| `DICT<K,V>` | Key-value pair iteration and conversion |
| `ARRAY<T,N>` | Direct memory copy (same element type) or element-wise conversion |

## 5.3 Object Marshalling

Objects created via `NEW` are managed by opaque handles:

- Each object is tracked by the runtime with a unique handle ID
- Method calls go through the runtime bridge using the handle
- Object destruction (`DELETE`) releases the handle and invokes the destructor/finalizer

---

# 6. Binary Output Format

## 6.1 Compilation Pipeline

```
Source �?Frontend �?IR �?Optimization �?Backend �?Assembly �?Binary
```

## 6.2 Output Artifacts

| File | Format | Description |
|------|--------|-------------|
| `*.ir` | Text | Human-readable IR representation |
| `*.ir.bin` | Binary | Serialized IR (binary format) |
| `*.asm` | Text | Target-specific assembly |
| `*.asm.bin` | Binary | Encoded assembly |
| `*.o` / `*.obj` | Object | Relocatable object file |
| `*.exe` / (no ext) | Executable | Final linked binary |

## 6.3 Auxiliary Directory

During compilation, `polyc` generates intermediate artifacts in an `aux/` subdirectory:

```
aux/
├── <filename>.ir         # IR text (if --emit-ir)
├── <filename>.ir.bin     # Binary-encoded IR
├── <filename>.asm        # Generated assembly
├── <filename>.asm.bin    # Binary-encoded assembly
├── <filename>.obj        # Object file
└── <filename>.symbols    # Symbol table dump
```

## 6.4 Target Architectures

| Architecture | Instruction Set | Status |
|-------------|-----------------|--------|
| x86_64 | SSE2/AVX/AVX2/AVX-512 | �?|
| ARM64 (AArch64) | NEON/SVE | �?|
| WebAssembly | MVP + SIMD | �?|
