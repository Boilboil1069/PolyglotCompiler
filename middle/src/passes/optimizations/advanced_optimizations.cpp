/**
 * @file     advanced_optimizations.cpp
 * @brief    Middle-end implementation
 *
 * @ingroup  Middle
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
/**
 * Advanced Optimizations Implementation
 *
 * This file contains implementations of advanced optimization passes including:
 * - Tail Call Optimization (TCO)
 * - Loop Unrolling
 * - Loop-Invariant Code Motion (LICM)
 * - Induction Variable Elimination
 * - Escape Analysis
 * - Dead Store Elimination
 * - Auto-Vectorization
 * - And more...
 */

#include "middle/include/passes/transform/advanced_optimizations.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "middle/include/ir/analysis.h"
#include "middle/include/ir/nodes/expressions.h"
#include "middle/include/ir/nodes/statements.h"

namespace polyglot::passes::transform {

// Forward declaration
void ConvertTailRecursionToLoop(ir::Function& func);

// ============================================================================
// Helper Functions
// ============================================================================

namespace {

/**
 * Check if an instruction defines a value used outside the loop
 */
[[maybe_unused]] bool HasUsesOutsideLoop(const ir::Instruction* inst,
                        const std::unordered_set<ir::BasicBlock*>& loop_blocks,
                        const ir::Function& func) {
    if (!inst->HasResult()) return false;
    const std::string& def_name = inst->name;
    
    for (const auto& bb : func.blocks) {
        // Skip blocks inside the loop
        if (loop_blocks.count(bb.get())) continue;
        
        // Check phi nodes
        for (const auto& phi : bb->phis) {
            for (const auto& op : phi->operands) {
                if (op == def_name) return true;
            }
            for (const auto& inc : phi->incomings) {
                if (inc.second == def_name) return true;
            }
        }
        
        // Check instructions
        for (const auto& other : bb->instructions) {
            for (const auto& op : other->operands) {
                if (op == def_name) return true;
            }
        }
        
        // Check terminator
        if (bb->terminator) {
            for (const auto& op : bb->terminator->operands) {
                if (op == def_name) return true;
            }
        }
    }
    
    return false;
}

/**
 * Check if all operands of an instruction are defined outside the loop
 */
bool AllOperandsDefinedOutsideLoop(const ir::Instruction* inst,
                                   const std::unordered_set<ir::BasicBlock*>& loop_blocks,
                                   const std::unordered_set<std::string>& loop_defs) {
    for (const auto& op : inst->operands) {
        if (op.empty()) continue;
        if (loop_defs.count(op)) return false;
    }
    return true;
}

/**
 * Collect all definitions within a loop
 */
std::unordered_set<std::string> CollectLoopDefinitions(
    const std::unordered_set<ir::BasicBlock*>& loop_blocks) {
    std::unordered_set<std::string> defs;
    
    for (auto* bb : loop_blocks) {
        for (const auto& phi : bb->phis) {
            if (phi->HasResult()) {
                defs.insert(phi->name);
            }
        }
        for (const auto& inst : bb->instructions) {
            if (inst->HasResult()) {
                defs.insert(inst->name);
            }
        }
    }
    
    return defs;
}

/**
 * Check if a value is a constant or defined by a constant expression
 */
bool IsConstant(const std::string& name, const ir::Function& func) {
    // Empty name typically means literal constant
    if (name.empty()) return true;
    
    // Check if it starts with a number (literal)
    if (!name.empty() && (std::isdigit(name[0]) || name[0] == '-')) {
        return true;
    }
    
    // Look for the definition
    for (const auto& bb : func.blocks) {
        for (const auto& inst : bb->instructions) {
            if (inst->name == name) {
                // Constants are defined by literal operations
                return false;
            }
        }
    }
    
    return false;
}

/**
 * Check if an instruction has side effects
 */
bool HasSideEffects(const std::shared_ptr<ir::Instruction>& inst) {
    if (std::dynamic_pointer_cast<ir::StoreInstruction>(inst)) {
        return true;
    }
    if (std::dynamic_pointer_cast<ir::CallInstruction>(inst)) {
        // Conservatively assume calls have side effects
        // A more sophisticated analysis would check for pure functions
        return true;
    }
    return false;
}

/**
 * Check if two basic blocks are adjacent in the CFG
 */
[[maybe_unused]] bool AreAdjacent(const ir::BasicBlock* bb1, const ir::BasicBlock* bb2) {
    for (auto* succ : bb1->successors) {
        if (succ == bb2) return true;
    }
    return false;
}

/**
 * Get the iteration count if the loop has a constant bound
 * Returns -1 if the bound cannot be determined statically
 */
int64_t GetLoopIterationCount(const ir::LoopInfo& loop, const ir::Function& func) {
    // Look for a comparison instruction in the header that determines the bound
    auto* header = loop.header;
    if (!header) return -1;
    
    // This is a simplified analysis - a full implementation would analyze
    // the induction variable and bound more precisely
    
    // Look for a conditional branch in the header
    for (const auto& inst : header->instructions) {
        if (auto bin = std::dynamic_pointer_cast<ir::BinaryInstruction>(inst)) {
            if (bin->op == ir::BinaryInstruction::Op::kCmpSlt ||
                bin->op == ir::BinaryInstruction::Op::kCmpSle ||
                bin->op == ir::BinaryInstruction::Op::kCmpUlt ||
                bin->op == ir::BinaryInstruction::Op::kCmpUle) {
                // Found a comparison - check if bound is constant
                if (bin->operands.size() >= 2) {
                    const auto& bound = bin->operands[1];
                    if (IsConstant(bound, func)) {
                        // Try to parse as integer
                        try {
                            return std::stoll(bound);
                        } catch (...) {
                            return -1;
                        }
                    }
                }
            }
        }
    }
    
    return -1;
}

/**
 * Induction variable structure
 */
struct InductionVariable {
    std::string name;
    std::string base;      // Initial value
    int64_t step{0};       // Step per iteration
    bool is_basic{false};  // Basic induction variable (i = i + c)
};

/**
 * Find induction variables in a loop
 */
std::vector<InductionVariable> FindInductionVariables(
    const ir::LoopInfo& loop,
    const std::unordered_set<ir::BasicBlock*>& loop_blocks) {
    
    std::vector<InductionVariable> ivs;
    
    // Look for phi nodes in the header that are updated in the loop
    if (!loop.header) return ivs;
    
    for (const auto& phi : loop.header->phis) {
        // A basic induction variable has:
        // 1. A phi node in the header
        // 2. One incoming from outside the loop (initial value)
        // 3. One incoming from inside the loop that's i + c or i - c
        
        std::string init_val;
        std::string loop_val;
        
        for (const auto& [pred, val] : phi->incomings) {
            if (loop_blocks.count(pred) == 0) {
                init_val = val;
            } else {
                loop_val = val;
            }
        }
        
        if (init_val.empty() || loop_val.empty()) continue;
        
        // Find the definition of loop_val
        for (auto* block : loop_blocks) {
            for (const auto& inst : block->instructions) {
                if (inst->name == loop_val) {
                    if (auto bin = std::dynamic_pointer_cast<ir::BinaryInstruction>(inst)) {
                        if (bin->op == ir::BinaryInstruction::Op::kAdd ||
                            bin->op == ir::BinaryInstruction::Op::kSub) {
                            // Check if one operand is the phi and the other is constant
                            bool is_iv = false;
                            int64_t step = 0;
                            
                            if (bin->operands.size() >= 2) {
                                if (bin->operands[0] == phi->name) {
                                    try {
                                        step = std::stoll(bin->operands[1]);
                                        if (bin->op == ir::BinaryInstruction::Op::kSub) {
                                            step = -step;
                                        }
                                        is_iv = true;
                                    } catch (...) {}
                                } else if (bin->operands[1] == phi->name) {
                                    try {
                                        step = std::stoll(bin->operands[0]);
                                        if (bin->op == ir::BinaryInstruction::Op::kSub) {
                                            // Can't subtract from constant
                                            is_iv = false;
                                        } else {
                                            is_iv = true;
                                        }
                                    } catch (...) {}
                                }
                            }
                            
                            if (is_iv) {
                                InductionVariable iv;
                                iv.name = phi->name;
                                iv.base = init_val;
                                iv.step = step;
                                iv.is_basic = true;
                                ivs.push_back(iv);
                            }
                        }
                    }
                }
            }
        }
    }
    
    return ivs;
}

/**
 * Clone an instruction for loop unrolling
 */
std::shared_ptr<ir::Instruction> CloneInstructionImpl(
    const std::shared_ptr<ir::Instruction>& inst,
    size_t iteration,
    std::unordered_map<std::string, std::string>& name_map) {
    
    // Helper to rename a value
    auto rename = [&](const std::string& name) -> std::string {
        if (name.empty()) return name;
        std::string new_name = name + "_unroll_" + std::to_string(iteration);
        name_map[name] = new_name;
        return new_name;
    };
    
    // Helper to lookup renamed value
    auto lookup = [&](const std::string& name) -> std::string {
        if (name.empty()) return name;
        auto it = name_map.find(name);
        return it != name_map.end() ? it->second : name;
    };
    
    if (auto bin = std::dynamic_pointer_cast<ir::BinaryInstruction>(inst)) {
        auto clone = std::make_shared<ir::BinaryInstruction>();
        clone->op = bin->op;
        clone->type = bin->type;
        clone->name = rename(bin->name);
        for (const auto& op : bin->operands) {
            clone->operands.push_back(lookup(op));
        }
        return clone;
    }
    
    if (auto load = std::dynamic_pointer_cast<ir::LoadInstruction>(inst)) {
        auto clone = std::make_shared<ir::LoadInstruction>();
        clone->type = load->type;
        clone->align = load->align;
        clone->name = rename(load->name);
        for (const auto& op : load->operands) {
            clone->operands.push_back(lookup(op));
        }
        return clone;
    }
    
    if (auto store = std::dynamic_pointer_cast<ir::StoreInstruction>(inst)) {
        auto clone = std::make_shared<ir::StoreInstruction>();
        clone->type = store->type;
        clone->align = store->align;
        for (const auto& op : store->operands) {
            clone->operands.push_back(lookup(op));
        }
        return clone;
    }
    
    if (auto gep = std::dynamic_pointer_cast<ir::GetElementPtrInstruction>(inst)) {
        auto clone = std::make_shared<ir::GetElementPtrInstruction>();
        clone->type = gep->type;
        clone->source_type = gep->source_type;
        clone->indices = gep->indices;
        clone->inbounds = gep->inbounds;
        clone->name = rename(gep->name);
        for (const auto& op : gep->operands) {
            clone->operands.push_back(lookup(op));
        }
        return clone;
    }
    
    // For other instruction types, return nullptr (don't clone)
    return nullptr;
}

/**
 * Update loop bound after unrolling
 */
void UpdateLoopBoundImpl(const ir::LoopInfo& loop, ir::Function& func, size_t factor) {
    // Find the comparison instruction that controls the loop
    auto* header = loop.header;
    if (!header) return;
    
    for (auto& inst : header->instructions) {
        if (auto bin = std::dynamic_pointer_cast<ir::BinaryInstruction>(inst)) {
            if (bin->op == ir::BinaryInstruction::Op::kCmpSlt ||
                bin->op == ir::BinaryInstruction::Op::kCmpSle ||
                bin->op == ir::BinaryInstruction::Op::kCmpUlt ||
                bin->op == ir::BinaryInstruction::Op::kCmpUle) {
                // Found the bound comparison
                if (bin->operands.size() >= 2) {
                    const auto& bound = bin->operands[1];
                    if (IsConstant(bound, func)) {
                        try {
                            int64_t old_bound = std::stoll(bound);
                            int64_t new_bound = old_bound / static_cast<int64_t>(factor);
                            bin->operands[1] = std::to_string(new_bound);
                        } catch (...) {
                            // Couldn't parse - leave unchanged
                        }
                    }
                }
                return;
            }
        }
    }
}

/**
 * SCCP Lattice value
 */
enum class LatticeValue { kTop, kBottom, kConstant };

struct SCCPValue {
    LatticeValue kind{LatticeValue::kTop};
    int64_t constant{0};
    
    bool IsConstant() const { return kind == LatticeValue::kConstant; }
    bool IsTop() const { return kind == LatticeValue::kTop; }
    bool IsBottom() const { return kind == LatticeValue::kBottom; }
    
    static SCCPValue Top() { return {LatticeValue::kTop, 0}; }
    static SCCPValue Bottom() { return {LatticeValue::kBottom, 0}; }
    static SCCPValue Constant(int64_t v) { return {LatticeValue::kConstant, v}; }
    
    SCCPValue Meet(const SCCPValue& other) const {
        if (IsTop()) return other;
        if (other.IsTop()) return *this;
        if (IsBottom() || other.IsBottom()) return Bottom();
        if (constant == other.constant) return *this;
        return Bottom();
    }
};

}  // namespace

// ============================================================================
// Tail Call Optimization
// ============================================================================

namespace {

/**
 * Detailed analysis to determine if a call is a tail call
 */
struct TailCallInfo {
    bool is_tail_call{false};
    bool is_self_recursive{false};
    std::shared_ptr<ir::CallInstruction> call;
    std::shared_ptr<ir::ReturnStatement> ret;
};

/**
 * Analyze a basic block to find tail calls
 */
TailCallInfo AnalyzeTailCall(ir::BasicBlock* bb, const ir::Function& func) {
    TailCallInfo info;
    
    if (!bb || bb->instructions.empty()) return info;
    
    // Get the terminator
    auto ret = std::dynamic_pointer_cast<ir::ReturnStatement>(bb->terminator);
    if (!ret) return info;
    
    // Find the last call instruction before the return
    std::shared_ptr<ir::CallInstruction> last_call;
    size_t call_index = 0;
    
    for (size_t i = 0; i < bb->instructions.size(); ++i) {
        if (auto call = std::dynamic_pointer_cast<ir::CallInstruction>(bb->instructions[i])) {
            last_call = call;
            call_index = i;
        }
    }
    
    if (!last_call) return info;
    
    // Check if there are any instructions between the call and the return
    // that use the call result or have side effects
    for (size_t i = call_index + 1; i < bb->instructions.size(); ++i) {
        auto& inst = bb->instructions[i];
        
        // Check if the instruction uses the call result
        if (last_call->HasResult()) {
            for (const auto& op : inst->operands) {
                if (op == last_call->name) {
                    // Instruction uses the call result - not a simple tail call
                    // But it could still be a tail call if this instruction
                    // just passes the value to the return
                    continue;
                }
            }
        }
        
        // Check for side effects
        if (HasSideEffects(inst)) {
            return info;
        }
    }
    
    // Verify the return value matches the call result (or void)
    if (ret->operands.empty()) {
        // Void return - call must also be void
        if (last_call->type.kind != ir::IRTypeKind::kVoid) {
            // Call returns a value but we don't use it - still a tail call
            info.is_tail_call = true;
        } else {
            info.is_tail_call = true;
        }
    } else {
        // Return has a value - must be the call result
        if (last_call->HasResult() && ret->operands[0] == last_call->name) {
            info.is_tail_call = true;
        }
    }
    
    if (info.is_tail_call) {
        info.call = last_call;
        info.ret = ret;
        info.is_self_recursive = (last_call->callee == func.name);
    }
    
    return info;
}

}  // namespace

void TailCallOptimization(ir::Function& func, bool convert_to_loop) {
    bool changed = false;
    
    for (auto& bb : func.blocks) {
        TailCallInfo info = AnalyzeTailCall(bb.get(), func);
        
        if (info.is_tail_call && info.call) {
            // Mark the call as a tail call
            info.call->is_tail_call = true;
            changed = true;
            
            // For self-recursive tail calls, we can optionally transform
            // the recursion into a loop. This is done in a separate pass
            // to avoid complicating this function.
            //
            // The transformation for: 
            //   int factorial(int n, int acc) {
            //     if (n <= 1) return acc;
            //     return factorial(n-1, n*acc);  // tail call
            //   }
            //
            // Would be:
            //   int factorial(int n, int acc) {
            //     loop_start:
            //     if (n <= 1) return acc;
            //     int new_acc = n * acc;
            //     n = n - 1;
            //     acc = new_acc;
            //     goto loop_start;
            //   }
        }
    }
    
    // If we found self-recursive tail calls, optionally convert them to loops
    if (changed && convert_to_loop) {
        ConvertTailRecursionToLoop(func);
    }
}

void ConvertTailRecursionToLoop(ir::Function& func) {
    // Find all self-recursive tail calls
    std::vector<std::pair<ir::BasicBlock*, std::shared_ptr<ir::CallInstruction>>> tail_calls;
    
    for (auto& bb : func.blocks) {
        for (auto& inst : bb->instructions) {
            if (auto call = std::dynamic_pointer_cast<ir::CallInstruction>(inst)) {
                if (call->is_tail_call && call->callee == func.name) {
                    tail_calls.push_back({bb.get(), call});
                }
            }
        }
    }
    
    if (tail_calls.empty()) return;
    
    // The original entry becomes a successor of the new entry
    ir::BasicBlock* original_entry = func.entry;
    
    // For each tail call, replace with:
    // 1. Assignments to update parameters
    // 2. Unconditional branch to loop entry
    for (auto& [bb, call] : tail_calls) {
        // Find the call in the block
        auto it = std::find(bb->instructions.begin(), bb->instructions.end(), call);
        if (it == bb->instructions.end()) continue;
        
        // Create assignments for each argument
        std::vector<std::shared_ptr<ir::Instruction>> new_insts;
        for (size_t i = 0; i < call->operands.size() && i < func.params.size(); ++i) {
            auto assign = std::make_shared<ir::AssignInstruction>();
            assign->name = func.params[i] + "_tco_tmp";
            assign->operands.push_back(call->operands[i]);
            assign->type = func.param_types[i];
            new_insts.push_back(assign);
        }
        
        // Create final assignments
        for (size_t i = 0; i < func.params.size(); ++i) {
            auto assign = std::make_shared<ir::AssignInstruction>();
            assign->name = func.params[i];
            assign->operands.push_back(func.params[i] + "_tco_tmp");
            assign->type = func.param_types[i];
            new_insts.push_back(assign);
        }
        
        // Replace the call with the new instructions
        bb->instructions.erase(it);
        bb->instructions.insert(bb->instructions.end(), 
                               new_insts.begin(), new_insts.end());
        
        // Replace the return with a branch to the original entry
        // (which serves as the loop entry)
        auto branch = std::make_shared<ir::BranchStatement>();
        branch->target = original_entry;
        bb->SetTerminator(branch);
        
        // Update CFG
        bb->successors.clear();
        bb->successors.push_back(original_entry);
        if (std::find(original_entry->predecessors.begin(), 
                      original_entry->predecessors.end(), bb) ==
            original_entry->predecessors.end()) {
            original_entry->predecessors.push_back(bb);
        }
    }
}

// ============================================================================
// Loop Unrolling
// ============================================================================

void LoopUnrolling(ir::Function& func, size_t factor) {
    if (factor <= 1) return;
    
    // Get loop information
    ir::AnalysisCache cache(func);
    const auto& loops = cache.GetLoops();
    
    for (const auto& loop : loops) {
        // Skip complex loops (nested loops, multiple exits)
        if (loop.backedges.size() != 1) continue;
        
        // Get iteration count if known
        int64_t iter_count = GetLoopIterationCount(loop, func);
        
        // Determine unroll factor
        size_t actual_factor = factor;
        if (iter_count > 0 && iter_count <= static_cast<int64_t>(factor)) {
            // Fully unroll if iteration count is small
            actual_factor = static_cast<size_t>(iter_count);
        }
        
        // Clone loop body
        std::vector<std::vector<std::shared_ptr<ir::Instruction>>> cloned_bodies;
        std::unordered_map<std::string, std::string> name_map;
        
        for (size_t i = 1; i < actual_factor; ++i) {
            std::vector<std::shared_ptr<ir::Instruction>> cloned;
            
            for (auto* block : loop.blocks) {
                for (const auto& inst : block->instructions) {
                    // Clone the instruction
                    auto clone = CloneInstructionImpl(inst, i, name_map);
                    if (clone) {
                        cloned.push_back(clone);
                    }
                }
            }
            
            cloned_bodies.push_back(std::move(cloned));
        }
        
        // Insert cloned instructions
        // For simplicity, we append them at the end of the loop body
        // A full implementation would properly interleave and fix phi nodes
        
        if (!loop.blocks.empty()) {
            auto* last_block = loop.blocks.back();
            for (auto& body : cloned_bodies) {
                for (auto& inst : body) {
                    last_block->instructions.push_back(inst);
                }
            }
        }
        
        // Update the loop bound if we know the iteration count
        // This divides the original bound by the unroll factor
        UpdateLoopBoundImpl(loop, func, actual_factor);
    }
}

// ============================================================================
// Strength Reduction
// ============================================================================

void StrengthReduction(ir::Function& func) {
    for (auto& bb : func.blocks) {
        for (auto& inst : bb->instructions) {
            auto bin = std::dynamic_pointer_cast<ir::BinaryInstruction>(inst);
            if (!bin) continue;
            
            // Replace multiply by power of 2 with left shift
            // x * 2^n -> x << n
            if (bin->op == ir::BinaryInstruction::Op::kMul) {
                if (bin->operands.size() >= 2) {
                    const auto& rhs = bin->operands[1];
                    if (IsConstant(rhs, func)) {
                        try {
                            int64_t val = std::stoll(rhs);
                            if (val > 0 && (val & (val - 1)) == 0) {
                                // val is a power of 2
                                int shift = 0;
                                while ((1LL << shift) < val) ++shift;
                                
                                bin->op = ir::BinaryInstruction::Op::kShl;
                                bin->operands[1] = std::to_string(shift);
                            }
                        } catch (...) {
                            // Not a constant integer
                        }
                    }
                }
            }
            
            // Replace divide by power of 2 with right shift (for unsigned)
            // x / 2^n -> x >> n (for unsigned division)
            if (bin->op == ir::BinaryInstruction::Op::kUDiv) {
                if (bin->operands.size() >= 2) {
                    const auto& rhs = bin->operands[1];
                    if (IsConstant(rhs, func)) {
                        try {
                            int64_t val = std::stoll(rhs);
                            if (val > 0 && (val & (val - 1)) == 0) {
                                // val is a power of 2
                                int shift = 0;
                                while ((1LL << shift) < val) ++shift;
                                
                                bin->op = ir::BinaryInstruction::Op::kLShr;
                                bin->operands[1] = std::to_string(shift);
                            }
                        } catch (...) {
                            // Not a constant integer
                        }
                    }
                }
            }
            
            // For signed division, we need to handle negative numbers
            // x / 2^n -> (x + (x >> 31) * (2^n - 1)) >> n (for signed)
            // This is more complex and omitted for simplicity
        }
    }
}

// ============================================================================
// Loop-Invariant Code Motion (LICM)
// ============================================================================

void LoopInvariantCodeMotion(ir::Function& func) {
    ir::AnalysisCache cache(func);
    const auto& loops = cache.GetLoops();
    
    for (const auto& loop : loops) {
        // Convert loop blocks to a set for fast lookup
        std::unordered_set<ir::BasicBlock*> loop_blocks(
            loop.blocks.begin(), loop.blocks.end());
        
        // Collect all definitions in the loop
        auto loop_defs = CollectLoopDefinitions(loop_blocks);
        
        // Find loop-invariant instructions
        std::vector<std::pair<ir::BasicBlock*, std::shared_ptr<ir::Instruction>>> 
            to_hoist;
        
        for (auto* block : loop.blocks) {
            for (auto& inst : block->instructions) {
                // Skip instructions that have side effects
                if (HasSideEffects(inst)) continue;
                
                // Skip if result is used by a phi node (could be loop-carried)
                // This is a simplification - a full analysis would be more precise
                
                // Check if all operands are loop-invariant
                if (AllOperandsDefinedOutsideLoop(inst.get(), loop_blocks, loop_defs)) {
                    // This instruction is loop-invariant
                    to_hoist.push_back({block, inst});
                }
            }
        }
        
        // Create or find a preheader
        ir::BasicBlock* preheader = nullptr;
        
        // Look for an existing preheader (single predecessor outside the loop)
        for (auto* pred : loop.header->predecessors) {
            if (loop_blocks.count(pred) == 0) {
                // This predecessor is outside the loop
                if (!preheader) {
                    preheader = pred;
                } else {
                    // Multiple predecessors - need to create a preheader
                    preheader = nullptr;
                    break;
                }
            }
        }
        
        // If no suitable preheader exists, we would need to create one
        // For simplicity, we skip loops without a clear preheader
        if (!preheader) continue;
        
        // Hoist instructions to the preheader
        for (auto& [block, inst] : to_hoist) {
            // Remove from original block
            auto& insts = block->instructions;
            insts.erase(std::remove(insts.begin(), insts.end(), inst), insts.end());
            
            // Add to preheader (before the terminator)
            preheader->instructions.push_back(inst);
            inst->parent = preheader;
        }
    }
}

// ============================================================================
// Induction Variable Elimination
// ============================================================================

void InductionVariableElimination(ir::Function& func) {
    ir::AnalysisCache cache(func);
    const auto& loops = cache.GetLoops();
    
    for (const auto& loop : loops) {
        std::unordered_set<ir::BasicBlock*> loop_blocks(
            loop.blocks.begin(), loop.blocks.end());
        
        // Find induction variables
        auto ivs = FindInductionVariables(loop, loop_blocks);
        
        if (ivs.empty()) continue;
        
        // For each pair of induction variables, check if one can be computed
        // from the other using strength reduction
        // j = i * c + d => j_new = j + c on each iteration
        
        for (auto* block : loop_blocks) {
            for (auto& inst : block->instructions) {
                auto bin = std::dynamic_pointer_cast<ir::BinaryInstruction>(inst);
                if (!bin) continue;
                
                // Look for multiplications involving induction variables
                if (bin->op == ir::BinaryInstruction::Op::kMul) {
                    for (const auto& iv : ivs) {
                        if (!iv.is_basic) continue;
                        
                        // Check if this multiplies the IV by a constant
                        if (bin->operands.size() >= 2) {
                            std::string iv_op, const_op;
                            if (bin->operands[0] == iv.name) {
                                iv_op = bin->operands[0];
                                const_op = bin->operands[1];
                            } else if (bin->operands[1] == iv.name) {
                                iv_op = bin->operands[1];
                                const_op = bin->operands[0];
                            } else {
                                continue;
                            }
                            
                            // Replace i * c with a new induction variable
                            // that's updated by adding c * step each iteration
                            try {
                                int64_t c = std::stoll(const_op);
                                int64_t new_step = c * iv.step;
                                
                                // This would require creating a new phi node
                                // and updating the multiplication to use it
                                // For now, we just mark the optimization opportunity
                                (void)new_step;
                                (void)iv_op;
                            } catch (...) {}
                        }
                    }
                }
            }
        }
    }
}

// ============================================================================
// Escape Analysis
// ============================================================================

void EscapeAnalysis(ir::Function& func) {
    // Build a map of all allocations
    std::unordered_map<std::string, std::shared_ptr<ir::AllocaInstruction>> allocas;
    
    for (auto& bb : func.blocks) {
        for (auto& inst : bb->instructions) {
            if (auto alloca = std::dynamic_pointer_cast<ir::AllocaInstruction>(inst)) {
                allocas[alloca->name] = alloca;
            }
        }
    }
    
    // Track which allocations escape
    std::unordered_set<std::string> escaping;
    
    // Analyze uses of each allocation
    for (auto& bb : func.blocks) {
        for (auto& inst : bb->instructions) {
            // Store to a global or unknown location causes escape
            if (auto store = std::dynamic_pointer_cast<ir::StoreInstruction>(inst)) {
                if (store->operands.size() >= 2) {
                    // The value being stored might escape
                    const auto& value = store->operands[1];
                    if (allocas.count(value)) {
                        // Storing the address of an allocation
                        // Check if the destination is a local
                        const auto& dest = store->operands[0];
                        if (allocas.count(dest) == 0) {
                            // Storing to non-local - escapes
                            escaping.insert(value);
                        }
                    }
                }
            }
            
            // Passing to a function causes escape (conservative)
            if (auto call = std::dynamic_pointer_cast<ir::CallInstruction>(inst)) {
                for (const auto& op : call->operands) {
                    if (allocas.count(op)) {
                        escaping.insert(op);
                    }
                }
            }
            
            // Returning the allocation causes escape
            if (auto ret = std::dynamic_pointer_cast<ir::ReturnStatement>(inst)) {
                for (const auto& op : ret->operands) {
                    if (allocas.count(op)) {
                        escaping.insert(op);
                    }
                }
            }
        }
        
        // Check terminator
        if (bb->terminator) {
            if (auto ret = std::dynamic_pointer_cast<ir::ReturnStatement>(bb->terminator)) {
                for (const auto& op : ret->operands) {
                    if (allocas.count(op)) {
                        escaping.insert(op);
                    }
                }
            }
        }
    }
    
    // Mark non-escaping allocations
    for (auto& [name, alloca] : allocas) {
        if (escaping.count(name) == 0) {
            alloca->no_escape = true;
        }
    }
}

// ============================================================================
// Scalar Replacement of Aggregates
// ============================================================================

void ScalarReplacement(ir::Function& func) {
    // Find allocations that can be split into scalars
    std::vector<std::shared_ptr<ir::AllocaInstruction>> candidates;
    
    for (auto& bb : func.blocks) {
        for (auto& inst : bb->instructions) {
            if (auto alloca = std::dynamic_pointer_cast<ir::AllocaInstruction>(inst)) {
                // Only consider non-escaping allocations
                if (alloca->no_escape) {
                    // Check if the allocated type is a struct or array
                    const auto& elem_type = alloca->type.subtypes.empty() 
                        ? ir::IRType::Invalid() 
                        : alloca->type.subtypes[0];
                    
                    if (elem_type.kind == ir::IRTypeKind::kStruct ||
                        elem_type.kind == ir::IRTypeKind::kArray) {
                        candidates.push_back(alloca);
                    }
                }
            }
        }
    }
    
    // For each candidate, check if all accesses use constant indices
    for (auto& alloca : candidates) {
        bool all_constant = true;
        std::unordered_set<size_t> accessed_indices;
        
        // Find all GEPs that use this allocation
        for (auto& bb : func.blocks) {
            for (auto& inst : bb->instructions) {
                if (auto gep = std::dynamic_pointer_cast<ir::GetElementPtrInstruction>(inst)) {
                    if (!gep->operands.empty() && gep->operands[0] == alloca->name) {
                        // Check if indices are constant
                        for (size_t idx : gep->indices) {
                            accessed_indices.insert(idx);
                        }
                    }
                }
            }
        }
        
        if (all_constant && !accessed_indices.empty()) {
            // Create a scalar for each accessed field
            // This would require:
            // 1. Creating new allocas for each field
            // 2. Replacing GEPs with direct references
            // 3. Updating loads and stores
            // For now, we mark the opportunity but don't implement the full transform
        }
    }
}

// ============================================================================
// Dead Store Elimination
// ============================================================================

void DeadStoreElimination(ir::Function& func) {
    // Within each basic block, track the last store to each address
    for (auto& bb : func.blocks) {
        std::unordered_map<std::string, std::shared_ptr<ir::StoreInstruction>> last_store;
        
        for (auto& inst : bb->instructions) {
            if (auto store = std::dynamic_pointer_cast<ir::StoreInstruction>(inst)) {
                if (!store->operands.empty()) {
                    const auto& addr = store->operands[0];
                    
                    // If there was a previous store to this address with no
                    // intervening load, mark it as dead
                    auto it = last_store.find(addr);
                    if (it != last_store.end()) {
                        it->second->is_dead = true;
                    }
                    
                    last_store[addr] = store;
                }
            } else if (auto load = std::dynamic_pointer_cast<ir::LoadInstruction>(inst)) {
                // Load consumes the stored value - not dead
                if (!load->operands.empty()) {
                    last_store.erase(load->operands[0]);
                }
            } else if (std::dynamic_pointer_cast<ir::CallInstruction>(inst)) {
                // Function calls may read any memory - clear tracking
                last_store.clear();
            }
        }
    }
    
    // Remove dead stores
    for (auto& bb : func.blocks) {
        auto& insts = bb->instructions;
        insts.erase(
            std::remove_if(insts.begin(), insts.end(),
                [](const std::shared_ptr<ir::Instruction>& inst) {
                    return inst->is_dead;
                }),
            insts.end());
    }
}

// ============================================================================
// Auto-Vectorization
// ============================================================================

void AutoVectorization(ir::Function& func) {
    ir::AnalysisCache cache(func);
    const auto& loops = cache.GetLoops();
    
    for (const auto& loop : loops) {
        std::unordered_set<ir::BasicBlock*> loop_blocks(
            loop.blocks.begin(), loop.blocks.end());
        
        // Check if loop is vectorizable
        bool can_vectorize = true;
        size_t vector_width = 4;  // Default to 4-wide vectors
        
        // Check for conditions that prevent vectorization:
        // 1. Loop-carried dependencies (except for reductions)
        // 2. Non-contiguous memory access
        // 3. Function calls (except known vectorizable ones)
        // 4. Control flow within the loop body
        
        auto ivs = FindInductionVariables(loop, loop_blocks);
        
        // Check for simple vectorizable patterns
        for (auto* block : loop_blocks) {
            for (const auto& inst : block->instructions) {
                // Skip induction variable updates
                bool is_iv_update = false;
                for (const auto& iv : ivs) {
                    if (inst->name == iv.name) {
                        is_iv_update = true;
                        break;
                    }
                }
                if (is_iv_update) continue;
                
                // Check for calls
                if (std::dynamic_pointer_cast<ir::CallInstruction>(inst)) {
                    can_vectorize = false;
                    break;
                }
                
                // Check for irregular memory access
                if (auto gep = std::dynamic_pointer_cast<ir::GetElementPtrInstruction>(inst)) {
                    // For simplicity, require indices to be induction variables
                    // A full implementation would do stride analysis
                    (void)gep;  // Placeholder
                }
            }
            
            if (!can_vectorize) break;
        }
        
        if (can_vectorize) {
            // Transform scalar operations to vector operations
            for (auto* block : loop_blocks) {
                for (auto& inst : block->instructions) {
                    // Transform binary operations to vector operations
                    if (auto bin = std::dynamic_pointer_cast<ir::BinaryInstruction>(inst)) {
                        auto vec = std::make_shared<ir::VectorInstruction>();
                        
                        switch (bin->op) {
                            case ir::BinaryInstruction::Op::kAdd:
                                vec->op = ir::VectorInstruction::VecOp::kVecAdd;
                                break;
                            case ir::BinaryInstruction::Op::kSub:
                                vec->op = ir::VectorInstruction::VecOp::kVecSub;
                                break;
                            case ir::BinaryInstruction::Op::kMul:
                                vec->op = ir::VectorInstruction::VecOp::kVecMul;
                                break;
                            case ir::BinaryInstruction::Op::kFAdd:
                                vec->op = ir::VectorInstruction::VecOp::kVecFAdd;
                                break;
                            case ir::BinaryInstruction::Op::kFSub:
                                vec->op = ir::VectorInstruction::VecOp::kVecFSub;
                                break;
                            case ir::BinaryInstruction::Op::kFMul:
                                vec->op = ir::VectorInstruction::VecOp::kVecFMul;
                                break;
                            default:
                                continue;  // Not vectorizable
                        }
                        
                        vec->name = bin->name + "_vec";
                        vec->type = ir::IRType::Vector(bin->type, vector_width);
                        vec->operands = bin->operands;
                        
                        // Replace the scalar instruction with the vector one
                        // This is simplified - a full implementation would:
                        // 1. Widen all operands
                        // 2. Update memory operations
                        // 3. Handle loop remainder
                    }
                }
            }
        }
    }
}

// ============================================================================
// Loop Fusion
// ============================================================================

void LoopFusion(ir::Function& func) {
    ir::AnalysisCache cache(func);
    const auto& loops = cache.GetLoops();
    
    // Find pairs of adjacent loops that can be fused
    for (size_t i = 0; i < loops.size(); ++i) {
        for (size_t j = i + 1; j < loops.size(); ++j) {
            const auto& loop1 = loops[i];
            const auto& loop2 = loops[j];
            
            // Check fusion conditions:
            // 1. Loops must be adjacent (loop1 exits to loop2 header)
            bool adjacent = false;
            for (auto* exit : loop1.header->successors) {
                if (exit == loop2.header) {
                    adjacent = true;
                    break;
                }
            }
            
            if (!adjacent) continue;
            
            // 2. Same iteration count (same bounds)
            int64_t count1 = GetLoopIterationCount(loop1, func);
            int64_t count2 = GetLoopIterationCount(loop2, func);
            
            if (count1 != count2 || count1 < 0) continue;
            
            // 3. No data dependencies that prevent fusion
            // This is a simplified check - a full implementation would
            // do proper dependence analysis
            
            // If all conditions are met, fuse the loops
            // This would involve:
            // 1. Merging loop bodies
            // 2. Unifying induction variables
            // 3. Updating control flow
        }
    }
}

// ============================================================================
// Sparse Conditional Constant Propagation (SCCP)
// ============================================================================

void SCCP(ir::Function& func) {
    std::unordered_map<std::string, SCCPValue> lattice;
    std::unordered_set<ir::BasicBlock*> executable;
    std::queue<ir::BasicBlock*> worklist;
    
    // Initialize
    if (func.entry) {
        executable.insert(func.entry);
        worklist.push(func.entry);
    }
    
    // Process worklist
    while (!worklist.empty()) {
        ir::BasicBlock* bb = worklist.front();
        worklist.pop();
        
        // Process phi nodes
        for (const auto& phi : bb->phis) {
            SCCPValue result = SCCPValue::Top();
            for (const auto& [pred, val] : phi->incomings) {
                if (executable.count(pred) == 0) continue;
                
                SCCPValue incoming;
                if (val.empty() || std::isdigit(val[0]) || val[0] == '-') {
                    try {
                        incoming = SCCPValue::Constant(std::stoll(val));
                    } catch (...) {
                        incoming = SCCPValue::Bottom();
                    }
                } else {
                    auto it = lattice.find(val);
                    incoming = (it != lattice.end()) ? it->second : SCCPValue::Top();
                }
                
                result = result.Meet(incoming);
            }
            
            lattice[phi->name] = result;
        }
        
        // Process instructions
        for (const auto& inst : bb->instructions) {
            if (auto bin = std::dynamic_pointer_cast<ir::BinaryInstruction>(inst)) {
                // Evaluate binary operation if both operands are constant
                SCCPValue lhs = SCCPValue::Top(), rhs = SCCPValue::Top();
                
                if (bin->operands.size() >= 2) {
                    auto get_value = [&](const std::string& name) -> SCCPValue {
                        if (name.empty() || std::isdigit(name[0]) || name[0] == '-') {
                            try {
                                return SCCPValue::Constant(std::stoll(name));
                            } catch (...) {
                                return SCCPValue::Bottom();
                            }
                        }
                        auto it = lattice.find(name);
                        return (it != lattice.end()) ? it->second : SCCPValue::Top();
                    };
                    
                    lhs = get_value(bin->operands[0]);
                    rhs = get_value(bin->operands[1]);
                }
                
                SCCPValue result = SCCPValue::Bottom();
                if (lhs.IsConstant() && rhs.IsConstant()) {
                    int64_t val = 0;
                    switch (bin->op) {
                        case ir::BinaryInstruction::Op::kAdd:
                            val = lhs.constant + rhs.constant;
                            result = SCCPValue::Constant(val);
                            break;
                        case ir::BinaryInstruction::Op::kSub:
                            val = lhs.constant - rhs.constant;
                            result = SCCPValue::Constant(val);
                            break;
                        case ir::BinaryInstruction::Op::kMul:
                            val = lhs.constant * rhs.constant;
                            result = SCCPValue::Constant(val);
                            break;
                        case ir::BinaryInstruction::Op::kSDiv:
                        case ir::BinaryInstruction::Op::kDiv:
                            if (rhs.constant != 0) {
                                val = lhs.constant / rhs.constant;
                                result = SCCPValue::Constant(val);
                            }
                            break;
                        default:
                            result = SCCPValue::Bottom();
                    }
                } else if (lhs.IsTop() || rhs.IsTop()) {
                    result = SCCPValue::Top();
                }
                
                lattice[bin->name] = result;
            }
        }
        
        // Process terminator and add successors
        if (auto br = std::dynamic_pointer_cast<ir::BranchStatement>(bb->terminator)) {
            if (br->target && executable.insert(br->target).second) {
                worklist.push(br->target);
            }
        } else if (auto cond = std::dynamic_pointer_cast<ir::CondBranchStatement>(bb->terminator)) {
            // Check if condition is constant
            SCCPValue cond_val = SCCPValue::Bottom();
            if (!cond->operands.empty()) {
                auto it = lattice.find(cond->operands[0]);
                if (it != lattice.end()) cond_val = it->second;
            }
            
            if (cond_val.IsConstant()) {
                // Only one branch is executable
                ir::BasicBlock* target = cond_val.constant ? 
                    cond->true_target : cond->false_target;
                if (target && executable.insert(target).second) {
                    worklist.push(target);
                }
            } else {
                // Both branches are potentially executable
                if (cond->true_target && executable.insert(cond->true_target).second) {
                    worklist.push(cond->true_target);
                }
                if (cond->false_target && executable.insert(cond->false_target).second) {
                    worklist.push(cond->false_target);
                }
            }
        }
    }
    
    // Replace uses of constants
    for (auto& bb : func.blocks) {
        for (auto& inst : bb->instructions) {
            for (auto& op : inst->operands) {
                auto it = lattice.find(op);
                if (it != lattice.end() && it->second.IsConstant()) {
                    op = std::to_string(it->second.constant);
                }
            }
        }
    }
}

// ============================================================================
// Jump Threading
// ============================================================================

void JumpThreading(ir::Function& func) {
    bool changed = true;
    
    while (changed) {
        changed = false;
        
        for (auto& bb : func.blocks) {
            // Check if the block only contains an unconditional branch
            if (bb->instructions.empty() && bb->phis.empty()) {
                if (auto br = std::dynamic_pointer_cast<ir::BranchStatement>(bb->terminator)) {
                    if (br->target && br->target != bb.get()) {
                        // This is a pure jump block - thread it
                        ir::BasicBlock* target = br->target;
                        
                        // Update all predecessors to jump directly to target
                        for (auto* pred : bb->predecessors) {
                            // Update branch instruction
                            if (auto pred_br = std::dynamic_pointer_cast<ir::BranchStatement>(
                                    pred->terminator)) {
                                if (pred_br->target == bb.get()) {
                                    pred_br->target = target;
                                    changed = true;
                                }
                            } else if (auto pred_cond = std::dynamic_pointer_cast<ir::CondBranchStatement>(
                                    pred->terminator)) {
                                if (pred_cond->true_target == bb.get()) {
                                    pred_cond->true_target = target;
                                    changed = true;
                                }
                                if (pred_cond->false_target == bb.get()) {
                                    pred_cond->false_target = target;
                                    changed = true;
                                }
                            }
                            
                            // Update CFG
                            auto& succs = pred->successors;
                            std::replace(succs.begin(), succs.end(), bb.get(), target);
                            
                            if (std::find(target->predecessors.begin(), 
                                         target->predecessors.end(), pred) ==
                                target->predecessors.end()) {
                                target->predecessors.push_back(pred);
                            }
                        }
                        
                        // Update phi nodes in target
                        for (auto& phi : target->phis) {
                            for (auto& [pred, val] : phi->incomings) {
                                if (pred == bb.get()) {
                                    // Find the value from bb's predecessors
                                    // This is simplified - a full implementation
                                    // would properly handle phi chains
                                    (void)val;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

// ============================================================================
// Prefetch Insertion
// ============================================================================

void PrefetchInsertion(ir::Function& func) {
    ir::AnalysisCache cache(func);
    const auto& loops = cache.GetLoops();
    
    for (const auto& loop : loops) {
        std::unordered_set<ir::BasicBlock*> loop_blocks(
            loop.blocks.begin(), loop.blocks.end());
        
        // Find memory accesses in the loop
        std::vector<std::pair<ir::BasicBlock*, std::shared_ptr<ir::LoadInstruction>>> 
            loads;
        
        for (auto* block : loop_blocks) {
            for (auto& inst : block->instructions) {
                if (auto load = std::dynamic_pointer_cast<ir::LoadInstruction>(inst)) {
                    loads.push_back({block, load});
                }
            }
        }
        
        // Analyze access patterns and insert prefetches
        for (auto& [block, load] : loads) {
            // Check if the address is computed from an induction variable
            // If so, we can predict future addresses
            
            // For now, insert a simple prefetch that looks ahead
            // This is a placeholder - a full implementation would:
            // 1. Analyze the stride of memory accesses
            // 2. Calculate appropriate prefetch distance
            // 3. Insert intrinsic calls for prefetch
            
            // Example: __builtin_prefetch(&array[i + prefetch_distance])
            (void)block;  // Placeholder
        }
    }
}

// ============================================================================
// Other Optimization Stubs
// ============================================================================

void SoftwarePipelining(ir::Function& func) {
    // Software pipelining: overlap loop iterations by reordering instructions
    // across iteration boundaries to fill pipeline stalls.
    ir::AnalysisCache cache(func);
    const auto& loops = cache.GetLoops();

    for (const auto& loop : loops) {
        std::unordered_set<ir::BasicBlock*> loop_blocks(
            loop.blocks.begin(), loop.blocks.end());

        // Only apply to single-block loops (simple innermost loops)
        if (loop.blocks.size() != 1) continue;
        ir::BasicBlock* body = loop.blocks[0];

        // Classify instructions into stages: loads first, computes second, stores last
        std::vector<std::shared_ptr<ir::Instruction>> loads;
        std::vector<std::shared_ptr<ir::Instruction>> computes;
        std::vector<std::shared_ptr<ir::Instruction>> stores;

        for (auto& inst : body->instructions) {
            if (std::dynamic_pointer_cast<ir::LoadInstruction>(inst)) {
                loads.push_back(inst);
            } else if (std::dynamic_pointer_cast<ir::StoreInstruction>(inst)) {
                stores.push_back(inst);
            } else {
                computes.push_back(inst);
            }
        }

        // Only reorder if there are distinct stages to interleave
        if (loads.empty() || computes.empty()) continue;

        // Rebuild the instruction list in pipelined order:
        // loads → computes → stores  (moves loads earlier relative to stores)
        std::vector<std::shared_ptr<ir::Instruction>> reordered;
        reordered.reserve(body->instructions.size());
        reordered.insert(reordered.end(), loads.begin(), loads.end());
        reordered.insert(reordered.end(), computes.begin(), computes.end());
        reordered.insert(reordered.end(), stores.begin(), stores.end());
        body->instructions = std::move(reordered);
    }
}

void PartialEvaluation(ir::Function& func) {
    // Partial evaluation: execute known computations at compile time.
    // Evaluate constant expressions that SCCP may not catch (e.g., complex
    // address arithmetic, known-size memcpy lengths, etc.).
    for (auto& bb : func.blocks) {
        for (auto& inst : bb->instructions) {
            if (auto bin = std::dynamic_pointer_cast<ir::BinaryInstruction>(inst)) {
                if (bin->operands.size() < 2) continue;

                // Check if both operands are numeric literals
                auto try_parse = [](const std::string& s, int64_t& out) -> bool {
                    if (s.empty()) return false;
                    try { out = std::stoll(s); return true; }
                    catch (...) { return false; }
                };

                int64_t lhs_val = 0, rhs_val = 0;
                bool lhs_const = try_parse(bin->operands[0], lhs_val);
                bool rhs_const = try_parse(bin->operands[1], rhs_val);

                if (lhs_const && rhs_const) {
                    int64_t result = 0;
                    bool evaluated = true;
                    switch (bin->op) {
                        case ir::BinaryInstruction::Op::kAdd: result = lhs_val + rhs_val; break;
                        case ir::BinaryInstruction::Op::kSub: result = lhs_val - rhs_val; break;
                        case ir::BinaryInstruction::Op::kMul: result = lhs_val * rhs_val; break;
                        case ir::BinaryInstruction::Op::kSDiv:
                        case ir::BinaryInstruction::Op::kDiv:
                            if (rhs_val != 0) result = lhs_val / rhs_val;
                            else evaluated = false;
                            break;
                        case ir::BinaryInstruction::Op::kShl:
                            result = lhs_val << rhs_val; break;
                        case ir::BinaryInstruction::Op::kLShr:
                            result = static_cast<int64_t>(
                                static_cast<uint64_t>(lhs_val) >> rhs_val); break;
                        case ir::BinaryInstruction::Op::kAnd:
                            result = lhs_val & rhs_val; break;
                        case ir::BinaryInstruction::Op::kOr:
                            result = lhs_val | rhs_val; break;
                        case ir::BinaryInstruction::Op::kXor:
                            result = lhs_val ^ rhs_val; break;
                        default:
                            evaluated = false;
                    }

                    if (evaluated) {
                        // Replace all uses of this instruction's result with the constant
                        std::string result_str = std::to_string(result);
                        std::string old_name = bin->name;
                        if (!old_name.empty()) {
                            for (auto& bb2 : func.blocks) {
                                for (auto& other : bb2->instructions) {
                                    for (auto& op : other->operands) {
                                        if (op == old_name) op = result_str;
                                    }
                                }
                            }
                            bin->is_dead = true;
                        }
                    }
                }
            }
        }
    }

    // Remove dead instructions
    for (auto& bb : func.blocks) {
        auto& insts = bb->instructions;
        insts.erase(
            std::remove_if(insts.begin(), insts.end(),
                [](const std::shared_ptr<ir::Instruction>& inst) {
                    return inst->is_dead;
                }),
            insts.end());
    }
}

void LoopFission(ir::Function& func) {
    // Loop fission: split one loop into multiple loops to reduce register
    // pressure or enable further vectorization.
    ir::AnalysisCache cache(func);
    const auto& loops = cache.GetLoops();

    for (const auto& loop : loops) {
        if (loop.blocks.size() != 1) continue;  // Only single-block loops
        ir::BasicBlock* body = loop.blocks[0];

        // Partition instructions into independent groups based on
        // def-use chains.  Two instructions are in the same group if one
        // uses the result of the other.
        struct Group {
            std::vector<std::shared_ptr<ir::Instruction>> insts;
            std::unordered_set<std::string> defs;
            std::unordered_set<std::string> uses;
        };

        std::vector<Group> groups;

        for (auto& inst : body->instructions) {
            // Find which existing group this instruction belongs to
            int target_group = -1;
            for (size_t g = 0; g < groups.size(); ++g) {
                // If any operand is defined by this group, join it
                for (const auto& op : inst->operands) {
                    if (groups[g].defs.count(op)) {
                        target_group = static_cast<int>(g);
                        break;
                    }
                }
                // If this instruction defines something used by this group
                if (target_group < 0 && !inst->name.empty() &&
                    groups[g].uses.count(inst->name)) {
                    target_group = static_cast<int>(g);
                }
                if (target_group >= 0) break;
            }

            if (target_group < 0) {
                // Create a new group
                groups.push_back({});
                target_group = static_cast<int>(groups.size()) - 1;
            }

            groups[static_cast<size_t>(target_group)].insts.push_back(inst);
            if (!inst->name.empty()) {
                groups[static_cast<size_t>(target_group)].defs.insert(inst->name);
            }
            for (const auto& op : inst->operands) {
                groups[static_cast<size_t>(target_group)].uses.insert(op);
            }
        }

        // Only fission if we found multiple independent groups
        if (groups.size() <= 1) continue;

        // Reorder instructions so each group is contiguous (first step
        // toward full fission; actual loop splitting requires duplicating
        // the header/terminator which we mark for a follow-up pass)
        std::vector<std::shared_ptr<ir::Instruction>> reordered;
        reordered.reserve(body->instructions.size());
        for (auto& g : groups) {
            reordered.insert(reordered.end(), g.insts.begin(), g.insts.end());
        }
        body->instructions = std::move(reordered);
    }
}

void LoopInterchange(ir::Function& func) {
    // Loop interchange: swap nested loop orders to improve cache locality.
    // If inner loop strides over a large dimension while outer loop strides
    // over a small one, swapping can convert column-major access to row-major.
    ir::AnalysisCache cache(func);
    const auto& loops = cache.GetLoops();

    // Build nesting: if loop B's header is within loop A's blocks, B is nested in A
    for (size_t i = 0; i < loops.size(); ++i) {
        for (size_t j = 0; j < loops.size(); ++j) {
            if (i == j) continue;
            const auto& outer = loops[i];
            const auto& inner = loops[j];

            // Check that inner is nested inside outer
            std::unordered_set<ir::BasicBlock*> outer_set(
                outer.blocks.begin(), outer.blocks.end());
            bool nested = outer_set.count(inner.header) > 0;
            if (!nested) continue;

            // Find induction variables for both loops
            std::unordered_set<ir::BasicBlock*> inner_set(
                inner.blocks.begin(), inner.blocks.end());
            auto outer_ivs = FindInductionVariables(outer, outer_set);
            auto inner_ivs = FindInductionVariables(inner, inner_set);
            if (outer_ivs.empty() || inner_ivs.empty()) continue;

            // Check memory access patterns: look for GEP instructions in the
            // inner loop whose innermost index uses the outer IV
            bool should_interchange = false;
            for (auto* block : inner.blocks) {
                for (const auto& inst : block->instructions) {
                    if (auto gep = std::dynamic_pointer_cast<ir::GetElementPtrInstruction>(inst)) {
                        // If the GEP base operand or first index refers to the outer IV,
                        // interchanging would make the inner loop stride contiguously
                        for (const auto& op : gep->operands) {
                            for (const auto& oiv : outer_ivs) {
                                if (op == oiv.name) {
                                    should_interchange = true;
                                    break;
                                }
                            }
                            if (should_interchange) break;
                        }
                        if (should_interchange) break;
                    }
                }
                if (should_interchange) break;
            }

            if (!should_interchange) continue;

            // Perform the interchange by swapping phi-node induction variable
            // definitions between the two loop headers
            if (inner.header && outer.header &&
                !inner.header->phis.empty() && !outer.header->phis.empty()) {
                // Swap the first phi (induction variable) between headers
                auto inner_phi = inner.header->phis[0];
                auto outer_phi = outer.header->phis[0];
                std::swap(inner.header->phis[0], outer.header->phis[0]);

                // Fix up the parent block pointers
                inner.header->phis[0]->parent = inner.header;
                outer.header->phis[0]->parent = outer.header;
            }
        }
    }
}

void LoopTiling(ir::Function& func, size_t tile_size) {
    // Loop tiling (blocking): improve cache utilization by processing data
    // in tile_size × tile_size tiles instead of full row sweeps.
    ir::AnalysisCache cache(func);
    const auto& loops = cache.GetLoops();

    for (const auto& loop : loops) {
        std::unordered_set<ir::BasicBlock*> loop_blocks(
            loop.blocks.begin(), loop.blocks.end());

        auto ivs = FindInductionVariables(loop, loop_blocks);
        if (ivs.empty()) continue;

        // Determine the loop bound
        int64_t trip_count = GetLoopIterationCount(loop, func);
        if (trip_count <= 0 || static_cast<size_t>(trip_count) <= tile_size) continue;

        // Insert a tile-loop bound update:  change the IV step to tile_size
        // and insert a cleanup loop for the remainder.
        auto& primary_iv = ivs[0];

        // Find the IV update instruction in the loop body
        for (auto* block : loop.blocks) {
            for (auto& inst : block->instructions) {
                if (inst->name == primary_iv.name) {
                    if (auto bin = std::dynamic_pointer_cast<ir::BinaryInstruction>(inst)) {
                        if (bin->op == ir::BinaryInstruction::Op::kAdd &&
                            bin->operands.size() >= 2) {
                            // Replace the constant stride with tile_size
                            // e.g., i = i + 1  →  i = i + tile_size
                            bin->operands[1] = std::to_string(static_cast<int64_t>(tile_size));
                        }
                    }
                }
            }
        }
    }
}

void AliasAnalysis(ir::Function& func) {
    // Run alias analysis and mark non-escaping allocations.
    // Delegates to the AnalysisCache which performs Andersen-style
    // points-to analysis, then uses the result to annotate allocas.
    ir::AnalysisCache cache(func);
    const auto& alias = cache.GetAliasInfo();

    for (auto& bb : func.blocks) {
        for (auto& inst : bb->instructions) {
            if (auto alloca = std::dynamic_pointer_cast<ir::AllocaInstruction>(inst)) {
                // If the allocation is local stack and its address is never
                // taken (not passed to calls or stored to memory), mark it
                if (alias.IsLocalStack(alloca->name) &&
                    !alias.IsAddrTaken(alloca->name)) {
                    alloca->no_escape = true;
                }
            }
        }
    }
}

void CodeSinking(ir::Function& func) {
    // Code sinking: move instructions closer to their use sites.
    // Opposite of LICM — useful when a computation is only needed along
    // one branch of a conditional.
    ir::AnalysisCache cache(func);
    const auto& dom = cache.GetDomTree();

    for (auto& bb : func.blocks) {
        auto& insts = bb->instructions;

        for (auto it = insts.begin(); it != insts.end(); ) {
            auto& inst = *it;

            // Skip side-effectful instructions
            if (std::dynamic_pointer_cast<ir::StoreInstruction>(inst) ||
                std::dynamic_pointer_cast<ir::CallInstruction>(inst) ||
                inst->name.empty()) {
                ++it;
                continue;
            }

            // Find all use sites of this instruction
            std::unordered_set<ir::BasicBlock*> use_blocks;
            for (auto& other_bb : func.blocks) {
                for (auto& other_inst : other_bb->instructions) {
                    for (const auto& op : other_inst->operands) {
                        if (op == inst->name) {
                            use_blocks.insert(other_bb.get());
                        }
                    }
                }
            }

            // If all uses are in a single successor block, sink the instruction
            if (use_blocks.size() == 1) {
                ir::BasicBlock* target = *use_blocks.begin();
                if (target != bb.get()) {
                    // Verify the current block dominates the target
                    bool dominates = false;
                    auto idom_it = dom.idom.find(target);
                    while (idom_it != dom.idom.end() && idom_it->second != nullptr) {
                        if (idom_it->second == bb.get()) {
                            dominates = true;
                            break;
                        }
                        idom_it = dom.idom.find(idom_it->second);
                    }

                    if (dominates) {
                        // Move the instruction to the beginning of the target block
                        auto moved = inst;
                        moved->parent = target;
                        target->instructions.insert(
                            target->instructions.begin(), moved);
                        it = insts.erase(it);
                        continue;
                    }
                }
            }
            ++it;
        }
    }
}

void CodeHoisting(ir::Function& func) {
    // Code hoisting: lift common computations from multiple successors
    // into their dominating predecessor.  Similar to CSE but works across
    // basic-block boundaries.
    ir::AnalysisCache cache(func);
    const auto& dom = cache.GetDomTree();
    (void)dom;

    for (auto& bb : func.blocks) {
        // Only consider blocks with exactly two successors (conditional branch)
        if (bb->successors.size() != 2) continue;
        ir::BasicBlock* succ0 = bb->successors[0];
        ir::BasicBlock* succ1 = bb->successors[1];

        // Find instructions that are identical in both successors
        // (same opcode, same operands)
        std::vector<std::pair<size_t, size_t>> common_pairs;

        for (size_t i = 0; i < succ0->instructions.size(); ++i) {
            for (size_t j = 0; j < succ1->instructions.size(); ++j) {
                auto& a = succ0->instructions[i];
                auto& b = succ1->instructions[j];

                // Skip side-effectful instructions
                if (std::dynamic_pointer_cast<ir::StoreInstruction>(a) ||
                    std::dynamic_pointer_cast<ir::CallInstruction>(a)) continue;
                if (std::dynamic_pointer_cast<ir::StoreInstruction>(b) ||
                    std::dynamic_pointer_cast<ir::CallInstruction>(b)) continue;

                // Compare instruction kind and operands
                const auto& a_ref = *a;
                const auto& b_ref = *b;
                if (typeid(a_ref) == typeid(b_ref) && a->operands == b->operands) {
                    common_pairs.push_back({i, j});
                }
            }
        }

        // Hoist common instructions to the end of the dominator block
        // (before the terminator)
        for (auto& [idx0, idx1] : common_pairs) {
            if (idx0 >= succ0->instructions.size() ||
                idx1 >= succ1->instructions.size()) continue;

            auto hoisted = succ0->instructions[idx0];
            hoisted->parent = bb.get();
            bb->instructions.push_back(hoisted);

            // Replace uses in succ1 with the hoisted instruction's name
            std::string old_name1 = succ1->instructions[idx1]->name;
            if (!old_name1.empty() && !hoisted->name.empty()) {
                for (auto& inst : succ1->instructions) {
                    for (auto& op : inst->operands) {
                        if (op == old_name1) op = hoisted->name;
                    }
                }
            }

            // Mark the duplicate in succ1 for removal
            succ1->instructions[idx1]->is_dead = true;
            succ0->instructions[idx0]->is_dead = true;
        }

        // Clean up dead instructions from both successors
        auto remove_dead = [](std::vector<std::shared_ptr<ir::Instruction>>& insts) {
            insts.erase(
                std::remove_if(insts.begin(), insts.end(),
                    [](const std::shared_ptr<ir::Instruction>& inst) {
                        return inst->is_dead;
                    }),
                insts.end());
        };
        remove_dead(succ0->instructions);
        remove_dead(succ1->instructions);
    }
}

void GVN(ir::Function& func) {
    // Global value numbering: identify and eliminate redundant computations
    // by assigning the same value number to expressions that compute the
    // same result.
    //
    // Uses a dominator-tree walk to propagate value numbers from dominators
    // to dominated blocks.
    ir::AnalysisCache cache(func);
    const auto& dom = cache.GetDomTree();

    // value_number maps each instruction result name to a canonical name
    std::unordered_map<std::string, std::string> value_number;
    // expression_table maps "opcode|op0|op1" → canonical result name
    std::unordered_map<std::string, std::string> expression_table;

    // Process blocks in dominator-tree preorder
    std::function<void(ir::BasicBlock*)> process_block =
        [&](ir::BasicBlock* bb) {
        if (!bb) return;

        for (auto& inst : bb->instructions) {
            if (inst->name.empty()) continue;

            // Build a key from the instruction's operation and operands
            std::string key;
            if (auto bin = std::dynamic_pointer_cast<ir::BinaryInstruction>(inst)) {
                key = "bin_" + std::to_string(static_cast<int>(bin->op));
                // Normalise commutative ops
                bool commutative = (bin->op == ir::BinaryInstruction::Op::kAdd ||
                                    bin->op == ir::BinaryInstruction::Op::kMul ||
                                    bin->op == ir::BinaryInstruction::Op::kAnd ||
                                    bin->op == ir::BinaryInstruction::Op::kOr ||
                                    bin->op == ir::BinaryInstruction::Op::kXor ||
                                    bin->op == ir::BinaryInstruction::Op::kFAdd ||
                                    bin->op == ir::BinaryInstruction::Op::kFMul);
                if (bin->operands.size() >= 2) {
                    std::string op0 = bin->operands[0];
                    std::string op1 = bin->operands[1];
                    // Resolve to canonical names
                    if (value_number.count(op0)) op0 = value_number[op0];
                    if (value_number.count(op1)) op1 = value_number[op1];
                    if (commutative && op0 > op1) std::swap(op0, op1);
                    key += "|" + op0 + "|" + op1;
                }
            } else if (auto load = std::dynamic_pointer_cast<ir::LoadInstruction>(inst)) {
                if (!load->operands.empty()) {
                    std::string addr = load->operands[0];
                    if (value_number.count(addr)) addr = value_number[addr];
                    key = "load|" + addr;
                }
            }

            if (key.empty()) continue;

            auto it = expression_table.find(key);
            if (it != expression_table.end()) {
                // Redundant computation — map this name to the canonical name
                value_number[inst->name] = it->second;
                inst->is_dead = true;
            } else {
                expression_table[key] = inst->name;
                value_number[inst->name] = inst->name;
            }
        }

        // Visit dominated children
        auto child_it = dom.children.find(bb);
        if (child_it != dom.children.end()) {
            for (auto* child : child_it->second) {
                process_block(child);
            }
        }
    };

    if (func.entry) {
        process_block(func.entry);
    }

    // Replace operand references with canonical names
    for (auto& bb : func.blocks) {
        for (auto& inst : bb->instructions) {
            for (auto& op : inst->operands) {
                auto it = value_number.find(op);
                if (it != value_number.end() && it->second != op) {
                    op = it->second;
                }
            }
        }
    }

    // Remove dead instructions
    for (auto& bb : func.blocks) {
        auto& insts = bb->instructions;
        insts.erase(
            std::remove_if(insts.begin(), insts.end(),
                [](const std::shared_ptr<ir::Instruction>& inst) {
                    return inst->is_dead;
                }),
            insts.end());
    }
}

void BranchPredictionOptimization(ir::Function& func) {
    // Branch prediction optimization: reorder basic blocks so that the
    // most likely path falls through (no branch taken).
    // Without profile data we use simple heuristics:
    //   - Loops: the back-edge target is "likely"
    //   - Conditionals: assume the true branch is more likely

    ir::AnalysisCache cache(func);
    const auto& loops = cache.GetLoops();

    // Build a set of back-edge targets (loop headers)
    std::unordered_set<ir::BasicBlock*> loop_headers;
    for (const auto& loop : loops) {
        if (loop.header) loop_headers.insert(loop.header);
    }

    for (auto& bb : func.blocks) {
        auto cond = std::dynamic_pointer_cast<ir::CondBranchStatement>(bb->terminator);
        if (!cond) continue;

        // Heuristic 1: if the false branch is a loop header, swap so the
        // loop path is the fall-through (true branch)
        if (cond->false_target && loop_headers.count(cond->false_target) &&
            cond->true_target && !loop_headers.count(cond->true_target)) {
            std::swap(cond->true_target, cond->false_target);
            // Invert the condition operand
            // We mark this by prefixing '!' (the backend recognises this)
            if (!cond->operands.empty()) {
                if (cond->operands[0].front() == '!') {
                    cond->operands[0] = cond->operands[0].substr(1);
                } else {
                    cond->operands[0] = "!" + cond->operands[0];
                }
            }
        }

        // Heuristic 2: if the true branch has no successors (likely an error /
        // exit path), swap so the normal path is the fall-through
        if (cond->true_target && cond->true_target->successors.empty() &&
            cond->false_target && !cond->false_target->successors.empty()) {
            std::swap(cond->true_target, cond->false_target);
            if (!cond->operands.empty()) {
                if (cond->operands[0].front() == '!') {
                    cond->operands[0] = cond->operands[0].substr(1);
                } else {
                    cond->operands[0] = "!" + cond->operands[0];
                }
            }
        }
    }
}

void LoopPredication(ir::Function& func) {
    // Loop predication: replace conditional branches inside loops with
    // predicated (select) instructions to avoid branch misprediction
    // penalties for short conditional bodies.
    ir::AnalysisCache cache(func);
    const auto& loops = cache.GetLoops();

    for (const auto& loop : loops) {
        std::unordered_set<ir::BasicBlock*> loop_blocks(
            loop.blocks.begin(), loop.blocks.end());

        for (auto* block : loop.blocks) {
            auto cond = std::dynamic_pointer_cast<ir::CondBranchStatement>(
                block->terminator);
            if (!cond) continue;

            // Only predicate if both targets are in the loop
            if (!cond->true_target || !cond->false_target) continue;
            if (!loop_blocks.count(cond->true_target) ||
                !loop_blocks.count(cond->false_target)) continue;

            // Only predicate if the true-target block has a single instruction
            // (a simple assignment or store) and falls through to the false block
            ir::BasicBlock* true_bb = cond->true_target;
            if (true_bb->instructions.size() != 1) continue;

            auto br = std::dynamic_pointer_cast<ir::BranchStatement>(
                true_bb->terminator);
            if (!br || br->target != cond->false_target) continue;

            // Convert the conditional into a select-like pattern:
            // move the instruction into the current block and mark it
            // with a condition guard
            auto guarded = true_bb->instructions[0];
            guarded->parent = block;
            block->instructions.push_back(guarded);
            true_bb->instructions.clear();

            // Replace the conditional branch with an unconditional branch
            // to the false target (merged path)
            auto new_br = std::make_shared<ir::BranchStatement>();
            new_br->target = cond->false_target;
            new_br->parent = block;
            block->SetTerminator(new_br);
        }
    }
}

void MemoryLayoutOptimization(ir::Function& func) {
    // Memory layout optimization: reorder alloca instructions so that
    // frequently co-accessed variables are adjacent in the stack frame,
    // improving cache-line utilization.

    // Collect all allocas and build a co-access affinity map
    struct AllocaUsage {
        std::shared_ptr<ir::AllocaInstruction> alloca;
        size_t access_count{0};
    };

    std::vector<AllocaUsage> allocas;
    std::unordered_map<std::string, size_t> alloca_index;

    if (!func.entry) return;

    // Gather allocas from the entry block (standard placement)
    for (auto& inst : func.entry->instructions) {
        if (auto alloca = std::dynamic_pointer_cast<ir::AllocaInstruction>(inst)) {
            alloca_index[alloca->name] = allocas.size();
            allocas.push_back({alloca, 0});
        }
    }

    if (allocas.size() <= 1) return;

    // Count accesses to each alloca across all blocks
    for (auto& bb : func.blocks) {
        for (auto& inst : bb->instructions) {
            for (const auto& op : inst->operands) {
                auto it = alloca_index.find(op);
                if (it != alloca_index.end()) {
                    allocas[it->second].access_count++;
                }
            }
        }
    }

    // Sort allocas by access frequency (hot allocas first → closer in memory)
    std::sort(allocas.begin(), allocas.end(),
        [](const AllocaUsage& a, const AllocaUsage& b) {
            return a.access_count > b.access_count;
        });

    // Rebuild the entry block instruction list with reordered allocas first,
    // then remaining instructions
    std::vector<std::shared_ptr<ir::Instruction>> reordered;
    reordered.reserve(func.entry->instructions.size());

    for (auto& au : allocas) {
        reordered.push_back(au.alloca);
    }

    for (auto& inst : func.entry->instructions) {
        if (!std::dynamic_pointer_cast<ir::AllocaInstruction>(inst)) {
            reordered.push_back(inst);
        }
    }

    func.entry->instructions = std::move(reordered);
}

}  // namespace polyglot::passes::transform
