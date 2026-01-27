#include "runtime/include/libs/base.h"
#include "runtime/include/gc/root_guard.h"

#include <stdio.h>
#include <string.h>

void *polyglot_memcpy(void *dest, const void *src, size_t size) {
  return memcpy(dest, src, size);
}

int polyglot_memcmp(const void *lhs, const void *rhs, size_t size) {
  return memcmp(lhs, rhs, size);
}

void *polyglot_memset(void *dest, int value, size_t size) { return memset(dest, value, size); }

size_t polyglot_strlen(const char *s) { return strlen(s); }

char *polyglot_strcpy(char *dest, const char *src) { return strcpy(dest, src); }

char *polyglot_strncpy(char *dest, const char *src, size_t n) { return strncpy(dest, src, n); }

int polyglot_strcmp(const char *lhs, const char *rhs) { return strcmp(lhs, rhs); }

int polyglot_strncmp(const char *lhs, const char *rhs, size_t n) { return strncmp(lhs, rhs, n); }

// Example helper showing how a C caller can root a GC-managed allocation.
// Not used elsewhere but kept as documentation and test hook.
void *polyglot_alloc_rooted(size_t size) {
  void *ptr = polyglot_alloc(size);
  POLYGLOT_WITH_ROOT(_guard, &ptr);
  (void)_guard;  // guard keeps ptr rooted for caller scope (GCC/Clang cleanup)
  return ptr;
}

void polyglot_println(const char *message) {
  if (!message) return ;
  printf("%s\n", message);
}

bool polyglot_read_file(const char *path, char **out_buf, size_t *out_size) {
  if (!path || !out_buf || !out_size) return false;
  FILE *f = fopen(path, "rb");
  if (!f) return false;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return false;
  }
  long len = ftell(f);
  if (len < 0) {
    fclose(f);
    return false;
  }
  rewind(f);
  char *buf = (char *)polyglot_alloc((size_t)len + 1);
  if (!buf) {
    fclose(f);
    return false;
  }
  size_t read = fread(buf, 1, (size_t)len, f);
  fclose(f);
  buf[read] = '\0';
  *out_buf = buf;
  *out_size = read;
  polyglot_gc_register_root((void **)out_buf);
  return true;
}

bool polyglot_write_file(const char *path, const char *data, size_t size) {
  if (!path || !data) return false;
  FILE *f = fopen(path, "wb");
  if (!f) return false;
  size_t written = fwrite(data, 1, size, f);
  fclose(f);
  return written == size;
}

void polyglot_free_file_buffer(char *buf) {
  if (!buf) return;
  polyglot_gc_unregister_root((void **)&buf);
}
