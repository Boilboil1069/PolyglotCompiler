#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>

#include "runtime/include/services/advanced_threading.h"

using namespace polyglot::runtime::services;

// ============ 测试1: 线程池 ============
TEST_CASE("Threading - Thread Pool", "[threading][pool]") {
    SECTION("Basic task submission") {
        ThreadPool pool(4);
        
        auto future = pool.Submit([]() { return 42; });
        
        REQUIRE(future.get() == 42);
    }
    
    SECTION("Multiple tasks") {
        ThreadPool pool(4);
        std::vector<std::future<int>> futures;
        
        for (int i = 0; i < 100; ++i) {
            futures.push_back(pool.Submit([i]() { return i * 2; }));
        }
        
        for (int i = 0; i < 100; ++i) {
            REQUIRE(futures[i].get() == i * 2);
        }
    }
    
    SECTION("Wait for completion") {
        ThreadPool pool(4);
        
        for (int i = 0; i < 50; ++i) {
            pool.Submit([]() { 
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            });
        }
        
        pool.Wait();
        REQUIRE(true);
    }
    
    SECTION("Exception handling") {
        ThreadPool pool(4);
        
        auto future = pool.Submit([]() -> int {
            throw std::runtime_error("test error");
            return 0;
        });
        
        REQUIRE_THROWS_AS(future.get(), std::runtime_error);
    }
    
    SECTION("Thread pool size") {
        ThreadPool pool(8);
        REQUIRE(pool.NumThreads() == 8);
    }
}

// ============ 测试2: 任务调度器 ============
TEST_CASE("Threading - Task Scheduler", "[threading][scheduler]") {
    SECTION("Basic scheduling") {
        TaskScheduler scheduler;
        
        TaskScheduler::Task task;
        task.func = []() { /* work */ };
        task.priority = TaskScheduler::kNormal;
        
        size_t id = scheduler.Schedule(task);
        scheduler.Execute();
        
        REQUIRE(id > 0);
    }
    
    SECTION("Priority scheduling") {
        TaskScheduler scheduler;
        
        std::atomic<int> counter{0};
        
        TaskScheduler::Task high, low;
        high.priority = TaskScheduler::kHigh;
        high.func = [&counter]() { counter += 100; };
        
        low.priority = TaskScheduler::kLow;
        low.func = [&counter]() { counter += 1; };
        
        scheduler.Schedule(low);
        scheduler.Schedule(high);
        scheduler.Execute();
        
        REQUIRE(counter == 101);
    }
    
    SECTION("Task dependencies") {
        TaskScheduler scheduler;
        
        std::atomic<bool> first_done{false};
        
        TaskScheduler::Task first, second;
        first.func = [&first_done]() { first_done = true; };
        second.func = [&first_done]() { REQUIRE(first_done); };
        
        size_t id1 = scheduler.Schedule(first);
        second.dependencies = {id1};
        scheduler.Schedule(second);
        
        scheduler.Execute();
        REQUIRE(true);
    }
    
    SECTION("Task cancellation") {
        TaskScheduler scheduler;
        
        TaskScheduler::Task task;
        task.func = []() { /* work */ };
        
        size_t id = scheduler.Schedule(task);
        bool cancelled = scheduler.Cancel(id);
        
        REQUIRE(cancelled);
    }
    
    SECTION("Multiple tasks") {
        TaskScheduler scheduler;
        
        for (int i = 0; i < 100; ++i) {
            TaskScheduler::Task task;
            task.func = []() { /* work */ };
            scheduler.Schedule(task);
        }
        
        scheduler.Execute();
        REQUIRE(true);
    }
}

// ============ 测试3: 工作窃取调度器 ============
TEST_CASE("Threading - Work Stealing Scheduler", "[threading][worksteal]") {
    SECTION("Parallel for") {
        WorkStealingScheduler scheduler(4);
        std::atomic<int> sum{0};
        
        scheduler.ParallelFor(0, 1000, [&sum](size_t i) {
            sum += i;
        });
        
        REQUIRE(sum == 499500);  // 0 + 1 + ... + 999
    }
    
    SECTION("Parallel reduce") {
        WorkStealingScheduler scheduler(4);
        
        int result = scheduler.ParallelReduce(0, 100, 0, [](int a, int b) {
            return a + b;
        });
        
        REQUIRE(result == 4950);  // 0 + 1 + ... + 99
    }
    
    SECTION("Load balancing") {
        WorkStealingScheduler scheduler(4);
        std::atomic<int> counter{0};
        
        scheduler.ParallelFor(0, 10000, [&counter](size_t i) {
            counter++;
        });
        
        REQUIRE(counter == 10000);
    }
    
    SECTION("Small workload") {
        WorkStealingScheduler scheduler(4);
        std::atomic<int> counter{0};
        
        scheduler.ParallelFor(0, 10, [&counter](size_t i) {
            counter++;
        });
        
        REQUIRE(counter == 10);
    }
    
    SECTION("Large workload") {
        WorkStealingScheduler scheduler(8);
        std::atomic<long long> sum{0};
        
        scheduler.ParallelFor(0, 100000, [&sum](size_t i) {
            sum += i;
        });
        
        REQUIRE(sum > 0);
    }
}

// ============ 测试4: 同步原语 - 读写锁 ============
TEST_CASE("Threading - RWLock", "[threading][rwlock]") {
    SECTION("Basic read lock") {
        RWLock lock;
        
        lock.ReadLock();
        // Critical section
        lock.ReadUnlock();
        
        REQUIRE(true);
    }
    
    SECTION("Basic write lock") {
        RWLock lock;
        
        lock.WriteLock();
        // Critical section
        lock.WriteUnlock();
        
        REQUIRE(true);
    }
    
    SECTION("Multiple readers") {
        RWLock lock;
        std::atomic<int> readers{0};
        
        std::vector<std::thread> threads;
        for (int i = 0; i < 10; ++i) {
            threads.emplace_back([&lock, &readers]() {
                lock.ReadLock();
                readers++;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                readers--;
                lock.ReadUnlock();
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        REQUIRE(readers == 0);
    }
    
    SECTION("RAII guard") {
        RWLock lock;
        
        {
            RWLock::ReadGuard guard(lock);
            // Automatically unlocked
        }
        
        {
            RWLock::WriteGuard guard(lock);
            // Automatically unlocked
        }
        
        REQUIRE(true);
    }
    
    SECTION("Writer exclusion") {
        RWLock lock;
        std::atomic<int> writers{0};
        std::atomic<int> max_concurrent{0};
        
        std::vector<std::thread> threads;
        for (int i = 0; i < 5; ++i) {
            threads.emplace_back([&]() {
                lock.WriteLock();
                writers++;
                int current = writers.load();
                if (current > max_concurrent) {
                    max_concurrent = current;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                writers--;
                lock.WriteUnlock();
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        REQUIRE(max_concurrent == 1);  // Only one writer at a time
    }
}

// ============ 测试5: 同步原语 - 屏障 ============
TEST_CASE("Threading - Barrier", "[threading][barrier]") {
    SECTION("Basic barrier") {
        Barrier barrier(5);
        std::atomic<int> counter{0};
        
        std::vector<std::thread> threads;
        for (int i = 0; i < 5; ++i) {
            threads.emplace_back([&barrier, &counter]() {
                counter++;
                barrier.Wait();
                // All threads have reached this point
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        REQUIRE(counter == 5);
    }
    
    SECTION("Barrier reset") {
        Barrier barrier(3);
        
        std::vector<std::thread> threads;
        for (int i = 0; i < 3; ++i) {
            threads.emplace_back([&barrier]() {
                barrier.Wait();
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        barrier.Reset(3);
        
        threads.clear();
        for (int i = 0; i < 3; ++i) {
            threads.emplace_back([&barrier]() {
                barrier.Wait();
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        REQUIRE(true);
    }
    
    SECTION("Synchronization point") {
        Barrier barrier(4);
        std::atomic<int> before{0};
        std::atomic<int> after{0};
        
        std::vector<std::thread> threads;
        for (int i = 0; i < 4; ++i) {
            threads.emplace_back([&]() {
                before++;
                barrier.Wait();
                REQUIRE(before == 4);
                after++;
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        REQUIRE(after == 4);
    }
}

// ============ 测试6: 无锁数据结构 ============
TEST_CASE("Threading - Lock-Free Queue", "[threading][lockfree]") {
    SECTION("Basic push/pop") {
        LockFreeQueue<int> queue;
        
        queue.Push(42);
        
        int value;
        bool success = queue.Pop(value);
        
        REQUIRE(success);
        REQUIRE(value == 42);
    }
    
    SECTION("Multiple producers") {
        LockFreeQueue<int> queue;
        
        std::vector<std::thread> producers;
        for (int i = 0; i < 10; ++i) {
            producers.emplace_back([&queue, i]() {
                for (int j = 0; j < 100; ++j) {
                    queue.Push(i * 100 + j);
                }
            });
        }
        
        for (auto& t : producers) {
            t.join();
        }
        
        int count = 0;
        int value;
        while (queue.Pop(value)) {
            count++;
        }
        
        REQUIRE(count == 1000);
    }
    
    SECTION("Producer-consumer") {
        LockFreeQueue<int> queue;
        std::atomic<int> sum{0};
        std::atomic<bool> done{false};
        
        std::thread producer([&queue, &done]() {
            for (int i = 0; i < 1000; ++i) {
                queue.Push(i);
            }
            done = true;
        });
        
        std::thread consumer([&queue, &sum, &done]() {
            int value;
            while (!done || !queue.Empty()) {
                if (queue.Pop(value)) {
                    sum += value;
                }
            }
        });
        
        producer.join();
        consumer.join();
        
        REQUIRE(sum == 499500);
    }
}

// ============ 测试7: 协程 ============
TEST_CASE("Threading - Coroutines", "[threading][coroutine]") {
    SECTION("Basic coroutine") {
        CoroutineScheduler scheduler;
        
        bool executed = false;
        auto coro = scheduler.Create([&executed]() {
            executed = true;
        });
        
        scheduler.Schedule(coro);
        scheduler.Run();
        
        REQUIRE(executed);
    }
    
    SECTION("Multiple coroutines") {
        CoroutineScheduler scheduler;
        std::atomic<int> counter{0};
        
        for (int i = 0; i < 10; ++i) {
            auto coro = scheduler.Create([&counter]() {
                counter++;
            });
            scheduler.Schedule(coro);
        }
        
        scheduler.Run();
        
        REQUIRE(counter == 10);
    }
}

// ============ 测试8: Future/Promise ============
TEST_CASE("Threading - Future/Promise", "[threading][future]") {
    SECTION("Basic future") {
        Promise<int> promise;
        Future<int> future = promise.GetFuture();
        
        std::thread t([p = std::move(promise)]() mutable {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            p.SetValue(42);
        });
        
        REQUIRE(future.Get() == 42);
        t.join();
    }
    
    SECTION("Future callback") {
        Promise<int> promise;
        Future<int> future = promise.GetFuture();
        
        bool callback_called = false;
        future.Then([&callback_called](int value) {
            callback_called = true;
            REQUIRE(value == 100);
        });
        
        promise.SetValue(100);
        
        REQUIRE(callback_called);
    }
    
    SECTION("Exception in future") {
        Promise<int> promise;
        Future<int> future = promise.GetFuture();
        
        promise.SetException(std::make_exception_ptr(std::runtime_error("error")));
        
        REQUIRE_THROWS_AS(future.Get(), std::runtime_error);
    }
}

// 性能基准测试
TEST_CASE("Threading - Performance", "[threading][benchmark]") {
    // BENCHMARK("Thread Pool - 1000 tasks") {
        ThreadPool pool(4);
        for (int i = 0; i < 1000; ++i) {
            pool.Submit([]() { return 42; });
        }
        pool.Wait();
    };
    
    // BENCHMARK("Work Stealing - ParallelFor 10000") {
        WorkStealingScheduler scheduler(4);
        scheduler.ParallelFor(0, 10000, [](size_t i) {});
    };
    
    // BENCHMARK("LockFree Queue - 10000 ops") {
        LockFreeQueue<int> queue;
        for (int i = 0; i < 10000; ++i) {
            queue.Push(i);
        }
        int value;
        while (queue.Pop(value)) {}
    };
}
