#include "runtime/include/libs/base.h"
#include "runtime/include/gc/root_guard.h"

#include <string.h>

void *polyglot_memcpy(void *dest, const void *src, size_t size) {
  return memcpy(dest, src, size);
}

int polyglot_memcmp(const void *lhs, const void *rhs, size_t size) {
  return memcmp(lhs, rhs, size);
}

// Example helper showing how a C caller can root a GC-managed allocation.
// Not used elsewhere but kept as documentation and test hook.
void *polyglot_alloc_rooted(size_t size) {
  void *ptr = polyglot_alloc(size);
  POLYGLOT_WITH_ROOT(_guard, &ptr);
  (void)_guard;  // guard keeps ptr rooted for caller scope (GCC/Clang cleanup)
  return ptr;
}
