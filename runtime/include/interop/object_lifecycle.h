#pragma once

#include <cstddef>
#include <cstdint>

/// @file object_lifecycle.h
/// @brief Language-specific object cleanup / destructor bridge functions and
///        cross-language registration helpers used by the .ploy lowering layer.
///
/// These symbols are emitted by the .ploy IR lowering pass (DELETE expression,
/// EXTEND declaration) and resolved at link time by the polyglot linker.

namespace polyglot::runtime::interop {

// ============================================================================
// Language-specific destructor / cleanup bridges
// ============================================================================

/// Release a Python object handle (decrements CPython refcount / invokes __del__).
/// @param object Opaque pointer to a Python object.
void __ploy_py_del(void *object);

/// Delete a C++ object previously allocated with NEW.
/// @param object Opaque pointer to the C++ object.  The destructor is called
///        and the memory is freed.
void __ploy_cpp_delete(void *object);

/// Drop a Rust object (invokes `drop` on the owned value).
/// @param object Opaque pointer to the Rust object.
void __ploy_rust_drop(void *object);

/// Release a Java object reference (JNI DeleteGlobalRef).
/// @param object Opaque pointer to a JNI global reference.
void __ploy_java_release(void *object);

/// Dispose a .NET object (calls IDisposable.Dispose / releases GCHandle).
/// @param object Opaque pointer to a .NET GCHandle.
void __ploy_dotnet_dispose(void *object);

// ============================================================================
// Cross-language class extension / vtable registration
// ============================================================================

/// Register a derived class extension across language boundaries.
///
/// When a .ploy module uses `EXTEND` to subclass a foreign class the lowering
/// pass emits a call to this function at module-init time so the runtime can
/// build the appropriate vtable / method-dispatch table.
///
/// @param language    Source language of the base class ("python", "cpp", ...).
/// @param base_class  Name of the base class.
/// @param derived     Name of the derived class (defined in .ploy).
void __ploy_extend_register(const char *language, const char *base_class,
                             const char *derived);

// ============================================================================
// Cross-language string / container conversion helpers
// ============================================================================

/// Convert a string from one language representation to another.
/// The source pointer is an opaque handle whose layout depends on the source
/// language (e.g. Python str, Rust &str slice, C++ std::string*).
/// Returns a newly-allocated C string (null-terminated) that the caller owns.
///
/// @param src   Opaque pointer to the source string object.
/// @param len   Length of the source string in bytes (0 = compute from src).
/// @return Pointer to a GC-managed null-terminated C string, or nullptr on error.
char *__ploy_rt_string_convert(const void *src, std::size_t len);

/// Byte-level memory copy exposed as a runtime symbol so the linker can emit
/// CALL relocations for large struct copies.
///
/// @param dst   Destination buffer.
/// @param src   Source buffer.
/// @param size  Number of bytes to copy.
void *__ploy_rt_memcpy(void *dst, const void *src, std::size_t size);

/// Convert a tuple from one language layout to another.
/// @param tuple  Pointer to a RuntimeTuple (or foreign tuple handle).
/// @return Pointer to the converted RuntimeTuple owned by the runtime.
void *__ploy_rt_convert_tuple(void *tuple);

/// Convert a dict from one language layout to another.
/// @param dict  Pointer to a RuntimeDict (or foreign dict handle).
/// @return Pointer to the converted RuntimeDict owned by the runtime.
void *__ploy_rt_dict_convert(void *dict);

/// Convert a struct from one language layout to another.
/// The source pointer describes a packed StructFieldDesc array followed by
/// data bytes.
/// @param src  Pointer to the source struct descriptor + data.
/// @return Pointer to the converted struct data owned by the runtime.
void *__ploy_rt_convert_struct(void *src);

}  // namespace polyglot::runtime::interop
