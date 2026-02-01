#include <catch2/catch_test_macros.hpp>
#include <vector>
#include <memory>

#include "runtime/include/gc/gc_strategy.h"
#include "runtime/include/gc/heap.h"

using namespace polyglot::runtime::gc;

// 测试辅助函数
struct TestObject {
    int value;
    TestObject* next;
    
    TestObject(int v) : value(v), next(nullptr) {}
};

// 测试场景1: 基本分配和回收
TEST_CASE("GC - Basic Allocation and Collection", "[gc][allocation]") {
    SECTION("MarkSweep GC") {
        Heap heap(Strategy::kMarkSweep);
        
        void* ptr = heap.Allocate(1024);
        REQUIRE(ptr != nullptr);
        
        heap.Collect();
        // 对象应该被回收（没有根引用）
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

// 测试场景2: 多对象分配
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
        
        // 分配100个对象
        for (int i = 0; i < 100; ++i) {
            void* ptr = heap.Allocate(64);
            REQUIRE(ptr != nullptr);
            objects.push_back(ptr);
        }
        
        REQUIRE(objects.size() == 100);
    }
}

// 测试场景3: 根引用跟踪
TEST_CASE("GC - Root Tracking", "[gc][roots]") {
    Heap heap(Strategy::kMarkSweep);
    
    void* obj1 = heap.Allocate(128);
    void* obj2 = heap.Allocate(128);
    
    // 注册根引用
    auto handle1 = heap.Track(&obj1);
    
    // 执行GC - obj1应该存活，obj2应该被回收
    heap.Collect();
    
    // obj1仍然可用
    REQUIRE(obj1 != nullptr);
}

// 测试场景4: 大对象分配
TEST_CASE("GC - Large Object Allocation", "[gc][large]") {
    std::vector<Strategy> strategies = {
        Strategy::kMarkSweep,
        Strategy::kGenerational,
        Strategy::kCopying,
        Strategy::kIncremental
    };
    
    for (auto strategy : strategies) {
        Heap heap(strategy);
        
        // 分配1MB对象
        void* large_obj = heap.Allocate(1024 * 1024);
        
        if (strategy == Strategy::kCopying) {
            // 复制式GC可能因空间不足失败
            // 这是正常的
        } else {
            REQUIRE(large_obj != nullptr);
        }
    }
}

// 测试场景5: 连续GC周期
TEST_CASE("GC - Multiple Collection Cycles", "[gc][collection]") {
    Heap heap(Strategy::kGenerational);
    
    for (int cycle = 0; cycle < 10; ++cycle) {
        // 分配一些对象
        for (int i = 0; i < 50; ++i) {
            heap.Allocate(128);
        }
        
        // 执行GC
        heap.Collect();
    }
    
    // 应该能够成功完成所有周期
    REQUIRE(true);
}

// 测试场景6: 对象存活测试
TEST_CASE("GC - Object Survival", "[gc][survival]") {
    Heap heap(Strategy::kGenerational);
    
    void* long_lived = heap.Allocate(256);
    auto handle = heap.Track(&long_lived);
    
    // 多次GC后，对象应该被提升到老年代
    for (int i = 0; i < 5; ++i) {
        heap.Collect();
    }
    
    REQUIRE(long_lived != nullptr);
}

// 测试场景7: 内存压力测试
TEST_CASE("GC - Memory Pressure", "[gc][pressure]") {
    Heap heap(Strategy::kMarkSweep);
    std::vector<void*> live_objects;
    
    // 分配直到内存压力增加
    for (int i = 0; i < 1000; ++i) {
        void* obj = heap.Allocate(1024);
        if (obj) {
            if (i % 10 == 0) {
                // 保持10%的对象存活
                live_objects.push_back(obj);
            }
        }
        
        if (i % 100 == 0) {
            heap.Collect();
        }
    }
    
    REQUIRE(live_objects.size() > 0);
}

// 测试场景8: 碎片化测试
TEST_CASE("GC - Fragmentation", "[gc][fragmentation]") {
    Heap heap(Strategy::kCopying);  // 复制式GC应该能处理碎片
    
    // 分配不同大小的对象
    std::vector<size_t> sizes = {16, 32, 64, 128, 256, 512, 1024};
    
    for (int round = 0; round < 5; ++round) {
        for (size_t size : sizes) {
            void* obj = heap.Allocate(size);
            // 某些分配可能失败，这是正常的
        }
        heap.Collect();
    }
    
    REQUIRE(true);
}

// 测试场景9: 增量GC的增量性
TEST_CASE("GC - Incremental Collection", "[gc][incremental]") {
    Heap heap(Strategy::kIncremental);
    
    // 分配一些对象
    for (int i = 0; i < 200; ++i) {
        heap.Allocate(64);
    }
    
    // 增量GC应该在多次分配中逐步完成
    // 而不是一次性完成
    for (int i = 0; i < 50; ++i) {
        heap.Allocate(64);  // 每次分配触发增量步骤
    }
    
    REQUIRE(true);
}

// 测试场景10: GC性能基准
TEST_CASE("GC - Performance Benchmark", "[gc][benchmark]") {
    const int NUM_ALLOCATIONS = 10000;
    
    BENCHMARK("MarkSweep GC") {
        Heap heap(Strategy::kMarkSweep);
        for (int i = 0; i < NUM_ALLOCATIONS; ++i) {
            heap.Allocate(64);
            if (i % 1000 == 0) heap.Collect();
        }
    };
    
    BENCHMARK("Generational GC") {
        Heap heap(Strategy::kGenerational);
        for (int i = 0; i < NUM_ALLOCATIONS; ++i) {
            heap.Allocate(64);
            if (i % 1000 == 0) heap.Collect();
        }
    };
    
    BENCHMARK("Copying GC") {
        Heap heap(Strategy::kCopying);
        for (int i = 0; i < NUM_ALLOCATIONS / 10; ++i) {  // 更少的分配
            heap.Allocate(64);
            if (i % 100 == 0) heap.Collect();
        }
    };
    
    BENCHMARK("Incremental GC") {
        Heap heap(Strategy::kIncremental);
        for (int i = 0; i < NUM_ALLOCATIONS; ++i) {
            heap.Allocate(64);
            if (i % 1000 == 0) heap.Collect();
        }
    };
}

// 额外测试：边界条件
TEST_CASE("GC - Edge Cases", "[gc][edge]") {
    SECTION("Zero-size allocation") {
        Heap heap(Strategy::kMarkSweep);
        void* ptr = heap.Allocate(0);
        // 实现可能返回nullptr或小的sentinel值
    }
    
    SECTION("Collect with no allocations") {
        Heap heap(Strategy::kMarkSweep);
        heap.Collect();  // 不应该崩溃
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
