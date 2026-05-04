# Sample 39 — Visibility (PUB / PRIVATE) and Attributes (@name)

Demonstrates the v1.16.0 surface syntax for module-boundary visibility
and built-in declaration attributes.

## Surface syntax

```
@<attr1> @<attr2>(<arg>, ...)  PUB | PRIVATE  FUNC | STRUCT  ...
```

* `PUB` — exported across module boundaries; eligible target of `EXPORT`.
* `PRIVATE` — module-local (default); explicit `PRIVATE` blocks `EXPORT`.
* Default visibility (no keyword) is private but treated leniently by
  `EXPORT` for backward compatibility — sema auto-promotes to `PUB` and
  emits a deprecation warning.
* Annotations (`@inline`, `@hot`, ...) precede the visibility keyword.

## Built-in attribute catalog

| Attribute            | Intent                                        |
|----------------------|-----------------------------------------------|
| `@inline`            | Hint: prefer inlining at every call site.     |
| `@noinline`          | Hint: do not inline.                          |
| `@always_inline`     | Force inlining.                               |
| `@hot`               | Optimise for hot path (size/speed bias).      |
| `@cold`              | Optimise for cold path (size bias).           |
| `@profile`           | Always emit profile-collection trampolines.   |
| `@no_profile`        | Suppress profile collection.                  |
| `@deprecated("msg")` | Mark the symbol deprecated; sema warns.       |
| `@link_name("sym")`  | Override the mangled export name.             |
| `@target("a,b,...")` | Limit the symbol to the listed architectures. |

Unknown annotations are accepted with a sema warning so third-party
tooling can extend the catalog without modifying the compiler.

## Build

```bash
./build/polyc tests/samples/39_visibility_attrs/visibility_attrs.ploy \
    -o /tmp/sample39.o
```

## Future work

* Wire each attribute into the optimiser / profiler pipelines (currently
  the attributes survive parsing and sema but do not yet drive
  lowering decisions).
* Visibility / attribute prefix on `CLASS`, `CONST`, `TYPE`, and `LET` at
  module scope.
* Nested `MODULE name { ... }` blocks for finer-grained visibility scopes.
