/**
 * @file     rust_rt.h
 * @brief    Rust language runtime support library.
 *
 * @details  Bridges the polyglot runtime to compiled Rust artefacts.  Rust
 *           crates participate in polyglot programs as `cdylib` shared
 *           libraries that expose `extern "C"` symbols, so this layer is a
 *           dynamic-loader plus call trampoline (no Rust compiler / std is
 *           required at build time).  The trampolines move primitive
 *           arguments across the C ABI; richer values are exchanged via the
 *           explicit `polyglot_rust_result_t` struct that mirrors a Rust
 *           `Result<T, E>` once the crate has run `catch_unwind` on its
 *           side.  Supported `version_hint` values:
 *
 *               2018 / 2021 / 2024 -> Rust edition (informational only)
 *               0                  -> auto
 *
 * @ingroup  Runtime / Libs
 * @author   Manning Cyrus
 * @date     2026-04-26
 */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Rust-side `Result<*mut c_void, *const c_char>` mirror.  When ok != 0, the
// crate must populate `value`; otherwise it must populate `error` with a
// pointer to a NUL-terminated UTF-8 string that lives at least until the
// caller has had a chance to copy it.
typedef struct {
  int ok;             // 1 = success, 0 = failure
  void *value;        // success payload (opaque)
  const char *error;  // borrowed string; valid until next call on this thread
} polyglot_rust_result_t;

// ----- Lifecycle -----------------------------------------------------------

int polyglot_rust_init(int version_hint);
void polyglot_rust_shutdown(void);

// ----- Always-available helpers --------------------------------------------

void polyglot_rust_print(const char *message);
char *polyglot_rust_strdup_gc(const char *message, void ***root_handle_out);
void polyglot_rust_release(char **ptr, void ***root_handle);

// ----- cdylib loading ------------------------------------------------------

// Load a Rust cdylib (LoadLibrary / dlopen).  The path is platform-native;
// passing just the crate base name (e.g. `mycrate`) lets the runtime probe
// the host's standard search path with the right extension.
void *polyglot_rust_load_crate(const char *path);
void polyglot_rust_unload_crate(void *handle);

// Resolve an `extern "C"` symbol from a loaded crate.
void *polyglot_rust_resolve(void *handle, const char *symbol);

// ----- Call trampolines ----------------------------------------------------
//
// Each trampoline expects the loaded symbol to honour `extern "C"` and to
// have wrapped its body in `std::panic::catch_unwind` on the Rust side.  We
// cannot intercept Rust panics from C, so a panic that escapes the boundary
// is undefined behaviour — by convention every Polyglot-Rust ABI symbol
// returns a `polyglot_rust_result_t` to model fallible operations safely.

typedef polyglot_rust_result_t (*polyglot_rust_call_fn)(const void *const *argv, int argc);
typedef long long (*polyglot_rust_i64_fn)(long long);
typedef double (*polyglot_rust_f64_fn)(double);
typedef const char *(*polyglot_rust_str_fn)(const char *);

// Invoke a `polyglot_rust_call_fn`; the result is forwarded verbatim.
polyglot_rust_result_t polyglot_rust_call(polyglot_rust_call_fn fn,
                                          const void *const *argv, int argc);

// Convenience trampolines for primitive symbols.
long long polyglot_rust_call_i64(polyglot_rust_i64_fn fn, long long arg);
double polyglot_rust_call_f64(polyglot_rust_f64_fn fn, double arg);

// Borrow a NUL-terminated string returned by a Rust function and copy it
// onto the GC heap.  Releases via polyglot_rust_release.
char *polyglot_rust_call_str(polyglot_rust_str_fn fn, const char *arg,
                             void ***root_handle_out);

// Allocate a `(ptr, len)` pair on the raw heap so it can be passed across
// FFI to a Rust slice constructor.  `len` is in elements.
void *polyglot_rust_slice_make(const void *ptr, size_t len, size_t elem_size);
void polyglot_rust_slice_destroy(void *slice);

#ifdef __cplusplus
}
#endif
