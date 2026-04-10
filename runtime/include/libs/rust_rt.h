/**
 * @file     rust_rt.h
 * @brief    Language-specific runtime libraries
 *
 * @ingroup  Runtime / Libs
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
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
