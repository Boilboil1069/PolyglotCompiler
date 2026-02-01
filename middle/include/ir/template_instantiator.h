#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include "middle/include/ir/nodes/types.h"

namespace polyglot::ir {

// Template parameter
struct TemplateParameter {
    std::string name;          // Parameter name
    bool is_typename{true};    // true: typename/class, false: non-type parameter
    IRType type;               // Type for non-type parameters
    std::string default_value; // Default value
};

// Template argument
struct TemplateArgument {
    bool is_type{true};        // true: type argument, false: non-type argument
    IRType type;               // Type argument
    std::string value;         // Value for non-type argument
};

// Template instantiation key (for caching)
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

// Template specialization information
struct TemplateSpecialization {
    std::vector<TemplateArgument> pattern;  // Specialization pattern
    void *specialized_ast{nullptr};         // Specialized AST (type depends on class vs function)
    int specificity{0};                     // How specific this specialization is (for sorting)
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
                // Simplified: use the type's string representation
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

// Template instantiation manager
class TemplateInstantiator {
public:
    // Register a class template
    void RegisterClassTemplate(const std::string &name,
                               const std::vector<TemplateParameter> &params,
                               void *template_ast) {
        class_templates_[name].params = params;
        class_templates_[name].ast = template_ast;
    }
    
    // Register a function template
    void RegisterFunctionTemplate(const std::string &name,
                                  const std::vector<TemplateParameter> &params,
                                  void *template_ast) {
        function_templates_[name].params = params;
        function_templates_[name].ast = template_ast;
    }
    
    // Register a template specialization
    void RegisterSpecialization(const std::string &template_name,
                               const std::vector<TemplateArgument> &pattern,
                               void *specialized_ast) {
        TemplateSpecialization spec;
        spec.pattern = pattern;
        spec.specialized_ast = specialized_ast;
        spec.specificity = CalculateSpecificity(pattern);
        
        specializations_[template_name].push_back(spec);
        
        // Sort by how specific each specialization is (more specific first)
        std::sort(specializations_[template_name].begin(),
                 specializations_[template_name].end(),
                 [](const TemplateSpecialization &a, const TemplateSpecialization &b) {
                     return a.specificity > b.specificity;
                 });
    }
    
    // Instantiate a class template
    // Returns the instantiated class name (e.g., "vector<int>")
    std::string InstantiateClass(const std::string &template_name,
                                 const std::vector<TemplateArgument> &arguments) {
        TemplateInstantiationKey key{template_name, arguments};
        
        // Check cache
        auto it = instantiated_classes_.find(key);
        if (it != instantiated_classes_.end()) {
            return it->second;
        }
        
        // Generate instantiated class name
        std::string instantiated_name = GenerateInstanceName(template_name, arguments);
        
        // Find the best specialization
        void *ast = FindBestSpecialization(template_name, arguments);
        if (!ast) {
            // Use the primary template
            auto tmpl_it = class_templates_.find(template_name);
            if (tmpl_it == class_templates_.end()) {
                return "";  // Error: template does not exist
            }
            ast = tmpl_it->second.ast;
        }
        
        // Perform parameter substitution and instantiate
        // TODO: Implement AST traversal and parameter substitution
        
        // Cache the result
        instantiated_classes_[key] = instantiated_name;
        
        return instantiated_name;
    }
    
    // Instantiate a function template
    std::string InstantiateFunction(const std::string &template_name,
                                   const std::vector<TemplateArgument> &arguments) {
        TemplateInstantiationKey key{template_name, arguments};
        
        // Check cache
        auto it = instantiated_functions_.find(key);
        if (it != instantiated_functions_.end()) {
            return it->second;
        }
        
        // Generate instantiated function name
        std::string instantiated_name = GenerateInstanceName(template_name, arguments);
        
        // Find the template
        auto tmpl_it = function_templates_.find(template_name);
        if (tmpl_it == function_templates_.end()) {
            return "";  // Error: template does not exist
        }
        
        // Perform parameter substitution and instantiate
        // TODO: Implement AST traversal and parameter substitution
        
        // Cache the result
        instantiated_functions_[key] = instantiated_name;
        
        return instantiated_name;
    }
    
    // Type deduction (for function templates)
    std::vector<TemplateArgument> DeduceTemplateArguments(
        const std::string &template_name,
        const std::vector<IRType> &argument_types) {
        
        auto tmpl_it = function_templates_.find(template_name);
        if (tmpl_it == function_templates_.end()) {
            return {};
        }
        
        // TODO: Implement template parameter deduction
        // 1. Match function parameter types
        // 2. Deduce template parameters from argument types
        // 3. Check constraints
        
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
    
    // Find the best specialization
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
    
    // Check whether arguments match a specialization pattern
    bool MatchesPattern(const std::vector<TemplateArgument> &arguments,
                       const std::vector<TemplateArgument> &pattern) {
        if (arguments.size() != pattern.size()) return false;
        
        for (size_t i = 0; i < arguments.size(); ++i) {
            const auto &arg = arguments[i];
            const auto &pat = pattern[i];
            
            if (arg.is_type != pat.is_type) return false;
            
            if (arg.is_type) {
                // TODO: Implement more complex type matching (template params, wildcards, etc.)
                if (arg.type != pat.type) return false;
            } else {
                if (arg.value != pat.value) return false;
            }
        }
        
        return true;
    }
    
    // Compute how specific a specialization is
    int CalculateSpecificity(const std::vector<TemplateArgument> &pattern) {
        int specificity = 0;
        for (const auto &arg : pattern) {
            if (arg.is_type) {
                // Concrete types are more specific than template parameters
                if (arg.type.kind != IRTypeKind::kInvalid) {
                    specificity += 10;
                }
            } else {
                // Concrete value
                specificity += 10;
            }
        }
        return specificity;
    }
    
    // Generate the instantiated name
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
    
    // Convert a type to its string representation
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
