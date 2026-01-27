#include <iostream>
#include <string>

namespace polyglot::tools {

std::string RuntimeStatus() {
  return "runtime ok";
}

}  // namespace polyglot::tools

int main() {
  std::cout << polyglot::tools::RuntimeStatus() << "\n";
  return 0;
}
