# Per-Language Multi-Version Compilation & Tool-chain Management

> Project version: **1.3.0** &nbsp;|&nbsp; Status: **delivered**

PolyglotCompiler distinguishes between the **source language** (cpp / python / java
/ dotnet / rust / go / javascript / ruby) and the **language version** (e.g.
`c++20`, `python 3.11`, `java 17`, `rust 2021`). This document describes the
foundation that lets every component agree on the version that should be used
for a particular translation unit.

> Phase 1 (this milestone) ships the type system, the CLI surface, the
> `polyver` tool-chain manager, the diagnostic codes, and the wiring through
> `polyc`. Per-frontend version gating, ploy `LANG` syntax, runtime ABI
> selection, the UI Toolchains tab and the integration tests are tracked
> separately in Phase 2 and Phase 3.

## Supported version matrix

| Language    | Recognised versions                                      | Default      | Enum (`polyglot::frontends`)        |
|-------------|----------------------------------------------------------|--------------|-------------------------------------|
| cpp         | c++98 c++03 c++11 c++14 c++17 c++20 c++23 c++26          | c++20        | `CppDialect`                        |
| python      | 2.7 3.6 3.8 3.10 3.11 3.12 3.13                          | 3.11         | `PythonVersion`                     |
| java        | 8 11 17 21 23                                            | 17           | `JavaRelease`                       |
| dotnet (C#) | 7.3 8 9 10 11 12 (target: net6 net7 net8 net9)           | C# 11 / net8 | `DotnetLangVersion` / `…Framework`  |
| rust        | 2015 2018 2021 2024 (editions)                           | 2021         | `RustEdition`                       |
| go          | 1.18 1.20 1.21 1.22 1.23                                 | 1.21         | `GoVersion`                         |
| javascript  | es5 es2015 es2017 es2020 es2022 es2023 esnext            | es2022       | `EcmaVersion`                       |
| ruby        | 1.9 2.7 3.0 3.2 3.3                                      | 3.2          | `RubyVersion`                       |

Every enum reserves the member `kAuto`; the value `auto` (or omitting the flag)
asks the frontend to infer the version from the inference order described
below.

## Inference order

For a given translation unit, the effective version is resolved as:

1. **Explicit per-call annotation** (Phase 2 — ploy `@LANG(version)` /
   `WITH LANG`).
2. **File-level pragma** (frontend-specific; e.g. C++ `#pragma poly std=c++23`).
3. **Project pin** &mdash; the value recorded in `<project>/.polyglot/toolchains.lock`
   (written by `polyver use`).
4. **CLI flag** passed to `polyc` (see below).
5. **User catalog default** &mdash; the entry marked `"default": true` in
   `~/.polyglot/toolchains.json` for that language.
6. **Tool-chain probe** &mdash; the highest version discovered by
   `polyver detect` for that language.
7. **Conservative fallback** &mdash; the `kXxxVersionDefault` constant defined
   in `frontends/common/include/language_versions.h`.

If steps 1–6 disagree the build emits diagnostic
`E_LANG_VERSION_MISMATCH` (`6001`); if step 6 has to fall back to step 7 the
warning `W_LANG_VERSION_FALLBACK` (`6002`) is raised; if no tool-chain at all
is available a hard `E_TOOLCHAIN_NOT_FOUND` (`6003`) is reported.

## `polyc` command-line surface

`polyc` accepts one optional flag per language plus a discovery command. All
flags accept `auto` (default) to keep inference enabled. Aliases follow common
upstream conventions.

| Flag                          | Alias       | Example values                              |
|-------------------------------|-------------|---------------------------------------------|
| `--std=<dialect>`             | `-std=`     | `c++20`, `c++23`, `20`                      |
| `--python-version=<v>`        | `--py=`     | `3.11`, `3.13`                              |
| `--java-release=<n>`          | `--java=`   | `17`, `21`                                  |
| `--cs-lang=<v>`               | `--csharp=` | `11`, `12`                                  |
| `--target-framework=<tfm>`    | `--tfm=`    | `net8`, `net9`                              |
| `--rust-edition=<y>`          | `--edition=`| `2021`, `2024`                              |
| `--go-version=<v>`            | `--go=`     | `1.21`, `1.22`                              |
| `--ecma=<v>`                  | `--es=`     | `es2022`, `esnext`                          |
| `--ruby-version=<v>`          | `--ruby=`   | `3.2`                                       |
| `--list-language-versions`    | —           | print the version matrix above and exit     |

The selected versions are forwarded by `tools/polyc/src/stage_frontend.cpp`
into `polyglot::frontends::FrontendOptions`, where each frontend can react.

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
| python      | `python3.13` … `python3.6`, `python3`, `python`, `python2`         |
| java        | `java -version` (parses `(?:openjdk|java) version "?(\d+)`)        |
| dotnet      | `dotnet --list-runtimes` (one entry per `Microsoft.NETCore.App` major) |
| rust        | `rustc --version` (records edition `2021`; full version stored in `vendor`) |
| go          | `go version` (`go1.X` → `1.X`)                                     |
| javascript  | `node --version`; major→`es2020/es2022/es2023`                     |
| ruby        | `ruby --version`                                                   |

## Diagnostic codes

| Code   | Symbol                          | Severity | Meaning                                                                |
|--------|---------------------------------|----------|------------------------------------------------------------------------|
| `6001` | `kLangVersionMismatch`          | Error    | A pragma / pin / flag asks for version *X* but the active tool-chain provides *Y*. |
| `6002` | `kLangVersionFallback`          | Warning  | No source could supply a version; the conservative default was used.   |
| `6003` | `kToolchainNotFound`            | Error    | No tool-chain at all is available for the requested language.          |

## Roadmap (still WIP)

* **Phase 2 &mdash; ploy syntax (done)**: module-level
  `LANG <name> = "<ver>";`, scoped `WITH LANG (name=ver, …) { … }` blocks,
  and single-statement `@LANG (name=ver) <stmt>` annotations are wired
  through the ploy lexer / parser / sema pipeline. Sema keeps a stack of
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
* **Phase 2 &mdash; runtime / backend gating (mostly done)**: every frontend
  now honors the `FrontendOptions` version field. C++ gates concepts /
  consteval on C++20+, Python gates the walrus operator on 3.8+, Java
  gates `record` on Java 17+, .NET gates `file class` on C# 11+, Rust
  gates `let ... else` on edition 2021+, and JavaScript gates optional
  chaining on ES2020+. Each violation surfaces as
  `ErrorCode::kLangVersionMismatch`. The runtime consumes the
  descriptor `VERSION` line to dispatch to the matching ABI bridge
  variant; the linker (`tools/polyld`) and the linker library
  (`tools/polyld_lib`) thread the version through descriptor loading.
* **Phase 2 &mdash; LINK pinning (done)**: `LinkEntry::lang_version` is
  now resolved against the *target* (foreign) language, not the source
  (host) language, so wrapping a `LINK` in `WITH LANG` / `@LANG` makes
  the bridge stub mangle the version into its name and the lowering
  emits one descriptor per pinned LINK. The lowering also recurses into
  `WithLangBlock::body` and `LangAnnotation::target` so wrapped LINKs
  and CALLs reach the descriptor pipeline.
* **Phase 3 (done)** &mdash; `polyui` Tool-chains tab calling
  `polyver list/detect`, ploy LANG syntax highlighting, and nine
  integration test directories under
  `tests/integration/language_versions/` exercising every supported
  language plus the per-callsite dual-pin coexistence path.

The project VERSION has been bumped to `1.3.0` and the requirements
ledger entry `2026-04-27-3` carries the `--end -done` completion mark.

