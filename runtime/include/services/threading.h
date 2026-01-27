#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

namespace polyglot::runtime::services {

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

}  // namespace polyglot::runtime::services
