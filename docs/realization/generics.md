# Generics — Bounded Type Parameters and Type-Erased Lowering (since v1.15.0)

## Surface syntax

```ploy
FUNC max<T: Comparable>(a: T, b: T) -> T {
    IF (a > b) { RETURN a; } ELSE { RETURN b; }
}

STRUCT Pair<A, B> { first: A, second: B }

FUNC sum<T>(a: T, b: T) -> T WHERE T: Numeric {
    RETURN a + b;
}
```

* **Type parameter list** — `<T: Bound1 + Bound2, U>` follows the
  function or struct name.  Bounds are plain identifier names; sema
  validates each against the built-in trait registry.
* **WHERE clause** — `WHERE T: Bound1 + Bound2, U: Bound3` between
  the return type and the body augments the matching parameter's
  bounds vector.  References to undeclared parameters are rejected at
  parse time.
* **Generic instantiation** — In a type position, `Pair<i32, String>`
  is parsed as a `ParameterizedType` carrying type arguments.  Sema
  resolves every argument and collapses to the underlying struct
  identity.

## Built-in trait registry

| Bound        | Host-language contract                                                  |
| ------------ | ----------------------------------------------------------------------- |
| `Comparable` | C++ `std::totally_ordered`, Rust `Ord`, Java `Comparable`, .NET `IComparable`, Python `__lt__` / `__eq__` |
| `Hashable`   | C++ `std::hash`, Rust `Hash`, Java `hashCode`, .NET `GetHashCode`, Python `__hash__` |
| `Numeric`    | C++ `std::is_arithmetic`, Rust `Num`, Java `Number`, .NET `INumber`, Python `numbers.Number` |
| `Iterable`   | C++ ranges, Rust `IntoIterator`, Java `Iterable`, .NET `IEnumerable`, Python `__iter__` |
| `Display`    | C++ `operator<<`, Rust `Display`, Java `toString`, .NET `ToString`, Python `__str__` |

The set lives in `frontends/ploy/src/sema/sema.cpp`
(`PloySema::ValidateTypeParamBounds`).  Unknown bound names are
reported as `kTypeMismatch` diagnostics.

## Lowering shape (MVP)

The v1.15.0 implementation lowers each generic declaration once with
every type parameter resolved to `core::Type::Any()`.  Inside the body,
references to `T` therefore behave as opaque pointer-like values, and
the IR builder emits a single function (or struct layout) that
services every call site:

```text
function max(any %a, any %b) -> any { ... }
```

This keeps the diagnostic surface stable while full
per-instantiation monomorphisation is built out.

## Sema integration

* `FuncDecl::type_params` and `StructDecl::type_params` carry a
  vector of `TypeParam { name, bounds }`.  WHERE clauses are merged
  into the matching parameter's `bounds` during parsing.
* `PloySema::active_type_params_` is a name-set that
  `ResolveType(SimpleType)` consults first; any matching name
  resolves to `Any`.  The set is populated for the duration of
  `AnalyzeFuncDecl` and `AnalyzeStructDecl` and popped on every exit
  path.
* `PloySema::ValidateTypeParamBounds` checks every declared bound
  against the built-in registry.

## Cross-language host mapping

The MVP path treats every generic instantiation as one cross-language
stub.  For instance, calling `max(10, 20)` from Python lowers to a
single `max` symbol whose argument types degrade to `Any`; the host
adapter marshals through the same boxed-value path used by other
`Any`-returning functions.  Once monomorphisation lands, each
concrete instantiation will publish its own stub with the per-host
type contract spelled out in the registry table above.

## Future work

* **Monomorphisation.** Generate a distinct IR function per concrete
  type-argument tuple inferred from call-site argument types; emit
  one cross-language stub per instantiation.
* **Bound enforcement against concrete types.** Reject `Comparable`
  for non-ordered types; reject `Numeric` for non-arithmetic types;
  surface the rejection at the call site rather than the definition.
* **Parametric generics in `LINK` declarations.** Allow
  `LINK rust::vec::Vec<T> AS ...` once monomorphisation is in place.
* **Generic methods on classes.** Extend `CLASS` blocks
  (demand 2026-04-28-9) to declare `METHOD push<T>(item: T)` rows.
* **Higher-kinded bounds.** Extend the registry with user-defined
  traits and `WHERE` constraints over multiple parameters.
