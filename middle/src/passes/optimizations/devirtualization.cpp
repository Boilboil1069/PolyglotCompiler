/**
 * @file     devirtualization.cpp
 * @brief    Middle-end implementation
 *
 * @ingroup  Middle
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
// Devirtualization pass implementation

#include "middle/include/passes/devirtualization.h"
#include "middle/include/ir/nodes/statements.h"
#include <algorithm>

namespace polyglot::passes {

bool DevirtualizationPass::Run() {
    devirtualized_count_ = 0;
    
    // Iterate over all functions
    for (auto &func : ir_ctx_.Functions()) {
        if (OptimizeFunction(func.get())) {
            // Function was modified
        }
    }
    
    return devirtualized_count_ > 0;
}

bool DevirtualizationPass::OptimizeFunction(ir::Function *func) {
    if (!func) return false;
    
    bool modified = false;
    
    // Iterate over all basic blocks
    for (auto &block : func->blocks) {
        // Iterate over all instructions
        for (size_t i = 0; i < block->instructions.size(); ++i) {
            auto &inst = block->instructions[i];
            
            // Look for CallInstruction
            if (auto *call = dynamic_cast<ir::CallInstruction*>(inst.get())) {
                if (TryDevirtualize(call, block.get(), func)) {
                    modified = true;
                    devirtualized_count_++;
                }
            }
        }
    }
    
    return modified;
}

bool DevirtualizationPass::TryDevirtualize(ir::CallInstruction *call, 
                                          ir::BasicBlock *block,
                                          ir::Function *func) {
    if (!call) return false;
    
    // Check whether this is a virtual call
    // Characteristics of a virtual call:
    // 1. First argument is the this pointer
    // 2. Callee name contains the class (ClassName::methodName)
    
    std::string callee = call->callee;
    size_t pos = callee.find("::");
    if (pos == std::string::npos) {
        // Not a member function call
        return false;
    }
    
    std::string class_name = callee.substr(0, pos);
    std::string method_name = callee.substr(pos + 2);
    
    // Check whether this method is virtual
    auto *methods = class_metadata_.GetMethods(class_name);
    if (!methods) return false;
    
    const ir::MethodInfo *method_info = nullptr;
    for (const auto &m : *methods) {
        if (m.name == method_name) {
            method_info = &m;
            break;
        }
    }
    
    if (!method_info || !method_info->is_virtual) {
        // Not virtual, no devirtualization needed
        return false;
    }
    
    // Strategy 1: check whether the class is final
    if (IsFinalClass(class_name)) {
        // Virtuals in final classes can be called directly
        // Already a direct call; mark as optimized
        return true;
    }
    
    // Strategy 2: check whether the method is final
    if (IsFinalMethod(class_name, method_name)) {
        // Final methods cannot be overridden; call directly
        return true;
    }
    
    // Strategy 3: check whether there is a unique implementation
    const ir::MethodInfo *unique_impl = GetUniqueImplementation(class_name, method_name);
    if (unique_impl) {
        // Only one implementation; call it directly
        call->callee = unique_impl->mangled_name;
        return true;
    }
    
    // Strategy 4: attempt type propagation
    if (!call->operands.empty()) {
        std::string obj_name = call->operands[0];
        std::string obj_type = InferObjectType(obj_name, func);
        if (!obj_type.empty() && obj_type != class_name) {
            // A more specific type was identified
            auto *derived_methods = class_metadata_.GetMethods(obj_type);
            if (derived_methods) {
                for (const auto &m : *derived_methods) {
                    if (m.name == method_name) {
                        // Found a derived-class implementation
                        call->callee = m.mangled_name;
                        return true;
                    }
                }
            }
        }
    }
    
    return false;
}

bool DevirtualizationPass::IsFinalClass(const std::string &class_name) {
    // Check the final_classes_ set
    // Note: this set must be populated during class parsing
    return final_classes_.count(class_name) > 0;
}

bool DevirtualizationPass::IsFinalMethod(const std::string &class_name, 
                                        const std::string &method_name) {
    // Check whether the method is marked final
    auto *methods = class_metadata_.GetMethods(class_name);
    if (!methods) return false;
    
    for (const auto &m : *methods) {
        if (m.name == method_name) {
            // Use the is_final flag on MethodInfo
            return m.is_final;
        }
    }
    
    return false;
}

const ir::MethodInfo* DevirtualizationPass::GetUniqueImplementation(
    const std::string &class_name, const std::string &method_name) {
    
    // Find all possible implementations across the class hierarchy
    std::vector<const ir::MethodInfo*> implementations;
    
    // Check the base class itself
    const ir::MethodInfo *base_impl = class_metadata_.FindVirtualMethod(
        class_name, method_name);
    if (base_impl) {
        implementations.push_back(base_impl);
    }
    
    // Collect known classes from functions whose name contains "::" (member
    // functions reveal class names) and from final_classes_.
    std::unordered_set<std::string> candidate_classes;
    for (auto &fn : ir_ctx_.Functions()) {
        auto sep = fn->name.find("::");
        if (sep != std::string::npos) {
            candidate_classes.insert(fn->name.substr(0, sep));
        }
    }
    for (const auto &fc : final_classes_) {
        candidate_classes.insert(fc);
    }

    // Check each candidate: if it derives from class_name and overrides the
    // method, count it as a separate implementation.
    for (const auto &cand : candidate_classes) {
        if (cand == class_name) continue;
        if (!class_metadata_.IsBaseOf(class_name, cand)) continue;

        const ir::MethodInfo *derived_impl =
            class_metadata_.FindVirtualMethod(cand, method_name);
        if (derived_impl) {
            // Only count it if it is a genuinely different override
            bool duplicate = false;
            for (const auto *existing : implementations) {
                if (existing->mangled_name == derived_impl->mangled_name) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                implementations.push_back(derived_impl);
            }
        }
    }

    if (implementations.size() == 1) {
        return implementations[0];
    }
    
    return nullptr;
}

std::string DevirtualizationPass::InferObjectType(const std::string &value_name, 
                                                  ir::Function *func) {
    // Check cache first
    auto it = type_cache_.find(value_name);
    if (it != type_cache_.end()) {
        return it->second;
    }
    
    // Simple type inference:
    // 1. If the value comes from a new expression, the type is known
    // 2. If the value comes from a local, inspect its assignments
    // 3. If the value is a parameter, use the parameter type
    
    if (!func) {
        return "";
    }
    
    // Walk all instructions in the function to find the definition of value_name
    for (auto &block : func->blocks) {
        for (auto &inst : block->instructions) {
            if (inst->name == value_name) {
                // Found the definition
                // Inspect the instruction type
                if (auto *call = dynamic_cast<ir::CallInstruction*>(inst.get())) {
                    // Constructor/new calls may reveal the type
                    std::string callee = call->callee;
                    if (callee.find("::") != std::string::npos) {
                        std::string class_name = callee.substr(0, callee.find("::"));
                        type_cache_[value_name] = class_name;
                        return class_name;
                    }
                }
                // Other instruction types...
            }
        }
    }
    
    return "";
}

} // namespace polyglot::passes
