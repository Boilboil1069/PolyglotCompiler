#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Duplicate a C string into GC-managed memory and register a root for the pointer.
// Call polyglot_python_release to unregister the root once done.
char *polyglot_python_strdup_gc(const char *message, void ***root_handle_out);

// Unregister the root created by polyglot_python_strdup_gc and clear the pointer.
void polyglot_python_release(char **ptr, void ***root_handle);

#ifdef __cplusplus
}
#endif
