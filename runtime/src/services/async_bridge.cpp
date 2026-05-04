/**
 * @file     async_bridge.cpp
 * @brief    Cooperative async-task / Future bridge backing the
 *           Ploy `ASYNC` / `AWAIT` runtime ABI.  The C surface
 *           defined in `async_bridge.h` is implemented as thin
 *           wrappers around the cooperative event loop in
 *           `event_loop.{h,cpp}`; per-language adapters resolve
 *           native awaitables by handing payload pointers to
 *           `__ploy_rt_future_resolve`.
 *
 * @ingroup  Runtime / Services
 * @author   Manning Cyrus
 * @date     2026-05-04
 */
#include "runtime/include/services/async_bridge.h"
#include "runtime/include/services/event_loop.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

namespace polyglot::runtime::services {

// Friends from event_loop.cpp — implementation-private accessors.
void *EventLoopTakeResolved(std::uint64_t future_id);
void EventLoopBumpActiveFrames(int delta);
void EventLoopBumpSuspended(int delta);
AsyncSchedulerSnapshot EventLoopSnapshot();
void EventLoopReset();

namespace {

struct FutureRegistry {
  std::mutex mu;
  std::unordered_map<std::uint64_t, FutureHandle> handles;
};

FutureRegistry &Registry() {
  static FutureRegistry r;
  return r;
}

std::uint64_t RegisterHandle(const std::string &lang) {
  static std::atomic<std::uint64_t> next_id{1};
  std::uint64_t id = next_id.fetch_add(1);
  auto &r = Registry();
  std::lock_guard<std::mutex> lock(r.mu);
  FutureHandle h;
  h.id = id;
  h.source_lang = lang;
  r.handles.emplace(id, h);
  return id;
}

} // namespace

std::uint64_t SpawnPloyTask(std::function<void()> task) {
  std::uint64_t id = RegisterHandle("ploy");
  EventLoopSchedule([id, fn = std::move(task)]() {
    fn();
    auto &r = Registry();
    std::lock_guard<std::mutex> lock(r.mu);
    auto it = r.handles.find(id);
    if (it != r.handles.end()) {
      it->second.ready = true;
    }
  });
  return id;
}

void ResolveFuture(std::uint64_t future_id, void *payload) {
  EventLoopWake(future_id, payload);
  auto &r = Registry();
  std::lock_guard<std::mutex> lock(r.mu);
  auto it = r.handles.find(future_id);
  if (it != r.handles.end()) {
    it->second.ready = true;
    it->second.payload = payload;
  }
}

std::size_t RunUntilIdle(std::size_t max_ticks) {
  return EventLoopRun(max_ticks == 0 ? 1024 : max_ticks);
}

AsyncSchedulerSnapshot SnapshotScheduler() { return EventLoopSnapshot(); }

void ResetScheduler() {
  EventLoopReset();
  auto &r = Registry();
  std::lock_guard<std::mutex> lock(r.mu);
  r.handles.clear();
}

} // namespace polyglot::runtime::services

// ---------------------------------------------------------------------
// C ABI surface.  These thin shims are the symbols emitted by the
// lowering pass; they delegate to the C++ helpers above.
// ---------------------------------------------------------------------

extern "C" {

void __ploy_rt_async_enter(void) {
  polyglot::runtime::services::EventLoopBumpActiveFrames(1);
}

void __ploy_rt_async_complete(void) {
  polyglot::runtime::services::EventLoopBumpActiveFrames(-1);
}

uint64_t __ploy_rt_async_spawn(ploy_async_task_fn fn, void *user_data) {
  if (fn == nullptr) return 0;
  return polyglot::runtime::services::SpawnPloyTask([fn, user_data]() { fn(user_data); });
}

void *__ploy_rt_await(void *future_handle) {
  // The middle-end currently passes a raw 64-bit future id reinterpreted
  // as a pointer; foreign adapters that produce real handle pointers
  // hash them into the same id space when registering.  Looking up the
  // resolved payload returns NULL when the future has not yet fired —
  // a future iteration will integrate true coroutine suspension; this
  // initial drop runs the scheduler to completion before returning so
  // the value is observable when the caller polls.
  auto id = reinterpret_cast<std::uintptr_t>(future_handle);
  polyglot::runtime::services::EventLoopBumpSuspended(1);
  void *payload = polyglot::runtime::services::EventLoopTakeResolved(
      static_cast<std::uint64_t>(id));
  if (payload == nullptr) {
    polyglot::runtime::services::RunUntilIdle(0);
    payload = polyglot::runtime::services::EventLoopTakeResolved(
        static_cast<std::uint64_t>(id));
  }
  return payload;
}

void __ploy_rt_future_resolve(uint64_t future_id, void *payload) {
  polyglot::runtime::services::ResolveFuture(future_id, payload);
}

size_t __ploy_rt_async_run(size_t max_ticks) {
  return polyglot::runtime::services::RunUntilIdle(max_ticks);
}

size_t __ploy_rt_async_pending(void) {
  return polyglot::runtime::services::SnapshotScheduler().pending_tasks;
}

size_t __ploy_rt_async_suspended(void) {
  return polyglot::runtime::services::SnapshotScheduler().suspended_tasks;
}

size_t __ploy_rt_async_completed(void) {
  return polyglot::runtime::services::SnapshotScheduler().completed_tasks;
}

size_t __ploy_rt_async_active_frames(void) {
  return polyglot::runtime::services::SnapshotScheduler().active_async_frames;
}

} // extern "C"
