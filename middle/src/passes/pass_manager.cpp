#include <functional>
#include <vector>

#include "middle/include/ir/ir_context.h"

namespace polyglot::passes {

class PassManager {
 public:
  using Pass = std::function<void(ir::IRContext &)>;

  void AddPass(Pass pass) { passes_.push_back(std::move(pass)); }

  void Run(ir::IRContext &context) {
    for (auto &pass : passes_) {
      pass(context);
    }
  }

 private:
  std::vector<Pass> passes_{};
};

}  // namespace polyglot::passes
