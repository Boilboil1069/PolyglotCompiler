# Grammar Polish Bundle (since v1.18.0)

This document captures the v1.18.0 P3 polish bundle: a series of minor
grammar refinements driven by feedback from users porting code from
Rust / Python / TypeScript.

The Chinese counterpart lives at
[`grammar_polish_zh.md`](grammar_polish_zh.md).

## 1. Optional outer parens on `IF` / `WHILE` / `FOR`

`IF (cond) { … }` and `IF cond { … }` are now equivalent; the same
applies to `WHILE` and `FOR`.

For `IF` and `WHILE` no parser change was strictly required because
the expression grammar already accepts `(expr)` as a grouped
expression.  `FOR` was taught explicitly: `ParseForStatement` matches
an optional leading `(` and, when seen, requires a matching `)` after
the iterable.

```ploy
IF (n > 0) { … }            // identical to IF n > 0 { … }
WHILE (running) { … }       // identical to WHILE running { … }
FOR (x IN xs) { … }         // identical to FOR x IN xs { … }
```

## 2. `IF LET` `OPTION<T>` destructuring

A new statement form unwraps an `OPTION<T>` into a binding visible in
the THEN-body's scope:

```ploy
IF LET Some(x) = opt { use(x); }
IF LET Some(x) = opt { use(x); } ELSE { fallback(); }
IF LET None    = opt { … }       // takes no binding
```

A new AST node `IfLetStatement` holds `ctor`, `bindings`, `scrutinee`,
`then_body`, and `else_body`.  Sema enforces:

- The scrutinee must be `OPTION<T>` (or `Any` / `Unknown` for
  unresolved cross-language values);
- `Some` requires exactly one binding name;
- `None` accepts zero bindings;
- The binding is introduced as a fresh `kVariable` symbol whose type
  is `T` (the inner type argument of the `OPTION`).

The lowering pass dispatches on the scrutinee's truthiness as the MVP
implementation; full tag-discriminant emission lands with the
dedicated OPTION lowering work track.

## 3. `NULL` ↔ `OPTION<T>` diagnostic

`NULL` is reserved for raw-pointer interop.  Using it to construct an
`OPTION<T>` value is now rejected by sema with a targeted message
suggesting `None`:

```ploy
LET o: OPTION<i32> = NULL;
// error: cannot initialise OPTION<T> with NULL; use 'None' instead
```

## 4. `///` documentation comments

A line beginning with exactly three slashes is a *doc comment*.  The
lexer keeps a per-instance buffer (`pending_doc_`) of doc lines (one
optional leading space stripped, trailing CR removed) and exposes
`TakePendingDoc()` to the parser.

`ParseFuncDecl`, `ParseStructDecl`, and `ParseVarDecl` collect the
pending buffer at entry and store it on the resulting AST node.  Plain
`//` line comments and `////` (four-slash) banners remain ordinary
line comments.

```ploy
/// Adds two integers.
/// Overflow wraps modulo 2^64.
FUNC add(a: i64, b: i64) -> i64 { RETURN a + b; }
```

## 5. `polydoc` extractor

The new `polydoc` executable (`tools/polydoc/`) walks one or more
`.ploy` files, harvests every doc-bearing top-level declaration, and
emits a Markdown or JSON report.

```sh
polydoc src/foo.ploy                 # Markdown to stdout
polydoc --json src/foo.ploy          # JSON to stdout
polydoc -o api.md src/foo.ploy       # write Markdown to api.md
```

The Markdown form pairs each entry with a synthesised signature line
(`FUNC name(params) -> R`, `STRUCT Name`, `LET name: T`).  The JSON
form records the same data plus the source file path so downstream
toolchains can index it.

## 6. `LIST<T>` naming clarification

`LIST<T>` is a contiguous-sequence container — equivalent to Rust
`Vec<T>` and C++ `std::vector<T>`.  It is **not** a linked list.  The
spec, this document, and the IDE tooltips all carry the same wording
to avoid confusion with linked-list APIs.

## 7. Tests

- `tests/unit/frontends/ploy/polish_grammar_test.cpp` (11 cases):
  optional parens on IF/WHILE/FOR, `IF LET Some` / `IF LET None`,
  `NULL`-with-OPTION diagnostic, `///` capture for FUNC / STRUCT /
  LET, and the `//`-vs-`///` boundary.
- `tests/samples/41_grammar_polish/` exercises the new constructs
  end-to-end and is referenced from the samples README.
