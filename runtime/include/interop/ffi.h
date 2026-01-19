#pragma once

#include <string>

namespace polyglot::runtime::interop {

struct ForeignFunction {
  std::string name;
  void *address{nullptr};
};

ForeignFunction Bind(const std::string &name, void *address);

}  // namespace polyglot::runtime::interop
