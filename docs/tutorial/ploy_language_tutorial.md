# `.ploy` Language Tutorial

> **Document Version**: 3.0.0  
> **Last Updated**: 2026-05-07  
> **Project**: PolyglotCompiler 1.45.2  
> **Audience**: Developers writing `.ploy` glue, library authors, and anyone wiring multi-language pipelines through the `polyc` toolchain.

---

## Table of Contents

1. [What is `.ploy`?](#1-what-is-ploy)
2. [Hello, polyglot world](#2-hello-polyglot-world)
3. [Lexical structure](#3-lexical-structure)
4. [Module imports](#4-module-imports)
5. [Type system](#5-type-system)
6. [Variables and constants](#6-variables-and-constants)
7. [Operators and expressions](#7-operators-and-expressions)
8. [Control flow](#8-control-flow)
9. [Functions](#9-functions)
10. [Visibility and attributes (v1.16)](#10-visibility-and-attributes-v116)
11. [Generics (v1.15)](#11-generics-v115)
12. [Structured exceptions (v1.13)](#12-structured-exceptions-v113)
13. [Cooperative async / await (v1.14)](#13-cooperative-async--await-v114)
14. [Extended string literals (v1.17)](#14-extended-string-literals-v117)
15. [STRUCT, OPTION, pattern matching](#15-struct-option-pattern-matching)
16. [Object-oriented constructs](#16-object-oriented-constructs)
17. [Cross-language linking](#17-cross-language-linking)
18. [Type mapping and conversion](#18-type-mapping-and-conversion)
19. [Pipeline composition](#19-pipeline-composition)
20. [Diagnostic codes](#20-diagnostic-codes)
21. [Documentation comments and `polydoc` (v1.18)](#21-documentation-comments-and-polydoc-v118)
22. [Sample matrix tour](#22-sample-matrix-tour)
23. [Keyword reference](#23-keyword-reference)

---

# 1. What is `.ploy`?

`.ploy` is the cross-language DSL at the centre of PolyglotCompiler. A `.ploy` source file does three things:

1. Declares **modules** in any of the nine supported host languages (C++, Python, Rust, Java, .NET / C#, Go, JavaScript, Ruby, and `.ploy` itself).
2. Declares **typed bridges** (`LINK`, `MAP_TYPE`, `CONVERT`) between functions across those languages.
3. Hosts **polyglot business logic** in its own statement / expression language, dispatching to host-language code through `CALL`, `NEW`, `METHOD`, `GET`, `SET`, `WITH`, `DELETE`, `EXTEND`, and `PIPELINE`.

A `.ploy` file is parsed by the `frontend_ploy` library, lowered to the same SSA IR used by every other frontend, optimised by `middle_ir`, and emitted by one of the three backends (`backend_x86_64`, `backend_arm64`, `backend_wasm`).

---

# 2. Hello, polyglot world

```ploy
// hello.ploy — minimal self-contained program

FUNC main() -> i32 {
    PRINTLN "Hello, polyglot world!";
    RETURN 0;
}
```

Compile and run:

```bash
polyc hello.ploy -o hello
./hello
```

A two-language version reusing C++ for arithmetic and Python for formatting:

```ploy
// hello_polyglot.ploy
IMPORT cpp::math_ops;
IMPORT python::string_utils;

LINK(python, cpp, string_utils::format_result, math_ops::abs_val) RETURNS python::str {
    MAP_TYPE(python::int, cpp::int);
}

FUNC main() -> i32 {
    LET formatted = CALL(python, string_utils::format_result, -42);
    PRINTLN formatted;
    RETURN 0;
}
```

---

# 3. Lexical structure

## 3.1 Comments

```ploy
// Line comment.
/* Block comment. */
/// Doc comment harvested by polydoc (since v1.18).
```

## 3.2 Identifiers

`[A-Za-z_][A-Za-z0-9_]*`. Case-sensitive. Identifiers are interned by `frontend_common::SharedTokenPool`.

## 3.3 Keywords (54)

See section 23 for the complete reference. Keywords are uppercase by convention; the parser is **case-sensitive** (`func` is an identifier, `FUNC` is the keyword).

## 3.4 Literals

| Form                 | Example                                        | Notes                                                        |
|----------------------|------------------------------------------------|--------------------------------------------------------------|
| Integer              | `42`, `0xff`, `0o17`, `0b1010`, `1_000_000`    | Underscore separators allowed.                               |
| Floating-point       | `3.14`, `2.5e-3`, `1_000.5`                    |                                                              |
| Boolean              | `TRUE`, `FALSE`                                |                                                              |
| Null                 | `NULL`                                         |                                                              |
| String (regular)     | `"hello\nworld"`                               | Standard escapes (`\n`, `\t`, `\\`, `\"`, `\xNN`, `\uNNNN`). |
| String (raw)         | `r"C:\path"`, `r#"contains "quotes""#`         | No escapes; `#` padding for embedded quotes.                 |
| String (multiline)   | `"""line1\nline2"""`                           | Newlines preserved verbatim.                                 |
| String (template)    | `f"answer = {42}, pi = {3.14}"`                | Brace-delimited interpolation.                               |
| Character            | `'A'`, `'\n'`                                  |                                                              |

## 3.5 Whitespace and statement terminators

Statements terminate with `;`. Blocks use `{ ... }`. The parser treats line breaks as ordinary whitespace — there is no off-side rule.

---

# 4. Module imports

```ploy
IMPORT cpp::math_ops;            // C++ translation unit
IMPORT python::string_utils;     // Python module
IMPORT rust::data;               // Rust crate
IMPORT java::com.example.Util;   // Java FQN
IMPORT dotnet::App.Util;         // .NET namespace
IMPORT go::pkg.util;             // Go package
IMPORT javascript::./util.js;    // JS file (Node resolution)
IMPORT ruby::util;               // Ruby (require / Bundler)

IMPORT python::numpy PACKAGE "numpy" VERSION ">=1.21";
IMPORT java::org.json.JSONObject PACKAGE "org.json:json:20231013";
IMPORT rust::serde PACKAGE "serde" VERSION "1";
IMPORT dotnet::Newtonsoft.Json PACKAGE "Newtonsoft.Json" VERSION "13.0.*";
```

Per-language source-file location rules are documented in `docs/specs/ploy_language.md`. The driver resolves module paths via the same package-manager glue that `polyver detect` records.

---

# 5. Type system

## 5.1 Primitive types (case-insensitive synonyms)

| `.ploy`             | C++           | Python  | Rust  | Java     | .NET     | Go      | JS / Ruby             |
|---------------------|---------------|---------|-------|----------|----------|---------|------------------------|
| `INT` / `i32`       | `int`         | `int`   | `i32` | `int`    | `int`    | `int32` | `Number` / `Integer`   |
| `LONG` / `i64`      | `int64_t`     | `int`   | `i64` | `long`   | `long`   | `int64` | `BigInt` / `Integer`   |
| `i8`, `i16`, `u8` … | sized ints    | `int`   | typed | typed    | typed    | typed   | typed                  |
| `FLOAT` / `f32`     | `float`       | `float` | `f32` | `float`  | `float`  | `float32` | `Number` / `Float`   |
| `DOUBLE` / `f64`    | `double`      | `float` | `f64` | `double` | `double` | `float64` | `Number` / `Float`   |
| `BOOL`              | `bool`        | `bool`  | `bool`| `boolean`| `bool`   | `bool`  | `Boolean` / `TrueClass`|
| `STRING`            | `std::string` | `str`   | `String` | `String`| `string`| `string`| `String`              |
| `CHAR`              | `char`        | `str[1]`| `char` | `char`  | `char`   | `rune`  | n/a                    |
| `VOID`              | `void`        | `None`  | `()`  | `void`   | `void`   | n/a     | `null`                 |

## 5.2 Composite types

| Form              | Example                       | Description                                  |
|-------------------|-------------------------------|----------------------------------------------|
| `LIST<T>`         | `LIST<INT>`                   | Dynamic array.                               |
| `MAP<K, V>`       | `MAP<STRING, INT>`            | Hash map.                                    |
| `SET<T>`          | `SET<STRING>`                 | Hash set.                                    |
| `OPTION<T>`       | `OPTION<INT>`                 | Some / None ADT, destructured by `IF LET`.   |
| `RESULT<T, E>`    | `RESULT<INT, STRING>`         | Ok / Err ADT.                                |
| `STRUCT`          | `STRUCT Point { x: i32, y: i32 }` | Named record.                            |
| `T*`              | `INT*`                        | Pointer (host ABI).                          |

## 5.3 Type qualifiers

| Qualifier  | Meaning                                                   |
|------------|-----------------------------------------------------------|
| `CONST`    | Read-only binding.                                        |
| `MUTABLE`  | Mutable binding (default for `LET VAR`).                  |
| `PUB`      | Visible across module / link boundaries (v1.16).          |
| `PRIVATE`  | Visible only within current `.ploy` translation unit.     |

---

# 6. Variables and constants

```ploy
LET x = 42;                      // immutable, type inferred
LET y: i64 = 100;                // immutable, explicit type
LET VAR counter: i32 = 0;        // mutable
LET PI: DOUBLE = 3.14159265;     // file-scope constant

VAR scratch: STRING = "tmp";     // shorthand for LET VAR (top-level declarations)
```

`LET`-bindings must be initialised. `LET VAR` without an initialiser is a parse error (`polyc-err-E2001`).

---

# 7. Operators and expressions

| Class       | Operators                                              |
|-------------|--------------------------------------------------------|
| Arithmetic  | `+`, `-`, `*`, `/`, `%`, unary `-`                     |
| Comparison  | `==`, `!=`, `<`, `<=`, `>`, `>=`                       |
| Logical     | `AND`, `OR`, `NOT`, `&&`, `||`, `!`                    |
| Bitwise     | `&`, `|`, `^`, `~`, `<<`, `>>`                         |
| Assignment  | `=`, `+=`, `-=`, `*=`, `/=`, `%=`                      |
| Member      | `.`, `[]`                                              |
| Pointer     | `->`, unary `&`, unary `*`                             |
| Range       | `1..10` (half-open), `1..=10` (inclusive)              |

Precedence follows the standard C / Rust hybrid documented in `docs/specs/ploy_grammar.md` §6.

---

# 8. Control flow

## 8.1 IF / ELSE — outer parentheses are optional (v1.18)

```ploy
IF x > 0 {
    PRINTLN "positive";
} ELSE IF x < 0 {
    PRINTLN "negative";
} ELSE {
    PRINTLN "zero";
}

IF (x > 0) { PRINTLN "still ok with parens"; }
```

## 8.2 IF LET destructuring (v1.18)

```ploy
FUNC head_or_zero(opt: OPTION<i32>) -> i32 {
    IF LET Some(x) = opt {
        RETURN x;
    } ELSE {
        RETURN 0;
    }
}
```

## 8.3 WHILE / DO WHILE / FOR

```ploy
LET VAR i: i32 = 0;
WHILE i < 10 {
    PRINTLN i;
    i = i + 1;
}

FOR x IN xs {
    PRINTLN x;
}

FOR i IN 0..10 {
    PRINTLN i;
}
```

## 8.4 MATCH (pattern matching, since v1.10)

```ploy
MATCH value {
    1            => PRINTLN "one";
    2 | 3        => PRINTLN "two or three";
    n IF n > 10  => PRINTLN "big";
    _            => PRINTLN "other";
}
```

## 8.5 BREAK / CONTINUE / RETURN

```ploy
WHILE TRUE {
    IF done { BREAK; }
    IF skip { CONTINUE; }
}
RETURN 0;
```

---

# 9. Functions

```ploy
FUNC add(a: i32, b: i32) -> i32 {
    RETURN a + b;
}

// Default arguments (v1.10)
FUNC greet(name: STRING = "world") -> STRING {
    RETURN f"hello, {name}";
}

// Multiple return values via STRUCT or RESULT.
FUNC parse(s: STRING) -> RESULT<i32, STRING> {
    IF s == "0" { RETURN Ok(0); }
    RETURN Err("not a digit");
}
```

A `.ploy` program enters at `FUNC main() -> i32`. The driver also accepts `FUNC main()` (returning unit, treated as exit code 0).

---

# 10. Visibility and attributes (v1.16)

```ploy
PUB STRUCT Point { x: i32, y: i32 }   // visible across LINK boundaries
PRIVATE FUNC inner_helper() -> i32 { RETURN 7; }

@inline @hot
PUB FUNC fast_path(a: i32, b: i32) -> i32 { RETURN a + b; }

@deprecated("use fast_path")
PUB FUNC slow_path(a: i32, b: i32) -> i32 { RETURN a + b; }

@link_name("ploy_run")
PUB FUNC run() -> i32 { RETURN fast_path(inner_helper(), 1); }

EXPORT run AS "ploy_run";   // requires PUB; exporting PRIVATE is polyc-err-E2410
```

Built-in attributes:

| Attribute              | Effect                                                                |
|------------------------|-----------------------------------------------------------------------|
| `@inline`              | Hint to the inliner pass to inline aggressively.                      |
| `@hot`                 | Place into the hot section; influences PGO and code layout.           |
| `@cold`                | Place into the cold section.                                          |
| `@deprecated("msg")`   | Emit `polyc-warn-W2401` at every call site with `msg` as rationale.   |
| `@link_name("sym")`    | Override the externally visible symbol name.                          |
| `@target("feature,…")` | Constrain backend feature requirements (e.g. `avx2`, `neon`).         |
| `@no_mangle`           | Disable name mangling (interop with raw C ABIs).                      |

Unknown attributes raise `polyc-err-E2402`. The full catalogue lives in `docs/specs/ploy_attributes.md`.

---

# 11. Generics (v1.15)

```ploy
STRUCT Pair<A, B> {
    first: A,
    second: B
}

FUNC max<T: Comparable>(a: T, b: T) -> T {
    IF a > b { RETURN a; } ELSE { RETURN b; }
}

FUNC identity<T>(x: T) -> T { RETURN x; }

FUNC sum<T>(a: T, b: T) -> T WHERE T: Numeric {
    RETURN a + b;
}
```

Bounds are written either inline (`T: Comparable`) or as a trailing `WHERE` clause. The runtime currently follows the **type-erasure MVP** path described in `docs/realization/generics.md`; full per-instantiation monomorphisation is tracked as follow-up work.

Built-in trait bounds: `Comparable`, `Numeric`, `Hashable`, `Display`, `Clone`, `Send`.

---

# 12. Structured exceptions (v1.13)

```ploy
FUNC main() {
    TRY {
        THROW "boom";
    }
    CATCH (e: Error) {
        PRINTLN f"caught: {e.message}";
    }
    FINALLY {
        PRINTLN "cleanup";
    }
}
```

- `THROW <expr>` raises a value; non-`Error` values are wrapped into the unified `Error` handle by the runtime bridge.
- Multiple `CATCH` clauses are matched top-to-bottom by static type.
- `FINALLY` always runs, even when control leaves through `RETURN`, `BREAK`, or a re-throw.

Cross-language reverse-path interception of Python / C++ / Java / .NET / Rust exceptions is implemented in the runtime data plane; the IR-level dispatcher unifying every host model is tracked as future work.

---

# 13. Cooperative async / await (v1.14)

```ploy
ASYNC FUNC fetch_one() -> i32 { RETURN 1; }
ASYNC FUNC fetch_two() -> i32 { RETURN 2; }

ASYNC FUNC pipeline_demo() -> i32 {
    LET a = AWAIT fetch_one();
    LET b = AWAIT fetch_two();
    RETURN a + b;
}
```

- `ASYNC FUNC name(...) -> T` declares a function whose return value is implicitly wrapped as `Future<T>`.
- `AWAIT <expr>` suspends the surrounding ASYNC frame until the awaited future resolves.
- The bridging runtime ABI (`__ploy_rt_async_*`) is implemented in `runtime/services/async_bridge.{h,cpp}` and `runtime/services/event_loop.{h,cpp}`.

Inspect the cooperative loop with:

```bash
polyrt async --json        # snapshot
polyrt async --run=64      # advance the event loop by 64 ticks
```

The runtime async bridges connect to Python `asyncio`, Rust `Future`, C++20 coroutines, Java `CompletableFuture`, and .NET `Task<T>`.

---

# 14. Extended string literals (v1.17)

```ploy
LET path     = r"C:\projects\polyglot";                        // raw — no escapes
LET sql      = r#"SELECT "name", "age" FROM users"#;           // raw with # padding
LET haiku    = """An old silent pond
A frog jumps in
splash, silence again""";                                      // multiline
LET greeting = f"answer = {42}, pi = {3.14}";                   // template / interpolation
```

Template strings interpolate any expression typed `Display`. Mixed forms (`fr"..."`, `rf"..."`, etc.) are intentionally **not** part of the surface grammar — combine via concatenation or prefer `f"..."` plus `r"..."` substrings.

---

# 15. STRUCT, OPTION, pattern matching

```ploy
STRUCT Point { x: i32, y: i32 }

LET p: Point = Point { x: 1, y: 2 };
PRINTLN p.x;
PRINTLN p.y;

FUNC find(xs: LIST<i32>, key: i32) -> OPTION<i32> {
    FOR (i, x) IN xs.enumerate() {
        IF x == key { RETURN Some(i); }
    }
    RETURN None;
}

MATCH find(xs, 7) {
    Some(i) => PRINTLN f"index = {i}";
    None    => PRINTLN "not found";
}
```

---

# 16. Object-oriented constructs

`.ploy` exposes a host-neutral object model so cross-language calls have a single shape regardless of whether the receiver is a C++ object, a Python instance, a Java reference, a .NET handle, a Rust struct, etc.

| Operation                              | Syntax                                                  |
|----------------------------------------|---------------------------------------------------------|
| Construct a host object                | `LET p = NEW(python, Person, "Alice", 30);`             |
| Call an instance method                | `LET name = METHOD(p, "get_name");`                     |
| Read a field / property                | `LET age = GET(p, "age");`                              |
| Write a field / property               | `SET(p, "age", 31);`                                    |
| Borrow with explicit lifetime          | `WITH(p) { ... }`                                       |
| Release a host handle eagerly          | `DELETE(p);`                                            |
| Extend a host class with a `.ploy` impl | `EXTEND python::Animal AS Cat { ... }`                  |

See samples `05_class_instantiation` … `08_delete_extend` for a working tour.

---

# 17. Cross-language linking

`LINK` declarations bind a target callable to a source callable across language boundaries:

```ploy
LINK(cpp, python, math_ops::add, string_utils::concat) RETURNS cpp::int {
    MAP_TYPE(cpp::int, python::str);
    MAP_TYPE(cpp::int, python::str);
}
```

The directive form is:

```
LINK(<target_lang>, <source_lang>, <target_callable>, <source_callable>) RETURNS <target_type> {
    MAP_TYPE(<target_param_type>, <source_param_type>);
    ... // exactly one MAP_TYPE per parameter
}
```

Sema enforces:

- both callables must agree on arity (`polyc-err-E3104`);
- exactly one `MAP_TYPE` per parameter (`polyc-err-E3105`);
- the `RETURNS` type must be marshallable in the target language (`polyc-err-E3106`).

Once a link is declared, a `CALL` invokes the bridged callable through the runtime marshalling layer:

```ploy
LET sum = CALL(cpp, math_ops::add, 1, 2);
```

The marshalling is generated by `runtime/src/interop/` and recorded in the per-target call-graph (`polyc --emit=call-graph:cg.json`).

---

# 18. Type mapping and conversion

Global, file-wide type equivalences are declared with `MAP_TYPE`:

```ploy
MAP_TYPE(cpp::int,    python::int);
MAP_TYPE(cpp::double, python::float);
MAP_TYPE(rust::String, java::String);
```

Custom converters are registered with `CONVERT`:

```ploy
CONVERT(cpp::std::vector<int>, python::list) USING py_list_from_vec;
CONVERT(rust::Vec<u8>,          dotnet::byte[]) USING dotnet_bytes_from_vec;
```

The converter symbol must resolve to a runtime helper visible to the link target (`polyrt` records the symbol in the marshalling table).

---

# 19. Pipeline composition

`PIPELINE` chains a sequence of cross-language calls, threading the previous stage's output into the next stage's first parameter:

```ploy
PIPELINE preprocess(text: STRING) -> STRING {
    text
    | python::nlp::tokenize
    | rust::filter::lowercase
    | cpp::compress::deflate
    | dotnet::Cipher::encrypt
}
```

Sema verifies that adjacent stages either share a `MAP_TYPE` or have a `CONVERT` registered.

---

# 20. Diagnostic codes

Every `.ploy` diagnostic carries a stable id of the form `polyc-(err|warn)-<E####|W####>`. Highlights:

| Code                  | Meaning                                                        |
|-----------------------|----------------------------------------------------------------|
| `polyc-err-E2001`     | Missing initialiser on `LET` / `LET VAR`.                      |
| `polyc-err-E2102`     | Unknown identifier.                                            |
| `polyc-err-E2410`     | `EXPORT` of a `PRIVATE` declaration.                           |
| `polyc-err-E2402`     | Unknown `@attribute`.                                          |
| `polyc-err-E3104`     | LINK arity mismatch.                                           |
| `polyc-err-E3105`     | MAP_TYPE count mismatch.                                       |
| `polyc-err-E3106`     | Non-marshallable `RETURNS` type.                               |
| `polyc-warn-W2101`    | `--container` does not match resolved output suffix.           |
| `polyc-warn-W2401`    | Call to `@deprecated` API.                                     |
| `polyc-warn-W2501`    | Unused `IMPORT`.                                               |

Run `polyc --check file.ploy` to obtain the diagnostics as LSP-shaped JSON; the same payload powers the IDE Problems panel and `polyls`. The full table lives in `docs/specs/ploy_diagnostics.md`.

---

# 21. Documentation comments and `polydoc` (v1.18)

Lines beginning with `///` are doc comments harvested by the `polydoc` tool when attached to a top-level `FUNC`, `STRUCT`, `LET`, or `VAR`:

```ploy
/// Returns the absolute value of `n`.
/// Wrapping behaviour at i32::MIN follows the host backend.
FUNC abs(n: i32) -> i32 {
    IF n < 0 { RETURN -n; }
    RETURN n;
}

/// Maximum number of retries before giving up.
LET MAX_RETRY: i32 = 5;
```

```bash
polydoc file.ploy                # Markdown to stdout
polydoc --json file.ploy         # machine-readable
polydoc -o api.md file.ploy
```

The same doc payload is surfaced through `polyls` hover / signature-help responses.

---

# 22. Sample matrix tour

| Range            | Theme                                                                  |
|------------------|------------------------------------------------------------------------|
| `00_minimal`     | Single-line minimal sample with byte-pinned stdout.                    |
| `01` … `09`      | Core `.ploy` interop — LINK, MAP_TYPE, PIPELINE, control flow, OOP.    |
| `10` … `16`      | Diagnostics, Java / .NET interop, generic containers, async pipeline, full stack, CONFIG / VENV. |
| `17` … `30`      | Real domains — strings, numerics, file I/O, JSON, image, SQL, HTTP, concurrency, event loop, plugin, ML, analytics, game loop. |
| `31`, `32`       | Explicit-width integers, typed handles.                                |
| `33`, `34`       | Pattern matching, default arguments.                                   |
| `35`             | EXTEND on dynamic languages.                                           |
| `36`             | TRY / CATCH / FINALLY / THROW (v1.13).                                 |
| `37`             | ASYNC / AWAIT (v1.14).                                                 |
| `38`             | Generics with bounds (v1.15).                                          |
| `39`             | Visibility and attributes (v1.16).                                     |
| `40`             | Extended string literals (v1.17).                                      |
| `41`             | Grammar polish — optional outer parens, IF LET, `///` doc comments.    |

Drive the whole matrix with:

```bash
scripts/build_all_samples.sh   --polyc build/polyc --polyld build/polyld
scripts\build_all_samples.ps1  -Polyc build\polyc.exe -Polyld build\polyld.exe
```

The harness writes `samples_report.json` (top-level alphabetised `ok` array) which `samples_regression_test.cpp` cross-checks against the per-sample status field.

---

# 23. Keyword reference

The 54 `.ploy` keywords (current as of 1.45.2):

```
AND, AS, ASYNC, AWAIT, BREAK, CALL, CASE, CATCH, CONST, CONTINUE,
CONVERT, DELETE, DO, DOUBLE, ELSE, EXPORT, EXTEND, FALSE, FINALLY,
FLOAT, FOR, FUNC, GET, IF, IMPORT, IN, INT, LET, LINK, LIST, LONG,
MAP, MAP_TYPE, MATCH, METHOD, NEW, NOT, NULL, OPTION, OR, PACKAGE,
PIPELINE, PRIVATE, PUB, RESULT, RETURN, RETURNS, SET, STRING, STRUCT,
THROW, TRUE, TRY, USING, VAR, VOID, WHERE, WHILE, WITH
```

Type identifiers (`i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32`, `u64`, `f32`, `f64`, `Error`, `Future`, `Some`, `None`, `Ok`, `Err`, `Comparable`, `Numeric`, `Hashable`, `Display`, `Clone`, `Send`) are reserved identifiers but not keywords; redefining them is a hard error.

---

*Maintained by the PolyglotCompiler team*  
*Last updated: 2026-05-07*  
*Document version: v3.0.0*
