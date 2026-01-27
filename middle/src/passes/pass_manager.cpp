#include <functional>
#include <vector>

#include "middle/include/ir/ir_context.h"
#include "middle/include/ir/passes/opt.h"
#include "middle/include/passes/transform/constant_fold.h"
#include "middle/include/passes/transform/dead_code_elim.h"
#include "middle/include/passes/transform/common_subexpr.h"
#include "middle/include/passes/transform/inlining.h"

namespace polyglot::passes {

class PassManager {
 public:
  enum class OptLevel { kO0, kO1, kO2 };

  using Pass = std::function<void(ir::IRContext &)>;

  void AddPass(Pass pass) { passes_.push_back(std::move(pass)); }
  void SetOptLevel(OptLevel lvl) { level_ = lvl; }

  void Run(ir::IRContext &context) {
    if (!passes_.empty()) {
      for (auto &pass : passes_) pass(context);
      return;
    }

    switch (level_) {
      case OptLevel::kO0:
        // no optimizations
        break;
      case OptLevel::kO1:
        for (auto &fn : context.Functions()) {
          polyglot::ir::passes::ConstantFold(*fn);
          polyglot::ir::passes::CopyProp(*fn);
          polyglot::ir::passes::DeadCodeEliminate(*fn);
          polyglot::ir::passes::CanonicalizeCFG(*fn);
        }
        break;
      case OptLevel::kO2:
      default:
        for (auto &fn : context.Functions()) {
          polyglot::ir::passes::RunDefaultOptimizations(*fn);
        }
        break;
    }
  }

 private:
  std::vector<Pass> passes_{};
  OptLevel level_{OptLevel::kO2};
};

}  // namespace polyglot::passes
