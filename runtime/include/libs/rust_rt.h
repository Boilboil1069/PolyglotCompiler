#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void polyglot_rust_print(const char *message);
char *polyglot_rust_strdup_gc(const char *message, void ***root_handle_out);
void polyglot_rust_release(char **ptr, void ***root_handle);

#ifdef __cplusplus
}
#endif
