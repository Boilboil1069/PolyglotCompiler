/**
 * @file     loop_optimization.h
 * @brief    Transformation passes
 *
 * @ingroup  Middle / Transform
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include "middle/include/ir/cfg.h"
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <memory>

namespace polyglot::ir {
    struct BasicBlock;
    struct Function;
    struct Instruction;
}

namespace polyglot::passes::transform {

/**
 * Loop information structure
 * Contains all relevant information about a natural loop
 */
struct LoopInfo {
    ir::BasicBlock* header{nullptr};                 // Loop header block
    std::unordered_set<ir::BasicBlock*> body;        // All blocks in the loop
    std::vector<ir::BasicBlock*> exits;              // Exit blocks (have successors outside loop)
    std::vector<ir::BasicBlock*> latches;            // Latch blocks (have back edges to header)
    ir::BasicBlock* preheader{nullptr};              // Preheader block (single external pred)
    LoopInfo* parent{nullptr};                       // Parent loop (if nested)
    int depth{0};                                    // Nesting depth
    std::vector<LoopInfo*> nested_loops;             // Nested loops
};

/**
 * Loop analysis - detect natural loops
 * Uses dominator tree and back edge detection
 */
class LoopAnalysis {
public:
    explicit LoopAnalysis(ir::Function& func);
    
    const std::vector<std::unique_ptr<LoopInfo>>& GetLoops() const { return loops_; }
    LoopInfo* GetLoopForBlock(ir::BasicBlock* bb) const;
    bool IsInLoop(ir::BasicBlock* bb) const;
    
private:
    void AnalyzeLoops();
    void FindBackEdges();
    void ConstructLoop(ir::BasicBlock* header, ir::BasicBlock* latch);
    void ComputeNesting();
    
    ir::Function& func_;
    std::vector<std::unique_ptr<LoopInfo>> loops_;
    std::unordered_map<ir::BasicBlock*, LoopInfo*> block_to_loop_;
    std::vector<std::pair<ir::BasicBlock*, ir::BasicBlock*>> back_edges_;
};

/**
 * Loop Unrolling Pass
 * Replicates loop body to reduce loop overhead and enable ILP
 */
class LoopUnrollingPass {
public:
    explicit LoopUnrollingPass(ir::Function& func, int unroll_factor = 4);
    
    bool Run();
    
private:
    bool CanUnroll(const LoopInfo* loop) const;
    void UnrollLoop(LoopInfo* loop);
    void UpdateLoopBound(LoopInfo* loop);
    
    ir::Function& func_;
    int unroll_factor_;
};

/**
 * Loop-Invariant Code Motion (LICM) Pass
 * Hoists loop-invariant computations out of loops
 */
class LICMPass {
public:
    explicit LICMPass(ir::Function& func);
    
    bool Run();
    
private:
    bool ProcessLoop(LoopInfo* loop);
    bool IsLoopInvariant(ir::Instruction* inst, const LoopInfo* loop) const;
    bool IsSafeToHoist(ir::Instruction* inst, const LoopInfo* loop) const;
    void HoistInstruction(ir::Instruction* inst, LoopInfo* loop);
    void CreatePreheader(LoopInfo* loop);
    
    ir::Function& func_;
};

/**
 * Loop Fusion Pass
 * Merges adjacent loops with the same iteration space
 */
class LoopFusionPass {
public:
    explicit LoopFusionPass(ir::Function& func);
    
    bool Run();
    
private:
    bool CanFuseLoops(const LoopInfo* loop1, const LoopInfo* loop2) const;
    void FuseLoops(LoopInfo* loop1, LoopInfo* loop2);
    
    ir::Function& func_;
};

/**
 * Loop Strength Reduction Pass
 * Replaces expensive operations with cheaper ones based on induction variables
 */
class LoopStrengthReductionPass {
public:
    explicit LoopStrengthReductionPass(ir::Function& func);
    
    bool Run();
    
private:
    /** @brief InductionVarInfo data structure. */
    struct InductionVarInfo {
        std::string base;
        int64_t step{0};
    };
    
    bool ProcessLoop(LoopInfo* loop);
    bool IsInductionVariable(const std::string& var, const LoopInfo* loop) const;
    
    ir::Function& func_;
};

}  // namespace polyglot::passes::transform
