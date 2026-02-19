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

1. Validates the language is supported (`cpp`, `python`, `rust`, `c`, `ploy`)
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

## 7. Future Extensions

- **Version constraints**: `IMPORT python PACKAGE numpy >= 1.20;`
- **Selective import**: `IMPORT python PACKAGE numpy USE (array, mean, std);`
- **Package auto-discovery**: Automatic detection of installed packages
