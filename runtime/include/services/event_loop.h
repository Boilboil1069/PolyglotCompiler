/**
 * @file     event_loop.h
 * @brief    Single-threaded cooperative event loop that backs the
 *           async runtime bridge.  The loop is intentionally minimal:
 *           ready tasks run to their next AWAIT, suspended tasks park
 *           on a future, and resolved futures wake their owning task
 *           on the next tick.  A multi-thread work-stealing extension
 *           can be layered on by sharing this state through the
 *           existing `runtime/threading.{h,cpp}` primitives.
 *
 * @ingroup  Runtime / Services
 * @author   Manning Cyrus
 * @date     2026-05-04
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace polyglot::runtime::services {

// Drive the cooperative event loop for at most `max_ticks` iterations.
// Returns the number of completed tasks.  Calling this from inside a
// task is a no-op (re-entrancy is detected and rejected).
std::size_t EventLoopRun(std::size_t max_ticks);

// Enqueue a callable on the ready queue.  The callable is captured
// by value and executed on a future scheduler tick.  Returns the
// future-handle id that will resolve once the task completes.
std::uint64_t EventLoopSchedule(std::function<void()> task);

// Wake any task suspended on the given future id.  Idempotent — a
// future may be resolved at most once; subsequent calls are dropped.
void EventLoopWake(std::uint64_t future_id, void *payload);

// Total scheduler tick count since process start (or since the most
// recent `ResetScheduler` invocation).
std::size_t EventLoopTickCount();

} // namespace polyglot::runtime::services
