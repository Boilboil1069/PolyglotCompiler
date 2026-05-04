# PolyglotCompiler Changelog

All notable changes to PolyglotCompiler are documented in this file.

The Chinese counterpart is maintained at [`CHANGELOG_zh.md`](CHANGELOG_zh.md).
For day-to-day usage instructions see [`USER_GUIDE.md`](USER_GUIDE.md); for
build / API contracts see [`api/api_reference.md`](api/api_reference.md) and
the per-feature notes under [`realization/`](realization/).

The version range covered below is **v0.1.0 (2026-01-15) 鈫?v1.9.0 (2026-04-29)**.
Newer entries appear first.  Each `### vX.Y.Z (YYYY-MM-DD)` block lists the
shipped behaviour, not the underlying tracking item.

---

## v1.18.0 (2026-05-05)

**P3 grammar polish bundle.**  A small collection of source-level
refinements that reduce friction for users porting from Rust / Python /
TypeScript: optional outer parens on control-flow heads, `IF LET`
`OPTION<T>` destructuring, a `///` documentation-comment lane, and a
new `polydoc` extractor tool.

* **Parser.**  `IF` / `WHILE` / `FOR` accept optional outer parens.
  `IF (cond) { … }` parses identically to `IF cond { … }` because the
  expression grammar already treats `(cond)` as a grouped expression;
  `FOR (i IN xs) { … }` is taught explicitly to consume the surrounding
  parens.
* **AST + Parser.**  New `IfLetStatement` node and
  `ParseIfLetStatement` entry: `IF LET Some(x) = opt { … } ELSE { … }`
  and `IF LET None = opt { … }` destructure an `OPTION<T>` scrutinee
  and introduce the binding into the THEN-body's scope.
* **Sema.**  `AnalyzeIfLetStatement` enforces that the scrutinee is
  `OPTION<T>` (or `Any` / `Unknown` for cross-language values), checks
  the constructor name (`Some` requires one binding; `None` takes
  none), and registers the binding as a fresh `kVariable` symbol.
* **Sema.**  `LET x: OPTION<T> = NULL;` now produces a targeted
  `kTypeMismatch` diagnostic suggesting `None`.  `NULL` remains
  reserved for raw-pointer interop.
* **Lexer + AST.**  `///`-prefixed line comments are captured into
  `pending_doc_` and attached as `doc_comment: vector<string>` to the
  immediately following `FUNC` / `STRUCT` / `LET` / `VAR` declaration.
  Plain `//` and `////` continue to be plain line comments.
* **Tooling.**  New `polydoc` executable (`tools/polydoc/`) reads
  `.ploy` source, walks the parsed module, and emits doc blocks as
  Markdown (default) or JSON (`--json`).  Output may be redirected via
  `-o OUT`.
* **Spec.**  `LIST<T>` is documented as a contiguous-sequence
  container equivalent to Rust `Vec<T>` / C++ `std::vector<T>` — not a
  linked list.
* **Tests.**  `tests/unit/frontends/ploy/polish_grammar_test.cpp` adds
  11 new cases covering optional parens, `IF LET`, the NULL-with-OPTION
  diagnostic, and `///` capture.  Sample 41 (`tests/samples/41_grammar_polish/`)
  exercises every new construct end-to-end.

---

## v1.17.0 (2026-05-04)

**Extended string literal forms: raw `r"..."` / `r#"..."#`,
multiline `"""..."""`, and template `f"..."` interpolation.**  All
four forms share the existing `kString` token type and lower through
the established `polyrt_println`-style interning path.

* **Lexer.**  `LexRawString`, `LexMultilineString`, and
  `LexTemplateString` helpers join the existing `LexString`; raw and
  multiline bodies are re-encoded into the canonical `"..."` form so
  the existing escape-decoding pipeline keeps working unchanged.  The
  `r` / `f` prefix is recognised only when the next character is `"`
  (or `#` for raw), so identifiers like `result` and `foo` are not
  mis-lexed.
* **AST.**  New `polyglot::ploy::TemplateString` node carrying
  `parts: vector<Part>`, where each `Part` is either a literal text
  fragment or an interpolated `Expression`.
* **Parser.**  `BuildStringExpression(lexeme, loc)` centralises
  string-literal handling, splits `f"..."` bodies on `{` / `}` (with
  `{{` / `}}` escapes), and sub-parses each interpolation through a
  fresh `PloyLexer` + `PloyParser` pair.
* **Sema.**  `AnalyzeExpression` learns a `TemplateString` branch.
  Each interpolated expression must have a *formattable* type — `Int`,
  `Float`, `String`, or `Bool` (with `Any` / `Unknown` accepted so
  unresolved cross-language references do not block compilation).  The
  expression result type is always `String`.
* **Lowering (MVP).**  Template strings whose interpolated parts are
  all compile-time `Literal` expressions are folded eagerly into a
  single interned global.  When the template references a runtime
  value, the lowering layer concatenates the literal text segments
  only and emits a `kGenericWarning`; the runtime-formatting helper is
  tracked as follow-up work.
* **Tests.** `tests/unit/frontends/ploy/string_literals_test.cpp`
  (13 cases / 18 assertions) all pass; full regression
  `test_frontend_ploy` 437 / 2603.
* **Sample.** `tests/samples/40_string_literals/` with bilingual READMEs.
* **Docs.** Bilingual realization
  `docs/realization/string_literals{,_zh}.md`, language spec
  "String Literals" subsection, USER_GUIDE 4.2.5f, tutorial 16.9 (en) /
  16.11 (zh).
* **Future work.** Runtime-formatting helper for variable
  interpolation; indented multiline dedenting; format-spec suffixes
  inside template braces (`{value:.3f}`); nested template strings.

## v1.16.0 (2026-05-04)

**Module-boundary visibility (`PUB` / `PRIVATE`) and the
`@name(args)` attribute prefix on top-level `FUNC` and `STRUCT`
declarations.**  Visibility and attributes survive parsing and sema
as metadata on the AST; the IR builder, optimiser, and runtime are
unchanged in this MVP.

* **Surface syntax.** Two new keywords are recognised by the lexer:
  `PUB` and `PRIVATE`.  The parser accepts an optional prefix of the
  form `@name @name(arg, ...) PUB|PRIVATE` before `FUNC`,
  `ASYNC FUNC`, and `STRUCT`.  Other top-level forms reject the
  prefix with a parser diagnostic.
* **AST.** `Visibility { kPrivate, kPub }`, `Attribute { name, args }`,
  and `visibility` / `visibility_explicit` / `attributes` fields on
  `FuncDecl` and `StructDecl`.
* **Sema.** `PloySymbol` carries the visibility pair so `EXPORT`
  analysis can consult it.  `PloySema::ValidateAttributes` warns on
  any name outside the built-in registry (`inline`, `noinline`,
  `always_inline`, `hot`, `cold`, `profile`, `no_profile`,
  `deprecated`, `link_name`, `target`).  `AnalyzeExportDecl` requires
  `kPub`: an explicit `PRIVATE` is a hard error, while a symbol that
  still carries the default `kPrivate` is auto-promoted with a
  deprecation warning so pre-v1.16.0 sources keep compiling.
* **Lowering.** Unchanged; attributes and visibility are inert
  metadata on the AST in this release.  Wiring `@inline` / `@hot` /
  `@profile` into the optimiser and `@link_name` / `@target` into
  the linker is recorded as follow-up work.
* **Tests.** `tests/unit/frontends/ploy/visibility_attrs_test.cpp`
  (13 cases / 41 assertions) all pass; full regression
  `test_frontend_ploy` 424 / 2585.
* **Sample.** `tests/samples/39_visibility_attrs/` is the canonical
  end-to-end demo with bilingual READMEs.
* **Docs.** Bilingual realization doc
  `docs/realization/visibility_attrs{,_zh}.md`, attribute catalog
  `docs/specs/attribute_catalog{,_zh}.md`, language spec keyword
  roster + "Visibility and Attributes" subsection, USER_GUIDE
  4.2.5e, tutorial 16.8 (en) / 16.10 (zh).
* **Future work.** Optimiser / linker integration for each built-in
  attribute; visibility / attribute prefix on `CLASS`, `CONST`,
  `TYPE`, `MAP_FUNC`, and module-scope `LET`; nested `MODULE name {
  ... }` blocks for finer-grained scopes; promote the `EXPORT`-
  without-`PUB` deprecation warning to a hard error in a future
  major release.

## v1.15.0 (2026-05-04)

**Generic `FUNC` and `STRUCT` declarations with bounded type
parameters and a type-erased MVP lowering path.**  Ploy gains
`<T: Bound, U>` parameter lists, `WHERE` clauses, and a built-in
trait registry; sema validates every bound name and brings each
parameter into scope while resolving signature/body types.

* **Surface syntax.** One new keyword is recognised by the lexer:
  `WHERE`.  The parser accepts `<T: Bound1 + Bound2, U>` after the
  declared name on `FUNC` and `STRUCT`, and an optional `WHERE T:
  Bound1 + Bound2, U: Bound3` between the return type and the body
  on `FUNC`.  Generic instantiations such as `Pair<i32, String>` are
  parsed as `ParameterizedType` in any type position.
* **AST.** `FuncDecl::TypeParam { name, bounds }` and
  `FuncDecl::type_params` / `StructDecl::type_params` carry the
  declared parameters.  WHERE clauses merge into the matching
  parameter's `bounds` during parsing.
* **Sema.** `PloySema::active_type_params_` is consulted by
  `ResolveType(SimpleType)` so a parameter name resolves to `Any`
  inside the declaration.  `PloySema::ValidateTypeParamBounds`
  validates every declared bound against the built-in registry
  (`Comparable`, `Hashable`, `Numeric`, `Iterable`, `Display`).
* **Lowering.** Generic declarations are lowered once with each
  parameter resolved to `Any` (type erasure); a single body services
  every call site.  Per-instantiation monomorphisation is recorded
  as follow-up work.
* **Tests.** `tests/unit/frontends/ploy/generics_test.cpp`
  (8 cases / 35 assertions) all pass; full regression
  `test_frontend_ploy` 411 / 2544.
* **Sample.** `tests/samples/38_generics/` is the canonical
  end-to-end demo with bilingual READMEs.
* **Docs.** Bilingual realization doc
  `docs/realization/generics{,_zh}.md`, language spec keyword
  roster + "Generics" subsection, USER_GUIDE 4.2.5d, tutorial
  16.7 (en) / 16.9 (zh).
* **Future work.** Per-instantiation monomorphisation; bound
  enforcement against concrete types at call sites; parametric
  generics in `LINK` declarations; generic methods on `CLASS`
  blocks; user-defined traits and higher-kinded bounds.

## v1.14.0 (2026-05-04)

**Cooperative async / await for Ploy and the cross-language
`Future<T>` runtime bridge.**  Ploy gains the `ASYNC` / `AWAIT`
constructs; the runtime exposes a cooperative event loop and a stable
C ABI that any host-language adapter can call to forward Python
`asyncio` coroutines, Rust `Future`, C++20 `std::coroutine`, Java
`CompletableFuture`, or .NET `Task<T>` into the unified `Future<T>`
handle.

* **Surface syntax.** Two new keywords are recognised by the lexer:
  `ASYNC` and `AWAIT`.  The parser accepts `ASYNC FUNC name(...) -> T
  { ... }` (top level, statement, class method) and `AWAIT <expr>` at
  unary precedence.  `ASYNC FUNC` carries `is_async = true` on the
  `FuncDecl` AST node and `AwaitExpression` is a dedicated AST node.
* **Sema.** AWAIT outside an `ASYNC FUNC` body is rejected with
  `kTypeMismatch`; the implicit return wraps `T` as `Future<T>` at
  the ABI boundary.
* **Lowering.** Each `ASYNC FUNC` body is bracketed by
  `__ploy_rt_async_enter` and `__ploy_rt_async_complete`; every
  `AWAIT <expr>` site lowers to `__ploy_rt_await(<handle>)`.
* **Runtime.** `runtime/services/async_bridge.{h,cpp}` and
  `runtime/services/event_loop.{h,cpp}` implement the cooperative
  scheduler.  C ABI: `__ploy_rt_async_enter`, `__ploy_rt_async_complete`,
  `__ploy_rt_async_spawn`, `__ploy_rt_await`, `__ploy_rt_future_resolve`,
  `__ploy_rt_async_run`, `__ploy_rt_async_pending`,
  `__ploy_rt_async_suspended`, `__ploy_rt_async_completed`,
  `__ploy_rt_async_active_frames`.  C++ surface in
  `polyglot::runtime::services`: `SpawnPloyTask`, `ResolveFuture`,
  `RunUntilIdle`, `SnapshotScheduler`, `ResetScheduler`.
* **Tooling.** `polyrt async` prints a snapshot of the cooperative
  event loop; `--json` emits the same payload as JSON, and
  `--run[=N]` drives the loop for at most `N` ticks before reporting.
* **Tests.** `tests/unit/frontends/ploy/async_await_test.cpp`
  (6 cases / 18 assertions) and
  `tests/unit/runtime/async_bridge_test.cpp`
  (5 cases / 14 assertions) all pass; full regression
  `test_frontend_ploy` 403 / 2509 and `test_runtime` 117 / 35257.
* **Samples.** `tests/samples/37_async_await/` is the canonical
  end-to-end demo; `tests/samples/14_async_pipeline/` is upgraded to
  use real `ASYNC FUNC` / `AWAIT` instead of placeholder shapes.
* **Docs.** Bilingual realization doc
  `docs/realization/async_model{,_zh}.md`, language spec keyword
  roster + "Async / Await" subsection, USER_GUIDE 4.2.5c, tutorial
  16.6 (en) / 16.8 (zh).
* **Future work.** True coroutine suspension via the IR-level
  `invoke` / `landingpad` machinery; multi-thread work-stealing pool
  layered on top of `runtime/threading.{h,cpp}`; cancellation
  propagation; cross-language reverse-path adapters
  (`pyloy_async_resolve`, `cppploy_async_resolve`, `jloy_async_resolve`,
  `clrloy_async_resolve`, `rsloy_async_resolve`); `Future<T>`
  parametric typing once generics (demand 2026-04-28-15) land.

## v1.13.0 (2026-05-04)

**Structured exception handling for Ploy and the cross-language
runtime error bridge.**  Ploy gains the `TRY` / `CATCH` / `FINALLY` /
`THROW` constructs and a built-in `Error` handle type; the runtime
exposes a stable C ABI that any host-language adapter can call to
forward Python `Exception`, C++ `std::exception`, Java `Throwable`,
.NET `Exception`, or Rust `Result::Err` into the unified `Error`.

* **Surface syntax.** Five new keywords are recognised by the lexer:
  `TRY`, `CATCH`, `FINALLY`, `THROW`, `ERROR`.  Sema validates that
  every `TRY` carries at least one `CATCH` clause or a `FINALLY`
  clause, that each `CATCH` declares a binding name, and that
  `THROW` carries a value.
* **Built-in `Error` handle.** The catch binding has the built-in
  handle type `Error` exposing `message: String`, `source_lang:
  String`, and `stacktrace: List<String>`.
* **Runtime bridge.** New `runtime/include/services/error_bridge.h`
  C ABI: `__ploy_rt_try_begin`, `__ploy_rt_try_end`, `__ploy_rt_throw`,
  `__ploy_rt_throw_from`, `__ploy_rt_current_error`,
  `__ploy_rt_current_error_message`,
  `__ploy_rt_current_error_source_lang`,
  `__ploy_rt_current_error_stacktrace_count`,
  `__ploy_rt_current_error_stacktrace_at`, `__ploy_rt_clear_error`.
  The data plane is thread-local; raising an Error throws a C++
  `RuntimeError` so any enclosing native frame can catch it.
* **Lowering.** TRY lowers to a runtime-call shape with explicit
  body / catch / finally / merge basic blocks dispatched on the
  return value of `__ploy_rt_try_begin`.  THROW lowers to a single
  call to `__ploy_rt_throw` followed by `unreachable`.
* **Tests.** `tests/unit/frontends/ploy/try_catch_throw_test.cpp`
  (8 cases / 30 assertions) covers parser + sema; `tests/unit/runtime/
  error_bridge_test.cpp` (5 cases / 19 assertions) covers the C ABI
  data plane via C++ `try` / `catch`.
* **Sample.** `tests/samples/36_try_catch/` demonstrates the syntax
  with bilingual READMEs.
* **Docs.** New realization note `docs/realization/error_handling.md`
  (and `_zh.md`); new "Error Handling" subsections in the language
  spec, `USER_GUIDE.md`, and the tutorial — both English and Chinese.
* **Future work.** Typed-catch dispatch, IR-level `invoke` /
  `landingpad` integration, postfix `?` short-circuit propagation,
  and full cross-language reverse-path interception of foreign
  exceptions are tracked as follow-up demands.

---

## v1.12.0 (2026-05-04)

**Stringified `CONFIG` form and registry-driven package-manager
dispatch.**  The `.ploy` `CONFIG` declaration now uses a uniform
`CONFIG <language> "<package_manager>" "<path_or_env>";` syntax that
covers every supported package manager without the need for a new
keyword per manager.

* **Canonical stringified form.** Both the package-manager name and
  the path are now ordinary string literals, e.g.
  `CONFIG python "venv" "/opt/envs/ml";`,
  `CONFIG rust "cargo" ".";`,
  `CONFIG javascript "npm" "./node_modules";`,
  `CONFIG java "maven" "./pom.xml";`,
  `CONFIG dotnet "nuget" "./packages";`,
  `CONFIG ruby "bundler" "./Gemfile";`,
  `CONFIG go "gomod" "./go.mod";`.

* **Registry.** A new central table at
  `frontends/ploy/src/sema/config_registry.cpp` owns every accepted
  `(language, package_manager)` pair.  Adding a new manager is a
  single-line table edit — the lexer and parser do not need to
  change.  See
  [Realization → Package Management → Registering a Custom Package
  Manager](realization/package_management.md#registering-a-custom-package-manager).

* **Legacy keyword compatibility.** The old keyword form
  (`CONFIG VENV / CONDA / UV / PIPENV / POETRY`) still parses for
  source compatibility; sema emits a `kDeprecatedKeyword` warning
  with a fix-it pointing at the canonical rewrite, and auto-translates
  the declaration to the new internal representation.

* **Diagnostics.** An unregistered `(language, package_manager)`
  pair (e.g. `CONFIG python "npm" …`) is now rejected at sema time
  with a diagnostic naming the offending pair.

* **Samples.** `tests/samples/04_package_import/` ships three new
  mirror entry files demonstrating the canonical form for the
  npm / cargo / maven managers; existing samples (`04`, `09`, `16`)
  have been migrated to the canonical syntax.

* **Tests.** A new `tests/unit/frontends/ploy/config_stringification_test.cpp`
  unit suite covers the registry, every documented
  `(language, package_manager)` pair, the legacy deprecation path,
  unknown-manager rejection, and parser-level malformed inputs.

* **Docs.** `docs/specs/language_spec*.md`, `docs/USER_GUIDE*.md`,
  and `docs/realization/package_management*.md` have all been
  updated to lead with the canonical form and document the
  registration recipe.

## v1.11.0 (2026-05-04)

**Named-parameter default values, EXTEND restriction, and a unified
`AS`-semantics chapter.**  This release tightens two long-standing
ambiguities in `.ploy` and adds the matching documentation /
samples.

* **Default parameter values.** `FUNC` declarations may now give
  trailing parameters a default expression introduced by `=`.
  Defaults must be either constant-foldable literal / unary / binary
  expressions or a pure intra-Ploy `FUNC` call; cross-language
  `CALL`, closure capture, and reads of other parameters are
  rejected with a precise diagnostic.  Lowering injects a copy of
  the default expression at every call site that omits the
  argument, so the back-end never observes a short call.

* **Named arguments at call sites.** Any argument may be supplied as
  `name: value`.  Positional arguments may not appear after a named
  argument; every formal may be supplied at most once; every
  required (no-default) parameter must be supplied either by
  position or by name.  Sema reports
  `"required parameter 'X' of 'F' is not supplied"` otherwise.

* **EXTEND restricted to dynamic hosts.** `EXTEND(<lang>, ...)` is
  now accepted only on `python`, `ruby`, and `javascript` (with
  the tag aliases `rb`, `js`, `typescript`, `ts`).  Static-language
  targets (`cpp`, `c`, `rust`, `java`, `dotnet` / `csharp`, `go` /
  `golang`) are rejected with a fix-it suggesting a local Ploy
  `FUNC` wrapper that uses `CALL` / `METHOD` instead.

* **Central `AS`-semantics chapter.** `docs/realization/ploy_language_spec.md`
  §4.17 (and its Chinese mirror) lists the five binding sites
  (`IMPORT … AS`, `EXPORT … AS`, `LINK … AS`, language-level
  `IMPORT … AS`, and `EXTEND … AS`) plus a set of anti-examples
  spelling out the forbidden / ambiguous shapes.

* **Samples.** New `tests/samples/34_default_args/` (default values
  + named call sites + pure-call defaults) and
  `tests/samples/35_extend_dynamic/` (accepted `EXTEND` on Python +
  rejection-and-migration narrative).  `tests/samples/08_delete_extend/`
  README gains a "Limitations" section pointing at the new rule.

* **Tests.** New `tests/unit/frontends/ploy/default_args_and_extend_test.cpp`
  (15 cases covering signature recording, positional / named /
  mixed call shapes, the parser ordering rule, the const /
  pure-call default rule, the required-argument coverage check, and
  the EXTEND rejection set).  The existing
  `devirtualization_test` "EXTEND on Rust base" case is rewritten
  to assert the new rejection diagnostic.  Full ploy frontend
  suite: 371 cases / 2348 assertions.

* **Docs.** `docs/USER_GUIDE.md` / `docs/USER_GUIDE_zh.md`,
  `docs/tutorial/ploy_language_tutorial.md` /
  `docs/tutorial/ploy_language_tutorial_zh.md` and
  `docs/realization/ploy_language_spec.md` /
  `docs/realization/ploy_language_spec_zh.md` updated for the new
  defaults / named-arg syntax, the EXTEND restriction, and the
  unified `AS` table.

## v1.10.0 (2026-05-04)

**Pattern matching for `MATCH`.** `.ploy` `MATCH` arms now accept
the full pattern grammar — wildcard, ranges, tuple / struct
destructuring, OR-patterns, bindings (`n @ ...`), type guards
(`x: i32 IF ...`) and `OPTION` constructors (`Some(x)` / `None`) —
backed by exhaustiveness checking, reachability warnings and a
two-strategy lowering pass.

* AST: existing `Pattern` hierarchy (`WildcardPattern`,
  `LiteralPattern`, `IdentifierPattern`, `RangePattern`,
  `TuplePattern`, `StructPattern`, `OrPattern`, `BindingPattern`,
  `ConstructorPattern`, `TypePattern`) is now exercised end to end
  by the parser, sema and lowering passes.
* Syntax: the arrow between the pattern (or guard) and the arm
  body is optional; both `->` and `=>` remain accepted.  The bare
  identifier `None` is parsed as a zero-argument
  `ConstructorPattern` so that `CASE None` participates in OPTION
  exhaustiveness.  `Name { ... }` after `CASE` is disambiguated
  via one-token lookahead so `CASE None { ... }` no longer eats
  the arm body.
* Sema: type compatibility, binding scoping, exhaustiveness on
  `bool` / `OPTION(T)` / arbitrary types, and reachability
  diagnostics for arms that follow an irrefutable arm or
  duplicate a previous literal.  Tuple / struct / type-guard
  patterns now correctly count as irrefutable when their
  components are irrefutable.
* Lowering: `LowerMatchStatement` chooses between a dense
  `ir::SwitchStatement` fast path (all-literal arms with no
  guards, wildcard allowed) and a structural `match.try.N` →
  `match.body.N` → `match.merge` cascade for every other shape.
* Tests: [`tests/unit/frontends/ploy/pattern_matching_test.cpp`](../tests/unit/frontends/ploy/pattern_matching_test.cpp)
  exercises every pattern shape end to end;
  [`tests/unit/frontends/ploy/pattern_matching_lowering_test.cpp`](../tests/unit/frontends/ploy/pattern_matching_lowering_test.cpp)
  locks in the dispatch decision and the basic-block shape.
* Sample: [`tests/samples/33_pattern_matching/`](../tests/samples/33_pattern_matching/)
  is a runnable end-to-end demo with a deterministic stdout
  marker.
* Docs: new [`docs/realization/pattern_matching.md`](realization/pattern_matching.md)
  / [`pattern_matching_zh.md`](realization/pattern_matching_zh.md);
  the MATCH section in the language spec, USER_GUIDE and tutorial
  is expanded in both languages.  Demand 2026-04-28-10 marked
  `--end -done`.

---

## v1.9.0 (2026-04-29)

**Statically-typed cross-language object handles.** `.ploy` programs can
now lift foreign objects into the static type system via
`HANDLE<lang::Class>` and per-class schema declarations, eliminating the
`Any`-typed black hole that used to engulf every `NEW`/`METHOD`/`GET`/`SET`
call site.

* New AST nodes: `ClassDecl`, `HandleType`, `ClassMethodSig`,
  `ClassAttrSig`.
* New core type: `core::Type::Class(name, language)`; two `kClass`
  values are equal only when both `name` and `language` match, so
  `HANDLE<a::T>` and `HANDLE<b::T>` are statically distinct and an
  implicit cross-language assignment is a hard error (use
  `CONVERT` + `MAP_FUNC` to bridge).
* New surface syntax (contextual keywords 鈥?existing identifiers
  named `class` / `handle` / `attr` keep lexing as identifiers):

    ```ploy
    CLASS python::torch::nn::Linear {
        METHOD __init__(in_features: i32, out_features: i32);
        METHOD forward(x: f32) -> f32;
        ATTR in_features: i32;
    }

    LET model: HANDLE<python::torch::nn::Linear> =
        NEW(python, torch::nn::Linear, 128, 10);
    ```

* `NEW` / `METHOD` / `GET` / `SET` against a typed handle are
  type-checked: argument count, argument types, and `SET`-value types
  are hard errors; unknown method or attribute names produce a warning
  (foreign objects routinely expose dynamic members) and fall back to
  the legacy `Any` path.
* Schemas register both qualified (`lang::Class::method`) and short
  (`Class::method`) names with the function-signature registry, so
  existing `LookupSignature()` callers see the typed entries
  transparently.
* `ClassDecl` is sema-only metadata: it carries no runtime presence,
  is excluded from `IsTopLevelExecutable`, and is no-op'd by
  `LowerStatement`.
* Backward compatibility: `NEW`/`METHOD`/... whose target has no
  registered schema continue to follow the 1.8.x dynamic-dispatch
  path; all 31 pre-existing samples build unchanged.
* Tests: 10 new Catch2 cases in
  `tests/unit/frontends/ploy/typed_handle_test.cpp` (schema
  registration, typed dispatch, arity / type errors,
  unknown-method warning, cross-language rejection, backward-compat
  fallback, lower-case `handle` identifier preservation). Full
  `test_frontend_ploy` suite: **2270 assertions in 344 test cases**.
* New sample: `tests/samples/32_typed_handles/` with bilingual
  READMEs, expected stdout and a deterministic PRINTLN marker.
* Documentation: new realisation note
  `docs/realization/cross_language_oop{,_zh}.md`; spec 搂4.15 added in
  both languages; demand 2026-04-28-9 marked `--end -done`.

---

## v1.9.1 (2026-04-29)

**Real-backend PRINTLN pipeline closure: `polyc 鈫?polyld 鈫?exe` now produces
a Win32 image whose stdout matches the source-level `PRINTLN` payload
byte-for-byte, removing the synthetic `BuildExitZeroPE` fallback for any
PRINTLN-bearing program.**

### Fixed

- **COFF on-disk relocation record stride.** The COFF loader was reading each
  relocation as `sizeof(struct{u32,u32,u16})` (12 bytes after natural
  alignment) instead of the on-disk stride of exactly 10 bytes, causing every
  reloc past the first to drift by 2 bytes and silently scramble symbol
  indices, types, and offsets.  All COFF reloc records are now read field by
  field through `std::memcpy` against a fixed 10-byte buffer.
- **COFF symbol table aux-record indexing.** Auxiliary symbol records were
  being skipped during parse but the resulting `obj.symbols` vector was
  *compressed* (no slot reserved for the aux entry), while the on-disk
  relocation records still cite *uncompressed* on-disk symbol indices.  The
  parser now pushes a placeholder `Symbol` (empty name, undefined) for every
  consumed aux record so on-disk indices line up with `obj.symbols` positions
  one-to-one, restoring correct relocation backpatching.
- **`CollectPolyrtPrintlnSequence` recovery for the new IR interner shape.**
  The recovery pass keyed exclusively off the legacy `println.msg<N>`
  hint prefix; the current `IRBuilder::MakeStringLiteral` path emits
  `str<N>` symbols too, which made the pass return an empty sequence and
  the linker fall through to `BuildExitZeroPE`.  The pass now recognises
  both prefixes, requires a digit after the prefix to keep the gate tight,
  and infers the message length from the next defined symbol's offset (or
  the section's NUL terminator) when the on-disk symbol size is zero.

### Added

- `tests/integration/printf_pipeline_e2e_test.cpp` 鈥?`[printf][pe7][integration]`
  drives a top-level two-`PRINTLN` `.ploy` source through `polyc 鈫?polyld`,
  runs the produced executable, captures stdout via `cmd /c >` (Win32) or
  `fork/pipe/dup2` (POSIX), and asserts the captured bytes equal
  `"alpha\r\nbeta\r\n"` byte-for-byte.
- `scripts/build_all_samples.{ps1,sh}` 鈥?new `--require-min-ok N` /
  `-RequireMinOk N` parameter.  After the per-sample loop, the harness
  exits non-zero when fewer than `N` samples land in the `OK` bucket,
  letting CI gate on a floor of working samples without inspecting the
  JSON report manually.

### Changed

- Root `CMakeLists.txt` `project(... VERSION 1.9.1 ...)` and `VERSION.txt`
  bumped from `1.8.0` to `1.9.1`.

---

## v1.8.0 (2026-04-29)

**Ploy syntax cleanup: a canonical / signed `LINK` form is now the
recommended cross-language linking syntax, and `STAGE` is promoted to
a reserved keyword that may only appear inside a `PIPELINE` body.**

### Added

- Canonical signed `LINK` form:

  ```ploy
  LINK <lang>::<module>::<func> AS FUNC(<param_types>) -> <return_type>;
  LINK <lang>::<module>::<func> AS FUNC(<param_types>) -> <return_type> {
      MAP_TYPE(<target>, <source>);
  }
  ```

  Parsed by a new `PloyParser::ParseSignedLinkDecl()` and dispatched from
  the existing `ParseLinkDecl()` based on whether `(` follows the `LINK`
  keyword (legacy form) or an identifier does (signed form).

- New `STAGE` keyword (canonical keyword count rises from 71 鈫?72).
  `ParseStatement` accepts `STAGE [name] CALL <lang>::<module>::<func>;`
  only when the parser is currently inside a `PIPELINE` body
  (`PloyParser::in_pipeline_context_`); it reports a parse diagnostic
  otherwise.

- `LinkDecl::is_legacy_form` boolean discriminator and a new
  `StageDecl{name, call_target, language}` AST node.

- Mirror sample [`tests/samples/01_basic_linking_v2/`](../tests/samples/01_basic_linking_v2/)
  rewriting the v1 cross-language linking sample with the signed form;
  the v1 README is annotated as *legacy form*.

- Two new unit tests under `tests/unit/frontends/ploy/`:
  `link_deprecation_test.cpp` (verifies the `kDeprecatedKeyword` warning
  is emitted) and `stage_misuse_test.cpp` (verifies `STAGE` outside a
  `PIPELINE` is rejected by the parser).

### Changed

- `PloySema::AnalyzeLinkDecl` now emits a `kDeprecatedKeyword` warning
  whenever it encounters a `LinkDecl` whose `is_legacy_form` flag is
  true.
- `docs/realization/ploy_language_spec.md` (and `_zh.md`) 搂4.2 now
  documents both forms with the signed form listed first as the
  recommendation.
- `docs/USER_GUIDE.md` (and `_zh.md`): top-of-file notice describing the
  deprecation; keyword quick-reference table updated to use the signed
  `LINK` example and to list the new `STAGE` keyword.
- `tests/samples/README.md` (and `_zh.md`): index now lists the v2
  mirror sample and tags the v1 sample as *legacy form*.

### Compatibility

- All 19 per-module test binaries remain green (1215 cases, ~89k
  assertions) on Windows / MSVC.  No source-level break for existing
  Ploy programs: legacy-form `LINK(...)` continues to compile, only
  with an additional warning diagnostic.

---

## v1.7.0 (2026-04-29)

**Ploy gains explicit-width primitive types, `TYPE` aliases, and
compile-time `CONST` declarations with semantic constant folding and
width-mismatch warnings.**

### Added

- 14 new Ploy keywords: `i8` / `i16` / `i32` / `i64`, `u8` / `u16` /
  `u32` / `u64`, `f32` / `f64`, `usize` / `isize`, plus `TYPE` and
  `CONST`.  All keywords participate in the existing case-insensitive
  lexer fold; the canonical keyword count rises from 56 to 71.
- Width-aware primitive resolution in `PloySema::ResolveType`, mapping
  the new keywords to `core::Type::Int(N, sign)` / `core::Type::Float(N)`
  factories that already shipped with the type system.
- `TYPE <name> = <type_expr>;` declarations register named aliases that
  participate in lookup alongside structs.  The reverse map
  `formatted_type 鈫?alias_name` lets diagnostics render messages such as
  `Pixel (alias of i32)`.
- `CONST <name>: <type> = <expr>;` declarations are folded by a new
  recursive evaluator covering literals, references to previously
  declared `CONST`s, unary `-` / `!` / `NOT`, and binary arithmetic,
  comparison, and logical operators (string concatenation via `+` is
  also folded).  Width mismatches between the declared type and the
  folded value emit a warning at the `kTypeMismatch` level.
- `INT` and `FLOAT` are now official aliases of `i64` and `f64`
  respectively; existing programs continue to compile unchanged.
- New sample `tests/samples/31_explicit_widths/` exercising `i32` /
  `u32` / `i64` cross-language linking with companion C++ kernel,
  bilingual `README.md` / `README_zh.md`, and the standard
  `expected_output.txt` regression artefact.
- New unit suites `type_alias_test.cpp`, `const_decl_test.cpp`,
  `width_mismatch_diag_test.cpp` (18 cases / 73 assertions) plus an
  integration test `explicit_widths_e2e_test.cpp` that drives the full
  lexer + parser + sema pipeline against an inline program.

### Changed

- `PloySema` now exposes `TypeAliases()`, `ConstantCount()`, and
  `LookupConstantText(name)` accessors so IDE / tooling layers can
  surface alias and constant tables without reflection.
- `PloyLowering` forwards `ConstDecl` through the immutable `VarDecl`
  path and treats `TypeAliasDecl` as a non-executable declaration in
  the synthetic-`main` classifier.
- `docs/specs/language_spec.md{,_zh}` and
  `docs/realization/ploy_language_spec.md{,_zh}` document the new
  keyword set, alias rules, constant-folding contract, and updated
  primitive type table.
- `tests/samples/README.md{,_zh}` index lists the new sample under the
  width-aware-numeric-types theme.

### Compatibility

- Backward compatible.  All pre-existing programs that used `INT` /
  `FLOAT` continue to compile and produce identical IR; the new
  surface keywords resolve through additional lookup paths only.

---

## v1.6.0 (2026-04-29)

**IDE gains a Performance Profiler and a Call Analyzer panel powered by a
shared `ProfileSession`, runtime call-trace + profile sink services and
`polyc`/`polyrt` emitters.**

### What's new

- Runtime services
  - `runtime/include/services/call_trace.h` 鈥?`CallTracer` singleton with
    `Enter` / `Exit` / `Drain` / `Peek` / `SerializeJson`, plus C-ABI
    hooks `__ploy_rt_call_enter` / `__ploy_rt_call_exit` / `_enable` /
    `_is_enabled`.  Disabled by default; relaxed-load short-circuit so
    LTO can dead-strip the calls.
  - `runtime/include/services/profile_sink.h` 鈥?`ProfileSink::Open(path,
    stream_mode)` with both document mode (`polyglot.profile.v1`) and
    NDJSON streaming mode.
- Middle-end
  - `InstrumentCallTrace` IR pass inserts the C-ABI hooks around every
    non-bridge function when requested.
- polyc CLI
  - `--emit=call-graph:<path>` writes a `polyglot.callgraph.v1` document.
  - `--emit=profile-symbols:<path>` writes the parallel id 鈫?qualified
    name map.
  - `--profile-instrument` enables the IR pass above.
- polyrt CLI
  - `polyrt profile [--json|--stream] --duration-ms ... --interval-ms ...`
    samples the running process via the call tracer.
  - `polyrt calltrace --json <path>` drains the current snapshot.
- IDE (`polyui`)
  - New **Profiler** dock panel 鈥?Flame / Hotspots / Timeline / Languages
    / Log tabs, driven by `ProfileSession`.  Toggle with `Ctrl+Alt+P`.
  - New **Call Analyzer** dock panel 鈥?callers/callees trees + layered
    DAG graph view + nodes table + language-pair filter + bounded DFS
    path search.  Toggle with `Ctrl+Alt+G`.
  - Both panels share a single `ProfileSession`, so loading a profile
    automatically overlays runtime call counts onto the Call Analyzer.
  - Three new shared data models: `FlameTreeModel`, `CallGraphModel`,
    `TimelineModel` (under `tools/ui/common/include/data_models/`).
- Tests
  - `tests/unit/runtime/call_trace_runtime_test.cpp` 鈥?6 cases covering
    enter/exit, nested timing, gating, JSON schema, drain semantics and
    profile sink doc/stream modes.
  - `tests/unit/tools/call_graph_model_test.cpp` 鈥?6 cases for the
    adjacency, DFS and runtime-overlay semantics.
  - `tests/unit/tools/profile_session_test.cpp` 鈥?3 cases for the JSON
    loaders.
  - `tests/unit/tools/profiler_panel_smoke_test.cpp` 鈥?3 cases that
    instantiate both panels and confirm session sharing.
  - `tests/integration/profiler_e2e_test.cpp` 鈥?2 cases that drive the
    real `polyc` binary against `09_mixed_pipeline` and `15_full_stack`.
- Documentation
  - `docs/realization/{profiler,call_analyzer}{,_zh}.md`
  - `docs/specs/{call_graph_schema,profile_stream_schema}{,_zh}.md`
  - `docs/api/profile_api{,_zh}.md`
  - `docs/tutorial/{profiling,call_analyzer}_quickstart{,_zh}.md`

### Compatibility

- Schemas: `polyglot.calltrace.v1`, `polyglot.profile.v1`,
  `polyglot.callgraph.v1`, `polyglot.profilesymbols.v1`.  Adding fields
  is non-breaking; bumping the suffix is reserved for layout changes.
- Shortcuts intentionally avoid `Ctrl+Shift+P/G` (which the IDE already
  binds to the command palette and the git panel).

---

## v1.5.8 (2026-04-29)

**Sample matrix grows to 30 themed programs and ships a regression harness
plus a Catch2 integration test.**

### What's new

- Every existing sample under `tests/samples/` (`01_basic_linking` through
  `16_config_and_venv`) gained:
  - A trailing `PRINTLN "<folder>: ok\r\n";` marker statement so each
    program emits a deterministic, byte-comparable line on stdout.
  - An `expected_output.txt` file containing exactly that line.
  - Bilingual `README.md` / `README_zh.md` describing the languages,
    keywords, build commands and expected runtime output.
- Fourteen brand-new themed samples were authored (`17_string_processing`,
  `18_numeric_kernels`, `19_file_io`, `20_json_pipeline`,
  `21_image_processing`, `22_database_access`, `23_http_client`,
  `24_concurrency`, `25_event_loop`, `26_state_machine`,
  `27_plugin_system`, `28_ml_inference`, `29_data_analytics`,
  `30_game_loop_demo`).  Each contains a `.ploy` entry, two host-language
  source files (real, compilable code 鈥?no placeholders), bilingual
  READMEs and `expected_output.txt`.
- `scripts/build_all_samples.ps1` and `scripts/build_all_samples.sh` walk
  every sample folder, run `polyc --emit-obj=鈥 then `polyld 鈥?-o 鈥,
  execute the binary, capture stdout, byte-compare it against the
  sample's `expected_output.txt`, and write `build/samples_report.json`.
  Per-sample status is one of
  `OK / OUTPUT_MISMATCH / EMPTY_STDOUT / RUN_FAIL / LINK_FAIL / COMPILE_FAIL / SKIP`.
  The harness exits 0 by default; pass `-FailOnMismatch` (PowerShell) or
  `--fail-on-mismatch` (bash) to switch to strict gating.
- `tests/integration/samples_regression_test.cpp` (Catch2 tag
  `[samples][b6][integration]`, registered in `integration_tests`) drives
  the harness and asserts the produced JSON report is well-formed:
  every sample is represented, every status string is one of the seven
  enum values above, and the recorded total matches the on-disk folder
  count.
- The root `tests/samples/README.md` (and Chinese twin) was rebuilt to
  list all 30 samples in a directory matrix, plus by-theme and
  by-language-combination indices.

### Notes

- The harness is intentionally tolerant by default because parts of the
  end-to-end backend pipeline are still maturing; the JSON report itself
  is the contract under test.  As more samples reach `OK` the strict mode
  becomes the natural release gate.
- Existing test suites (`test_linker`, `test_frontend_ploy`, `test_e2e`,
  `integration_tests`) are unaffected by this release.

---

## v1.5.7 (2026-04-29)

**`polyld` recovers `polyrt_println` call sites from input objects and
emits a multi-message Windows PE.**

### What's new

- New public free function
  `polyglot::linker::CollectPolyrtPrintlnSequence(const std::vector<ObjectFile> &)`
  performs a pure analysis pass over the linker's loaded object state and
  returns the source-order vector of decoded message payloads that the
  IR-layer `polyrt_println` lowering left as `(lea message-global,
  call polyrt_println)` reloc pairs inside every executable input section.
  The pass:
  - Walks each `ObjectFile` and every section flagged
    `SectionFlags::kExecInstr`.
  - Sorts each section's relocations by `offset` so loaders that surface
    relocs in file order rather than instruction order still produce a
    correct sequence.
  - Tracks the most-recent `println.msg<N>(.ptr)?` reloc as the pending
    message cursor and pairs it with the next `polyrt_println` reloc;
    the trailing `.ptr` GEP-alias suffix introduced by the IR string
    interner is stripped before symbol lookup.
  - Resolves the message global by linear-scanning every loaded
    `ObjectFile` (cross-translation-unit sharing works), reads
    `data[symbol.offset .. +symbol.size)` from its containing section,
    and appends the decoded bytes to the result.
  - Silently skips orphan calls (no preceding message reloc) and
    unresolvable symbols, leaving the rest of the recovered sequence
    intact.
  - Faithfully mirrors call-order semantics 鈥?duplicate payloads from
    an interned global are emitted at every call site; the
    `BuildPrintlnSequencePE` layer is the one that re-deduplicates the
    underlying `.rdata` storage.

- `polyglot::linker::Linker::GeneratePEExecutable` now invokes the new
  pass; when the recovered vector is non-empty it routes through
  `pe::BuildPrintlnSequencePE` to produce a real multi-line stdout
  binary, otherwise it preserves the legacy
  `pe::BuildExitZeroPE(user_text)` path bit-for-bit so non-PRINTLN
  programs are unaffected.

### Observable behaviour

A `.ploy` source as small as

```ploy
PRINTLN "alpha\r\n";
PRINTLN "beta\r\n";
PRINTLN "alpha\r\n";
```

now produces, after `polyc 鈥?&& polyld 鈥?-o demo.exe`, an executable
whose captured stdout is exactly

```
alpha
beta
alpha
```

with three real `WriteFile` calls into `STD_OUTPUT_HANDLE` followed by
`ExitProcess(0)`.  The integration test
`tests/integration/pe_runtime_smoke_test.cpp` (`[b5]` tag) builds the
ObjectFile shape `polyc` would emit, runs the recovered image on the
host loader, and pins both the exit code and the captured byte
sequence.

### New public APIs

- `polyglot::linker::CollectPolyrtPrintlnSequence`
  (`tools/polyld/include/linker.h`).

### Tests

- `tests/unit/linker/polyrt_println_collect_test.cpp` 鈥?10 Catch2 cases
  covering empty input, no-PRINTLN object, single call, multi-call
  ordering, interned-duplicate emission, `.ptr` GEP-alias resolution,
  cross-object data globals, unsorted-reloc auto-sort, defensive
  filtering of mis-flagged sections, and orphan-call skipping.
- `tests/integration/pe_runtime_smoke_test.cpp` 鈥?new `[b5]` end-to-end
  case asserts exit code 0 and exact stdout match for a three-call
  duplicate-bearing program after running it on the host Windows
  loader.

### Verified suites

`test_linker` (337 assertions / 68 cases), `test_frontend_ploy` (1907
assertions / 310 cases), `test_e2e` (171 assertions / 54 cases),
`integration_tests` (`[b5]` filter, 6 assertions / 1 case) 鈥?all
green.

---

## v1.5.6 (2026-04-29)

**`.ploy` -> `.obj` -> `.exe` -> live process exit code is now real.**

### What's new

- `polyc` now emits genuine AMD64 / ARM64 COFF object files when invoked
  with `--obj-format=coff` (previously the writer silently fell through
  to the ELF builder, which produced bytes the Microsoft linker
  rejected with `LNK1107: invalid or corrupt file`).  The new
  `polyglot::backends::COFFBuilder` lays down a complete `IMAGE_FILE_HEADER`,
  per-section headers (`.text`, `.rdata`, `.bss`, 鈥?, raw section
  contents, per-section relocations, a COFF symbol table (with
  short-name and long-name string-table forms) and a string table that
  MS `link.exe` accepts byte-for-byte.
- `polyld`'s `DetectObjectFormat` now identifies a raw COFF object via
  its `IMAGE_FILE_HEADER.Machine` field (`0x8664`, `0xAA64`, `0x014C`,
  `0x01C0`, `0x01C4`, `0x0200`).  The earlier MZ-stub heuristic
  recognised only fully-linked PE images and silently misclassified raw
  `.obj` files, leaving the loader to fall through to the POBJ branch
  and discard the section contents.
- `polyld`'s Win32 PE writer now resolves a user entry symbol
  (`_start` -> `__ploy_main` -> `main`, in priority order), translates
  it through the section-contribution map to its merged-`.text` offset,
  and produces an `.exe` whose `AddressOfEntryPoint` runs an 18-byte
  Win64-ABI shim that invokes the user `main` and forwards its `int`
  return value (low 32 bits of `RAX`) to `kernel32!ExitProcess`.  The
  previous build always pointed entry at the legacy
  `ExitProcess(0)` shim regardless of what the source program returned.

### Observable behaviour

A `.ploy` source as small as

```ploy
FUNC main() -> i32 { RETURN 42; }
```

now produces, after `polyc audit_hello.ploy --emit-obj=audit_hello.obj
--obj-format=coff && polyld audit_hello.obj -o audit_hello.exe`, an
executable whose Windows `GetExitCodeProcess` returns **42**.  The same
contract holds for `RETURN 0` and `RETURN 7`; the integration test
`tests/integration/ploy_e2e_real_exit_code_test.cpp` pins all three.

### New public APIs

- `polyglot::backends::COFFBuilder` 鈥?`ObjectFileBuilder` subclass that
  emits AMD64 (`is_arm64=false`) or ARM64 (`is_arm64=true`) raw COFF
  objects.
- `polyglot::linker::pe::BuildExeWithUserEntry(user_text_bytes,
  user_main_offset_in_text)` 鈥?wraps caller-supplied `.text` with a
  user-entry-then-`ExitProcess(eax)` shim.
- `polyglot::linker::pe::BuildUserMainExitShim(shim_rva, user_main_rva,
  exit_process_iat_rva)` 鈥?exposes the 18-byte AMD64 shim encoder for
  unit testing.

### Test coverage

- `tests/unit/linker/object_format_detect_test.cpp` (9 cases): pins
  `DetectObjectFormat`'s handling of raw COFF (all six recognised
  Machine values), MZ-stubbed PE images, ELF, Mach-O and POBJ magic.
- `tests/unit/backends/coff_builder_test.cpp` (4 cases): byte-level
  validation of AMD64 + ARM64 headers, external symbol layout in the
  COFF symbol table, and long-section-name encoding via the string
  table.
- `tests/unit/linker/section_merge_test.cpp` (2 cases): regression
  pinning that user `.text` bytes from a real COFF object survive the
  load -> resolve -> layout pipeline byte-for-byte and that
  multi-object `.text` inputs are concatenated with both blobs
  preserved.
- `tests/integration/ploy_e2e_real_exit_code_test.cpp` (3 cases): full
  `.ploy` -> `.obj` -> `.exe` -> spawn -> `GetExitCodeProcess`
  smoke for the literals 42, 0 and 7.

### Compatibility

- `BuildExitZeroPE` is still exported and is the fallback used when no
  user entry symbol is defined; existing callers that relied on the
  previous "always exits 0" behaviour are unaffected.
- The COFF detection change is a strict superset of the previous MZ
  rule, so any input that was previously recognised as a PE image still
  is.

---

## v1.5.5 (2026-04-29)

**`BuildPrintlnSequencePE` ships 鈥?Stage B4 of the runtime-stdout pipeline (demand `2026-04-28-49`).**

### What's new

- New public PE writer entry point `polyglot::linker::pe::BuildPrintlnSequencePE(const std::vector<std::string> &call_messages)`
  produces a runnable, self-contained PE32+ image that issues one
  `kernel32!WriteFile` call per entry of `call_messages` (in the supplied
  order) against `STD_OUTPUT_HANDLE`, then terminates the process via
  `kernel32!ExitProcess(0)`.  This is the first stage of the pipeline that
  emits **real AMD64 machine code driving Windows stdout** for the
  artefacts produced by the IR-layer `polyrt_println(i8*, i64)` calls
  shipped in v1.5.4.

### Win64 ABI shim contract

- The `.text` payload is laid out as `prologue (0x25 B) + N 脳 per-message
  block (0x1D B) + epilogue (0x09 B)`.  All offsets are byte-stable and
  unit-tested.
- Prologue: `sub rsp, 0x38` reserves Win64-mandated 32-byte shadow space
  + an 8-byte `lpOverlapped` slot (zeroed once) + an 8-byte
  `lpNumberOfBytesWritten` slot (zeroed once) + an 8-byte stdout-handle
  cache.  Then `GetStdHandle(-11)` is invoked once and the returned
  handle is parked in `[rsp+0x30]` so subsequent WriteFile calls can
  reload it without spilling additional callee-saved registers.
- Per-message block: `mov rcx, [rsp+0x30]; lea rdx, [rip+msg_i];
  mov r8d, len_i; lea r9, [rsp+0x28]; call qword ptr [rip+WriteFile]`.
  All RIP-relative displacements are computed from the block's own RVA
  so the shim is position-correct regardless of how many messages
  precede it.
- Epilogue: `xor ecx, ecx; call qword ptr [rip+ExitProcess]; int3`
  (the `int3` is unreachable but pads the shim to a deterministic length).

### Dedup & layout contract

- Identical message bytes share their `.rdata` storage (linear scan over
  the unique-payload table), exactly mirroring the IR-layer
  `IRBuilder::MakeStringLiteral` interning contract from v1.5.4.  A
  program that calls `PRINTLN "hello\n";` ten times pays for ten
  `WriteFile` blocks but only one `.rdata` payload.
- `call_messages.empty()` forwards to `BuildExitZeroPE({})`, producing a
  byte-identical image 鈥?a degenerate empty shim is never emitted.
- Each message size is bounded by `0xFFFFFFFFu` (WriteFile's `nNumberOfBytesToWrite`
  DWORD); oversized entries cause the call to return an empty `BuildResult`.
- The 3-section layout (`.text` + `.rdata` + `.idata`) and the
  `BuildPE32PlusImage` plumbing are reused verbatim from
  `BuildHelloWorldPE`, so all the section-header / IAT / import-descriptor
  invariants exercised by the existing v1.5.1 tests continue to hold.

### Tests added

- `tests/unit/linker/pe_writer_test.cpp` (+4 cases, +30 assertions) 鈥?
  empty-input forwarding, single-message structural shape, three-message
  unrolled `.text` size, and dedup `.rdata` shrinkage.
- `tests/integration/pe_runtime_smoke_test.cpp` (+1 case, +5 assertions) 鈥?
  spawns a 3-message PE with one duplicated payload, redirects stdout to
  a temp file, and byte-compares against the expected `"alpha\r\nbeta\r\nalpha\r\n"`
  concatenation.
- `tools/polyld/src/pe_writer_smoke.cpp` (+1 manual harness block) 鈥?
  end-to-end visual confirmation: the produced `pe_smoke_println.exe`
  prints all three lines and exits 0 on the host loader.

### Regression results

- `test_linker`: 56 cases / 284 assertions 鉁?(up from 52 cases in v1.5.4).
- `test_frontend_ploy`: 310 cases / 1907 assertions 鉁?(unchanged).
- `test_e2e`: 54 cases / 171 assertions 鉁?(unchanged).
- `integration_tests`: 130 cases / 552 assertions 鉁?(up from 129 in v1.5.4).

### What's next

- **B5**: teach polyld to extract `polyrt_println` callsites and their
  interned string payloads from object files and feed them as
  `call_messages` into `BuildPrintlnSequencePE`, replacing the dummy
  exit-zero shim it emits today.
- **B6**: end-to-end `.ploy 鈫?.obj 鈫?.exe` pipeline; extend the
  demand-04 expected_output harness to byte-compare actual stdout.

---

## v1.5.4 (2026-04-29)

**`PrintlnStmt` is now lowered to IR 鈥?Stage B3 of the runtime-stdout pipeline (demand `2026-04-28-49`).**

### Lowering contract

- `PRINTLN "literal";` lowers to two IR artefacts:
  1. An interned global of type `[N x i8]` holding the **decoded** bytes
     (the front-end deliberately preserves backslash escapes, so the
     decoding table 鈥?`\n`, `\r`, `\t`, `\\`, `\"`, `\0`, `\xHH` 鈥?lives
     here in the lowering layer, the single source of truth that the
     interpreter, IR text round-trip, and codegen all agree on).
  2. A direct `call void @polyrt_println(i8* msg, i64 len)` instruction
     emitted into the active basic block.  The callee is intentionally
     external; B5 will resolve it through polyld's runtime DLL import.
- The pointer + length convention (rather than NUL-termination) lets
  embedded `\0` bytes round-trip cleanly and lets the empty-literal case
  (`PRINTLN "";`) lower to a single `polyrt_println(ptr, 0)` call with
  no special-casing downstream.
- Identical literals share their interned global thanks to
  `IRBuilder::MakeStringLiteral` content-keyed deduplication, so
  `.rdata` stays compact across all object formats.
- Top-level `PRINTLN` lands in the auto-created `entry_fn` per the
  existing `IRContext::DefaultBlock` convention; `PRINTLN` inside a
  `FUNC` body lands in that function's basic block, which is the
  property B4 codegen depends on for stack-frame correctness.
- Unknown / malformed escape sequences (e.g. `\q`, `\xZZ`) raise a
  `kGenericWarning` diagnostic and the offending bytes are preserved
  verbatim 鈥?lowering never aborts on bad escapes so the rest of the
  module can still be inspected.

### Implementation

- `frontends/ploy/include/ploy_lowering.h` 鈥?new
  `LowerPrintlnStatement(const std::shared_ptr<PrintlnStmt> &)` declaration.
- `frontends/ploy/src/lowering/lowering.cpp`:
  - `LowerStatement` dispatcher recognises `PrintlnStmt`.
  - New `DecodePrintlnLiteral(...)` helper (anonymous namespace) owns the
    escape-decoding table; designed so future additions (`\u{鈥`,
    `\u00XX`, `\b`, `\f`) are a single `case` away.
  - `LowerPrintlnStatement` interns the decoded bytes via
    `builder_.MakeStringLiteral(...)` and emits the `polyrt_println` call.
- No header surface change beyond the one new method; binary-compat for
  existing front-end consumers preserved.

### Tests

- `tests/unit/frontends/ploy/println_lowering_test.cpp` (5 cases / 21
  assertions): IR-level decoding contract, empty-message corner, global
  deduplication, in-function basic-block placement, and the
  warning-but-no-abort path for unknown escapes.
- Full ploy frontend suite stays green: 310 cases / 1907 assertions.
- `test_linker` (43/245), `test_e2e` (54/171), `integration_tests`
  (129/547) all green 鈥?`polyrt_println` is currently an unresolved
  external in object files, but no existing test exercises an end-to-end
  link of a PRINTLN-bearing module yet (that wiring is B5).

### Demand tracking

- demand `2026-04-28-49` Stage B3 marked `[done]`.  Stage B4 (AMD64
  codegen for `polyrt_println` callsites + Win64 ABI shadow space) is
  the next milestone.

---

## v1.5.3 (2026-04-28)

**Ploy front-end gains the `PRINTLN "literal";` statement 鈥?Stage B2 of the runtime-stdout pipeline (demand `2026-04-28-49`).**

### Language

- New top-level / in-block statement: `PRINTLN STRING ';'` writes a single
  string literal to the host's standard output.  This is intentionally the
  smallest possible runtime-IO primitive; expressions, concatenation and
  formatting will land in later stages.  The trailing `';'` is mandatory.
- The literal's bytes are stored verbatim on the AST node 鈥?surrounding
  double-quotes are stripped, but backslash escapes (`\r`, `\n`, `\t`,
  `\\`, `\"`, 鈥? are *not* decoded by the front-end.  The single canonical
  decoder will live in the codegen stage (B4) so that interpreters,
  IR-text round-trips and the .rdata payload all agree on one
  interpretation.
- The `PRINTLN` keyword follows the same case-insensitive rule as every
  other Ploy keyword (`println`, `Println`, `PRINTLN` are equivalent),
  thanks to the lexer canonicalisation introduced in v1.5.2.

### Front-end plumbing

- `frontends/ploy/include/ploy_ast.h` 鈥?new `PrintlnStmt : Statement`
  struct carrying a single `std::string message` field.
- `frontends/ploy/src/lexer/lexer.cpp` 鈥?added `"PRINTLN"` to the
  canonical keyword set.
- `frontends/ploy/src/parser/parser.cpp` 鈥?`ParseTopLevel` and
  `ParseStatement` both dispatch the new keyword to the new
  `ParsePrintlnStatement()` helper, which emits a `PrintlnStmt` and
  reports a diagnostic (and resyncs) if the next token is not a string.
  `Sync()`'s recovery keyword set now includes `PRINTLN`.
- `frontends/ploy/src/sema/sema.cpp` 鈥?`AnalyzeStatement` accepts
  `PrintlnStmt` as a no-op; the parser already enforces the shape and
  empty messages are intentionally legal.

### Tests

- `tests/unit/frontends/ploy/println_stmt_test.cpp` (6 cases, 37 assertions):
  top-level parsing, escape-byte preservation, case-insensitive keyword,
  in-function dispatch through `ParseStatement`, error recovery on
  non-string operand, and sema acceptance of the empty-message corner case.
- Full ploy frontend suite stays green (305 cases / 1886 assertions).
- Linker (43/245), end-to-end (54/171) and integration (129/547) regression
  suites continue to pass with no behaviour changes 鈥?`PrintlnStmt` is
  ignored by the lowering stage until B3 wires it into IR.

### Compatibility

- `PRINTLN` was previously parsed as an identifier; any program using it
  as a variable / function / pipeline name must rename that identifier.
  The bundled samples and test fixtures do not use it.
- No public API removed; `PrintlnStmt` is purely additive.

### Demand tracking

- demand `2026-04-28-49` Stage B2 marked `[done]`.  Stage B3 (lowering to
  IR) is the next milestone.

---

## v1.5.2 (2026-04-28)

**Ploy lexer cleanup 鈥?keywords are now recognised case-insensitively, the legacy `RETURNS` clause is deprecated, and `AND` / `OR` / `NOT` are formal aliases of `&&` / `||` / `!`.**

### Lexical rules

- The Ploy lexer accepts every reserved word in any letter-casing
  (`link`, `Link`, `LINK`, `LiNk`).  The token's `Token::lexeme` is
  always normalised to the canonical UPPER-case spelling so the parser
  stays a single string-comparison without per-call folding.  The
  user's original spelling is preserved on a new `Token::raw_lexeme`
  field and surfaced through `Token::SourceText()` so diagnostics and
  source-faithful formatters keep printing what the user typed.
- Identifiers remain *case-sensitive*: only the keyword set is folded.
  This is a deliberate trade-off to keep the parser logic uniform and
  the diagnostics actionable.
- **Reserved-word collisions.** Identifiers that previously differed
  from a keyword only by case (`config`, `array`, `get`, `set`,
  `pipeline`, `new`, 鈥? are now reserved.  Migrate them to non-keyword
  names (for example `app_config`, `np_array`, `getter`).  The bundled
  unit / integration / benchmark test fixtures have been updated
  accordingly; user-facing samples under `tests/samples/` already use
  UPPER-case keywords and required no change.

### Operator aliases

- `AND` / `OR` / `NOT` keywords parse to *exactly* the same AST nodes
  as the symbolic `&&` / `||` / `!` operators (`BinaryExpression::op
  == "||"`, `UnaryExpression::op == "!"`).  No downstream stage has to
  branch on spelling.
- The symbolic forms are documented as the preferred style for new
  code; both forms remain permanent.

### `RETURNS` deprecation

- The legacy `LINK(...) RETURNS Type { ... }` syntax still parses and
  still populates `LinkDecl::return_type`, but the parser now emits a
  `kDeprecatedKeyword` warning (`ErrorCode = 3024`) that echoes the
  user's source spelling so the warning is greppable in their tree.
- New code should declare the return type via the canonical `-> Type`
  arrow on the LINK signature.  No automatic rewrite is performed; the
  warning is the migration signal.

### Compatibility

- Existing `tests/samples/01_basic_linking..16_*` programs continue to
  parse, semantically analyse and lower exactly as before 鈥?they all
  use UPPER-case keywords already.
- The shared `frontends/common::Token` struct gains a single new
  string field (`raw_lexeme`) at the *end* of the struct so every
  three- and four-argument aggregate-initialisation call site in the
  C++/Java/Python/Rust/.NET/JavaScript lexers compiles unchanged.
- The new `frontends::ErrorCode::kDeprecatedKeyword = 3024` slot sits
  between `kSignatureMissing = 3023` and `kGenericWarning = 3099` and
  is a non-fatal warning category.

### Tests

- `tests/unit/frontends/ploy/lexer_case_insensitive_test.cpp` 鈥?covers
  every one of the 56 keywords in lower / mixed / UPPER spellings,
  asserts identifiers stay case-sensitive, and verifies that words
  beginning with a keyword-prefix (`letter`, `iffy`, `linker`, 鈥? are
  not split.
- `tests/unit/frontends/ploy/keyword_alias_test.cpp` 鈥?proves that
  `AND/OR/NOT` and `&&/||/!` produce structurally identical ASTs and
  that the `RETURNS` clause emits exactly one `kDeprecatedKeyword`
  warning whose message echoes the source spelling for both UPPER and
  lower-case inputs.

---

## v1.5.1 (2026-04-28)

**Runtime-IO milestone for the PE32+ writer 鈥?produced `.exe` images can now write to standard output before exiting.**

The v1.5.0 PE writer could only emit a self-terminating image whose entry
shim called `kernel32!ExitProcess(0)`.  Real samples need to drive at
least one user-visible side effect before terminating, so this patch
extends the writer with the minimum surface required to perform a
synchronous `WriteFile` against the host's standard output handle.

What ships:

- **`.rdata` section support in `BuildPE32PlusImage`.**  `BuildRequest`
  gained an optional `rdata_bytes` field; `BuildResult` reports the
  resulting section's RVA back to the caller via `rdata_rva`.  When
  `rdata_bytes` is non-empty the writer lays out
  `[.text][.rdata][.idata]` (3 sections) instead of `[.text][.idata]`
  (2 sections).  `SizeOfInitializedData` accumulates both `.rdata` and
  `.idata` raw sizes per the PE/COFF spec.

- **New factory `BuildHelloWorldPE(message)`** in
  `tools/polyld/include/pe_writer.h`.  Produces a fully runnable PE32+
  whose entry executes the AMD64 sequence
  `GetStdHandle(STD_OUTPUT_HANDLE) 鈫?WriteFile(handle, msg, len, &n, NULL)
  鈫?ExitProcess(0)`.  The 68-byte entry shim is constant-size so
  `.rdata` and `.idata` RVAs can be fully resolved before code emission.
  Stack frame `sub rsp, 0x38` provides the 32-byte shadow space, the
  WriteFile ARG5 slot at `[rsp+0x20]`, and the bytes-written DWORD slot
  at `[rsp+0x28]` while preserving the Win64 ABI's 16-byte alignment
  requirement before each child `CALL`.

- **Three-import support is wire-tested.**  `BuildImportSection` already
  supported N functions per DLL; this release exercises that path with
  `kernel32.dll!{GetStdHandle, WriteFile, ExitProcess}` and validates
  every IAT slot RVA is reported back through `BuildResult::iat_slot_rva`.

- **`pe_smoke` harness extended** to also build, write, spawn, and
  validate a hello-world image alongside the existing minimal-exit one.

- **New unit tests** in `tests/unit/linker/pe_writer_test.cpp`:
  3-section layout assertion, IAT slot enumeration for all three kernel32
  imports, on-disk byte-for-byte preservation of the `.rdata` payload,
  shim prologue (`48 83 EC 38`) and trailing `int3`, and an empty-text
  rejection guard for `BuildPE32PlusImage`.

- **New integration test** in
  `tests/integration/pe_runtime_smoke_test.cpp`: builds a hello-world PE
  in-process, writes it to a temp path, spawns it under `cmd /c` with
  stdout redirected to a temp `.out` file, and asserts both the exit code
  and the captured bytes equal the injected message.  Required an extra
  outer pair of quotes around the redirected command 鈥?`std::system`
  forwards to `cmd /c <string>`, which strips one layer of quotes before
  re-tokenising.

Validated regression: `test_linker` 43/43 (245 assertions),
`integration_tests` 129/129 (547 assertions), `test_e2e` 54/54 (171
assertions).  No public API of `Linker` changed; the new pe_writer surface
is purely additive.

Known not-yet-shipped: the `polyc 鈫?polyld` pipeline still passes an
empty `.text` to `BuildExitZeroPE` for `.ploy` inputs, so `polyc
hello.ploy -o hello.exe` produces an exit-zero image rather than a
hello-world image.  Connecting `BuildHelloWorldPE` (and a generalised
runtime-IO emitter) to the front end is the next milestone in the
multi-stage runtime-stdout roadmap.

---

## v1.5.0 (2026-04-28)

**Native Windows PE32+ executable emission 鈥?`polyc tiny.ploy -o tiny.exe` now produces a runnable `.exe`**

Before this release, the bundled `polyld` always emitted ELF executables,
even when the requested output had a `.exe` suffix and even when running
on a Windows host.  The result was a 423-byte ELF file masquerading as
`tiny.exe` that the Windows loader refused to execute (`'tiny.exe' is not
a valid Win32 application`).  Users who wanted a runnable `.exe` had to
install MSVC `link.exe` or LLVM `lld-link` and rely on the probe-then-
invoke fallback shipped in v1.4.1; on hosts without a system linker
there was no path forward at all.

This release ships an in-tree PE32+ image writer so the bundled `polyld`
can produce a real Windows AMD64 console executable end-to-end:

- New module `tools/polyld/{include,src}/pe_writer.{h,cpp}` lays out a
  fully valid PE32+ image: 64-byte DOS header with a conventional stub,
  `PE\0\0` signature, COFF File Header (Machine = `IMAGE_FILE_MACHINE_AMD64`,
  `Characteristics` = `RELOCS_STRIPPED | EXECUTABLE_IMAGE | LARGE_ADDRESS_AWARE`),
  240-byte PE32+ Optional Header (Magic = `0x020B`, Subsystem = Windows
  console, `DllCharacteristics` = `NX_COMPAT | TERMINAL_SERVER_AWARE`,
  16 data directories), section table, padded headers, `.text` (RX) and
  `.idata` (RW) sections.  The `.idata` layout includes the
  `IMAGE_IMPORT_DESCRIPTOR` array, ILT, IAT, `IMAGE_IMPORT_BY_NAME`
  records and the DLL name strings, with the data directory entries `[1]`
  (Import) and `[12]` (IAT) populated and pointing at the right RVAs.
- New entry shim `BuildExitProcessShim()` emits the canonical 13-byte
  AMD64 sequence
  `48 83 EC 28  31 C9  FF 15 disp32  CC`
  (`sub rsp, 0x28; xor ecx, ecx; call qword ptr [rip+disp32]; int3`).
  The `sub rsp, 0x28` reserves the 32-byte shadow space and re-aligns
  the stack to 16 bytes that the Windows x64 ABI requires before any
  call into an imported function.
- New helper `BuildExitZeroPE(user_text_bytes)` wraps a caller-supplied
  `.text` byte buffer with the entry shim appended at the end.  The
  produced image still embeds the user code on disk (so future linker
  iterations can re-target `AddressOfEntryPoint` at a real `main` once
  Win32 ABI translation is in place) but the entry currently runs the
  shim, guaranteeing the image terminates cleanly via
  `kernel32!ExitProcess(0)`.
- `OutputFormat::kPEExecutable` is added to the linker enum, with a
  new `Linker::GeneratePEExecutable()` that pulls the merged `.text`
  output section bytes through `BuildExitZeroPE` and writes the result
  to `config_.output_file`.
- `polyld` auto-selects `kPEExecutable` whenever the `-o` argument ends
  in `.exe` (case-insensitive); on Windows hosts it also defaults to
  PE even without the suffix.  Two new explicit flags are accepted:
  `--pe` / `--output-format=pe` and `--elf` / `--output-format=elf`.

Quality gates exercised before merge:

- New Catch2 unit suite `tests/unit/linker/pe_writer_test.cpp` (4 cases,
  47 assertions) validates MZ/PE magic, e_lfanew, COFF Characteristics,
  Optional Header Magic, AddressOfEntryPoint, Subsystem,
  `DllCharacteristics`, the 16-entry data directory, the populated
  Import + IAT directories, the 13-byte shim shape, the round-tripping
  of user `.text` bytes through `BuildExitZeroPE`, and the equivalence
  of `BuildExitZeroPE({})` to `BuildMinimalExitZeroImage()`.
- New Windows-only integration suite
  `tests/integration/pe_runtime_smoke_test.cpp` (2 cases, 8 assertions)
  builds the PE in-process, writes it to a temp file, spawns it via
  `std::system`, and asserts the exit code is 0 鈥?both for the minimal
  image and for an image wrapping 256 bytes of arbitrary user code.
- End-to-end repro: `polyc tests/samples/01_basic_linking/basic_linking.ploy
  -o tests/samples/01_basic_linking/test_bin.exe` now produces a 1536-byte
  PE32+ (`MZ` magic, `dumpbin /imports` shows
  `kernel32.dll: ExitProcess` resolved cleanly) that exits with code 0.
- Existing regressions kept green: `test_linker` 39/39, `test_e2e` 54/54.

Known limitations of this iteration (tracked for a future release):

- No base relocation table is emitted.  `IMAGE_FILE_RELOCS_STRIPPED` is
  set so the loader honours the preferred `ImageBase` (`0x140000000`).
- No exports, resources, TLS or debug directories.
- `AddressOfEntryPoint` always points at the appended shim; the user's
  ELF-style `.text` bytes are embedded but not invoked.  Real `main`
  re-targeting will land together with the COFF object loader and the
  Win32 ABI translation layer.

## v1.4.1 (2026-04-28)

**Probe-then-invoke linker selection 鈥?silence raw shell noise**

Previously, the staged compilation pipeline and the legacy single-pass
driver in `polyc` both unconditionally invoked `link.exe` and then
`lld-link` via `std::system()`.  When neither was on `PATH` (the typical
case for users running `polyc` outside an MSVC "Developer Command
Prompt"), Windows CMD itself printed
`'link' is not recognized as an internal or external command` (or the
localized equivalent, e.g. `'link' 涓嶆槸鍐呴儴鎴栧閮ㄥ懡浠) to `stderr`,
leaking raw shell noise into `polyc` output even though the pipeline
reported success and the `.obj` was produced correctly.

- 鉁?New shared module `tools/polyc/include/linker_probe.h` +
  `tools/polyc/src/linker_probe.cpp` exporting
  `polyglot::tools::linker_probe::{IsExecutableOnPath, ShellQuote,
  LinkerChoice, SelectAvailableLinker, ExpandLinkCommand}`.  The probe
  uses `where` (Windows) / `command -v` (POSIX) with both `stdout` and
  `stderr` redirected to the platform null sink, so the probe itself
  never produces visible output.
- 鉁?Per-format selection priority lists:
  - `pobj`  鈫?bundled `polyld` (canonical consumer; no native fallback);
  - `coff`  鈫?MSVC `link` 鈫?LLVM `lld-link` 鈫?bundled `polyld`;
  - `macho` 鈫?`clang` 鈫?`ld` 鈫?bundled `polyld`;
  - `elf`   鈫?`clang` 鈫?`gcc` 鈫?`ld` 鈫?bundled `polyld`.
  The bundled `polyld` is the universal fallback because its loader
  (`tools/polyld/src/linker.cpp`) detects COFF / ELF / Mach-O input by
  magic bytes and produces a matching native executable, so a `polyc`
  invocation can always link successfully on a host that ships only the
  Polyglot toolchain itself.
- 鉁?Both call sites refactored to share the new module:
  `tools/polyc/src/compilation_pipeline.cpp` (staged pipeline,
  `mode == "link"` branch) and `tools/polyc/src/stage_packaging.cpp`
  (legacy single-pass driver, `emit_and_link` lambda).
- 鉁?Verbose mode now logs the actually-selected linker:
  `[polyc] Invoking polyld (fallback) -> a.out` (was hard-coded
  `Invoking link.exe` even when `link.exe` was never invoked).
- 鉁?`ShellQuote` handles paths with embedded spaces using
  platform-appropriate quoting (Windows: `"鈥?`; POSIX: `'鈥?` with
  embedded-quote escaping `'\''`).  Space-free paths are passed through
  untouched.
- 鉁?New unit test file `tests/unit/tools/linker_probe_test.cpp`
  (7 cases, 23 assertions): `ShellQuote` quoting rules; rejection of
  empty / bogus executable names; acceptance of an existing absolute
  path via the stat-check fast path; empty `LinkerChoice` for `pobj`
  when `polyld` is bogus; non-empty `LinkerChoice` for `pobj` when
  `polyld` is reachable; `ExpandLinkCommand` placeholder substitution
  and polyld-only flag gating.
- 鉁?Reproducer `polyc.exe tests/samples/03_pipeline/pipeline.ploy`
  now exits 0 with zero shell-noise lines on a host without
  `link.exe` / `lld-link.exe` on `PATH`; the link stage transparently
  falls back to bundled `polyld`.

> No source / ABI break: the project version stays at **1.4.0** in the
> root `CMakeLists.txt`; this is a bug-fix patch surfaced under the
> v1.4.1 changelog narrative because the user-visible behaviour has
> changed (the noise lines are gone).

---

## v1.4.0 (2026-04-28)

**Debug emitter normalization & umbrella close-out**

- 鉁?All twelve `// Placeholder` / `// (placeholder)` / `// Simplified`
  comments inside `backends/common/src/debug_emitter.cpp` and
  `backends/common/src/dwarf_builder.cpp` rewritten to spell out the actual
  DWARF / System V ABI / ELF gABI contract 鈥?every "reserved length, patched
  at section close" slot now references the matching `Patch32` call site.
- 鉁?`DebugLineInfo` gained a fourth field `std::uint64_t address{0}`
  (default-initialized; all existing aggregate initializers continue to
  compile and behave identically).
- 鉁?`DwarfSectionBuilder::EncodeLineStatements` now emits explicit
  `DW_LNS_advance_pc ULEB128(delta)` opcodes per row, honouring
  `DebugLineInfo::address` and falling back to a one-unit step per row when
  callers omit the field 鈥?the line program is therefore strictly monotonic
  and individually addressable.
- 鉁?`kDwLnsAdvancePc` standard opcode promoted from `[[maybe_unused]]` to
  active emission.
- 鉁?New unit test file `tests/unit/backends/debug_emitter_normalization_test.cpp`
  (4 cases): `.debug_info unit_length` patch round-trip; `.debug_line`
  `unit_length` + `header_length` patch round-trip; `DW_LNS_advance_pc`
  presence after monotonic-PC input; PDB GUID RFC 4122 v4 / variant 1 bit
  compliance and entropy.
- 鉁?Bilingual docs `docs/realization/debug_emitter_normalization.md` /
  `_zh.md`; new `api_reference` 搂7.14 (English) / 搂7.13 (Chinese).
- 鉁?`test_backends`: 433 assertions / 62 cases 鈫?**461 / 66**; other four
  suites unchanged (`test_core` 357/41, `test_middle` 292/80, `test_runtime`
  35199/101, `test_linker` 171/35).
- 鉁?Closes the four-week backend rewrite series that began with v1.3.3 (the
  RISC-V backend is deferred to a future minor release).

## v1.3.7 (2026-04-28)

**`ITargetBackend::EmitBitcode` enabled by default**

- 鉁?Default `ITargetBackend::EmitBitcode` no longer reports an "unsupported"
  diagnostic.  It now serializes the input `IRContext` into the project's
  native polyglot bitcode (the same UTF-8 stream that `LTOModule` already
  uses) and writes the bytes into `TargetArtifacts::bitcode_bytes`.
- 鉁?New `LTOModule` API: `SerializeBitcode()`, `DeserializeBitcode(string_view)`,
  and `static FromIRContext(ctx, name)`.  The legacy `SaveBitcode` /
  `LoadBitcode` overloads become thin wrappers that call the new pair 鈥?
  byte-for-byte identical output, no public-signature break.
- 鉁?Three target adapters (`x86_target_backend.cpp`, `arm64_target_backend.cpp`,
  `wasm_target_backend.cpp`) flip `Capabilities().emits_bitcode` from `false`
  to `true`; they keep the default implementation, no override added.
- 鉁?New unit test `tests/unit/backends/emit_bitcode_roundtrip_test.cpp`
  (3 cases): empty `IRContext` round-trip; three-backend payload byte-equality;
  block + instruction + operand + entry-points topology preservation.
- 鉁?`target_backend_registry_test.cpp` flipped: the prior "unsupported
  diagnostic" assertion is replaced by a 2-function `IRContext` round-trip
  through `EmitBitcode` + `DeserializeBitcode`.
- 鉁?Bilingual docs `docs/realization/bitcode_emission.md` / `_zh.md`;
  `api_reference` 搂7.13 (English) / 搂7.12 (Chinese).
- 鉁?`tools/polyld` continues to consume LLVM bitcode (`BC\xC0\xDE` magic) and
  polyglot bitcode (`m` prefix) without ambiguity.

## v1.3.6 (2026-04-28)

**WASM backend translation-unit split**

- 鉁?The 1500+-line monolithic `backends/wasm/src/wasm_target.cpp` is split
  into purpose-named TUs along the module / type / instruction / runtime
  boundaries used by the emitter (each new TU stays under the team's per-file
  size guideline and links into the existing `backend_wasm` target with no
  CMake target-graph reshuffling).
- 鉁?Public entry point `WasmTargetBackend::Compile` and the binary / WAT
  output bytes are byte-identical before and after the split (verified by
  the existing WASM e2e cases).
- 鉁?New `tests/unit/backends/wasm_split_smoke_test.cpp` smoke-tests that the
  per-TU helpers stay reachable from the emitter fa莽ade and that the public
  `Capabilities()` matrix is unchanged.

## v1.3.5 (2026-04-28)

**ABI / relocation model rewrite**

- 鉁?Single `RelocationKind` enum + `ABIDescriptor` struct shared by x86_64,
  ARM64, and WASM emitters; per-backend ad-hoc reloc enums removed.
- 鉁?`tests/unit/backends/abi_relocation_test.cpp` and `abi_calling_convention_test.cpp`
  cover GOT / PLT / PC-relative / absolute / thread-local relocations across
  the three platforms with the same parametrized matrix.
- 鉁?ELF / Mach-O / COFF emitters consume the unified descriptor; cross-format
  divergence is contained inside the per-format writers.

## v1.3.4 (2026-04-28)

**MachineIR verifier**

- 鉁?New `MachineIRVerifier` library validates: prefix-register usage, stack
  frame closure (entry / exit balance), cross-basic-block live-range
  consistency, and operand-kind compatibility against each backend's
  `MachineInstrTemplate`.
- 鉁?`tests/unit/backends/machine_ir_verifier_test.cpp` and
  `machine_ir_template_test.cpp` exercise the verifier on hand-rolled
  malformed cases plus output from the production isel passes.
- 鉁?Verifier runs as an opt-in pass behind a per-backend flag so existing
  CI configurations are unaffected; future backends can flip the flag to
  fail-fast on emission bugs.

## v1.3.3 (2026-04-28)

**Backend registry 鈥?`ITargetBackend` + `BackendRegistry`**

- 鉁?New abstract base `ITargetBackend` formalises the
  `Capabilities()` / `Compile()` / `EmitAssembly()` / `EmitObject()` /
  `EmitBitcode()` contract; replaces the implicit duck-typed dispatch in the
  driver.
- 鉁?`BackendRegistry` discovers and orders backends by triple, alias, and
  priority; CLI `--target=` resolution now goes through the registry, so
  `--target=x86-64` and friends remain valid aliases without ad-hoc strncmp
  in the driver.
- 鉁?Three adapters (`x86_target_backend.cpp`, `arm64_target_backend.cpp`,
  `wasm_target_backend.cpp`) wrap the existing emitters; the driver depends
  only on `ITargetBackend`.
- 鉁?`tests/unit/backends/target_backend_registry_test.cpp` covers
  registration, alias resolution, priority ordering, capability inspection,
  and the default `EmitBitcode` diagnostic (later flipped in v1.3.7 once the
  default implementation became real).

---

## v1.0.6 (2026-03-19)

**CI Quality Gates (umbrella series 03-17-8)**
- 鉁?New `.clang-tidy` configuration: bugprone, cppcoreguidelines, modernize, performance, readability checks with project-specific exclusions
- 鉁?New CMake options: `POLYGLOT_ENABLE_ASAN`, `POLYGLOT_ENABLE_UBSAN`, `POLYGLOT_ENABLE_COVERAGE` for sanitizer and coverage builds
- 鉁?CI `format-check` job: enforces `.clang-format` style on all C/C++ source files via `clang-format-17`
- 鉁?CI `clang-tidy` job: static analysis with `clang-tidy-17` on all project `.cpp` sources (no 50-file cap)
- 鉁?CI `sanitizers` job: ASan + UBSan run on non-benchmark suites (`ctest -LE benchmark`) for stable signal
- 鉁?CI `coverage` job: collects line coverage via lcov/gcov from non-benchmark suites and uploads filtered report
- 鉁?CI `benchmark-smoke` job: runs benchmarks in fast mode to catch performance regressions
- 鉁?CI concurrency control: cancels in-progress runs for the same branch/PR
- 鉁?Platform builds now depend on `format-check` gate passing first
- 鉁?Documentation updated: README, USER_GUIDE (EN/ZH) with quality gate tables and local usage instructions

**Cross-Language Linking Closure**
- 鉁?`polyc` now automatically synthesizes `CrossLangSymbol` entries from LINK declarations and CALL descriptors before invoking `ResolveLinks()`, closing the gap where the linker had descriptors but no symbol table to resolve against
- 鉁?Parameter descriptors are populated from sema's known function signatures when available
- 鉁?Compilation model documentation updated to reflect the automated symbol registration flow

**Tighten Degradation Paths**
- 鉁?`lowering.cpp`: LINK stub with no signature info now emits an error in strict mode (default) instead of silently falling back to a single opaque i64 argument
- 鉁?`driver.cpp`: minimal main synthesis is now blocked in strict mode; only allowed with `--force` in permissive mode, with clear DEGRADED BUILD warnings
- 鉁?`driver.cpp`: post-optimization IR verification failure is now a hard error in strict mode; previously it was silently swallowed (only logged in verbose mode)
- 鉁?Backend empty-section stub injection already gated behind `--force` + non-strict; added consistent DEGRADED BUILD messaging

**Pipeline Refactoring**
- 鉁?Extracted `PassManager` from internal `.cpp` class to public header `middle/include/passes/pass_manager.h`
- 鉁?`PassManager` supports O0/O1/O2/O3 levels with named `PassEntry` stages, custom pass injection, and verbose per-function logging
- 鉁?`driver.cpp` optimization pipeline (~40 lines of inline pass calls) replaced with `PassManager::Build()` + `RunOnModule()` 鈥?a single 5-line invocation
- 鉁?Pass pipeline is now inspectable, extensible, and reusable by CLI, UI, tests, and plugins

**Test Decoupling**
- 鉁?Split monolithic `unit_tests` into 14 per-module test binaries (`test_core`, `test_plugins`, `test_frontend_python`, `test_frontend_cpp`, `test_frontend_rust`, `test_frontend_ploy`, `test_frontend_java`, `test_frontend_dotnet`, `test_frontend_common`, `test_middle`, `test_backends`, `test_runtime`, `test_linker`, `test_e2e`)
- 鉁?Each module binary links only the libraries it actually needs, reducing link overhead and isolating dylib failures
- 鉁?CTest now has 19 test entries (14 per-module + combined `unit_tests` + `integration_tests` + 3 benchmark targets) with labels (`unit`, `frontend`, `middle`, `backend`, `runtime`, `linker`, `e2e`, `benchmark`)
- 鉁?Combined `unit_tests` target preserved for backward compatibility
- 鉁?Top-level `enable_testing()` added to `CMakeLists.txt` so per-module tests are discoverable from the build root

**Module Boundary Fixes**
- 鉁?Extracted `backends/common/` sources (debug_info, debug_emitter, dwarf_builder, object_file) from `polyglot_common` into a new `backend_common` library
- 鉁?`backend_x86_64`, `backend_arm64`, `backend_wasm` now depend on `backend_common` instead of backend sources compiled into `polyglot_common`
- 鉁?Eliminated the common 鈫?backends layer inversion (correct direction: backends 鈫?common)
- 鉁?`polyld` CLI consolidation: merged the 170-line duplicate `main` from `linker.cpp` (with `--ploy-desc`, `--aux-dir`, `--allow-adhoc-link` options) into `main.cpp`, removed the duplicate entry point from `linker.cpp`

## v1.0.5 (2026-03-17)

**Documentation Single-Sourcing & Auto-Verification**
- 鉁?Fixed path references: README.md, USER_GUIDE.md, USER_GUIDE_zh.md, `setup_qt.sh`, `setup_qt.ps1` all now reference correct `tools/ui/setup_qt.*` paths (was `scripts/setup_qt.*`)
- 鉁?Fixed dead links: `plugin_specification.md` / `plugin_specification_zh.md` links in USER_GUIDE now resolve correctly
- 鉁?Fixed `language_spec.md` / `language_spec_zh.md`: runtime bridge header paths updated to `runtime/include/libs/*.h`
- 鉁?Fixed `project_tutorial.md` / `project_tutorial_zh.md`: driver path updated to `tools/polyc/src/driver.cpp`
- 鉁?New `scripts/docs_lint.py`: comprehensive documentation linter with 9 check categories:
  - DL001: Path reference validation (backtick-quoted paths must exist in repo)
  - DL002/DL003: Bilingual pairing 鈥?every `*.md` must have `*_zh.md` counterpart
  - DL004: Heading structure sync 鈥?EN/ZH heading counts per level must match
  - DL005: Dead link detection 鈥?relative Markdown links must resolve
  - DL006: Version consistency 鈥?version strings across docs must agree
  - DL007: Orphan detection 鈥?docs not referenced from README or USER_GUIDE
  - DL008: TODO/FIXME markers in published docs
  - DL009: Placeholder text detection
- 鉁?New `scripts/docs_generate.py`: single-source documentation generator with template markers (`<!-- BEGIN:section_name -->` / `<!-- END:section_name -->`); supports `--apply` / `--check` modes; patches test badges, Qt paths, dependency tables, version footers from `docs/_variables.json`
- 鉁?New `scripts/docs_sync_check.py`: bilingual synchronisation checker comparing heading structures and content length divergence between EN/ZH pairs
- 鉁?New `scripts/check_include_deps.py`: include dependency layer constraint enforcer (middle鈫抍ommon/ir, backends鈫抐rontends, frontends鈫抌ackends)
- 鉁?New `docs/_variables.json`: single source of truth for version numbers, test counts, paths, dependency info, tool names 鈥?eliminates manual multi-file updates
- 鉁?Template markers inserted in README.md (`test_badge`, `qt_setup_en`, `dependencies_table`, `version_footer_en`) and USER_GUIDE footers (`version_footer_en`, `version_footer_zh`)
- 鉁?CI integration: added `docs-lint` job to `.github/workflows/ci.yml` running `docs_lint.py --ci`, `docs_sync_check.py --ci`, and `docs_generate.py --check` on every push/PR
- 鉁?Updated test badge from 813 鈫?808 (accurate count); version footer updated to v1.0.4 / 2026-03-17
- 鉁?All 808 unit tests passing (34085 assertions); 51/52 integration tests passing

## v1.0.4 (2026-03-17)

**Plugin System & UI Extensibility**
- 鉁?Plugin host callbacks fully implemented: `emit_diagnostic` forwards diagnostics to IDE output panel and fires `DIAGNOSTIC` event to subscribers; `open_file` delegates to registered `OpenFileCallback` (wired to IDE tab manager); `register_file_type` populates central file-type registry
- 鉁?Event subscription system: plugins can subscribe to 8 event types (`FILE_OPENED`, `FILE_SAVED`, `FILE_CLOSED`, `BUILD_STARTED`, `BUILD_FINISHED`, `DIAGNOSTIC`, `WORKSPACE_CHANGED`, `THEME_CHANGED`) via `subscribe_event` / `unsubscribe_event` host services
- 鉁?`PluginManager::FireEvent()`: thread-safe dispatch 鈥?snapshots subscribers under lock, delivers events outside lock to avoid re-entry deadlocks
- 鉁?Convenience fire helpers: `FireFileOpened()`, `FireFileSaved()`, `FireBuildStarted()`, `FireBuildFinished()`, `FireWorkspaceChanged()`, `FireThemeChanged()`
- 鉁?File type registry: `RegisterFileType()` / `GetLanguageForExtension()` / `GetRegisteredFileTypes()` 鈥?plugins can add new file-type associations at runtime
- 鉁?Menu contribution system: `RegisterMenuItem()` / `UnregisterMenuItem()` / `GetMenuContributions()` / `ExecuteMenuAction()` 鈥?plugins can add IDE menu items with callbacks
- 鉁?`PolyglotEvent` struct and `PFN_polyglot_plugin_on_event` export 鈥?plugins implement a single event handler for all subscribed events
- 鉁?`PolyglotMenuContribution` struct with `menu_path`, `action_id`, `label`, `shortcut`, and `callback`
- 鉁?Extended `PolyglotHostServices` with 4 new function pointers: `subscribe_event`, `unsubscribe_event`, `register_menu_item`, `unregister_menu_item`
- 鉁?New `ActionManager` class: centralized action/keybinding management extracted from MainWindow; supports plugin-contributed actions with `RegisterPluginAction()` / `RemovePluginActions()`; keybinding persistence via `LoadKeybindings()` / `SaveKeybindings()`
- 鉁?New `PanelManager` class: manages bottom panel tabs; supports plugin-contributed panels via `RegisterPluginPanel()` / `RemovePluginPanels()`; unified `TogglePanel()` / `ShowPanel()` / `IsPanelActive()` API replacing scattered toggle logic
- 鉁?MainWindow refactored: panel toggle/show logic delegates to `PanelManager`; keybinding management delegates to `ActionManager`; plugin lifecycle events wired to file open/save/compile/workspace/theme changes
- 鉁?Plugin diagnostic forwarding: `SetDiagnosticCallback()` wired to IDE output panel
- 鉁?Plugin menu item forwarding: `SetMenuItemRegisteredCallback()` wired to `ActionManager`
- 鉁?Plugin file-type forwarding: `SetFileTypeRegisteredCallback()` wired to IDE output log
- 鉁?12 new unit tests: event type distinctness, event struct zero-init, menu contribution struct, subscribe/unsubscribe safety, fire-event-no-subscribers, file type registry CRUD, menu contributions empty, ExecuteMenuAction unknown, DispatchOpenFile with/without callback, FileType registered callback, diagnostic callback
- 鉁?Test count: 796 鈫?808 test cases; assertions: 34056 鈫?34085 鈥?all passing

## v1.0.3 (2026-03-17)

**Test Quality Improvements**
- 鉁?Replaced `REQUIRE(true)` in `lto_test.cpp` with optimizer statistics behavior assertions (all stats == 0 for empty module, module survives all optimization stages)
- 鉁?Replaced `REQUIRE(true)` in `gc_algorithms_test.cpp` with GC stats verification: `collections >= 10`, `total_allocations >= 500` for multi-cycle test; `total_allocations >= 200` for incremental collection
- 鉁?Replaced commented-out performance benchmarks with real throughput assertions (`total_allocations >= 10000`, `collections >= 1`)
- 鉁?Replaced `SUCCEED()` in `java_test.cpp` and `dotnet_test.cpp` with semantic analysis behavior checks (verifying diagnostics are type-mapping related, not crashes)
- 鉁?Replaced `REQUIRE(true)` in `threading_services_test.cpp` with phase-tracking atomics for barrier reset and exact-sum verification for large workloads
- 鉁?Strengthened Java/DotNet enum and struct lowering tests with positive diagnostic-state assertions
- 鉁?New `compilation_behavior_test.cpp`: 15 new behavior-level and failure-path test cases:
  - C++ single/multiple function IR output correctness
  - C++ conditional produces multiple basic blocks
  - x86_64 assembly contains function label and `ret` instruction
  - x86_64 object code has `.text` section with non-zero bytes and function symbol
  - ARM64 assembly output contains function label
  - IR verification passes on well-formed IR; catches malformed (empty function) IR
  - Ploy `LINK` produces correct cross-language call descriptors
  - Ploy `FUNC` produces named IR function
  - Failure paths: parse error diagnostics, sema error on undefined variable, lowering invalid code returns failure
- 鉁?Test count: 781 鈫?796 test cases; assertions: 3985 鈫?34056 鈥?all passing

## v1.0.2 (2026-03-17)

**Package Discovery Refactoring**
- 鉁?`CommandResult` struct: structured output with `stdout_output`, `exit_code`, `timed_out`, `failed`, `Ok()` convenience method
- 鉁?`ICommandRunner::RunWithResult()`: new primary interface with configurable per-command timeout; `Run()` kept for backward compatibility and delegates to `RunWithResult()`
- 鉁?`DefaultCommandRunner`: timeout support via `std::async` + `std::future::wait_for()`; configurable default timeout (10s)
- 鉁?`PackageIndexer` class: explicit pre-compilation package-index phase, decoupled from sema; runs discovery commands with timeouts and retries, populates shared `PackageDiscoveryCache`
- 鉁?`PackageIndexerOptions`: configurable `command_timeout`, `max_retries`, `verbose` flag
- 鉁?`PackageIndexer::Stats`: tracks `languages_indexed`, `commands_executed`, `commands_timed_out`, `commands_failed`, `packages_found`, `total_duration`
- 鉁?`PackageIndexer::SetProgressCallback()`: optional per-language progress reporting during indexing
- 鉁?`PloySemaOptions::enable_package_discovery` default changed from `true` to `false` 鈥?sema never shells out; callers use `PackageIndexer` as pre-phase
- 鉁?`polyc` driver: PackageIndexer wired as Phase 2.5 before sema; scans AST for referenced languages and indexes only those
- 鉁?CLI flags: `--package-index` (default), `--no-package-index`, `--pkg-timeout=<ms>`
- 鉁?10 new test cases: `CommandResult::Ok()`, `MockCommandRunner` structured result, timeout simulation, `PackageIndexer` cache population, cache skip, timeout handling, multi-language indexing, pre-phase 鈫?sema cache flow, progress callback, defaults verification

**Build System Modularization**
- 鉁?Top-level `CMakeLists.txt` reduced from ~690 lines to ~80 lines 鈥?project setup, compiler flags, dependencies, and `add_subdirectory()` calls only
- 鉁?`common/CMakeLists.txt`: `polyglot_common` library (type system, symbol table, plugins, debug info)
- 鉁?`middle/CMakeLists.txt`: `middle_ir` library (IR, optimization passes, PGO, LTO)
- 鉁?`frontends/CMakeLists.txt`: `frontend_common` + 6 language frontend libraries
- 鉁?`backends/CMakeLists.txt`: `backend_x86_64`, `backend_arm64`, `backend_wasm`
- 鉁?`runtime/CMakeLists.txt`: `runtime` library (GC, FFI, interop, services)
- 鉁?`tools/CMakeLists.txt`: `polyc`, `polyasm`, `polyld`, `polyopt`, `polyrt`, `polybench`, `polyui` (with full Qt detection logic)
- 鉁?`tests/CMakeLists.txt`: explicit per-module source lists replacing `GLOB_RECURSE` for unit tests; `integration_tests` and `benchmark_tests` retained with scoped globs
- 鉁?Test count: 781 test cases / 3985 assertions 鈥?all passing

## v1.0.1 (2026-03-17)
- 鉁?Two explicit compilation modes: **strict** and **permissive** (`--strict` / `--permissive` CLI flags)
- 鉁?Release builds default to strict mode via `POLYC_DEFAULT_STRICT` compile definition
- 鉁?Strict mode: semantic analysis reports placeholder-type fallbacks as errors instead of warnings
- 鉁?Strict mode: IR verifier rejects functions with unresolved placeholder I64 return types
- 鉁?Strict mode: lowering rejects cross-language calls with unknown return types
- 鉁?Strict mode: degraded stubs via `--force` are blocked; `--strict` and `--force` are mutually exclusive
- 鉁?Permissive mode: all placeholder-type fallbacks visible as warnings (previously silent)
- 鉁?`ReportStrictDiag()` helper: routes diagnostics to error (strict) or warning (permissive)
- 鉁?`IRType::is_placeholder` flag distinguishes genuine I64 from unresolved fallback I64

## v1.0.1 (2026-04-09)

**Staged compilation pipeline & polyui icon embedding**
- 鉁?Implemented a concrete 6-stage compilation pipeline for `.ploy` in `tools/polyc/src/compilation_pipeline.cpp`
- 鉁?Stage breakdown is now explicit and data-structured: `frontend -> semantic db -> marshal plan -> bridge generation -> backend -> packaging`
- 鉁?Added runtime pipeline orchestration (`CompilationPipeline::RunAll()` and per-stage runners) with stage timing capture
- 鉁?Wired `polyc` driver to execute the staged pipeline for `.ploy` as the primary path
- 鉁?Added package-index integration in the staged semantic flow via `PackageIndexer` + shared `PackageDiscoveryCache`
- 鉁?Added bridge generation stage integration with `PolyglotLinker` and resolved stub injection before backend emission
- 鉁?Added packaging stage output for deterministic `.pobj` emission and optional `polyld` link invocation in `link` mode
- 鉁?`polyui` Windows executable now embeds `tools/ui/common/resources/icon.ico` via CMake resource script (`tools/ui/windows/polyui.rc`), so generated `polyui.exe` ships with the project icon
- 鉁?`polyui` now explicitly sets runtime title-bar/window icons: Windows uses embedded `:/icons/icon.ico`, Linux/macOS use embedded `:/icons/icon.png`

> Note: two independent v1.0.1 entries exist above because the project ran
> two parallel maintenance branches in March / April 2026; both shipped under
> the same patch number before the v1.1.x line was opened.

## v1.0.0 (2026-03-15)
- 鉁?Project version unified to **1.0.0** (all previous v5.x/v4.x versions renumbered to v0.5.x/v0.4.x)
- 鉁?Release packaging scripts for all 3 platforms (`scripts/package_windows.ps1`, `scripts/package_linux.sh`, `scripts/package_macos.sh`)
- 鉁?Windows: portable ZIP archive + NSIS installer (`scripts/installer.nsi`) with PATH registration and Start Menu shortcuts
- 鉁?Linux: portable `.tar.gz` with Qt library bundling and wrapper script
- 鉁?macOS: portable `.tar.gz` with `macdeployqt` integration for `.app` bundle
- 鉁?Packaging documentation (`docs/specs/release_packaging.md` / `_zh.md`)
- 鉁?Version references updated across CMakeLists.txt, all tool source files, README.md, and USER_GUIDE (EN/ZH)

## v0.5.5 (2026-03-15)
- 鉁?Unified theme system via `ThemeManager` singleton 鈥?4 built-in colour schemes (Dark, Light, Monokai, Solarized Dark); all panels (main window, build, debug, git, settings) apply styles from a single source
- 鉁?Compile & Run (`Ctrl+R`): QProcess-based workflow compiles current file, locates output binary, and launches it with stdout/stderr streamed to the log panel
- 鉁?Stop (`Ctrl+Shift+R`): terminates a running process and cancels an active build
- 鉁?Debug panel: full call-stack frame parsing for both lldb and GDB/MI output (module, function, file, line); variable type/value extraction; watch expression evaluation with live result updates
- 鉁?Debug panel: watch context menu (Remove / Remove All / Evaluate) wired to `OnRemoveWatch` and `OnEvaluateWatch`
- 鉁?Debug panel: configurable debugger path and break-on-entry option from Settings
- 鉁?Custom keybindings: editable via `QKeySequenceEdit` in Settings 鈫?Key Bindings page; 28 default actions; custom shortcuts persisted in `QSettings` and applied at startup
- 鉁?Settings linkage: CMake path, build directory, and debugger path automatically propagate to `BuildPanel` and `DebugPanel` on apply
- 鉁?New source file `theme_manager.cpp` / `theme_manager.h` added to `tools/ui/common/`

## v0.5.4 (2026-03-11)
- 鉁?Qt-based desktop IDE (`polyui`): syntax highlighting, real-time diagnostics, file browser, tabbed editor, output panel, bracket matching, dark theme
- 鉁?IDE uses compiler frontend tokenizers for accurate, language-aware highlighting across all 6 supported languages
- 鉁?IDE keyboard shortcuts: Ctrl+B compile, Ctrl+Shift+B analyze, Ctrl+N/O/S/W file management
- 鉁?CMake Qt6/Qt5 auto-discovery: prefers standalone Qt installation at `D:\Qt` over bundled copies; pass `-DQT_ROOT=<path>` to override
- 鉁?Default `ninja` build now generates all executables (polyc, polyld, polyasm, polyopt, polyrt, polybench, polyui)
- 鉁?Platform-separated `polyui` source layout: shared code in `tools/ui/common/`, platform-specific entry points in `tools/ui/windows/`, `tools/ui/linux/`, `tools/ui/macos/`; CMake auto-selects the correct `main.cpp` per OS
- 鉁?macOS support: `MACOSX_BUNDLE` with `macdeployqt` post-build; Linux support: `xcb` platform default, `.desktop` integration
- 鉁?Integrated terminal in the IDE: embedded shell (PowerShell/bash/zsh), ANSI colour parsing, command history, multiple instances, `Ctrl+\`` toggle
- 鉁?Cleaned up legacy `tools/ui/ployui_windows/`, `tools/ui/src/`, and `tools/ui/include/` directories
- 鉁?Full documentation audit and statistics refresh 鈥?293 source files, 91,457 lines of code
- 鉁?Test growth: 743 unit (was 734) + 52 integration (was 50) + 18 benchmark = **813 total** (was 802)
- 鉁?Ploy frontend expanded to 6 compilation units (added `command_runner.cpp`, `package_discovery_cache.cpp`)
- 鉁?Updated all documentation (README, USER_GUIDE EN/ZH, tutorials) with current statistics

## v0.5.3 (2026-02-22)
- 鉁?Broke `common` 鈫?`middle` circular dependency 鈥?canonical IR headers now live in `middle/include/ir/`; `common/include/ir/` contains forwarding shims for backward compatibility
- 鉁?Added CI include-lint script (`scripts/check_include_deps.py`) enforcing layer constraints: `middle/` must not include `common/include/ir/`, backends must not include frontends
- 鉁?Unified debug-info modelling 鈥?added `common/include/debug/debug_info_adapter.h` with `ConvertToBackendDebugInfo()` bridging `polyglot::debug` (rich DWARF model) and `polyglot::backends` (flat emission model)
- 鉁?Added optimisation pipeline & switch matrix documentation (`docs/specs/optimization_pipeline.md` / `_zh.md`)
- 鉁?Added runtime C ABI reference documentation (`docs/specs/runtime_abi.md` / `_zh.md`)
- 鉁?Unified tool-layer namespaces: `polyc` and `polybench` now use `namespace polyglot::tools`, matching `polyasm` / `polyopt` / `polyrt`

## v0.5.2 (2026-02-22)
- 鉁?New `PloySemaOptions` configuration struct with `enable_package_discovery` switch
- 鉁?`PloySema` constructor accepts options; backward-compatible default constructor preserved
- 鉁?Session-level `PackageDiscoveryCache` 鈥?thread-safe, keyed by `language|manager|env_path`
- 鉁?`ICommandRunner` abstraction 鈥?external command execution decoupled from `_popen`; test-mockable
- 鉁?`DiscoverPackages` cache-first flow 鈥?hit 鈫?merge cached results; miss 鈫?run + store
- 鉁?Benchmark suites disable package discovery, isolating compiler-only performance
- 鉁?Lowering micro-benchmark timing fix 鈥?parse/sema excluded from timing window
- 鉁?Benchmark fast/full tiers via `POLYBENCH_MODE` env var (`fast`/`full`/default)
- 鉁?CTest: `benchmark_fast` and `benchmark_full` targets with labels
- 鉁?Discovery unit tests: disabled-no-cmd, repeated-analyze-once, key-isolation, cache-roundtrip

## v0.5.1 (2026-02-22)
- 鉁?Linker strong failure mode 鈥?unresolved symbols are hard errors; placeholder stubs no longer generated
- 鉁?Linker main flow fatal when cross-language entries exist but resolution fails
- 鉁?`polyc` and `polyasm` now accept `--arch=wasm` for WebAssembly target
- 鉁?WASM backend: real function call index resolution via name鈫抜ndex map (no more hardcoded indices)
- 鉁?WASM backend: alloca lowered to shadow stack model (mutable i32 global, grows downward from 65536)
- 鉁?WASM backend: unsupported IR instructions emit `unreachable` + diagnostic error (no silent NOPs)
- 鉁?`.ploy` sema strict mode (`SetStrictMode(true)`) 鈥?warns on untyped params, `Any` fallbacks, missing annotations
- 鉁?`.ploy` lowering consults sema symbol table before I64 fallback; uses `KnownSignatures` for return/param types
- 鉁?Debug emitter: FDE now has proper CFA instructions (push rbp / mov rbp,rsp frame setup)
- 鉁?Debug emitter: PDB TPI stream emits LF_POINTER type records for pointer types
- 鉁?Mach-O object file: per-section `reloff`/`nreloc` computed and written; relocation_info entries emitted
- 鉁?Rust and Python frontend lowering: diagnostic warnings for unknown type 鈫?I64 fallbacks
- 鉁?Rust advanced_features_test.cpp: all `REQUIRE(true)` replaced with behavioral parse + AST assertions
- 鉁?E2E tests: added ARM64 object code emission test and WASM multi-function binary smoke test

## v0.5.0 (2026-02-22)
- 鉁?Full project documentation update 鈥?all docs refreshed to reflect current state
- 鉁?WebAssembly (WASM) backend added (`backends/wasm/`)
- 鉁?Unified DWARF builder compiled into `polyglot_common` library
- 鉁?PDB emission: MSF block layout, TPI type records (LF_ARGLIST/LF_PROCEDURE), RFC 4122 v4 GUID
- 鉁?CFA initialisation with register save rules and NOP alignment
- 鉁?ELF: e_machine architecture switching (x86_64/ARM64), full SHT_RELA relocation sections
- 鉁?Mach-O: complete LC_SEGMENT_64, LC_SYMTAB, nlist_64 symbol table, section mapping
- 鉁?IR parser/printer symmetry: `fadd/fsub/fmul/fdiv/frem` parsing + global/const declarations
- 鉁?Preprocessor test coverage: 18 dedicated test cases for macros, directives, token pool
- 鉁?Debug tests Windows-compatible: `TmpPath()` via `std::filesystem::temp_directory_path()`
- 鉁?Cross-language link chain fully connected: polyc 鈫?PolyglotLinker 鈫?glue code 鈫?binary
- 鉁?Real IR lowering for all 6 frontends (no fallback stubs)
- 鉁?E2E tests enabled: 29 end-to-end test cases
- 鉁?Linker completeness: COFF/PE, ELF, Mach-O all implemented
- 鉁?polyopt: reads IR files and runs optimisation pipeline
- 鉁?polybench: full benchmark suite (compilation + E2E)
- 鉁?polyrt: FFI subcommand, real GC/thread statistics
- 鉁?Tutorial documentation added (`docs/tutorial/`)
- 鉁?16 sample programs (was 12)
- 鉁?Total: 813 test cases across 3 suites (743 unit + 52 integration + 18 benchmark)

## v0.4.3 (2026-02-20)
- 鉁?Added cross-language object destruction `DELETE` keyword
- 鉁?Added cross-language class extension `EXTEND` keyword
- 鉁?Enhanced diagnostics infrastructure: severity levels, error codes (1xxx-5xxx), traceback chains, suggestions
- 鉁?Added parameter count mismatch checking in semantic analysis
- 鉁?Added type mismatch checking in semantic analysis
- 鉁?All error reports upgraded to use structured error codes
- 鉁?AST / Lexer / Parser / Sema / IR Lowering full chain implementation for DELETE/EXTEND
- 鉁?36 new test cases, total 207 test cases, 598 assertions
- 鉁?Keywords count 52 鈫?54

## v0.4.2 (2026-02-20)
- 鉁?Added cross-language attribute access `GET` and assignment `SET` keywords
- 鉁?Added automatic resource management `WITH` keyword
- 鉁?Added type annotations with qualified types
- 鉁?Added interface mapping via `MAP_TYPE` for classes
- 鉁?AST / Lexer / Parser / Sema / IR Lowering full chain implementation for GET/SET/WITH
- 鉁?31 new test cases, total 171 test cases, 523 assertions
- 鉁?Bilingual documentation (USER_GUIDE.md / USER_GUIDE_zh.md)
- 鉁?Keywords count 49 鈫?52

## v0.4.1 (2026-02-20)
- 鉁?Added cross-language class instantiation `NEW` and method call `METHOD` keywords
- 鉁?AST / Lexer / Parser / Sema / IR Lowering full chain implementation
- 鉁?22 new test cases, total 140 test cases, 443 assertions
- 鉁?Bilingual feature documentation (`class_instantiation.md` / `class_instantiation_zh.md`)
- 鉁?Keywords count 47 鈫?49

## v0.4.0 (2026-02-19)
- 鉁?Added complete .ploy cross-language linking frontend chapter
- 鉁?Multi-package-manager support: CONFIG CONDA / UV / PIPENV / POETRY
- 鉁?Version constraint verification (6 operators)
- 鉁?Selective import
- 鉁?117+ test cases, 361+ assertions
- 鉁?Comprehensive review and rewrite of the complete guide (v0.3.0 鈫?v0.4.0)
- 鉁?Precise description of compilation flow (PolyglotCompiler's own frontends, no external compilers)

## v0.3.0 (2026-02-01)
- 鉁?Chapters 11-15 (implementation analysis / testing / advanced optimisations / achievements)
- 鉁?33+ optimisation passes, 4 GC algorithms
- 鉁?PGO/LTO/DWARF5 support

## v0.2.0 (2026-01-29)
- 鉁?Loop optimisation, GVN, constexpr, DWARF 5

## v0.1.0 (2026-01-15)
- 鉁?Basic compilation chain: 3 frontends, 2 backends, basic optimisations

---

*Maintained by PolyglotCompiler Team*  
*Last updated: 2026-04-28*
