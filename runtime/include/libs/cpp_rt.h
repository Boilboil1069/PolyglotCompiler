#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void polyglot_cpp_print(const char *message);
char *polyglot_cpp_strdup_gc(const char *message, void ***root_handle_out);
void polyglot_cpp_release(char **ptr, void ***root_handle);

#ifdef __cplusplus
}
#endif
