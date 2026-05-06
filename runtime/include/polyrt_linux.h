/**
 * @file     polyrt_linux.h
 * @brief    Linux-only libc-free runtime entry points used by polyld
 *           when emitting `ET_EXEC` static executables.  Each helper
 *           lowers to a direct syscall (`write(2)` / `exit_group(2)`)
 *           via inline assembly so that downstream link products never
 *           depend on `crt1.o` / `libc.so` / `ld-linux*.so.*`.
 *
 * @ingroup  Runtime / lang-rt
 * @author   Manning Cyrus
 * @date     2026-05-06
 */

#pragma once

#if defined(__GNUC__) || defined(__clang__)
#  define POLYRT_NORETURN __attribute__((noreturn))
#else
#  define POLYRT_NORETURN
#endif

#if defined(__cplusplus)
extern "C" {
#endif

/// Write a NUL-terminated string followed by a single LF byte to file
/// descriptor 1 (stdout).  Implemented as a raw `write(2)` syscall on
/// every supported architecture so the call site can be reached from
/// a linker-emitted static executable that has never run a C runtime
/// initialiser.  Returns the byte count actually written, or a
/// negative errno on failure (matching the kernel's ABI).
long polyrt_println(const char *msg);

/// Terminate the calling thread group with the given exit code.  Maps
/// to `exit_group(2)` (`__NR_exit_group`) on x86_64 / aarch64 so all
/// threads in the process are torn down even when the runtime never
/// linked against libpthread.  Never returns.
POLYRT_NORETURN void polyrt_exit(int code);

#if defined(__cplusplus)
} // extern "C"
#endif
