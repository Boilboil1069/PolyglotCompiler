/**
 * @file     gvn.cpp
 * @brief    Middle-end implementation
 *
 * @ingroup  Middle
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include "middle/include/passes/transform/gvn.h"
#include "middle/include/ir/analysis.h"
#include "middle/include/ir/cfg.h"
#include "middle/include/ir/nodes/statements.h"
#include <algorithm>
#include <functional>
#include <iterator>

namespace polyglot::passes::transform {

using namespace polyglot::ir;

// ============================================================================
// GVN Pass Implementation
// ============================================================================

GVNPass::GVNPass(ir::Function& func) 
    : func_(func), next_value_number_(1) {}

bool GVNPass::Run() {
    ComputeValueNumbers();
    EliminateRedundancies();
    
    return !replacements_.empty();
}

void GVNPass::ComputeValueNumbers() {
    // Process blocks in dominator tree order for better precision
    
    for (auto& bb_ptr : func_.blocks) {
        BasicBlock* bb = bb_ptr.get();
        
        // Process phi nodes
        for (auto& phi_ptr : bb->phis) {
            PhiInstruction* phi = phi_ptr.get();
            if (!phi->name.empty()) {
                value_numbers_[phi->name] = next_value_number_++;
                number_to_canonical_[value_numbers_[phi->name]] = phi->name;
            }
        }
        
        // Process instructions
        for (auto& inst_ptr : bb->instructions) {
            Instruction* inst = inst_ptr.get();
            
            if (!IsPure(inst)) {
                // Non-pure instructions get unique value numbers
                if (!inst->name.empty()) {
                    value_numbers_[inst->name] = next_value_number_++;
                    number_to_canonical_[value_numbers_[inst->name]] = inst->name;
                }
                continue;
            }
            
            // Create expression for pure instructions
            Expression expr = CreateExpression(inst);
            
            // Check if we've seen this expression before
            auto it = expression_to_number_.find(expr);
            if (it != expression_to_number_.end()) {
                // Found redundant computation!
                size_t vn = it->second;
                value_numbers_[inst->name] = vn;
                
                // Record replacement
                std::string canonical = number_to_canonical_[vn];
                if (canonical != inst->name) {
                    replacements_[inst->name] = canonical;
                }
            } else {
                // New expression
                size_t vn = next_value_number_++;
                expression_to_number_[expr] = vn;
                value_numbers_[inst->name] = vn;
                number_to_canonical_[vn] = inst->name;
            }
        }
    }
}

void GVNPass::EliminateRedundancies() {
    if (replacements_.empty()) return;
    
    // Replace all uses of redundant values with their canonical versions
    for (auto& bb_ptr : func_.blocks) {
        BasicBlock* bb = bb_ptr.get();
        
        // Update phi nodes
        for (auto& phi_ptr : bb->phis) {
            for (auto& incoming : phi_ptr->incomings) {
                auto it = replacements_.find(incoming.second);
                if (it != replacements_.end()) {
                    incoming.second = it->second;
                }
            }
        }
        
        // Update instructions
        for (auto& inst_ptr : bb->instructions) {
            for (auto& op : inst_ptr->operands) {
                auto it = replacements_.find(op);
                if (it != replacements_.end()) {
                    op = it->second;
                }
            }
        }
        
        // Update terminator
        if (bb->terminator) {
            for (auto& op : bb->terminator->operands) {
                auto it = replacements_.find(op);
                if (it != replacements_.end()) {
                    op = it->second;
                }
            }
        }
    }
    
    // Remove dead instructions (those that were replaced)
    for (auto& bb_ptr : func_.blocks) {
        auto& insts = bb_ptr->instructions;
        insts.erase(
            std::remove_if(insts.begin(), insts.end(), 
                [this](const std::shared_ptr<Instruction>& inst) {
                    return replacements_.count(inst->name) > 0;
                }),
            insts.end()
        );
    }
}

size_t GVNPass::GetValueNumber(const std::string& operand) {
    // Check if it's a constant
    try {
        size_t idx = 0;
        std::stoll(operand, &idx, 0);
        if (idx == operand.size()) {
            // It's a constant - create a unique value number based on the value
            auto it = value_numbers_.find(operand);
            if (it == value_numbers_.end()) {
                size_t vn = next_value_number_++;
                value_numbers_[operand] = vn;
                return vn;
            }
            return it->second;
        }
    } catch (...) {}
    
    // Regular variable
    auto it = value_numbers_.find(operand);
    if (it != value_numbers_.end()) {
        return it->second;
    }
    
    // Unknown operand (might be function parameter)
    size_t vn = next_value_number_++;
    value_numbers_[operand] = vn;
    return vn;
}

GVNPass::Expression GVNPass::CreateExpression(Instruction* inst) {
    Expression expr;
    
    if (auto* bin = dynamic_cast<BinaryInstruction*>(inst)) {
        expr.opcode = "binary_" + std::to_string(static_cast<int>(bin->op));
        
        if (bin->operands.size() >= 2) {
            size_t vn1 = GetValueNumber(bin->operands[0]);
            size_t vn2 = GetValueNumber(bin->operands[1]);
            
            // For commutative operations, normalize the order
            if (bin->op == BinaryInstruction::Op::kAdd ||
                bin->op == BinaryInstruction::Op::kMul ||
                bin->op == BinaryInstruction::Op::kAnd ||
                bin->op == BinaryInstruction::Op::kOr ||
                bin->op == BinaryInstruction::Op::kXor) {
                if (vn1 > vn2) std::swap(vn1, vn2);
            }
            
            expr.operand_numbers.push_back(vn1);
            expr.operand_numbers.push_back(vn2);
        }
    } else if (auto* load = dynamic_cast<LoadInstruction*>(inst)) {
        expr.opcode = "load";
        if (!load->operands.empty()) {
            expr.operand_numbers.push_back(GetValueNumber(load->operands[0]));
        }
    } else if (auto* gep = dynamic_cast<GetElementPtrInstruction*>(inst)) {
        expr.opcode = "gep";
        for (const auto& op : gep->operands) {
            expr.operand_numbers.push_back(GetValueNumber(op));
        }
    } else if (auto* cast = dynamic_cast<CastInstruction*>(inst)) {
        expr.opcode = "cast_" + std::to_string(static_cast<int>(cast->cast));
        if (!cast->operands.empty()) {
            expr.operand_numbers.push_back(GetValueNumber(cast->operands[0]));
        }
    } else {
        // Generic instruction
        expr.opcode = "inst_" + inst->name;
    }
    
    return expr;
}

bool GVNPass::IsPure(Instruction* inst) const {
    // Instructions with side effects are not pure
    if (dynamic_cast<StoreInstruction*>(inst)) return false;
    if (dynamic_cast<CallInstruction*>(inst)) return false;
    if (dynamic_cast<InvokeInstruction*>(inst)) return false;
    if (dynamic_cast<AllocaInstruction*>(inst)) return false;
    
    // Load instructions might not be pure if there are intervening stores
    // For now, treat them as pure (conservative)
    
    return true;
}

size_t GVNPass::ExpressionHash::operator()(const Expression& expr) const {
    size_t h = std::hash<std::string>{}(expr.opcode);
    for (size_t vn : expr.operand_numbers) {
        h ^= std::hash<size_t>{}(vn) + 0x9e3779b9 + (h << 6) + (h >> 2);
    }
    return h;
}

// ============================================================================
// PRE Pass Implementation
// ============================================================================

PREPass::PREPass(ir::Function& func) : func_(func) {}

bool PREPass::Run() {
    ComputeAvailability();
    ComputeAnticipation();
    
    bool changed = false;
    
    // Find insertion points and insert compensation code
    for (auto& bb_ptr : func_.blocks) {
        BasicBlock* bb = bb_ptr.get();

        auto& anticipated_set = anticipated_in_[bb];
        auto& available_set = available_in_[bb];

        for (const auto& expr : anticipated_set) {
            if (available_set.count(expr) > 0) continue;

            if (bb->predecessors.size() > 1) {
                if (InsertCompensationCode(bb, expr)) {
                    changed = true;
                }
            }
        }
    }
    
    return changed;
}

void PREPass::ComputeAvailability() {
    // Compute available expressions using dataflow analysis
    bool changed = true;
    
    while (changed) {
        changed = false;
        
        for (auto& bb_ptr : func_.blocks) {
            BasicBlock* bb = bb_ptr.get();
            
            // available_in[bb] = ∩ available_out[pred] for all pred
            std::set<Expression, ExpressionCompare> new_in;
            bool first_pred = true;
            
            for (auto* pred : bb->predecessors) {
                auto& pred_out = available_out_[pred];
                
                if (first_pred) {
                    new_in = pred_out;
                    first_pred = false;
                } else {
                    std::set<Expression, ExpressionCompare> intersection;
                    std::set_intersection(new_in.begin(), new_in.end(),
                                          pred_out.begin(), pred_out.end(),
                                          std::inserter(intersection, intersection.begin()),
                                          ExpressionCompare{});
                    new_in = std::move(intersection);
                }
            }
            
            if (new_in != available_in_[bb]) {
                available_in_[bb] = new_in;
                changed = true;
            }
            
            // available_out[bb] = (available_in[bb] - kill[bb]) ∪ gen[bb]
            auto new_out = available_in_[bb];
            
            // Add generated expressions
            for (const auto& inst_ptr : bb->instructions) {
                // Simplified: treat pure instructions as generated expressions
                if (IsPure(inst_ptr.get())) {
                    Expression expr = CreateExpression(inst_ptr.get());
                    new_out.insert(expr);
                }
            }
            
            if (new_out != available_out_[bb]) {
                available_out_[bb] = new_out;
                changed = true;
            }
        }
    }
}

void PREPass::ComputeAnticipation() {
    // Compute anticipated expressions using backwards dataflow analysis
    bool changed = true;
    
    while (changed) {
        changed = false;
        
        // Process blocks in reverse order
        for (auto it = func_.blocks.rbegin(); it != func_.blocks.rend(); ++it) {
            BasicBlock* bb = it->get();
            
            // anticipated_out[bb] = ∩ anticipated_in[succ] for all succ
            std::set<Expression, ExpressionCompare> new_out;
            bool first_succ = true;
            
            for (auto* succ : bb->successors) {
                auto& succ_in = anticipated_in_[succ];
                
                if (first_succ) {
                    new_out = succ_in;
                    first_succ = false;
                } else {
                    std::set<Expression, ExpressionCompare> intersection;
                    std::set_intersection(new_out.begin(), new_out.end(),
                                          succ_in.begin(), succ_in.end(),
                                          std::inserter(intersection, intersection.begin()),
                                          ExpressionCompare{});
                    new_out = std::move(intersection);
                }
            }
            
            if (new_out != anticipated_out_[bb]) {
                anticipated_out_[bb] = new_out;
                changed = true;
            }
            
            // anticipated_in[bb] = (anticipated_out[bb] - kill[bb]) ∪ use[bb]
            auto new_in = anticipated_out_[bb];
            
            // Add used expressions
            for (const auto& inst_ptr : bb->instructions) {
                if (IsPure(inst_ptr.get())) {
                    Expression expr = CreateExpression(inst_ptr.get());
                    new_in.insert(expr);
                }
            }
            
            if (new_in != anticipated_in_[bb]) {
                anticipated_in_[bb] = new_in;
                changed = true;
            }
        }
    }
}

bool PREPass::InsertCompensationCode(BasicBlock* bb, const Expression& expr) {
    // Insert computation of the expression at the beginning of bb.
    // Infer the result type from the original instruction that matches this
    // expression anywhere in the function.
    
    IRType result_type = IRType::I64();  // sensible default for integer expressions

    // Scan function blocks to find the instruction that produced this expression
    // and adopt its type.
    for (const auto &blk : func_.blocks) {
        if (!blk) continue;
        for (const auto &inst : blk->instructions) {
            if (!inst || !IsPure(inst.get())) continue;
            Expression existing = CreateExpression(inst.get());
            if (existing == expr) {
                result_type = inst->type;
                goto found;
            }
        }
    }
found:

    // Create new instruction for the expression
    auto new_inst = std::make_shared<Instruction>();
    new_inst->name = "_pre_temp_" + std::to_string(next_temp_id_++);
    new_inst->type = result_type;

    // Copy operand names into the new instruction so it is self-contained
    new_inst->operands = expr.operands;
    
    // Add to beginning of block
    bb->instructions.insert(bb->instructions.begin(), new_inst);
    
    return true;
}

PREPass::Expression PREPass::CreateExpression(Instruction* inst) {
    // Reuse GVN's expression creation logic
    Expression expr;
    
    if (auto* bin = dynamic_cast<BinaryInstruction*>(inst)) {
        expr.opcode = "binary_" + std::to_string(static_cast<int>(bin->op));
        if (bin->operands.size() >= 2) {
            expr.operands = {bin->operands[0], bin->operands[1]};
        }
    }
    
    return expr;
}

bool PREPass::IsPure(Instruction* inst) const {
    if (dynamic_cast<StoreInstruction*>(inst)) return false;
    if (dynamic_cast<CallInstruction*>(inst)) return false;
    return true;
}

// ============================================================================
// Alias Analysis Pass Implementation
// ============================================================================

AliasAnalysisPass::AliasAnalysisPass(ir::Function& func) : func_(func) {
    AnalyzePointers();
}

AliasAnalysisPass::AliasResult AliasAnalysisPass::Query(
    const std::string& ptr1, const std::string& ptr2) {
    
    // Simple alias analysis based on points-to sets
    auto it1 = points_to_.find(ptr1);
    auto it2 = points_to_.find(ptr2);
    
    if (it1 == points_to_.end() || it2 == points_to_.end()) {
        return AliasResult::kMayAlias;
    }
    
    const auto& set1 = it1->second;
    const auto& set2 = it2->second;
    
    // Check for intersection
    for (const auto& obj : set1) {
        if (set2.count(obj) > 0) {
            return AliasResult::kMayAlias;
        }
    }
    
    return AliasResult::kNoAlias;
}

void AliasAnalysisPass::AnalyzePointers() {
    BuildPointsToSets();
}

void AliasAnalysisPass::BuildPointsToSets() {
    // Andersen's points-to analysis (simplified)
    bool changed = true;
    
    // Initialize points-to sets
    for (auto& bb_ptr : func_.blocks) {
        for (auto& inst_ptr : bb_ptr->instructions) {
            if (auto* alloca = dynamic_cast<AllocaInstruction*>(inst_ptr.get())) {
                // Alloca creates a new object
                points_to_[alloca->name].insert(alloca->name);
            }
        }
    }
    
    // Iterate until fixpoint
    while (changed) {
        changed = false;
        
        for (auto& bb_ptr : func_.blocks) {
            for (auto& inst_ptr : bb_ptr->instructions) {
                std::unordered_set<std::string> new_pointees;
                
                if (auto* load = dynamic_cast<LoadInstruction*>(inst_ptr.get())) {
                    // Load from pointer: load = *ptr
                    // points-to(load) = ∪ points-to(o) for all o in points-to(ptr)
                    if (!load->operands.empty()) {
                        auto ptr_it = points_to_.find(load->operands[0]);
                        if (ptr_it != points_to_.end()) {
                            for (const auto& obj : ptr_it->second) {
                                // Load could point to what the object points to
                                auto obj_it = points_to_.find(obj);
                                if (obj_it != points_to_.end()) {
                                    new_pointees.insert(obj_it->second.begin(), obj_it->second.end());
                                } else {
                                    new_pointees.insert(obj);
                                }
                            }
                        }
                    }
                    
                    // Update points-to set
                    if (!new_pointees.empty()) {
                        auto& current = points_to_[load->name];
                        size_t old_size = current.size();
                        current.insert(new_pointees.begin(), new_pointees.end());
                        if (current.size() > old_size) changed = true;
                    }
                    
                } else if (auto* gep = dynamic_cast<GetElementPtrInstruction*>(inst_ptr.get())) {
                    // GEP derives from base pointer
                    // points-to(gep) = points-to(base)
                    if (!gep->operands.empty()) {
                        auto base_it = points_to_.find(gep->operands[0]);
                        if (base_it != points_to_.end()) {
                            auto& current = points_to_[gep->name];
                            size_t old_size = current.size();
                            current.insert(base_it->second.begin(), base_it->second.end());
                            if (current.size() > old_size) changed = true;
                        }
                    }
                    
                } else if (auto* phi = dynamic_cast<PhiInstruction*>(inst_ptr.get())) {
                    // Phi merges points-to sets
                    // points-to(phi) = ∪ points-to(incoming) for all incoming values
                    for (const auto& incoming : phi->incomings) {
                        const auto& value = incoming.second;
                        auto value_it = points_to_.find(value);
                        if (value_it != points_to_.end()) {
                            auto& current = points_to_[phi->name];
                            size_t old_size = current.size();
                            current.insert(value_it->second.begin(), value_it->second.end());
                            if (current.size() > old_size) changed = true;
                        }
                    }
                    
                } else if (auto* bin = dynamic_cast<BinaryInstruction*>(inst_ptr.get())) {
                    // Pointer arithmetic
                    if (bin->operands.size() >= 2) {
                        // If either operand is a pointer, result points to same objects
                        for (const auto& op : bin->operands) {
                            auto op_it = points_to_.find(op);
                            if (op_it != points_to_.end()) {
                                auto& current = points_to_[bin->name];
                                size_t old_size = current.size();
                                current.insert(op_it->second.begin(), op_it->second.end());
                                if (current.size() > old_size) changed = true;
                            }
                        }
                    }
                }
            }
        }
    }
}

}  // namespace polyglot::passes::transform
