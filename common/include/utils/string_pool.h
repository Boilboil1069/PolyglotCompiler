#pragma once

#include <string>
#include <unordered_set>

namespace polyglot::utils {

class StringPool {
 public:
  const std::string &Intern(const std::string &value) {
    auto [it, inserted] = pool_.insert(value);
    return *it;
  }

  size_t Size() const { return pool_.size(); }

 private:
  std::unordered_set<std::string> pool_{};
};

}  // namespace polyglot::utils
