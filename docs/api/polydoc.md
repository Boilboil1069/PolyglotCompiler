# `polydoc` — Ploy doc-comment extractor (since v1.18.0)

`polydoc` is a small command-line tool that walks one or more `.ploy`
source files, harvests every `///` documentation block attached to a
top-level `FUNC` / `STRUCT` / `LET` / `VAR` declaration, and emits the
result as Markdown or JSON.

The Chinese counterpart lives at [`polydoc_zh.md`](polydoc_zh.md).

## Synopsis

```text
polydoc [--json] [-o OUT] FILE [FILE ...]
```

| Flag        | Description                                              |
|-------------|----------------------------------------------------------|
| `--json`    | Emit a JSON document instead of Markdown                 |
| `-o OUT`    | Write the rendered output to `OUT` (default: stdout)     |
| `-h / --help` | Show usage and exit                                    |

`polydoc` exits 0 on success, 1 on I/O error, and 2 on argument error.

## Doc-comment surface

A doc comment is a line beginning with **exactly three** slashes:

```ploy
/// First line.
/// Second line.
FUNC add(a: i32, b: i32) -> i32 { … }
```

Plain `//` line comments and `////` four-slash banners are *not* doc
comments and are ignored by `polydoc`.  The lexer strips one optional
leading space and any trailing CR before storing the line, so
`/// hello` records as `hello`.

Doc blocks attach to the *immediately following* declaration.  An
intervening blank line is fine; an intervening non-comment statement
or a blank that contains another non-doc token clears the buffer.

`polydoc` walks the parsed module and collects entries in source order.

## Markdown output

```sh
polydoc src/api.ploy
```

emits, for the example above:

```markdown
# src/api.ploy

## `FUNC add(a: I32, b: I32) -> I32`

First line.
Second line.
```

The signature line is synthesised from the AST and uses the canonical
upper-case spelling of built-in primitive types.

## JSON output

```sh
polydoc --json src/api.ploy
```

emits a single object:

```json
{
  "file": "src/api.ploy",
  "entries": [
    {
      "kind": "func",
      "name": "add",
      "signature": "FUNC add(a: I32, b: I32) -> I32",
      "doc": ["First line.", "Second line."]
    }
  ]
}
```

`kind` is one of `func`, `struct`, `let`, or `var`.  `signature` is a
single-line, human-readable summary.  `doc` is the unmodified list of
doc-comment lines in source order.

## Exit codes

| Code | Meaning                                                    |
|------|------------------------------------------------------------|
| 0    | All requested files parsed and emitted successfully        |
| 1    | A source file could not be read or an output file written  |
| 2    | Bad argument (no input file, missing value after `-o`)     |

Parser errors do not change the exit code; `polydoc` walks whatever
declarations were successfully parsed and emits documentation for
those.  Use `polyc` for full diagnostics.

## See also

- [`docs/realization/grammar_polish.md`](../realization/grammar_polish.md)
  — full v1.18.0 polish bundle
- [`docs/specs/language_spec.md`](../specs/language_spec.md) §
  documentation comments
