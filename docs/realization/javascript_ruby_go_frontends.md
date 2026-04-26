# JavaScript / Ruby / Go Frontends

> Demand: `2026-04-26-01` — *"Add three more languages to the frontend: JS, Ruby, Go."*

This document describes the realisation of three additional language frontends
plugged into the existing PolyglotCompiler pipeline.

## 1. Overview

Each new frontend follows the same six-stage anatomy already used by the
Java / Rust / .NET frontends:

| Stage          | File                                          |
| -------------- | --------------------------------------------- |
| AST nodes      | `frontends/<lang>/include/<lang>_ast.h`       |
| Lexer          | `<lang>_lexer.h` + `src/lexer/lexer.cpp`      |
| Parser         | `<lang>_parser.h` + `src/parser/parser.cpp`   |
| Sema           | `<lang>_sema.h`   + `src/sema/sema.cpp`       |
| IR Lowering    | `<lang>_lowering.h` + `src/lowering/lowering.cpp` |
| Frontend glue  | `<lang>_frontend.h` + `src/<lang>_frontend.cpp` |

All three register themselves at static-initialisation time through
`REGISTER_FRONTEND(...)` so that `polyc`, `polyui`, and the test binaries pick
them up automatically.

## 2. JavaScript (ECMAScript)

* **Library target:** `frontend_javascript`
* **Display name:** `JavaScript`
* **Aliases:** `javascript`, `js`, `node`, `es`, `ecmascript`
* **Extensions:** `.js`, `.mjs`, `.cjs`

### Lexer
Handles standard ES syntax including:

* Template literals with `${...}` interpolation
* Regular expression literals (lookback-driven disambiguation against `/`)
* Numeric literals: decimal, hex (`0x`), octal (`0o`), binary (`0b`), `BigInt`
  suffix `n`
* Automatic Semicolon Insertion is left to the parser; the lexer emits
  newlines as part of the token stream.

### Parser
Covers function declarations, arrow functions, classes (including `extends`,
`static`, getters/setters, private `#` fields), destructuring assignment,
spread / rest, default parameters, `import` / `export`, and modern statements
(`for-of`, `for-in`, `try/catch/finally`, `switch`, labelled `break`).

### Sema
Tracks scopes (`var` vs `let`/`const`), validates `break`/`continue` placement
and warns on use-before-declaration in the temporal dead zone.

### Lowering
Maps each function and method to an IR `Function`. Closures are captured by
boxing free variables into a synthetic environment record so that the existing
SSA infrastructure can analyse them like any other allocation.

## 3. Ruby

* **Library target:** `frontend_ruby`
* **Display name:** `Ruby`
* **Aliases:** `ruby`, `rb`
* **Extensions:** `.rb`, `.rbw`

### Lexer
Recognises the canonical Ruby keyword set (`def`, `class`, `module`, `do`,
`end`, …), method-name suffixes `?`/`!`, symbol literals (`:foo`), and both
single- and double-quoted strings. Heredocs and `%w()` arrays are degenerate
to ordinary string literals for the time being so that the parser can move on.

### Parser
Builds a single top-level `Module` containing classes, modules, methods,
top-level statements and constant assignments. The expression grammar follows
Ruby's classic precedence ladder; method calls without parentheses
(`puts "hi"`) are accepted via a single-token look-ahead.

### Sema
Performs scope and arity bookkeeping, ensures `break`/`next`/`redo` only
appear inside iterators or loops, and rejects duplicate top-level constants
and method definitions in the same scope.

### Lowering
Each method becomes an IR `Function`; instance methods are emitted as
`Receiver.method` symbols so that the topology tool can show them under their
defining class. Blocks (`do |x| … end`) are lowered to anonymous IR functions
plus a closure record.

## 4. Go

* **Library target:** `frontend_go`
* **Display name:** `Go`
* **Aliases:** `go`, `golang`
* **Extensions:** `.go`

### Lexer
Implements the Go specification's *automatic semicolon insertion* rule: after
a token that may terminate a statement (identifier, literal, `)`, `]`, `}`,
`++`, `--`, `return`, `break`, `continue`, `fallthrough`) the lexer emits an
implicit `;` whenever the next character is a newline. Three-character
operators (`<<=`, `>>=`, `&^=`, `...`) are tried before two-character
(`<-`, `:=`, `==`, `!=`, …) and finally single-character punctuation.
Raw strings (`` ` … ` ``) and rune literals (`'…'`) are supported.

### Parser
Implements the full surface grammar:

* Package & imports (single and grouped)
* Top-level `var` / `const` / `type` blocks
* Function declarations including method receivers `func (r T) M(...)`
* Multi-return signatures and variadic `...T` parameters
* Composite literals (`T{...}`), type assertions `x.(T)`, slice expressions
  `a[lo:hi:max]`, channel ops `<-ch` / `ch <- v`, and `go` / `defer`
* Statements: `if` (with init clause), three-clause / cond-only / `range`
  `for`, `switch` (expression and type), `select`

### Sema
Validates `break` / `continue` / `fallthrough` placement, checks for duplicate
top-level identifiers, and verifies that imports and method receivers are
well-formed.

### Lowering
`ToIRType` maps the Go primitive set to IR types
(`int8/16/32/64`, `uint*`, `float32/64`, `bool`, `string`, `byte`, `rune`,
`error`); composite types are mapped via `core::Type::Slice`, `Array`, manual
`kPointer` records, and `kStruct` records. Methods are emitted as
`Recv.Method` symbols and qualified with the package name for any non-`main`
package, matching the existing convention used by Java packages.

## 5. Build & Test Integration

* `frontends/CMakeLists.txt` declares the three new libraries.
* `tools/CMakeLists.txt` links `polyc_lib`, `polybench`, and `polyui` against
  them, plus they are listed in `_POLYUI_DYLIB_TARGETS` for macOS bundle
  deployment.
* `tests/CMakeLists.txt` ships dedicated test binaries
  `test_frontend_javascript`, `test_frontend_ruby`, `test_frontend_go` and
  registers them with CTest.
* The `unit_tests` aggregate target also picks up the new sources.

## 6. UI / Driver Integration

* `tools/polyc/src/driver.cpp` extends the `--lang` help line to advertise
  `javascript|ruby|go` (the registry already drives extension-based detection).
* `tools/ui/common/src/mainwindow.cpp` adds `JavaScript`, `Ruby`, and `Go`
  to the language combo, the file extension mapping and `DetectLanguage`.
* `tools/ui/common/src/settings_dialog.cpp` widens the *Default Language*
  combo to match.
* `tools/ui/common/src/file_browser.cpp` extends the workspace globs with
  `*.js`, `*.mjs`, `*.cjs`, `*.rb`, `*.go`.
* `tools/ui/common/src/compiler_service.cpp` registers keyword tables for
  completion and pattern-based workspace indexers for `function` / `class`
  (JS), `def` / `class` / `module` (Ruby) and `func` / `type` (Go).

## 7. Testing Strategy

Each language ships a focused Catch2 test file at
`tests/unit/frontends/<lang>/<lang>_test.cpp` exercising:

* Lexer keyword & literal recognition
* Parser acceptance of representative real-world programs
* At least one negative test (malformed program → diagnostic)
* End-to-end lowering producing a non-empty IR with the expected function
  symbol

These tests are deliberately behavioural — every assertion observes a real
program property and never resorts to `REQUIRE(true)` placeholders.
