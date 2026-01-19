#pragma once

#include <stddef.h>

void *polyglot_memcpy(void *dest, const void *src, size_t size);
int polyglot_memcmp(const void *lhs, const void *rhs, size_t size);
