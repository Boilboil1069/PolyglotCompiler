# Attribute Catalog (since v1.16.0)

This catalog enumerates every annotation recognised by the Ploy
front-end's built-in registry.  Annotations not listed here are
accepted with a sema warning so third-party tooling can extend the
set without modifying the compiler.

## Calling convention

```
@<name>                       # no arguments
@<name>(<arg>, <arg>, ...)    # comma-separated, captured verbatim
```

Multiple annotations stack and apply left-to-right; ordering relative
to the visibility keyword (`PUB` / `PRIVATE`) does not matter.

## Catalog

### Inlining

| Name              | Args | Effect                                          |
|-------------------|------|-------------------------------------------------|
| `@inline`         | —    | Hint: prefer inlining at every call site.       |
| `@noinline`       | —    | Hint: do not inline.                            |
| `@always_inline`  | —    | Force inlining; sema accepts, optimiser to wire. |

### Code-placement / profiling

| Name              | Args | Effect                                          |
|-------------------|------|-------------------------------------------------|
| `@hot`            | —    | Optimise for hot path (speed bias).             |
| `@cold`           | —    | Optimise for cold path (size bias).             |
| `@profile`        | —    | Always emit profile-collection trampolines.    |
| `@no_profile`     | —    | Suppress profile collection.                    |

### Documentation / linkage

| Name              | Args                              | Effect                                                |
|-------------------|-----------------------------------|-------------------------------------------------------|
| `@deprecated`     | `("msg")`                         | Mark the symbol deprecated; sema warns at use sites.  |
| `@link_name`      | `("symbol")`                      | Override the mangled export name.                     |
| `@target`         | `("x86_64,arm64,...")`            | Limit the symbol to the listed architectures.         |

## Where annotations may appear

The v1.16.0 MVP accepts the prefix only in front of `FUNC`,
`ASYNC FUNC`, and `STRUCT` declarations.  Other top-level forms
(`CLASS`, `CONST`, `TYPE`, `MAP_FUNC`, `EXTEND`, ...) reject the
prefix with a parser diagnostic.  Extending the prefix to those forms
is tracked as follow-up work in
`docs/realization/visibility_attrs.md`.

## Cross-language mapping (planned)

The MVP does not yet propagate annotations into host-language
bindings.  The intended mapping when wiring is added is:

| Ploy attribute     | C++                          | Rust                       | Java / .NET / Python   |
|--------------------|------------------------------|----------------------------|------------------------|
| `@inline`          | `inline`                     | `#[inline]`                | (host-default hint)    |
| `@always_inline`   | `[[gnu::always_inline]]`     | `#[inline(always)]`        | (host-default hint)    |
| `@noinline`        | `[[gnu::noinline]]`          | `#[inline(never)]`         | (host-default hint)    |
| `@deprecated("m")` | `[[deprecated("m")]]`        | `#[deprecated = "m"]`      | host-native equivalent |
| `@link_name("s")`  | linker `--defsym` / `extern` | `#[export_name = "s"]`     | host-native equivalent |
