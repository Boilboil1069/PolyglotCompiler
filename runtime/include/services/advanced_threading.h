#pragma once

#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <vector>

#include "runtime/include/services/threading.h"

namespace polyglot::runtime::services {

// 高级线程服务

// 1. 线程池（Thread Pool）
class ThreadPool {
 public:
  explicit ThreadPool(size_t num_threads = 0);  // 0 = hardware concurrency
  ~ThreadPool();

  // 提交任务
  template <typename F>
  auto Submit(F&& task) -> std::future<decltype(task())>;

  // 等待所有任务完成
  void Wait();

  // 获取工作线程数
  size_t NumThreads() const { return num_threads_; }

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  size_t num_threads_;
};

// 2. 任务调度器（Task Scheduler）
class TaskScheduler {
 public:
  enum Priority { kLow, kNormal, kHigh, kRealtime };

  struct Task {
    std::function<void()> func;
    Priority priority{kNormal};
    std::vector<size_t> dependencies;  // 依赖的任务ID
    size_t id{0};
  };

  TaskScheduler();

  // 调度任务
  size_t Schedule(Task task);

  // 执行所有任务
  void Execute();

  // 取消任务
  bool Cancel(size_t task_id);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// 3. 工作窃取调度器（Work-Stealing Scheduler）
class WorkStealingScheduler {
 public:
  explicit WorkStealingScheduler(size_t num_workers);

  // 并行for循环
  template <typename F>
  void ParallelFor(size_t start, size_t end, F&& body);

  // 并行reduce
  template <typename T, typename F>
  T ParallelReduce(size_t start, size_t end, T init, F&& reducer);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// 4. 同步原语

// 读写锁（Reader-Writer Lock）
class RWLock {
 public:
  void ReadLock();
  void ReadUnlock();
  void WriteLock();
  void WriteUnlock();

  // RAII辅助类
  class ReadGuard {
   public:
    explicit ReadGuard(RWLock &lock) : lock_(lock) { lock_.ReadLock(); }
    ~ReadGuard() { lock_.ReadUnlock(); }
   private:
    RWLock &lock_;
  };

  class WriteGuard {
   public:
    explicit WriteGuard(RWLock &lock) : lock_(lock) { lock_.WriteLock(); }
    ~WriteGuard() { lock_.WriteUnlock(); }
   private:
    RWLock &lock_;
  };

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// 屏障（Barrier）
class Barrier {
 public:
  explicit Barrier(size_t num_threads);
  
  // 等待所有线程到达
  void Wait();

  // 重置屏障
  void Reset(size_t num_threads);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// 信号量（Semaphore）
class Semaphore {
 public:
  explicit Semaphore(int initial_count = 0);

  void Wait();
  void Signal();
  bool TryWait();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// 5. 无锁数据结构

// 无锁队列（Lock-Free Queue）
template <typename T>
class LockFreeQueue {
 public:
  LockFreeQueue();
  ~LockFreeQueue();

  void Push(const T &value);
  bool Pop(T &value);
  bool Empty() const;

 private:
  struct Node;
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// 无锁栈（Lock-Free Stack）
template <typename T>
class LockFreeStack {
 public:
  LockFreeStack();
  ~LockFreeStack();

  void Push(const T &value);
  bool Pop(T &value);
  bool Empty() const;

 private:
  struct Node;
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// 6. 线程局部存储（Thread-Local Storage）
template <typename T>
class ThreadLocal {
 public:
  ThreadLocal() = default;
  explicit ThreadLocal(T value);

  T &Get();
  void Set(const T &value);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// 7. 协程（Coroutines）支持
class Coroutine {
 public:
  enum State { kSuspended, kRunning, kCompleted };

  virtual ~Coroutine() = default;

  // 恢复执行
  virtual void Resume() = 0;

  // 挂起
  virtual void Yield() = 0;

  // 获取状态
  virtual State GetState() const = 0;

  // 是否完成
  bool IsCompleted() const { return GetState() == kCompleted; }
};

// 协程调度器
class CoroutineScheduler {
 public:
  // 创建协程
  template <typename F>
  std::shared_ptr<Coroutine> Create(F&& func);

  // 调度协程
  void Schedule(std::shared_ptr<Coroutine> coro);

  // 运行调度器
  void Run();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// 8. 异步I/O

// 异步任务（Future/Promise）
template <typename T>
class Future {
 public:
  Future();

  // 等待结果
  T Get();

  // 检查是否就绪
  bool IsReady() const;

  // 设置回调
  template <typename F>
  void Then(F&& callback);

 private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

template <typename T>
class Promise {
 public:
  Promise();

  // 设置值
  void SetValue(const T &value);

  // 设置异常
  void SetException(std::exception_ptr ex);

  // 获取关联的Future
  Future<T> GetFuture();

 private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

// 9. 内存序（Memory Order）控制

enum class MemoryOrder {
  kRelaxed,
  kAcquire,
  kRelease,
  kAcqRel,
  kSeqCst
};

// 原子操作包装
template <typename T>
class Atomic {
 public:
  Atomic() : value_() {}
  explicit Atomic(T value) : value_(value) {}

  T Load(MemoryOrder order = MemoryOrder::kSeqCst) const;
  void Store(T value, MemoryOrder order = MemoryOrder::kSeqCst);
  T Exchange(T value, MemoryOrder order = MemoryOrder::kSeqCst);
  bool CompareExchange(T &expected, T desired,
                      MemoryOrder success = MemoryOrder::kSeqCst,
                      MemoryOrder failure = MemoryOrder::kSeqCst);

  T FetchAdd(T arg, MemoryOrder order = MemoryOrder::kSeqCst);
  T FetchSub(T arg, MemoryOrder order = MemoryOrder::kSeqCst);

 private:
  std::atomic<T> value_;
};

// 10. 线程性能分析

struct ThreadStats {
  size_t num_tasks_executed{0};
  size_t num_steals{0};          // 工作窃取次数
  size_t total_exec_time_us{0};  // 总执行时间（微秒）
  size_t idle_time_us{0};        // 空闲时间
};

class ThreadProfiler {
 public:
  static void StartProfiling();
  static void StopProfiling();
  static ThreadStats GetStats(size_t thread_id);
  static void ResetStats();
};

}  // namespace polyglot::runtime::services
