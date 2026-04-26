/**
 * @file     cpp_rt.h
 * @brief    C++ language runtime support library.
 *
 * @details  Unlike the other language bridges, C++ is the host language of
 *           the polyglot runtime itself, so this layer does not embed a
 *           separate VM.  Instead it provides a uniform surface for talking
 *           to compiled C++ artefacts at run time: dynamically loading
 *           shared libraries, resolving (mangled or extern-C) symbols,
 *           invoking them with primitive arguments, and routing C++
 *           exceptions across the C ABI boundary back to the polyglot side.
 *
 *           The implementation lives in cpp_rt.cpp so it can use try/catch
 *           and, on POSIX, the Itanium C++ ABI's __cxa_demangle.
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

// ----- Lifecycle -----------------------------------------------------------

// Initialise C++ runtime helpers.  Currently this only resets the per-thread
// exception buffer.  Always returns 0; provided for API symmetry with the
// other language bridges.  `version_hint` is reserved (e.g. ABI year).
int polyglot_cpp_init(int version_hint);

// Tear down all loaded libraries and release per-thread state.
void polyglot_cpp_shutdown(void);

// ----- Always-available helpers --------------------------------------------

void polyglot_cpp_print(const char *message);
char *polyglot_cpp_strdup_gc(const char *message, void ***root_handle_out);
void polyglot_cpp_release(char **ptr, void ***root_handle);

// ----- Dynamic library bridge ---------------------------------------------

// Load a shared library (LoadLibrary on Windows, dlopen elsewhere).  Returns
// an opaque handle that must later be passed to polyglot_cpp_unload_library,
// or NULL on failure.
void *polyglot_cpp_load_library(const char *path);

// Unload a library previously returned by polyglot_cpp_load_library.
void polyglot_cpp_unload_library(void *handle);

// Resolve a symbol (extern "C" or already mangled) inside a loaded library.
void *polyglot_cpp_resolve_symbol(void *handle, const char *symbol);

// Demangle an Itanium / MSVC mangled name onto the GC heap.  The returned
// pointer is rooted (release via polyglot_cpp_release).  When the platform
// has no demangler available, the input is duplicated verbatim.
char *polyglot_cpp_demangle(const char *mangled, void ***root_handle_out);

// ----- Call trampolines (extern-C ABIs only) -------------------------------
//
// These helpers are guarded by try/catch on the C++ side so that a stray
// exception escaping a loaded library does not unwind through C frames in
// the polyglot runtime.  When an exception is caught it is converted to a
// string and stashed in the thread-local "last exception" slot, accessible
// via polyglot_cpp_last_exception().

typedef void (*polyglot_cpp_void_void_fn)(void);
typedef void (*polyglot_cpp_void_str_fn)(const char *);
typedef long long (*polyglot_cpp_i64_void_fn)(void);
typedef long long (*polyglot_cpp_i64_i64_fn)(long long);
typedef double (*polyglot_cpp_f64_f64_fn)(double);

// Returns 0 on success, -1 if the call threw.
int polyglot_cpp_try_call_void_void(polyglot_cpp_void_void_fn fn);
int polyglot_cpp_try_call_void_str(polyglot_cpp_void_str_fn fn, const char *arg);
int polyglot_cpp_try_call_i64_void(polyglot_cpp_i64_void_fn fn, long long *out);
int polyglot_cpp_try_call_i64_i64(polyglot_cpp_i64_i64_fn fn, long long arg, long long *out);
int polyglot_cpp_try_call_f64_f64(polyglot_cpp_f64_f64_fn fn, double arg, double *out);

// Retrieve (or NULL) the message from the most recent exception caught on
// this thread.  The pointer is owned by the runtime and remains valid until
// the next try-call on the same thread.
const char *polyglot_cpp_last_exception(void);

// Clear the thread-local last-exception slot.
void polyglot_cpp_clear_exception(void);

#ifdef __cplusplus
}
#endif
