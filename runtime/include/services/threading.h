#pragma once

#include <thread>

namespace polyglot::runtime::services {

class Threading {
 public:
  template <typename Fn>
  void RunInThread(Fn &&fn) {
    std::thread(std::forward<Fn>(fn)).detach();
  }
};

}  // namespace polyglot::runtime::services
