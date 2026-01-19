#pragma once

#include <string>
#include <unordered_map>

namespace polyglot::core {

class Config {
 public:
  void SetOption(const std::string &key, const std::string &value) {
    options_[key] = value;
  }

  std::string GetOption(const std::string &key,
                        const std::string &default_value = "") const {
    auto it = options_.find(key);
    if (it == options_.end()) {
      return default_value;
    }
    return it->second;
  }

 private:
  std::unordered_map<std::string, std::string> options_{};
};

}  // namespace polyglot::core
