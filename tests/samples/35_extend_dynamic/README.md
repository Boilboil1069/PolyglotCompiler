# 35_extend_dynamic — `EXTEND` is restricted to dynamic host languages

`extend_dynamic.ploy` shows the **accepted** form of `EXTEND` after
demand 2026-04-28-11: only `python`, `ruby`, and `javascript`
(plus their tag aliases `rb`, `js`, `ts`) may appear as the language
argument.  The semantics is "host-language monkey-patch": the
override is installed in the foreign runtime's method dispatch table
when the program loads.  The foreign object **does not** enter the
Ploy static type system.

```ploy
EXTEND(python, torch::nn::Module) AS LinearReLU {
    FUNC forward(x: f64) -> f64 { RETURN x; }
}
```

## What is rejected

Sema rejects `EXTEND` on every statically-typed language with the
diagnostic:

```
EXTEND is not allowed on statically-typed language 'rust'
  — its type system cannot accept an out-of-source subclass without
    breaking soundness
suggestion: wrap the foreign API in a local Ploy FUNC and use
            CALL / METHOD instead, or move the EXTEND target to a
            dynamic host (python / ruby / javascript)
```

The covered rejection set is `cpp`, `c`, `rust`, `java`, `dotnet`,
`csharp`, `go`, `golang`.

## Migration path

For a static-language extension point, replace the `EXTEND` block
with a local Ploy FUNC plus `CALL` / `METHOD`:

```ploy
// Before (rejected):
//   EXTEND(rust, tokio::Task) AS MyTask { FUNC run(id: i32) -> i32 { ... } }

// After:
FUNC my_task_run(id: i32) -> i32 {
    RETURN CALL(rust, tokio_task_run, id);
}
```

The Chinese mirror lives at [`README_zh.md`](README_zh.md).
