# .ploy Language Specification — Polyglot Link Language

## 1. Overview

`.ploy` is a domain-specific language designed for the PolyglotCompiler project. It serves as the **polyglot link description language**, enabling developers to express cross-language function-level linking, variable sharing, type mapping, and control flow orchestration between heterogeneous source languages (C++, Python, Rust, etc.).

The `.ploy` file is processed by a dedicated frontend (`frontend_ploy`) that produces IR suitable for the middle-end and linker to generate cross-language glue code and interop stubs.

## 2. Design Goals

- **Explicit cross-language linking**: Provide `LINK` directives that map a target function/variable in one language to a source function/variable in another.
- **Type bridging**: Declare how types are marshalled across language boundaries via `MAP_TYPE`.
- **Module imports**: Reference compiled modules from different languages via `IMPORT`.
- **Package imports**: Reference external packages from target languages via `IMPORT ... PACKAGE`.
- **Control flow**: Support `IF/ELSE`, `WHILE`, `FOR`, `MATCH` for orchestrating complex linking logic.
- **Variable declarations**: Support `LET` (immutable) and `VAR` (mutable) bindings.
- **Function definitions**: Support native `.ploy` functions for glue logic.
- **Pipeline composition**: `PIPELINE` blocks for chaining multi-language function calls.
- **Custom type conversion**: `MAP_FUNC` for defining complex conversion functions.
- **Package ecosystem access**: `IMPORT ... PACKAGE` for using language-native packages (e.g., Python numpy).

## 3. Lexical Structure

### 3.1 Keywords

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

Total: 71 keywords (including the deprecated `RETURNS`, the
language-version-pinning `LANG`, the standard-output statement `PRINTLN`,
and the explicit-width primitives + `TYPE` / `CONST` introduced in
`Ploy 1.7.0`).

**Case-insensitivity (Ploy 1.5.2+).** Reserved words are matched
case-insensitively at the lexer level: `link`, `Link`, `LINK`, `LiNk`
all yield the same `kKeyword` token.  `Token::lexeme` is normalised to
the canonical UPPER spelling for downstream comparison; the original
source spelling is retained on `Token::raw_lexeme` and exposed via
`Token::SourceText()`.  Identifiers remain case-sensitive — only the
keyword set is folded.  Identifiers that previously differed from a
keyword only by case (`config`, `array`, `get`, …) are now reserved.

`*` `RETURNS` is parsed for backward compatibility but the parser
emits a non-fatal `kDeprecatedKeyword` warning (ErrorCode = 3024) at
the token's source location.  New code should use the `-> Type` arrow
on the LINK signature.

### 3.2 Identifiers

Identifiers follow C-style rules: start with a letter or underscore, followed by letters, digits, or underscores. Scope-qualified names use `::` (e.g., `cpp::math::add`). Package-qualified names use `.` (e.g., `numpy.linalg`).

### 3.3 Literals

- **Integer**: `42`, `0xFF`, `0b1010`, `0o77`
- **Float**: `3.14`, `1.0e-5`
- **String**: `"hello"`, `"escaped \"quotes\""`
- **Boolean**: `TRUE`, `FALSE`
- **Null**: `NULL`
- **List**: `[1, 2, 3]`
- **Tuple**: `(1, "hello")`
- **Struct**: `Point { x: 1.0, y: 2.0 }`

### 3.4 Operators

```
+  -  *  /  %          (arithmetic)
== != < > <= >=        (comparison)
&& || !                (logical)
=                      (assignment)
->                     (arrow / return type)
::                     (scope resolution)
.                      (member access / package path separator)
,  ;  :                (delimiters)
( ) { } [ ]            (grouping)
```

### 3.5 Comments

```ploy
// Single-line comment
/* Multi-line
   comment */
```

### 3.6 Semicolon Rules

All **simple statements** require a trailing semicolon:

```ploy
LET x = 42;                                // variable declaration
VAR y = 0;                                  // mutable variable
RETURN x;                                   // return statement
BREAK;                                      // loop break
CONTINUE;                                   // loop continue
x = x + 1;                                  // expression statement
IMPORT cpp::math;                           // module import
IMPORT python PACKAGE numpy AS np;          // package import
EXPORT f AS "fn";                           // export
LINK(cpp, python, f, g);                    // simple link
MAP_TYPE(cpp::int, python::int);            // type mapping
```

**Block statements** do NOT require a trailing semicolon:

```ploy
FUNC f() -> void { RETURN; }                // no ; after }
PIPELINE p { }                               // no ; after }
IF x > 0 { } ELSE { }                       // no ; after }
WHILE x > 0 { }                             // no ; after }
FOR i IN items { }                           // no ; after }
MATCH x { CASE 1 => { } }                   // no ; after }
STRUCT S { x: i32; }                         // no ; after }
MAP_FUNC f(x: i32) -> i32 { RETURN x; }     // no ; after }
LINK(a, b, c, d) { MAP_TYPE(a::t, b::t); }  // LINK with body: no ; after }
```

## 4. Grammar

### 4.1 Top-Level Declarations

```
program         ::= (top_level_decl)*
top_level_decl  ::= link_decl | import_decl | export_decl | map_type_decl
                   | pipeline_decl | func_decl | struct_decl | map_func_decl
                   | var_decl | statement
```

### 4.2 LINK Directive

The core construct for cross-language function-level linking.  Two surface
forms are recognised by the parser:

#### 4.2.1 Canonical / signed form (recommended, since v1.8.0)

```ploy
LINK <lang>::<module>::<func> AS FUNC(<param_types>) -> <return_type>;
LINK <lang>::<module>::<func> AS FUNC(<param_types>) -> <return_type> {
    MAP_TYPE(<target_type>, <source_type>);
    // ... one MAP_TYPE per parameter
}
```

The signed form embeds a real function signature so that:

* the target symbol is fully qualified (`lang::module::func`) and unambiguous;
* parameter and return types are statically declared and checked by sema;
* the form reads naturally as "link this symbol *as* a function of this signature".

Examples:

```ploy
// Simple signed link (no body)
LINK cpp::math::add AS FUNC(cpp::int, cpp::int) -> cpp::int;

// Signed link with MAP_TYPE body
LINK cpp::math::process AS FUNC(cpp::double, cpp::int) -> cpp::double {
    MAP_TYPE(cpp::double, python::float);
    MAP_TYPE(cpp::int, python::int);
}
```

#### 4.2.2 Legacy comma form (deprecated, still parsed)

```ploy
LINK(target_language, source_language, target_function, source_function);

// with body
LINK(cpp, python, math::process, data::load) {
    MAP_TYPE(cpp::double, python::float);
    MAP_TYPE(cpp::int, python::int);
}

// AS VAR / AS STRUCT modifiers (still apply to legacy form only)
LINK(cpp, python, config_data, py_config) AS VAR;
LINK(cpp, rust, Point, RustPoint) AS STRUCT {
    MAP_TYPE(cpp::double, rust::f64);
    MAP_TYPE(cpp::int, rust::i32);
}
```

The semantic analyzer emits a `kDeprecatedKeyword` warning for every
legacy-form `LINK(...)` it encounters.  New code SHOULD use the signed form;
existing samples are mirrored under `tests/samples/<n>_v2/` where the
canonical form has been adopted (see `01_basic_linking_v2/`).

### 4.3 IMPORT Directive

Three forms of import:

```ploy
// Form 1: Path import with alias
IMPORT "path/to/module" AS my_module;

// Form 2: Qualified module import (language::module)
IMPORT cpp::math_utils;
IMPORT python::data_loader;
IMPORT rust::validator;

// Form 3: Package import (language PACKAGE package_name [AS alias])
IMPORT python PACKAGE numpy AS np;
IMPORT python PACKAGE scipy.optimize AS opt;
IMPORT rust PACKAGE serde;
```

Package import supports dotted package paths (e.g., `numpy.linalg`, `scipy.optimize`).

### 4.4 EXPORT Directive

```ploy
EXPORT function_name;
EXPORT function_name AS "external_name";
```

### 4.5 MAP_TYPE Directive

Declares a type mapping between two language-specific types:

```ploy
MAP_TYPE(source_language::type, target_language::type);
MAP_TYPE(cpp::int, python::int);
MAP_TYPE(cpp::std::string, python::str);
MAP_TYPE(rust::f64, cpp::double);
```

### 4.6 STRUCT Declaration

```ploy
STRUCT Point {
    x: f64;
    y: f64;
    label: STRING;
}

STRUCT DataSet {
    name: STRING;
    values: LIST(f64);
    metadata: DICT(STRING, STRING);
}
```

### 4.7 MAP_FUNC Declaration

Custom type conversion function:

```ploy
MAP_FUNC normalize(x: f64) -> f64 {
    IF x < 0.0 {
        RETURN 0.0;
    }
    IF x > 1.0 {
        RETURN 1.0;
    }
    RETURN x;
}
```

### 4.8 PIPELINE Directive

Chain multiple cross-language calls in named stages:

```ploy
PIPELINE data_analysis {
    FUNC load() -> LIST(f64) {
        LET data = CALL(python, loader::read, "data.csv");
        RETURN data;
    }

    FUNC process(data: LIST(f64)) -> f64 {
        LET result = CALL(cpp, math::compute, data);
        RETURN result;
    }
}
```

### 4.9 Function Definitions

```ploy
FUNC add(a: i32, b: i32) -> i32 {
    RETURN a + b;
}

FUNC greet(name: STRING) -> void {
    LET msg = "Hello, " + name;
    RETURN;
}
```

### 4.10 Variable Declarations

```ploy
LET x = 42;                              // immutable, type inferred
VAR y = 3.14;                            // mutable, type inferred
LET result = CALL(cpp, compute, x);      // cross-language call result
```

### 4.11 Control Flow

#### IF/ELSE

```ploy
IF condition {
    LET a = 1;
} ELSE IF other_condition {
    LET b = 2;
} ELSE {
    LET c = 3;
}
```

#### WHILE Loop

```ploy
VAR i = 0;
WHILE i < 10 {
    i = i + 1;
}
```

#### FOR Loop

```ploy
FOR item IN collection {
    LET processed = item + 1;
}
```

#### MATCH

```ploy
MATCH value {
    CASE 0 => {
        RETURN "zero";
    }
    CASE 1 => {
        RETURN "one";
    }
    DEFAULT => {
        RETURN "other";
    }
}
```

### 4.12 CALL Expression

Cross-language function call with automatic type marshalling:

```ploy
CALL(language, function_name, arg1, arg2, ...);
```

Examples:

```ploy
LET data = CALL(python, loader::read, "input.csv");
LET result = CALL(cpp, math::compute, data, 42);
LET valid = CALL(rust, validator::check, result);
```

### 4.13 CONVERT Expression

Explicit type conversion:

```ploy
LET x = CONVERT(python_value, i32);
LET items = CONVERT(raw_list, LIST(f64));
```

### 4.14 Expressions

```
expression      ::= assignment_expr
assignment_expr ::= logical_or ('=' assignment_expr)?
logical_or      ::= logical_and ('||' logical_and)*
logical_and     ::= equality ('&&' equality)*
equality        ::= comparison (('==' | '!=') comparison)*
comparison      ::= addition (('<' | '>' | '<=' | '>=') addition)*
addition        ::= multiplication (('+' | '-') multiplication)*
multiplication  ::= unary (('*' | '/' | '%') unary)*
unary           ::= ('!' | '-' | 'NOT') unary | call_expr
call_expr       ::= primary ('(' arguments? ')')* ('.' identifier)*
primary         ::= identifier | literal | '(' expression ')'
                   | call_directive | list_literal | struct_literal
                   | convert_expr
```

## 5. Type System

### 5.1 Built-in Primitive Types

| .ploy Type   | C++ Equivalent | Python Equivalent | Rust Equivalent |
|-------------|---------------|-------------------|-----------------|
| i32         | int32_t       | int               | i32             |
| i64         | int64_t       | int               | i64             |
| f32         | float         | float             | f32             |
| f64         | double        | float             | f64             |
| BOOL        | bool          | bool              | bool            |
| STRING / str| std::string   | str               | String          |
| VOID        | void          | None              | ()              |
| ptr         | void*         | object            | *mut u8         |

### 5.2 Container Types

| .ploy Type        | C++ Equivalent               | Python Equivalent  | Rust Equivalent         |
|-------------------|------------------------------|--------------------|-------------------------|
| LIST(T)           | std::vector\<T\>             | list               | Vec\<T\>                |
| TUPLE(T1, T2)     | std::tuple\<T1, T2\>        | tuple              | (T1, T2)                |
| DICT(K, V)        | std::unordered_map\<K, V\>   | dict               | HashMap\<K, V\>         |
| OPTION(T)         | std::optional\<T\>           | Optional[T]        | Option\<T\>             |

### 5.3 Cross-Language Type Conversion

The compiler generates marshalling code based on `MAP_TYPE` directives and the built-in type conversion table. Numeric promotions, string encoding conversions, and collection adaptors are generated automatically where safe.

### 5.4 Strict Type Annotation Requirement (since 2026-04-09-12)

**All parameters and return values at cross-language boundaries must carry explicit type annotations.**

When the semantic analysis pass processes a `LINK` declaration, any parameter or return value without an explicit type annotation is assigned `Type::Unknown()` internally. This type propagates through the pipeline and, during IR lowering, produces an `IRTypeKind::kInvalid` IR type. The IR verifier then rejects the compilation with the error:

```
strict: function '<name>' has unresolved Invalid return type —
    add an explicit type annotation or LINK with MAP_TYPE
```

or:

```
strict: function '<name>' parameter N has unresolved Invalid type —
    add an explicit type annotation
```

**This strict behaviour is active by default** (i.e., `PloySemaOptions::strict_mode` defaults to `true`, and the driver enables IR verifier strict mode whenever `--dev` is not active).

#### How to Fix

Add explicit type annotations on all parameters and return values in `LINK` declarations:

```ploy
// ✗ Missing annotations — compile error in strict mode
LINK(cpp, python, f, g);

// ✓ With explicit annotations
LINK(cpp, python, f, g) {
    MAP_TYPE(cpp::int, python::int);
}
```

Or annotate the function signature directly:

```ploy
// ✓ Inline type annotation
LINK(cpp, python, process(x: f64) -> f64, np::process);
```

#### Opting Out (Not Recommended)

Legacy code that cannot be annotated immediately may pass `PloySemaOptions{.strict_mode = false}` when constructing `PloySema`. This suppresses the `Type::Unknown()` error but permits I64 placeholder IR, which may cause incorrect codegen at runtime.

## 6. Compilation Pipeline

```
.ploy source
    │
    ▼
┌──────────┐
│  Lexer   │  → Token stream (71 keywords, case-insensitive + operators + literals)
└──────────┘
    │
    ▼
┌──────────┐
│  Parser  │  → AST (declarations, statements, expressions)
└──────────┘
    │
    ▼
┌──────────┐
│   Sema   │  → Type-checked AST, symbol resolution, link validation
└──────────┘
    │
    ▼
┌───────────┐
│ Lowering  │  → Polyglot IR (cross-language call nodes, marshalling)
└───────────┘
    │
    ▼
┌───────────┐
│  Linker   │  → Glue code generation, stub emission
└───────────┘
```

## 7. Complete Example

```ploy
// ===== Module and Package Imports =====
IMPORT cpp::math_utils;
IMPORT python PACKAGE numpy AS np;
IMPORT rust::validator;

// ===== Type Mappings =====
MAP_TYPE(cpp::int, python::int);
MAP_TYPE(cpp::double, python::float);
MAP_TYPE(cpp::std::string, python::str);

// ===== Struct Definition =====
STRUCT DataPoint {
    timestamp: i64;
    value: f64;
    label: STRING;
}

// ===== Conversion Function =====
MAP_FUNC normalize(x: f64) -> f64 {
    IF x < 0.0 {
        RETURN 0.0;
    }
    IF x > 1.0 {
        RETURN 1.0;
    }
    RETURN x;
}

// ===== Cross-Language Links =====
LINK(cpp, python, math_utils::process, np::compute) {
    MAP_TYPE(cpp::double, python::float);
}

LINK(cpp, rust, Point, RustPoint) AS STRUCT {
    MAP_TYPE(cpp::double, rust::f64);
}

// ===== Pipeline =====
PIPELINE analyze_data {
    FUNC load() -> LIST(f64) {
        LET raw = CALL(python, np::loadtxt, "input.csv");
        RETURN raw;
    }

    FUNC process(data: LIST(f64)) -> f64 {
        LET result = CALL(cpp, math_utils::process, data);
        RETURN result;
    }

    FUNC validate(value: f64) -> BOOL {
        LET ok = CALL(rust, validator::check, value);
        RETURN ok;
    }
}

// ===== Glue Function =====
FUNC transform(input: f64, scale: i32) -> f64 {
    VAR result = input * scale;
    WHILE result > 1000.0 {
        result = result / 2.0;
    }
    RETURN result;
}

// ===== Exports =====
EXPORT analyze_data AS "data_pipeline";
EXPORT transform AS "transform_fn";
```
