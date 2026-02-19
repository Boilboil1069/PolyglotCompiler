#include "runtime/include/interop/container_marshal.h"

#include <cstdlib>
#include <cstring>

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

void *__ploy_rt_convert_list_to_pylist(void *list) {
    // This function would integrate with the Python C API to create a
    // PyListObject from a RuntimeList.  At link time, the Python interpreter
    // must be available.  For now, return the list pointer wrapped in a
    // ForeignHandle so the Python runtime can pick it up.
    return list; // Placeholder: actual CPython integration
}

void *__ploy_rt_convert_pylist_to_list(void *pylist, std::size_t elem_size) {
    // Reverse direction: iterate a PyListObject and copy elements into
    // a newly-created RuntimeList.
    (void)pylist;
    return __ploy_rt_list_create(elem_size, 0);
}

void *__ploy_rt_convert_dict_to_pydict(void *dict) {
    return dict; // Placeholder: actual CPython integration
}

void *__ploy_rt_convert_pydict_to_dict(void *pydict, std::size_t key_size, std::size_t value_size) {
    (void)pydict;
    return __ploy_rt_dict_create(key_size, value_size);
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

}  // namespace polyglot::runtime::interop
