#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

void *polyglot_memcpy(void *dest, const void *src, size_t size);
int polyglot_memcmp(const void *lhs, const void *rhs, size_t size);
void *polyglot_memset(void *dest, int value, size_t size);
size_t polyglot_strlen(const char *s);
char *polyglot_strcpy(char *dest, const char *src);
char *polyglot_strncpy(char *dest, const char *src, size_t n);
int polyglot_strcmp(const char *lhs, const char *rhs);
int polyglot_strncmp(const char *lhs, const char *rhs, size_t n);

// GC-backed allocation helpers exposed with C linkage for runtimes.
#ifdef __cplusplus
extern "C" {
#endif

void *polyglot_alloc(size_t size);
void polyglot_gc_collect();
void polyglot_gc_register_root(void **slot);
void polyglot_gc_unregister_root(void **slot);

// Convenience for C callers to allocate and keep a root alive for the caller scope (uses cleanup when available).
void *polyglot_alloc_rooted(size_t size);

// Cross-language basic IO
void polyglot_println(const char *message);
bool polyglot_read_file(const char *path, char **out_buf, size_t *out_size);
bool polyglot_write_file(const char *path, const char *data, size_t size);
void polyglot_free_file_buffer(char *buf);

#ifdef __cplusplus
}
#endif
