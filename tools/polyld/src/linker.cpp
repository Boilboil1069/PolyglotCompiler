#include <iostream>
#include <string>
#include <vector>

namespace polyglot::tools {

struct ObjectFile {
  std::string name;
};

class Linker {
 public:
  void AddObject(const ObjectFile &obj) { objects_.push_back(obj); }
  std::string Link() const { return "linked.out"; }

 private:
  std::vector<ObjectFile> objects_{};
};

}  // namespace polyglot::tools

int main() {
  polyglot::tools::Linker ld;
  ld.AddObject({"dummy.o"});
  std::cout << ld.Link() << "\n";
  return 0;
}
