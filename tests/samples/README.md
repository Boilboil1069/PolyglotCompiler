# .ploy Sample Programs

This directory contains complete multi-language sample programs organized by feature category. Each sample folder includes:

- A `.ploy` file demonstrating the feature
- Corresponding C++ (`.cpp`), Python (`.py`), and/or Rust (`.rs`) source files
- All source files are compilable by PolyglotCompiler

## Directory Structure

| Folder | Feature | Languages | Description |
|--------|---------|-----------|-------------|
| `01_basic_linking/` | LINK, CALL, IMPORT, EXPORT | C++, Python | Basic cross-language function linking |
| `02_type_mapping/` | MAP_TYPE, STRUCT, containers | C++, Python | Type mapping with complex types |
| `03_pipeline/` | PIPELINE, control flow | C++, Python | Multi-stage pipeline with IF/WHILE/FOR/MATCH |
| `04_package_import/` | IMPORT PACKAGE, CONFIG | C++, Python | Package imports with version constraints |
| `05_class_instantiation/` | NEW, METHOD | C++, Python | Cross-language class instantiation and method calls |
| `06_attribute_access/` | GET, SET | C++, Python | Cross-language attribute access and assignment |
| `07_resource_management/` | WITH | C++, Python | Automatic resource management via `__enter__`/`__exit__` |
| `08_delete_extend/` | DELETE, EXTEND | C++, Python | Object destruction and class extension |
| `09_mixed_pipeline/` | All features | C++, Python, Rust | Complete ML pipeline combining all features |
| `10_error_handling/` | Error checking | C++, Python | Error scenarios and diagnostics demo |

## Compilation

Each sample can be compiled with a single command:

```bash
# Compile any .ploy sample (auto-discovers referenced source files)
polyc <sample>.ploy -o <output>

# Example
polyc 01_basic_linking/basic_linking.ploy -o basic_linking
polyc 09_mixed_pipeline/mixed_pipeline.ploy -o ml_pipeline
```

## Feature Quick Reference

| Keyword | Sample | Description |
|---------|--------|-------------|
| `LINK` | 01, 09 | Cross-language function binding |
| `IMPORT` | 01-10 | Module/package import |
| `EXPORT` | 01-10 | Symbol export |
| `MAP_TYPE` | 02, 09 | Type mapping between languages |
| `PIPELINE` | 03, 09 | Multi-stage pipeline declaration |
| `NEW` | 05, 08, 09 | Cross-language class instantiation |
| `METHOD` | 05-09 | Cross-language method call |
| `GET` | 06, 08 | Read foreign object attribute |
| `SET` | 06, 08, 09 | Write foreign object attribute |
| `WITH` | 07, 09 | Automatic resource management |
| `DELETE` | 08, 09 | Object destruction |
| `EXTEND` | 08, 09 | Class extension/inheritance |
| `CONFIG` | 04, 09 | Package manager configuration |
| `CONVERT` | 02 | Explicit type conversion |
