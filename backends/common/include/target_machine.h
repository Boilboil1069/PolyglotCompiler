#pragma once

#include <string>

namespace polyglot::backends {

class TargetMachine {
 public:
  virtual ~TargetMachine() = default;
  virtual std::string TargetTriple() const = 0;
  virtual std::string EmitAssembly() = 0;
};

}  // namespace polyglot::backends
