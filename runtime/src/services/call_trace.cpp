/**
 * @file     call_trace.cpp
 * @brief    Function-level call tracing implementation
 *
 * @ingroup  Runtime / Services
 * @author   Manning Cyrus
 * @date     2026-04-29
 */
#include "runtime/include/services/call_trace.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <sstream>
#include <thread>
#include <vector>

namespace {

// Module-local enable flag.  std::atomic<int> rather than bool so the
// extern "C" hook can read it without a C/C++ ABI mismatch.
std::atomic<int> g_call_trace_enabled{0};

// Per-thread frame describing one in-flight call.  Mirrors the private
// CallTracer::ThreadStackEntry but carries the language pointer too so
// Exit can attribute time without re-walking the global hash table.
struct LocalEntry {
  const char *name;
  const char *language;
  std::chrono::steady_clock::time_point started;
  std::uint64_t children_inclusive_ns;
};

// Thread-local stack of in-flight calls.  Each thread owns its own
// stack so Enter/Exit do not need to coordinate across threads except
// when committing the final timing into the global table.
thread_local std::vector<LocalEntry> tl_stack;

inline std::uint64_t NowNs() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

} // namespace

extern "C" void __ploy_rt_call_trace_enable(int enabled) {
  g_call_trace_enabled.store(enabled, std::memory_order_release);
}

extern "C" int __ploy_rt_call_trace_is_enabled(void) {
  return g_call_trace_enabled.load(std::memory_order_acquire);
}

extern "C" void __ploy_rt_call_enter(const char *qualified_name, const char *language) {
  if (!g_call_trace_enabled.load(std::memory_order_acquire)) {
    return;
  }
  if (qualified_name == nullptr) {
    return;
  }
  polyglot::runtime::services::CallTracer::Instance().Enter(qualified_name, language);
}

extern "C" void __ploy_rt_call_exit(const char *qualified_name) {
  if (!g_call_trace_enabled.load(std::memory_order_acquire)) {
    return;
  }
  if (qualified_name == nullptr) {
    return;
  }
  polyglot::runtime::services::CallTracer::Instance().Exit(qualified_name);
}

namespace polyglot::runtime::services {

CallTracer &CallTracer::Instance() {
  static CallTracer instance;
  return instance;
}

void CallTracer::Enter(const char *qualified_name, const char *language) {
  // Push a frame onto the per-thread stack.  Inclusive timing accrues
  // here; the global table is touched only on Exit so contention is
  // bounded by the function frequency rather than the call duration.
  ::LocalEntry frame{qualified_name, language ? language : "unknown",
                     std::chrono::steady_clock::now(), 0};
  ::tl_stack.push_back(frame);
  total_events_.fetch_add(1, std::memory_order_relaxed);
}

void CallTracer::Exit(const char *qualified_name) {
  if (::tl_stack.empty()) {
    dropped_events_.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  auto frame = ::tl_stack.back();
  ::tl_stack.pop_back();
  if (frame.name != qualified_name) {
    // Mismatched enter/exit pair (likely from longjmp or unhandled
    // exception).  Account it as dropped and resume the parent without
    // crashing the host process.
    dropped_events_.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  const auto end = std::chrono::steady_clock::now();
  const std::uint64_t inclusive = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(end - frame.started).count());
  const std::uint64_t self =
      inclusive > frame.children_inclusive_ns ? inclusive - frame.children_inclusive_ns : 0;

  if (!::tl_stack.empty()) {
    ::tl_stack.back().children_inclusive_ns += inclusive;
  }

  std::lock_guard<std::mutex> lk(mu_);
  auto &slot = stats_[frame.name ? std::string(frame.name) : std::string("<anon>")];
  if (slot.qualified_name.empty()) {
    slot.qualified_name = frame.name ? frame.name : "<anon>";
    slot.language = frame.language ? frame.language : "unknown";
  }
  slot.call_count += 1;
  slot.inclusive_ns += inclusive;
  slot.self_ns += self;
}

CallTraceSnapshot CallTracer::DrainSnapshot() {
  std::lock_guard<std::mutex> lk(mu_);
  CallTraceSnapshot snap;
  snap.total_events = total_events_.exchange(0, std::memory_order_acq_rel);
  snap.dropped_events = dropped_events_.exchange(0, std::memory_order_acq_rel);
  snap.entries.reserve(stats_.size());
  for (auto &kv : stats_) {
    snap.entries.push_back(kv.second);
  }
  stats_.clear();
  return snap;
}

CallTraceSnapshot CallTracer::PeekSnapshot() const {
  std::lock_guard<std::mutex> lk(mu_);
  CallTraceSnapshot snap;
  snap.total_events = total_events_.load(std::memory_order_acquire);
  snap.dropped_events = dropped_events_.load(std::memory_order_acquire);
  snap.entries.reserve(stats_.size());
  for (const auto &kv : stats_) {
    snap.entries.push_back(kv.second);
  }
  return snap;
}

void CallTracer::Clear() {
  std::lock_guard<std::mutex> lk(mu_);
  stats_.clear();
  total_events_.store(0, std::memory_order_release);
  dropped_events_.store(0, std::memory_order_release);
}

namespace {

void EscapeJsonString(std::ostringstream &os, const std::string &s) {
  os << '"';
  for (char c : s) {
    switch (c) {
    case '"':
      os << "\\\"";
      break;
    case '\\':
      os << "\\\\";
      break;
    case '\n':
      os << "\\n";
      break;
    case '\r':
      os << "\\r";
      break;
    case '\t':
      os << "\\t";
      break;
    default:
      if (static_cast<unsigned char>(c) < 0x20) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(c));
        os << buf;
      } else {
        os << c;
      }
    }
  }
  os << '"';
}

} // namespace

std::string CallTracer::SerializeJson(const CallTraceSnapshot &snap) {
  std::ostringstream os;
  os << "{\"schema\":\"polyglot.calltrace.v1\",";
  os << "\"total_events\":" << snap.total_events << ",";
  os << "\"dropped_events\":" << snap.dropped_events << ",";
  os << "\"entries\":[";
  for (std::size_t i = 0; i < snap.entries.size(); ++i) {
    const auto &e = snap.entries[i];
    if (i)
      os << ',';
    os << "{\"name\":";
    EscapeJsonString(os, e.qualified_name);
    os << ",\"language\":";
    EscapeJsonString(os, e.language);
    os << ",\"call_count\":" << e.call_count;
    os << ",\"inclusive_ns\":" << e.inclusive_ns;
    os << ",\"self_ns\":" << e.self_ns << '}';
  }
  os << "]}";
  return os.str();
}

} // namespace polyglot::runtime::services
