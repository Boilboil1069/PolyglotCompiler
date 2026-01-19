#include <string>

namespace polyglot::backends::x86_64 {

std::string SelectInstructions() {
  return "mov rax, rax";
}

}  // namespace polyglot::backends::x86_64
