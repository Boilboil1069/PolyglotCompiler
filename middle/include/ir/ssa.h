#pragma once

#include <string>
#include <unordered_map>

namespace polyglot::ir {

struct SSAVariable {
  std::string name;
  size_t version{0};
};

class SSABuilder {
 public:
  SSAVariable NextVersion(const std::string &name) {
    size_t &version = versions_[name];
    version += 1;
    return SSAVariable{name, version};
  }

 private:
  std::unordered_map<std::string, size_t> versions_{};
};

}  // namespace polyglot::ir
