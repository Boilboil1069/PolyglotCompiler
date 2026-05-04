# Pattern Matching

This document describes the `MATCH` statement and its pattern grammar
in `.ploy`, the semantic checks the front-end performs on each arm,
and the two lowering strategies the IR generator selects between.
The Chinese counterpart lives at
[`pattern_matching_zh.md`](pattern_matching_zh.md).

## 1. Surface syntax

```
match_stmt    ::= 'MATCH' expr '{' (case_arm | default_arm)* '}'
case_arm      ::= 'CASE' pattern ('IF' expr)? arm_body
default_arm   ::= 'DEFAULT' arm_body
arm_body      ::= ('->' | '=>')? '{' statement* '}'
```

The arrow between the pattern (or guard) and the arm body is optional;
both `->` and `=>` are accepted for source written against the older
spec drafts.  The canonical published form omits the arrow:

```ploy
MATCH value {
    CASE 0 { RETURN "zero"; }
    CASE _ { RETURN "other"; }
}
```

The `MATCH` statement does **not** fall through: each arm executes its
body and control jumps to the join point that follows the `MATCH`.
The `MATCH` itself is a statement, not an expression — to produce a
value, every arm must `RETURN` (or assign through a captured `VAR`).

## 2. Pattern grammar

```
pattern         ::= pattern_primary ('|' pattern_primary)*

pattern_primary ::= '_'                                          # wildcard
                  | literal_with_optional_minus
                  | literal '..'  literal                        # half-open range
                  | literal '..=' literal                        # inclusive range
                  | '(' pattern (',' pattern)* ')'               # tuple
                  | Identifier '(' pattern (',' pattern)* ')'    # constructor
                  | Identifier '{' field_pattern (',' field_pattern)* (',' '..')? '}'
                  | Identifier '@' pattern_primary               # binding
                  | Identifier ':' Type                          # type guard
                  | 'None'                                       # OPTION unit ctor
                  | Identifier                                   # bare bind

field_pattern   ::= Identifier (':' pattern)?
```

The bare identifier `None` is parsed as a zero-argument
`ConstructorPattern` (matching the OPTION unit variant) instead of an
`IdentifierPattern`, so that `CASE None` participates in OPTION
exhaustiveness rather than silently capturing the scrutinee.

### 2.1 Pattern semantics

| Pattern              | Refutable? | Bindings introduced              |
| -------------------- | ---------- | -------------------------------- |
| `_`                  | No         | none                             |
| `42`, `"x"`, `TRUE`  | Yes        | none                             |
| `1..10`, `0..=255`   | Yes        | none                             |
| `(a, b)`             | Iff any element is | union of element bindings |
| `Point { x, y, .. }` | Iff any field sub-pattern is | union of field bindings |
| `1 \| 2 \| 3`        | Iff every alternative is | bindings of the first alternative (all alternatives must bind the same names with compatible types) |
| `n @ 0..=100`        | Iff sub-pattern is | `n` plus sub bindings |
| `n: i32`             | No (statically refined) | `n` with the refined type |
| `Some(x)`            | Yes        | bindings of `x` (and so on)      |
| `None`               | Yes        | none                             |
| `name`               | No         | `name` bound to scrutinee        |

A pattern is **irrefutable** when it accepts every value of the
scrutinee's static type.  Irrefutable patterns are what allow a
`MATCH` to be exhaustive without an explicit `DEFAULT` or `CASE _`.

### 2.2 Disambiguation

A bare identifier `Name` followed by `{` is ambiguous: it could be a
struct pattern (`Point { x, y }`) or an identifier pattern whose `{`
opens the arm body (`CASE None { RETURN ...; }`).  The parser performs
one-token lookahead past the `{`: if the next token is an identifier,
`..`, or the immediate closer `}`, the parser commits to a
`StructPattern`; otherwise the `{` is treated as the arm body.

## 3. Semantic checks

### 3.1 Type compatibility

Each pattern is checked against the scrutinee's static type.  Numeric
literals are checked for assignability into the scrutinee type, range
patterns require an integer or floating-point scrutinee, struct
patterns are checked against the registered struct schema (unknown
fields produce a hard error), constructor patterns are checked
against the OPTION schema, and tuple patterns must match the
scrutinee's arity.

### 3.2 Bindings and scope

Bindings introduced by a pattern are added to the symbol table for
the duration of the arm body and its `IF` guard, then popped on exit.
Bindings introduced by the same name in different arms therefore
never leak across arms, and the same identifier can appear in
multiple arms with different types.

### 3.3 Exhaustiveness

When the `MATCH` has no `DEFAULT` and no irrefutable arm, the front-
end checks that the union of all arm patterns covers the scrutinee
type:

| Scrutinee type | Required cover                                        |
| -------------- | ----------------------------------------------------- |
| `bool`         | both `TRUE` and `FALSE` literal arms                  |
| `OPTION(T)`    | both a `Some(_)` arm and a `None` arm                 |
| anything else  | one irrefutable arm (`CASE _` / `CASE name` / tuple / struct of irrefutable subs / `name: Type`) or `DEFAULT` |

A non-exhaustive MATCH is reported as a hard error
(`E_TYPE_MISMATCH`) with a fix-it suggesting either `CASE _` or the
specific missing variant.

### 3.4 Reachability

After each arm is recorded, the analyzer flags as **unreachable**:

* arms that follow an irrefutable arm or a `DEFAULT`;
* literal arms whose value duplicates a previously-recorded literal.

Unreachable arms produce warnings rather than errors; the lowering
pass still emits them so that user-facing line numbers and the
diagnostic UI continue to work.

### 3.5 Guards

`IF expr` after a pattern is type-checked as a `bool` expression
inside the scope of the pattern's bindings.  A guard makes the arm
refutable even if its pattern would otherwise be irrefutable, so the
arm cannot satisfy exhaustiveness on its own.

## 4. Lowering

`LowerMatchStatement` selects between two paths:

### 4.1 Fast path — dense switch table

When every CASE pattern is a simple integer literal (or the wildcard
`_`) and no arm carries an `IF` guard, the lowering pass emits a
single `ir::SwitchStatement` whose default target is either the
explicit `DEFAULT` / `CASE _` block or a synthesised `match.merge`
unreachable block.  Each arm becomes its own basic block and the
backend is free to materialise the table as a dense jump table.

### 4.2 Cascade path — structural if/else chain

For any other shape of pattern (range, tuple, struct, OR, binding,
type-guard, constructor, or any guarded arm) the lowering pass
synthesises an `i1` predicate per arm via `lower_predicate` and
chains the arms as `match.try.N` → `match.body.N` → `match.merge`
basic blocks:

```
                 ┌──────────────────┐
                 │ entry            │
                 │ scrutinee = …    │──┐
                 └──────────────────┘  │
                                       ▼
                       ┌──────────────────┐    miss     ┌──────────────────┐
                       │ match.try.0      │ ──────────► │ match.try.1      │ ─► …
                       │ pred(p0)         │             │ pred(p1)         │
                       └──────────────────┘             └──────────────────┘
                                │ hit                          │ hit
                                ▼                              ▼
                       ┌──────────────────┐             ┌──────────────────┐
                       │ match.body.0     │             │ match.body.1     │
                       │ bind(p0); body0  │             │ bind(p1); body1  │
                       └──────────────────┘             └──────────────────┘
                                │ no terminator                │
                                └─────────────► match.merge ◄──┘
```

Pattern bindings are materialised before the body block by reusing
the scrutinee SSA value (or a tuple/struct projection of it); no copy
is required because `.ploy` values are immutable by default.

The cascade is what enables guards, OR-patterns with mixed
alternatives, OPTION variants, and arbitrary nested patterns to
compile down to plain branches the verifier already understands.

## 5. Examples

### 5.1 Boolean dispatch — exhaustive without DEFAULT

```ploy
FUNC describe(b: bool) -> STRING {
    MATCH b {
        CASE TRUE  { RETURN "yes"; }
        CASE FALSE { RETURN "no"; }
    }
}
```

### 5.2 OPTION unwrap

```ploy
FUNC unwrap_or(opt: OPTION(i32), fallback: i32) -> i32 {
    MATCH opt {
        CASE Some(x) { RETURN x; }
        CASE None    { RETURN fallback; }
    }
}
```

### 5.3 Range / OR / binding / type guard, all in one MATCH

```ploy
FUNC classify(value: i32) -> i32 {
    MATCH value {
        CASE 0                    { RETURN 100; }
        CASE 2 | 4 | 8 | 16       { RETURN 102; }
        CASE 10..20               { RETURN 103; }
        CASE n @ 100..=199        { RETURN n + 200; }
        CASE n: i32 IF n > 1000   { RETURN n - 1000; }
        CASE _                    { RETURN -1; }
    }
}
```

### 5.4 Tuple and struct destructuring

```ploy
STRUCT Point { x: i32, y: i32, label: STRING }

FUNC pair_kind(p: TUPLE(i32, i32)) -> i32 {
    MATCH p {
        CASE (0, 0) { RETURN 200; }
        CASE (_, 0) { RETURN 201; }
        CASE (0, _) { RETURN 202; }
        CASE (a, b) { RETURN a + b; }
    }
}

FUNC point_kind(pt: Point) -> i32 {
    MATCH pt {
        CASE Point { x: 0, y: 0, .. } { RETURN 300; }
        CASE Point { x, y, .. }       { RETURN x + y; }
    }
}
```

A complete runnable example is shipped under
[`tests/samples/33_pattern_matching/`](../../tests/samples/33_pattern_matching/).

## 6. Diagnostics catalogue

| Severity | Trigger                                              | Suggestion |
| -------- | ---------------------------------------------------- | ---------- |
| Error    | non-exhaustive MATCH on `bool`                       | add the missing `TRUE` / `FALSE` arm |
| Error    | non-exhaustive MATCH on `OPTION`                     | add `Some(_)` and / or `None` |
| Error    | non-exhaustive MATCH on any other type               | add `CASE _` or `DEFAULT` |
| Error    | OR-pattern alternatives bind different names         | align bindings across alternatives |
| Error    | range pattern on a non-numeric scrutinee             | restrict the range to integer / float scrutinees |
| Error    | tuple pattern arity mismatch                         | match the scrutinee's tuple arity |
| Error    | struct pattern names an unknown field                | check the struct schema |
| Error    | guard expression is not `bool`                       | wrap the guard in `==` / `<` / `AND` |
| Warning  | arm follows an irrefutable arm or `DEFAULT`          | move the unreachable arm above the catch-all or delete it |
| Warning  | duplicate literal arm                                | delete one of the duplicates |

## 7. Test coverage

* Unit: [`tests/unit/frontends/ploy/pattern_matching_test.cpp`](../../tests/unit/frontends/ploy/pattern_matching_test.cpp)
  exercises every pattern shape, the exhaustiveness rules and the
  reachability warnings.
* Lowering: [`tests/unit/frontends/ploy/pattern_matching_lowering_test.cpp`](../../tests/unit/frontends/ploy/pattern_matching_lowering_test.cpp)
  locks in the fast / cascade dispatch decision and the basic-block
  shape for both paths.
* Sample: [`tests/samples/33_pattern_matching/`](../../tests/samples/33_pattern_matching/)
  is a runnable end-to-end demo with a deterministic stdout marker.
