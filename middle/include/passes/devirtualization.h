// 虚函数去虚化优化Pass
// 将确定类型的虚函数调用转换为直接调用

#pragma once

#include "middle/include/ir/ir_context.h"
#include "middle/include/ir/cfg.h"
#include "middle/include/ir/nodes/statements.h"
#include "middle/include/ir/class_metadata.h"
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace polyglot::passes {

// 虚函数去虚化优化
// 
// 优化策略：
// 1. Final类检测 - 如果类标记为final，所有虚函数调用可以去虚化
// 2. 单一实现检测 - 如果虚函数只有一个实现，可以去虚化
// 3. 类型传播 - 如果能确定对象的实际类型，可以去虚化
// 4. 调用点分析 - 分析特定调用点的可能类型
class DevirtualizationPass {
public:
    explicit DevirtualizationPass(ir::IRContext &ctx, ir::ClassMetadata &metadata)
        : ir_ctx_(ctx), class_metadata_(metadata) {}
    
    // 运行优化
    bool Run();
    
private:
    // 分析函数中的虚函数调用
    bool OptimizeFunction(ir::Function *func);
    
    // 尝试去虚化单个调用指令
    bool TryDevirtualize(ir::CallInstruction *call, ir::BasicBlock *block);
    
    // 检查类是否为final（不能被继承）
    bool IsFinalClass(const std::string &class_name);
    
    // 检查方法是否为final（不能被重写）
    bool IsFinalMethod(const std::string &class_name, const std::string &method_name);
    
    // 获取虚函数的唯一实现（如果只有一个）
    const ir::MethodInfo* GetUniqueImplementation(const std::string &class_name, 
                                                   const std::string &method_name);
    
    // 尝试通过类型传播确定对象类型
    std::string InferObjectType(const std::string &value_name, ir::Function *func);
    
    ir::IRContext &ir_ctx_;
    ir::ClassMetadata &class_metadata_;
    
    // 统计信息
    size_t devirtualized_count_ = 0;
    
    // Final类集合（可以从类属性中读取）
    std::unordered_set<std::string> final_classes_;
    
    // 类型推导缓存
    std::unordered_map<std::string, std::string> type_cache_;
};

} // namespace polyglot::passes
