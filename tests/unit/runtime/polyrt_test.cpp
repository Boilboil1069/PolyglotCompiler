#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cstring>
#include <vector>

#include "runtime/include/gc/heap.h"
#include "runtime/include/gc/gc_api.h"
#include "runtime/include/services/threading.h"
#include "runtime/include/interop/container_marshal.h"

using namespace polyglot::runtime;
using namespace polyglot::runtime::gc;
using namespace polyglot::runtime::interop;

// ============================================================================
// GCStats API tests
// ============================================================================

TEST_CASE("GCStats - Default constructed stats are zeroed", "[runtime][gc][stats]") {
    GCStats stats{};
    REQUIRE(stats.total_allocations == 0);
    REQUIRE(stats.total_bytes_allocated == 0);
    REQUIRE(stats.current_heap_bytes == 0);
    REQUIRE(stats.peak_heap_bytes == 0);
    REQUIRE(stats.collections == 0);
    REQUIRE(stats.total_freed_bytes == 0);
    REQUIRE(stats.live_objects == 0);
    REQUIRE(stats.root_count == 0);
}

TEST_CASE("GCStats - MarkSweep tracks allocations", "[runtime][gc][stats]") {
    Heap heap(Strategy::kMarkSweep);

    void *p1 = heap.Allocate(128);
    void *p2 = heap.Allocate(256);
    REQUIRE(p1 != nullptr);
    REQUIRE(p2 != nullptr);

    GCStats stats = heap.GetStats();
    REQUIRE(stats.total_allocations >= 2);
    REQUIRE(stats.total_bytes_allocated >= 384);
    REQUIRE(stats.current_heap_bytes > 0);
}

TEST_CASE("GCStats - Generational tracks allocations", "[runtime][gc][stats]") {
    Heap heap(Strategy::kGenerational);

    void *p1 = heap.Allocate(64);
    void *p2 = heap.Allocate(64);
    REQUIRE(p1 != nullptr);
    REQUIRE(p2 != nullptr);

    GCStats stats = heap.GetStats();
    REQUIRE(stats.total_allocations >= 2);
    REQUIRE(stats.total_bytes_allocated >= 128);
}

TEST_CASE("GCStats - Copying tracks allocations", "[runtime][gc][stats]") {
    Heap heap(Strategy::kCopying);

    void *p1 = heap.Allocate(32);
    REQUIRE(p1 != nullptr);

    GCStats stats = heap.GetStats();
    REQUIRE(stats.total_allocations >= 1);
    REQUIRE(stats.total_bytes_allocated >= 32);
}

TEST_CASE("GCStats - Incremental tracks allocations", "[runtime][gc][stats]") {
    Heap heap(Strategy::kIncremental);

    void *p1 = heap.Allocate(512);
    REQUIRE(p1 != nullptr);

    GCStats stats = heap.GetStats();
    REQUIRE(stats.total_allocations >= 1);
    REQUIRE(stats.total_bytes_allocated >= 512);
}

TEST_CASE("GCStats - Collection count increments", "[runtime][gc][stats]") {
    std::vector<Strategy> strategies = {
        Strategy::kMarkSweep,
        Strategy::kGenerational,
        Strategy::kCopying,
        Strategy::kIncremental
    };

    for (auto strategy : strategies) {
        Heap heap(strategy);

        heap.Allocate(64);
        heap.Collect();

        GCStats stats = heap.GetStats();
        REQUIRE(stats.collections >= 1);
    }
}

TEST_CASE("GCStats - Peak heap is monotonic", "[runtime][gc][stats]") {
    Heap heap(Strategy::kMarkSweep);

    heap.Allocate(1024);
    GCStats stats1 = heap.GetStats();

    heap.Allocate(2048);
    GCStats stats2 = heap.GetStats();

    REQUIRE(stats2.peak_heap_bytes >= stats1.peak_heap_bytes);
}

// ============================================================================
// ThreadProfiler tests
// ============================================================================

using namespace polyglot::runtime::services;

TEST_CASE("ThreadProfiler - Start and stop without crash", "[runtime][threading][profiler]") {
    ThreadProfiler::ResetStats();
    ThreadProfiler::StartProfiling();
    ThreadProfiler::StopProfiling();
    // After stop, stats should still be queryable with zeroed initial values
    ThreadStats stats = ThreadProfiler::GetStats(0);
    REQUIRE(stats.num_tasks_executed == 0);
}

TEST_CASE("ThreadProfiler - Reset clears statistics", "[runtime][threading][profiler]") {
    ThreadProfiler::ResetStats();
    ThreadProfiler::StartProfiling();
    ThreadProfiler::StopProfiling();

    ThreadProfiler::ResetStats();

    // After reset, stats for any thread id should be zeroed.
    ThreadStats stats = ThreadProfiler::GetStats(0);
    REQUIRE(stats.num_tasks_executed == 0);
    REQUIRE(stats.num_steals == 0);
    REQUIRE(stats.total_exec_time_us == 0);
    REQUIRE(stats.idle_time_us == 0);
}

TEST_CASE("ThreadProfiler - GetStats returns zeroed stats for unknown thread",
           "[runtime][threading][profiler]") {
    ThreadProfiler::ResetStats();

    ThreadStats stats = ThreadProfiler::GetStats(99999);
    REQUIRE(stats.num_tasks_executed == 0);
    REQUIRE(stats.num_steals == 0);
    REQUIRE(stats.total_exec_time_us == 0);
    REQUIRE(stats.idle_time_us == 0);
}

// ============================================================================
// Container marshal conversion tests
// ============================================================================

TEST_CASE("Container marshal - cppvec_to_list converts buffer", "[runtime][interop][container]") {
    // Create a small array of integers and convert it to a RuntimeList.
    int data[] = {10, 20, 30, 40, 50};
    void *list = __ploy_rt_convert_cppvec_to_list(data, 5, sizeof(int));
    REQUIRE(list != nullptr);

    // Verify the resulting RuntimeList has the correct element count.
    RuntimeList *rt_list = static_cast<RuntimeList *>(list);
    REQUIRE(rt_list->count == 5);
    REQUIRE(rt_list->elem_size == sizeof(int));

    // Verify element values via the data buffer.
    const int *elems = static_cast<const int *>(rt_list->data);
    for (int i = 0; i < 5; ++i) {
        REQUIRE(elems[i] == data[i]);
    }
}

TEST_CASE("Container marshal - list_generic converts buffer", "[runtime][interop][container]") {
    double data[] = {1.0, 2.5, 3.75};
    void *list = __ploy_rt_convert_list_generic(data, 3, sizeof(double));
    REQUIRE(list != nullptr);

    RuntimeList *rt_list = static_cast<RuntimeList *>(list);
    REQUIRE(rt_list->count == 3);
    REQUIRE(rt_list->elem_size == sizeof(double));

    const double *elems = static_cast<const double *>(rt_list->data);
    REQUIRE(elems[0] == Catch::Approx(1.0));
    REQUIRE(elems[1] == Catch::Approx(2.5));
    REQUIRE(elems[2] == Catch::Approx(3.75));
}

TEST_CASE("Container marshal - cppvec_to_list with count zero", "[runtime][interop][container]") {
    // Zero-count conversion should still succeed with an empty list.
    void *list = __ploy_rt_convert_cppvec_to_list(nullptr, 0, sizeof(int));
    REQUIRE(list != nullptr);

    RuntimeList *rt_list = static_cast<RuntimeList *>(list);
    REQUIRE(rt_list->count == 0);
}

TEST_CASE("Container marshal - list_generic with single element", "[runtime][interop][container]") {
    char data[] = {'Z'};
    void *list = __ploy_rt_convert_list_generic(data, 1, sizeof(char));
    REQUIRE(list != nullptr);

    RuntimeList *rt_list = static_cast<RuntimeList *>(list);
    REQUIRE(rt_list->count == 1);
    REQUIRE(rt_list->elem_size == sizeof(char));
    REQUIRE(static_cast<const char *>(rt_list->data)[0] == 'Z');
}
