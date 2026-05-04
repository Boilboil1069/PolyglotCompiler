# 33 — Pattern Matching

Demonstrates the extended `MATCH` pattern semantics: literals,
wildcards, ranges, tuples, structs, OR-patterns, bindings, type
guards and `OPTION` constructor patterns, all type-checked for
exhaustiveness and reachability.

## What you will see

* `classify_int` — literal, OR (`2 | 4 | 8 | 16`), half-open and
  inclusive range (`10..20` / `20..=29`), binding (`n @ 100..=199`)
  and a type-guard arm (`n: i32 IF n > 1000`) terminated by a
  catch-all `CASE _`.
* `classify_pair` — tuple destructuring with the "ignore one element"
  shorthand `(_, b)`.  Without a `DEFAULT` arm the final irrefutable
  `(a, b)` is what makes the MATCH exhaustive.
* `classify_point` — struct destructuring with field-by-name binding
  and the `..` rest specifier.
* `classify_option` — OPTION variant matching with `Some(x)` and the
  bare `None` constructor; both arms together are exhaustive, so no
  `_` or `DEFAULT` is needed.

## Run it

```powershell
polyc 33_pattern_matching/pattern_matching.ploy --emit-obj=build/sample.obj --quiet
polyld build/sample.obj -o build/sample.exe
.\build\sample.exe
```

Expected stdout: `33_pattern_matching: ok` (followed by `\r\n`).

## Why it matters

* **No silent fall-through.** Each arm executes its body and exits
  the `MATCH` — the historical C-style fall-through hazard does not
  exist in `.ploy`.
* **Compile-time exhaustiveness.** Boolean and `OPTION` MATCHes are
  required to cover every variant; for any other type the front-end
  insists on either `CASE _` or a `DEFAULT` arm.
* **Compile-time reachability.** Arms after an irrefutable wildcard
  and duplicate literal arms produce warnings, so dead code never
  reaches the lowering pass.
* **Single switch table for hot paths.** When every arm is a simple
  integer literal (or `CASE _`) the lowering pass collapses the
  cascade into one `SwitchStatement`, letting the backend emit a
  dense jump table.
