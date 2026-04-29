# 32 — Typed Cross-Language Handles

Demonstrates **demand 2026-04-28-9**: cross-language objects enter the
static type system as `HANDLE<lang::Class>`, so `NEW`, `METHOD`, `GET`
and `SET` are checked at compile time instead of being routed blindly
to the foreign runtime.

## What you will see

* `CLASS python::torch::nn::Linear { METHOD ...; ATTR ...; }` — explicit
  method and attribute signatures for a Python class.
* `CLASS cpp::matrix::Matrix { ... }` — same idea for a C++ class.
* `LET model: HANDLE<python::torch::nn::Linear> = NEW(python, ...)` —
  the constructor returns a typed handle, not an opaque `Any`.
* `METHOD(python, model, forward, 1.0)` — argument count and types are
  validated against the registered schema.
* `GET(python, model, in_features)` — attribute type comes from the
  matching `ATTR` row.

## Run it

```powershell
polyc 32_typed_handles/typed_handles.ploy --emit-obj=build/sample.obj --quiet
polyld build/sample.obj -o build/sample.exe
.\build\sample.exe
```

Expected stdout: `32_typed_handles: ok` (followed by `\r\n`).

## Why it matters

* **Compile-time safety.** A wrong-arity `METHOD(...)` or a misspelled
  `ATTR` is a sema diagnostic instead of a runtime crash inside the
  foreign interpreter.
* **No silent cross-language casting.** `HANDLE<python::A>` and
  `HANDLE<cpp::A>` are statically distinct, even when the class names
  match.  Conversion has to go through `CONVERT` + `MAP_FUNC`.
* **Backward compatible.** `NEW(...)` without a matching `CLASS` block
  still parses and falls back to dynamic dispatch (warning only); old
  samples keep building unchanged.
