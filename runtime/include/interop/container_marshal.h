#pragma once

#include <cstddef>
#include <cstdint>

/// @file container_marshal.h
/// @brief Runtime container data structures and marshalling functions
///        for cross-language container type conversion.

namespace polyglot::runtime::interop {

// ============================================================================
// Runtime Container Descriptors
// ============================================================================

/// Dynamically-sized list descriptor used as the in-memory representation
/// for LIST[T] values at runtime.
struct RuntimeList {
    std::size_t count{0};
    std::size_t capacity{0};
    std::size_t elem_size{0};
    void *data{nullptr};
};

/// Heterogeneous tuple descriptor.  Element offsets are computed from
/// element sizes and stored for O(1) access.
struct RuntimeTuple {
    std::size_t num_elements{0};
    std::size_t *offsets{nullptr};
    void *data{nullptr};
};

/// Hash-map descriptor for DICT[K,V] values at runtime.
struct RuntimeDictEntry {
    void *key{nullptr};
    void *value{nullptr};
    RuntimeDictEntry *next{nullptr};
};

struct RuntimeDict {
    std::size_t count{0};
    std::size_t bucket_count{0};
    std::size_t key_size{0};
    std::size_t value_size{0};
    RuntimeDictEntry **buckets{nullptr};
};

// ============================================================================
// List Operations
// ============================================================================

/// Create a new runtime list.
/// @param elem_size      Size of each element in bytes.
/// @param initial_capacity  Pre-allocated element count.
/// @return Opaque pointer to a RuntimeList.
void *__ploy_rt_list_create(std::size_t elem_size, std::size_t initial_capacity);

/// Push an element onto the end of the list.
void __ploy_rt_list_push(void *list, const void *elem);

/// Retrieve a pointer to the element at the given index.
void *__ploy_rt_list_get(void *list, std::size_t index);

/// Return the current element count.
std::size_t __ploy_rt_list_len(void *list);

/// Free the list and its backing storage.
void __ploy_rt_list_free(void *list);

// ============================================================================
// Tuple Operations
// ============================================================================

/// Create a tuple with the given element sizes.
void *__ploy_rt_tuple_create(std::size_t num_elements, const std::size_t *elem_sizes);

/// Get a pointer to the element at the given index.
void *__ploy_rt_tuple_get(void *tuple, std::size_t index);

/// Free the tuple.
void __ploy_rt_tuple_free(void *tuple);

// ============================================================================
// Dict Operations
// ============================================================================

/// Create a new runtime dictionary.
void *__ploy_rt_dict_create(std::size_t key_size, std::size_t value_size);

/// Insert a key-value pair.
void __ploy_rt_dict_insert(void *dict, const void *key, const void *value);

/// Look up a value by key.  Returns nullptr if not found.
void *__ploy_rt_dict_lookup(void *dict, const void *key);

/// Return the number of entries.
std::size_t __ploy_rt_dict_len(void *dict);

/// Free the dictionary and all entries.
void __ploy_rt_dict_free(void *dict);

// ============================================================================
// Cross-Language Container Conversion
// ============================================================================

/// Convert a RuntimeList into a Python list (opaque PyObject*).
void *__ploy_rt_convert_list_to_pylist(void *list);

/// Convert a Python list into a RuntimeList.
void *__ploy_rt_convert_pylist_to_list(void *pylist, std::size_t elem_size);

/// Convert a RuntimeDict into a Python dict (opaque PyObject*).
void *__ploy_rt_convert_dict_to_pydict(void *dict);

/// Convert a Python dict into a RuntimeDict.
void *__ploy_rt_convert_pydict_to_dict(void *pydict, std::size_t key_size, std::size_t value_size);

/// Convert a Rust Vec into a RuntimeList (assumes contiguous layout).
void *__ploy_rt_convert_vec_to_list(void *vec, std::size_t elem_size);

/// Convert a RuntimeList into a Rust Vec.
void *__ploy_rt_convert_list_to_vec(void *list, std::size_t elem_size);

}  // namespace polyglot::runtime::interop
