/**
 * @file     string_pool.h
 * @brief    Shared utility classes
 *
 * @ingroup  Common / Utils
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <string>
#include <unordered_set>

namespace polyglot::utils {

/** @brief StringPool class. */
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
