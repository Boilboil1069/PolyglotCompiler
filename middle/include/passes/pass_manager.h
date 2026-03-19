#pragma once

#include <functional>
#include <string>
#include <vector>

#include "middle/include/ir/ir_context.h"

namespace polyglot::passes {

// ============================================================================
// Function-level pass: operates on a single IR function
// ============================================================================
using FunctionPass = std::function<void(ir::Function &)>;

// Named pass entry for diagnostics and logging
struct PassEntry {
    std::string name;
    FunctionPass pass;
};

// ============================================================================
// PassManager — configurable optimization pipeline
//
// Builds a sequence of function-level passes based on the optimization level.
// The driver calls Build() once, then RunOnModule() to apply all passes to
// every function in the IR module.
// ============================================================================
class PassManager {
  public:
    enum class OptLevel { kO0 = 0, kO1 = 1, kO2 = 2, kO3 = 3 };

    PassManager() = default;
    explicit PassManager(OptLevel level) : level_(level) {}

    void SetOptLevel(OptLevel level) { level_ = level; }
    OptLevel GetOptLevel() const { return level_; }

    // Add a custom pass (appended after built-in passes)
    void AddPass(const std::string &name, FunctionPass pass) {
        custom_passes_.push_back({name, std::move(pass)});
    }

    // Build the pass pipeline based on the current opt level.
    // Must be called before RunOnModule().
    void Build();

    // Run all passes on every function in the module.
    // Returns the number of passes executed per function.
    size_t RunOnModule(ir::IRContext &module, bool verbose = false);

    // Access the built pipeline (for inspection/testing)
    const std::vector<PassEntry> &Pipeline() const { return pipeline_; }

  private:
    void BuildO1();
    void BuildO2();
    void BuildO3();

    OptLevel level_{OptLevel::kO0};
    std::vector<PassEntry> custom_passes_;
    std::vector<PassEntry> pipeline_;
};

}  // namespace polyglot::passes
