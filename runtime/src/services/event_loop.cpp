/**
 * @file     event_loop.cpp
 * @brief    Cooperative event loop implementation.  See header for
 *           the public surface.  Implementation choices:
 *             * single mutex protects the ready/suspended queues so
 *               adapters from different threads can resolve a future
 *               without racing against the loop driver;
 *             * tasks execute to completion or until they touch a
 *               future that is not yet ready, in which case they are
 *               re-queued by the surrounding `__ploy_rt_await` shim;
 *             * the loop is intentionally non-preemptive — every
 *               cooperative yield happens at an explicit AWAIT site.
 *
 * @ingroup  Runtime / Services
 * @author   Manning Cyrus
 * @date     2026-05-04
 */
#include "runtime/include/services/event_loop.h"
#include "runtime/include/services/async_bridge.h"

#include <atomic>
#include <deque>
#include <mutex>
#include <unordered_map>

namespace polyglot::runtime::services {
namespace {

struct LoopState {
  std::mutex mu;
  std::deque<std::function<void()>> ready;
  std::unordered_map<std::uint64_t, void *> resolved;  // future-id -> payload
  std::atomic<std::uint64_t> next_id{1};
  std::atomic<std::size_t> tick_count{0};
  std::atomic<std::size_t> completed{0};
  std::atomic<std::size_t> pending{0};
  std::atomic<std::size_t> suspended{0};
  std::atomic<std::size_t> active_frames{0};
  std::atomic<bool> running{false};
};

LoopState &State() {
  static LoopState state;
  return state;
}

} // namespace

std::size_t EventLoopRun(std::size_t max_ticks) {
  auto &s = State();
  bool expected = false;
  if (!s.running.compare_exchange_strong(expected, true)) {
    return 0;  // re-entrant call; nothing to drive
  }

  std::size_t completed_here = 0;
  std::size_t ticks = 0;
  while (ticks < max_ticks) {
    std::function<void()> task;
    {
      std::lock_guard<std::mutex> lock(s.mu);
      if (s.ready.empty()) break;
      task = std::move(s.ready.front());
      s.ready.pop_front();
      if (s.pending > 0) --s.pending;
    }
    ++ticks;
    s.tick_count.fetch_add(1, std::memory_order_relaxed);
    task();
    ++completed_here;
    s.completed.fetch_add(1, std::memory_order_relaxed);
  }

  s.running.store(false);
  return completed_here;
}

std::uint64_t EventLoopSchedule(std::function<void()> task) {
  auto &s = State();
  std::uint64_t id = s.next_id.fetch_add(1, std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> lock(s.mu);
    s.ready.push_back(std::move(task));
  }
  s.pending.fetch_add(1, std::memory_order_relaxed);
  return id;
}

void EventLoopWake(std::uint64_t future_id, void *payload) {
  auto &s = State();
  std::lock_guard<std::mutex> lock(s.mu);
  s.resolved.emplace(future_id, payload);
  if (s.suspended > 0) --s.suspended;
}

std::size_t EventLoopTickCount() {
  return State().tick_count.load(std::memory_order_relaxed);
}

// ---------------------------------------------------------------------
// Internal helpers shared with async_bridge.cpp via a private surface.
// Declared here (not in the header) because they are an implementation
// detail of the runtime module rather than a stable C ABI.
// ---------------------------------------------------------------------

void *EventLoopTakeResolved(std::uint64_t future_id) {
  auto &s = State();
  std::lock_guard<std::mutex> lock(s.mu);
  auto it = s.resolved.find(future_id);
  if (it == s.resolved.end()) return nullptr;
  void *payload = it->second;
  s.resolved.erase(it);
  return payload;
}

void EventLoopBumpActiveFrames(int delta) {
  auto &s = State();
  if (delta > 0) s.active_frames.fetch_add(static_cast<std::size_t>(delta));
  else if (delta < 0 && s.active_frames > 0)
    s.active_frames.fetch_sub(static_cast<std::size_t>(-delta));
}

void EventLoopBumpSuspended(int delta) {
  auto &s = State();
  if (delta > 0) s.suspended.fetch_add(static_cast<std::size_t>(delta));
  else if (delta < 0 && s.suspended > 0)
    s.suspended.fetch_sub(static_cast<std::size_t>(-delta));
}

AsyncSchedulerSnapshot EventLoopSnapshot() {
  auto &s = State();
  AsyncSchedulerSnapshot snap;
  snap.pending_tasks = s.pending.load();
  snap.suspended_tasks = s.suspended.load();
  snap.completed_tasks = s.completed.load();
  snap.loop_iterations = s.tick_count.load();
  snap.active_async_frames = s.active_frames.load();
  return snap;
}

void EventLoopReset() {
  auto &s = State();
  std::lock_guard<std::mutex> lock(s.mu);
  s.ready.clear();
  s.resolved.clear();
  s.next_id.store(1);
  s.tick_count.store(0);
  s.completed.store(0);
  s.pending.store(0);
  s.suspended.store(0);
  s.active_frames.store(0);
  s.running.store(false);
}

} // namespace polyglot::runtime::services
