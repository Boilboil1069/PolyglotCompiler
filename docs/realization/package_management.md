# Package Management in .ploy

## Overview

The `.ploy` language provides comprehensive package management capabilities that allow
importing packages from different language ecosystems (Python, Rust, C++), with support
for version constraints, selective imports, package auto-discovery, and virtual environment
configuration.

## Features

### 1. Version Constraints

Specify the minimum, maximum, or exact version of a package required by your project.

**Syntax:**

```ploy
IMPORT <language> PACKAGE <package_name> <version_op> <version>;
```

**Supported version operators:**

| Operator | Meaning | Example |
|----------|---------|---------|
| `>=`     | Greater than or equal to | `IMPORT python PACKAGE numpy >= 1.20;` |
| `<=`     | Less than or equal to | `IMPORT python PACKAGE scipy <= 1.10.0;` |
| `==`     | Exactly equal to | `IMPORT python PACKAGE torch == 2.0.0;` |
| `>`      | Strictly greater than | `IMPORT python PACKAGE flask > 2.0;` |
| `<`      | Strictly less than | `IMPORT python PACKAGE django < 5.0;` |
| `~=`     | Compatible release | `IMPORT python PACKAGE requests ~= 2.28;` |

**Compatible release (`~=`)** follows PEP 440 semantics:
- `~= 1.20` means `>= 1.20, < 2.0`
- `~= 1.20.3` means `>= 1.20.3, < 1.21.0`

**Examples:**

```ploy
// Require NumPy version 1.20 or later
IMPORT python PACKAGE numpy >= 1.20;

// Require exact PyTorch version
IMPORT python PACKAGE torch == 2.0.0;

// Require a Rust crate with version constraint
IMPORT rust PACKAGE serde >= 1.0;

// Combined with alias
IMPORT python PACKAGE numpy >= 1.20 AS np;
```

**Validation:** The semantic analyzer validates version strings at compile time. If package
auto-discovery is enabled and the installed package version doesn't satisfy the constraint,
a compile-time error is reported.

### 2. Selective Imports

Import only specific functions, classes, or symbols from a package instead of the entire module.

**Syntax:**

```ploy
IMPORT <language> PACKAGE <package_name>::(<symbol1>, <symbol2>, ...);
```

**Examples:**

```ploy
// Import only array, mean, and std from numpy
IMPORT python PACKAGE numpy::(array, mean, std);

// Import specific functions from a submodule
IMPORT python PACKAGE numpy.linalg::(solve, inv);

// Single symbol import
IMPORT python PACKAGE os::(path);
```

**Combining with version constraints and aliases:**

The full syntax is parsed in this fixed order:

```
IMPORT <language> PACKAGE <package>[::(<symbols>)] [<version_op> <version>] [AS <alias>];
                         ~~~~~~~~~~~~~~~~~~~~~~~~  ~~~~~~~~~~~~~~~~~~~~~~~  ~~~~~~~~~~~
                         ① package + optional       ② optional version      ③ optional
                           selective imports          constraint (for pkg)     alias
```

> **Note:** The version constraint (e.g., `>= 1.20`) applies to the **entire package** (e.g., numpy),
> not to the individual selected symbols. The `::()` syntax immediately follows the package name
> and means "import only these symbols from the package"; the version constraint comes after and
> means "require this version of the package".

```ploy
// Selective import + version constraint: require numpy >= 1.20, import only array and mean
IMPORT python PACKAGE numpy::(array, mean) >= 1.20;

// Selective import + version constraint: require torch >= 2.0, import only tensor and no_grad
IMPORT python PACKAGE torch::(tensor, no_grad) >= 2.0;

// Whole-package import + alias: alias torch as pt
IMPORT python PACKAGE torch >= 2.0 AS pt;
```

> **Restriction:** Selective import `::()` and `AS` alias **cannot be combined**.
> For example, `IMPORT python PACKAGE torch::(tensor, no_grad) AS pt;` is illegal because
> `pt` is ambiguous — it is unclear whether it refers to `torch::tensor` or `torch::no_grad`.
> If you need an alias, use a whole-package import (without `::()`); if you need selective import, omit `AS`.

**Behavior:**
- The package itself is registered as a symbol (e.g., `numpy`)
- Each selected symbol is registered individually (e.g., `array`, `mean`)
- The linker generates targeted bindings only for the selected symbols
- Duplicate symbols in the selection list cause a compile-time error
- Combining selective import with AS alias causes a compile-time error

### 3. Package Auto-Discovery

The compiler automatically detects packages installed in the local environment and validates
imports against the discovered packages.

**Supported discovery mechanisms:**

| Language | Discovery Method | Command Used |
|----------|-----------------|--------------|
| Python   | pip freeze      | `python -m pip list --format=freeze` |
| Rust     | cargo install   | `cargo install --list` |
| C/C++    | pkg-config      | `pkg-config --list-all` |

**Behavior:**
- Discovery is triggered automatically when a `PACKAGE` import is encountered
- Discovery runs at most once per language per compilation unit
- If a virtual environment is configured (via `CONFIG VENV`), the discovery uses
  the venv's Python executable instead of the system Python
- Discovery is **best-effort**: if a package is not found, the compiler does NOT
  fail — the package may be available at link time in a different environment

### 4. Virtual Environment Support

Configure a specific virtual environment to use for package resolution and discovery.

**Syntax:**

```ploy
CONFIG VENV [<language>] "<path/to/venv>";
```

**Examples:**

```ploy
// Configure a Python virtual environment (language defaults to "python")
CONFIG VENV python "C:/Users/me/envs/data_science";

// Default language (Python) when language is omitted
CONFIG VENV "/home/user/.virtualenvs/ml";

// Must appear before IMPORT statements that use it
CONFIG VENV python "/opt/envs/production";
IMPORT python PACKAGE numpy >= 1.20;
```

**Rules:**
- Only one venv configuration per language is allowed per compilation unit
- Duplicate `CONFIG VENV` for the same language produces a compile-time error
- The language must be a valid supported language (cpp, python, rust, c, ploy)
- The venv path is used when running package discovery commands
- On Windows, the venv's Python is located at `<venv_path>\Scripts\python.exe`
- On Unix/macOS, the venv's Python is located at `<venv_path>/bin/python`

## Full Example

```ploy
// Configure Python virtual environment
CONFIG VENV python "C:/Users/me/envs/data_science";

// Import with version constraints
IMPORT python PACKAGE numpy >= 1.20 AS np;
IMPORT python PACKAGE scipy.optimize >= 1.8 AS opt;
IMPORT python PACKAGE pandas >= 1.5.0;

// Selective imports
IMPORT python PACKAGE numpy::(array, mean, std);

// Selective import with version constraint
IMPORT python PACKAGE torch::(tensor, no_grad) >= 2.0;

// Rust crate with version constraint
IMPORT rust PACKAGE rayon >= 1.7;

// Standard qualified imports
IMPORT cpp::math;
IMPORT rust::serde;

// Use imported packages in LINK declarations
LINK(cpp, python, compute_mean, np::mean);
LINK(cpp, python, compute_std, np::std);

MAP_TYPE(cpp::double, python::float);

// Pipeline using cross-language calls
PIPELINE data_analysis {
    FUNC statistics(data: LIST(f64)) -> TUPLE(f64, f64) {
        LET avg = CALL(python, np::mean, data);
        LET sd = CALL(python, np::std, data);
        RETURN (avg, sd);
    }
}

EXPORT data_analysis AS "data_analysis_pipeline";
```

## IR Metadata Generation

For each import with extended features, the lowering phase generates IR metadata:

- **Version constraint**: A global symbol `__ploy_module_<lang>_<pkg>_version_constraint`
  containing the operator and version string (e.g., `">= 1.20"`)
- **Selected symbols**: A global symbol `__ploy_module_<lang>_<pkg>_selected_symbols`
  containing a comma-separated list of selected symbols, plus individual external symbol
  declarations for each selected symbol

This metadata is consumed by the polyglot linker to:
1. Verify package compatibility at link time
2. Generate targeted binding code for only the selected symbols
3. Configure the correct virtual environment for runtime execution
