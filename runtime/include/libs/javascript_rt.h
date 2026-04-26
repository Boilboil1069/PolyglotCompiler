/**
 * @file     javascript_rt.h
 * @brief    JavaScript / ECMAScript runtime support library.
 *
 * @details  Provides a host-side bridge to a JavaScript engine for the
 *           polyglot pipeline.  Modelled on java_rt / dotnet_rt: the engine
 *           is loaded dynamically (LoadLibrary / dlopen) so the compiler does
 *           not require Node.js or V8 headers at build time.  Supported
 *           backends, selected through `version_hint`:
 *
 *             18 -> Node.js 18 (LTS)
 *             20 -> Node.js 20 (LTS)
 *             22 -> Node.js 22
 *              0 -> auto-detect from PATH / NODE_HOME / NVM
 *
 *           The bridge exposes an `eval` primitive plus simple function-call
 *           and property-access helpers; results are returned as opaque
 *           handles which must be released with polyglot_js_release_value.
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

// Initialise the JavaScript engine.  Returns 0 on success, non-zero if no
// supported runtime could be located (typical when neither node nor a
// standalone V8 build is on the system).
int polyglot_js_init(int version_hint);

// Tear down the engine and release all resident resources.
void polyglot_js_shutdown(void);

// Print a message followed by '\n' (the `console.log` semantic).  Always
// available, even when the engine is not initialised, so that lowered
// `console.log("hello")` works in standalone mode.
void polyglot_js_print(const char *message);

// Duplicate a C string into GC-managed memory and root the slot.
char *polyglot_js_strdup_gc(const char *message, void ***root_handle_out);

// Release a GC-rooted string previously returned by polyglot_js_strdup_gc.
void polyglot_js_release(char **ptr, void ***root_handle);

// Evaluate a JavaScript source snippet.  Returns an opaque value handle on
// success or NULL on failure / when the engine is not initialised.  The
// returned handle must be released with polyglot_js_release_value.
void *polyglot_js_eval(const char *source);

// Look up a global by name (e.g. `Math`, `JSON`).  Returns NULL on miss.
void *polyglot_js_get_global(const char *name);

// Read a property off an object value.  Returns NULL on miss.
void *polyglot_js_get_property(void *object, const char *name);

// Call a function value with the given positional arguments.  `this_arg` may
// be NULL to invoke the function with `globalThis` as the receiver.  Each
// argument is itself a JS value handle.
void *polyglot_js_call_function(void *function, void *this_arg,
                                const void *const *args, int arg_count);

// Convert a JS value to its string representation.  The returned string is
// allocated on the GC heap; release it with polyglot_js_release.
char *polyglot_js_value_to_string(void *value, void ***root_handle_out);

// Convert a JS value to a double.  Returns 0.0 if the value is not numeric.
double polyglot_js_value_to_number(void *value);

// Box a C string / double into a fresh JS value handle.
void *polyglot_js_string_value(const char *utf8);
void *polyglot_js_number_value(double n);

// Release a value handle returned by any of the calls above.
void polyglot_js_release_value(void *value);

#ifdef __cplusplus
}
#endif
