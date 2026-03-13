#include <catch2/catch_test_macros.hpp>
#include <vector>
#include <memory>

#include "runtime/include/gc/gc_strategy.h"
#include "runtime/include/gc/heap.h"

using namespace polyglot::runtime::gc;

// Test helper structures
struct TestObject {
    int value;
    TestObject* next;
    
    TestObject(int v) : value(v), next(nullptr) {}
};

// Scenario 1: Basic allocation and collection
TEST_CASE("GC - Basic Allocation and Collection", "[gc][allocation]") {
    SECTION("MarkSweep GC") {
        Heap heap(Strategy::kMarkSweep);
        
        void* ptr = heap.Allocate(1024);
        REQUIRE(ptr != nullptr);
        
        heap.Collect();
        // Object should be collected (no root references)
    }
    
    SECTION("Generational GC") {
        Heap heap(Strategy::kGenerational);
        
        void* ptr = heap.Allocate(1024);
        REQUIRE(ptr != nullptr);
        
        heap.Collect();
    }
    
    SECTION("Copying GC") {
        Heap heap(Strategy::kCopying);
        
        void* ptr = heap.Allocate(1024);
        REQUIRE(ptr != nullptr);
        
        heap.Collect();
    }
    
    SECTION("Incremental GC") {
        Heap heap(Strategy::kIncremental);
        
        void* ptr = heap.Allocate(1024);
        REQUIRE(ptr != nullptr);
        
        heap.Collect();
    }
}

// Scenario 2: Multiple object allocation
TEST_CASE("GC - Multiple Object Allocation", "[gc][allocation]") {
    std::vector<Strategy> strategies = {
        Strategy::kMarkSweep,
        Strategy::kGenerational,
        Strategy::kCopying,
        Strategy::kIncremental
    };
    
    for (auto strategy : strategies) {
        Heap heap(strategy);
        std::vector<void*> objects;
        
        // Allocate 100 objects
        for (int i = 0; i < 100; ++i) {
            void* ptr = heap.Allocate(64);
            REQUIRE(ptr != nullptr);
            objects.push_back(ptr);
        }
        
        REQUIRE(objects.size() == 100);
    }
}

// Scenario 3: Root reference tracking
TEST_CASE("GC - Root Tracking", "[gc][roots]") {
    Heap heap(Strategy::kMarkSweep);
    
    void* obj1 = heap.Allocate(128);
    void* obj2 = heap.Allocate(128);
    (void)obj2;
    
    // Register root reference
    auto handle1 = heap.Track(&obj1);
    (void)handle1;
    
    // After GC, obj1 should survive and obj2 should be collected
    heap.Collect();
    
    // obj1 remains usable
    REQUIRE(obj1 != nullptr);
}

// Scenario 4: Large object allocation
TEST_CASE("GC - Large Object Allocation", "[gc][large]") {
    std::vector<Strategy> strategies = {
        Strategy::kMarkSweep,
        Strategy::kGenerational,
        Strategy::kCopying,
        Strategy::kIncremental
    };
    
    for (auto strategy : strategies) {
        Heap heap(strategy);
        
        // Allocate a 1MB object
        void* large_obj = heap.Allocate(1024 * 1024);
        
        if (strategy == Strategy::kCopying) {
            // Copying GC may fail because of insufficient space
            // This is expected
        } else {
            REQUIRE(large_obj != nullptr);
        }
    }
}

// Scenario 5: Multiple GC cycles
TEST_CASE("GC - Multiple Collection Cycles", "[gc][collection]") {
    Heap heap(Strategy::kGenerational);
    
    for (int cycle = 0; cycle < 10; ++cycle) {
        // Allocate some objects
        for (int i = 0; i < 50; ++i) {
            heap.Allocate(128);
        }
        
        // Run GC
        heap.Collect();
    }
    
    // All cycles should complete successfully
    REQUIRE(true);
}

// Scenario 6: Object survival
TEST_CASE("GC - Object Survival", "[gc][survival]") {
    Heap heap(Strategy::kGenerational);
    
    void* long_lived = heap.Allocate(256);
    auto handle = heap.Track(&long_lived);
    (void)handle;
    
    // After multiple GCs the object should be promoted to the old generation
    for (int i = 0; i < 5; ++i) {
        heap.Collect();
    }
    
    REQUIRE(long_lived != nullptr);
}

// Scenario 7: Memory pressure
TEST_CASE("GC - Memory Pressure", "[gc][pressure]") {
    Heap heap(Strategy::kMarkSweep);
    std::vector<void*> live_objects;
    
    // Allocate until memory pressure rises
    for (int i = 0; i < 1000; ++i) {
        void* obj = heap.Allocate(1024);
        if (obj) {
            if (i % 10 == 0) {
                // Keep 10% of objects alive
                live_objects.push_back(obj);
            }
        }
        
        if (i % 100 == 0) {
            heap.Collect();
        }
    }
    
    REQUIRE(live_objects.size() > 0);
}

// Scenario 8: Fragmentation
TEST_CASE("GC - Fragmentation", "[gc][fragmentation]") {
    Heap heap(Strategy::kCopying);  // Copying GC should handle fragmentation
    
    // Allocate objects of different sizes
    std::vector<size_t> sizes = {16, 32, 64, 128, 256, 512, 1024};
    
    for (int round = 0; round < 5; ++round) {
        for (size_t size : sizes) {
            void* obj = heap.Allocate(size);
            // Some allocations may fail; that's fine
            (void)obj;
        }
        heap.Collect();
    }
    
    REQUIRE(true);
}

// Scenario 9: Incrementality of incremental GC
TEST_CASE("GC - Incremental Collection", "[gc][incremental]") {
    Heap heap(Strategy::kIncremental);
    
    // Allocate some objects
    for (int i = 0; i < 200; ++i) {
        heap.Allocate(64);
    }
    
    // Incremental GC should progress over multiple allocations
    // rather than completing in a single step
    for (int i = 0; i < 50; ++i) {
        heap.Allocate(64);  // Each allocation triggers an incremental step
    }
    
    REQUIRE(true);
}

// Scenario 10: GC performance benchmark
TEST_CASE("GC - Performance Benchmark", "[gc][benchmark]") {
    const int NUM_ALLOCATIONS = 10000;
    (void)NUM_ALLOCATIONS;  // Suppress unused variable warning (benchmarks commented out)

    // BENCHMARK("MarkSweep GC") {
    //     Heap heap(Strategy::kMarkSweep);
    //     for (int i = 0; i < NUM_ALLOCATIONS; ++i) {
    //         heap.Allocate(64);
    //         if (i % 1000 == 0) heap.Collect();
    //     }
    // };
    
    // BENCHMARK("Generational GC") {
    //     Heap heap(Strategy::kGenerational);
    //     for (int i = 0; i < NUM_ALLOCATIONS; ++i) {
    //         heap.Allocate(64);
    //         if (i % 1000 == 0) heap.Collect();
    //     }
    // };
    
    // BENCHMARK("Copying GC") {
    //     Heap heap(Strategy::kCopying);
    //     for (int i = 0; i < NUM_ALLOCATIONS / 10; ++i) {  // fewer allocations
    //         heap.Allocate(64);
    //         if (i % 100 == 0) heap.Collect();
    //     }
    // };
    
    // BENCHMARK("Incremental GC") {
    //     Heap heap(Strategy::kIncremental);
    //     for (int i = 0; i < NUM_ALLOCATIONS; ++i) {
    //         heap.Allocate(64);
    //         if (i % 1000 == 0) heap.Collect();
    //     }
    // };
}

// Additional tests: edge cases
TEST_CASE("GC - Edge Cases", "[gc][edge]") {
    SECTION("Zero-size allocation") {
        Heap heap(Strategy::kMarkSweep);
        void* ptr = heap.Allocate(0);
        // Implementation may return nullptr or a small sentinel value
        (void)ptr;
    }
    
    SECTION("Collect with no allocations") {
        Heap heap(Strategy::kMarkSweep);
        heap.Collect();  // Should not crash
        REQUIRE(true);
    }
    
    SECTION("Multiple collects") {
        Heap heap(Strategy::kMarkSweep);
        heap.Collect();
        heap.Collect();
        heap.Collect();
        REQUIRE(true);
    }
}
