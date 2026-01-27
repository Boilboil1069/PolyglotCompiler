#include <stdio.h>
#include <string.h>

#include "runtime/include/libs/base.h"
#include "runtime/include/libs/rust_rt.h"

void polyglot_rust_print(const char *message) {
  if (!message) return;
  printf("%s\n", message);
}

char *polyglot_rust_strdup_gc(const char *message, void ***root_handle_out) {
  if (!message) return NULL;
  size_t len = strlen(message) + 1;
  char *buf = (char *)polyglot_alloc(len);
  if (!buf) return NULL;
  memcpy(buf, message, len);
  polyglot_gc_register_root((void **)&buf);
  if (root_handle_out) *root_handle_out = (void **)&buf;
  return buf;
}

void polyglot_rust_release(char **ptr, void ***root_handle) {
  if (!ptr || !*ptr) return;
  polyglot_gc_unregister_root((void **)ptr);
  *ptr = NULL;
  if (root_handle) *root_handle = NULL;
}
