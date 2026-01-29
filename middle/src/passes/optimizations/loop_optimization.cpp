#include "middle/include/passes/transform/loop_optimization.h"
#include "middle/include/ir/analysis.h"
#include "middle/include/ir/nodes/statements.h"
#include <algorithm>
#include <queue>

namespace polyglot::passes::transform {

using namespace polyglot::ir;

// ============================================================================
// Loop Analysis Implementation
// ============================================================================

LoopAnalysis::LoopAnalysis(ir::Function& func) : func_(func) {
    AnalyzeLoops();
}

void LoopAnalysis::AnalyzeLoops() {
    // Use AnalysisCache to get loop information
    AnalysisCache cache(func_);
    const auto& ir_loops = cache.GetLoops();
    
    // Convert IR loops to our LoopInfo structure
    for (const auto& ir_loop : ir_loops) {
        auto loop = std::make_unique<LoopInfo>();
        loop->header = ir_loop.header;
        for (auto* bb : ir_loop.blocks) {
            loop->body.insert(bb);
        }
        loop->depth = 1;  // TODO: Calculate proper depth
        loop->preheader = nullptr;
        
        loops_.push_back(std::move(loop));
    }
}

void LoopAnalysis::FindBackEdges() {
    // Implementation delegated to AnalysisCache
}

void LoopAnalysis::ConstructLoop(ir::BasicBlock* header, ir::BasicBlock* latch) {
    // Implementation delegated to AnalysisCache
}

void LoopAnalysis::ComputeNesting() {
    // Compute loop depth and nesting relationships
    for (auto& loop : loops_) {
        loop->depth = 1;
        for (auto& other_loop : loops_) {
            if (loop.get() != other_loop.get()) {
                // Check if loop is nested in other_loop
                bool is_nested = true;
                for (auto* bb : loop->body) {
                    if (other_loop->body.find(bb) == other_loop->body.end()) {
                        is_nested = false;
                        break;
                    }
                }
                if (is_nested) {
                    loop->depth++;
                    other_loop->nested_loops.push_back(loop.get());
                }
            }
        }
    }
}

LoopInfo* LoopAnalysis::GetLoopForBlock(ir::BasicBlock* bb) const {
    auto it = block_to_loop_.find(bb);
    return it != block_to_loop_.end() ? it->second : nullptr;
}

// ============================================================================
// Loop Unrolling Pass Implementation
// ============================================================================

LoopUnrollingPass::LoopUnrollingPass(ir::Function& func, int unroll_factor)
    : func_(func), unroll_factor_(unroll_factor) {}

bool LoopUnrollingPass::Run() {
    LoopAnalysis analysis(func_);
    bool changed = false;
    
    for (const auto& loop_ptr : analysis.GetLoops()) {
        if (IsUnrollable(loop_ptr.get())) {
            if (UnrollLoop(loop_ptr.get())) {
                changed = true;
            }
        }
    }
    
    return changed;
}

bool LoopUnrollingPass::IsUnrollable(const LoopInfo* loop) const {
    // Check if loop is small enough to unroll
    if (loop->body.size() > 10) return false;  // Too large
    
    // Check if loop has known iteration count
    int iter_count = EstimateIterationCount(loop);
    if (iter_count < 0 || iter_count > 100) return false;
    
    // Check if loop has simple structure
    if (loop->exits.size() != 1) return false;  // Multiple exits
    
    return true;
}

int LoopUnrollingPass::EstimateIterationCount(const LoopInfo* loop) const {
    // Simple estimation - look for constant bounds
    // In a real implementation, this would use more sophisticated analysis
    return -1;  // Unknown iteration count
}

bool LoopUnrollingPass::UnrollLoop(LoopInfo* loop) {
    if (!loop->preheader || unroll_factor_ <= 1) return false;
    
    // Clone loop body
    std::vector<std::unique_ptr<BasicBlock>> cloned_blocks;
    std::map<std::string, std::string> value_map;  // old -> new name mapping
    
    for (int i = 1; i < unroll_factor_; ++i) {
        for (auto* bb : loop->body) {
            if (bb == loop->header) continue;  // Handle header specially
            
            auto cloned_bb = std::make_unique<BasicBlock>();
            cloned_bb->name = bb->name + "_unroll_" + std::to_string(i);
            
            // Clone instructions
            for (const auto& inst_ptr : bb->instructions) {
                auto cloned_inst = CloneInstruction(inst_ptr.get(), value_map, i);
                cloned_bb->instructions.push_back(std::move(cloned_inst));
            }
            
            // Clone terminator
            if (bb->terminator) {
                cloned_bb->terminator = CloneInstruction(bb->terminator.get(), value_map, i);
            }
            
            cloned_blocks.push_back(std::move(cloned_bb));
        }
    }
    
    // Update header's phi nodes to handle multiple iterations
    if (loop->header) {
        for (auto& phi_ptr : loop->header->phis) {
            auto* phi = phi_ptr.get();
            // Add incoming values from unrolled iterations
            for (int i = 1; i < unroll_factor_; ++i) {
                for (const auto& [bb_name, value] : phi->incomings) {
                    std::string new_bb_name = bb_name + "_unroll_" + std::to_string(i);
                    std::string new_value = value_map.count(value) ? value_map[value] : value;
                    phi->incomings.push_back({new_bb_name, new_value});
                }
            }
        }
    }
    
    // Insert cloned blocks into function
    for (auto& bb : cloned_blocks) {
        func_.blocks.push_back(std::move(bb));
    }
    
    return true;
}

std::unique_ptr<Instruction> LoopUnrollingPass::CloneInstruction(
    const Instruction* inst, std::map<std::string, std::string>& value_map, int iteration) {
    
    auto cloned = std::make_unique<Instruction>();
    cloned->name = inst->name + "_unroll_" + std::to_string(iteration);
    cloned->type = inst->type;
    
    // Map old name to new name
    if (!inst->name.empty()) {
        value_map[inst->name] = cloned->name;
    }
    
    // Clone operands with renaming
    for (const auto& op : inst->operands) {
        std::string new_op = value_map.count(op) ? value_map[op] : op;
        cloned->operands.push_back(new_op);
    }
    
    return cloned;
}

// ============================================================================
// LICM (Loop-Invariant Code Motion) Pass Implementation
// ============================================================================

LICMPass::LICMPass(ir::Function& func) : func_(func) {}

bool LICMPass::Run() {
    LoopAnalysis analysis(func_);
    bool changed = false;
    
    for (const auto& loop_ptr : analysis.GetLoops()) {
        if (ProcessLoop(loop_ptr.get())) {
            changed = true;
        }
    }
    
    return changed;
}

bool LICMPass::ProcessLoop(LoopInfo* loop) {
    bool changed = false;
    std::vector<Instruction*> to_hoist;
    
    // Find loop-invariant instructions
    for (auto* bb : loop->body) {
        for (auto& inst_ptr : bb->instructions) {
            Instruction* inst = inst_ptr.get();
            if (IsLoopInvariant(inst, loop) && IsSafeToHoist(inst, loop)) {
                to_hoist.push_back(inst);
            }
        }
    }
    
    // Hoist invariant instructions to preheader
    for (auto* inst : to_hoist) {
        HoistInstruction(inst, loop);
        changed = true;
    }
    
    return changed;
}

bool LICMPass::IsLoopInvariant(ir::Instruction* inst, const LoopInfo* loop) const {
    // Check if all operands are defined outside the loop or are constants
    for (const auto& op : inst->operands) {
        // Simple heuristic: check if operand looks like a number
        bool is_constant = false;
        try {
            std::stoll(op);
            is_constant = true;
        } catch (...) {
            // Not a number
        }
        
        if (is_constant) continue;
        
        // Check if operand is defined outside the loop
        bool defined_in_loop = false;
        for (auto* bb : loop->body) {
            for (const auto& i : bb->instructions) {
                if (i->name == op) {
                    defined_in_loop = true;
                    break;
                }
            }
            for (const auto& phi : bb->phis) {
                if (phi->name == op) {
                    defined_in_loop = true;
                    break;
                }
            }
            if (defined_in_loop) break;
        }
        
        if (defined_in_loop) return false;
    }
    
    return true;
}

bool LICMPass::IsSafeToHoist(ir::Instruction* inst, const LoopInfo* loop) const {
    // Check if instruction has side effects
    if (dynamic_cast<StoreInstruction*>(inst)) return false;
    if (dynamic_cast<CallInstruction*>(inst)) return false;
    
    // Check if instruction can trap (e.g., division by zero)
    if (auto* bin = dynamic_cast<BinaryInstruction*>(inst)) {
        if (bin->op == BinaryInstruction::Op::kDiv || bin->op == BinaryInstruction::Op::kMod) {
            return false;  // Could divide by zero
        }
    }
    
    return true;
}

void LICMPass::HoistInstruction(ir::Instruction* inst, LoopInfo* loop) {
    // Create preheader if it doesn't exist
    if (!loop->preheader) {
        CreatePreheader(loop);
    }
    
    if (!loop->preheader) return;  // Failed to create preheader
    
    // Find the basic block containing this instruction
    BasicBlock* source_bb = nullptr;
    std::vector<std::unique_ptr<Instruction>>::iterator inst_it;
    for (auto* bb : loop->body) {
        for (auto it = bb->instructions.begin(); it != bb->instructions.end(); ++it) {
            if (it->get() == inst) {
                source_bb = bb;
                inst_it = it;
                break;
            }
        }
        if (source_bb) break;
    }
    
    if (!source_bb) return;
    
    // Move instruction to preheader (before terminator)
    auto inst_ptr = std::move(*inst_it);
    source_bb->instructions.erase(inst_it);
    
    // Insert before preheader's terminator
    loop->preheader->instructions.push_back(std::move(inst_ptr));
}

void LICMPass::CreatePreheader(LoopInfo* loop) {
    if (!loop->header) return;
    
    // Create new preheader block
    auto preheader = std::make_unique<BasicBlock>();
    preheader->name = loop->header->name + "_preheader";
    
    // Redirect predecessors outside loop to preheader
    for (const auto& pred_name : loop->header->predecessors) {
        // Check if predecessor is outside loop
        bool is_outside = true;
        for (auto* bb : loop->body) {
            if (bb->name == pred_name) {
                is_outside = false;
                break;
            }
        }
        
        if (is_outside) {
            // Update predecessor to jump to preheader
            for (auto& bb_ptr : func_.blocks) {
                if (bb_ptr->name == pred_name && bb_ptr->terminator) {
                    // Update branch target
                    for (auto& op : bb_ptr->terminator->operands) {
                        if (op == loop->header->name) {
                            op = preheader->name;
                        }
                    }
                }
            }
            
            // Update preheader's predecessor list
            preheader->predecessors.push_back(pred_name);
        }
    }
    
    // Preheader jumps to loop header
    auto branch = std::make_unique<Instruction>();
    branch->type = "br";
    branch->operands.push_back(loop->header->name);
    preheader->terminator = std::move(branch);
    preheader->successors.push_back(loop->header->name);
    
    // Update loop header's predecessors
    loop->header->predecessors.erase(
        std::remove_if(loop->header->predecessors.begin(),
                      loop->header->predecessors.end(),
                      [&](const std::string& pred) {
                          for (auto* bb : loop->body) {
                              if (bb->name == pred) return false;
                          }
                          return true;
                      }),
        loop->header->predecessors.end());
    loop->header->predecessors.push_back(preheader->name);
    
    // Add preheader to function
    loop->preheader = preheader.get();
    func_.blocks.push_back(std::move(preheader));
}

// ============================================================================
// Loop Fusion Pass Implementation
// ============================================================================

LoopFusionPass::LoopFusionPass(ir::Function& func) : func_(func) {}

bool LoopFusionPass::Run() {
    LoopAnalysis analysis(func_);
    bool changed = false;
    
    const auto& loops = analysis.GetLoops();
    for (size_t i = 0; i < loops.size(); ++i) {
        for (size_t j = i + 1; j < loops.size(); ++j) {
            if (CanFuseLoops(loops[i].get(), loops[j].get())) {
                // TODO: Implement loop fusion
                // FuseLoops(loops[i].get(), loops[j].get());
                // changed = true;
            }
        }
    }
    
    return changed;
}

bool LoopFusionPass::CanFuseLoops(const LoopInfo* loop1, const LoopInfo* loop2) const {
    // Check if loops have same header structure
    if (!loop1->header || !loop2->header) return false;
    
    // Check if loops are at same nesting level
    if (loop1->depth != loop2->depth) return false;
    
    // Check if loops have compatible iteration spaces
    // (simplified check - real implementation would analyze induction variables)
    if (loop1->body.size() + loop2->body.size() > 20) return false;  // Too large
    
    // Check for data dependencies between loops
    // (simplified - real implementation would use dependency analysis)
    for (auto* bb1 : loop1->body) {
        for (const auto& inst1 : bb1->instructions) {
            for (auto* bb2 : loop2->body) {
                for (const auto& inst2 : bb2->instructions) {
                    // Check if inst2 uses value defined by inst1
                    for (const auto& op : inst2->operands) {
                        if (op == inst1->name) {
                            return false;  // Data dependency
                        }
                    }
                }
            }
        }
    }
    
    return true;
}

void LoopFusionPass::FuseLoops(LoopInfo* loop1, LoopInfo* loop2) {
    if (!loop1->header || !loop2->header) return;
    
    // Merge loop2's body into loop1
    for (auto* bb : loop2->body) {
        if (bb != loop2->header) {  // Don't duplicate header
            loop1->body.insert(bb);
        }
    }
    
    // Merge phi nodes from loop2's header into loop1's header
    for (auto& phi : loop2->header->phis) {
        loop1->header->phis.push_back(std::move(phi));
    }
    
    // Update loop depth and nesting
    loop1->nested_loops.insert(loop1->nested_loops.end(),
                              loop2->nested_loops.begin(),
                              loop2->nested_loops.end());
    
    // Redirect loop2's exits to loop1
    loop1->exits.insert(loop2->exits.begin(), loop2->exits.end());
}

// ============================================================================
// Loop Strength Reduction Pass Implementation
// ============================================================================

LoopStrengthReductionPass::LoopStrengthReductionPass(ir::Function& func) : func_(func) {}

bool LoopStrengthReductionPass::Run() {
    LoopAnalysis analysis(func_);
    bool changed = false;
    
    for (const auto& loop_ptr : analysis.GetLoops()) {
        if (ProcessLoop(loop_ptr.get())) {
            changed = true;
        }
    }
    
    return changed;
}

bool LoopStrengthReductionPass::ProcessLoop(LoopInfo* loop) {
    bool changed = false;
    std::map<std::string, InductionVarInfo> induction_vars;
    
    // Identify induction variables
    for (auto* bb : loop->body) {
        for (auto& phi_ptr : bb->phis) {
            if (IsInductionVariable(phi_ptr->name, loop)) {
                InductionVarInfo info;
                info.base = phi_ptr->name;
                info.step = 1;  // Simplified
                induction_vars[phi_ptr->name] = info;
            }
        }
    }
    
    // Find multiplication by induction variables
    for (auto* bb : loop->body) {
        for (auto& inst_ptr : bb->instructions) {
            auto* bin_inst = dynamic_cast<BinaryInstruction*>(inst_ptr.get());
            if (!bin_inst || bin_inst->op != BinaryInstruction::Op::kMul) continue;
            
            // Check if one operand is induction variable
            std::string iv_name;
            std::string const_op;
            
            if (induction_vars.count(bin_inst->operands[0])) {
                iv_name = bin_inst->operands[0];
                const_op = bin_inst->operands[1];
            } else if (induction_vars.count(bin_inst->operands[1])) {
                iv_name = bin_inst->operands[1];
                const_op = bin_inst->operands[0];
            } else {
                continue;
            }
            
            // Check if other operand is loop-invariant
            bool is_invariant = true;
            for (auto* loop_bb : loop->body) {
                for (const auto& i : loop_bb->instructions) {
                    if (i->name == const_op) {
                        is_invariant = false;
                        break;
                    }
                }
            }
            
            if (!is_invariant) continue;
            
            // Replace multiplication with accumulator
            // Create new accumulator variable
            std::string accum_name = bin_inst->name + "_accum";
            
            // Add phi node for accumulator in loop header
            if (loop->header) {
                auto accum_phi = std::make_unique<PhiInstruction>();
                accum_phi->name = accum_name;
                accum_phi->type = bin_inst->type;
                accum_phi->incomings.push_back({loop->preheader ? loop->preheader->name : "", "0"});
                loop->header->phis.push_back(std::move(accum_phi));
            }
            
            // Replace multiplication with addition
            bin_inst->op = BinaryInstruction::Op::kAdd;
            bin_inst->operands[0] = accum_name;
            bin_inst->operands[1] = const_op;
            
            changed = true;
        }
    }
    
    return changed;
}

bool LoopStrengthReductionPass::IsInductionVariable(const std::string& var, const LoopInfo* loop) const {
    // Check if variable is a phi node that increments by a constant
    if (!loop->header) return false;
    
    for (const auto& phi_ptr : loop->header->phis) {
        if (phi_ptr->name != var) continue;
        
        // Check if phi has increment pattern: phi = [init, ..., phi + const]
        for (const auto& [bb_name, value] : phi_ptr->incomings) {
            // Look for bb in loop that has instruction: result = phi + const
            for (auto* bb : loop->body) {
                if (bb->name != bb_name) continue;
                
                for (const auto& inst : bb->instructions) {
                    auto* bin_inst = dynamic_cast<const BinaryInstruction*>(inst.get());
                    if (!bin_inst) continue;
                    
                    if (bin_inst->op == BinaryInstruction::Op::kAdd &&
                        (bin_inst->operands[0] == var || bin_inst->operands[1] == var)) {
                        return true;  // Found increment pattern
                    }
                }
            }
        }
    }
    
    return false;
}

}  // namespace polyglot::passes::transform
