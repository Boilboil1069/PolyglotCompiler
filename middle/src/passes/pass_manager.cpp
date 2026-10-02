/**
 * @file     pass_manager.cpp
 * @brief    Middle-end implementation
 *
 * @ingroup  Middle
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include <iostream>

#include "middle/include/ir/passes/opt.h"
#include "middle/include/passes/pass_manager.h"
#include "middle/include/passes/transform/advanced_optimizations.h"

namespace polyglot::passes {

// ============================================================================
// Build pass pipeline based on optimization level
// ============================================================================

void PassManager::Build() {
  pipeline_.clear();

  if (level_ == OptLevel::kO0) {
    // Append only custom passes
    for (auto &p : custom_passes_) {
      pipeline_.push_back(p);
    }
    return;
  }

  // O1 and above
  BuildO1();

  if (static_cast<int>(level_) >= 2) {
    BuildO2();
  }

  if (static_cast<int>(level_) >= 3) {
    BuildO3();
  }

  // Append custom passes at the end
  for (auto &p : custom_passes_) {
    pipeline_.push_back(p);
  }
}

void PassManager::BuildO1() {
  pipeline_.push_back({"ConstantFold", [](ir::Function &fn) { ir::passes::ConstantFold(fn); }});
  pipeline_.push_back({"CopyProp", [](ir::Function &fn) { ir::passes::CopyProp(fn); }});
  pipeline_.push_back(
      {"DeadCodeEliminate", [](ir::Function &fn) { ir::passes::DeadCodeEliminate(fn); }});
  pipeline_.push_back(
      {"CanonicalizeCFG", [](ir::Function &fn) { ir::passes::CanonicalizeCFG(fn); }});
  pipeline_.push_back(
      {"EliminateRedundantPhis", [](ir::Function &fn) { ir::passes::EliminateRedundantPhis(fn); }});
  pipeline_.push_back({"CSE", [](ir::Function &fn) { ir::passes::CSE(fn); }});
}

// Only transformations with complete CFG/SSA and alias semantics belong in
// the compiler's default pipeline. The experimental loop/vector transforms
// remain available as explicit APIs; they are not safe production defaults.
void PassManager::BuildO2() {
  pipeline_.push_back({"StrengthReduction", [](ir::Function &fn) { transform::StrengthReduction(fn); }});
  pipeline_.push_back({"CSE (post-O2)", [](ir::Function &fn) { ir::passes::CSE(fn); }});
  pipeline_.push_back({"DeadCodeEliminate (post-O2)", [](ir::Function &fn) { ir::passes::DeadCodeEliminate(fn); }});
}

void PassManager::BuildO3() {
  pipeline_.push_back({"EscapeAnalysis", [](ir::Function &fn) { transform::EscapeAnalysis(fn); }});
  pipeline_.push_back({"ConstantFold (post-O3)", [](ir::Function &fn) { ir::passes::ConstantFold(fn); }});
  pipeline_.push_back({"CopyProp (post-O3)", [](ir::Function &fn) { ir::passes::CopyProp(fn); }});
  pipeline_.push_back({"DeadCodeEliminate (post-O3)", [](ir::Function &fn) { ir::passes::DeadCodeEliminate(fn); }});
}

// ============================================================================
// Run pipeline on all functions in the module
// ============================================================================

size_t PassManager::RunOnModule(ir::IRContext &module, bool verbose) {
  if (pipeline_.empty()) {
    Build();
  }

  size_t pass_count = pipeline_.size();

  for (auto &fn : module.Functions()) {
    for (const auto &entry : pipeline_) {
      if (verbose) {
        std::cerr << "[opt]   " << entry.name << " on " << fn->name << "\n";
      }
      entry.pass(*fn);
    }
  }

  return pass_count;
}

} // namespace polyglot::passes
