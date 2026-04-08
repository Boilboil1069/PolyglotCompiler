# Cross-Language Class Instantiation — Implementation Details

## 1. Overview

This document describes the `.ploy` language support for cross-language class instantiation and
method invocation. Through the `NEW` and `METHOD` keywords, `.ploy` programs can create instances
of classes from foreign languages and call their methods, enabling object-oriented cross-language
interoperability.

### 1.1 Motivation

The existing `.ploy` language supports cross-language function calls via the `CALL` keyword,
but many language ecosystems are built around classes and objects:

- **Python PyTorch**: `model = torch.nn.Linear(784, 10)` → `model.forward(data)`
- **Python scikit-learn**: `scaler = StandardScaler()` → `scaler.fit_transform(data)`
- **Rust tokio**: `let runtime = tokio::Runtime::new()` → `runtime.spawn(task)`

Without class instantiation support, developers cannot use these object-oriented APIs from `.ploy`.

### 1.2 Design Goals

| Goal | Description |
|------|-------------|
| **Cross-language constructors** | `NEW(language, class, args...)` — instantiate a class in the target language |
| **Cross-language method calls** | `METHOD(language, object, method, args...)` — invoke a method on an object |
| **Consistent syntax** | Follows the same parenthesized calling convention as `CALL()` |
| **Opaque object handles** | Objects are passed as opaque handles; type safety is ensured by the target language |
| **Member access compatibility** | Complements the existing `.` syntax (`MemberExpression`) |

## 2. Language Extensions

### 2.1 New Keywords

| Keyword  | Purpose                          | Description |
|----------|----------------------------------|-------------|
| `NEW`    | Cross-language class instantiation | Calls the constructor of a target language class, returns an object handle |
| `METHOD` | Cross-language method invocation   | Invokes a method on a target language object |
| `GET`    | Cross-language property access     | Reads a property/field from a target language object |
| `SET`    | Cross-language property mutation   | Writes a property/field on a target language object |
| `WITH`   | Cross-language resource management | Context-managed scope with automatic cleanup |
| `DELETE` | Cross-language object destruction  | Explicitly destroys/releases a target language object |
| `EXTEND` | Cross-language class extension     | Extends a target language class with new methods |

With these additions, the `.ploy` language has **54 reserved keywords** in total.

### 2.2 Syntax Definition

#### NEW — Constructor Call

```
NEW(language, class_name [, arg1, arg2, ...])
```

| Parameter | Description | Examples |
|-----------|-------------|----------|
| `language` | Target language identifier | `python`, `rust`, `cpp`, `java`, `dotnet` |
| `class_name` | Class name (may use `::` qualification) | `MyClass`, `torch::nn::Linear` |
| `arg1, arg2, ...` | Constructor arguments (optional) | `784, 10` |

**Examples:**

```ploy
// Instantiate a Python class (no arguments)
LET model = NEW(python, sklearn::LinearRegression);

// Instantiate a Python class (with arguments)
LET layer = NEW(python, torch::nn::Linear, 784, 10);

// Instantiate a Rust struct
LET runtime = NEW(rust, tokio::Runtime);

// With string arguments
LET conn = NEW(python, sqlite3::Connection, "database.db");
```

#### METHOD — Method Invocation

```
METHOD(language, object, method_name [, arg1, arg2, ...])
```

| Parameter | Description | Examples |
|-----------|-------------|----------|
| `language` | Target language identifier | `python`, `rust`, `cpp`, `java`, `dotnet` |
| `object` | Object expression (typically a variable) | `model`, `scaler` |
| `method_name` | Method name (may use `::` qualification) | `forward`, `fit_transform` |
| `arg1, arg2, ...` | Method arguments (optional) | `data, 42` |

**Examples:**

```ploy
// Call a method (with arguments)
LET output = METHOD(python, model, forward, input_data);

// Call a method (no arguments)
LET vocab = METHOD(python, tokenizer, get_vocab);

// Call a qualified method
LET result = METHOD(python, obj, utils::serialize, data);
```

### 2.3 Comparison with Existing Features

| Feature  | Syntax | Purpose | Return Value |
|----------|--------|---------|--------------|
| `CALL`   | `CALL(lang, func, args...)` | Call a cross-language function | Function return value |
| `NEW`    | `NEW(lang, class, args...)` | Instantiate a cross-language class | Object handle |
| `METHOD` | `METHOD(lang, obj, method, args...)` | Call an object method | Method return value |

## 3. Implementation Details

### 3.1 AST Nodes

Two new expression nodes are added:

```
NewExpression
├── language: string          // Target language identifier
├── class_name: string        // Class name (may contain :: qualifiers)
└── args: Expression[]        // Constructor arguments

MethodCallExpression
├── language: string          // Target language identifier
├── object: Expression        // Receiver object
├── method_name: string       // Method name (may contain :: qualifiers)
└── args: Expression[]        // Method arguments
```

### 3.2 Parser Handling

`NEW` and `METHOD` are recognized in `ParsePrimary()`, after `CALL`:

```
ParsePrimary:
    CALL → ParseCallDirective()
    NEW → ParseNewExpression()
    METHOD → ParseMethodCallDirective()
    CONVERT → ParseConvertExpression()
    ...
```

Parsing process:
1. Consume the keyword (`NEW` or `METHOD`)
2. Expect `(`
3. Parse language name
4. Expect `,`
5. For `NEW`: parse class name (with `::` qualification); for `METHOD`: parse object expression + `,` + method name
6. Optionally parse remaining arguments (`,`-separated)
7. Expect `)`

### 3.3 Semantic Analysis

- **Language validation**: Checks that `language` is a valid language identifier (`cpp`, `python`, `rust`, `c`, `ploy`)
- **Name validation**: Checks that `class_name` (`NEW`) or `method_name` (`METHOD`) is non-empty
- **Argument analysis**: Recursively analyzes each argument expression
- **Object validation** (`METHOD`): Analyzes the receiver object expression to ensure it is defined
- **Return type**: Both `NEW` and `METHOD` return `Any` type (foreign types cannot be statically resolved)

### 3.4 IR Lowering

#### NEW IR Generation

Constructor calls are compiled into bridge stub function calls:

```
Source: LET model = NEW(python, torch::nn::Linear, 784, 10);
IR:     %0 = call __ploy_bridge_ploy_python_torch__nn__Linear____init__(784, 10) -> ptr
```

- Stub name format: `__ploy_bridge_ploy_<language>_<class>____init__`
- Return type: `ptr` (opaque object pointer)
- Generates a `CrossLangCallDescriptor` with `source_function` set to `<class>::__init__`

#### METHOD IR Generation

Method calls are compiled into bridge stub function calls with the receiver object as the first argument:

```
Source: LET output = METHOD(python, model, forward, data);
IR:     %1 = call __ploy_bridge_ploy_python_forward(%model, %data) -> i64
```

- The receiver object is automatically inserted as the first argument (similar to Python's `self`)
- Generates a `CrossLangCallDescriptor` with `source_param_types` containing object type + argument types

## 4. Complete Examples

### 4.1 Machine Learning Inference Pipeline

```ploy
// Import Python ML packages
IMPORT python PACKAGE torch;
IMPORT python PACKAGE sklearn;
IMPORT cpp::inference_engine;

// Cross-language type mapping
LINK(cpp, python, run_inference, torch::forward) {
    MAP_TYPE(cpp::float_ptr, python::Tensor);
}

FUNC ml_pipeline() -> INT {
    // Instantiate Python classes
    LET scaler = NEW(python, sklearn::StandardScaler);
    LET model = NEW(python, torch::nn::Sequential);

    // Prepare data
    LET data = [1.0, 2.0, 3.0];

    // Call methods for preprocessing
    LET scaled = METHOD(python, scaler, fit_transform, data);

    // Call methods for inference
    LET prediction = METHOD(python, model, forward, scaled);

    // Pass results to C++ postprocessing
    LET result = CALL(cpp, inference_engine::postprocess, prediction);

    RETURN 0;
}
```

### 4.2 Database Operations

```ploy
IMPORT python PACKAGE sqlite3;

FUNC query_database() -> INT {
    // Create database connection
    LET conn = NEW(python, sqlite3::Connection, "data.db");

    // Execute query
    LET cursor = METHOD(python, conn, execute, "SELECT * FROM users");

    // Fetch results
    LET rows = METHOD(python, cursor, fetchall);

    // Close connection
    METHOD(python, conn, close);

    RETURN 0;
}
```

### 4.3 Usage in PIPELINE

```ploy
IMPORT python PACKAGE torch;

PIPELINE training_pipeline {
    LET model = NEW(python, torch::nn::Linear, 128, 10);
    LET optimizer = NEW(python, torch::optim::SGD, 0.01);
    LET loss = METHOD(python, model, forward, 42);
    RETURN 0;
}
```

## 5. Position in the Compiler Pipeline

```
┌────────┐   ┌────────┐   ┌────────┐   ┌─────────┐   ┌──────────┐
│ Lexer  │ → │ Parser │ → │  Sema  │ → │Lowering │ → │   IR     │
│  (54   │   │ NEW    │   │Language │   │ctor stub│   │ bridge   │
│  kws)  │   │ METHOD │   │validate│   │meth stub│   │  calls   │
└────────┘   └────────┘   └────────┘   └─────────┘   └──────────┘
```

## 6. Limitations

| Limitation | Description | Status |
|-----------|-------------|--------|
| **Static type unknown** | `NEW` and `METHOD` return `Any` type; full type checking is not possible at compile time without class schemas | Partially resolved via `ForeignClassSchema` |
| **No destructors** | Automatic destructor / `__del__` invocation is not supported; use `METHOD(lang, obj, close)` manually | Still applies |
| **No inheritance** | `.ploy` does not support defining subclasses in the target language | Still applies |
| **No property assignment** | Previously only method calls were supported; direct `obj.attr = value` was not available. **Now resolved:** `GET` and `SET` resolve types from class schemas registered via `RegisterClassSchema()` | **Resolved** |

## 7. Implementation Details (Updated)

### 7.1 Class Schema Resolution

The semantic analyzer maintains a `class_schemas_` map (`std::map<std::string, ForeignClassSchema>`) that registers known foreign class layouts. When processing `NEW`, `METHOD`, `GET`, or `SET`:

1. The analyzer looks up the class name in `class_schemas_`
2. If found, it validates:
   - Constructor parameter count and types for `NEW`
   - Method signature (param count, return type) for `METHOD`
   - Field existence and type for `GET`/`SET`
3. If not found and strict mode is enabled, an error is emitted; otherwise a warning is emitted and the expression defaults to `Any`

This moves type checking for cross-language OOP from IR lowering time to semantic analysis time.
