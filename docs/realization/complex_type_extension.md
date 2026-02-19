# Complex Parameter Type Extension — Implementation Details

## 1. Overview

This document describes the extension of the `.ploy` language to support complex parameter types for cross-language function-level linking. The goal is to enable seamless marshalling of container types (lists, tuples, dictionaries), structured types (structs), and optional types across language boundaries.

### 1.1 Motivation

The initial `.ploy` language supports primitive types (`INT`, `FLOAT`, `BOOL`, `STRING`, `VOID`) and simple `ARRAY`. Real-world cross-language interop requires richer type support:

- **Python**: `list`, `tuple`, `dict`, `Optional`
- **C++**: `std::vector<T>`, `std::map<K,V>`, `std::tuple<...>`, `struct`, `std::optional<T>`
- **Rust**: `Vec<T>`, `HashMap<K,V>`, `(T1, T2, ...)`, `struct`, `Option<T>`

### 1.2 Design Goals

1. **Unified container type syntax**: `LIST(T)`, `TUPLE(T1, T2, ...)`, `DICT(K, V)`, `OPTION(T)`
2. **Struct definitions**: Named aggregate types with fields for cross-language struct mapping
3. **Conversion functions**: `MAP_FUNC` declarations for custom type conversion logic
4. **Explicit conversion**: `CONVERT(expr, target_type)` for explicit type casting
5. **Container literals**: `[1, 2, 3]` for lists, `(1, "hello")` for tuples

## 2. Language Extensions

### 2.1 New Keywords

| Keyword     | Purpose                                     |
|-------------|---------------------------------------------|
| `LIST`      | List/vector container type                  |
| `TUPLE`     | Tuple container type                        |
| `DICT`      | Dictionary/map container type               |
| `OPTION`    | Optional/nullable container type            |
| `MAP_FUNC`  | Custom type mapping function declaration    |
| `CONVERT`   | Explicit type conversion expression         |
| `PACKAGE`   | Language-native package import              |

### 2.2 Container Type Syntax

Container types use **parenthesized** type arguments:

```ploy
LIST(i32)                          // List of 32-bit integers
TUPLE(i32, STRING, f64)            // Heterogeneous tuple
DICT(STRING, i32)                  // Dictionary: string keys, int values
OPTION(i32)                        // Optional integer (nullable)
LIST(LIST(f64))                    // Nested list
DICT(STRING, LIST(f64))            // Dict with list values
```

### 2.3 Struct Definitions

```ploy
STRUCT Point {
    x: f64;
    y: f64;
    label: STRING;
}

STRUCT DataSet {
    name: STRING;
    values: LIST(f64);
    metadata: DICT(STRING, STRING);
}
```

### 2.4 MAP_FUNC Declarations

```ploy
// Conversion function with logic
MAP_FUNC normalize(x: f64) -> f64 {
    IF x < 0.0 {
        RETURN 0.0;
    }
    IF x > 1.0 {
        RETURN 1.0;
    }
    RETURN x;
}

// Container element conversion
MAP_FUNC to_list(x: f64) -> LIST(f64) {
    LET result = [x];
    RETURN result;
}
```

### 2.5 CONVERT Expression

```ploy
LET x = CONVERT(python_value, i32);
LET items = CONVERT(raw_list, LIST(f64));
LET table = CONVERT(cpp_map, DICT(STRING, i32));
```

### 2.6 Container Literals

```ploy
LET numbers = [1, 2, 3, 4, 5];                          // list literal
LET pair = (1, "hello");                                  // tuple literal
LET origin = Point { x: 0.0, y: 0.0, label: "origin" };  // struct literal
```

## 3. Extended Type Mapping Table

| .ploy Type          | C++ Equivalent             | Python Equivalent   | Rust Equivalent         |
|---------------------|----------------------------|---------------------|-------------------------|
| i32                 | int32_t                    | int                 | i32                     |
| i64                 | int64_t                    | int                 | i64                     |
| f32                 | float                      | float               | f32                     |
| f64                 | double                     | float               | f64                     |
| BOOL                | bool                       | bool                | bool                    |
| STRING / str        | std::string                | str                 | String                  |
| VOID                | void                       | None                | ()                      |
| ptr                 | void*                      | object              | *mut u8                 |
| LIST(T)             | std::vector\<T\>           | list                | Vec\<T\>                |
| TUPLE(T1,T2,...)    | std::tuple\<T1,T2,...\>    | tuple               | (T1, T2, ...)           |
| DICT(K,V)           | std::unordered_map\<K,V\>  | dict                | HashMap\<K,V\>          |
| OPTION(T)           | std::optional\<T\>         | Optional / None     | Option\<T\>             |
| STRUCT Name         | struct Name                | class/namedtuple    | struct Name             |

## 4. AST Extensions

### 4.1 Type Representation

The existing `ParameterizedType` node handles `LIST(T)`, `TUPLE(T1,T2,...)`, `DICT(K,V)`, and `OPTION(T)` via the `name` field:

- `name == "LIST"` with 1 type arg → list type
- `name == "TUPLE"` with N type args → tuple type
- `name == "DICT"` with 2 type args → dict type
- `name == "OPTION"` with 1 type arg → optional type

### 4.2 New Expression Nodes

| Node | Fields | Purpose |
|------|--------|---------|
| `ListLiteral` | elements: vector\<Expression\> | List literal `[a, b, c]` |
| `TupleLiteral` | elements: vector\<Expression\> | Tuple literal `(a, b)` |
| `StructLiteral` | struct_name, fields | Struct literal `Name { f: v }` |
| `ConvertExpression` | expr, target_type | Type conversion `CONVERT(e, T)` |

### 4.3 New Statement Nodes

| Node | Fields | Purpose |
|------|--------|---------|
| `StructDecl` | name, fields (name + type) | Struct definition |
| `MapFuncDecl` | name, params, return_type, body | Conversion function |

## 5. Semantic Analysis Extensions

### 5.1 Type Resolution

- `LIST(T)` → `core::Type::Array(T)` (dynamic array)
- `TUPLE(T1,T2,...)` → `core::Type::Tuple({T1, T2, ...})`
- `DICT(K,V)` → `core::Type::GenericInstance("dict", {K, V})`
- `OPTION(T)` → `core::Type::Optional(T)`
- `STRUCT Name` → `core::Type::Struct(Name)`

### 5.2 Validation

- **Struct**: No duplicate field names, all field types valid
- **MAP_FUNC**: Parameter and return types valid, registered as conversion function
- **CONVERT**: Source type known, target type valid, conversion path exists
- **Container literals**: Elements have compatible types

## 6. IR Lowering Extensions

### 6.1 Container Type IR Representation

```
LIST(T)           → ptr (pointer to runtime list descriptor)
TUPLE(T1,T2,...)  → struct { T1, T2, ... } or ptr
DICT(K,V)         → ptr (pointer to runtime dict descriptor)
OPTION(T)         → struct { i1 has_value, T value }
STRUCT Name       → struct { fields... }
```

### 6.2 Lowering Rules

| Construct | IR Output |
|-----------|-----------|
| List literal `[a, b]` | `__ploy_rt_list_create` + `__ploy_rt_list_push` per element |
| Tuple literal `(a, b)` | Allocate struct + store elements |
| Struct literal | Allocate struct + store fields |
| `CONVERT(e, T)` | Call to `__ploy_convert_<type>` or MAP_FUNC |
| `MAP_FUNC` decl | Generate `__ploy_mapfunc_<name>` IR function |

## 7. Runtime Container Marshalling

### 7.1 Data Structures

| Structure | Layout | Purpose |
|-----------|--------|---------|
| RuntimeList | count, capacity, elem_size, data ptr | Dynamic array |
| RuntimeTuple | num_elements, offsets array, packed data | Fixed-size heterogeneous |
| RuntimeDict | count, bucket_count, key_size, value_size, buckets | Hash table |

### 7.2 Runtime Functions

```cpp
// List operations
void *__ploy_rt_list_create(size_t elem_size, size_t initial_capacity);
void  __ploy_rt_list_push(void *list, const void *elem);
void *__ploy_rt_list_get(void *list, size_t index);
size_t __ploy_rt_list_len(void *list);
void  __ploy_rt_list_free(void *list);

// Tuple operations
void *__ploy_rt_tuple_create(size_t num_elements, const size_t *elem_sizes);
void *__ploy_rt_tuple_get(void *tuple, size_t index);
void  __ploy_rt_tuple_free(void *tuple);

// Dict operations
void *__ploy_rt_dict_create(size_t key_size, size_t value_size);
void  __ploy_rt_dict_insert(void *dict, const void *key, const void *value);
void *__ploy_rt_dict_lookup(void *dict, const void *key);
size_t __ploy_rt_dict_len(void *dict);
void  __ploy_rt_dict_free(void *dict);

// Cross-language conversion
void *__ploy_rt_convert_list_to_pylist(void *list);
void *__ploy_rt_convert_pylist_to_list(void *pylist, size_t elem_size);
void *__ploy_rt_convert_dict_to_pydict(void *dict);
void *__ploy_rt_convert_pydict_to_dict(void *pydict, size_t key_size, size_t value_size);
void *__ploy_rt_convert_vec_to_list(void *vec, size_t elem_size);
void *__ploy_rt_convert_list_to_vec(void *list, size_t elem_size);
```

## 8. Linker Extensions

### 8.1 Container Marshalling in Glue Stubs

The `PolyglotLinker` emits marshalling code for container type parameters:

| Conversion                    | Strategy                                              |
|-------------------------------|-------------------------------------------------------|
| `LIST(T)` → `python::list`   | Iterate + convert each element via Python C API       |
| `LIST(T)` → `rust::Vec<T>`   | Direct memory copy if layout-compatible, else iterate |
| `TUPLE(...)` → `python::tuple`| Pack elements into Python tuple via C API             |
| `DICT(K,V)` → `python::dict` | Iterate entries + convert via Python C API            |
| `STRUCT` → `STRUCT`          | Field-by-field conversion with recursive marshalling  |
| `OPTION(T)` → `rust::Option<T>` | Check discriminant + convert inner value           |

## 9. Usage Examples

### 9.1 Cross-Language List Processing

```ploy
IMPORT python PACKAGE numpy AS np;
IMPORT cpp::math_engine;

MAP_TYPE(python::list, cpp::std::vector_double);
MAP_TYPE(cpp::double, python::float);

LINK(cpp, python, math_engine::compute_stats, np::mean) {
    MAP_TYPE(python::list, cpp::std::vector_double);
}

PIPELINE analyze {
    FUNC load() -> LIST(f64) {
        LET data = CALL(python, np::loadtxt, "input.csv");
        RETURN data;
    }

    FUNC compute(data: LIST(f64)) -> f64 {
        LET result = CALL(cpp, math_engine::compute_stats, data);
        RETURN result;
    }
}
```

### 9.2 Cross-Language Struct Mapping

```ploy
STRUCT CppPoint {
    x: f64;
    y: f64;
}

MAP_FUNC to_complex(x: f64) -> TUPLE(f64, f64) {
    RETURN (x, 0.0);
}

LINK(cpp, python, geometry::distance, point_gen::make_point) AS STRUCT {
    MAP_TYPE(cpp::double, python::float);
}
```

### 9.3 Optional Types

```ploy
FUNC safe_divide(a: f64, b: f64) -> OPTION(f64) {
    IF b == 0.0 {
        RETURN NULL;
    }
    RETURN a / b;
}

PIPELINE safe_compute {
    FUNC run() -> f64 {
        LET result = safe_divide(10.0, 0.0);
        MATCH result {
            CASE NULL => {
                RETURN -1.0;
            }
            DEFAULT => {
                RETURN CONVERT(result, f64);
            }
        }
    }
}
```
