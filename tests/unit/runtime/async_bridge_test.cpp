/**
 * @file     async_bridge_test.cpp
 * @brief    Unit tests for the cooperative async/Future runtime bridge.
 *           Covers the C ABI surface invoked by code lowered from
 *           `ASYNC` / `AWAIT` plus the C++ helpers consumed by the
 *           `polyrt async` introspection command.
 *
 * @ingroup  Tests / Unit / Runtime
 * @author   Manning Cyrus
 * @date     2026-05-04
 */
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>
#include <cstring>

#include "runtime/include/services/async_bridge.h"

using polyglot::runtime::services::ResetScheduler;
using polyglot::runtime::services::RunUntilIdle;
using polyglot::runtime::services::SnapshotScheduler;
using polyglot::runtime::services::SpawnPloyTask;

TEST_CASE("async_bridge: enter/complete tracks active frames",
          "[runtime][async-bridge]") {
    ResetScheduler();
    auto baseline = SnapshotScheduler().active_async_frames;
    __ploy_rt_async_enter();
    __ploy_rt_async_enter();
    CHECK(SnapshotScheduler().active_async_frames == baseline + 2);
    __ploy_rt_async_complete();
    __ploy_rt_async_complete();
    CHECK(SnapshotScheduler().active_async_frames == baseline);
}

TEST_CASE("async_bridge: SpawnPloyTask runs on RunUntilIdle",
          "[runtime][async-bridge]") {
    ResetScheduler();
    std::atomic<int> counter{0};
    SpawnPloyTask([&counter]() { counter.fetch_add(1); });
    SpawnPloyTask([&counter]() { counter.fetch_add(2); });
    SpawnPloyTask([&counter]() { counter.fetch_add(4); });

    auto snap_before = SnapshotScheduler();
    CHECK(snap_before.pending_tasks == 3);

    std::size_t completed = RunUntilIdle(64);
    CHECK(completed == 3);
    CHECK(counter.load() == 7);

    auto snap_after = SnapshotScheduler();
    CHECK(snap_after.pending_tasks == 0);
    CHECK(snap_after.completed_tasks >= 3);
}

TEST_CASE("async_bridge: __ploy_rt_async_run drives the C ABI loop",
          "[runtime][async-bridge]") {
    ResetScheduler();
    std::atomic<int> seen{0};
    SpawnPloyTask([&seen]() { seen.fetch_add(1); });
    SpawnPloyTask([&seen]() { seen.fetch_add(1); });

    std::size_t completed = __ploy_rt_async_run(0);
    CHECK(completed == 2);
    CHECK(seen.load() == 2);
    CHECK(__ploy_rt_async_pending() == 0);
}

TEST_CASE("async_bridge: future resolution surfaces payload via AWAIT",
          "[runtime][async-bridge]") {
    ResetScheduler();
    int sentinel = 1234;
    // The current bridge lowers AWAIT to a polled lookup against the
    // resolved-payload table; resolving by id ahead of the lookup must
    // surface the stored pointer.
    __ploy_rt_future_resolve(42, &sentinel);
    void *payload = __ploy_rt_await(reinterpret_cast<void *>(static_cast<std::uintptr_t>(42)));
    REQUIRE(payload != nullptr);
    CHECK(*static_cast<int *>(payload) == 1234);
}

TEST_CASE("async_bridge: spawn from C ABI accepts opaque user data",
          "[runtime][async-bridge]") {
    ResetScheduler();
    static std::atomic<int> hit{0};
    int payload = 7;
    auto fn = [](void *p) {
        hit.fetch_add(*static_cast<int *>(p));
    };
    auto id = __ploy_rt_async_spawn(fn, &payload);
    CHECK(id != 0);
    __ploy_rt_async_run(0);
    CHECK(hit.load() == 7);
}
