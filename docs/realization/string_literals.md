# Realization — Extended String Literals (v1.17.0)

Demand: `2026-04-28-17`.  Adds raw, multiline, and template string
literal forms to `.ploy` source.

## 1. Surface syntax

| Form          | Syntax                                  |
| ------------- | --------------------------------------- |
| Regular       | `"hello\n"`                             |
| Raw           | `r"C:\path\no\escape"`                  |
| Raw padded    | `r#"contains "quotes""#` / `r##"..."##` |
| Multiline     | `"""line1\nline2"""`                    |
| Template      | `f"x = {expr}"` / `f"""..."""`          |

A leading identifier-like character (`r`, `f`) is recognised as a
string-literal prefix only when the very next character is `"` (or `#`
for raw); ordinary identifiers like `result`, `foo` keep their usual
meaning.

## 2. Lexer

- New helpers in `frontends/ploy/src/lexer/lexer.cpp`:
  `LexRawString`, `LexMultilineString`, `LexTemplateString`.
  `LexString` itself absorbs the triple-quote case so the existing
  one-token-per-string contract is preserved.
- All four extended forms produce a single `kString` token.  Raw and
  multiline bodies are **re-encoded** into the canonical `"..."`
  representation (newlines → `\n`, embedded `"` → `\"`, etc.) so the
  existing escape-decoding pipeline (`DecodePrintlnLiteral` and friends)
  works without modification.
- Template literals retain the `f"..."` (or `f"""..."""`) prefix in the
  emitted lexeme so the parser can dispatch on the leading character.

## 3. AST

- New node `polyglot::ploy::TemplateString` (`frontends/ploy/include/ploy_ast.h`).
- Each `TemplateString` carries `parts: vector<Part>`; each `Part` is
  either a literal text fragment or an interpolated `Expression`.

## 4. Parser

- `BuildStringExpression(lexeme, loc)` (in `frontends/ploy/src/parser/parser.cpp`)
  centralises string handling.  When the lexeme starts with `f"` it
  carves out the body, walks character-by-character, splits on `{` /
  `}` (with `{{` / `}}` escapes), and uses a fresh `PloyLexer` +
  `PloyParser` instance to sub-parse each interpolated expression.
- Diagnostics for unmatched `}` flow through the surrounding parser's
  diagnostics sink.

## 5. Sema

- `PloySema::AnalyzeExpression` learns a `TemplateString` branch.
- Each interpolated expression must have a *formattable* type — `Int`,
  `Float`, `String`, or `Bool` (with `Any` / `Unknown` accepted so
  unresolved cross-language references do not block compilation).
- The expression result type is always `String`.

## 6. Lowering (MVP scope)

- `PloyLowering::LowerExpression` learns a `TemplateString` branch.
- When every interpolated expression is itself a constant `Literal`,
  the lowering layer assembles the formatted bytes at compile time and
  emits a single interned global through `IRBuilder::MakeStringLiteral`
  — exactly the same path regular string literals take.
- When the template references a runtime value, the lowering layer
  concatenates the literal text segments only and emits a warning
  (`kGenericWarning`); the runtime-formatting helper is tracked as
  follow-up work.

## 7. Cross-language transport

- Template interpolation expands strictly on the `.ploy` side; the host
  language receives a normal already-formatted string and reuses every
  existing marshalling / NUL-terminated conversion path unchanged.
- Raw and multiline literals make it ergonomic to embed SQL / JSON /
  cross-language source snippets inline (see
  `tests/samples/22_database_access`, `20_json_pipeline`).

## 8. Future work

- Wire a runtime helper (`polyrt_format_to_string(...)` or similar) so
  template strings can interpolate runtime values without losing IR
  fidelity; remove the lowering-layer warning.
- Support **indented multiline** dedenting:
  `"""\n  line one\n  line two\n"""` → strip the common leading
  whitespace on every line and the leading / trailing newline.
- Support **format spec** suffixes inside template braces
  (`{value:.3f}`).
- Permit nested template strings inside an interpolation segment.
