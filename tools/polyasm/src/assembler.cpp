#include <iostream>
#include <string>

namespace polyglot::tools {

std::string Assemble(const std::string &source) {
  (void)source;
  return "object.o";
}

}  // namespace polyglot::tools

int main() {
  std::cout << polyglot::tools::Assemble("/* stub asm */") << "\n";
  return 0;
}
