#include <stdio.h>

void polyglot_python_print(const char *message) {
  if (!message) {
    return;
  }
  printf("%s\n", message);
}
