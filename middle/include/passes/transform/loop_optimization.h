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

// Loop information structure
struct LoopInfo {
    ir::BasicBlock* header;                          // Loop header block
    std::unordered_set<ir::BasicBlock*> body;        // All blocks in the loop
    std::unordered_set<ir::BasicBlock*> exits;       // Exit blocks
    ir::BasicBlock* preheader;                       // Preheader block (for LICM)
    int depth;                                       // Nesting depth
    std::vector<LoopInfo*> nested_loops;             // Nested loops
};

// Loop analysis - detect natural loops
class LoopAnalysis {
public:
    explicit LoopAnalysis(ir::Function& func);
    
    const std::vector<std::unique_ptr<LoopInfo>>& GetLoops() const { return loops_; }
    LoopInfo* GetLoopForBlock(ir::BasicBlock* bb) const;
    
private:
    void AnalyzeLoops();
    void FindBackEdges();
    void ConstructLoop(ir::BasicBlock* header, ir::BasicBlock* latch);
    void ComputeNesting();
    
    ir::Function& func_;
    std::vector<std::unique_ptr<LoopInfo>> loops_;
    std::unordered_map<ir::BasicBlock*, LoopInfo*> block_to_loop_;
};

// Loop Unrolling Pass
class LoopUnrollingPass {
public:
    explicit LoopUnrollingPass(ir::Function& func, int unroll_factor = 4);
    
    bool Run();
    
private:
    bool UnrollLoop(LoopInfo* loop);
    bool IsUnrollable(const LoopInfo* loop) const;
    int EstimateIterationCount(const LoopInfo* loop) const;
    std::unique_ptr<ir::Instruction> CloneInstruction(const ir::Instruction* inst, 
                                                      std::map<std::string, std::string>& value_map, 
                                                      int iteration);
    
    ir::Function& func_;
    int unroll_factor_;
};

// Loop-Invariant Code Motion (LICM) Pass
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

// Loop Fusion Pass
class LoopFusionPass {
public:
    explicit LoopFusionPass(ir::Function& func);
    
    bool Run();
    
private:
    bool CanFuseLoops(const LoopInfo* loop1, const LoopInfo* loop2) const;
    void FuseLoops(LoopInfo* loop1, LoopInfo* loop2);
    
    ir::Function& func_;
};

// Loop Strength Reduction Pass
class LoopStrengthReductionPass {
public:
    explicit LoopStrengthReductionPass(ir::Function& func);
    
    bool Run();
    
private:
    struct InductionVarInfo {
        std::string base;
        int step;
    };
    
    bool ProcessLoop(LoopInfo* loop);
    bool IsInductionVariable(const std::string& var, const LoopInfo* loop) const;
    
    ir::Function& func_;
};

}  // namespace polyglot::passes::transform
