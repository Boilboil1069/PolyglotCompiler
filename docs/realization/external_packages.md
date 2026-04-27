# External Package Consumption

This document describes how `polyc` discovers and loads symbols from
real third-party packages across every supported language frontend.
It covers the search order, CLI flags, on-disk layout assumptions,
and the runtime ABI bridges that turn lowered IR into actual
foreign function calls at link time.

> Status: this document tracks the rolling implementation of demand
> `2026-04-26-03`.  Phases ① – ⑥ are wired through the frontends and
> the indexer; phases ⑦ – ⑨ (linker dlopen, runtime ABI bridges,
> hermetic E2E suite) are tracked separately.

## 1. CLI surface

`tools/polyc/src/driver.cpp` exposes a single, language-orthogonal
external-package option group, declared under "External-package
options" in `--help`:

| Flag | Purpose |
| --- | --- |
| `-I<path>` / `--I=<path>` | C/C++ user header search root |
| `-isystem <path>` | C/C++ system header search root |
| `-D<name>[=<value>]` | Define a C/C++ preprocessor macro |
| `-U<name>` | Undefine a C/C++ preprocessor macro |
| `--python-stubs=<dir>` | Add a directory of `.pyi` stubs |
| `--classpath=<paths>` / `-cp` | Java classpath (`;` on Windows, `:` elsewhere) |
| `--reference=<dll>` / `-r` | .NET assembly reference (`.dll` / `.exe`) |
| `--crate-dir=<dir>` | Rust cargo project root used by `cargo metadata` |
| `--extern <name>=<path>` | Rust extern crate mapping |
| `--js-project=<dir>` | JavaScript/TypeScript project root |
| `--node-modules=<dir>` | Additional node_modules root |
| `--ruby-project=<dir>` | Ruby Bundler project root |
| `--gem-path=<dir>` | Additional gem search path |
| `--go-project=<dir>` | Go module root containing `go.mod` |
| `--go-mod-cache=<dir>` | Additional Go module cache root |

Each flag flows through `DriverSettings`, then `FrontendOptions`
(see `frontends/common/include/language_frontend.h`) and is consumed
by the matching frontend's `Lower()` entry point and by the ploy
`PackageIndexer` (see below).

## 2. C++ — preprocessor wiring

`frontends/common/src/preprocessor.cpp` provides a self-contained
C-preprocessor with `#include`, `#define`, function-like macros,
include guards, `#if/#ifdef/#elif/#else/#endif`, conditional
expression evaluation, and `#pragma once`.  The driver calls it
ahead of every C/C++ compilation unit (`stage_frontend.cpp` /
`compilation_pipeline.cpp`); the parser then receives the expanded
text.  Search paths populated via `-I` and `-isystem` are honoured
for both quoted and angle-bracketed includes; the angle-bracket
form prefers system paths.  The lexer's `kPreprocessor` token
stream therefore only ever surfaces leftover `#line` / `#pragma`
markers, which the parser filters at the top of `ParseStatement`.

## 3. Python — `.pyi` stub loader

`frontends/python/src/pyi_loader.cpp` ships an indentation-aware
parser for the typeshed-compatible subset of `.pyi`.  Search order
is:

1. Each directory passed via `--python-stubs=<dir>` (in CLI order)
2. `PYTHON_STUBS` environment variable (PATH-style, platform sep)
3. Discovered site-packages roots emitted by `PackageIndexer`
4. The compiled-in built-in module registry (`BuiltinModuleExports`)

For a request `import foo.bar`, the loader probes the following
inside every search root:

```
<root>/foo/bar.pyi
<root>/foo/bar/__init__.pyi
<root>/foo-stubs/bar.pyi
```

Stub bodies feed an exports map (`name -> core::Type`) consulted by
`PythonSemaOptions` inside `python_sema.cpp`.  The hard-coded
`BuiltinModuleExports()` is retained strictly as a deterministic
fallback for hermetic testing.

## 4. Java — `.class` / `.jar` reader

`frontends/java/src/class_file_reader.cpp` parses the JVM class
file format directly: constant pool, `methods_count` /
`fields_count` tables, `Signature`, `Code`, `Exceptions`,
`InnerClasses`, and `module-info.class` for module-level
visibility.  `--classpath` / `-cp` accepts both directories
(scanned for `.class` trees) and `.jar` archives (read via the
project's bundled mini-zip reader).  Loaded type/method
signatures populate the Java sema's `SymbolTable` so that
`import java.util.List` resolves to the real `List<E>` shape.

## 5. .NET — ECMA-335 metadata reader

`frontends/dotnet/src/metadata_reader.cpp` walks the PE/CLI
format described by ECMA-335: `#Strings`, `#US`, `#GUID`,
`#Blob` heaps, plus the `TypeDef`, `MethodDef`, `MemberRef`,
and `AssemblyRef` tables.  `--reference=<dll>` / `-r` may be
specified multiple times; assemblies are also looked up under
the directories returned by `dotnet --list-runtimes` and the
NuGet global cache (`%USERPROFILE%/.nuget/packages` on
Windows, `~/.nuget/packages` elsewhere).

## 6. Rust — crate loader & cargo metadata

Two complementary mechanisms are in play:

**Source-level crate loader** (`frontends/rust/src/crate_loader.cpp`)
re-uses the project's own Rust lexer and parser to walk
`src/lib.rs` / `src/main.rs` of an extern crate (or workspace
member), indexing every `pub` item — functions, structs, enums,
traits, type aliases, consts, macros, impl methods, and inline
or external submodules.  Binary artefacts (`.rlib` / `.rmeta`)
are detected by header signature and exposed as opaque crate
entries when only artefacts are available.

**Indexer-level cargo integration** (`frontends/ploy/src/sema/package_indexer.cpp`)
runs `cargo metadata --format-version 1 --no-deps` against the
crate root supplied by `--crate-dir`.  A small purpose-built JSON
walker (so the ploy frontend stays free of heavy dependencies)
extracts each package's `name`, `version`, and `manifest_path`
into `PackageInfo.install_path`.  When no crate root is provided,
`cargo install --list` is queried for globally-installed binary
crates.  Tests inject pip-style `name==version` output via the
`MockCommandRunner`; that path is preserved by short-circuiting
to `ParseFreezeOutput`.

`--extern <name>=<path>` is forwarded to the Rust crate loader
unchanged so `use external_crate::Item` resolves to the artefact
on disk.

## 7. Ploy package indexing

`PackageIndexer::IndexLanguage` accepts a `VenvConfig` per
language; for Rust the `venv_path` field is interpreted as the
cargo project root (i.e. the value of `--crate-dir`).  Both
`stage_frontend.cpp` and `compilation_pipeline.cpp` populate
this channel from the driver's `DriverSettings::rust_crate_dir`
so the indexer always sees the same value the user passed.

The resulting `PackageInfo` map flows through
`PloySemaOptions::discovery_cache` and is consumed by lowering
in `ploy/src/lowering/lowering.cpp`, where install paths are
emitted as link-time hints alongside `IMPORT` statements.

## 8. Diagnostics

Every loader is fail-soft by design: a missing stub, missing
classpath entry, or unreachable cargo binary downgrades to a
non-fatal diagnostic and falls through to the next backend.
Strict mode (`--strict`) promotes those warnings to errors so
production builds reject placeholder symbol resolution.

## 9. Pending work

The link-time half of the demand — `polyld` resolving every
lowered foreign symbol via `dlopen`/`LoadLibrary`+`dlsym` and
the language-specific runtime bridges in `runtime/src/libs/*_rt.c`
(CPython C-API, JNI, hostfxr, Rust cdylib, system ABI for C/C++)
— landed under demand `2026-04-26-04`.  Hermetic E2E coverage for
demand `2026-04-26-03` lives in
`tests/integration/external_packages/demand_03_test.cpp` and uses
the on-disk fixtures under
`tests/fixtures/external_packages/{cpp,python,rust,...}` (no
network and no system-installed runtimes are required).

## 10. Go / JavaScript / Ruby external packages (2026-04-27-1)

The three frontends added by demand `2026-04-26-01` �� Go, JavaScript
and Ruby �� were initially wired only at the lex/parse layer.  This
section documents the full resolution + lowering + ABI-bridge path
delivered in demand `2026-04-27-1`.

### 10.1 Go

* Resolver: `frontends/go/include/go_import_resolver.h`.
* Search order:
  1. `<--go-project>/<import-path>` (project-local sub-package).
  2. `<--go-project>/vendor/<import-path>` (vendor mode).
  3. `replace` directives from the project's `go.mod`.
  4. Module cache supplied via `--go-mod-cache=<dir>` and any
     auto-detected `GOPATH/pkg/mod` honouring the `!lower` escape
     rule for upper-case path segments.
  5. `GOROOT/src/<import-path>` (auto-detected from `GOROOT` env or
     `go env GOROOT` when available).
* Export harvesting reuses `GoLexer`+`GoParser` to walk every `.go`
  file (excluding `_test.go`) and records uppercase-prefixed
  `FuncDecl` / `TypeSpec` items as cross-package symbols.
* ABI bridge: `runtime/src/libs/go_rt.c` exposes
  `__ploy_go_load_pkg`, `__ploy_go_call`.  Missing host runtime
  emits one diagnostic and degrades to NULL returns.

### 10.2 JavaScript / TypeScript

* Resolver: `frontends/javascript/include/javascript_import_resolver.h`.
* Resolution mirrors Node.js:
  1. Relative specifiers (`./x`, `../y`) probe extensions in the
     order `.d.ts �� .ts �� .mjs �� .cjs �� .js �� .json` and fall back
     to `<dir>/index.<ext>`.
  2. Bare specifiers walk `node_modules` ancestors of the importer
     directory, then the project root, then every `--node-modules`
     root supplied on the command line.
  3. `package.json` resolution prefers `types` / `typings`, then
     `module`, then `main`, finally `index.*`.
* Export harvesting reuses `JsLexer`+`JsParser` against the
  resolved file (the parser accepts the `.d.ts` form because TS
  declarations are a syntactic superset of the JS surface we
  consume).
* ABI bridge: `runtime/src/libs/javascript_rt.c` exposes
  `__ploy_js_require`, `__ploy_js_call`.

### 10.3 Ruby

* Resolver: `frontends/ruby/include/ruby_import_resolver.h`.
* Search order for `require` / `load`:
  1. `RUBYLIB` environment paths.
  2. `--gem-path=<dir>` roots, treated both as plain `$LOAD_PATH`
     entries and as Bundler / RubyGems vendor directories
     (`<root>/<gem>/lib/<feature>.rb`).
  3. The project root itself and `<project>/lib/`.
  4. The host Ruby's `$LOAD_PATH` probed once via
     `ruby -e "puts $LOAD_PATH"` (silent fallback if absent).
* `require_relative` resolves against the importer file directory
  with `.rb` probing.
* `autoload` calls are resolved on registration so the constant is
  exposed at semantic-analysis time.
* ABI bridge: `runtime/src/libs/ruby_rt.c` exposes
  `__ploy_ruby_require`, `__ploy_ruby_call`.

### 10.4 CLI options

The driver options were already declared by demand `2026-04-26-03`
(`--go-project`, `--go-mod-cache`, `--js-project`, `--node-modules`,
`--ruby-project`, `--gem-path`).  In `2026-04-27-1` they are now
threaded through `FrontendOptions` and consumed by every frontend's
`Analyze()` / `Lower()` entry point.

### 10.5 Tests

Hermetic fixtures are shipped under
`tests/fixtures/external_packages/{go,javascript,ruby}/` and
exercised by `tests/integration/external_packages/external_packages_test.cpp`.
Each language has at least one positive case and one
missing-package case that asserts a clean diagnostic instead of a
crash.
