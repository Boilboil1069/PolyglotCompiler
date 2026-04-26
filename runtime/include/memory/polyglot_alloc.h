/**
 * @file     polyglot_alloc.h
 * @brief    Project-wide raw allocator backed by mimalloc.
 *
 * @details  The runtime exposes two layers of memory management:
 *
 *           1. The garbage-collected heap reached through `polyglot_alloc`
 *              (declared in libs/base.h).  All managed objects flow through
 *              `gc::GlobalHeap().Allocate()` which is now backed by mimalloc.
 *
 *           2. The raw, manually-managed heap reached through the
 *              `polyglot_raw_*` family declared here.  These wrappers delegate
 *              directly to mimalloc and are intended for short-lived control
 *              blocks that the GC is not aware of (e.g. interop dictionaries,
 *              tuple offsets, FFI argument trampolines).
 *
 *           Both layers share the same underlying allocator, giving the entire
 *           runtime the throughput / fragmentation characteristics of mimalloc
 *           instead of the platform `malloc` family.
 *
 *           The header is C-compatible so the C runtime sources
 *           (`libs/*.c`) can use the same wrappers without pulling in the C++
 *           mimalloc API.
 *
 * @ingroup  Runtime / Memory
 * @author   Manning Cyrus
 * @date     2026-04-26
 */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Allocate `size` bytes from the raw mimalloc heap.  Returns `NULL` on failure.
void *polyglot_raw_malloc(size_t size);

/// Allocate and zero-initialise `count * size` bytes from mimalloc.
void *polyglot_raw_calloc(size_t count, size_t size);

/// Resize the block previously returned by `polyglot_raw_malloc` /
/// `polyglot_raw_calloc` / `polyglot_raw_realloc`.  Calling with `ptr == NULL`
/// is equivalent to `polyglot_raw_malloc(new_size)`; calling with
/// `new_size == 0` frees the block and returns `NULL`.
void *polyglot_raw_realloc(void *ptr, size_t new_size);

/// Free a block previously returned by any of the `polyglot_raw_*` allocators.
/// Passing `NULL` is a no-op.
void polyglot_raw_free(void *ptr);

/// Return the human-readable name of the active raw allocator.
/// Currently always returns the literal string `"mimalloc"`.
const char *polyglot_allocator_name(void);

/// Return the version string of the active raw allocator (e.g. `"2.1.7"`).
/// The pointer references a static buffer owned by the runtime.
const char *polyglot_allocator_version(void);

#ifdef __cplusplus
}  // extern "C"
#endif
