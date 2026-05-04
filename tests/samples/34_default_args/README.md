# 34_default_args — Named-parameter default values

`default_args.ploy` demonstrates the named-parameter / default-value
extension introduced by demand 2026-04-28-11.  Three shapes are
covered:

| Sample call           | Equivalent positional form |
| --------------------- | -------------------------- |
| `add(10)`             | `add(10, 0)`               |
| `add(x: 7)`           | `add(7, 0)`                |
| `add(2, y: 5)`        | `add(2, 5)`                |
| `scale(value: 6)`     | `scale(6, one())` → `6`    |
| `scale(4, 3)`         | `scale(4, 3)`              |

Rules enforced by sema and the parser:

* Required parameters must precede defaulted parameters in the
  declaration; mixing the order is a parse-time error.
* A default expression must be either a constant-foldable literal /
  unary / binary expression or a pure intra-Ploy call (no
  cross-language `CALL`, no closure capture).  Reading another
  parameter inside the default is rejected.
* A call site may freely mix positional and named arguments; a
  positional argument may not follow a named argument.  Every
  required (no-default) parameter must be supplied either by
  position or by name.

Lowering injects a copy of the default expression at every call site
that omits the corresponding argument, so the back-end never sees a
short call.

The Chinese mirror lives at [`README_zh.md`](README_zh.md).
