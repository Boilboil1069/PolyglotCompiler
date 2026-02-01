#pragma once

#include <atomic>
#include <condition_variable>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace polyglot::runtime::services {

// Basic threading helpers
class ThreadLocalStorage {
 public:
  void Set(const std::string &key, void *value) { storage_[key] = value; }
  void *Get(const std::string &key) const {
    auto it = storage_.find(key);
    return it == storage_.end() ? nullptr : it->second;
  }
  void Erase(const std::string &key) { storage_.erase(key); }

 private:
  thread_local static std::unordered_map<std::string, void *> storage_;
};

class Threading {
 public:
  template <typename Fn>
  std::thread Run(Fn &&fn) {
    return std::thread(std::forward<Fn>(fn));
  }

  template <typename Fn>
  void RunDetached(Fn &&fn) {
    std::thread(std::forward<Fn>(fn)).detach();
  }

  void SleepFor(std::chrono::milliseconds duration) { std::this_thread::sleep_for(duration); }
};

using Mutex = std::mutex;
using LockGuard = std::lock_guard<Mutex>;

// Advanced threading services

// 1. Thread pool
class ThreadPool {
 public:
  explicit ThreadPool(size_t num_threads = 0);  // 0 = hardware concurrency
  ~ThreadPool();

  template <typename F>
  auto Submit(F&& task) -> std::future<decltype(task())>;

  void Wait();
  size_t NumThreads() const { return num_threads_; }

 private:
  size_t num_threads_;
  std::vector<std::thread> workers_;
  std::queue<std::function<void()>> tasks_;
  std::mutex mutex_;
  std::condition_variable cv_;
  bool stop_{false};
  std::atomic<size_t> active_{0};
};

// 2. Task scheduler
class TaskScheduler {
 public:
  enum Priority { kLow, kNormal, kHigh, kRealtime };

  struct Task {
    std::function<void()> func;
    Priority priority{kNormal};
    std::vector<size_t> dependencies;
    size_t id{0};
  };

  TaskScheduler();

  size_t Schedule(Task task);
  void Execute();
  bool Cancel(size_t task_id);

 private:
  std::vector<Task> tasks_;
    size_t next_id_{1};
};

// 3. Work-stealing scheduler
class WorkStealingScheduler {
 public:
  explicit WorkStealingScheduler(size_t num_workers);

  template <typename F>
  void ParallelFor(size_t start, size_t end, F&& body);

  template <typename T, typename F>
  T ParallelReduce(size_t start, size_t end, T init, F&& reducer);

 private:
  size_t num_workers_;
};

// 4. Synchronization primitives
class RWLock {
 public:
  void ReadLock();
  void ReadUnlock();
  void WriteLock();
  void WriteUnlock();

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
  std::mutex mutex_;
  std::condition_variable cv_;
  int readers_{0};
  bool writer_{false};
};

class Barrier {
 public:
  explicit Barrier(size_t num_threads);

  void Wait();

  void Reset(size_t num_threads);

  std::mutex mutex_;
  std::condition_variable cv_;
  size_t num_threads_;
  size_t count_{0};
  size_t generation_{0};
};

class Semaphore {
 public:
  explicit Semaphore(int initial_count = 0);

  void Wait();
  void Signal();
  bool TryWait();

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  int count_;
};

// 5. Lock-free data structures
template <typename T>
class LockFreeQueue {
 public:
  LockFreeQueue();
  ~LockFreeQueue();

  void Push(const T &value);
  bool Pop(T &value);
  bool Empty() const;

 private:
  struct Node {
    T value;
    std::atomic<Node*> next;
    Node(const T& val) : value(val), next(nullptr) {}
  };
  std::atomic<Node*> head_;
  std::atomic<Node*> tail_;
};

template <typename T>
class LockFreeStack {
 public:
  LockFreeStack();
  ~LockFreeStack();

  void Push(const T &value);
  bool Pop(T &value);
  bool Empty() const;

 private:
  struct Node {
    T value;
    std::atomic<Node*> next;
    Node(const T& val) : value(val), next(nullptr) {}
  };
  std::atomic<Node*> head_;
};

// 6. Thread-local storage
template <typename T>
class ThreadLocal {
 public:
  ThreadLocal() = default;
  explicit ThreadLocal(T value);

  T &Get();
  void Set(const T &value);

 private:
  std::unordered_map<std::thread::id, T> storage_;
  std::mutex mutex_;
  T initial_{};
};

// 7. Coroutines
class Coroutine {
 public:
  enum State { kSuspended, kRunning, kCompleted };

  virtual ~Coroutine() = default;
  virtual void Resume() = 0;
  virtual void Yield() = 0;
  virtual State GetState() const = 0;
  bool IsCompleted() const { return GetState() == kCompleted; }
};

class CoroutineScheduler {
 public:
  template <typename F>
  std::shared_ptr<Coroutine> Create(F&& func);

  void Schedule(std::shared_ptr<Coroutine> coro);
  void Run();

 private:
  std::vector<std::shared_ptr<Coroutine>> coroutines_;
  std::mutex mutex_;
};

// 8. Async I/O (Future/Promise)
template <typename T>
class Future {
 public:
  Future();
  explicit Future(std::shared_ptr<std::promise<T>> promise);

  T Get();
  bool IsReady() const;

  template <typename F>
  void Then(F&& callback);

 private:
    std::shared_ptr<std::promise<T>> promise_;
    std::shared_future<T> future_;
};

template <typename T>
class Promise {
 public:
  Promise();

  void SetValue(const T &value);
  void SetException(std::exception_ptr ex);
  Future<T> GetFuture();

 private:
  struct Impl;
    std::shared_ptr<std::promise<T>> promise_;
};

// 9. Memory order control
enum class MemoryOrder {
  kRelaxed,
  kAcquire,
  kRelease,
  kAcqRel,
  kSeqCst
};

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

// 10. Thread profiling
struct ThreadStats {
  size_t num_tasks_executed{0};
  size_t num_steals{0};
  size_t total_exec_time_us{0};
  size_t idle_time_us{0};
};

class ThreadProfiler {
 public:
  static void StartProfiling();
  static void StopProfiling();
  static ThreadStats GetStats(size_t thread_id);
  static void ResetStats();
};

}  // namespace polyglot::runtime::services

// Inline/template implementations
namespace polyglot::runtime::services {

inline std::memory_order ToStdOrder(MemoryOrder order) {
  switch (order) {
    case MemoryOrder::kRelaxed: return std::memory_order_relaxed;
    case MemoryOrder::kAcquire: return std::memory_order_acquire;
    case MemoryOrder::kRelease: return std::memory_order_release;
    case MemoryOrder::kAcqRel: return std::memory_order_acq_rel;
    case MemoryOrder::kSeqCst: return std::memory_order_seq_cst;
  }
  return std::memory_order_seq_cst;
}

template <typename F>
auto ThreadPool::Submit(F &&task) -> std::future<decltype(task())> {
  using R = decltype(task());
  auto packaged = std::make_shared<std::packaged_task<R()>>(std::forward<F>(task));
  auto fut = packaged->get_future();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    tasks_.emplace([packaged]() { (*packaged)(); });
  }
  cv_.notify_one();
  return fut;
}

template <typename F>
void WorkStealingScheduler::ParallelFor(size_t start, size_t end, F &&body) {
  size_t workers = num_workers_ == 0 ? std::max<size_t>(1, std::thread::hardware_concurrency()) : num_workers_;
  size_t total = end - start;
  size_t chunk = (total + workers - 1) / workers;
  std::vector<std::thread> threads;
  for (size_t w = 0; w < workers; ++w) {
    size_t s = start + w * chunk;
    if (s >= end) break;
    size_t e = std::min(end, s + chunk);
    threads.emplace_back([=, &body]() {
      for (size_t i = s; i < e; ++i) body(i);
    });
  }
  for (auto &t : threads) t.join();
}

template <typename T, typename F>
T WorkStealingScheduler::ParallelReduce(size_t start, size_t end, T init, F &&reducer) {
  std::mutex m;
  T result = init;
  ParallelFor(start, end, [&](size_t i) {
    std::lock_guard<std::mutex> lock(m);
    result = reducer(result, static_cast<T>(i));
  });
  return result;
}

template <typename T>
LockFreeQueue<T>::LockFreeQueue() {
  Node *dummy = new Node(T{});
  head_.store(dummy);
  tail_.store(dummy);
}

template <typename T>
LockFreeQueue<T>::~LockFreeQueue() {
  T value;
  while (Pop(value)) {}
  delete head_.load();
}

template <typename T>
void LockFreeQueue<T>::Push(const T &value) {
  Node *node = new Node(value);
  while (true) {
    Node *tail = tail_.load(std::memory_order_acquire);
    Node *next = tail->next.load(std::memory_order_acquire);
    if (tail == tail_.load(std::memory_order_acquire)) {
      if (next == nullptr) {
        if (tail->next.compare_exchange_weak(next, node, std::memory_order_release, std::memory_order_relaxed)) {
          tail_.compare_exchange_strong(tail, node, std::memory_order_release, std::memory_order_relaxed);
          return;
        }
      } else {
        tail_.compare_exchange_weak(tail, next, std::memory_order_release, std::memory_order_relaxed);
      }
    }
  }
}

template <typename T>
bool LockFreeQueue<T>::Pop(T &value) {
  while (true) {
    Node *head = head_.load(std::memory_order_acquire);
    Node *tail = tail_.load(std::memory_order_acquire);
    Node *next = head->next.load(std::memory_order_acquire);
    if (head == head_.load(std::memory_order_acquire)) {
      if (head == tail) {
        if (next == nullptr) return false;
        tail_.compare_exchange_weak(tail, next, std::memory_order_release, std::memory_order_relaxed);
      } else {
        value = next->value;
        if (head_.compare_exchange_weak(head, next, std::memory_order_release, std::memory_order_relaxed)) {
          delete head;
          return true;
        }
      }
    }
  }
}

template <typename T>
bool LockFreeQueue<T>::Empty() const {
  Node *head = head_.load(std::memory_order_acquire);
  return head->next.load(std::memory_order_acquire) == nullptr;
}

template <typename T>
LockFreeStack<T>::LockFreeStack() : head_(nullptr) {}

template <typename T>
LockFreeStack<T>::~LockFreeStack() {
  T value;
  while (Pop(value)) {}
}

template <typename T>
void LockFreeStack<T>::Push(const T &value) {
  Node *node = new Node(value);
  node->next.store(head_.load(std::memory_order_relaxed), std::memory_order_relaxed);
  while (!head_.compare_exchange_weak(node->next, node, std::memory_order_release, std::memory_order_relaxed)) {}
}

template <typename T>
bool LockFreeStack<T>::Pop(T &value) {
  Node *node = head_.load(std::memory_order_acquire);
  while (node) {
    Node *next = node->next.load(std::memory_order_relaxed);
    if (head_.compare_exchange_weak(node, next, std::memory_order_acq_rel, std::memory_order_relaxed)) {
      value = node->value;
      delete node;
      return true;
    }
  }
  return false;
}

template <typename T>
bool LockFreeStack<T>::Empty() const {
  return head_.load(std::memory_order_acquire) == nullptr;
}

template <typename F>
std::shared_ptr<Coroutine> CoroutineScheduler::Create(F &&func) {
  struct FuncCoroutine : Coroutine {
    using Fn = std::function<void()>;

    explicit FuncCoroutine(Fn fn) : fn_(std::move(fn)) {}

    void Resume() override {
      if (state_ == kCompleted) return;
      state_ = kRunning;
      fn_();
      state_ = kCompleted;
    }

    void Yield() override { /* no-op in simplified coroutine */ }

    State GetState() const override { return state_; }

    Fn fn_;
    State state_{kSuspended};
  };

  using Fn = typename FuncCoroutine::Fn;
  return std::make_shared<FuncCoroutine>(Fn(std::forward<F>(func)));
}

template <typename T>
ThreadLocal<T>::ThreadLocal(T value) : initial_(value) {}

template <typename T>
T &ThreadLocal<T>::Get() {
  std::lock_guard<std::mutex> lock(mutex_);
  auto id = std::this_thread::get_id();
  auto it = storage_.find(id);
  if (it == storage_.end()) {
    it = storage_.emplace(id, initial_).first;
  }
  return it->second;
}

template <typename T>
void ThreadLocal<T>::Set(const T &value) {
  std::lock_guard<std::mutex> lock(mutex_);
  storage_[std::this_thread::get_id()] = value;
}

template <typename T>
Future<T>::Future() : promise_(std::make_shared<std::promise<T>>()), future_(promise_->get_future()) {}

template <typename T>
Future<T>::Future(std::shared_ptr<std::promise<T>> promise)
    : promise_(std::move(promise)), future_(promise_->get_future()) {}

template <typename T>
T Future<T>::Get() {
  return future_.get();
}

template <typename T>
bool Future<T>::IsReady() const {
  return future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

template <typename T>
template <typename F>
void Future<T>::Then(F &&callback) {
  std::thread([cb = std::forward<F>(callback), fut = future_]() mutable { cb(fut.get()); }).detach();
}

template <typename T>
Promise<T>::Promise() : promise_(std::make_shared<std::promise<T>>()) {}

template <typename T>
void Promise<T>::SetValue(const T &value) {
  promise_->set_value(value);
}

template <typename T>
void Promise<T>::SetException(std::exception_ptr ex) {
  promise_->set_exception(ex);
}

template <typename T>
Future<T> Promise<T>::GetFuture() {
  return Future<T>(promise_);
}

template <typename T>
T Atomic<T>::Load(MemoryOrder order) const {
  return value_.load(ToStdOrder(order));
}

template <typename T>
void Atomic<T>::Store(T value, MemoryOrder order) {
  value_.store(value, ToStdOrder(order));
}

template <typename T>
T Atomic<T>::Exchange(T value, MemoryOrder order) {
  return value_.exchange(value, ToStdOrder(order));
}

template <typename T>
bool Atomic<T>::CompareExchange(T &expected, T desired, MemoryOrder success, MemoryOrder failure) {
  return value_.compare_exchange_strong(expected, desired, ToStdOrder(success), ToStdOrder(failure));
}

template <typename T>
T Atomic<T>::FetchAdd(T arg, MemoryOrder order) {
  return value_.fetch_add(arg, ToStdOrder(order));
}

template <typename T>
T Atomic<T>::FetchSub(T arg, MemoryOrder order) {
  return value_.fetch_sub(arg, ToStdOrder(order));
}

}  // namespace polyglot::runtime::services
