#include "middle/include/passes/pass_manager.h"

#include <iostream>

#include "middle/include/ir/passes/opt.h"
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
    pipeline_.push_back({"ConstantFold", [](ir::Function &fn) {
        ir::passes::ConstantFold(fn);
    }});
    pipeline_.push_back({"CopyProp", [](ir::Function &fn) {
        ir::passes::CopyProp(fn);
    }});
    pipeline_.push_back({"DeadCodeEliminate", [](ir::Function &fn) {
        ir::passes::DeadCodeEliminate(fn);
    }});
    pipeline_.push_back({"CanonicalizeCFG", [](ir::Function &fn) {
        ir::passes::CanonicalizeCFG(fn);
    }});
    pipeline_.push_back({"EliminateRedundantPhis", [](ir::Function &fn) {
        ir::passes::EliminateRedundantPhis(fn);
    }});
    pipeline_.push_back({"CSE", [](ir::Function &fn) {
        ir::passes::CSE(fn);
    }});
}

void PassManager::BuildO2() {
    pipeline_.push_back({"StrengthReduction", [](ir::Function &fn) {
        transform::StrengthReduction(fn);
    }});
    pipeline_.push_back({"LoopInvariantCodeMotion", [](ir::Function &fn) {
        transform::LoopInvariantCodeMotion(fn);
    }});
    pipeline_.push_back({"LoopUnrolling", [](ir::Function &fn) {
        transform::LoopUnrolling(fn, 4);
    }});
    pipeline_.push_back({"DeadStoreElimination", [](ir::Function &fn) {
        transform::DeadStoreElimination(fn);
    }});
    pipeline_.push_back({"InductionVariableElimination", [](ir::Function &fn) {
        transform::InductionVariableElimination(fn);
    }});
    pipeline_.push_back({"SCCP", [](ir::Function &fn) {
        transform::SCCP(fn);
    }});
    pipeline_.push_back({"GVN", [](ir::Function &fn) {
        transform::GVN(fn);
    }});
    pipeline_.push_back({"JumpThreading", [](ir::Function &fn) {
        transform::JumpThreading(fn);
    }});
    // Cleanup after loop opts
    pipeline_.push_back({"DeadCodeEliminate (post-O2)", [](ir::Function &fn) {
        ir::passes::DeadCodeEliminate(fn);
    }});
    pipeline_.push_back({"CanonicalizeCFG (post-O2)", [](ir::Function &fn) {
        ir::passes::CanonicalizeCFG(fn);
    }});
}

void PassManager::BuildO3() {
    pipeline_.push_back({"TailCallOptimization", [](ir::Function &fn) {
        transform::TailCallOptimization(fn);
    }});
    pipeline_.push_back({"EscapeAnalysis", [](ir::Function &fn) {
        transform::EscapeAnalysis(fn);
    }});
    pipeline_.push_back({"ScalarReplacement", [](ir::Function &fn) {
        transform::ScalarReplacement(fn);
    }});
    pipeline_.push_back({"AutoVectorization", [](ir::Function &fn) {
        transform::AutoVectorization(fn);
    }});
    pipeline_.push_back({"LoopFusion", [](ir::Function &fn) {
        transform::LoopFusion(fn);
    }});
    pipeline_.push_back({"CodeSinking", [](ir::Function &fn) {
        transform::CodeSinking(fn);
    }});
    pipeline_.push_back({"CodeHoisting", [](ir::Function &fn) {
        transform::CodeHoisting(fn);
    }});
    pipeline_.push_back({"LoopTiling", [](ir::Function &fn) {
        transform::LoopTiling(fn, 64);
    }});
    // Final cleanup
    pipeline_.push_back({"DeadCodeEliminate (post-O3)", [](ir::Function &fn) {
        ir::passes::DeadCodeEliminate(fn);
    }});
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

}  // namespace polyglot::passes
