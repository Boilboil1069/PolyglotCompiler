#include "runtime/include/interop/calling_convention.h"

namespace polyglot::runtime::interop {

bool ValidateSignature(const ForeignSignature &sig) {
  if (sig.result.size == 0 && !sig.result.is_pointer && sig.result.name != "void") return false;
  for (const auto &arg : sig.args) {
    if (arg.size == 0 || arg.alignment == 0) return false;
  }
  return true;
}

}  // namespace polyglot::runtime::interop
