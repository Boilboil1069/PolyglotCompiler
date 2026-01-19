#pragma once

#include <string>
#include <unordered_map>

#include "frontends/common/include/diagnostics.h"

namespace polyglot::frontends {

class Preprocessor {
 public:
  explicit Preprocessor(Diagnostics &diagnostics) : diagnostics_(diagnostics) {}

  void Define(const std::string &name, const std::string &value) {
    macros_[name] = value;
  }

  void Undefine(const std::string &name) { macros_.erase(name); }

  std::string Expand(const std::string &source);
  std::string Process(const std::string &source);

 private:
  Diagnostics &diagnostics_;
  std::unordered_map<std::string, std::string> macros_{};
};

}  // namespace polyglot::frontends
