/**
 * Loop Optimization Passes Implementation
 *
 * This file contains implementations of various loop optimization passes:
 * - Loop Analysis: Detect and analyze loop structures
 * - Loop Unrolling: Replicate loop body to reduce overhead
 * - LICM (Loop-Invariant Code Motion): Hoist invariant computations
 * - Loop Fusion: Merge adjacent loops with the same bounds
 * - Loop Strength Reduction: Replace expensive operations with cheaper ones
 */

#include "middle/include/passes/transform/loop_optimization.h"

#include <algorithm>
#include <queue>
#include <stack>
#include <unordered_set>

#include "middle/include/ir/analysis.h"
#include "middle/include/ir/nodes/expressions.h"
#include "middle/include/ir/nodes/statements.h"

namespace polyglot::passes::transform {

using namespace polyglot::ir;

// ============================================================================
// Helper Functions
// ============================================================================

namespace {

/**
 * Check if a value is defined within the loop
 */
bool IsDefinedInLoop(const std::string& name, const LoopInfo* loop) {
    if (name.empty()) return false;
    
    for (auto* bb : loop->body) {
        // Check phi nodes
        for (const auto& phi : bb->phis) {
            if (phi->name == name) return true;
        }
        // Check instructions
        for (const auto& inst : bb->instructions) {
            if (inst->HasResult() && inst->name == name) return true;
        }
    }
    return false;
}

/**
 * Check if an instruction may have side effects
 */
bool MayHaveSideEffects(const ir::Instruction* inst) {
    if (dynamic_cast<const ir::StoreInstruction*>(inst)) return true;
    if (dynamic_cast<const ir::CallInstruction*>(inst)) return true;
    return false;
}

/**
 * Find the induction variable for a loop
 * Returns the phi node representing the IV, or nullptr if not found
 */
ir::PhiInstruction* FindInductionVariable(LoopInfo* loop) {
    if (!loop->header) return nullptr;
    
    for (auto& phi : loop->header->phis) {
        // Check if this phi is updated in the loop body
        bool has_loop_update = false;
        std::string update_var;
        
        for (const auto& [pred, val] : phi->incomings) {
            if (loop->body.count(pred)) {
                has_loop_update = true;
                update_var = val;
                break;
            }
        }
        
        if (!has_loop_update) continue;
        
        // Check if the update is i = i + c or i = i - c
        for (auto* bb : loop->body) {
            for (const auto& inst : bb->instructions) {
                if (inst->name == update_var) {
                    if (auto bin = std::dynamic_pointer_cast<ir::BinaryInstruction>(inst)) {
                        if (bin->op == ir::BinaryInstruction::Op::kAdd ||
                            bin->op == ir::BinaryInstruction::Op::kSub) {
                            // Check if one operand is the phi
                            if (bin->operands.size() >= 2 &&
                                (bin->operands[0] == phi->name || 
                                 bin->operands[1] == phi->name)) {
                                return phi.get();
                            }
                        }
                    }
                }
            }
        }
    }
    
    return nullptr;
}

/**
 * Get the loop trip count if it can be determined statically
 * Returns -1 if the trip count cannot be determined
 */
int64_t GetTripCount(const LoopInfo* loop, const ir::Function& func) {
    if (!loop->header) return -1;
    
    // Look for comparison instruction in header
    for (const auto& inst : loop->header->instructions) {
        if (auto cmp = std::dynamic_pointer_cast<ir::BinaryInstruction>(inst)) {
            if (cmp->op == ir::BinaryInstruction::Op::kCmpSlt ||
                cmp->op == ir::BinaryInstruction::Op::kCmpSle ||
                cmp->op == ir::BinaryInstruction::Op::kCmpUlt ||
                cmp->op == ir::BinaryInstruction::Op::kCmpUle) {
                // Try to extract the bound
                if (cmp->operands.size() >= 2) {
                    try {
                        return std::stoll(cmp->operands[1]);
                    } catch (...) {
                        return -1;
                    }
                }
            }
        }
    }
    
    (void)func;
    return -1;
}

/**
 * Clone an instruction with renamed values
 */
std::shared_ptr<ir::Instruction> CloneInstruction(
    const std::shared_ptr<ir::Instruction>& inst,
    int iteration,
    std::unordered_map<std::string, std::string>& rename_map) {
    
    auto suffix = "_unroll" + std::to_string(iteration);
    
    // Helper to rename operands
    auto rename = [&](const std::string& name) -> std::string {
        auto it = rename_map.find(name);
        return it != rename_map.end() ? it->second : name;
    };
    
    // Clone based on instruction type
    if (auto bin = std::dynamic_pointer_cast<ir::BinaryInstruction>(inst)) {
        auto clone = std::make_shared<ir::BinaryInstruction>();
        clone->op = bin->op;
        clone->type = bin->type;
        clone->name = bin->name + suffix;
        rename_map[bin->name] = clone->name;
        for (const auto& op : bin->operands) {
            clone->operands.push_back(rename(op));
        }
        return clone;
    }
    
    if (auto load = std::dynamic_pointer_cast<ir::LoadInstruction>(inst)) {
        auto clone = std::make_shared<ir::LoadInstruction>();
        clone->type = load->type;
        clone->align = load->align;
        clone->name = load->name + suffix;
        rename_map[load->name] = clone->name;
        for (const auto& op : load->operands) {
            clone->operands.push_back(rename(op));
        }
        return clone;
    }
    
    if (auto store = std::dynamic_pointer_cast<ir::StoreInstruction>(inst)) {
        auto clone = std::make_shared<ir::StoreInstruction>();
        clone->type = store->type;
        clone->align = store->align;
        for (const auto& op : store->operands) {
            clone->operands.push_back(rename(op));
        }
        return clone;
    }
    
    if (auto gep = std::dynamic_pointer_cast<ir::GetElementPtrInstruction>(inst)) {
        auto clone = std::make_shared<ir::GetElementPtrInstruction>();
        clone->type = gep->type;
        clone->source_type = gep->source_type;
        clone->indices = gep->indices;
        clone->inbounds = gep->inbounds;
        clone->name = gep->name + suffix;
        rename_map[gep->name] = clone->name;
        for (const auto& op : gep->operands) {
            clone->operands.push_back(rename(op));
        }
        return clone;
    }
    
    if (auto assign = std::dynamic_pointer_cast<ir::AssignInstruction>(inst)) {
        auto clone = std::make_shared<ir::AssignInstruction>();
        clone->type = assign->type;
        clone->name = assign->name + suffix;
        rename_map[assign->name] = clone->name;
        for (const auto& op : assign->operands) {
            clone->operands.push_back(rename(op));
        }
        return clone;
    }
    
    return nullptr;
}

}  // namespace

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
            block_to_loop_[bb] = loop.get();
        }
        loop->depth = 1;
        loop->preheader = nullptr;
        
        // Find preheader (single predecessor outside the loop)
        for (auto* pred : loop->header->predecessors) {
            if (loop->body.count(pred) == 0) {
                if (!loop->preheader) {
                    loop->preheader = pred;
                } else {
                    // Multiple external predecessors - no single preheader
                    loop->preheader = nullptr;
                    break;
                }
            }
        }
        
        // Find latches (blocks with back edges to header)
        for (auto* bb : loop->body) {
            for (auto* succ : bb->successors) {
                if (succ == loop->header) {
                    loop->latches.push_back(bb);
                    break;
                }
            }
        }
        
        // Find exit blocks (blocks that have successors outside the loop)
        for (auto* bb : loop->body) {
            for (auto* succ : bb->successors) {
                if (loop->body.count(succ) == 0) {
                    loop->exits.push_back(bb);
                    break;
                }
            }
        }
        
        loops_.push_back(std::move(loop));
    }
    
    // Compute nesting
    ComputeNesting();
}

void LoopAnalysis::FindBackEdges() {
    // Back edges are found during dominator tree computation
    // A back edge is an edge from a dominated block to its dominator
    AnalysisCache cache(func_);
    const auto& dom_tree = cache.GetDomTree();
    
    for (auto& bb : func_.blocks) {
        for (auto* succ : bb->successors) {
            // Check if succ dominates bb using idom map
            auto it = dom_tree.idom.find(bb.get());
            if (it != dom_tree.idom.end()) {
                ir::BasicBlock* dom = it->second;
                while (dom) {
                    if (dom == succ) {
                        // succ dominates bb, so bb -> succ is a back edge
                        back_edges_.push_back({bb.get(), succ});
                        break;
                    }
                    auto dom_it = dom_tree.idom.find(dom);
                    dom = (dom_it != dom_tree.idom.end()) ? dom_it->second : nullptr;
                }
            }
        }
    }
}

void LoopAnalysis::ConstructLoop(ir::BasicBlock* header, ir::BasicBlock* latch) {
    // Find all blocks in the natural loop defined by the back edge
    auto loop = std::make_unique<LoopInfo>();
    loop->header = header;
    loop->body.insert(header);
    
    std::stack<ir::BasicBlock*> worklist;
    worklist.push(latch);
    
    while (!worklist.empty()) {
        ir::BasicBlock* bb = worklist.top();
        worklist.pop();
        
        if (loop->body.insert(bb).second) {
            // First time seeing this block
            for (auto* pred : bb->predecessors) {
                worklist.push(pred);
            }
        }
    }
    
    loops_.push_back(std::move(loop));
}

void LoopAnalysis::ComputeNesting() {
    // Compute loop nesting by checking containment
    for (auto& loop : loops_) {
        loop->depth = 1;
        for (auto& other : loops_) {
            if (loop.get() == other.get()) continue;
            
            // Check if loop is contained in other
            bool contained = true;
            for (auto* bb : loop->body) {
                if (other->body.count(bb) == 0) {
                    contained = false;
                    break;
                }
            }
            
            if (contained && loop->body.size() < other->body.size()) {
                // loop is nested in other
                loop->depth++;
                loop->parent = other.get();
                other->nested_loops.push_back(loop.get());
            }
        }
    }
}

LoopInfo* LoopAnalysis::GetLoopForBlock(ir::BasicBlock* bb) const {
    auto it = block_to_loop_.find(bb);
    return it != block_to_loop_.end() ? it->second : nullptr;
}

bool LoopAnalysis::IsInLoop(ir::BasicBlock* bb) const {
    return block_to_loop_.count(bb) > 0;
}

// ============================================================================
// Loop Unrolling Pass Implementation
// ============================================================================

LoopUnrollingPass::LoopUnrollingPass(ir::Function& func, int unroll_factor)
    : func_(func), unroll_factor_(unroll_factor) {}

bool LoopUnrollingPass::Run() {
    LoopAnalysis analysis(func_);
    bool changed = false;
    
    for (const auto& loop : analysis.GetLoops()) {
        // Check if loop can be unrolled
        if (!CanUnroll(loop.get())) continue;
        
        // Unroll the loop
        UnrollLoop(loop.get());
        changed = true;
    }
    
    return changed;
}

bool LoopUnrollingPass::CanUnroll(const LoopInfo* loop) const {
    // Check conditions for unrolling
    
    // 1. Must have a single latch
    if (loop->latches.size() != 1) return false;
    
    // 2. Must have a single exit
    if (loop->exits.size() != 1) return false;
    
    // 3. Must have a preheader
    if (!loop->preheader) return false;
    
    // 4. Must not have nested loops (for simplicity)
    if (!loop->nested_loops.empty()) return false;
    
    // 5. Trip count should be known or large enough
    int64_t trip_count = GetTripCount(loop, func_);
    if (trip_count > 0 && trip_count < unroll_factor_) {
        // Trip count too small for the unroll factor
        return false;
    }
    
    return true;
}

void LoopUnrollingPass::UnrollLoop(LoopInfo* loop) {
    // Collect instructions to clone (excluding IV update and comparison)
    std::vector<std::shared_ptr<ir::Instruction>> body_insts;
    ir::PhiInstruction* iv = FindInductionVariable(loop);
    
    for (auto* bb : loop->body) {
        for (auto& inst : bb->instructions) {
            // Skip IV-related instructions in header
            if (bb == loop->header && iv) {
                // Skip IV update
                bool is_iv_related = false;
                for (const auto& [pred, val] : iv->incomings) {
                    if (val == inst->name) {
                        is_iv_related = true;
                        break;
                    }
                }
                if (is_iv_related) continue;
            }
            body_insts.push_back(inst);
        }
    }
    
    // Clone body (unroll_factor_ - 1) times
    for (int i = 1; i < unroll_factor_; ++i) {
        std::unordered_map<std::string, std::string> rename_map;
        
        for (const auto& inst : body_insts) {
            auto clone = CloneInstruction(inst, i, rename_map);
            if (clone) {
                // Insert clone at the end of the latch block
                if (!loop->latches.empty()) {
                    loop->latches[0]->instructions.push_back(clone);
                }
            }
        }
        
        // Update IV: i = i + step
        if (iv) {
            // Find the step value
            std::string step_val;
            for (auto* bb : loop->body) {
                for (auto& inst : bb->instructions) {
                    if (auto bin = std::dynamic_pointer_cast<ir::BinaryInstruction>(inst)) {
                        if (bin->operands.size() >= 2 && bin->operands[0] == iv->name) {
                            step_val = bin->operands[1];
                            break;
                        }
                    }
                }
            }
            
            if (!step_val.empty()) {
                // Create IV increment: iv = iv + step
                auto incr = std::make_shared<ir::BinaryInstruction>();
                incr->op = ir::BinaryInstruction::Op::kAdd;
                incr->type = iv->type;
                incr->name = iv->name + "_inc" + std::to_string(i);
                incr->operands.push_back(i == 1 ? iv->name : iv->name + "_inc" + std::to_string(i-1));
                incr->operands.push_back(step_val);
                
                if (!loop->latches.empty()) {
                    loop->latches[0]->instructions.push_back(incr);
                }
            }
        }
    }
    
    // Update loop bound to account for unrolling
    // new_bound = old_bound / unroll_factor
    UpdateLoopBound(loop);
}

void LoopUnrollingPass::UpdateLoopBound(LoopInfo* loop) {
    // Find and update the comparison instruction in the header
    for (auto& inst : loop->header->instructions) {
        if (auto cmp = std::dynamic_pointer_cast<ir::BinaryInstruction>(inst)) {
            if (cmp->op == ir::BinaryInstruction::Op::kCmpSlt ||
                cmp->op == ir::BinaryInstruction::Op::kCmpSle ||
                cmp->op == ir::BinaryInstruction::Op::kCmpUlt ||
                cmp->op == ir::BinaryInstruction::Op::kCmpUle) {
                if (cmp->operands.size() >= 2) {
                    try {
                        int64_t bound = std::stoll(cmp->operands[1]);
                        int64_t new_bound = bound / unroll_factor_;
                        cmp->operands[1] = std::to_string(new_bound);
                    } catch (...) {
                        // Bound is not a constant - need runtime calculation
                    }
                }
            }
        }
    }
}

// ============================================================================
// LICM (Loop-Invariant Code Motion) Pass Implementation
// ============================================================================

LICMPass::LICMPass(ir::Function& func) : func_(func) {}

bool LICMPass::Run() {
    LoopAnalysis analysis(func_);
    bool changed = false;
    
    // Process loops from innermost to outermost
    std::vector<LoopInfo*> ordered_loops;
    for (const auto& loop : analysis.GetLoops()) {
        ordered_loops.push_back(loop.get());
    }
    
    // Sort by depth (innermost first)
    std::sort(ordered_loops.begin(), ordered_loops.end(),
        [](const LoopInfo* a, const LoopInfo* b) {
            return a->depth > b->depth;
        });
    
    for (auto* loop : ordered_loops) {
        changed |= ProcessLoop(loop);
    }
    
    return changed;
}

bool LICMPass::ProcessLoop(LoopInfo* loop) {
    if (!loop->preheader) {
        CreatePreheader(loop);
        if (!loop->preheader) return false;  // Couldn't create preheader
    }
    
    bool changed = false;
    
    // Find loop-invariant instructions
    std::vector<std::pair<ir::BasicBlock*, std::shared_ptr<ir::Instruction>>> to_hoist;
    
    for (auto* bb : loop->body) {
        for (auto& inst : bb->instructions) {
            if (IsLoopInvariant(inst.get(), loop) && IsSafeToHoist(inst.get(), loop)) {
                to_hoist.push_back({bb, inst});
            }
        }
    }
    
    // Hoist instructions
    for (auto& [bb, inst] : to_hoist) {
        HoistInstruction(inst.get(), loop);
        
        // Remove from original block
        auto& insts = bb->instructions;
        insts.erase(std::remove(insts.begin(), insts.end(), inst), insts.end());
        
        changed = true;
    }
    
    return changed;
}

bool LICMPass::IsLoopInvariant(ir::Instruction* inst, const LoopInfo* loop) const {
    // An instruction is loop-invariant if all its operands are:
    // 1. Constants
    // 2. Defined outside the loop
    // 3. Loop-invariant themselves
    
    for (const auto& op : inst->operands) {
        if (op.empty()) continue;
        
        // Check if constant
        if (std::isdigit(op[0]) || op[0] == '-') continue;
        
        // Check if defined in loop
        if (IsDefinedInLoop(op, loop)) {
            // Need to check if the definition is also loop-invariant
            // For simplicity, we only hoist if operands are defined outside
            return false;
        }
    }
    
    return true;
}

bool LICMPass::IsSafeToHoist(ir::Instruction* inst, const LoopInfo* loop) const {
    // Not safe to hoist:
    // 1. Instructions with side effects (unless we can prove they execute)
    // 2. Loads that might alias with stores in the loop
    // 3. Instructions whose results are used only conditionally
    
    if (MayHaveSideEffects(inst)) return false;
    
    // Check if the loop is guaranteed to execute
    // For safety, only hoist if the loop has a preheader
    if (!loop->preheader) return false;
    
    // Check for aliasing with stores in the loop
    if (auto load = dynamic_cast<ir::LoadInstruction*>(inst)) {
        for (auto* bb : loop->body) {
            for (const auto& other : bb->instructions) {
                if (auto store = std::dynamic_pointer_cast<ir::StoreInstruction>(other)) {
                    // Conservative check: assume any store might alias
                    (void)load;
                    return false;
                }
            }
        }
    }
    
    return true;
}

void LICMPass::HoistInstruction(ir::Instruction* inst, LoopInfo* loop) {
    if (!loop->preheader) return;
    
    // Find the instruction in the loop and create a copy in the preheader
    for (auto* bb : loop->body) {
        for (auto& orig : bb->instructions) {
            if (orig.get() == inst) {
                // Move to preheader (before terminator)
                loop->preheader->instructions.push_back(orig);
                inst->parent = loop->preheader;
                return;
            }
        }
    }
}

void LICMPass::CreatePreheader(LoopInfo* loop) {
    // Find external predecessors
    std::vector<ir::BasicBlock*> ext_preds;
    for (auto* pred : loop->header->predecessors) {
        if (loop->body.count(pred) == 0) {
            ext_preds.push_back(pred);
        }
    }
    
    if (ext_preds.empty()) return;
    if (ext_preds.size() == 1) {
        // Already have a single external predecessor
        loop->preheader = ext_preds[0];
        return;
    }
    
    // Need to create a new preheader
    // This would require modifying the CFG structure
    // For now, we leave loops without single preheaders unoptimized
}

// ============================================================================
// Loop Fusion Pass Implementation
// ============================================================================

LoopFusionPass::LoopFusionPass(ir::Function& func) : func_(func) {}

bool LoopFusionPass::Run() {
    LoopAnalysis analysis(func_);
    bool changed = false;
    const auto& loops = analysis.GetLoops();
    
    // Find pairs of fusible loops
    for (size_t i = 0; i < loops.size(); ++i) {
        for (size_t j = i + 1; j < loops.size(); ++j) {
            if (CanFuseLoops(loops[i].get(), loops[j].get())) {
                FuseLoops(loops[i].get(), loops[j].get());
                changed = true;
                break;  // Re-analyze after fusion
            }
        }
        if (changed) break;
    }
    
    return changed;
}

bool LoopFusionPass::CanFuseLoops(const LoopInfo* loop1, const LoopInfo* loop2) const {
    // Conditions for fusion:
    // 1. Loops must be adjacent (loop1 exits directly to loop2's preheader)
    // 2. Same iteration space (same bounds)
    // 3. No dependencies that prevent fusion
    // 4. Both must be innermost loops (no nested loops)
    
    if (!loop1->nested_loops.empty() || !loop2->nested_loops.empty()) {
        return false;
    }
    
    // Check adjacency
    bool adjacent = false;
    for (auto* exit : loop1->exits) {
        for (auto* succ : exit->successors) {
            if (succ == loop2->preheader || succ == loop2->header) {
                adjacent = true;
                break;
            }
        }
    }
    if (!adjacent) return false;
    
    // Check same bounds
    int64_t count1 = GetTripCount(loop1, func_);
    int64_t count2 = GetTripCount(loop2, func_);
    if (count1 != count2 || count1 < 0) return false;
    
    // Check for data dependencies
    // Simplified: check if loop2 uses values defined in loop1
    for (auto* bb2 : loop2->body) {
        for (const auto& inst : bb2->instructions) {
            for (const auto& op : inst->operands) {
                if (IsDefinedInLoop(op, loop1)) {
                    // Data dependency - can still fuse if ordering is preserved
                    // For simplicity, we reject this case
                    return false;
                }
            }
        }
    }
    
    return true;
}

void LoopFusionPass::FuseLoops(LoopInfo* loop1, LoopInfo* loop2) {
    // Merge loop2's body into loop1
    
    // 1. Redirect loop1's back edge to go through loop2's body first
    // 2. Unify induction variables
    // 3. Update phi nodes
    
    // Find loop1's latch
    if (loop1->latches.empty() || loop2->latches.empty()) return;
    
    ir::BasicBlock* latch1 = loop1->latches[0];
    
    // Insert loop2's body between loop1's body and back edge
    for (auto* bb2 : loop2->body) {
        if (bb2 == loop2->header) continue;  // Skip header (will merge with loop1's)
        
        // Add instructions from bb2 to latch1
        for (auto& inst : bb2->instructions) {
            latch1->instructions.push_back(inst);
        }
    }
    
    // Update loop1 to include loop2's blocks
    for (auto* bb : loop2->body) {
        loop1->body.insert(bb);
    }
}

// ============================================================================
// Loop Strength Reduction Pass Implementation
// ============================================================================

LoopStrengthReductionPass::LoopStrengthReductionPass(ir::Function& func) : func_(func) {}

bool LoopStrengthReductionPass::Run() {
    LoopAnalysis analysis(func_);
    bool changed = false;
    
    for (const auto& loop : analysis.GetLoops()) {
        changed |= ProcessLoop(loop.get());
    }
    
    return changed;
}

bool LoopStrengthReductionPass::ProcessLoop(LoopInfo* loop) {
    bool changed = false;
    
    // Find the induction variable
    ir::PhiInstruction* iv = FindInductionVariable(loop);
    if (!iv) return false;
    
    // Find multiplication expressions involving the IV
    for (auto* bb : loop->body) {
        for (auto& inst : bb->instructions) {
            auto bin = std::dynamic_pointer_cast<ir::BinaryInstruction>(inst);
            if (!bin) continue;
            
            if (bin->op == ir::BinaryInstruction::Op::kMul) {
                // Check if this is i * c where i is the IV
                std::string iv_op, const_op;
                
                if (bin->operands.size() >= 2) {
                    if (bin->operands[0] == iv->name) {
                        iv_op = bin->operands[0];
                        const_op = bin->operands[1];
                    } else if (bin->operands[1] == iv->name) {
                        iv_op = bin->operands[1];
                        const_op = bin->operands[0];
                    } else {
                        continue;
                    }
                    
                    // Check if const_op is a constant
                    bool is_const = false;
                    int64_t c_val = 0;
                    try {
                        c_val = std::stoll(const_op);
                        is_const = true;
                    } catch (...) {}
                    
                    if (!is_const) continue;
                    
                    // Replace i * c with a new induction variable j = j + c
                    // where j starts at init_i * c
                    
                    // Create new phi for j
                    auto j_phi = std::make_shared<ir::PhiInstruction>();
                    j_phi->name = bin->name + "_sr";
                    j_phi->type = bin->type;
                    
                    // Initial value: init_i * c (find from IV's incomings)
                    for (const auto& [pred, val] : iv->incomings) {
                        if (loop->body.count(pred) == 0) {
                            // This is the initial value from outside the loop
                            try {
                                int64_t init = std::stoll(val);
                                j_phi->incomings.push_back({pred, std::to_string(init * c_val)});
                            } catch (...) {
                                // Can't reduce if initial value isn't constant
                                continue;
                            }
                        }
                    }
                    
                    // Loop update: j = j + c * step
                    // Find IV step
                    int64_t step = 1;  // Default step
                    for (auto* blk : loop->body) {
                        for (auto& other : blk->instructions) {
                            if (auto update = std::dynamic_pointer_cast<ir::BinaryInstruction>(other)) {
                                if (update->operands.size() >= 2 && 
                                    update->operands[0] == iv->name) {
                                    if (update->op == ir::BinaryInstruction::Op::kAdd) {
                                        try {
                                            step = std::stoll(update->operands[1]);
                                        } catch (...) {}
                                    }
                                }
                            }
                        }
                    }
                    
                    // Create j_next = j + c * step
                    auto j_next = std::make_shared<ir::BinaryInstruction>();
                    j_next->op = ir::BinaryInstruction::Op::kAdd;
                    j_next->type = bin->type;
                    j_next->name = bin->name + "_sr_next";
                    j_next->operands.push_back(j_phi->name);
                    j_next->operands.push_back(std::to_string(c_val * step));
                    
                    // Update phi's loop incoming
                    for (auto* latch : loop->latches) {
                        j_phi->incomings.push_back({latch, j_next->name});
                    }
                    
                    // Add phi and update to header
                    loop->header->phis.push_back(j_phi);
                    
                    // Replace the multiplication with j_phi reference
                    bin->op = ir::BinaryInstruction::Op::kAdd;
                    bin->operands.clear();
                    bin->operands.push_back(j_phi->name);
                    bin->operands.push_back("0");
                    
                    // Add j_next instruction
                    loop->latches[0]->instructions.push_back(j_next);
                    
                    changed = true;
                }
            }
        }
    }
    
    return changed;
}

bool LoopStrengthReductionPass::IsInductionVariable(const std::string& var, 
                                                     const LoopInfo* loop) const {
    if (!loop->header) return false;
    
    // Check if var is a phi in the header with loop-carried updates
    for (const auto& phi : loop->header->phis) {
        if (phi->name == var) {
            for (const auto& [pred, val] : phi->incomings) {
                if (loop->body.count(pred)) {
                    return true;  // Has a value coming from inside the loop
                }
            }
        }
    }
    
    return false;
}

}  // namespace polyglot::passes::transform
