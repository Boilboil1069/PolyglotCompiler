/**
 * @file     polyrt_linux.c
 * @brief    Libc-free implementation of `polyrt_println` /
 *           `polyrt_exit` for Linux x86_64 and aarch64.  Both
 *           helpers issue raw kernel syscalls through inline
 *           assembly so the runtime can be statically linked into
 *           a polyld-emitted `ET_EXEC` image without dragging in
 *           `crt1.o`, `libc.so` or any dynamic loader.
 *
 *           The translation unit compiles unconditionally on every
 *           host so the wider `runtime` static library still builds
 *           on Windows / macOS development machines; on those hosts
 *           the function bodies collapse to a portable libc
 *           implementation that simply talks to `write(3)` /
 *           `_Exit(3)` so unit tests can still link against the
 *           symbols.  The Linux / aarch64 + Linux / x86_64 paths
 *           remain syscall-only as required by the polyld static
 *           output contract.
 *
 * @ingroup  Runtime / lang-rt
 * @author   Manning Cyrus
 * @date     2026-05-06
 */

#include "runtime/include/polyrt_linux.h"

#if defined(__linux__) && (defined(__x86_64__) || defined(__aarch64__))

/* --- Linux / supported arch: raw-syscall implementation -------------- */

/* Compute strlen without touching libc so we never accidentally bring
 * in a `<string.h>` dependency that would force a libc link.  The
 * implementation is the trivial one-byte-at-a-time scan; performance
 * does not matter here because polyrt_println is only used for the
 * smoke-style "hello" payload polyld emits for `00_minimal` samples. */
static unsigned long polyrt_strlen_local(const char *s) {
  unsigned long n = 0;
  while (s && s[n]) ++n;
  return n;
}

#if defined(__x86_64__)

/* Linux x86_64 syscall numbers (asm-generic/unistd_64.h). */
#define POLYRT_NR_WRITE      1L
#define POLYRT_NR_EXIT_GROUP 231L

static long polyrt_syscall3(long nr, long a, long b, long c) {
  long ret;
  __asm__ volatile(
      "mov %1, %%rax\n\t"
      "mov %2, %%rdi\n\t"
      "mov %3, %%rsi\n\t"
      "mov %4, %%rdx\n\t"
      "syscall\n\t"
      "mov %%rax, %0\n\t"
      : "=r"(ret)
      : "r"(nr), "r"(a), "r"(b), "r"(c)
      : "%rax", "%rdi", "%rsi", "%rdx", "%rcx", "%r11", "memory");
  return ret;
}

#elif defined(__aarch64__)

/* Linux aarch64 syscall numbers (asm-generic/unistd.h). */
#define POLYRT_NR_WRITE      64L
#define POLYRT_NR_EXIT_GROUP 94L

static long polyrt_syscall3(long nr, long a, long b, long c) {
  register long x8 __asm__("x8") = nr;
  register long x0 __asm__("x0") = a;
  register long x1 __asm__("x1") = b;
  register long x2 __asm__("x2") = c;
  __asm__ volatile("svc #0"
                   : "+r"(x0)
                   : "r"(x8), "r"(x1), "r"(x2)
                   : "memory");
  return x0;
}

#endif

long polyrt_println(const char *msg) {
  unsigned long n = polyrt_strlen_local(msg);
  long w = polyrt_syscall3(POLYRT_NR_WRITE, 1, (long)(unsigned long)msg, (long)n);
  if (w < 0) return w;
  /* Trailing newline: emit it as a separate one-byte write so we never
   * have to mutate the caller's string buffer. */
  static const char kNewline = '\n';
  long w2 = polyrt_syscall3(POLYRT_NR_WRITE, 1, (long)(unsigned long)&kNewline, 1);
  if (w2 < 0) return w2;
  return w + w2;
}

void polyrt_exit(int code) {
  (void)polyrt_syscall3(POLYRT_NR_EXIT_GROUP, (long)code, 0, 0);
  /* exit_group never returns; spin defensively in case the kernel
   * surprises us so the noreturn contract still holds. */
  for (;;) {
  }
}

#else  /* !Linux or unsupported arch */

/* --- Fallback: portable libc implementation -------------------------- */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

long polyrt_println(const char *msg) {
  if (!msg) return 0;
  long n = (long)fwrite(msg, 1, strlen(msg), stdout);
  if (fputc('\n', stdout) != EOF) ++n;
  return n;
}

void polyrt_exit(int code) {
  fflush(stdout);
  _Exit(code);
}

#endif
