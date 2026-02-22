#include "runtime/include/interop/container_marshal.h"

#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace polyglot::runtime::interop {

// ============================================================================
// Internal Helpers
// ============================================================================

namespace {

/// FNV-1a hash for arbitrary bytes (used by the dict implementation).
static std::size_t HashBytes(const void *data, std::size_t size) {
    const auto *bytes = static_cast<const std::uint8_t *>(data);
    std::size_t hash = 0xcbf29ce484222325ULL;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

/// Grow a RuntimeList when capacity is exhausted.
static void ListGrow(RuntimeList *list) {
    std::size_t new_cap = (list->capacity == 0) ? 8 : list->capacity * 2;
    void *new_data = std::realloc(list->data, new_cap * list->elem_size);
    if (new_data) {
        list->data = new_data;
        list->capacity = new_cap;
    }
}

} // anonymous namespace

// ============================================================================
// List Operations
// ============================================================================

void *__ploy_rt_list_create(std::size_t elem_size, std::size_t initial_capacity) {
    auto *list = static_cast<RuntimeList *>(std::calloc(1, sizeof(RuntimeList)));
    if (!list) return nullptr;
    list->elem_size = elem_size;
    list->capacity = initial_capacity;
    if (initial_capacity > 0) {
        list->data = std::calloc(initial_capacity, elem_size);
    }
    return list;
}

void __ploy_rt_list_push(void *raw, const void *elem) {
    if (!raw || !elem) return;
    auto *list = static_cast<RuntimeList *>(raw);
    if (list->count >= list->capacity) {
        ListGrow(list);
    }
    void *dst = static_cast<char *>(list->data) + list->count * list->elem_size;
    std::memcpy(dst, elem, list->elem_size);
    ++list->count;
}

void *__ploy_rt_list_get(void *raw, std::size_t index) {
    if (!raw) return nullptr;
    auto *list = static_cast<RuntimeList *>(raw);
    if (index >= list->count) return nullptr;
    return static_cast<char *>(list->data) + index * list->elem_size;
}

std::size_t __ploy_rt_list_len(void *raw) {
    if (!raw) return 0;
    return static_cast<RuntimeList *>(raw)->count;
}

void __ploy_rt_list_free(void *raw) {
    if (!raw) return;
    auto *list = static_cast<RuntimeList *>(raw);
    std::free(list->data);
    std::free(list);
}

// ============================================================================
// Tuple Operations
// ============================================================================

void *__ploy_rt_tuple_create(std::size_t num_elements, const std::size_t *elem_sizes) {
    if (num_elements == 0 || !elem_sizes) return nullptr;

    auto *tuple = static_cast<RuntimeTuple *>(std::calloc(1, sizeof(RuntimeTuple)));
    if (!tuple) return nullptr;
    tuple->num_elements = num_elements;
    tuple->offsets = static_cast<std::size_t *>(std::calloc(num_elements, sizeof(std::size_t)));

    // Compute total data size and element offsets with natural alignment
    std::size_t total_size = 0;
    for (std::size_t i = 0; i < num_elements; ++i) {
        // Align to element size (up to 8 bytes)
        std::size_t align = elem_sizes[i] > 8 ? 8 : elem_sizes[i];
        if (align == 0) align = 1;
        total_size = (total_size + align - 1) & ~(align - 1);
        tuple->offsets[i] = total_size;
        total_size += elem_sizes[i];
    }

    tuple->data = std::calloc(1, total_size);
    return tuple;
}

void *__ploy_rt_tuple_get(void *raw, std::size_t index) {
    if (!raw) return nullptr;
    auto *tuple = static_cast<RuntimeTuple *>(raw);
    if (index >= tuple->num_elements) return nullptr;
    return static_cast<char *>(tuple->data) + tuple->offsets[index];
}

void __ploy_rt_tuple_free(void *raw) {
    if (!raw) return;
    auto *tuple = static_cast<RuntimeTuple *>(raw);
    std::free(tuple->offsets);
    std::free(tuple->data);
    std::free(tuple);
}

// ============================================================================
// Dict Operations
// ============================================================================

void *__ploy_rt_dict_create(std::size_t key_size, std::size_t value_size) {
    auto *dict = static_cast<RuntimeDict *>(std::calloc(1, sizeof(RuntimeDict)));
    if (!dict) return nullptr;
    dict->key_size = key_size;
    dict->value_size = value_size;
    dict->bucket_count = 16; // Initial bucket count
    dict->buckets = static_cast<RuntimeDictEntry **>(
        std::calloc(dict->bucket_count, sizeof(RuntimeDictEntry *)));
    return dict;
}

void __ploy_rt_dict_insert(void *raw, const void *key, const void *value) {
    if (!raw || !key || !value) return;
    auto *dict = static_cast<RuntimeDict *>(raw);

    std::size_t hash = HashBytes(key, dict->key_size);
    std::size_t bucket = hash % dict->bucket_count;

    // Check for existing key and update
    for (RuntimeDictEntry *entry = dict->buckets[bucket]; entry; entry = entry->next) {
        if (std::memcmp(entry->key, key, dict->key_size) == 0) {
            std::memcpy(entry->value, value, dict->value_size);
            return;
        }
    }

    // Insert new entry
    auto *entry = static_cast<RuntimeDictEntry *>(std::calloc(1, sizeof(RuntimeDictEntry)));
    entry->key = std::malloc(dict->key_size);
    entry->value = std::malloc(dict->value_size);
    std::memcpy(entry->key, key, dict->key_size);
    std::memcpy(entry->value, value, dict->value_size);
    entry->next = dict->buckets[bucket];
    dict->buckets[bucket] = entry;
    ++dict->count;
}

void *__ploy_rt_dict_lookup(void *raw, const void *key) {
    if (!raw || !key) return nullptr;
    auto *dict = static_cast<RuntimeDict *>(raw);

    std::size_t hash = HashBytes(key, dict->key_size);
    std::size_t bucket = hash % dict->bucket_count;

    for (RuntimeDictEntry *entry = dict->buckets[bucket]; entry; entry = entry->next) {
        if (std::memcmp(entry->key, key, dict->key_size) == 0) {
            return entry->value;
        }
    }
    return nullptr;
}

std::size_t __ploy_rt_dict_len(void *raw) {
    if (!raw) return 0;
    return static_cast<RuntimeDict *>(raw)->count;
}

void __ploy_rt_dict_free(void *raw) {
    if (!raw) return;
    auto *dict = static_cast<RuntimeDict *>(raw);

    for (std::size_t i = 0; i < dict->bucket_count; ++i) {
        RuntimeDictEntry *entry = dict->buckets[i];
        while (entry) {
            RuntimeDictEntry *next = entry->next;
            std::free(entry->key);
            std::free(entry->value);
            std::free(entry);
            entry = next;
        }
    }

    std::free(dict->buckets);
    std::free(dict);
}

// ============================================================================
// Cross-Language Container Conversion
// ============================================================================

// Internal helper: dynamically load Python C API functions at runtime so that
// no hard link-time dependency on a specific CPython version is required.
// If the Python interpreter is not available the functions gracefully fall back
// to returning empty RuntimeList/RuntimeDict containers.
namespace {

#ifdef _WIN32
typedef HMODULE py_lib_t;
#define PY_LOAD(path) LoadLibraryA(path)
#define PY_SYM(lib, name) ((void *)GetProcAddress((lib), (name)))
#else
typedef void *py_lib_t;
#define PY_LOAD(path) dlopen((path), RTLD_LAZY)
#define PY_SYM(lib, name) dlsym((lib), (name))
#endif

static py_lib_t py_lib_ = nullptr;
static bool py_init_attempted_ = false;

// CPython C API function pointers resolved at runtime.
typedef void *(*PyList_New_fn)(long);
typedef int   (*PyList_SetItem_fn)(void *, long, void *);
typedef void *(*PyList_GetItem_fn)(void *, long);
typedef long  (*PyList_Size_fn)(void *);
typedef void *(*PyLong_FromLongLong_fn)(long long);
typedef long long (*PyLong_AsLongLong_fn)(void *);
typedef void *(*PyDict_New_fn)();
typedef int   (*PyDict_SetItem_fn)(void *, void *, void *);
typedef void *(*PyDict_GetItem_fn)(void *, void *);
typedef long  (*PyDict_Size_fn)(void *);
typedef void *(*PyDict_Keys_fn)(void *);
typedef void *(*PyBytes_FromStringAndSize_fn)(const char *, long);
typedef char *(*PyBytes_AsString_fn)(void *);
typedef long  (*PyBytes_Size_fn)(void *);

static PyList_New_fn              py_list_new_        = nullptr;
static PyList_SetItem_fn          py_list_setitem_    = nullptr;
static PyList_GetItem_fn          py_list_getitem_    = nullptr;
static PyList_Size_fn             py_list_size_       = nullptr;
static PyLong_FromLongLong_fn     py_long_from_       = nullptr;
static PyLong_AsLongLong_fn       py_long_as_         = nullptr;
static PyDict_New_fn              py_dict_new_        = nullptr;
static PyDict_SetItem_fn          py_dict_setitem_    = nullptr;
static PyDict_GetItem_fn          py_dict_getitem_    = nullptr;
static PyDict_Size_fn             py_dict_size_       = nullptr;
static PyDict_Keys_fn             py_dict_keys_       = nullptr;
static PyBytes_FromStringAndSize_fn py_bytes_from_    = nullptr;
static PyBytes_AsString_fn        py_bytes_as_        = nullptr;
static PyBytes_Size_fn            py_bytes_size_      = nullptr;

static bool EnsurePythonLoaded() {
    if (py_init_attempted_) return py_lib_ != nullptr;
    py_init_attempted_ = true;

#ifdef _WIN32
    py_lib_ = PY_LOAD("python3.dll");
    if (!py_lib_) py_lib_ = PY_LOAD("python310.dll");
    if (!py_lib_) py_lib_ = PY_LOAD("python311.dll");
    if (!py_lib_) py_lib_ = PY_LOAD("python312.dll");
#elif defined(__APPLE__)
    py_lib_ = PY_LOAD("libpython3.dylib");
#else
    py_lib_ = PY_LOAD("libpython3.so");
    if (!py_lib_) py_lib_ = PY_LOAD("libpython3.10.so");
    if (!py_lib_) py_lib_ = PY_LOAD("libpython3.11.so");
#endif

    if (!py_lib_) return false;

    py_list_new_     = (PyList_New_fn)PY_SYM(py_lib_, "PyList_New");
    py_list_setitem_ = (PyList_SetItem_fn)PY_SYM(py_lib_, "PyList_SetItem");
    py_list_getitem_ = (PyList_GetItem_fn)PY_SYM(py_lib_, "PyList_GetItem");
    py_list_size_    = (PyList_Size_fn)PY_SYM(py_lib_, "PyList_Size");
    py_long_from_    = (PyLong_FromLongLong_fn)PY_SYM(py_lib_, "PyLong_FromLongLong");
    py_long_as_      = (PyLong_AsLongLong_fn)PY_SYM(py_lib_, "PyLong_AsLongLong");
    py_dict_new_     = (PyDict_New_fn)PY_SYM(py_lib_, "PyDict_New");
    py_dict_setitem_ = (PyDict_SetItem_fn)PY_SYM(py_lib_, "PyDict_SetItem");
    py_dict_getitem_ = (PyDict_GetItem_fn)PY_SYM(py_lib_, "PyDict_GetItem");
    py_dict_size_    = (PyDict_Size_fn)PY_SYM(py_lib_, "PyDict_Size");
    py_dict_keys_    = (PyDict_Keys_fn)PY_SYM(py_lib_, "PyDict_Keys");
    py_bytes_from_   = (PyBytes_FromStringAndSize_fn)PY_SYM(py_lib_, "PyBytes_FromStringAndSize");
    py_bytes_as_     = (PyBytes_AsString_fn)PY_SYM(py_lib_, "PyBytes_AsString");
    py_bytes_size_   = (PyBytes_Size_fn)PY_SYM(py_lib_, "PyBytes_Size");

    return true;
}

} // anonymous namespace

void *__ploy_rt_convert_list_to_pylist(void *list) {
    if (!list) return nullptr;

    auto *rl = static_cast<RuntimeList *>(list);
    if (!EnsurePythonLoaded() || !py_list_new_ || !py_list_setitem_) {
        // CPython not available: wrap the RuntimeList pointer into a
        // ForeignHandle structure so it can be passed opaquely.
        // The caller is responsible for interpreting the handle.
        return list;
    }

    // Create a Python list with the same element count.
    void *pylist = py_list_new_(static_cast<long>(rl->count));
    if (!pylist) return nullptr;

    // Copy each element into the Python list.
    // For elements that are integer-sized (up to 8 bytes) we convert them
    // to PyLong objects.  For larger elements we wrap them as PyBytes.
    for (std::size_t i = 0; i < rl->count; ++i) {
        void *elem_ptr = static_cast<char *>(rl->data) + i * rl->elem_size;
        void *py_obj = nullptr;

        if (rl->elem_size <= 8 && py_long_from_) {
            // Treat the element as a (possibly smaller) integer value.
            long long val = 0;
            std::memcpy(&val, elem_ptr, rl->elem_size);
            py_obj = py_long_from_(val);
        } else if (py_bytes_from_) {
            py_obj = py_bytes_from_(static_cast<const char *>(elem_ptr),
                                    static_cast<long>(rl->elem_size));
        }

        if (py_obj) {
            py_list_setitem_(pylist, static_cast<long>(i), py_obj);
        }
    }

    return pylist;
}

void *__ploy_rt_convert_pylist_to_list(void *pylist, std::size_t elem_size) {
    if (!pylist) return __ploy_rt_list_create(elem_size, 0);

    if (!EnsurePythonLoaded() || !py_list_size_ || !py_list_getitem_) {
        // CPython not available: return an empty list as a safe fallback.
        return __ploy_rt_list_create(elem_size, 0);
    }

    long count = py_list_size_(pylist);
    if (count <= 0) return __ploy_rt_list_create(elem_size, 0);

    void *list = __ploy_rt_list_create(elem_size, static_cast<std::size_t>(count));
    auto *rl = static_cast<RuntimeList *>(list);
    if (!rl) return nullptr;

    for (long i = 0; i < count; ++i) {
        void *py_obj = py_list_getitem_(pylist, i);
        if (!py_obj) continue;

        char buf[8] = {0};
        if (elem_size <= 8 && py_long_as_) {
            long long val = py_long_as_(py_obj);
            std::memcpy(buf, &val, elem_size);
            __ploy_rt_list_push(list, buf);
        } else if (py_bytes_as_ && py_bytes_size_) {
            char *data = py_bytes_as_(py_obj);
            long size = py_bytes_size_(py_obj);
            if (data && static_cast<std::size_t>(size) >= elem_size) {
                __ploy_rt_list_push(list, data);
            } else {
                __ploy_rt_list_push(list, buf);
            }
        } else {
            __ploy_rt_list_push(list, buf);
        }
    }

    return list;
}

void *__ploy_rt_convert_dict_to_pydict(void *dict) {
    if (!dict) return nullptr;

    auto *rd = static_cast<RuntimeDict *>(dict);
    if (!EnsurePythonLoaded() || !py_dict_new_ || !py_dict_setitem_) {
        return dict;
    }

    void *pydict = py_dict_new_();
    if (!pydict) return nullptr;

    // Iterate all buckets and convert each key-value pair.
    for (std::size_t b = 0; b < rd->bucket_count; ++b) {
        RuntimeDictEntry *entry = rd->buckets[b];
        while (entry) {
            void *py_key = nullptr;
            void *py_val = nullptr;

            if (rd->key_size <= 8 && py_long_from_) {
                long long k = 0;
                std::memcpy(&k, entry->key, rd->key_size);
                py_key = py_long_from_(k);
            } else if (py_bytes_from_) {
                py_key = py_bytes_from_(static_cast<const char *>(entry->key),
                                        static_cast<long>(rd->key_size));
            }

            if (rd->value_size <= 8 && py_long_from_) {
                long long v = 0;
                std::memcpy(&v, entry->value, rd->value_size);
                py_val = py_long_from_(v);
            } else if (py_bytes_from_) {
                py_val = py_bytes_from_(static_cast<const char *>(entry->value),
                                        static_cast<long>(rd->value_size));
            }

            if (py_key && py_val) {
                py_dict_setitem_(pydict, py_key, py_val);
            }

            entry = entry->next;
        }
    }

    return pydict;
}

void *__ploy_rt_convert_pydict_to_dict(void *pydict, std::size_t key_size, std::size_t value_size) {
    if (!pydict) return __ploy_rt_dict_create(key_size, value_size);

    if (!EnsurePythonLoaded() || !py_dict_size_ || !py_dict_keys_ || !py_dict_getitem_) {
        return __ploy_rt_dict_create(key_size, value_size);
    }

    void *dict = __ploy_rt_dict_create(key_size, value_size);

    void *keys = py_dict_keys_(pydict);
    if (!keys || !py_list_size_ || !py_list_getitem_) return dict;

    long count = py_list_size_(keys);
    for (long i = 0; i < count; ++i) {
        void *py_key = py_list_getitem_(keys, i);
        void *py_val = py_dict_getitem_(pydict, py_key);
        if (!py_key || !py_val) continue;

        char kbuf[8] = {0};
        char vbuf[8] = {0};

        if (key_size <= 8 && py_long_as_) {
            long long k = py_long_as_(py_key);
            std::memcpy(kbuf, &k, key_size);
        }
        if (value_size <= 8 && py_long_as_) {
            long long v = py_long_as_(py_val);
            std::memcpy(vbuf, &v, value_size);
        }

        __ploy_rt_dict_insert(dict, kbuf, vbuf);
    }

    return dict;
}

void *__ploy_rt_convert_vec_to_list(void *vec, std::size_t elem_size) {
    // A Rust Vec<T> in memory is { ptr, len, cap }.
    // Extract the data pointer and length, then bulk-copy into a RuntimeList.
    if (!vec) return nullptr;

    struct RustVecLayout {
        void *ptr;
        std::size_t len;
        std::size_t cap;
    };
    auto *rv = static_cast<RustVecLayout *>(vec);

    void *list = __ploy_rt_list_create(elem_size, rv->len);
    auto *rl = static_cast<RuntimeList *>(list);
    if (rl && rv->ptr && rv->len > 0) {
        std::memcpy(rl->data, rv->ptr, rv->len * elem_size);
        rl->count = rv->len;
    }
    return list;
}

void *__ploy_rt_convert_list_to_vec(void *list, std::size_t elem_size) {
    if (!list) return nullptr;
    auto *rl = static_cast<RuntimeList *>(list);

    // Allocate Rust Vec layout: { ptr, len, cap }
    struct RustVecLayout {
        void *ptr;
        std::size_t len;
        std::size_t cap;
    };

    auto *rv = static_cast<RustVecLayout *>(std::calloc(1, sizeof(RustVecLayout)));
    if (!rv) return nullptr;

    rv->len = rl->count;
    rv->cap = rl->count;
    rv->ptr = std::malloc(rl->count * elem_size);
    if (rv->ptr && rl->data) {
        std::memcpy(rv->ptr, rl->data, rl->count * elem_size);
    }
    return rv;
}

void *__ploy_rt_convert_cppvec_to_list(void *vec_data, std::size_t count, std::size_t elem_size) {
    // Convert a C++ std::vector's contiguous buffer into a RuntimeList.
    // The caller extracts .data() and .size() from the std::vector and
    // passes them here so that no C++ ABI dependency is required.
    if (!vec_data || count == 0 || elem_size == 0) {
        return __ploy_rt_list_create(elem_size, 0);
    }

    void *list = __ploy_rt_list_create(elem_size, count);
    auto *rl = static_cast<RuntimeList *>(list);
    if (rl && rl->data) {
        std::memcpy(rl->data, vec_data, count * elem_size);
        rl->count = count;
    }
    return list;
}

void *__ploy_rt_convert_list_generic(void *src_data, std::size_t count, std::size_t elem_size) {
    // Generic fallback conversion: create a RuntimeList from any contiguous
    // memory buffer.  This is used when no specialized converter exists for
    // the source container type (e.g. arrays, slices, or foreign collections).
    if (!src_data || count == 0 || elem_size == 0) {
        return __ploy_rt_list_create(elem_size, 0);
    }

    void *list = __ploy_rt_list_create(elem_size, count);
    auto *rl = static_cast<RuntimeList *>(list);
    if (rl && rl->data) {
        std::memcpy(rl->data, src_data, count * elem_size);
        rl->count = count;
    }
    return list;
}

}  // namespace polyglot::runtime::interop
