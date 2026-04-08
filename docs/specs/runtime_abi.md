# Runtime C ABI Reference

This document specifies the **C-linkage ABI** exported by the PolyglotCompiler
runtime.  All symbols listed below use `extern "C"` linkage so that generated
code (regardless of source language) can call them through a flat, stable
binary interface.

The C++ API (classes, templates, namespaces) is the *implementation* layer;
the C ABI is the *contract* layer consumed by generated object code and the
polyglot linker.

---

## 1 Design Principles

| Principle | Details |
|-----------|---------|
| **Flat C symbols** | Every runtime entry point is `extern "C"` with a well-known mangled name. |
| **Opaque pointers** | Cross-language objects are passed as `void*`; the runtime reinterprets them. |
| **GC integration** | Allocations go through `polyglot_alloc`; roots must be registered. |
| **No exceptions** | The C ABI does not throw; errors are indicated by return values (`nullptr`, `false`). |

---

## 2 Core Services (`runtime/include/libs/base.h`)

| Symbol | Signature | Description |
|--------|-----------|-------------|
| `polyglot_alloc` | `void*(size_t)` | GC-backed allocation |
| `polyglot_gc_collect` | `void()` | Trigger garbage collection cycle |
| `polyglot_gc_register_root` | `void(void**)` | Register a GC root slot |
| `polyglot_gc_unregister_root` | `void(void**)` | Unregister a GC root slot |
| `polyglot_alloc_rooted` | `void*(size_t)` | Allocate + register root (convenience) |
| `polyglot_memcpy` | `void*(void*,const void*,size_t)` | Memory copy |
| `polyglot_memcmp` | `int(const void*,const void*,size_t)` | Memory compare |
| `polyglot_memset` | `void*(void*,int,size_t)` | Memory set |
| `polyglot_strlen` | `size_t(const char*)` | String length |
| `polyglot_strcpy` | `char*(char*,const char*)` | String copy |
| `polyglot_strncpy` | `char*(char*,const char*,size_t)` | Bounded string copy |
| `polyglot_strcmp` | `int(const char*,const char*)` | String compare |
| `polyglot_strncmp` | `int(const char*,const char*,size_t)` | Bounded string compare |
| `polyglot_println` | `void(const char*)` | Print a line to stdout |
| `polyglot_read_file` | `bool(const char*,char**,size_t*)` | Read file into buffer |
| `polyglot_write_file` | `bool(const char*,const char*,size_t)` | Write buffer to file |
| `polyglot_free_file_buffer` | `void(char*)` | Free buffer from `polyglot_read_file` |

---

## 3 Language Bridge Symbols

### 3.1 C++ Bridge (`runtime/include/libs/cpp_rt.h`)

| Symbol | Signature | Description |
|--------|-----------|-------------|
| `polyglot_cpp_print` | `void(const char*)` | Print from C++ context |
| `polyglot_cpp_strdup_gc` | `char*(const char*,void***)` | Duplicate string into GC memory |
| `polyglot_cpp_release` | `void(char**,void***)` | Release GC root |

### 3.2 Python Bridge (`runtime/include/libs/python_rt.h`)

| Symbol | Signature | Description |
|--------|-----------|-------------|
| `polyglot_python_strdup_gc` | `char*(const char*,void***)` | Duplicate string into GC memory |
| `polyglot_python_release` | `void(char**,void***)` | Release GC root |

### 3.3 Java Bridge (`runtime/include/libs/java_rt.h`)

Provides JNI initialisation, method invocation, and object lifecycle.

### 3.4 .NET Bridge (`runtime/include/libs/dotnet_rt.h`)

Provides CoreCLR host initialisation, managed method invocation, and object
lifecycle.

### 3.5 Rust Bridge (`runtime/include/libs/rust_rt.h`)

Provides FFI-compatible entry points for calling into Rust code.

---

## 4 Interop / Object Lifecycle (`runtime/include/interop/object_lifecycle.h`)

These `__ploy_*` symbols are emitted by the `.ploy` IR lowering pass and
resolved at link time:

| Symbol | Signature | Description |
|--------|-----------|-------------|
| `__ploy_py_del` | `void(void*)` | Release Python object (decref) |
| `__ploy_cpp_delete` | `void(void*)` | Delete C++ object |
| `__ploy_rust_drop` | `void(void*)` | Drop Rust value |
| `__ploy_java_release` | `void(void*)` | Release JNI global ref |
| `__ploy_dotnet_dispose` | `void(void*)` | Dispose .NET object |
| `__ploy_extend_register` | `void(const char*,const char*,const char*)` | Register cross-language class extension |
| `__ploy_rt_string_convert` | `char*(const void*,size_t)` | Cross-language string conversion |
| `__ploy_rt_memcpy` | `void*(void*,const void*,size_t)` | Runtime-level memcpy |
| `__ploy_rt_convert_tuple` | `void*(void*)` | Tuple layout conversion |
| `__ploy_rt_dict_convert` | `void*(void*)` | Dict layout conversion |
| `__ploy_rt_convert_struct` | `void*(void*)` | Struct layout conversion |

---

## 5 Container Marshalling (`runtime/include/interop/container_marshal.h`)

### 5.1 Data Structures

| Type | Fields | Description |
|------|--------|-------------|
| `RuntimeList` | `count`, `capacity`, `elem_size`, `data` | Dynamic list (`LIST[T]`) |
| `RuntimeTuple` | `num_elements`, `offsets`, `data` | Heterogeneous tuple |
| `RuntimeDict` | `count`, `bucket_count`, `key_size`, `value_size`, `buckets` | Hash map (`DICT[K,V]`) with automatic rehashing at 0.75 load factor |

**RuntimeDict Implementation Details:**
- Rehash triggered when `count / bucket_count >= 0.75`
- Bucket count grows exponentially (doubling) from initial `kMinBucketCount=16` up to `kMaxBucketCount=1<<30`
- Hash function: FNV-1a for string keys; identity for numeric keys
- Rehash recomputes all key hashes and redistributes entries into new buckets

### 5.2 List API

| Symbol | Signature |
|--------|-----------|
| `__ploy_rt_list_create` | `void*(size_t elem_size, size_t capacity)` |
| `__ploy_rt_list_push` | `void(void*,const void*)` |
| `__ploy_rt_list_get` | `void*(void*,size_t)` |
| `__ploy_rt_list_len` | `size_t(void*)` |
| `__ploy_rt_list_free` | `void(void*)` |

### 5.3 Tuple API

| Symbol | Signature |
|--------|-----------|
| `__ploy_rt_tuple_create` | `void*(size_t,const size_t*)` |
| `__ploy_rt_tuple_get` | `void*(void*,size_t)` |
| `__ploy_rt_tuple_free` | `void(void*)` |

### 5.4 Dict API

| Symbol | Signature |
|--------|-----------|
| `__ploy_rt_dict_create` | `void*(size_t,size_t)` |
| `__ploy_rt_dict_insert` | `void(void*,const void*,const void*)` |
| `__ploy_rt_dict_lookup` | `void*(void*,const void*)` |
| `__ploy_rt_dict_len` | `size_t(void*)` |
| `__ploy_rt_dict_free` | `void(void*)` |

---

## 5.5 Extension Registry (`runtime/include/interop/object_lifecycle.h`)

The extension registry manages cross-language class extensions via a thread-safe singleton.

### Implementation

- **Pattern**: Meyer's singleton (`static ExtensionRegistry &GetRegistry()`)
- **Storage**: `std::vector<ExtensionEntry>` instead of fixed-size global array
- **Thread Safety**: Protected by `std::mutex`
- **Duplicate Detection**: Registration checks for existing `(language, base_class, extension_name)` triples

### C++ API

```cpp
namespace polyglot::runtime::interop {

struct ExtensionEntry {
    std::string language;
    std::string base_class;
    std::string extension_name;
    void* extension_data;
};

class ExtensionRegistry {
public:
    static ExtensionRegistry &GetRegistry();
    bool Register(const std::string& lang,
                  const std::string& base,
                  const std::string& name,
                  void* data);
    std::optional<ExtensionEntry> Lookup(
        const std::string& lang,
        const std::string& base) const;
private:
    std::vector<ExtensionEntry> entries_;
    mutable std::mutex mutex_;
};

} // namespace polyglot::runtime::interop
```

### C ABI

| Symbol | Signature | Description |
|--------|-----------|-------------|
| `__ploy_extend_register` | `void(const char*,const char*,const char*)` | Register a cross-language class extension |

---

## 6 GC C++ API vs C ABI

| Concern | C++ API (`polyglot::runtime::gc::GC`) | C ABI (`polyglot_*`) |
|---------|---------------------------------------|----------------------|
| Allocation | `GC::Allocate(size)` | `polyglot_alloc(size)` |
| Collection | `GC::Collect()` | `polyglot_gc_collect()` |
| Root reg. | `GC::RegisterRoot(slot)` | `polyglot_gc_register_root(slot)` |
| Stats | `GC::GetStats()` → `GCStats` | Not exposed via C ABI |

The C ABI functions are thin wrappers that delegate to the singleton GC
instance.  Generated code always calls the C ABI; the C++ API is for
host-side tooling (`polyrt`, benchmarks).

---

## 7 Stability Guarantees

- **C ABI symbols** are stable across minor versions; removal requires a
  deprecation cycle.
- **C++ API** is internal and may change without notice.
- **`__ploy_*` symbols** are emitted by the lowering layer and consumed by
  the linker; they follow the same stability rules as C ABI symbols.
