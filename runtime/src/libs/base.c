#include "runtime/include/libs/base.h"

#include <string.h>

void *polyglot_memcpy(void *dest, const void *src, size_t size) {
  return memcpy(dest, src, size);
}

int polyglot_memcmp(const void *lhs, const void *rhs, size_t size) {
  return memcmp(lhs, rhs, size);
}
