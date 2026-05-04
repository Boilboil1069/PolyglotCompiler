/**
 * @file     error_bridge.cpp
 * @brief    Implementation of the cross-language structured exception
 *           bridge.  The data plane uses thread-local current-error
 *           storage shared with host-language adapters; raising an
 *           Error is implemented as a C++ exception (`RuntimeError`)
 *           so that any enclosing C++ frame on the call stack can
 *           catch it and route control back into Ploy's lowered
 *           CATCH dispatch.  Direct setjmp/longjmp at the caller's
 *           IR site is tracked under future work.
 *
 * @ingroup  Runtime / Services
 * @author   Manning Cyrus
 * @date     2026-05-04
 */
#include "runtime/include/services/error_bridge.h"

#include "runtime/include/services/exception.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace polyglot::runtime::services {
namespace {

struct ThreadState {
  ErrorPayload current_error;
  bool error_live{false};
  // Counter rather than a real handler stack: every TRY entry bumps
  // it and every TRY exit decrements.  When a throw happens with the
  // counter at zero we abort (uncaught error); otherwise we let the
  // C++ exception propagate to the enclosing CATCH-emulating frame.
  int active_handlers{0};
};

thread_local ThreadState g_state;

} // namespace

const ErrorPayload &CurrentErrorPayload() {
  return g_state.current_error;
}

void SetCurrentErrorPayload(ErrorPayload payload) {
  g_state.current_error = std::move(payload);
  g_state.error_live = true;
}

} // namespace polyglot::runtime::services

using polyglot::runtime::services::CaptureStackTrace;
using polyglot::runtime::services::ErrorPayload;
using polyglot::runtime::services::g_state;
using polyglot::runtime::services::RuntimeError;

extern "C" {

int __ploy_rt_try_begin(void) {
  ++g_state.active_handlers;
  return 0;
}

void __ploy_rt_try_end(void) {
  if (g_state.active_handlers > 0) --g_state.active_handlers;
}

void __ploy_rt_throw(const char *message_ptr) {
  __ploy_rt_throw_from(message_ptr, "ploy");
}

void __ploy_rt_throw_from(const char *message_ptr, const char *source_lang_ptr) {
  ErrorPayload payload;
  payload.message = message_ptr ? std::string(message_ptr) : std::string("<unspecified>");
  payload.source_lang = source_lang_ptr ? std::string(source_lang_ptr) : std::string("ploy");
  payload.stacktrace = CaptureStackTrace(32);
  std::string message_copy = payload.message;
  std::vector<std::string> trace_copy = payload.stacktrace;
  polyglot::runtime::services::SetCurrentErrorPayload(std::move(payload));

  if (g_state.active_handlers <= 0) {
    // Uncaught: terminate with a recognisable signal so the existing
    // panic infrastructure picks it up.
    std::abort();
  }
  // Propagate via C++ exception: any enclosing CATCH dispatcher in
  // the runtime / native layer can intercept this.  The emitted IR
  // shape currently relies on the data-plane accessors below rather
  // than on exception unwinding for control flow; marrying the two
  // is tracked under future work.
  throw RuntimeError(message_copy, std::move(trace_copy));
}

const char *__ploy_rt_current_error(void) {
  if (!g_state.error_live) return nullptr;
  return g_state.current_error.message.c_str();
}

const char *__ploy_rt_current_error_message(void) {
  if (!g_state.error_live) return nullptr;
  return g_state.current_error.message.c_str();
}

const char *__ploy_rt_current_error_source_lang(void) {
  if (!g_state.error_live) return nullptr;
  return g_state.current_error.source_lang.c_str();
}

size_t __ploy_rt_current_error_stacktrace_count(void) {
  if (!g_state.error_live) return 0;
  return g_state.current_error.stacktrace.size();
}

const char *__ploy_rt_current_error_stacktrace_at(size_t index) {
  if (!g_state.error_live) return nullptr;
  if (index >= g_state.current_error.stacktrace.size()) return nullptr;
  return g_state.current_error.stacktrace[index].c_str();
}

void __ploy_rt_clear_error(void) {
  g_state.current_error = ErrorPayload{};
  g_state.error_live = false;
}

} // extern "C"
