/**
 * @file     polyglot_alloc.cpp
 * @brief    mimalloc-backed implementation of the raw allocator C API.
 *
 * @ingroup  Runtime / Memory
 * @author   Manning Cyrus
 * @date     2026-04-26
 */
#include "runtime/include/memory/polyglot_alloc.h"

#include <mimalloc.h>

#include <cstdio>

extern "C" {

void *polyglot_raw_malloc(size_t size) {
  // Forward to mimalloc.  Returning NULL on failure matches the contract of
  // the standard `malloc` family that all callers expect.
  return mi_malloc(size);
}

void *polyglot_raw_calloc(size_t count, size_t size) {
  return mi_calloc(count, size);
}

void *polyglot_raw_realloc(void *ptr, size_t new_size) {
  return mi_realloc(ptr, new_size);
}

void polyglot_raw_free(void *ptr) {
  // mi_free is a safe no-op when called with NULL.
  mi_free(ptr);
}

const char *polyglot_allocator_name(void) { return "mimalloc"; }

const char *polyglot_allocator_version(void) {
  // mi_version() returns an integer in the form MAJOR*100 + MINOR*10 + PATCH.
  // We format it once on first call and cache the result in a static buffer.
  static char buffer[32];
  static bool initialised = false;
  if (!initialised) {
    int v = mi_version();
    int major = v / 100;
    int minor = (v / 10) % 10;
    int patch = v % 10;
    std::snprintf(buffer, sizeof(buffer), "%d.%d.%d", major, minor, patch);
    initialised = true;
  }
  return buffer;
}

}  // extern "C"
