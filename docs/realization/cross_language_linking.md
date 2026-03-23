# Cross-Language Function-Level Linking — Implementation Details

## 1. Architecture Overview

The cross-language function-level linking system is implemented as a new frontend (`frontend_ploy`) that processes `.ploy` files through the standard compiler pipeline: Lexer → Parser → Sema → Lowering. The lowering phase generates polyglot IR containing cross-language call nodes, which the linker then resolves into concrete glue code.

### 1.1 Component Map

```
frontends/ploy/
├── include/
│   ├── ploy_lexer.h          # Lexer (inherits LexerBase)
│   ├── ploy_ast.h             # AST node definitions
│   ├── ploy_parser.h          # Parser (inherits ParserBase)
│   ├── ploy_sema.h            # Semantic analyzer (ForeignClassSchema, class_schemas_)
│   └── ploy_lowering.h        # IR lowering
└── src/
    ├── lexer/
    │   └── lexer.cpp          # Lexer: 54 keywords, operators, literals, comments
    ├── parser/
    │   └── parser.cpp         # Parser: recursive-descent, ~1380 lines
    ├── sema/
    │   └── sema.cpp           # Semantic analysis: param-count, return-type, ABI schema validation
    └── lowering/
        └── lowering.cpp       # Lowering: structured exception safety for WITH

runtime/
└── include/interop/
    └── container_marshal.h    # Container marshalling header
└── src/interop/
    └── container_marshal.cpp  # Container marshalling implementation

linker (extended):
└── tools/polyld/
    ├── include/
    │   └── polyglot_linker.h  # Cross-language link resolver
    └── src/
        └── polyglot_linker.cpp # Link resolution and glue code gen
```

### 1.2 Integration Points

- **`polyglot_common`**: Uses `core::Type`, `core::TypeSystem`, `core::SourceLoc` for unified type handling.
- **`frontend_common`**: Inherits `LexerBase`, `ParserBase`, uses `Diagnostics` and `Token`.
- **`middle_ir`**: Generates `ir::IRContext`, `ir::Function`, uses `ir::IRBuilder`.
- **`runtime::interop`**: Leverages `FFIRegistry`, `TypeMapping`, `Marshalling`, `CallingConvention`, `ContainerMarshal` for runtime cross-language calls.
- **`tools/polyld`**: `PolyglotLinker` now validates ABI compatibility at link time via `ABIDescriptor` and `ValidateABICompatibility()` in `ResolveSymbolPair()`.

## 2. Lexer Design

The `PloyLexer` extends `frontends::LexerBase` and tokenizes `.ploy` source into the standard `Token` stream. It recognizes all 41 `.ploy` keywords, operators, literals, and comments.

### 2.1 Token Classification

| Token Kind           | Examples                                    |
|---------------------|---------------------------------------------|
| `kKeyword`          | LINK, IMPORT, FUNC, IF, WHILE, PACKAGE, etc |
| `kIdentifier`       | my_func, data_loader, math_utils            |
| `kNumber`           | 42, 3.14, 0xFF                              |
| `kString`           | "hello", "path/to/file"                     |
| `kSymbol`           | (, ), {, }, ;, ::, ->, ==, !=, etc.         |
| `kComment`          | // line comment, /* block comment */         |
| `kEndOfFile`        | end of input                                |

## 3. AST Design

The AST follows the same pattern as the C++ and Python frontends with `AstNode`, `Statement`, and `Expression` base types.

### 3.1 Key AST Nodes

- **`LinkDecl`**: Represents `LINK(target_lang, source_lang, target_func, source_func)` with optional body containing `MAP_TYPE` directives and optional `LinkKind` (`kFunction`, `kVariable`, `kStruct`).
- **`ImportDecl`**: `IMPORT` directive with `module_path`, `alias`, `language`, and `package_name` fields. Supports three forms: path import, qualified import, and package import.
- **`ExportDecl`**: `EXPORT` directive with function name and optional external name.
- **`MapTypeDecl`**: `MAP_TYPE(source_type, target_type)` directive.
- **`PipelineDecl`**: Named pipeline with a sequence of statements.
- **`FuncDecl`**: Function definition with parameters, return type, and body.
- **`StructDecl`**: Structure definition with named typed fields.
- **`MapFuncDecl`**: Custom type conversion function.
- **`CallExpr`**: Cross-language call `CALL(lang, func, args...)`.
- **`VarDecl`**: Variable declaration (`LET`/`VAR`).
- Control flow: `IfStatement`, `WhileStatement`, `ForStatement`, `MatchStatement`.

### 3.2 Complex Type Nodes

- **`ListLiteral`**: `[expr1, expr2, ...]`
- **`TupleLiteral`**: `(expr1, expr2)`
- **`StructLiteral`**: `StructName { field1: val1, field2: val2 }`
- **`ConvertExpression`**: `CONVERT(expr, type)`

## 4. Parser Design

The parser is a recursive-descent parser (~1380 lines) that handles all `.ploy` constructs.

### 4.1 LINK Parsing

The parser expects:

```
LINK '(' target_lang ',' source_lang ',' target_func ',' source_func ')'
    ( ';'                                    // simple link
    | '{' (MAP_TYPE(a, b) ';')* '}'          // link with body
    | 'AS' 'VAR' ';'                         // variable link
    | 'AS' 'STRUCT' '{' (MAP_TYPE ';')* '}'  // struct link
    )
```

### 4.2 IMPORT Parsing

Three forms:

```
IMPORT string_literal 'AS' identifier ';'                 // path import
IMPORT identifier '::' identifier ';'                      // qualified import
IMPORT identifier 'PACKAGE' dotted_path ['AS' alias] ';'   // package import
```

Package names support dotted paths: `numpy.linalg`, `scipy.optimize`.

### 4.3 MAP_TYPE Parsing

```
MAP_TYPE '(' qualified_type ',' qualified_type ')' ';'
```

## 5. Semantic Analysis

The Sema pass performs:

1. **Symbol Resolution**: Resolve all identifiers to their declarations, including cross-module references.
2. **Language Validation**: Verify that LINK and IMPORT directives reference valid languages (`cpp`, `python`, `rust`, `c`, `ploy`).
3. **Type Checking**: Validate that linked functions have compatible signatures after type mapping.
4. **Link Validation**: Ensure that LINK directives reference valid target/source functions. When `MAP_TYPE` entries are present, `param_count_known` and `validated` flags are set on the `FunctionSignature`.
5. **Type Mapping Validation**: Verify that MAP_TYPE declarations define valid conversions.
6. **Control Flow Validation**: Ensure BREAK/CONTINUE are inside loops, all paths return a value, etc.
7. **Struct Validation**: Verify no duplicate field names, all field types valid.
8. **MAP_FUNC Validation**: Verify parameter and return types.
9. **Package Import Validation**: Verify the language is valid for PACKAGE imports.
10. **Class Schema Validation**: `NEW`/`METHOD`/`GET`/`SET` now resolve types from the `ForeignClassSchema` registry (`class_schemas_` map). When a matching schema is found, argument count, field types, and return types are checked at sema time rather than deferred to IR lowering. Strict mode emits errors for unresolved types; non-strict mode emits warnings.

## 6. Lowering Design

The lowering phase converts the type-checked AST into polyglot IR:

1. **LINK directives** produce `CrossLanguageCallStub` entries containing source/target language identifiers, mangled symbols, marshalling descriptors, and calling convention info.

2. **CALL expressions** produce `ir::CallInstruction` nodes with cross-language metadata.

3. **PIPELINE blocks** are lowered to sequences of calls with automatic type conversion between stages.

4. **Control flow** is lowered to standard IR blocks using `ir::IRBuilder`.

5. **IMPORT declarations** produce `__ploy_module_<lang>_<path>` global references.

6. **STRUCT declarations** register struct metadata in the IR context.

7. **MAP_FUNC declarations** generate `__ploy_mapfunc_<name>` IR functions.

8. **Container literals** are lowered to runtime allocation and element store sequences.

9. **CONVERT expressions** generate calls to `__ploy_convert_<type>`.

10. **WITH statements** are lowered with structured exception safety: the lowering emits three distinct blocks — `body`, `finally`, and `exit` — ensuring the foreign `__exit__` method is always called even when an exception propagates through the body. This matches Python's `with` statement semantics.

## 7. Cross-Language Link Resolution

The `PolyglotLinker` extends the existing `Linker` with:

### 7.1 Glue Code Generation

For each `LINK` directive, the linker generates a wrapper function that:
1. Marshals arguments from target types to source types
2. Invokes the source function via FFI
3. Marshals the return value back to the target type

### 7.2 Type Marshalling

| Conversion             | Strategy                                      |
|-----------------------|-----------------------------------------------|
| int → int (same size) | Direct copy                                   |
| int → float           | Cast instruction                              |
| string → string       | Encoding conversion (UTF-8 normalization)     |
| array → array         | Element-wise copy with element type conversion|
| struct → struct       | Field-by-field marshalling                    |
| pointer → handle      | Wrap in ForeignHandle with ownership tracking |
| list → list           | RuntimeList intermediate conversion           |
| dict → dict           | RuntimeDict intermediate conversion           |
| tuple → tuple         | RuntimeTuple intermediate conversion          |

### 7.3 Symbol Resolution

The linker resolves cross-language symbols by:
1. Loading object files from all source languages
2. Demangling symbols to find the target functions
3. Generating bridge symbols that connect the calling conventions

### 7.4 ABI Compatibility Validation

Added in the link phase: `ResolveSymbolPair()` calls `ValidateABICompatibility()` before emitting glue code. The check covers:

| Check | Details |
|-------|---------|
| **Parameter count** | Source and target must declare the same number of parameters |
| **Pointer compatibility** | Both sides must agree on pointer vs. value for each argument position |
| **Calling convention** | `ABIDescriptor` resolves SysV AMD64 / Win64 / AAPCS64 per platform and language |
| **Return size** | Non-void returns are validated for size compatibility before marshalling |

`ABIDescriptor` is returned by `GetABIDescriptor(language, symbol)` and carries the convention tag, argument classes, and shadow-space requirements for the target platform.

## 8. Runtime Support

At runtime, cross-language calls go through:
1. **FFI Registry** (`runtime::interop::FFIRegistry`): Central lookup for foreign functions
2. **Ownership Tracker** (`runtime::interop::OwnershipTracker`): Ensures safe resource management
3. **Dynamic Library Loader** (`runtime::interop::DynamicLibrary`): Loads compiled modules
4. **Container Marshal** (`runtime::interop::ContainerMarshal`): Converts container types between languages

## 9. Error Handling

All errors are reported through `frontends::Diagnostics` with precise source locations:
- **Lexer errors**: Invalid tokens, unterminated strings
- **Parser errors**: Syntax errors with recovery
- **Sema errors**: Type mismatches, undefined references, invalid link targets, unsupported languages, duplicate struct fields
- **Linker errors**: Unresolved symbols, incompatible calling conventions
