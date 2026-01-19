#pragma once

#include <string>

namespace polyglot::backends {

struct ABI {
  std::string name;
  size_t pointer_size{8};
};

}  // namespace polyglot::backends
