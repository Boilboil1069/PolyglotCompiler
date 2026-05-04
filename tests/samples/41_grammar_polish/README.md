# Sample 41 — Grammar polish (since v1.18.0)

Showcases the v1.18.0 grammar polish bundle:

- Optional outer parens on `IF` / `WHILE` / `FOR`. `IF (cond) { … }` and
  `IF cond { … }` parse identically; the same applies to `WHILE` and
  `FOR (i IN xs) { … }`.
- `IF LET Some(x) = opt { … } ELSE { … }` to destructure an
  `OPTION<T>` value. `IF LET None = opt { … }` is also accepted and
  takes no bindings.
- `///` documentation comments attached to the immediately following
  `FUNC` / `STRUCT` / `LET` / `VAR` declaration. The `polydoc` tool
  walks `.ploy` sources and renders the harvested doc blocks as Markdown
  or JSON.
- Reminder: `LIST<T>` is a contiguous sequence container — the same
  shape as Rust `Vec<T>` or C++ `std::vector<T>`. It is *not* a linked
  list. See the spec for the formal definition.
- Reminder: `NULL` is reserved for raw-pointer interop. Do **not** use
  `NULL` to construct an `OPTION<T>` — sema rejects it with a targeted
  diagnostic suggesting `None` instead.

Build and run:

```sh
polyc 41_grammar_polish/grammar_polish.ploy -o grammar_polish
./grammar_polish

polydoc 41_grammar_polish/grammar_polish.ploy           # Markdown
polydoc --json 41_grammar_polish/grammar_polish.ploy    # JSON
```
