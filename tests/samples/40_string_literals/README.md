# Sample 40: Extended String Literals

Demonstrates the four string literal forms shipped in v1.17.0.

| Form          | Syntax                                  | Notes                                     |
| ------------- | --------------------------------------- | ----------------------------------------- |
| Regular       | `"hello\n"`                             | Standard backslash escapes               |
| Raw           | `r"C:\path\no\escape"`                  | Backslashes are literal                   |
| Raw padded    | `r#"contains "quotes""#`                | `#` padding lets the body hold `"`        |
| Multiline     | `"""line1\nline2"""`                    | Newlines preserved verbatim               |
| Template      | `f"x = {x + y}"`                        | Brace-delimited expression interpolation  |

## Build

```bash
polyc string_literals.ploy -o string_literals
./string_literals
```

## v1.17.0 MVP scope

Lexer, parser, AST, and sema are wired end-to-end.  The lowering layer
folds template interpolation **at compile time** when every interpolated
expression is a `Literal`; runtime variable interpolation is recorded
as a follow-up item — `docs/realization/string_literals.md` tracks the
full plan.
