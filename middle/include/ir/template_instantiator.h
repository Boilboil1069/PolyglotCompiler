#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include "middle/include/ir/nodes/types.h"

namespace polyglot::ir {

// 模板参数
struct TemplateParameter {
    std::string name;          // 参数名
    bool is_typename{true};    // true: typename/class, false: non-type parameter
    IRType type;               // 非类型参数的类型
    std::string default_value; // 默认值
};

// 模板实参
struct TemplateArgument {
    bool is_type{true};        // true: 类型实参, false: 非类型实参
    IRType type;               // 类型实参
    std::string value;         // 非类型实参的值
};

// 模板实例化键（用于缓存）
struct TemplateInstantiationKey {
    std::string template_name;
    std::vector<TemplateArgument> arguments;
    
    bool operator==(const TemplateInstantiationKey &other) const {
        if (template_name != other.template_name) return false;
        if (arguments.size() != other.arguments.size()) return false;
        
        for (size_t i = 0; i < arguments.size(); ++i) {
            const auto &arg1 = arguments[i];
            const auto &arg2 = other.arguments[i];
            
            if (arg1.is_type != arg2.is_type) return false;
            if (arg1.is_type) {
                if (arg1.type != arg2.type) return false;
            } else {
                if (arg1.value != arg2.value) return false;
            }
        }
        
        return true;
    }
};

// 模板特化信息
struct TemplateSpecialization {
    std::vector<TemplateArgument> pattern;  // 特化模式
    void *specialized_ast{nullptr};         // 特化的AST（实际类型取决于是类还是函数）
    int specificity{0};                     // 特化的具体程度（用于排序）
};

}  // namespace polyglot::ir

// Hash function for TemplateInstantiationKey
namespace std {
template <>
struct hash<polyglot::ir::TemplateInstantiationKey> {
    size_t operator()(const polyglot::ir::TemplateInstantiationKey &key) const {
        size_t h = hash<string>()(key.template_name);
        for (const auto &arg : key.arguments) {
            h ^= hash<bool>()(arg.is_type);
            if (arg.is_type) {
                // 简化：使用类型的字符串表示
                h ^= hash<int>()(static_cast<int>(arg.type.kind));
            } else {
                h ^= hash<string>()(arg.value);
            }
        }
        return h;
    }
};
}  // namespace std

namespace polyglot::ir {

// 模板实例化管理器
class TemplateInstantiator {
public:
    // 注册类模板
    void RegisterClassTemplate(const std::string &name,
                               const std::vector<TemplateParameter> &params,
                               void *template_ast) {
        class_templates_[name].params = params;
        class_templates_[name].ast = template_ast;
    }
    
    // 注册函数模板
    void RegisterFunctionTemplate(const std::string &name,
                                  const std::vector<TemplateParameter> &params,
                                  void *template_ast) {
        function_templates_[name].params = params;
        function_templates_[name].ast = template_ast;
    }
    
    // 注册模板特化
    void RegisterSpecialization(const std::string &template_name,
                               const std::vector<TemplateArgument> &pattern,
                               void *specialized_ast) {
        TemplateSpecialization spec;
        spec.pattern = pattern;
        spec.specialized_ast = specialized_ast;
        spec.specificity = CalculateSpecificity(pattern);
        
        specializations_[template_name].push_back(spec);
        
        // 按特化程度排序（越具体越靠前）
        std::sort(specializations_[template_name].begin(),
                 specializations_[template_name].end(),
                 [](const TemplateSpecialization &a, const TemplateSpecialization &b) {
                     return a.specificity > b.specificity;
                 });
    }
    
    // 实例化类模板
    // 返回实例化后的类名（如 "vector<int>"）
    std::string InstantiateClass(const std::string &template_name,
                                 const std::vector<TemplateArgument> &arguments) {
        TemplateInstantiationKey key{template_name, arguments};
        
        // 检查缓存
        auto it = instantiated_classes_.find(key);
        if (it != instantiated_classes_.end()) {
            return it->second;
        }
        
        // 生成实例化类名
        std::string instantiated_name = GenerateInstanceName(template_name, arguments);
        
        // 查找最佳特化
        void *ast = FindBestSpecialization(template_name, arguments);
        if (!ast) {
            // 使用主模板
            auto tmpl_it = class_templates_.find(template_name);
            if (tmpl_it == class_templates_.end()) {
                return "";  // 错误：模板不存在
            }
            ast = tmpl_it->second.ast;
        }
        
        // 执行参数替换并实例化
        // TODO: 实现 AST 遍历和参数替换
        
        // 缓存结果
        instantiated_classes_[key] = instantiated_name;
        
        return instantiated_name;
    }
    
    // 实例化函数模板
    std::string InstantiateFunction(const std::string &template_name,
                                   const std::vector<TemplateArgument> &arguments) {
        TemplateInstantiationKey key{template_name, arguments};
        
        // 检查缓存
        auto it = instantiated_functions_.find(key);
        if (it != instantiated_functions_.end()) {
            return it->second;
        }
        
        // 生成实例化函数名
        std::string instantiated_name = GenerateInstanceName(template_name, arguments);
        
        // 查找模板
        auto tmpl_it = function_templates_.find(template_name);
        if (tmpl_it == function_templates_.end()) {
            return "";  // 错误：模板不存在
        }
        
        // 执行参数替换并实例化
        // TODO: 实现 AST 遍历和参数替换
        
        // 缓存结果
        instantiated_functions_[key] = instantiated_name;
        
        return instantiated_name;
    }
    
    // 类型推导（用于函数模板）
    std::vector<TemplateArgument> DeduceTemplateArguments(
        const std::string &template_name,
        const std::vector<IRType> &argument_types) {
        
        auto tmpl_it = function_templates_.find(template_name);
        if (tmpl_it == function_templates_.end()) {
            return {};
        }
        
        // TODO: 实现模板参数推导算法
        // 1. 匹配函数参数类型
        // 2. 从实参类型推导模板参数
        // 3. 检查约束
        
        return {};
    }

private:
    struct TemplateInfo {
        std::vector<TemplateParameter> params;
        void *ast{nullptr};
    };
    
    std::unordered_map<std::string, TemplateInfo> class_templates_;
    std::unordered_map<std::string, TemplateInfo> function_templates_;
    std::unordered_map<std::string, std::vector<TemplateSpecialization>> specializations_;
    
    std::unordered_map<TemplateInstantiationKey, std::string> instantiated_classes_;
    std::unordered_map<TemplateInstantiationKey, std::string> instantiated_functions_;
    
    // 查找最佳特化
    void* FindBestSpecialization(const std::string &template_name,
                                const std::vector<TemplateArgument> &arguments) {
        auto it = specializations_.find(template_name);
        if (it == specializations_.end()) {
            return nullptr;
        }
        
        for (const auto &spec : it->second) {
            if (MatchesPattern(arguments, spec.pattern)) {
                return spec.specialized_ast;
            }
        }
        
        return nullptr;
    }
    
    // 检查实参是否匹配特化模式
    bool MatchesPattern(const std::vector<TemplateArgument> &arguments,
                       const std::vector<TemplateArgument> &pattern) {
        if (arguments.size() != pattern.size()) return false;
        
        for (size_t i = 0; i < arguments.size(); ++i) {
            const auto &arg = arguments[i];
            const auto &pat = pattern[i];
            
            if (arg.is_type != pat.is_type) return false;
            
            if (arg.is_type) {
                // TODO: 实现更复杂的类型匹配（考虑模板参数、通配符等）
                if (arg.type != pat.type) return false;
            } else {
                if (arg.value != pat.value) return false;
            }
        }
        
        return true;
    }
    
    // 计算特化的具体程度
    int CalculateSpecificity(const std::vector<TemplateArgument> &pattern) {
        int specificity = 0;
        for (const auto &arg : pattern) {
            if (arg.is_type) {
                // 具体类型比模板参数更具体
                if (arg.type.kind != IRTypeKind::kInvalid) {
                    specificity += 10;
                }
            } else {
                // 具体值
                specificity += 10;
            }
        }
        return specificity;
    }
    
    // 生成实例化名称
    std::string GenerateInstanceName(const std::string &template_name,
                                    const std::vector<TemplateArgument> &arguments) {
        std::string name = template_name + "<";
        for (size_t i = 0; i < arguments.size(); ++i) {
            if (i > 0) name += ",";
            
            const auto &arg = arguments[i];
            if (arg.is_type) {
                name += TypeToString(arg.type);
            } else {
                name += arg.value;
            }
        }
        name += ">";
        return name;
    }
    
    // 将类型转换为字符串
    std::string TypeToString(const IRType &type) const {
        switch (type.kind) {
            case IRTypeKind::kI32: return "int";
            case IRTypeKind::kI64: return "long";
            case IRTypeKind::kF64: return "double";
            case IRTypeKind::kStruct: return type.name;
            case IRTypeKind::kPointer:
                if (!type.subtypes.empty()) {
                    return TypeToString(type.subtypes[0]) + "*";
                }
                return "void*";
            default: return "unknown";
        }
    }
};

}  // namespace polyglot::ir
