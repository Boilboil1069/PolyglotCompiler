/**
 * @file     error_bridge.h
 * @brief    Cross-language structured exception bridge for the polyrt
 *           runtime.  Implements the C ABI invoked by code lowered from
 *           Ploy's TRY/CATCH/FINALLY/THROW constructs and the entry
 *           points used by per-language adapters to forward foreign
 *           exceptions (Python `Exception`, C++ `std::exception`, Java
 *           `Throwable`, .NET `Exception`, Rust `Result::Err`) into the
 *           unified Ploy `Error` handle.
 *
 * @ingroup  Runtime / Services
 * @author   Manning Cyrus
 * @date     2026-05-04
 */
#pragma once

#include <cstddef>
#include <cstdint>

// Portable noreturn attribute: GCC/Clang use __attribute__((noreturn));
// MSVC uses __declspec(noreturn).  Define POLYRT_NORETURN as a single
// token usable on extern "C" function declarations regardless of the
// host compiler so that public runtime headers compile cleanly under
// MSVC's /TP path as well as under GCC/Clang.
#ifndef POLYRT_NORETURN
#  if defined(_MSC_VER) && !defined(__clang__)
#    define POLYRT_NORETURN __declspec(noreturn)
#  elif defined(__GNUC__) || defined(__clang__)
#    define POLYRT_NORETURN __attribute__((noreturn))
#  else
#    define POLYRT_NORETURN
#  endif
#endif

#ifdef __cplusplus
#include <string>
#include <vector>

namespace polyglot::runtime::services {

// Snapshot of the most recently raised Error on the current thread.
// Returned by reference for inspection from the C++ side; the C ABI
// surface below exposes individual fields.
struct ErrorPayload {
  std::string message;
  std::string source_lang;
  std::vector<std::string> stacktrace;
};

const ErrorPayload &CurrentErrorPayload();
void SetCurrentErrorPayload(ErrorPayload payload);

} // namespace polyglot::runtime::services

extern "C" {
#endif

// Push a new exception handler scope onto the current thread.  The
// IR shape produced by the lowering treats the return value as a
// setjmp-style tag (0 = first entry, non-zero = unwound here); the
// current implementation always returns 0 and propagates throws via
// a C++ `RuntimeError` exception.  Callers in C++ test / runtime code
// must wrap the protected body in their own `try { ... } catch (
// const polyglot::runtime::services::RuntimeError &) { ... }` block.
int __ploy_rt_try_begin(void);

// Pop the most recent handler when the protected body completes
// normally.  No-op if the counter is already zero (defensive).
void __ploy_rt_try_end(void);

// Raise an Error from compiled Ploy code.  `message_ptr` is a
// NUL-terminated UTF-8 string interned by the lowering pass; pass
// nullptr to raise a sentinel "<unspecified>" error.  Throws a
// `RuntimeError` C++ exception when invoked inside an active handler
// scope; aborts otherwise.  Does not return normally.
POLYRT_NORETURN void __ploy_rt_throw(const char *message_ptr);

// Raise an Error tagged with a host-language origin label
// ("python", "cpp", "java", "dotnet", "rust", "ploy").  Used by the
// per-language adapters to forward foreign exceptions.  Does not
// return.
POLYRT_NORETURN void __ploy_rt_throw_from(const char *message_ptr,
                                          const char *source_lang_ptr);

// Accessors for the current Error payload, valid only inside a CATCH
// block (i.e. between `__ploy_rt_try_begin` returning non-zero and
// `__ploy_rt_clear_error`).
const char *__ploy_rt_current_error(void);
const char *__ploy_rt_current_error_message(void);
const char *__ploy_rt_current_error_source_lang(void);
size_t      __ploy_rt_current_error_stacktrace_count(void);
const char *__ploy_rt_current_error_stacktrace_at(size_t index);

// Mark the current Error as consumed.  Called at the end of a CATCH
// body so that a subsequent THROW from FINALLY does not re-deliver
// the same payload.
void __ploy_rt_clear_error(void);

#ifdef __cplusplus
} // extern "C"
#endif
