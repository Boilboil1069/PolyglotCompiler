#pragma once

#include <string>

namespace polyglot::runtime::interop {

struct ForeignFunction {
  std::string name;
  void *address{nullptr};
};

}  // namespace polyglot::runtime::interop
