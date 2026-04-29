# Statically-Typed Cross-Language Object Interop

> **Demand**: 2026-04-28-9 — *cross-language objects enter the static type
> system as `HANDLE<lang::Class>`*.
> **Version introduced**: 1.9.0.

## 1. Motivation

Before this work, every cross-language object expression in `.ploy`
collapsed to the opaque `Any` type:

| Expression                                          | Pre-1.9.0 type | New type                            |
| --------------------------------------------------- | -------------- | ----------------------------------- |
| `NEW(python, torch::nn::Linear, 10, 5)`             | `Any`          | `HANDLE<python::torch::nn::Linear>` |
| `METHOD(python, model, forward, x)`                 | `Any`          | declared return type                |
| `GET(python, model, in_features)`                   | `Any`          | declared `ATTR` type                |
| `SET(python, model, in_features, 5)`                | `Any`          | type-checked against `ATTR`         |

The cost of `Any` was *every* cross-language call site became a runtime
roulette: argument count, argument types and return types were only
discovered when the foreign interpreter raised — usually deep inside a
`__pyx_call` frame whose stack trace contained none of the user's
`.ploy` source positions.

`HANDLE<lang::Class>` lifts that contract back into the static type
system.  Once a class has a registered schema, the sema:

* **rejects arity mismatches** in `NEW` constructors and `METHOD` calls;
* **rejects type mismatches** for argument and `SET` value positions;
* **resolves `METHOD` and `GET` return types** so the rest of the
  pipeline (lowering, devirtualisation, ABI marshalling) sees concrete
  types rather than `Any`;
* **forbids silent cross-language casting** — `HANDLE<a::T>` and
  `HANDLE<b::U>` are statically distinct even when their class names
  coincide.

## 2. Surface syntax

### 2.1 `CLASS` schema declaration

```ploy
CLASS python::torch::nn::Linear {
    METHOD __init__(in_features: i32, out_features: i32);
    METHOD forward(x: f32) -> f32;
    ATTR in_features: i32;
    ATTR out_features: i32;
}
```

Grammar (informal):

```
class_decl   ::= "CLASS" lang "::" class_path "{" class_row* "}"
class_row    ::= "METHOD" name "(" param_list? ")" ("->" type)? ";"
              |  "ATTR"   name ":" type ";"
param_list   ::= param ("," param)*
param        ::= (name ":")? type
class_path   ::= ident ("::" ident)*
```

* `CLASS`, `HANDLE` and `ATTR` are **contextual keywords**: ordinary
  identifiers with the same spelling continue to lex as identifiers, so
  existing samples that use `class`, `handle` or `attr` as variable
  names keep building.  `METHOD` was already a canonical keyword (it
  predates this demand as the cross-language method-call form).
* Methods named `__init__`, `new` or `ctor` are recorded in
  `ForeignClassSchema::constructor_sig` and consulted by `NEW`.
* Schemas are global per-module: a redeclaration of the same
  `lang::path` is a hard error.

### 2.2 `HANDLE<lang::class_path>` type expression

```ploy
LET model: HANDLE<python::torch::nn::Linear> = NEW(python, torch::nn::Linear, 128, 10);
```

* `HANDLE<...>` is recognised in any type position parsed by
  `ParseQualifiedOrSimpleType`.
* The angle-bracket form is restricted to the `HANDLE` contextual
  keyword to avoid colliding with the existing `<` / `>` comparison
  operators in expression positions.
* Sema lowers `HandleType { language, class_path }` to
  `core::Type::Class(class_path, language)`.  Two such types are equal
  only when both fields match.

### 2.3 Backward compatibility

A `NEW(...)` whose target has **no** registered schema still parses and
returns `core::Type::Unknown()` — the existing dynamic-dispatch path.
Likewise, `METHOD` / `GET` / `SET` on a non-typed receiver follow the
pre-1.9.0 LINK-derived signature lookup.  Existing samples therefore
build unchanged.

## 3. Sema implementation

| Component                                             | Location (file:line, approx.)                 |
| ----------------------------------------------------- | ---------------------------------------------- |
| `core::Type::Class(name, lang)` factory               | `common/include/core/types.h:151`              |
| `HandleType` / `ClassDecl` / `ClassMethodSig` AST     | `frontends/ploy/include/ploy_ast.h:74` & `218` |
| `ParseClassDecl` + HANDLE-grammar                     | `frontends/ploy/src/parser/parser.cpp`         |
| `AnalyzeClassDecl` + schema registration helpers      | `frontends/ploy/src/sema/sema.cpp`             |
| `ResolveType(HandleType)` branch                      | same file, `ResolveType`                       |
| Schema-aware `AnalyzeNewExpression`                   | same file, line ~1149                          |
| Schema-aware `AnalyzeMethodCallExpression`            | same file, line ~1225                          |
| Schema-aware `AnalyzeGetAttrExpression`               | same file, line ~1322                          |
| Schema-aware `AnalyzeSetAttrExpression`               | same file, line ~1398                          |
| Cross-language compatibility rule in `AreTypesCompatible` | same file, line ~1597                      |

Each schema-aware path runs **before** the legacy `LookupSignature()`
fallback, so explicit `CLASS` blocks always win over LINK-derived
signatures when both are present.

## 4. Diagnostics policy

| Site                                       | Severity | Rationale                                                                                                                                          |
| ------------------------------------------ | -------- | -------------------------------------------------------------------------------------------------------------------------------------------------- |
| Argument count mismatch in `NEW` / `METHOD` | error    | Same as plain function-call sema.                                                                                                                  |
| Argument type mismatch                     | error    | Same as plain function-call sema.                                                                                                                  |
| `SET` value type mismatch                  | error    | An attribute write that violates the declared type would corrupt the foreign object's invariant.                                                   |
| Unknown `METHOD` on a typed handle         | warning  | Foreign objects routinely expose dynamically-added methods (Python `__getattr__`, C# `dynamic`).  We warn so the developer knows static dispatch is degraded, but compilation continues. |
| Unknown `ATTR` on a typed handle           | warning  | Same reason as above.                                                                                                                              |
| Cross-language handle assignment           | error    | A `HANDLE<a::T>` is a foreign-runtime pointer; reinterpreting it as `HANDLE<b::U>` would invariably crash the foreign runtime.                     |

Explicit conversion is always available via `CONVERT` + `MAP_FUNC`:

```ploy
LET py_obj: HANDLE<python::A> = ...;
LET cpp_obj: HANDLE<cpp::A> = CONVERT(py_obj, MAP_FUNC bridge::py_to_cpp);
```

## 5. Tests & samples

* **Unit tests**: `tests/unit/frontends/ploy/typed_handle_test.cpp`
  exercises schema registration, `NEW`/`METHOD`/`GET`/`SET` resolution,
  arity / type errors, the unknown-method-warning path, the
  cross-language-mixing error path, the backward-compat fallback, and
  the lower-case-`handle` identifier preservation.
* **Sample**: `tests/samples/32_typed_handles/` — runnable demo with
  bilingual READMEs, expected stdout, and a deterministic PRINTLN
  marker for the regression harness.

## 6. Future work (deferred)

* `HANDLE<self>` / generic class schemas.
* Method overloading by parameter type.
* Auto-generated `CLASS` blocks from `IMPORT` introspection (Python
  `inspect.signature`, C++ `[[clang::reflect]]`).
