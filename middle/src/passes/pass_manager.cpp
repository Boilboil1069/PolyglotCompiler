#include <functional>
#include <vector>

#include "middle/include/ir/ir_context.h"
#include "middle/include/ir/passes/opt.h"

namespace polyglot::passes {

class PassManager {
 public:
  using Pass = std::function<void(ir::IRContext &)>;

  void AddPass(Pass pass) { passes_.push_back(std::move(pass)); }

  void Run(ir::IRContext &context) {
    for (auto &pass : passes_) {
      pass(context);
    }
    // default optimization pipeline for all functions if no custom passes added
    if (passes_.empty()) {
      for (auto &fn : context.Functions()) {
        polyglot::ir::passes::RunDefaultOptimizations(*fn);
      }
    }
  }

 private:
  std::vector<Pass> passes_{};
};

}  // namespace polyglot::passes
