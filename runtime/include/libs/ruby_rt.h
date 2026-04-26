/**
 * @file     ruby_rt.h
 * @brief    Ruby (MRI / CRuby 2.7+, 3.x) runtime support library.
 *
 * @details  Loads libruby dynamically (LoadLibrary / dlopen) and exposes a
 *           thin C bridge that the polyglot pipeline can call from lowered
 *           Ruby IR.  Supported MRI versions, via `version_hint`:
 *
 *             27 -> Ruby 2.7
 *             30 -> Ruby 3.0
 *             31 -> Ruby 3.1
 *             32 -> Ruby 3.2
 *             33 -> Ruby 3.3
 *              0 -> auto-detect from PATH / RUBY_ROOT
 *
 *           When libruby cannot be located (typical for minimal CI images)
 *           polyglot_ruby_init returns -1 but `polyglot_ruby_print` remains
 *           usable as a pure libc fallback so simple "puts" programs still
 *           work.
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

// Bring up the Ruby VM.  Returns 0 on success, -1 if libruby could not be
// located or initialised.
int polyglot_ruby_init(int version_hint);

// Tear down the Ruby VM and release all global state.
void polyglot_ruby_shutdown(void);

// Print a message followed by '\n' (the `puts` semantic).  Works without a
// running VM by falling back to libc `puts`.
void polyglot_ruby_print(const char *message);

// Duplicate a C string into GC-managed memory and root the slot.
char *polyglot_ruby_strdup_gc(const char *message, void ***root_handle_out);

// Release a GC-rooted string previously returned by polyglot_ruby_strdup_gc.
void polyglot_ruby_release(char **ptr, void ***root_handle);

// `require "<feature>"`.  Returns 0 on success.
int polyglot_ruby_require(const char *feature);

// Evaluate a Ruby snippet.  Returns an opaque VALUE handle (Ruby's tagged
// pointer) on success, or NULL when the VM is not initialised.  The handle
// is rooted in the Ruby VM until polyglot_ruby_release_value is called.
void *polyglot_ruby_eval(const char *source);

// Resolve a top-level constant (e.g. "Array", "Math::PI").
void *polyglot_ruby_get_constant(const char *name);

// Invoke a method on `receiver` with positional argument handles.  Pass
// NULL `receiver` to invoke a top-level Kernel method (e.g. `puts`).
void *polyglot_ruby_call_method(void *receiver, const char *method_name,
                                const void *const *args, int arg_count);

// Convert a Ruby VALUE to its `.to_s` form.  Returned string lives on the
// GC heap; release with polyglot_ruby_release.
char *polyglot_ruby_value_to_string(void *value, void ***root_handle_out);

// Box primitives into Ruby VALUEs.
void *polyglot_ruby_string_value(const char *utf8);
void *polyglot_ruby_integer_value(long long n);
void *polyglot_ruby_float_value(double n);

// Release a VALUE handle returned by any of the calls above.
void polyglot_ruby_release_value(void *value);

#ifdef __cplusplus
}
#endif
