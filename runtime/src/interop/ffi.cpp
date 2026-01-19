#include "runtime/include/interop/ffi.h"

namespace polyglot::runtime::interop {

ForeignFunction Bind(const std::string &name, void *address) {
  return ForeignFunction{name, address};
}

}  // namespace polyglot::runtime::interop
