# .ploy Language Tutorial

> **Version**: 2.0.0
> **Last Updated**: 2026-02-22
> **Project**: PolyglotCompiler  

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [Getting Started](#2-getting-started)
3. [Module and Package Imports](#3-module-and-package-imports)
4. [Cross-Language Function Linking](#4-cross-language-function-linking)
5. [Type Mapping](#5-type-mapping)
6. [Variables and Expressions](#6-variables-and-expressions)
7. [Functions](#7-functions)
8. [Control Flow](#8-control-flow)
9. [Structs and Custom Types](#9-structs-and-custom-types)
10. [Cross-Language Calls](#10-cross-language-calls)
11. [Object-Oriented Interop](#11-object-oriented-interop)
12. [Pipelines](#12-pipelines)
13. [Environment Configuration](#13-environment-configuration)
14. [Type Conversion](#14-type-conversion)
15. [Exporting Symbols](#15-exporting-symbols)
16. [Error Handling and Diagnostics](#16-error-handling-and-diagnostics)
17. [Complete Examples](#17-complete-examples)
18. [Keyword Reference](#18-keyword-reference)
19. [Best Practices](#19-best-practices)

---

# 1. Introduction

## 1.1 What Is .ploy?

`.ploy` is a **domain-specific language** (DSL) designed for the PolyglotCompiler project. It provides a declarative way to express **cross-language function-level linking** and **object-oriented interoperability** between heterogeneous source languages, including C++, Python, Rust, Java, and C# (.NET).

Unlike general-purpose programming languages, `.ploy` acts as **"cross-language glue"** — it describes *how* functions, classes, and data flow between different language runtimes in a single, unified syntax.

## 1.2 What Can .ploy Do?

- **Link functions** from different languages (e.g., call a C++ function from Python)
- **Instantiate classes** from one language within another
- **Access and modify attributes** on foreign objects
- **Manage resources** automatically across language boundaries
- **Orchestrate multi-stage pipelines** that span multiple language runtimes
- **Manage package dependencies** (pip, conda, uv, cargo, NuGet, etc.)
- **Map types** between different language type systems

## 1.3 Supported Languages

| Language | Frontend | Identifier |
|----------|----------|------------|
| C++ | `frontend_cpp` | `cpp` |
| Python | `frontend_python` | `python` |
| Rust | `frontend_rust` | `rust` |
| Java | `frontend_java` | `java` |
| C# (.NET) | `frontend_dotnet` | `dotnet` |

## 1.4 File Extension

All `.ploy` source files use the `.ploy` extension:

```
my_project.ploy
pipeline.ploy
config_and_venv.ploy
```

---

# 2. Getting Started

## 2.1 Your First .ploy File

Create a file named `hello.ploy`:

```ploy
// hello.ploy — A minimal cross-language example

// Import modules from C++ and Python
IMPORT cpp::math_ops;
IMPORT python::string_utils;

// Link a C++ function to a Python function
LINK(cpp, python, math_ops::add, string_utils::format_result) {
    MAP_TYPE(cpp::int, python::int);
}

// Define a polyglot function
FUNC greet(a: INT, b: INT) -> STRING {
    LET sum = CALL(cpp, math_ops::add, a, b);
    LET message = CALL(python, string_utils::format_result, sum);
    RETURN message;
}

// Export the function for external use
EXPORT greet AS "polyglot_greet";
```

## 2.2 Compiling

Use the `polyc` driver to compile a `.ploy` file:

```bash
polyc hello.ploy -o hello
```

The driver automatically detects the `.ploy` extension and routes the file through the .ploy frontend (Lexer → Parser → Sema → Lowering).

## 2.3 Basic Program Structure

A typical `.ploy` file follows this order:

```ploy
// 1. Environment configuration (optional)
CONFIG VENV python "/path/to/venv";

// 2. Package imports (optional)
IMPORT python PACKAGE numpy >= 1.20 AS np;

// 3. Module imports
IMPORT cpp::module_name;
IMPORT python::module_name;

// 4. Cross-language links
LINK(cpp, python, func_a, func_b) {
    MAP_TYPE(cpp::int, python::int);
}

// 5. Type mappings
MAP_TYPE(cpp::double, python::float);

// 6. Struct definitions (optional)
STRUCT Config {
    width: INT;
    height: INT;
}

// 7. Functions and pipelines
FUNC main_func() -> INT { ... }
PIPELINE my_pipeline { ... }

// 8. Exports
EXPORT main_func;
EXPORT my_pipeline;
```

## 2.4 Comments

`.ploy` supports single-line comments with `//`:

```ploy
// This is a comment
FUNC foo() -> INT {
    LET x = 42;  // Inline comment
    RETURN x;
}
```

## 2.5 Statement Termination

All statements end with a semicolon (`;`):

```ploy
LET x = 10;
VAR y: FLOAT = 3.14;
RETURN x;
```

---

# 3. Module and Package Imports

## 3.1 Module Imports — IMPORT

Use `IMPORT` to bring modules from other languages into scope:

```ploy
// Qualified module import: <language>::<module_name>
IMPORT cpp::math_utils;
IMPORT python::data_processing;
IMPORT rust::serde;
IMPORT java::SortEngine;
IMPORT dotnet::Transformer;
```

These modules correspond to actual source files (e.g., `math_utils.cpp`, `data_processing.py`) in the project.

## 3.2 Package Imports — IMPORT PACKAGE

Import external packages from each language's ecosystem:

```ploy
// Basic package import
IMPORT python PACKAGE numpy;

// With version constraint
IMPORT python PACKAGE numpy >= 1.20;
IMPORT python PACKAGE scipy == 1.11;
IMPORT rust PACKAGE serde >= 1.0;

// With alias
IMPORT python PACKAGE numpy >= 1.20 AS np;
IMPORT python PACKAGE pandas >= 2.0 AS pd;

// Selective import (specific symbols)
IMPORT python PACKAGE numpy::(array, mean, std);
IMPORT python PACKAGE torch::(tensor, no_grad) >= 2.0;

// Sub-module selective import
IMPORT python PACKAGE numpy.linalg::(solve, inv);
```

### Version Operators

| Operator | Meaning | Example |
|----------|---------|---------|
| `>=` | Greater or equal | `>= 1.20` |
| `<=` | Less or equal | `<= 2.0` |
| `==` | Exact match | `== 1.10.0` |
| `>` | Strictly greater | `> 1.0` |
| `<` | Strictly less | `< 3.0` |
| `!=` | Not equal | `!= 2.0` |
| `~=` | Compatible (PEP 440) | `~= 1.20` |

### Rules

1. Selective import `::()` and `AS` alias **cannot be used simultaneously**.
2. Version constraints are optional.
3. At most one CONFIG per language per compilation unit.

---

# 4. Cross-Language Function Linking

## 4.1 LINK — Declaring a Cross-Language Binding

The `LINK` directive tells the PolyglotLinker to generate **glue code** that bridges function calls between two languages:

```ploy
// Basic LINK syntax
LINK(source_lang, target_lang, source_function, target_function) {
    MAP_TYPE(source_lang::type, target_lang::type);
}
```

### Example: Link C++ to Python

```ploy
// Link C++ math output to Python formatting
LINK(cpp, python, math_ops::add, string_utils::format_result) {
    MAP_TYPE(cpp::int, python::int);
}
```

### Example: Link Java to Python

```ploy
LINK(java, python, SortEngine::sortedKeys, collection_utils::invert_dict) {
    MAP_TYPE(java::ArrayList_String, python::list);
    MAP_TYPE(java::HashMap_String_Integer, python::dict);
}
```

### Example: Link Python to .NET

```ploy
LINK(python, dotnet, data_science::to_json, Transformer::ParseJson) {
    MAP_TYPE(python::str, dotnet::string);
}
```

## 4.2 LINK Body

The body of a LINK block contains `MAP_TYPE` declarations that specify how types are converted at the boundary:

```ploy
LINK(cpp, rust, image_processor::edge_detect, data_loader::merge_chunks) {
    MAP_TYPE(cpp::std::vector_double, rust::Vec_f64);
    MAP_TYPE(cpp::int, rust::usize);
}
```

---

# 5. Type Mapping

## 5.1 MAP_TYPE — Declaring Type Equivalences

`MAP_TYPE` tells the compiler how to marshal data between language-specific types:

```ploy
// Basic type mappings
MAP_TYPE(cpp::int, python::int);
MAP_TYPE(cpp::double, python::float);
MAP_TYPE(cpp::std::string, python::str);

// Container type mappings
MAP_TYPE(cpp::std::vector_int, java::ArrayList_Integer);
MAP_TYPE(java::HashMap_String_Integer, python::dict);
MAP_TYPE(rust::Vec_f64, python::list);
MAP_TYPE(dotnet::List_double, python::list);
```

## 5.2 Primitive Type Mappings

The .ploy type system provides platform-independent primitive types:

| .ploy Type | C++ | Python | Rust | Java | C# |
|-----------|-----|--------|------|------|-----|
| `INT` | `int` | `int` | `i32` | `int` | `int` |
| `FLOAT` | `double` | `float` | `f64` | `double` | `double` |
| `STRING` | `std::string` | `str` | `String` | `String` | `string` |
| `BOOL` | `bool` | `bool` | `bool` | `boolean` | `bool` |
| `VOID` | `void` | `None` | `()` | `void` | `void` |

## 5.3 Container Type Mappings

| .ploy Type | C++ | Python | Rust |
|-----------|-----|--------|------|
| `LIST<T>` | `std::vector<T>` | `list[T]` | `Vec<T>` |
| `TUPLE<T...>` | `std::tuple<T...>` | `tuple` | `(T...)` |
| `DICT<K,V>` | `std::unordered_map<K,V>` | `dict[K,V]` | `HashMap<K,V>` |
| `ARRAY<T,N>` | `T[N]` | `list[T]` | `[T; N]` |
| `OPTION<T>` | `std::optional<T>` | `Optional[T]` | `Option<T>` |

---

# 6. Variables and Expressions

## 6.1 LET — Immutable Variable

`LET` declares an **immutable** variable whose value cannot be reassigned:

```ploy
LET x = 42;
LET name: STRING = "PolyglotCompiler";
LET pi: FLOAT = 3.14159;
LET flag: BOOL = TRUE;
```

## 6.2 VAR — Mutable Variable

`VAR` declares a **mutable** variable whose value can be reassigned:

```ploy
VAR counter = 0;
VAR total: FLOAT = 0.0;

counter = counter + 1;   // Reassignment allowed
total = total + 3.14;
```

## 6.3 Expressions

### Arithmetic Operators

| Operator | Meaning | Example |
|----------|---------|---------|
| `+` | Addition | `a + b` |
| `-` | Subtraction | `a - b` |
| `*` | Multiplication | `a * b` |
| `/` | Division | `a / b` |
| `%` | Modulo | `a % b` |

### Comparison Operators

| Operator | Meaning | Example |
|----------|---------|---------|
| `==` | Equal | `a == b` |
| `!=` | Not equal | `a != b` |
| `<` | Less than | `a < b` |
| `>` | Greater than | `a > b` |
| `<=` | Less or equal | `a <= b` |
| `>=` | Greater or equal | `a >= b` |

### Logical Operators

| Operator | Meaning | Example |
|----------|---------|---------|
| `AND` | Logical AND | `a AND b` |
| `OR` | Logical OR | `a OR b` |
| `NOT` | Logical NOT | `NOT a` |

### Literals

```ploy
// Integer literals
LET a = 42;
LET b = -10;

// Float literals
LET c = 3.14;
LET d = -0.5;

// String literals
LET s = "Hello, World!";

// Boolean literals
LET t = TRUE;
LET f = FALSE;

// Null
LET n = NULL;

// Container literals
LET list = [1, 2, 3];
LET tuple = (1, "hello", 3.14);
LET dict = {"key1": 10, "key2": 20};
```

---

# 7. Functions

## 7.1 FUNC — Function Declaration

Define functions with typed parameters and a return type:

```ploy
FUNC add(a: INT, b: INT) -> INT {
    RETURN a + b;
}

FUNC greet(name: STRING) -> STRING {
    LET message = "Hello, " + name;
    RETURN message;
}

FUNC process(data: ARRAY[FLOAT], size: INT) -> FLOAT {
    LET result = CALL(cpp, math_ops::sum, data, size);
    RETURN result;
}
```

## 7.2 RETURN

Every non-void function must return a value using `RETURN`:

```ploy
FUNC compute(x: INT) -> INT {
    IF x > 0 {
        RETURN x * 2;
    } ELSE {
        RETURN 0;
    }
}
```

## 7.3 Void Functions

Functions that return nothing use `VOID` as the return type:

```ploy
FUNC log_message(msg: STRING) -> VOID {
    CALL(python, logger::info, msg);
}
```

## 7.4 Default Parameter Values & Named Arguments *(since 1.11.0)*

Trailing parameters may carry a default expression introduced by `=`,
and call sites may name any argument:

```ploy
FUNC add(x: i32, y: i32 = 0) -> i32 { RETURN x + y; }

LET a = add(10);          // y defaults to 0
LET b = add(x: 7);        // single named argument
LET c = add(2, y: 5);     // mixed positional + named
```

A default expression must be a constant-foldable literal/unary/binary
expression, **or** a pure intra-Ploy `FUNC` call:

```ploy
FUNC one() -> i32 { RETURN 1; }
FUNC scale(value: i32, factor: i32 = one()) -> i32 {
    RETURN value * factor;
}
```

Cross-language `CALL`, closure capture, and reading another parameter
inside a default are all rejected.  Positional arguments may not
appear after a named argument, and every required (no-default)
parameter must be supplied either by position or by name.

---

# 8. Control Flow

## 8.1 IF / ELSE

Conditional branching:

```ploy
IF prediction > threshold {
    RETURN 1;
} ELSE {
    RETURN 0;
}
```

With `ELSE IF`:

```ploy
IF score >= 90 {
    LET grade = "A";
} ELSE IF score >= 80 {
    LET grade = "B";
} ELSE IF score >= 70 {
    LET grade = "C";
} ELSE {
    LET grade = "F";
}
```

## 8.2 WHILE — Loop

```ploy
VAR i = 0;
WHILE i < 10 {
    CALL(python, process::step, i);
    i = i + 1;
}
```

## 8.3 FOR — Iteration

Iterate over ranges or collections:

```ploy
// Range-based loop
FOR i IN 0..10 {
    CALL(cpp, engine::tick, i);
}

// Collection-based loop
FOR item IN collection {
    CALL(python, processor::handle, item);
}
```

## 8.4 MATCH — Pattern Matching

`MATCH` dispatches on a single scrutinee.  Each `CASE` carries a
*pattern* and an optional `IF` guard; the catch-all is either
`DEFAULT` or the wildcard `CASE _`.

```ploy
MATCH mode {
    CASE 0 {
        RETURN CALL(python, ml_model::predict, data);
    }
    CASE 1 {
        CALL(cpp, image_processor::enhance, data, 100);
        RETURN CALL(python, ml_model::predict, data);
    }
    CASE 2 {
        CALL(cpp, image_processor::threshold, data, 100, 0.5);
        RETURN CALL(python, ml_model::predict, data);
    }
    DEFAULT {
        RETURN 0.0;
    }
}
```

Beyond literal arms, the `.ploy` `MATCH` accepts the full pattern
grammar:

```ploy
MATCH value {
    CASE 0                    { RETURN 100; }     // literal
    CASE 2 | 4 | 8 | 16       { RETURN 102; }     // OR-pattern
    CASE 10..20               { RETURN 103; }     // half-open range
    CASE 20..=29              { RETURN 104; }     // inclusive range
    CASE n @ 100..=199        { RETURN n + 200; } // binding on a range
    CASE x: i32 IF x > 1000   { RETURN x - 1000; }// type-guard + IF guard
    CASE _                    { RETURN -1; }      // wildcard catch-all
}
```

Tuple, struct and `OPTION` destructuring also work:

```ploy
MATCH point {
    CASE Point { x: 0, y: 0, .. } { RETURN "origin"; }
    CASE Point { x, y, .. }       { RETURN "general"; }
}

MATCH opt {
    CASE Some(x) { RETURN x; }
    CASE None    { RETURN -1; }
}
```

> **Notes**:
> * Each `CASE` body executes and **automatically exits** the
>   `MATCH` — there is no C-style fall-through.
> * `MATCH` on `bool` and `OPTION` is exhaustive when every variant
>   is covered, even without `DEFAULT`.
> * The arrow between the pattern and the body (`->` or `=>`) is
>   optional; the canonical form omits it.
> * Extended pattern semantics, the lowering strategy and the
>   diagnostic catalogue live in
>   [`docs/realization/pattern_matching.md`](../realization/pattern_matching.md).

## 8.5 BREAK and CONTINUE

Control loop execution:

```ploy
VAR current = CALL(python, ml_model::predict, data);
VAR iteration = 0;

WHILE iteration < max_iter {
    // Break out early if target is reached
    IF current > target {
        BREAK;
    }

    CALL(cpp, image_processor::enhance, data, 100);
    current = CALL(python, ml_model::predict, data);
    iteration = iteration + 1;
}
```

```ploy
FOR i IN 0..100 {
    IF i % 2 == 0 {
        CONTINUE;  // Skip even numbers
    }
    CALL(python, process::handle_odd, i);
}
```

---

# 9. Structs and Custom Types

## 9.1 STRUCT — Structure Definition

Define composite data types:

```ploy
STRUCT PipelineConfig {
    width: INT;
    height: INT;
    channels: INT;
    learning_rate: FLOAT;
    epochs: INT;
}

STRUCT Experiment {
    name: STRING;
    epochs: INT;
    learning_rate: FLOAT;
    converged: BOOL;
}
```

## 9.2 Struct Literals

Create struct instances using literal syntax:

```ploy
LET config = PipelineConfig {
    width: 1920,
    height: 1080,
    channels: 3,
    learning_rate: 0.001,
    epochs: 100
};
```

## 9.3 Member Access

Access struct fields with the dot operator:

```ploy
LET w = config.width;
LET lr = config.learning_rate;
```

---

# 10. Cross-Language Calls

## 10.1 CALL — Invoking a Function from Another Language

The `CALL` keyword executes a function from a specific language runtime:

```ploy
// Syntax: CALL(language, module::function, arg1, arg2, ...)
LET sum = CALL(cpp, math_ops::add, 10, 20);
LET formatted = CALL(python, string_utils::format_result, sum);
LET data = CALL(rust, data_loader::load_batch, "data.csv", 64);
LET sorted = CALL(java, SortEngine::sortInts, numbers);
```

### Cross-Language Call Chain

You can chain calls across languages, passing one language's output as another's input:

```ploy
FUNC process_data(input: ARRAY[FLOAT]) -> STRING {
    // Step 1: C++ preprocessing
    LET enhanced = CALL(cpp, image_processor::enhance, input, 100);
    
    // Step 2: Python ML inference
    LET prediction = CALL(python, ml_model::predict, enhanced);
    
    // Step 3: Rust post-processing
    LET compressed = CALL(rust, data_loader::compress, prediction);
    
    // Step 4: Python formatting
    LET report = CALL(python, string_utils::format_result, compressed);
    
    RETURN report;
}
```

---

# 11. Object-Oriented Interop

`.ploy` provides seven keywords for full cross-language OOP interoperability: `NEW`, `METHOD`, `GET`, `SET`, `WITH`, `DELETE`, and `EXTEND`.

## 11.1 NEW — Class Instantiation

Create class instances from any supported language:

```ploy
// Syntax: NEW(language, module::ClassName, arg1, arg2, ...)
LET mat = NEW(cpp, matrix::Matrix, 3, 3, 1.0);
LET model = NEW(python, model::LinearModel, 3, 1);
LET parser = NEW(rust, serde::json::Parser);
LET engine = NEW(java, SortEngine);
LET transformer = NEW(dotnet, Transformer);
```

The returned value is an **opaque handle** (of type `Any`) that can be passed to `METHOD`, `GET`, `SET`, `WITH`, and `DELETE`.

## 11.2 METHOD — Method Invocation

Call methods on foreign objects:

```ploy
// Syntax: METHOD(language, object, method_name, arg1, arg2, ...)
METHOD(cpp, mat, set, 0, 0, 5.0);
METHOD(cpp, mat, set, 1, 1, 10.0);
LET val = METHOD(cpp, mat, get, 0, 0);
LET norm = METHOD(cpp, mat, norm);

LET prediction = METHOD(python, model, forward, [1.0, 2.0, 3.0]);
LET params = METHOD(python, model, parameters);
```

### Chained Method Calls

```ploy
LET model = NEW(python, torch::nn::Linear, 784, 10);
LET optimizer = NEW(python, torch::optim::Adam,
                    METHOD(python, model, parameters), 0.001);
METHOD(python, optimizer, zero_grad);
LET loss = METHOD(python, model, compute_loss, predictions, labels);
METHOD(python, loss, backward);
METHOD(python, optimizer, step);
```

## 11.3 GET — Reading Attributes

Read an attribute from a foreign object:

```ploy
// Syntax: GET(language, object, attribute_name)
LET app = GET(python, settings_obj, app_name);
LET debug = GET(python, settings_obj, debug);
LET val = GET(cpp, sensor, value);
LET active = GET(cpp, sensor, active);
```

## 11.4 SET — Writing Attributes

Write a value to a foreign object's attribute:

```ploy
// Syntax: SET(language, object, attribute_name, value)
SET(python, counter, count, 100);
SET(python, counter, step, 5);
SET(cpp, sensor, value, 99.9);
SET(cpp, sensor, active, FALSE);
```

## 11.5 WITH — Resource Management

Automatic resource management, similar to Python's `with` statement:

```ploy
// Syntax: WITH(language, resource_expression) AS variable { body }
LET f = NEW(python, open, "data.csv");
WITH(python, f) AS handle {
    LET data = METHOD(python, handle, read);
    LET lines = METHOD(python, data, splitlines);
}
// handle is automatically closed after the block
```

## 11.6 DELETE — Object Destruction

Explicitly destroy foreign objects:

```ploy
// Syntax: DELETE(language, object)
LET obj = NEW(cpp, engine::GameObject, "Player", 0.0, 0.0, 0.0);
LET renderer = NEW(cpp, engine::Renderer, 1920, 1080, "OpenGL");

// Use the objects ...
METHOD(cpp, obj, move, 10.0, 5.0, 0.0);

// Explicitly destroy them
DELETE(cpp, renderer);  // Invokes C++ destructor
DELETE(cpp, obj);
DELETE(python, body);   // Releases Python reference
```

## 11.7 EXTEND — Class Extension

Extend a class from another language:

```ploy
// Syntax: EXTEND(language, base_class) { body }
EXTEND(python, base_classes::Component) {
    FUNC custom_update(dt: FLOAT) -> VOID {
        CALL(python, base_classes::Component::update, dt);
    }
}
```

> **Restriction (since 1.11.0).** `EXTEND` is accepted **only** on
> the dynamic host languages `python`, `ruby`, and `javascript`
> (with the tag aliases `rb`, `js`, `typescript`, `ts`).  Targeting
> any statically-typed language — `cpp`, `c`, `rust`, `java`,
> `dotnet` / `csharp`, `go` / `golang` — is rejected by sema; the
> recommended alternative is a local Ploy `FUNC` wrapper that uses
> `CALL` / `METHOD` to invoke the foreign API.  See sample
> [`tests/samples/35_extend_dynamic`](../../tests/samples/35_extend_dynamic/)
> for the migration pattern.

---

# 12. Pipelines

## 12.1 PIPELINE — Multi-Stage Workflow

Pipelines organize multi-stage, cross-language workflows into a single reusable unit:

```ploy
PIPELINE image_classification {

    // Stage 1: Preprocess in C++
    FUNC preprocess(input: ARRAY[FLOAT], size: INT) -> ARRAY[FLOAT] {
        CALL(cpp, image_processor::gaussian_blur, input, size, 1.5);
        CALL(cpp, image_processor::enhance, input, size);
        RETURN input;
    }

    // Stage 2: Classify in Python
    FUNC classify(data: ARRAY[FLOAT], threshold: FLOAT) -> INT {
        LET prediction = CALL(python, ml_model::predict, data);
        IF prediction > threshold {
            RETURN 1;
        } ELSE {
            RETURN 0;
        }
    }

    // Stage 3: Batch processing with FOR loop
    FUNC process_batch(batch_size: INT) -> INT {
        VAR total_positive = 0;
        FOR i IN 0..batch_size {
            LET sample = [1.0, 2.0, 3.0];
            LET result = CALL(python, ml_model::classify, sample, 0.5);
            IF result == 1 {
                total_positive = total_positive + 1;
            }
        }
        RETURN total_positive;
    }

    // Stage 4: MATCH-based dispatch
    FUNC dispatch(mode: INT, data: ARRAY[FLOAT]) -> FLOAT {
        MATCH mode {
            CASE 0 {
                RETURN CALL(python, ml_model::predict, data);
            }
            CASE 1 {
                CALL(cpp, image_processor::enhance, data, 100);
                RETURN CALL(python, ml_model::predict, data);
            }
            DEFAULT {
                RETURN 0.0;
            }
        }
    }
}
```

## 12.2 Pipeline with OOP

Pipelines can use `NEW`, `METHOD`, and all OOP features:

```ploy
PIPELINE training_pipeline {
    FUNC train(epochs: INT) -> FLOAT {
        LET model = NEW(python, model::LinearModel, 4, 2);
        LET loader = NEW(python, model::DataLoader, data, 1);

        VAR total_loss = 0.0;
        VAR epoch = 0;

        WHILE epoch < epochs {
            LET batch = METHOD(python, loader, next_batch);
            LET mat = NEW(cpp, matrix::Matrix, 1, 4, 0.0);
            LET loss = METHOD(python, model, train_step,
                             [1.0, 2.0, 3.0, 4.0], [1.0, 0.0]);
            total_loss = total_loss + loss;
            epoch = epoch + 1;
        }

        METHOD(python, loader, reset);
        RETURN total_loss;
    }
}
```

---

# 13. Environment Configuration

## 13.1 CONFIG — Package Manager Setup

Configure which package manager environment to use for package resolution:

```ploy
// Python virtual environment (venv)
CONFIG VENV python "env/python";
CONFIG VENV python "/opt/ml-env";

// Conda environment
CONFIG CONDA python "ml_env";

// uv-managed environment
CONFIG UV python "D:/venvs/uv_env";

// Pipenv project
CONFIG PIPENV python "C:/projects/myapp";

// Poetry project
CONFIG POETRY python "C:/projects/poetry_app";

// .NET environment
CONFIG VENV dotnet "env/dotnet";
```

### Rules

1. **One CONFIG per language** per compilation unit — duplicates cause a compile-time error.
2. **CONFIG must appear before IMPORT PACKAGE** directives that depend on it.
3. The language parameter is optional and defaults to `python`.

## 13.2 Practical Example

```ploy
// Set up Python environment
CONFIG VENV python "env/python";

// Import versioned packages from the configured environment
IMPORT python PACKAGE numpy >= 1.24 AS np;
IMPORT python PACKAGE pandas >= 2.0 AS pd;
IMPORT python PACKAGE scipy >= 1.11 AS sp;

// Set up .NET environment
CONFIG VENV dotnet "env/dotnet";

// Import .NET packages
IMPORT dotnet PACKAGE Newtonsoft.Json >= 13.0 AS njson;
```

> See [Sample 16: config_and_venv](../../tests/samples/16_config_and_venv/) for a full example.

---

# 14. Type Conversion

## 14.1 CONVERT — Explicit Type Marshalling

When automatic type mapping is insufficient, use `CONVERT` to explicitly marshal data between language-specific types:

```ploy
// Syntax: CONVERT(expression, target_type)

// Convert a Python list to a .NET List<double>
LET py_list = CALL(python, data_science::random_matrix, 1, 5);
LET dotnet_arr = CONVERT(py_list, dotnet::List_double);

// Convert back: .NET List<double> to Python list
LET py_flat = CONVERT(flat, python::list);

// Convert a primitive type
LET float_val = CONVERT(int_val, FLOAT);
```

## 14.2 MAP_FUNC — Mapping Function

Define a custom conversion function:

```ploy
MAP_FUNC convert_to_float(x: INT) -> FLOAT {
    LET result = CONVERT(x, FLOAT);
    RETURN result;
}
```

---

# 15. Exporting Symbols

## 15.1 EXPORT — Making Symbols Available

Use `EXPORT` to make functions and pipelines accessible from outside the `.ploy` module:

```ploy
// Export with original name
EXPORT compute;
EXPORT my_pipeline;

// Export with alias
EXPORT compute_and_format AS "polyglot_compute";
EXPORT distance_report AS "polyglot_distance";
EXPORT image_classification AS "image_pipeline";
```

---

# 16. Error Handling and Diagnostics

The `.ploy` semantic analyser produces detailed error messages when it detects problems. Here are common error scenarios and their diagnostics:

## 16.1 Parameter Count Mismatch

```ploy
// Error: cpp::compute expects 3 params, only 1 given
FUNC param_count_error() -> FLOAT {
    LET result = CALL(cpp, bad_functions::compute, 42);
    RETURN result;
}
```

```
Error [E3010]: Parameter count mismatch in call to 'compute'
  --> error_handling.ploy:44:9
   | Expected 3 argument(s), got 1
   = suggestion: Check the function signature for 'compute'
```

## 16.2 Type Mismatch

```ploy
// Error: python::process expects (INT, INT), but (STRING, INT) is given
FUNC type_mismatch_error() -> FLOAT {
    LET result = CALL(python, bad_functions::process, "hello", 42);
    RETURN result;
}
```

```
Error [E3011]: Type mismatch for parameter 1 in call to 'process'
  --> error_handling.ploy:58:9
   | Expected 'INT', got 'STRING'
   = suggestion: Consider using CONVERT to convert the argument type
```

## 16.3 Undefined Module

```ploy
// Error: Module 'nonexistent' is not imported
LET x = CALL(cpp, nonexistent::foo, 1);
```

## 16.4 Duplicate CONFIG

```ploy
CONFIG VENV python "env1";
CONFIG VENV python "env2";  // Error: Duplicate CONFIG for language 'python'
```

> See [Sample 10: error_handling](../../tests/samples/10_error_handling/) for a complete error scenario catalogue.

## 16.5 Structured Exception Handling — TRY / CATCH / FINALLY / THROW *(since v1.13.0)*

Use `TRY` / `CATCH` / `FINALLY` to handle runtime failures, and
`THROW` to raise an Error.  The catch binding has the built-in
`Error` handle type with `message: String`, `source_lang: String`,
and `stacktrace: List<String>`.

```ploy
FUNC main() {
    TRY {
        THROW "boom";
    }
    CATCH (e: Error) {
        PRINTLN "caught\r\n";
    }
    FINALLY {
        PRINTLN "cleanup\r\n";
    }
}
```

A bare `TRY` without any `CATCH` or `FINALLY` is rejected.  Multiple
`CATCH` clauses are accepted and dispatched in declaration order.  A
standalone `FINALLY` (no `CATCH`) is allowed for guaranteed cleanup.

The runtime bridge maps host-language exceptions (Python `Exception`,
C++ `std::exception`, Java `Throwable`, .NET `Exception`, Rust
`Result::Err`) onto the unified `Error` handle, so a single `CATCH`
clause can intercept failures originating from any linked language.

> See [Sample 36: try_catch](../../tests/samples/36_try_catch/) for the
> end-to-end example and `docs/realization/error_handling.md` for the
> IR / ABI model.

## 16.6 Cooperative Async / Await — ASYNC FUNC / AWAIT *(since v1.14.0)*

Use `ASYNC FUNC` to declare a cooperative asynchronous function and
`AWAIT` to suspend until a future resolves.  An `ASYNC FUNC` whose
declared return type is `T` is implicitly wrapped as `Future<T>` at
the ABI boundary.  `AWAIT <expr>` is only legal inside an `ASYNC
FUNC` body — sema rejects every other use.

```ploy
ASYNC FUNC fetch_one() -> i32 { RETURN 1; }
ASYNC FUNC fetch_two() -> i32 { RETURN 2; }

ASYNC FUNC chained() -> i32 {
    LET a = AWAIT fetch_one();
    LET b = AWAIT fetch_two();
    RETURN 0;
}
```

The cooperative event loop (single thread by default; a
work-stealing pool layered on top of `runtime/threading.{h,cpp}` is
the planned follow-up) lives in `runtime/services/async_bridge.cpp`
and `runtime/services/event_loop.cpp`.  Per-language adapters bridge
host awaitables (Python `asyncio` coroutines, Rust `Future`, C++20
`std::coroutine`, Java `CompletableFuture`, .NET `Task<T>`) onto the
same `Future<T>` handle through `__ploy_rt_future_resolve`.

The `polyrt async` CLI prints a snapshot of the cooperative event
loop; `polyrt async --json` emits the same payload as JSON, and
`polyrt async --run[=N]` drives the loop for at most `N` ticks before
reporting.

> See [Sample 37: async_await](../../tests/samples/37_async_await/)
> for the end-to-end example and
> `docs/realization/async_model.md` for the IR / ABI model.

## 16.7 Generics — FUNC<T> / STRUCT<T> with bounds *(since v1.15.0)*

Use `<T: Bound1 + Bound2, U>` after a `FUNC` or `STRUCT` name to
introduce generic type parameters with optional trait bounds.  The
optional `WHERE` clause between the return type and the body augments
the matching parameter's bounds.

```ploy
FUNC max<T: Comparable>(a: T, b: T) -> T {
    IF (a > b) { RETURN a; } ELSE { RETURN b; }
}

STRUCT Pair<A, B> { first: A, second: B }

FUNC sum<T>(a: T, b: T) -> T WHERE T: Numeric {
    RETURN a + b;
}
```

The built-in trait registry is `Comparable`, `Hashable`, `Numeric`,
`Iterable`, `Display`; sema rejects unknown bound names.  The
v1.15.0 lowering path is type-erased — every type parameter
resolves to `Any` and the function or struct is lowered once — so
a single body services every call site.

> See [Sample 38: generics](../../tests/samples/38_generics/) for the
> end-to-end example and `docs/realization/generics.md` for the
> IR / ABI model.

## 16.8 Visibility (PUB / PRIVATE) and Attributes (@name) *(since v1.16.0)*

Use `PUB` to export a top-level `FUNC`, `ASYNC FUNC`, or `STRUCT` to
other modules; the default is `PRIVATE` (module-local) and may be
written explicitly.  `EXPORT` requires a `PUB` target — an explicit
`PRIVATE` is a hard error, while a symbol that still carries the
default is auto-promoted with a deprecation warning so pre-v1.16.0
sources keep compiling.

```ploy
@inline @hot PUB FUNC fast(a: i32, b: i32) -> i32 { RETURN a + b; }
PRIVATE STRUCT Internal { x: i32 }
PUB FUNC api() -> i32 { RETURN fast(1, 2); }
EXPORT api AS "api_external";
```

Attributes have the shape `@name` or `@name(arg, ...)`; the built-in
registry is `@inline`, `@noinline`, `@always_inline`, `@hot`,
`@cold`, `@profile`, `@no_profile`, `@deprecated`, `@link_name`,
`@target`.  Unknown attributes are accepted with a sema warning so
third-party tooling can extend the catalog without modifying the
compiler.  The v1.16.0 MVP keeps attributes as inert metadata; wiring
them into the optimiser and linker is tracked as follow-up work.

> See [Sample 39: visibility_attrs](../../tests/samples/39_visibility_attrs/)
> for the end-to-end example,
> `docs/realization/visibility_attrs.md` for the IR / ABI model, and
> `docs/specs/attribute_catalog.md` for the full attribute catalog.

## 16.9 Extended String Literals *(since v1.17.0)*

Ploy 1.17.0 ships four string-literal forms, all sharing the same
`String` value type:

```ploy
LET reg  = "hello\n";                                  // regular
LET path = r"C:\path\no\escape";                       // raw
LET sql  = r#"SELECT "name" FROM users"#;              // raw with `#` padding
LET poem = """line one
line two""";                    // multiline
LET msg  = f"answer = {42}, pi = {3.14}";              // template
```

Template strings (`f"..."`) take brace-delimited interpolation
expressions; `{{` / `}}` are literal braces.  Sema requires every
interpolated expression to be `Int`, `Float`, `String`, or `Bool`.
The v1.17.0 lowering layer folds template strings whose interpolated
parts are all compile-time `Literal` expressions; the runtime
formatting helper for variable interpolation is tracked as future work.

> See [Sample 40: string_literals](../../tests/samples/40_string_literals/)
> for the end-to-end example and
> `docs/realization/string_literals.md` for the IR / ABI model.

---

# 17. Complete Examples

## 17.1 Basic Cross-Language Linking

> Source: [Sample 01: basic_linking](../../tests/samples/01_basic_linking/)

```ploy
IMPORT cpp::math_ops;
IMPORT python::string_utils;

LINK(cpp, python, math_ops::add, string_utils::format_result) {
    MAP_TYPE(cpp::int, python::int);
}

MAP_TYPE(cpp::int, python::int);
MAP_TYPE(cpp::double, python::float);

FUNC compute_and_format(a: INT, b: INT) -> STRING {
    LET sum = CALL(cpp, math_ops::add, a, b);
    LET formatted = CALL(python, string_utils::format_result, sum);
    RETURN formatted;
}

EXPORT compute_and_format AS "polyglot_compute";
```

## 17.2 ML Training Pipeline

> Source: [Sample 05: class_instantiation](../../tests/samples/05_class_instantiation/)

```ploy
IMPORT cpp::matrix;
IMPORT python::model;

LINK(cpp, python, matrix::Matrix, model::LinearModel) {
    MAP_TYPE(cpp::double, python::float);
    MAP_TYPE(cpp::int, python::int);
}

PIPELINE training_pipeline {
    FUNC train(epochs: INT) -> FLOAT {
        LET model = NEW(python, model::LinearModel, 4, 2);
        LET loader = NEW(python, model::DataLoader, data, 1);

        VAR total_loss = 0.0;
        VAR epoch = 0;

        WHILE epoch < epochs {
            LET batch = METHOD(python, loader, next_batch);
            LET mat = NEW(cpp, matrix::Matrix, 1, 4, 0.0);
            LET loss = METHOD(python, model, train_step,
                             [1.0, 2.0, 3.0, 4.0], [1.0, 0.0]);
            total_loss = total_loss + loss;
            epoch = epoch + 1;
        }

        METHOD(python, loader, reset);
        RETURN total_loss;
    }
}

EXPORT training_pipeline AS "train_pipeline";
```

## 17.3 Five-Language Full Stack

> Source: [Sample 15: full_stack](../../tests/samples/15_full_stack/)

This example demonstrates all five supported languages working together in a single pipeline: C++ handles computation, Python handles ML, Rust handles data loading, Java handles sorting, and C# handles JSON serialisation.

## 17.4 Environment Configuration and Package Management

> Source: [Sample 16: config_and_venv](../../tests/samples/16_config_and_venv/)

```ploy
CONFIG VENV python "env/python";
CONFIG VENV dotnet "env/dotnet";

IMPORT python PACKAGE numpy >= 1.24 AS np;
IMPORT python PACKAGE pandas >= 2.0 AS pd;
IMPORT dotnet PACKAGE Newtonsoft.Json >= 13.0 AS njson;

IMPORT dotnet::Transformer;
IMPORT python::data_science;

STRUCT Experiment {
    name: STRING;
    epochs: INT;
    learning_rate: FLOAT;
    converged: BOOL;
}

FUNC json_round_trip() -> STRING {
    LET json_str = CALL(python, data_science::to_json, "ResNet-50");
    LET transformer = NEW(dotnet, Transformer);
    LET parsed = METHOD(dotnet, transformer, ParseJson, json_str);
    LET enriched = METHOD(dotnet, transformer, Enrich, parsed, "validated", "true");
    LET back = METHOD(dotnet, transformer, ToJsonString, enriched);
    RETURN back;
}

FUNC explicit_conversion() -> FLOAT {
    LET py_list = CALL(python, data_science::random_matrix, 1, 5);
    LET dotnet_arr = CONVERT(py_list, dotnet::List_double);
    LET transformer = NEW(dotnet, Transformer);
    LET flat = METHOD(dotnet, transformer, Flatten, dotnet_arr);
    LET py_flat = CONVERT(flat, python::list);
    LET avg = CALL(python, data_science::mean, py_flat);
    RETURN avg;
}

EXPORT json_round_trip;
EXPORT explicit_conversion;
```

---

# 18. Keyword Reference

All **54 keywords** in the .ploy language:

| Keyword | Category | Description |
|---------|----------|-------------|
| `LINK` | Declaration | Cross-language function linking |
| `IMPORT` | Declaration | Module or package import |
| `EXPORT` | Declaration | Symbol export |
| `MAP_TYPE` | Declaration | Cross-language type mapping |
| `PIPELINE` | Declaration | Multi-stage pipeline declaration |
| `FUNC` | Declaration | Function definition |
| `CONFIG` | Declaration | Package manager configuration |
| `LET` | Variable | Immutable variable declaration |
| `VAR` | Variable | Mutable variable declaration |
| `STRUCT` | Type | Structure type definition |
| `VOID` | Type | Void return type |
| `INT` | Type | Integer type |
| `FLOAT` | Type | Floating-point type |
| `STRING` | Type | String type |
| `BOOL` | Type | Boolean type |
| `ARRAY` | Type | Array type |
| `LIST` | Type | List container type |
| `TUPLE` | Type | Tuple container type |
| `DICT` | Type | Dictionary container type |
| `OPTION` | Type | Optional type |
| `RETURN` | Control Flow | Return value from function |
| `IF` | Control Flow | Conditional branch |
| `ELSE` | Control Flow | Alternative branch |
| `WHILE` | Control Flow | Loop |
| `FOR` | Control Flow | Iteration |
| `IN` | Control Flow | Used with FOR and MATCH |
| `MATCH` | Control Flow | Pattern matching |
| `CASE` | Control Flow | Match case |
| `DEFAULT` | Control Flow | Default match case |
| `BREAK` | Control Flow | Break out of loop |
| `CONTINUE` | Control Flow | Skip to next iteration |
| `AS` | Operator | Alias/cast operator |
| `AND` | Operator | Logical AND |
| `OR` | Operator | Logical OR |
| `NOT` | Operator | Logical NOT |
| `CALL` | Operator | Cross-language function call |
| `CONVERT` | Operator | Explicit type conversion |
| `MAP_FUNC` | Operator | Mapping function |
| `NEW` | OOP | Cross-language class instantiation |
| `METHOD` | OOP | Cross-language method call |
| `GET` | OOP | Read foreign object attribute |
| `SET` | OOP | Write foreign object attribute |
| `WITH` | OOP | Automatic resource management |
| `DELETE` | OOP | Object destruction |
| `EXTEND` | OOP | Class extension |
| `TRUE` | Value | Boolean true literal |
| `FALSE` | Value | Boolean false literal |
| `NULL` | Value | Null literal |
| `PACKAGE` | Package | Package import keyword |
| `VENV` | Package Manager | venv/virtualenv environment |
| `CONDA` | Package Manager | Conda environment |
| `UV` | Package Manager | uv-managed environment |
| `PIPENV` | Package Manager | Pipenv project |
| `POETRY` | Package Manager | Poetry project |

---

# 19. Best Practices

## 19.1 File Organisation

1. **Place CONFIG at the top** — environment configuration must appear before any `IMPORT PACKAGE` that depends on it.
2. **Group IMPORT statements** — put all imports together, module imports first, then package imports.
3. **Define LINK blocks before functions** — link declarations provide the glue code context.
4. **Put MAP_TYPE after LINK blocks** — global type mappings complement the per-link mappings.
5. **Export at the bottom** — exports should be the last section.

## 19.2 Cross-Language Calling

1. **Prefer CALL for function calls** — use `CALL(lang, module::func, args...)`.
2. **Prefer METHOD for method calls** — use `METHOD(lang, obj, method, args...)`.
3. **Declare type mappings for every boundary** — always map types explicitly.
4. **Use CONVERT for explicit marshalling** — when automatic mapping is insufficient.

## 19.3 Object Lifecycle

1. **Always DELETE when done** — explicitly destroy objects to free resources.
2. **Use WITH for scoped resources** — files, connections, locks, etc.
3. **Keep object handles in LET** — immutability prevents accidental reassignment.

## 19.4 Pipeline Design

1. **One responsibility per stage** — each FUNC in a PIPELINE should do one thing.
2. **Use meaningful names** — `preprocess`, `classify`, `postprocess` are clearer than `stage1`, `stage2`.
3. **Mix languages strategically** — use C++/Rust for performance, Python for ML/data science, Java/.NET for enterprise logic.

## 19.5 Sample Programs

Consult the sample programs for real-world patterns:

| Sample | What to Learn |
|--------|---------------|
| [01_basic_linking](../../tests/samples/01_basic_linking/) | LINK, CALL, IMPORT, EXPORT basics |
| [02_type_mapping](../../tests/samples/02_type_mapping/) | MAP_TYPE with complex types and STRUCTs |
| [03_pipeline](../../tests/samples/03_pipeline/) | PIPELINE with IF/WHILE/FOR/MATCH |
| [04_package_import](../../tests/samples/04_package_import/) | IMPORT PACKAGE with version constraints |
| [05_class_instantiation](../../tests/samples/05_class_instantiation/) | NEW and METHOD for OOP |
| [06_attribute_access](../../tests/samples/06_attribute_access/) | GET and SET |
| [07_resource_management](../../tests/samples/07_resource_management/) | WITH for scoped resources |
| [08_delete_extend](../../tests/samples/08_delete_extend/) | DELETE and EXTEND |
| [09_mixed_pipeline](../../tests/samples/09_mixed_pipeline/) | All features combined in an ML pipeline |
| [10_error_handling](../../tests/samples/10_error_handling/) | Error scenarios and diagnostics |
| [11_java_interop](../../tests/samples/11_java_interop/) | Java cross-language interop |
| [12_dotnet_interop](../../tests/samples/12_dotnet_interop/) | .NET cross-language interop |
| [13_generic_containers](../../tests/samples/13_generic_containers/) | Generic container type interop |
| [14_async_pipeline](../../tests/samples/14_async_pipeline/) | Multi-stage signal processing pipeline |
| [15_full_stack](../../tests/samples/15_full_stack/) | Five-language full-stack demo |
| [16_config_and_venv](../../tests/samples/16_config_and_venv/) | CONFIG, IMPORT PACKAGE, CONVERT |
