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
| C++ | `frontend_cpp` | `.cpp`, `.cxx`, `.cc`, `.h`, `.hpp` | C++11–C++23 | ✅ |
| Python | `frontend_python` | `.py`, `.pyi` | 3.8+ | ✅ |
| Rust | `frontend_rust` | `.rs` | 2018/2021 edition | ✅ |
| Java | `frontend_java` | `.java` | 8, 17, 21, 23 | ✅ |
| C# (.NET) | `frontend_dotnet` | `.cs`, `.vb` | .NET 6, 7, 8, 9 | ✅ |
| .ploy | `frontend_ploy` | `.ploy` | 1.0 | ✅ |

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

## 2.2 Keywords (54 total)

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

## 2.3 Declarations

### LINK — Cross-Language Function Linking

```ploy
LINK <target_lang>::<module>::<function> AS FUNC(<param_types>) -> <return_type>;
```

**Example**:
```ploy
LINK cpp::math::add AS FUNC(INT, INT) -> INT;
LINK python::utils::format_string AS FUNC(STRING) -> STRING;
```

### IMPORT — Module Import

```ploy
IMPORT <language> MODULE <module_path>;
```

### IMPORT PACKAGE — Package Import with Version Constraints

```ploy
IMPORT <language> PACKAGE <package_name>;
IMPORT <language> PACKAGE <package_name> >= <version>;
IMPORT <language> PACKAGE <package_name>::(<symbol1>, <symbol2>) >= <version>;
```

**Version operators**: `>=`, `<=`, `>`, `<`, `==`, `!=`

### EXPORT — Symbol Export

```ploy
EXPORT <symbol_name>;
EXPORT <symbol_name> AS <alias>;
```

### MAP_TYPE — Cross-Language Type Mapping

```ploy
MAP_TYPE <ploy_type> = <lang>::<type_name>;
```

### CONFIG — Package Manager Configuration

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

| .ploy Type | C++ | Python | Rust | Java | C# |
|-----------|-----|--------|------|------|-----|
| `INT` | `int` | `int` | `i32` | `int` | `int` |
| `FLOAT` | `double` | `float` | `f64` | `double` | `double` |
| `STRING` | `std::string` | `str` | `String` | `String` | `string` |
| `BOOL` | `bool` | `bool` | `bool` | `boolean` | `bool` |
| `VOID` | `void` | `None` | `()` | `void` | `void` |

### Container Types

| .ploy Type | C++ | Python | Rust |
|-----------|-----|--------|------|
| `LIST<T>` | `std::vector<T>` | `list[T]` | `Vec<T>` |
| `TUPLE<T...>` | `std::tuple<T...>` | `tuple` | `(T...)` |
| `DICT<K,V>` | `std::unordered_map<K,V>` | `dict[K,V]` | `HashMap<K,V>` |
| `ARRAY<T,N>` | `T[N]` | `list[T]` | `[T; N]` |
| `OPTION<T>` | `std::optional<T>` | `Optional[T]` | `Option<T>` |

## 2.10 Operators

| Category | Operators |
|----------|----------|
| Arithmetic | `+`, `-`, `*`, `/`, `%` |
| Comparison | `==`, `!=`, `<`, `>`, `<=`, `>=` |
| Logical | `AND`, `OR`, `NOT` |
| Assignment | `=` |
| Member Access | `.`, `::` |

## 2.11 Statement Termination

All statements are terminated with a semicolon (`;`).

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
| `fn(T1, T2) -> R` | — | Function type |

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

| Source → Target | Convention | Bridge |
|----------------|------------|--------|
| .ploy → C++ | cdecl | Direct FFI |
| .ploy → Python | CPython C API | `__ploy_python_*` runtime |
| .ploy → Rust | Rust ABI (extern "C") | Direct FFI |
| .ploy → Java | JNI | `__ploy_java_*` runtime |
| .ploy → .NET | CoreCLR Hosting | `__ploy_dotnet_*` runtime |

## 4.3 Runtime Bridge Functions

| Bridge | Header | Description |
|--------|--------|-------------|
| `__ploy_python_call` | `runtime/include/python_bridge.h` | Invoke Python function |
| `__ploy_python_new` | `runtime/include/python_bridge.h` | Instantiate Python class |
| `__ploy_java_call` | `runtime/include/java_rt.h` | Invoke Java method via JNI |
| `__ploy_java_init` | `runtime/include/java_rt.h` | Initialize JVM |
| `__ploy_dotnet_call` | `runtime/include/dotnet_rt.h` | Invoke .NET method via CoreCLR |
| `__ploy_dotnet_init` | `runtime/include/dotnet_rt.h` | Initialize .NET runtime |

---

# 5. Type Marshalling Rules

## 5.1 Primitive Marshalling

| .ploy → C++ | Rule |
|-------------|------|
| `INT` → `int` | Direct (same ABI representation) |
| `FLOAT` → `double` | Direct |
| `STRING` → `std::string` | Copy string data |
| `BOOL` → `bool` | Direct (i1 ↔ i8) |

| .ploy → Python | Rule |
|----------------|------|
| `INT` → `int` | Convert to/from `PyLong` |
| `FLOAT` → `float` | Convert to/from `PyFloat` |
| `STRING` → `str` | Convert to/from `PyUnicode` |
| `BOOL` → `bool` | Convert to/from `PyBool` |

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
Source → Frontend → IR → Optimization → Backend → Assembly → Binary
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
| x86_64 | SSE2/AVX/AVX2/AVX-512 | ✅ |
| ARM64 (AArch64) | NEON/SVE | ✅ |
| WebAssembly | MVP + SIMD | ✅ |
