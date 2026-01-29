// 虚函数去虚化优化Pass实现

#include "middle/include/passes/devirtualization.h"
#include "middle/include/ir/nodes/statements.h"
#include <algorithm>

namespace polyglot::passes {

bool DevirtualizationPass::Run() {
    devirtualized_count_ = 0;
    
    // 遍历所有函数
    for (auto &func : ir_ctx_.Functions()) {
        if (OptimizeFunction(func.get())) {
            // 函数被修改
        }
    }
    
    return devirtualized_count_ > 0;
}

bool DevirtualizationPass::OptimizeFunction(ir::Function *func) {
    if (!func) return false;
    
    bool modified = false;
    
    // 遍历所有基本块
    for (auto &block : func->blocks) {
        // 遍历所有指令
        for (size_t i = 0; i < block->instructions.size(); ++i) {
            auto &inst = block->instructions[i];
            
            // 查找CallInstruction
            if (auto *call = dynamic_cast<ir::CallInstruction*>(inst.get())) {
                if (TryDevirtualize(call, block.get())) {
                    modified = true;
                    devirtualized_count_++;
                }
            }
        }
    }
    
    return modified;
}

bool DevirtualizationPass::TryDevirtualize(ir::CallInstruction *call, 
                                          ir::BasicBlock *block) {
    if (!call) return false;
    
    // 检查是否是虚函数调用
    // 虚函数调用的特征：
    // 1. 第一个参数是 this 指针
    // 2. 函数名包含类名（ClassName::methodName）
    
    std::string callee = call->callee;
    size_t pos = callee.find("::");
    if (pos == std::string::npos) {
        // 不是成员函数调用
        return false;
    }
    
    std::string class_name = callee.substr(0, pos);
    std::string method_name = callee.substr(pos + 2);
    
    // 检查这个方法是否是虚函数
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
        // 不是虚函数，无需去虚化
        return false;
    }
    
    // 策略1: 检查是否是final类
    if (IsFinalClass(class_name)) {
        // Final类的虚函数可以直接调用
        // 已经是直接调用形式，标记为已优化
        return true;
    }
    
    // 策略2: 检查是否是final方法
    if (IsFinalMethod(class_name, method_name)) {
        // Final方法不能被重写，可以直接调用
        return true;
    }
    
    // 策略3: 检查是否只有唯一实现
    const ir::MethodInfo *unique_impl = GetUniqueImplementation(class_name, method_name);
    if (unique_impl) {
        // 只有一个实现，直接调用它
        call->callee = unique_impl->mangled_name;
        return true;
    }
    
    // 策略4: 尝试类型传播
    if (!call->operands.empty()) {
        std::string obj_name = call->operands[0];
        std::string obj_type = InferObjectType(obj_name, nullptr);
        if (!obj_type.empty() && obj_type != class_name) {
            // 确定了更具体的类型
            auto *derived_methods = class_metadata_.GetMethods(obj_type);
            if (derived_methods) {
                for (const auto &m : *derived_methods) {
                    if (m.name == method_name) {
                        // 找到派生类的实现
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
    // 检查final_classes_集合
    // 注意：需要在类解析时填充这个集合
    return final_classes_.count(class_name) > 0;
}

bool DevirtualizationPass::IsFinalMethod(const std::string &class_name, 
                                        const std::string &method_name) {
    // 检查方法是否标记为final
    auto *methods = class_metadata_.GetMethods(class_name);
    if (!methods) return false;
    
    for (const auto &m : *methods) {
        if (m.name == method_name) {
            // TODO: 需要在MethodInfo中添加is_final标志
            // return m.is_final;
            return false;  // 暂时返回false
        }
    }
    
    return false;
}

const ir::MethodInfo* DevirtualizationPass::GetUniqueImplementation(
    const std::string &class_name, const std::string &method_name) {
    
    // 查找所有可能的实现
    std::vector<const ir::MethodInfo*> implementations;
    
    // 检查基类
    const ir::MethodInfo *base_impl = class_metadata_.FindVirtualMethod(
        class_name, method_name);
    if (base_impl) {
        implementations.push_back(base_impl);
    }
    
    // 检查所有派生类
    // 注意：需要维护类继承关系图
    // 简化实现：仅检查当前类
    
    if (implementations.size() == 1) {
        return implementations[0];
    }
    
    return nullptr;
}

std::string DevirtualizationPass::InferObjectType(const std::string &value_name, 
                                                  ir::Function *func) {
    // 检查缓存
    auto it = type_cache_.find(value_name);
    if (it != type_cache_.end()) {
        return it->second;
    }
    
    // 简单的类型推导：
    // 1. 如果值来自new表达式，可以确定类型
    // 2. 如果值来自局部变量，检查赋值语句
    // 3. 如果值来自参数，使用参数类型
    
    if (!func) {
        return "";
    }
    
    // 遍历函数的所有指令，查找value_name的定义
    for (auto &block : func->blocks) {
        for (auto &inst : block->instructions) {
            if (inst->name == value_name) {
                // 找到定义
                // 检查指令类型
                if (auto *call = dynamic_cast<ir::CallInstruction*>(inst.get())) {
                    // 如果是构造函数调用或new，可能知道类型
                    std::string callee = call->callee;
                    if (callee.find("::") != std::string::npos) {
                        std::string class_name = callee.substr(0, callee.find("::"));
                        type_cache_[value_name] = class_name;
                        return class_name;
                    }
                }
                // 其他指令类型...
            }
        }
    }
    
    return "";
}

} // namespace polyglot::passes
