# Visibility and Attributes (since v1.16.0)

Module-boundary visibility (`PUB` / `PRIVATE`) and the
`@name(args)` annotation prefix are surface-syntax features layered on
top of the existing top-level declaration grammar.  Lowering and the
runtime ABI are unchanged in the v1.16.0 MVP; the new state survives
parsing and sema as metadata that downstream passes can consult once
they are wired up.

## Surface syntax

```
@<attr>(<arg>, ...)   @<attr2>   PUB | PRIVATE   FUNC | STRUCT  ...
```

* Attribute names are plain identifiers (`inline`, `hot`, ...);
  arguments are captured verbatim as the lexer's lexeme text (literal
  source spelling, including any surrounding quotes).
* At most one of `PUB` / `PRIVATE` may appear per declaration.
* The prefix is only accepted before `FUNC`, `ASYNC FUNC`, and
  `STRUCT` declarations in the v1.16.0 MVP.  All other top-level
  forms reject it with a parser diagnostic.

## Built-in attribute registry

The sema accepts the following names without warning; everything else
produces a `kGenericWarning` so unknown attributes do not break
compilation.

| Name              | Intent                                          |
|-------------------|-------------------------------------------------|
| `inline`          | Hint: prefer inlining at every call site.       |
| `noinline`        | Hint: do not inline.                            |
| `always_inline`   | Force inlining.                                 |
| `hot`             | Optimise for hot path.                          |
| `cold`            | Optimise for cold path.                         |
| `profile`         | Always emit profile-collection trampolines.    |
| `no_profile`      | Suppress profile collection.                   |
| `deprecated`      | Mark the symbol deprecated.                     |
| `link_name`       | Override the mangled export name.               |
| `target`          | Limit the symbol to the listed architectures.   |

## Sema integration

* `FuncDecl` and `StructDecl` carry `Visibility visibility`,
  `bool visibility_explicit`, and `std::vector<Attribute> attributes`.
* `PloySymbol` carries the same `visibility` / `visibility_explicit`
  pair so `EXPORT` analysis can consult it.
* `PloySema::ValidateAttributes` walks the attribute list and emits a
  warning for every name outside the built-in registry.
* `PloySema::AnalyzeExportDecl` requires the target symbol to carry
  `Visibility::kPub`.  An *explicit* `PRIVATE` is a hard error; a
  symbol that still carries the default `kPrivate` (no PUB / PRIVATE
  keyword in the source) is auto-promoted to `kPub` with a deprecation
  warning so pre-v1.16.0 sources keep compiling.

## Lowering

The MVP lowers attributes and visibility as inert metadata on the
parsed AST.  The IR builder, optimiser, and runtime are not modified
in v1.16.0.  Wiring `@inline` / `@hot` / `@profile` into the optimiser
and `@link_name` / `@target` into the linker is tracked as follow-up
work.

## Cross-language host mapping

`PUB` controls module-boundary visibility within a polyglot binary;
host bindings continue to receive only the symbols listed by `EXPORT`,
which now requires `PUB`.  Attributes are not yet propagated to host
language bindings (e.g. `[[gnu::always_inline]]` for C++ or
`#[inline(always)]` for Rust); doing so cleanly is part of the
follow-up work above.

## Future work

* Propagate `@inline` / `@noinline` / `@always_inline` / `@hot` /
  `@cold` into the optimiser and back-end mangling.
* Honour `@link_name` in the linker so the IR symbol name is renamed
  to the requested external name without an aliasing global.
* Honour `@target("x86_64,arm64")` by skipping code generation on
  unsupported architectures.
* Visibility / attribute prefix on `CLASS`, `CONST`, `TYPE`, `MAP_FUNC`,
  and module-scope `LET`.
* Nested `MODULE name { ... }` blocks for finer-grained visibility
  scopes.
* Promote the `EXPORT`-without-`PUB` deprecation warning to a hard
  error in a future major release.
