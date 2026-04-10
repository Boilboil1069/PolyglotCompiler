/**
 * @file     threading.h
 * @brief    Runtime service infrastructure
 *
 * @ingroup  Runtime / Services
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
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
//
// Each ThreadLocalStorage instance maintains an independent key-value store.
// Thread safety is guaranteed by an internal mutex so the same instance can be
// used from multiple threads without external synchronization.
/** @brief ThreadLocalStorage class. */
class ThreadLocalStorage {
 public:
  void Set(const std::string &key, void *value) {
    std::lock_guard<std::mutex> lock(mu_);
    storage_[key] = value;
  }
  void *Get(const std::string &key) const {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = storage_.find(key);
    return it == storage_.end() ? nullptr : it->second;
  }
  void Erase(const std::string &key) {
    std::lock_guard<std::mutex> lock(mu_);
    storage_.erase(key);
  }

 private:
  mutable std::mutex mu_;
  std::unordered_map<std::string, void *> storage_;
};

/** @brief Threading class. */
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
/** @brief ThreadPool class. */
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
/** @brief TaskScheduler class. */
class TaskScheduler {
 public:
  /** @brief Priority enumeration. */
  enum Priority { kLow, kNormal, kHigh, kRealtime };

  /** @brief Task data structure. */
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
/** @brief WorkStealingScheduler class. */
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
/** @brief RWLock class. */
class RWLock {
 public:
  void ReadLock();
  void ReadUnlock();
  void WriteLock();
  void WriteUnlock();

  /** @brief ReadGuard class. */
  class ReadGuard {
   public:
    explicit ReadGuard(RWLock &lock) : lock_(lock) { lock_.ReadLock(); }
    ~ReadGuard() { lock_.ReadUnlock(); }

   private:
    RWLock &lock_;
  };

  /** @brief WriteGuard class. */
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

/** @brief Barrier class. */
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

/** @brief Semaphore class. */
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
/** @brief LockFreeQueue class. */
class LockFreeQueue {
 public:
  LockFreeQueue();
  ~LockFreeQueue();

  void Push(const T &value);
  bool Pop(T &value);
  bool Empty() const;

 private:
  /** @brief Node data structure. */
  struct Node {
    T value;
    std::atomic<Node*> next;
    Node(const T& val) : value(val), next(nullptr) {}
  };
  std::atomic<Node*> head_;
  std::atomic<Node*> tail_;
};

template <typename T>
/** @brief LockFreeStack class. */
class LockFreeStack {
 public:
  LockFreeStack();
  ~LockFreeStack();

  void Push(const T &value);
  bool Pop(T &value);
  bool Empty() const;

 private:
  /** @brief Node data structure. */
  struct Node {
    T value;
    std::atomic<Node*> next;
    Node(const T& val) : value(val), next(nullptr) {}
  };
  std::atomic<Node*> head_;
};

// 6. Thread-local storage
template <typename T>
/** @brief ThreadLocal class. */
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
/** @brief Coroutine class. */
class Coroutine {
 public:
  /** @brief State enumeration. */
  enum State { kSuspended, kRunning, kCompleted };

  virtual ~Coroutine() = default;
  virtual void Resume() = 0;
  virtual void Yield() = 0;
  virtual State GetState() const = 0;
  bool IsCompleted() const { return GetState() == kCompleted; }
};

/** @brief CoroutineScheduler class. */
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
//
// Implementation uses a shared state so that callbacks registered with Then()
// are invoked synchronously from SetValue() / SetException(), removing the
// need for detached threads.

template <typename T>
class Promise;

template <typename T>
/** @brief Future class. */
class Future {
 public:
  Future();

  T Get();
  bool IsReady() const;

  template <typename F>
  void Then(F&& callback);

 private:
  friend class Promise<T>;

  // Shared state between Promise and Future.
  /** @brief State data structure. */
  struct State {
    std::mutex mu;
    std::condition_variable cv;
    bool ready{false};
    T value{};
    std::exception_ptr exception{};
    std::function<void(T)> callback;
  };

  explicit Future(std::shared_ptr<State> state) : state_(std::move(state)) {}
  std::shared_ptr<State> state_;
};

template <typename T>
/** @brief Promise class. */
class Promise {
 public:
  Promise();

  void SetValue(const T &value);
  void SetException(std::exception_ptr ex);
  Future<T> GetFuture();

 private:
  std::shared_ptr<typename Future<T>::State> state_;
};

// 9. Memory order control
/** @brief MemoryOrder enumeration. */
enum class MemoryOrder {
  kRelaxed,
  kAcquire,
  kRelease,
  kAcqRel,
  kSeqCst
};

template <typename T>
/** @brief Atomic class. */
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
/** @brief ThreadStats data structure. */
struct ThreadStats {
  size_t num_tasks_executed{0};
  size_t num_steals{0};
  size_t total_exec_time_us{0};
  size_t idle_time_us{0};
};

/** @brief ThreadProfiler class. */
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
  /** @brief FuncCoroutine data structure. */
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
Future<T>::Future() : state_(std::make_shared<State>()) {}

template <typename T>
T Future<T>::Get() {
  std::unique_lock<std::mutex> lock(state_->mu);
  state_->cv.wait(lock, [&] { return state_->ready; });
  if (state_->exception) std::rethrow_exception(state_->exception);
  return state_->value;
}

template <typename T>
bool Future<T>::IsReady() const {
  std::lock_guard<std::mutex> lock(state_->mu);
  return state_->ready;
}

template <typename T>
template <typename F>
void Future<T>::Then(F &&callback) {
  std::lock_guard<std::mutex> lock(state_->mu);
  if (state_->ready && !state_->exception) {
    // Value already available — invoke synchronously.
    callback(state_->value);
  } else {
    // Store the callback; it will be invoked from SetValue().
    state_->callback = std::forward<F>(callback);
  }
}

template <typename T>
Promise<T>::Promise() : state_(std::make_shared<typename Future<T>::State>()) {}

template <typename T>
void Promise<T>::SetValue(const T &value) {
  std::function<void(T)> cb;
  {
    std::lock_guard<std::mutex> lock(state_->mu);
    state_->value = value;
    state_->ready = true;
    cb = std::move(state_->callback);
  }
  state_->cv.notify_all();
  // Invoke the callback outside the lock to avoid potential deadlocks.
  if (cb) cb(value);
}

template <typename T>
void Promise<T>::SetException(std::exception_ptr ex) {
  {
    std::lock_guard<std::mutex> lock(state_->mu);
    state_->exception = std::move(ex);
    state_->ready = true;
  }
  state_->cv.notify_all();
}

template <typename T>
Future<T> Promise<T>::GetFuture() {
  return Future<T>(state_);
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
