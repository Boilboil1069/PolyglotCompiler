// Devirtualization optimization pass
// Converts virtual calls with known types into direct calls

#pragma once

#include "middle/include/ir/ir_context.h"
#include "middle/include/ir/cfg.h"
#include "middle/include/ir/nodes/statements.h"
#include "middle/include/ir/class_metadata.h"
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace polyglot::passes {

// Devirtualization optimization
//
// Optimization strategies:
// 1. Final class detection - if a class is final, all virtual calls can be devirtualized
// 2. Single implementation detection - if a virtual function has only one implementation, devirtualize it
// 3. Type propagation - when an object's concrete type can be inferred, devirtualize
// 4. Call-site analysis - analyze possible types at each call site
class DevirtualizationPass {
public:
    explicit DevirtualizationPass(ir::IRContext &ctx, ir::ClassMetadata &metadata)
        : ir_ctx_(ctx), class_metadata_(metadata) {}
    
    // Run the optimization
    bool Run();
    
private:
    // Analyze virtual calls in a function
    bool OptimizeFunction(ir::Function *func);
    
    // Attempt to devirtualize a single call instruction
    bool TryDevirtualize(ir::CallInstruction *call, ir::BasicBlock *block);
    
    // Check if a class is final (non-inheritable)
    bool IsFinalClass(const std::string &class_name);
    
    // Check if a method is final (non-overridable)
    bool IsFinalMethod(const std::string &class_name, const std::string &method_name);
    
    // Get the unique implementation of a virtual function (if only one exists)
    const ir::MethodInfo* GetUniqueImplementation(const std::string &class_name, 
                                                   const std::string &method_name);
    
    // Try to determine the object type via type propagation
    std::string InferObjectType(const std::string &value_name, ir::Function *func);
    
    ir::IRContext &ir_ctx_;
    ir::ClassMetadata &class_metadata_;
    
    // Statistics
    size_t devirtualized_count_ = 0;
    
    // Set of final classes (can be read from class attributes)
    std::unordered_set<std::string> final_classes_;
    
    // Type inference cache
    std::unordered_map<std::string, std::string> type_cache_;
};

} // namespace polyglot::passes
