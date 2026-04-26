# Language Package Import — Design Document

## 1. Overview

This document describes the design and implementation of the `IMPORT ... PACKAGE` feature in the `.ploy` language, enabling direct import of language-native packages (e.g., Python's numpy, Rust's serde) for use in cross-language pipelines.

## 2. Motivation

The original `IMPORT` directive only supported two forms:

- `IMPORT "path" AS alias;` — file-path based import
- `IMPORT lang::module;` — qualified module import

Both forms assume the target is a compiled module within the PolyglotCompiler ecosystem. However, real-world cross-language workflows need access to **third-party packages** in target languages:

- Python: numpy, scipy, pandas, torch, etc.
- Rust: serde, rayon, tokio, etc.

Without package import, developers would need to manually write wrapper modules.

## 3. Syntax

```
IMPORT language PACKAGE package_path [AS alias] ';'
```

### 3.1 Parameters

| Parameter      | Description                                | Required |
|----------------|-------------------------------------------|----------|
| `language`     | Target language identifier (`python`, `rust`, `cpp`, `c`) | Yes |
| `package_path` | Package name with optional dotted sub-path | Yes |
| `alias`        | Short name for use in the `.ploy` file     | No |

### 3.2 Examples

```ploy
// Basic package import
IMPORT python PACKAGE numpy;

// Package import with alias
IMPORT python PACKAGE numpy AS np;

// Sub-package import
IMPORT python PACKAGE scipy.optimize AS opt;

// Dotted path (deep sub-package)
IMPORT python PACKAGE numpy.linalg;

// Rust package import
IMPORT rust PACKAGE serde;
```

### 3.3 Usage After Import

Once imported, the package name (or alias) can be used in `LINK` directives and `CALL` expressions:

```ploy
IMPORT python PACKAGE numpy AS np;

// Use in LINK
LINK(cpp, python, compute, np::mean);

// Use in CALL
LET result = CALL(python, np::mean, data);
```

## 4. Implementation

### 4.1 Lexer Changes

Added `PACKAGE` as a new keyword (keyword #35 in the set of 41).

### 4.2 AST Changes

`ImportDecl` extended with a new field:

```cpp
struct ImportDecl : public Statement {
    std::string module_path;      // Module or package path
    std::string alias;            // Optional alias
    std::string language;         // Language identifier (e.g., "python")
    std::string package_name;     // Package name (e.g., "numpy", "scipy.optimize")
};
```

When `package_name` is non-empty, the import is a package import.

### 4.3 Parser Changes

`ParseImportDecl` handles three forms:

1. **String literal** next → path import: `IMPORT "path" AS alias;`
2. **Identifier followed by `PACKAGE`** → package import: `IMPORT lang PACKAGE pkg [AS alias];`
3. **Identifier followed by `::`** → qualified import: `IMPORT lang::module;`

For package import, the parser:
1. Reads the language identifier
2. Consumes the `PACKAGE` keyword
3. Reads the package name, supporting dotted paths (e.g., `numpy.linalg` is read as `numpy` + `.` + `linalg`)
4. Optionally reads `AS alias`
5. Requires trailing `;`

### 4.4 Semantic Analysis Changes

`AnalyzeImportDecl` updated:

1. Validates the language is supported (`cpp`, `c`, `python`, `rust`, `ploy`,
   `java`, `dotnet`/`csharp`, `javascript`/`js`/`typescript`/`ts`,
   `ruby`/`rb`, `go`/`golang`)
2. Determines the symbol name:
   - If `alias` is set → use alias
   - Else if `package_name` is set → use package name
   - Else → use module path
3. Registers the symbol with `Kind::kImport` and the language identifier

### 4.5 Lowering Changes

No changes needed. The existing `LowerImportDecl` creates a `__ploy_module_<lang>_<path>` external global, which works for both module and package imports since `module_path` carries the package name.

## 5. Runtime Integration

At runtime, package imports are resolved by:

1. **Python packages**: The runtime loads the Python interpreter and calls `import <package>`. The `FFIRegistry` stores a reference to the imported module object, enabling subsequent function calls via `PyObject_CallMethod`.

2. **Rust packages**: Rust packages are compiled as shared libraries (`.so`/`.dll`). The runtime loads them via `DynamicLibrary` and resolves symbols using standard dynamic linking.

3. **C/C++ packages**: C/C++ packages are linked at compile time or loaded as shared libraries.

## 6. Error Handling

| Error | Message | When |
|-------|---------|------|
| Unknown language | `unknown language 'java' in IMPORT` | Language not in supported set |
| Empty package name | `IMPORT module path is empty` | Package name not provided |
| Duplicate import | `redefinition of symbol 'np'` | Same alias used twice |

## 7. Supported Languages and Discovery Strategies

The set of languages accepted in `IMPORT <lang> PACKAGE ...` has been
extended. Each language is paired with one or more package-manager
probes that populate the shared `PackageDiscoveryCache` during sema
pre-analysis.

| Language identifier(s)                 | Discovery commands                                                            | Cache key prefix |
|----------------------------------------|-------------------------------------------------------------------------------|------------------|
| `python`                               | `pip list --format=freeze` (+ conda/uv/poetry)                                | `python::`       |
| `rust`                                 | `cargo metadata --format-version 1`                                           | `rust::`         |
| `cpp`, `c`                             | pkg-config / system include scan                                              | `cpp::`          |
| `java`                                 | `mvn dependency:list` / `gradle dependencies`                                 | `java::`         |
| `dotnet`, `csharp`                     | `dotnet list package` / NuGet cache scan                                      | `dotnet::`       |
| `javascript`, `js`, `typescript`, `ts` | `npm ls --json` / `yarn list --json` / `pnpm list --json` (+ Node core stdlib) | `javascript::`   |
| `ruby`, `rb`                           | `bundle list` (when Gemfile present) / `gem list --local` (+ Ruby stdlib)     | `ruby::`         |
| `go`, `golang`                         | `go list -m all` (+ Go stdlib paths)                                          | `go::`           |

### 7.1 JavaScript / TypeScript

`PackageIndexer::IndexJavaScript` runs all three managers in turn so a
project using npm, Yarn, or pnpm is fully covered. The parsers are
inline (no `nlohmann::json` dependency in the indexer) so unit tests
can drive them with a `MockCommandRunner`. After the manager output is
parsed, the 32 Node.js core modules (`fs`, `http`, `path`, …) are
unconditionally injected so that
`IMPORT javascript PACKAGE fs::(readFileSync);` resolves even on a host
with no `package.json`.

Scoped names such as `@types/node` are preserved by splitting the
yarn-style `<name>@<version>` token on its **rightmost** `@`.

### 7.2 Ruby

`IndexRuby` first tries `bundle list` whenever a project path was
provided, then falls back to `gem list --local`. The parser handles
both the regular `name (1.2.3)` form and the `name (default: 2.6.3)`
form used by stdlib-bundled gems. ~80 Ruby stdlib names (`set`, `time`,
`json`, `csv`, …) are always injected.

### 7.3 Go

`IndexGo` runs `go list -m all` and parses each `<module-path> v<X.Y.Z>`
line (the first line — the main module, with no version — is skipped).
The Go standard library is also baked in (~100 paths covering `fmt`,
`net/http`, `encoding/json`, `sync`, `context`, …) so stdlib imports
work without a `go.mod`.

## 8. CLI Plumbing

`polyc` exposes per-language flags that feed the indexer's
project-path argument and any auxiliary search roots:

| Flag                                          | Purpose                                |
|-----------------------------------------------|----------------------------------------|
| `--python-stubs <dir>`                        | Python typeshed / stub roots           |
| `--classpath <jars>`, `-cp <jars>`            | Java classpath                         |
| `--reference <dll>`, `-r <dll>`               | .NET assembly references               |
| `--crate-dir <dir>`, `--extern <name=path>`   | Rust crate sources / `--extern`        |
| `--js-project <dir>`, `--node-modules <dir>`  | JS project root / extra `node_modules` |
| `--ruby-project <dir>`, `--gem-path <dir>`    | Ruby project root / extra GEM_PATH     |
| `--go-project <dir>`, `--go-mod-cache <dir>`  | Go project root / extra module cache   |

Both `--name=value` and `--name value` forms are accepted. The values
are stored on `polyc::Settings` and forwarded into `PloySemaOptions`
so that the indexer's per-language sub-helpers receive a non-empty
`project_path` when appropriate.

## 9. Future Extensions

- **Version constraints**: `IMPORT python PACKAGE numpy >= 1.20;`
- **Selective import**: `IMPORT python PACKAGE numpy USE (array, mean, std);`
- **Lock-file ingestion**: parse `package-lock.json`, `Gemfile.lock`,
  and `go.sum` directly to avoid spawning external commands.
