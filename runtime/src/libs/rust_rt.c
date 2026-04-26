/**
 * @file     rust_rt.c
 * @brief    Implementation of the Rust runtime support library.
 *
 * @details  Rust crates participate in polyglot programs as `cdylib` shared
 *           libraries.  This bridge therefore provides a thin loader plus
 *           call trampolines around the system dynamic linker; no Rust
 *           toolchain or std is required at build time.  Cross-FFI panics
 *           are undefined behaviour by Rust convention, so every richer
 *           call shape returns the explicit `polyglot_rust_result_t` ABI
 *           struct that the crate populates after running its body inside
 *           `std::panic::catch_unwind`.
 *
 * @ingroup  Runtime / Libs
 * @author   Manning Cyrus
 * @date     2026-04-26
 */

#include "runtime/include/libs/rust_rt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "runtime/include/libs/base.h"
#include "runtime/include/memory/polyglot_alloc.h"

#ifdef _WIN32
#include <windows.h>
typedef HMODULE rs_lib_t;
#define RS_LOAD(path)     LoadLibraryA(path)
#define RS_SYM(lib, name) ((void *)GetProcAddress((lib), (name)))
#define RS_UNLOAD(lib)    FreeLibrary(lib)
#else
#include <dlfcn.h>
typedef void *rs_lib_t;
#define RS_LOAD(path)     dlopen((path), RTLD_LAZY | RTLD_GLOBAL)
#define RS_SYM(lib, name) dlsym((lib), (name))
#define RS_UNLOAD(lib)    dlclose(lib)
#endif

// ----------------------------------------------------------------------------
// Loaded crate registry.  We keep an explicit list so polyglot_rust_shutdown
// can dlclose everything cleanly even when callers forget to unload.
// ----------------------------------------------------------------------------

typedef struct rs_crate_node {
  rs_lib_t handle;
  struct rs_crate_node *next;
} rs_crate_node_t;

static rs_crate_node_t *rs_crates_ = NULL;
static int rs_version_hint_ = 0;

static void register_crate(rs_lib_t h) {
  rs_crate_node_t *n =
      (rs_crate_node_t *)polyglot_raw_calloc(1, sizeof(*n));
  if (!n) return;
  n->handle = h;
  n->next = rs_crates_;
  rs_crates_ = n;
}

static int unregister_crate(rs_lib_t h) {
  rs_crate_node_t **pp = &rs_crates_;
  while (*pp) {
    if ((*pp)->handle == h) {
      rs_crate_node_t *gone = *pp;
      *pp = gone->next;
      polyglot_raw_free(gone);
      return 1;
    }
    pp = &(*pp)->next;
  }
  return 0;
}

// ----------------------------------------------------------------------------
// Slice descriptor — `(ptr, len, elem_size)` triple on the raw heap.
// Layout chosen to match what `core::slice::from_raw_parts` expects on the
// Rust side once the crate reads `(ptr, len)` from the struct.
// ----------------------------------------------------------------------------

typedef struct {
  const void *ptr;
  size_t len;
  size_t elem_size;
} polyglot_rust_slice_t;

// ----------------------------------------------------------------------------
// Lifecycle
// ----------------------------------------------------------------------------

int polyglot_rust_init(int version_hint) {
  rs_version_hint_ = version_hint;
  return 0;
}

void polyglot_rust_shutdown(void) {
  while (rs_crates_) {
    rs_crate_node_t *gone = rs_crates_;
    rs_crates_ = gone->next;
    if (gone->handle) RS_UNLOAD(gone->handle);
    polyglot_raw_free(gone);
  }
  rs_version_hint_ = 0;
}

// ----------------------------------------------------------------------------
// Always-available helpers
// ----------------------------------------------------------------------------

void polyglot_rust_print(const char *message) {
  if (!message) return;
  printf("%s\n", message);
}

char *polyglot_rust_strdup_gc(const char *message, void ***root_handle_out) {
  if (!message) return NULL;
  size_t len = strlen(message) + 1;
  char *buf = (char *)polyglot_alloc(len);
  if (!buf) return NULL;
  memcpy(buf, message, len);
  polyglot_gc_register_root((void **)&buf);
  if (root_handle_out) *root_handle_out = (void **)&buf;
  return buf;
}

void polyglot_rust_release(char **ptr, void ***root_handle) {
  if (!ptr || !*ptr) return;
  polyglot_gc_unregister_root((void **)ptr);
  *ptr = NULL;
  if (root_handle) *root_handle = NULL;
}

// ----------------------------------------------------------------------------
// cdylib loading
// ----------------------------------------------------------------------------

void *polyglot_rust_load_crate(const char *path) {
  if (!path || !path[0]) return NULL;

  rs_lib_t h = RS_LOAD(path);
  if (h) {
    register_crate(h);
    return (void *)h;
  }

  // Try with platform-specific extension when the caller passed a bare name.
  char buf[1024];
#ifdef _WIN32
  snprintf(buf, sizeof(buf), "%s.dll", path);
#elif defined(__APPLE__)
  snprintf(buf, sizeof(buf), "lib%s.dylib", path);
#else
  snprintf(buf, sizeof(buf), "lib%s.so", path);
#endif
  h = RS_LOAD(buf);
  if (h) {
    register_crate(h);
    return (void *)h;
  }

  fprintf(stderr, "[polyglot-rust] failed to load crate '%s'\n", path);
  return NULL;
}

void polyglot_rust_unload_crate(void *handle) {
  if (!handle) return;
  rs_lib_t h = (rs_lib_t)handle;
  if (unregister_crate(h)) RS_UNLOAD(h);
}

void *polyglot_rust_resolve(void *handle, const char *symbol) {
  if (!handle || !symbol) return NULL;
  return RS_SYM((rs_lib_t)handle, symbol);
}

// ----------------------------------------------------------------------------
// Call trampolines
// ----------------------------------------------------------------------------

polyglot_rust_result_t polyglot_rust_call(polyglot_rust_call_fn fn,
                                          const void *const *argv, int argc) {
  polyglot_rust_result_t err = {0, NULL, "polyglot_rust_call: null fn"};
  if (!fn) return err;
  return fn(argv, argc);
}

long long polyglot_rust_call_i64(polyglot_rust_i64_fn fn, long long arg) {
  return fn ? fn(arg) : 0;
}

double polyglot_rust_call_f64(polyglot_rust_f64_fn fn, double arg) {
  return fn ? fn(arg) : 0.0;
}

char *polyglot_rust_call_str(polyglot_rust_str_fn fn, const char *arg,
                             void ***root_handle_out) {
  if (!fn) return NULL;
  const char *out = fn(arg);
  if (!out) return NULL;
  return polyglot_rust_strdup_gc(out, root_handle_out);
}

// ----------------------------------------------------------------------------
// Slice helpers
// ----------------------------------------------------------------------------

void *polyglot_rust_slice_make(const void *ptr, size_t len, size_t elem_size) {
  polyglot_rust_slice_t *s =
      (polyglot_rust_slice_t *)polyglot_raw_calloc(1, sizeof(*s));
  if (!s) return NULL;
  s->ptr = ptr;
  s->len = len;
  s->elem_size = elem_size;
  return s;
}

void polyglot_rust_slice_destroy(void *slice) {
  if (slice) polyglot_raw_free(slice);
}
