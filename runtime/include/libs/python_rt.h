/**
 * @file     python_rt.h
 * @brief    Python (CPython 3.x) language runtime support library.
 *
 * @details  Provides a host-side bridge to a CPython interpreter.  Modelled
 *           on java_rt / dotnet_rt: the interpreter shared library is loaded
 *           dynamically (LoadLibrary / dlopen) so the compiler does not
 *           require Python development headers at build time.  Supported
 *           interpreters, selected through `version_hint`:
 *
 *               38  -> CPython 3.8
 *               39  -> CPython 3.9
 *               310 -> CPython 3.10
 *               311 -> CPython 3.11
 *               312 -> CPython 3.12
 *               313 -> CPython 3.13
 *                 0 -> auto-detect (POLYGLOT_PYTHON_LIBRARY env, then
 *                                   PYTHONHOME, then well-known names)
 *
 *           When no libpython can be located the print and strdup helpers
 *           continue to work; every entry point that requires a live
 *           interpreter returns NULL or a non-zero status.  Value handles
 *           returned from this layer are heap-allocated tagged-union
 *           descriptors that wrap a `PyObject*` via the engine's reference
 *           count, so they survive across GC cycles on the polyglot side
 *           and follow Python's own ownership rules on the engine side.
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

// Bring up the CPython interpreter.  Returns 0 on success, -1 if libpython
// could not be located or initialised.
int polyglot_python_init(int version_hint);

// Tear down the CPython interpreter and release all global state.
void polyglot_python_shutdown(void);

// ----- Always-available helpers (work without a live interpreter) ----------

// Print a message followed by '\n' (the Python `print` semantic).
void polyglot_python_print(const char *message);

// Duplicate a C string into GC-managed memory and root the slot.
char *polyglot_python_strdup_gc(const char *message, void ***root_handle_out);

// Release a GC-rooted string previously returned by polyglot_python_strdup_gc.
void polyglot_python_release(char **ptr, void ***root_handle);

// ----- Interpreter operations ---------------------------------------------

// Execute a single statement / expression.  Returns 0 on success, -1 on
// syntax / runtime errors (the error is printed to stderr).
int polyglot_python_run_string(const char *source);

// `import <module_name>`.  Returns an opaque value handle on success.
void *polyglot_python_import(const char *module_name);

// Resolve `obj.<name>` and return an opaque value handle.
void *polyglot_python_get_attr(void *object, const char *name);

// Set `obj.<name> = value`.  Returns 0 on success, -1 otherwise.
int polyglot_python_set_attr(void *object, const char *name, void *value);

// Call a callable handle with positional argument handles.
void *polyglot_python_call(void *callable, const void *const *args, int arg_count);

// Convenience: receiver.method(*args).  NULL when the attribute is missing.
void *polyglot_python_call_method(void *receiver, const char *method_name,
                                  const void *const *args, int arg_count);

// Convert a value to its `str()` form on the GC heap (release with
// polyglot_python_release).
char *polyglot_python_value_to_string(void *value, void ***root_handle_out);

// Convert a value to a 64-bit integer (returns 0 on type mismatch).
long long polyglot_python_value_to_int(void *value);

// Convert a value to a double (returns 0.0 on type mismatch).
double polyglot_python_value_to_float(void *value);

// Box primitives into Python value handles.
void *polyglot_python_string_value(const char *utf8);
void *polyglot_python_integer_value(long long n);
void *polyglot_python_float_value(double n);
void *polyglot_python_none_value(void);

// Release a value handle (decrements the underlying PyObject refcount).
void polyglot_python_release_value(void *value);

#ifdef __cplusplus
}
#endif
