#pragma once

#include <stddef.h>

void *polyglot_memcpy(void *dest, const void *src, size_t size);
int polyglot_memcmp(const void *lhs, const void *rhs, size_t size);

// GC-backed allocation helpers exposed with C linkage for runtimes.
void *polyglot_alloc(size_t size);
void polyglot_gc_collect();
void polyglot_gc_register_root(void **slot);
void polyglot_gc_unregister_root(void **slot);

// Convenience for C callers to allocate and keep a root alive for the caller scope (uses cleanup when available).
void *polyglot_alloc_rooted(size_t size);
