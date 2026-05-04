/**
 * @file     async_bridge.h
 * @brief    Cross-language asynchronous task / Future bridge for the
 *           polyrt runtime.  Implements the C ABI invoked by code
 *           lowered from Ploy's `ASYNC` / `AWAIT` constructs and the
 *           entry points used by per-language adapters to expose
 *           Python `asyncio` coroutines, C++20 `std::coroutine`
 *           awaitables, Java `CompletableFuture`, .NET `Task<T>` and
 *           Rust `Future` instances as unified Ploy `Future<T>`
 *           handles.
 *
 * @ingroup  Runtime / Services
 * @author   Manning Cyrus
 * @date     2026-05-04
 */
#pragma once

#include <cstddef>
#include <cstdint>

#ifdef __cplusplus
#include <functional>
#include <memory>

namespace polyglot::runtime::services {

// A future handle as observed by the runtime bridge.  The cooperative
// scheduler stores task-state pointers behind these opaque handles;
// language adapters wrap their native awaitables in the same payload
// shape so AWAIT can drive them uniformly.
struct FutureHandle {
  std::uint64_t id{0};
  bool ready{false};
  void *payload{nullptr};
  std::string source_lang;  // "ploy" | "python" | "cpp" | "java" | "dotnet" | "rust"
};

// Snapshot of the cooperative event loop, used by `polyrt async` and
// the test suite to inspect scheduler state without taking ownership
// of any internals.
struct AsyncSchedulerSnapshot {
  std::size_t pending_tasks{0};      // tasks queued but not yet run
  std::size_t suspended_tasks{0};    // tasks parked on an unresolved AWAIT
  std::size_t completed_tasks{0};    // tasks that ran to completion
  std::size_t loop_iterations{0};    // total scheduler tick count
  std::size_t active_async_frames{0};// frames currently in __ploy_rt_async_enter
};

// Spawn a Ploy task (a callable with no arguments returning void) on
// the cooperative event loop and return its FutureHandle id.  The
// callable is captured by value; the runtime owns the heap-allocated
// state until the task completes and the future is consumed.
std::uint64_t SpawnPloyTask(std::function<void()> task);

// Resolve a future with a payload pointer.  Subsequent AWAITs on the
// same future will see `ready=true` and receive the payload.  The
// runtime assumes ownership semantics are managed by the caller.
void ResolveFuture(std::uint64_t future_id, void *payload);

// Drive the event loop until either every spawned task completes or
// `max_ticks` iterations elapse.  Returns the number of tasks that
// completed during this drive.  Re-entrant calls are guarded so the
// scheduler cannot recursively pump itself.
std::size_t RunUntilIdle(std::size_t max_ticks = 1024);

// Inspect the scheduler without mutating it.  Safe to call from the
// CLI (`polyrt async`) and from the test suite at any time.
AsyncSchedulerSnapshot SnapshotScheduler();

// Reset the scheduler to a pristine state.  Intended for tests; the
// production runtime never calls this.
void ResetScheduler();

} // namespace polyglot::runtime::services

extern "C" {
#endif

// =====================================================================
// C ABI consumed by code lowered from `ASYNC FUNC` and `AWAIT`.  All
// symbols are unconditionally exported with C linkage so any backend
// (LLVM, native, WASM) can bind them by name.
// =====================================================================

// Mark entry into an ASYNC function frame.  Bumps the active-frame
// counter recorded in the per-thread scheduler so the runtime knows
// that an in-flight cooperative task is currently executing.  Idempotent
// across re-entrant invocations.
void __ploy_rt_async_enter(void);

// Mark normal completion of an ASYNC function frame.  Decrements the
// active-frame counter; the scheduler will mark the corresponding
// future as resolved on the next tick.
void __ploy_rt_async_complete(void);

// Spawn a callable on the cooperative event loop.  The handler is
// invoked exactly once with the supplied opaque user data; spawning
// returns the new future-handle id.
typedef void (*ploy_async_task_fn)(void *user_data);
uint64_t __ploy_rt_async_spawn(ploy_async_task_fn fn, void *user_data);

// AWAIT entry point.  Suspends the surrounding task until the future
// identified by the opaque handle is resolved, then returns the
// resolved payload.  Returns NULL when the handle is invalid or the
// runtime is being torn down.
void *__ploy_rt_await(void *future_handle);

// Resolve a future from outside (typically a language adapter that
// observes its native awaitable becoming ready).  After this call,
// any AWAIT on the same handle returns immediately with the payload.
void __ploy_rt_future_resolve(uint64_t future_id, void *payload);

// Pump the cooperative scheduler until quiescent or `max_ticks` is
// reached.  Returns the number of tasks that completed during the
// drive.  A `max_ticks` of 0 is treated as the runtime default.
size_t __ploy_rt_async_run(size_t max_ticks);

// Scheduler introspection helpers, mirrored to the C++ snapshot.
size_t __ploy_rt_async_pending(void);
size_t __ploy_rt_async_suspended(void);
size_t __ploy_rt_async_completed(void);
size_t __ploy_rt_async_active_frames(void);

#ifdef __cplusplus
} // extern "C"
#endif
