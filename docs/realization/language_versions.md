# Per-Language Multi-Version Compilation & Tool-chain Management

> Project version: **1.3.0** &nbsp;|&nbsp; Status: **delivered**

PolyglotCompiler distinguishes between the **source language** (cpp / python / java
/ dotnet / rust / go / javascript / ruby) and the **language version** (e.g.
`c++20`, `python 3.11`, `java 17`, `rust 2021`). This document describes the
foundation that lets every component agree on the version that should be used
for a particular translation unit.

> The version selector, CLI, `polyver`, poly `LANG` pin, foreign-signature
> extraction and per-frontend gating paths are wired end to end.  The contract
> is fail-closed: recognizing a version never implies that an unmodelled syntax
> form may be silently accepted or lowered.

## Recognised version selectors

| Language    | Recognised versions                                      | Default      | Enum (`polyglot::frontends`)        |
|-------------|----------------------------------------------------------|--------------|-------------------------------------|
| cpp         | c++98 c++03 c++11 c++14 c++17 c++20 c++23 c++26          | c++23         | `CppDialect`                        |
| python      | 2.7; every 3.x release from 3.6 through 3.14               | 3.14          | `PythonVersion`                     |
| java        | every release from 8 through 26                            | 26            | `JavaRelease`                       |
| dotnet (C#) | 7.3 8 9 10 11 12 13 14 preview (target: net6 … net10)     | C# 14 / net10 | `DotnetLangVersion` / `…Framework`  |
| rust        | 2015 2018 2021 2024 (editions)                            | 2024          | `RustEdition`                       |
| go          | every release from 1.18 through 1.26                       | 1.26          | `GoVersion`                         |
| javascript  | es5; every annual edition from es2015 through es2026; esnext | es2026      | `EcmaVersion`                       |
| ruby        | 1.9 2.7 3.0 3.1 3.2 3.3 3.4 4.0                          | 4.0           | `RubyVersion`                       |

Every enum reserves `kAuto`. Inside a frontend it resolves deterministically to
the default shown above. Project/tool-chain discovery is a caller concern:
`polyver` or the UI must convert a discovered value to an explicit enum before
invoking the frontend.

These are selectors, not a claim that every construct can be lowered. Analysis
must either build a faithful AST or report an error; lowering must report
`kUnsupportedLowering` for an analysed construct it cannot compile. It must
never silently discard a construct.

## Audited modern-language boundaries

This work advances every default to its current stable baseline and gives the
following modern syntax families a regression-tested version gate and AST
contract:

| Language | Representative faithfully analysed and precisely gated boundaries |
|----------|---------------------------------------------------------------------|
| C++ | C++11 `nullptr`, lambdas and range-for; C++17 structured bindings, folds and `if` initialisers; C++20 modules, concepts, `requires`, `<=>`, designated initialisers and coroutines; C++23 explicit object parameters and `if consteval`. C++26 is a selectable boundary, but unstandardised or unmodelled constructs are never presented as supported. |
| Python | Python 3.8 positional-only parameters and assignment expressions, structural pattern matching, `except*`, PEP 695 type parameters, Python 3.13 type-parameter defaults, and Python 3.14 template strings (a distinct AST) plus PEP 758 unparenthesised exception lists. |
| Rust | 2015/2018/2021/2024 keyword contexts, async/await, and the 2024 `gen`, guarded-string reservation and unsafe-attribute rules. |
| Java | Java 9 module descriptors and private interface methods; records, sealed types and switch expressions; Java 25 module imports, compact source files and flexible constructor bodies. The Java 26 default selects stable syntax; preview-only forms are not silently enabled as stable. |
| C# | C# 8 switch expressions, C# 9 `init`, plus raw strings, primary constructors and collection/parameter evolution; C# 14 extension blocks, `field`-backed properties, compound-assignment operators, `#:` file directives, null-conditional assignment, unbound `nameof`, and partial constructors/events. |
| Go | Generics and type sets; Go 1.22 integer range, 1.23 range-over-function, 1.24 generic aliases, and Go 1.26 `new(expression)` plus self-referential generic constraints. |
| JavaScript | ES2015 declaration/class/module/arrow-function boundaries, class private fields/static blocks, the RegExp `v` flag and ES2025 import attributes. Explicit-resource-management `using`/`await using` is accepted only under `esnext`; decorators still produce `kUnsupportedSyntax`. |
| Ruby | Safe navigation, pattern/rightward assignment, argument forwarding and block ASTs; Ruby 3.1 hash-value omission, Ruby 3.4 implicit `it`, and Ruby 4.0 line-leading logical continuation. |

Here, “complete modern support” means a complete and inspectable **frontend
boundary**: either produce a faithful AST and complete analysis, or fail at the
first stage that cannot preserve the source. It does not claim that every node
already has native IR, runtime ABI and backend support. Such nodes report
`kUnsupportedLowering` when they reach a lowering path that cannot preserve
their semantics.

## Resolution and conflict rules actually implemented

1. An explicit `polyc` / API version selects the caller's requirement.
2. A module-level poly `LANG` pin fills an `auto` selection for imported source.
3. If both are explicit they must agree, otherwise
   `kLangVersionMismatch` is reported.
4. If neither is explicit, the deterministic `kXxxVersionDefault` is used.

Scoped `WITH LANG` / `@LANG` pins remain attached to individual bridge call
descriptors. An unknown CLI selector is a command-line usage error: `polyc`
prints the error and exits with status 2. Invalid or conflicting poly pins
report `kLangVersionMismatch`. Neither path falls back to another dialect.

.NET also has a real TFM-derived default: `net6` through `net10` select C# 10
through C# 14 respectively. An explicit `--cs-lang` newer than the supported
default for the selected TFM, or `preview` paired with a stable explicit TFM,
reports `kLangVersionMismatch`. `--cs-lang=preview` remains available when the
TFM itself is left at `auto` for preview-boundary analysis. TFM currently
participates only in language-version compatibility; it does not validate the
API surface of the corresponding .NET reference pack.

The foreign-signature index currently projects fixed parameters only;
default/rest/variadic metadata and full overload resolution are not part of the
cross-language ABI model yet. An overload or short-name alias collision therefore
reports `kSignatureMismatch` and atomically rejects the module instead of
overwriting an earlier signature. A missing or unreadable foreign source reports
`kSignatureMissing`. C++ signature scanning treats `<...>` system headers as an
external contract, while local `"..."` headers must resolve; macros and
conditionals are still preprocessed for the selected dialect.

## `polyc` command-line surface

`polyc` accepts one optional flag per language plus a discovery command. All
flags accept `auto` (default), which selects the deterministic modern default
from the table. Aliases follow common upstream conventions.

| Flag                          | Alias       | Example values                              |
|-------------------------------|-------------|---------------------------------------------|
| `--std=<dialect>`             | `-std=`     | `c++20`, `c++23`, `20`                      |
| `--python-version=<v>`        | `--py=`     | `3.12`, `3.14`                              |
| `--java-release=<n>`          | `--java=`   | `21`, `26`                                  |
| `--cs-lang=<v>`               | `--csharp=` | `12`, `14`                                  |
| `--target-framework=<tfm>`    | `--tfm=`    | `net8`, `net10`                             |
| `--rust-edition=<y>`          | `--edition=`| `2021`, `2024`                              |
| `--go-version=<v>`            | `--go=`     | `1.22`, `1.26`                              |
| `--ecma=<v>`                  | `--es=`     | `es2024`, `es2026`, `esnext`                |
| `--ruby-version=<v>`          | `--ruby=`   | `3.4`, `4.0`                                |
| `--list-language-versions`    | —           | print the version matrix above and exit     |

The selected versions are forwarded by `tools/polyc/src/stage_frontend.cpp`
into `polyglot::frontends::FrontendOptions`, where each frontend can react.

`kUnsupportedSyntax`, `kUnsupportedLowering`, and `kLangVersionMismatch` are
non-bypassable boundary errors. `--force` cannot send a missing AST, partial IR,
or dialect conflict into backend / packaging stages.

## `polyver` &mdash; tool-chain manager

`polyver` is a standalone executable (`tools/polyver/`) that discovers the host
tool-chains, persists them in a JSON catalog and lets a project pin a default.

```text
polyver list [<lang>]                List discovered tool-chains
polyver detect                       Probe the host and refresh ~/.polyglot/toolchains.json
polyver use   <lang> <version>       Pin a per-project default (writes .polyglot/toolchains.lock)
polyver path  <lang> <version>       Print the absolute executable path of a tool-chain
polyver --version                    Print polyver version
polyver --help                       Print this help text
```

### Catalog locations

* **User catalog** &mdash; `~/.polyglot/toolchains.json`. Written by
  `polyver detect`; preserves any entry that `default: true` even when
  re-detecting.
* **Project lock** &mdash; `<project>/.polyglot/toolchains.lock`. Written by
  `polyver use`; takes precedence over the user catalog when looking up a
  tool-chain. The project root is the nearest ancestor that contains a
  `.polyglot/` directory; if none exists, `polyver use` bootstraps one in the
  current working directory.

### JSON schema (`polyglot.toolchains.v1`)

```json
{
  "schema": "polyglot.toolchains.v1",
  "generated_by": "polyver",
  "toolchains": [
    { "language": "cpp",    "version": "c++20", "path": "/usr/bin/g++",      "vendor": "gcc 11.4.0", "default": true },
    { "language": "python", "version": "3.11",  "path": "/usr/bin/python3",  "vendor": "CPython 3.11.6" },
    { "language": "rust",   "version": "2021",  "path": "~/.cargo/bin/rustc","vendor": "rustc 1.78.0" }
  ]
}
```

The same schema is used for the project lock file.

### Detection behaviour

`polyver detect` probes the host `PATH` for well-known executables:

| Language    | Probed executables / versions                                      |
|-------------|--------------------------------------------------------------------|
| cpp         | `clang++`, `g++`, `cl` (MSVC). Maps `gcc>=10`/`msvc>=19` → `c++20`, `gcc>=13` → `c++23` |
| python      | `python3.14` … `python3.7`, `python3`, `python`                    |
| java        | `java -version` (parses `(?:openjdk|java) version "?(\d+)`)        |
| dotnet      | `dotnet --list-runtimes` (one entry per `Microsoft.NETCore.App` major) |
| rust        | `rustc --version` (`rustc>=1.85` → edition `2024`, otherwise `2021`; full version stored in `vendor`) |
| go          | `go version` (`go1.X` → `1.X`)                                     |
| javascript  | `node --version`; release-line mapping through `es2026`            |
| ruby        | `ruby --version`                                                   |

## Diagnostic codes

| Code   | Symbol                          | Severity | Meaning                                                                |
|--------|---------------------------------|----------|------------------------------------------------------------------------|
| `2006` | `kUnsupportedSyntax`            | Error    | The selected frontend cannot faithfully represent a recognised source construct. |
| `4003` | `kUnsupportedLowering`          | Error    | Analysis succeeded, but lowering has no semantics-preserving implementation for the AST node. |
| `6001` | `kLangVersionMismatch`          | Error    | Source syntax, a pin, or a tool-chain conflicts with the selected language version. |
| `6002` | `kLangVersionFallback`          | Warning  | No source supplied a version; the deterministic stable default was used. |
| `6003` | `kToolchainNotFound`            | Error    | No tool-chain at all is available for the requested language.          |

## Implementation status

* **Phase 2 &mdash; poly syntax (done)**: module-level
  `LANG <name> = "<ver>";`, scoped `WITH LANG (name=ver, …) { … }` blocks,
  and single-statement `@LANG (name=ver) <stmt>` annotations are wired
  through the poly lexer / parser / sema pipeline. Sema keeps a stack of
  pin frames: the module-level pragma populates the bottom frame, while
  `WITH LANG` / `@LANG` push and pop inner frames. When sema visits each
  cross-language site (`AnalyzeCrossLangCall`, `AnalyzeNewExpression`,
  `AnalyzeMethodCallExpression`, `AnalyzeGetAttrExpression`,
  `AnalyzeSetAttrExpression`, `AnalyzeDeleteExpression`,
  `AnalyzeWithStatement`, `AnalyzeExtendDecl`, `AnalyzeLinkDecl`), it
  calls `ResolveLangVersion(language)` and stores the result on the AST
  node's `lang_version_pin` field. Lowering copies it into
  `CrossLangCallDescriptor::lang_version`; the bridge stage emits a
  paired `VERSION <lang> <ver>` line right after each `CALL` line in the
  `.paux` descriptor file; polyld's `LoadDescriptorFile` picks it up and
  attaches it to the matching call descriptor. Coverage lives in
  `tests/unit/frontends/ploy/lang_version_pin_test.cpp`.
* **Phase 2 &mdash; analysis / lowering boundaries (hardened)**: every frontend
  now honors the `FrontendOptions` version field for the implemented modern
  syntax families. Examples include C++ concepts/modules/spaceship, Python
  assignment expressions and PEP 695 declarations, Java records/sealed types,
  C# file/global/raw-string forms, Rust async/await edition boundaries, Go
  generics and range evolution, ECMAScript optional chaining, and Ruby modern
  argument/pattern forms. Source syntax that is newer than the selected
  release, as well as malformed or unknown selectors/pins, surfaces as
  `ErrorCode::kLangVersionMismatch`. The runtime consumes the
  descriptor `VERSION` line to dispatch to the matching ABI bridge
  variant; the linker (`tools/polyld`) and the linker library
  (`tools/polyld_lib`) thread the version through descriptor loading.
  Analysed constructs without a safe IR representation surface
  `kUnsupportedLowering` instead of being dropped.
* **Phase 2 &mdash; LINK pinning (done)**: `LinkEntry::lang_version` is
  now resolved against the *target* (foreign) language, not the source
  (host) language, so wrapping a `LINK` in `WITH LANG` / `@LANG` makes
  the bridge stub mangle the version into its name and the lowering
  emits one descriptor per pinned LINK. The lowering also recurses into
  `WithLangBlock::body` and `LangAnnotation::target` so wrapped LINKs
  and CALLs reach the descriptor pipeline.
* **Phase 3 (done)** &mdash; `polyui` Tool-chains tab calling
  `polyver list/detect`, poly LANG syntax highlighting, and nine
  integration test directories under
  `tests/integration/language_versions/` exercising every supported
  language plus the per-callsite dual-pin coexistence path.

The project VERSION has been bumped to `1.3.0` and the requirements
ledger entry `2026-04-27-3` carries the `--end -done` completion mark.
