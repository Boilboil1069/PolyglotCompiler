/**
 * @file     call_trace.h
 * @brief    Function-level call tracing infrastructure
 *
 * @ingroup  Runtime / Services
 * @author   Manning Cyrus
 * @date     2026-04-29
 */
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef __cplusplus
extern "C" {
#endif

// C ABI hooks injected by the InstrumentCallTrace IR pass at every
// function entry / exit edge.  The two pointers are stable, opaque
// identifiers chosen by the compiler (typically a pointer to the
// canonical mangled name held in .rodata).  When tracing is disabled
// at runtime the implementations short-circuit on the first cache line
// load so the LTO dead-code-stripper can elide the calls when the
// resulting whole-program graph contains no other observers.
void __ploy_rt_call_enter(const char *qualified_name, const char *language);
void __ploy_rt_call_exit(const char *qualified_name);

// Enable / disable the runtime sink without recompiling.  The default
// state is "disabled" so an instrumented binary in production pays at
// most a single relaxed-load per call site.
void __ploy_rt_call_trace_enable(int enabled);
int __ploy_rt_call_trace_is_enabled(void);

#ifdef __cplusplus
} // extern "C"
#endif

namespace polyglot::runtime::services {

// Per-function counters maintained by the call tracer.
//
// All numeric fields are reported in nanoseconds and are computed from
// std::chrono::steady_clock so they are monotonic across the process
// lifetime.  call_count is incremented atomically on every enter event
// while inclusive_ns / self_ns are updated under the per-thread stack
// lock to keep the timing accounting correct in the presence of
// recursion and pre-emption.
/** @brief CallStats data structure. */
struct CallStats {
  std::string qualified_name;
  std::string language;
  std::uint64_t call_count{0};
  std::uint64_t inclusive_ns{0};
  std::uint64_t self_ns{0};
};

// Snapshot of the entire call-trace ring buffer.
/** @brief CallTraceSnapshot data structure. */
struct CallTraceSnapshot {
  std::vector<CallStats> entries;
  std::uint64_t total_events{0};
  std::uint64_t dropped_events{0};
};

// Thread-safe singleton that aggregates per-function timing.
//
// The collector is implemented as a striped hash-map keyed on the
// qualified_name pointer (already interned by the compiler) which keeps
// enter/exit hot paths lock-free under the common case where most call
// sites observe contention only inside a single shard.
/** @brief CallTracer class. */
class CallTracer {
public:
  static CallTracer &Instance();

  void Enter(const char *qualified_name, const char *language);
  void Exit(const char *qualified_name);

  // Atomically swap the current statistics for an empty snapshot.  Used
  // by polyrt and the profile sink to drain the buffer between samples.
  CallTraceSnapshot DrainSnapshot();
  CallTraceSnapshot PeekSnapshot() const;

  // JSON serialisation of a snapshot.  The schema is documented in
  // docs/specs/call_graph_schema_en.md; consumers must not depend on
  // field ordering inside the JSON object.
  static std::string SerializeJson(const CallTraceSnapshot &snap);

  void Clear();

private:
  CallTracer() = default;

  struct ThreadStackEntry {
    const char *name{nullptr};
    std::chrono::steady_clock::time_point started;
    std::uint64_t children_inclusive_ns{0};
  };

  mutable std::mutex mu_;
  std::unordered_map<std::string, CallStats> stats_;
  std::atomic<std::uint64_t> total_events_{0};
  std::atomic<std::uint64_t> dropped_events_{0};
};

} // namespace polyglot::runtime::services
