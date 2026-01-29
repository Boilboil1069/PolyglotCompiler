#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <set>

namespace polyglot::ir {
    struct Function;
    struct Instruction;
    struct BasicBlock;
}

namespace polyglot::passes::transform {

// Value numbering for an expression
struct ValueNumber {
    size_t number;
    std::string canonical_name;  // Canonical representative
};

// Global Value Numbering Pass
// Eliminates redundant computations across basic blocks
class GVNPass {
public:
    explicit GVNPass(ir::Function& func);
    
    bool Run();
    
private:
    struct Expression {
        std::string opcode;
        std::vector<size_t> operand_numbers;
        
        bool operator==(const Expression& other) const {
            return opcode == other.opcode && operand_numbers == other.operand_numbers;
        }
    };
    
    struct ExpressionHash {
        size_t operator()(const Expression& expr) const;
    };
    
    // Compute value numbers for all instructions
    void ComputeValueNumbers();
    
    // Replace redundant computations
    void EliminateRedundancies();
    
    // Get value number for an operand
    size_t GetValueNumber(const std::string& operand);
    
    // Create expression key for instruction
    Expression CreateExpression(Instruction* inst);
    
    // Check if instruction is pure (no side effects)
    bool IsPure(Instruction* inst) const;
    
    ir::Function& func_;
    size_t next_value_number_;
    std::unordered_map<std::string, size_t> value_numbers_;
    std::unordered_map<Expression, size_t, ExpressionHash> expression_to_number_;
    std::unordered_map<size_t, std::string> number_to_canonical_;
    std::unordered_map<std::string, std::string> replacements_;
};

// Partial Redundancy Elimination (PRE)
// More aggressive than GVN, can eliminate partial redundancies
class PREPass {
public:
    explicit PREPass(ir::Function& func);
    
    bool Run();
    
private:
    struct Expression {
        std::string opcode;
        std::vector<std::string> operands;
        
        bool operator<(const Expression& other) const {
            if (opcode != other.opcode) return opcode < other.opcode;
            return operands < other.operands;
        }
    };
    
    struct ExpressionCompare {
        bool operator()(const Expression& a, const Expression& b) const {
            return a < b;
        }
    };
    
    void ComputeAvailability();
    void ComputeAnticipation();
    bool InsertCompensationCode(ir::BasicBlock* bb, const Expression& expr);
    Expression CreateExpression(ir::Instruction* inst);
    bool IsPure(ir::Instruction* inst) const;
    
    ir::Function& func_;
    std::unordered_map<ir::BasicBlock*, std::set<Expression, ExpressionCompare>> available_in_;
    std::unordered_map<ir::BasicBlock*, std::set<Expression, ExpressionCompare>> available_out_;
    std::unordered_map<ir::BasicBlock*, std::set<Expression, ExpressionCompare>> anticipated_in_;
    std::unordered_map<ir::BasicBlock*, std::set<Expression, ExpressionCompare>> anticipated_out_;
    size_t next_temp_id_ = 0;
};

// Alias Analysis Enhancement
class AliasAnalysisPass {
public:
    explicit AliasAnalysisPass(ir::Function& func);
    
    enum class AliasResult {
        kNoAlias,      // Definitely don't alias
        kMayAlias,     // Might alias
        kMustAlias     // Definitely alias
    };
    
    AliasResult Query(const std::string& ptr1, const std::string& ptr2);
    
private:
    void AnalyzePointers();
    void BuildPointsToSets();
    
    ir::Function& func_;
    std::unordered_map<std::string, std::unordered_set<std::string>> points_to_;
};

}  // namespace polyglot::passes::transform
