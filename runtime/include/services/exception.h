#pragma once

#include <stdexcept>
#include <string>

namespace polyglot::runtime::services {

class RuntimeError : public std::runtime_error {
 public:
  explicit RuntimeError(const std::string &message) : std::runtime_error(message) {}
};

}  // namespace polyglot::runtime::services
