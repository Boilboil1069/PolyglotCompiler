# 38_generics — Generic FUNC / STRUCT (since v1.15.0)

This sample demonstrates the surface syntax introduced by
[demand 2026-04-28-15](../../docs/demand/demand.md):

```ploy
STRUCT Pair<A, B> { first: A, second: B }
FUNC max<T: Comparable>(a: T, b: T) -> T { ... }
FUNC sum<T>(a: T, b: T) -> T WHERE T: Numeric { ... }
```

## Surface syntax

* **Type parameter list.** `<T: Bound1 + Bound2, U>` follows the
  function or struct name.  Bounds are plain identifier names; sema
  validates each against the built-in trait registry below.  An empty
  list (no `<...>`) marks a non-generic declaration.
* **WHERE clause.** `WHERE T: Bound1 + Bound2, U: Bound3` between the
  return type and the function body augments the bounds of the
  matching parameters.  References to undeclared parameters are
  rejected at parse time.
* **Generic instantiation in a type position.** `Pair<i32, String>`
  inside a field type or variable annotation is parsed as a
  parameterised type.  Sema validates type-argument resolution.

## Built-in trait registry

| Bound        | Host-language contract                                                  |
| ------------ | ----------------------------------------------------------------------- |
| `Comparable` | C++ `std::totally_ordered`, Rust `Ord`, Java `Comparable`, .NET `IComparable`, Python `__lt__`/`__eq__` |
| `Hashable`   | C++ `std::hash`, Rust `Hash`, Java `hashCode`, .NET `GetHashCode`, Python `__hash__` |
| `Numeric`    | C++ `std::is_arithmetic`, Rust `Num`, Java `Number`, .NET `INumber`, Python `numbers.Number` |
| `Iterable`   | C++ ranges, Rust `IntoIterator`, Java `Iterable`, .NET `IEnumerable`, Python `__iter__` |
| `Display`    | C++ `operator<<`, Rust `Display`, Java `toString`, .NET `ToString`, Python `__str__` |

Unknown bound names trigger a sema `kTypeMismatch` diagnostic.

## Lowering model (MVP)

The v1.15.0 implementation lowers generic functions and structs once
with each type parameter resolved to `Any` (type erasure).  The IR
treats `T` as an opaque value, so a single body services every
call site.  This keeps the diagnostic surface stable while full
per-instantiation monomorphisation is built out (see *future work*).

## Build and run

```bash
polyc 38_generics/generics.ploy --emit-obj=build/sample.obj --quiet
polyld build/sample.obj -o build/sample.exe
./build/sample.exe
# 38_generics: ok
```

## Future work

* **Monomorphisation.** Generate a distinct IR function per concrete
  type-argument tuple inferred from call-site argument types; emit
  one cross-language stub per instantiation.
* **Bound enforcement.** Lower the built-in trait checks against
  concrete types (e.g. reject `Comparable` for non-ordered structs).
* **Parametric generics in `LINK` declarations.** Allow
  `LINK rust::vec::Vec<T> AS ...` once monomorphisation is in place.
* **Higher-kinded bounds.** Extend the registry with user-defined
  traits and `WHERE` constraints over multiple parameters.
