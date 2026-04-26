/**
 * @file     cpp_rt.cpp
 * @brief    Implementation of the C++ runtime support library.
 *
 * @details  C++ is the host language of the polyglot runtime, so this file
 *           does not embed a separate VM.  Instead it provides a uniform
 *           bridge for foreign C++ artefacts: dynamic library loading,
 *           symbol resolution (mangled or extern-C), demangling, and call
 *           trampolines that turn escaping C++ exceptions into a per-thread
 *           error string so the C ABI surface remains exception-safe.
 *
 *           On POSIX we use the Itanium C++ ABI's `__cxa_demangle`; on
 *           Windows we fall back to copying the input verbatim because
 *           UnDecorateSymbolName would pull in dbghelp.dll just to support
 *           a diagnostic helper.
 *
 * @ingroup  Runtime / Libs
 * @author   Manning Cyrus
 * @date     2026-04-26
 */

#include "runtime/include/libs/cpp_rt.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <mutex>
#include <string>
#include <vector>

#include "runtime/include/libs/base.h"
#include "runtime/include/memory/polyglot_alloc.h"

#ifdef _WIN32
#include <windows.h>
using cpp_lib_t = HMODULE;
#define CPP_LOAD(path)     LoadLibraryA(path)
#define CPP_SYM(lib, name) reinterpret_cast<void *>(GetProcAddress((lib), (name)))
#define CPP_UNLOAD(lib)    FreeLibrary(lib)
#else
#include <dlfcn.h>
using cpp_lib_t = void *;
#define CPP_LOAD(path)     dlopen((path), RTLD_LAZY | RTLD_GLOBAL)
#define CPP_SYM(lib, name) dlsym((lib), (name))
#define CPP_UNLOAD(lib)    dlclose(lib)
#endif

#if defined(__GNUC__) || defined(__clang__)
#include <cxxabi.h>
#define POLYGLOT_CPP_HAS_DEMANGLE 1
#else
#define POLYGLOT_CPP_HAS_DEMANGLE 0
#endif

namespace {

// Per-thread last-exception slot.  Stored as a std::string so we can hand
// out a stable `c_str()` pointer between try-call invocations on the same
// thread.  Cleared on `polyglot_cpp_clear_exception` and on every fresh
// successful try-call.
thread_local std::string g_last_exception;

// Loaded libraries — a tiny vector under a global mutex.  We don't need
// per-library handles to escape; users hold the raw OS handle anyway.
std::mutex g_libs_mu;
std::vector<cpp_lib_t> g_libs;

void register_lib(cpp_lib_t h) {
  std::lock_guard<std::mutex> lk(g_libs_mu);
  g_libs.push_back(h);
}

bool unregister_lib(cpp_lib_t h) {
  std::lock_guard<std::mutex> lk(g_libs_mu);
  for (auto it = g_libs.begin(); it != g_libs.end(); ++it) {
    if (*it == h) {
      g_libs.erase(it);
      return true;
    }
  }
  return false;
}

void capture_current_exception() {
  try {
    throw;  // re-throw within catch handler
  } catch (const std::exception &e) {
    g_last_exception = e.what() ? e.what() : "(std::exception)";
  } catch (const char *s) {
    g_last_exception = s ? s : "(const char* exception)";
  } catch (...) {
    g_last_exception = "unknown C++ exception";
  }
}

}  // namespace

extern "C" {

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

int polyglot_cpp_init(int /*version_hint*/) {
  g_last_exception.clear();
  return 0;
}

void polyglot_cpp_shutdown(void) {
  std::lock_guard<std::mutex> lk(g_libs_mu);
  for (auto h : g_libs) {
    if (h) CPP_UNLOAD(h);
  }
  g_libs.clear();
  g_last_exception.clear();
}

// ---------------------------------------------------------------------------
// Always-available helpers
// ---------------------------------------------------------------------------

void polyglot_cpp_print(const char *message) {
  if (!message) return;
  std::printf("%s\n", message);
}

char *polyglot_cpp_strdup_gc(const char *message, void ***root_handle_out) {
  if (!message) return NULL;
  size_t len = std::strlen(message) + 1;
  char *buf = (char *)polyglot_alloc(len);
  if (!buf) return NULL;
  std::memcpy(buf, message, len);
  polyglot_gc_register_root((void **)&buf);
  if (root_handle_out) *root_handle_out = (void **)&buf;
  return buf;
}

void polyglot_cpp_release(char **ptr, void ***root_handle) {
  if (!ptr || !*ptr) return;
  polyglot_gc_unregister_root((void **)ptr);
  *ptr = NULL;
  if (root_handle) *root_handle = NULL;
}

// ---------------------------------------------------------------------------
// Dynamic library bridge
// ---------------------------------------------------------------------------

void *polyglot_cpp_load_library(const char *path) {
  if (!path || !path[0]) return NULL;
  cpp_lib_t h = CPP_LOAD(path);
  if (!h) {
    std::fprintf(stderr, "[polyglot-cpp] failed to load library '%s'\n", path);
    return NULL;
  }
  register_lib(h);
  return reinterpret_cast<void *>(h);
}

void polyglot_cpp_unload_library(void *handle) {
  if (!handle) return;
  cpp_lib_t h = reinterpret_cast<cpp_lib_t>(handle);
  if (unregister_lib(h)) CPP_UNLOAD(h);
}

void *polyglot_cpp_resolve_symbol(void *handle, const char *symbol) {
  if (!handle || !symbol) return NULL;
  return CPP_SYM(reinterpret_cast<cpp_lib_t>(handle), symbol);
}

char *polyglot_cpp_demangle(const char *mangled, void ***root_handle_out) {
  if (!mangled) return NULL;
#if POLYGLOT_CPP_HAS_DEMANGLE
  int status = 0;
  char *demangled = abi::__cxa_demangle(mangled, NULL, NULL, &status);
  if (status == 0 && demangled) {
    char *gc = polyglot_cpp_strdup_gc(demangled, root_handle_out);
    std::free(demangled);
    return gc;
  }
#endif
  return polyglot_cpp_strdup_gc(mangled, root_handle_out);
}

// ---------------------------------------------------------------------------
// Try-call trampolines
// ---------------------------------------------------------------------------

int polyglot_cpp_try_call_void_void(polyglot_cpp_void_void_fn fn) {
  if (!fn) return -1;
  try {
    g_last_exception.clear();
    fn();
    return 0;
  } catch (...) {
    capture_current_exception();
    return -1;
  }
}

int polyglot_cpp_try_call_void_str(polyglot_cpp_void_str_fn fn, const char *arg) {
  if (!fn) return -1;
  try {
    g_last_exception.clear();
    fn(arg);
    return 0;
  } catch (...) {
    capture_current_exception();
    return -1;
  }
}

int polyglot_cpp_try_call_i64_void(polyglot_cpp_i64_void_fn fn, long long *out) {
  if (!fn) return -1;
  try {
    g_last_exception.clear();
    long long v = fn();
    if (out) *out = v;
    return 0;
  } catch (...) {
    capture_current_exception();
    return -1;
  }
}

int polyglot_cpp_try_call_i64_i64(polyglot_cpp_i64_i64_fn fn, long long arg,
                                  long long *out) {
  if (!fn) return -1;
  try {
    g_last_exception.clear();
    long long v = fn(arg);
    if (out) *out = v;
    return 0;
  } catch (...) {
    capture_current_exception();
    return -1;
  }
}

int polyglot_cpp_try_call_f64_f64(polyglot_cpp_f64_f64_fn fn, double arg,
                                  double *out) {
  if (!fn) return -1;
  try {
    g_last_exception.clear();
    double v = fn(arg);
    if (out) *out = v;
    return 0;
  } catch (...) {
    capture_current_exception();
    return -1;
  }
}

const char *polyglot_cpp_last_exception(void) {
  return g_last_exception.empty() ? NULL : g_last_exception.c_str();
}

void polyglot_cpp_clear_exception(void) { g_last_exception.clear(); }

}  // extern "C"
