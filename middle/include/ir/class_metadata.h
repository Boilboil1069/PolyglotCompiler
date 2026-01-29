#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "middle/include/ir/nodes/types.h"
#include "middle/include/ir/nodes/expressions.h"

namespace polyglot::ir {

// 虚函数表项
struct VTableEntry {
    std::string function_name;  // 函数名称
    size_t offset;              // 在 vtable 中的偏移量（以指针大小为单位）
    bool is_pure{false};        // 是否为纯虚函数
};

// 虚函数表
struct VTable {
    std::string class_name;                    // 所属类名
    std::vector<VTableEntry> entries;          // vtable 条目
    std::shared_ptr<GlobalValue> global_var;   // vtable 的全局变量
    
    // 查找虚函数在 vtable 中的偏移
    int FindFunctionOffset(const std::string &func_name) const {
        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].function_name == func_name) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }
};

// 类布局信息
struct ClassLayout {
    std::string class_name;
    IRType struct_type;                         // 类的 IR 结构类型
    std::vector<std::string> field_names;       // 字段名称列表
    std::unordered_map<std::string, size_t> field_offsets;  // 字段名 -> 偏移量
    std::vector<std::string> base_classes;      // 基类列表（按声明顺序）
    std::shared_ptr<VTable> vtable;             // 主 vtable（第一个基类或本类的）
    bool has_vtable{false};                     // 是否包含 vtable 指针
    size_t vtable_offset{0};                    // 主 vtable 指针在对象中的偏移
    
    // 多继承支持：为每个基类保存 vtable 指针偏移
    std::unordered_map<std::string, size_t> base_vtable_offsets;  // 基类名 -> vtable 偏移
    std::unordered_map<std::string, std::shared_ptr<VTable>> base_vtables;  // 基类名 -> vtable
    
    // 虚继承支持：虚基类表（VBTable）
    std::vector<std::string> virtual_bases;      // 虚基类列表
    std::unordered_map<std::string, size_t> virtual_base_offsets;  // 虚基类名 -> 偏移
    bool has_vbtable{false};                     // 是否需要虚基类表
    size_t vbtable_offset{0};                    // 虚基类表指针偏移
    
    // 获取字段偏移
    size_t GetFieldOffset(const std::string &field_name) const {
        auto it = field_offsets.find(field_name);
        if (it != field_offsets.end()) {
            return it->second;
        }
        return static_cast<size_t>(-1);
    }
    
    // 获取指定基类的 vtable 偏移
    size_t GetBaseVTableOffset(const std::string &base_name) const {
        auto it = base_vtable_offsets.find(base_name);
        if (it != base_vtable_offsets.end()) {
            return it->second;
        }
        return static_cast<size_t>(-1);
    }
    
    // 获取虚基类的偏移
    size_t GetVirtualBaseOffset(const std::string &base_name) const {
        auto it = virtual_base_offsets.find(base_name);
        if (it != virtual_base_offsets.end()) {
            return it->second;
        }
        return static_cast<size_t>(-1);
    }
    
    // 检查是否是虚基类
    bool IsVirtualBase(const std::string &base_name) const {
        return std::find(virtual_bases.begin(), virtual_bases.end(), base_name) 
               != virtual_bases.end();
    }
};

// RTTI (运行时类型信息)
struct TypeInfo {
    std::string class_name;
    std::string mangled_name;           // 类型的修饰名称
    std::vector<std::string> base_types;  // 基类列表
    bool has_virtual_functions{false};   // 是否有虚函数
    size_t type_info_offset{0};         // type_info 在 vtable 前的偏移
    
    // type_info 对象的全局变量名
    std::string GetTypeInfoName() const {
        return "__type_info_" + class_name;
    }
};

// 方法信息
struct MethodInfo {
    std::string name;
    std::string mangled_name;   // 修饰后的名称
    IRType return_type;
    std::vector<IRType> param_types;
    bool is_virtual{false};
    bool is_pure_virtual{false};
    bool is_static{false};
    bool is_const{false};
    std::string access;         // public/protected/private
};

// 字段信息
struct FieldInfo {
    std::string name;
    IRType type;
    std::string access;         // public/protected/private
    bool is_static{false};
    bool is_const{false};
    bool is_mutable{false};
};

// 类元数据管理器
class ClassMetadata {
public:
    // 注册类布局
    void RegisterClass(const std::string &name, ClassLayout layout) {
        layouts_[name] = std::move(layout);
    }
    
    // 获取类布局
    const ClassLayout* GetLayout(const std::string &name) const {
        auto it = layouts_.find(name);
        return it != layouts_.end() ? &it->second : nullptr;
    }
    
    ClassLayout* GetLayout(const std::string &name) {
        auto it = layouts_.find(name);
        return it != layouts_.end() ? &it->second : nullptr;
    }
    
    // 注册方法
    void RegisterMethod(const std::string &class_name, MethodInfo method) {
        methods_[class_name].push_back(std::move(method));
    }
    
    // 获取类的所有方法
    const std::vector<MethodInfo>* GetMethods(const std::string &class_name) const {
        auto it = methods_.find(class_name);
        return it != methods_.end() ? &it->second : nullptr;
    }
    
    // 注册字段
    void RegisterField(const std::string &class_name, FieldInfo field) {
        fields_[class_name].push_back(std::move(field));
    }
    
    // 获取类的所有字段
    const std::vector<FieldInfo>* GetFields(const std::string &class_name) const {
        auto it = fields_.find(class_name);
        return it != fields_.end() ? &it->second : nullptr;
    }
    
    // 注册RTTI信息
    void RegisterTypeInfo(const std::string &class_name, TypeInfo type_info) {
        type_infos_[class_name] = std::move(type_info);
    }
    
    // 获取RTTI信息
    const TypeInfo* GetTypeInfo(const std::string &class_name) const {
        auto it = type_infos_.find(class_name);
        return it != type_infos_.end() ? &it->second : nullptr;
    }
    
    // 检查类型转换是否合法（用于dynamic_cast）
    bool IsBaseOf(const std::string &base, const std::string &derived) const {
        if (base == derived) return true;
        
        auto *layout = GetLayout(derived);
        if (!layout) return false;
        
        for (const auto &b : layout->base_classes) {
            if (b == base || IsBaseOf(base, b)) {
                return true;
            }
        }
        
        return false;
    }
    
    // 查找虚函数
    const MethodInfo* FindVirtualMethod(const std::string &class_name, 
                                       const std::string &method_name) const {
        auto it = methods_.find(class_name);
        if (it == methods_.end()) return nullptr;
        
        for (const auto &method : it->second) {
            if (method.name == method_name && method.is_virtual) {
                return &method;
            }
        }
        return nullptr;
    }
    
private:
    std::unordered_map<std::string, ClassLayout> layouts_;
    std::unordered_map<std::string, std::vector<MethodInfo>> methods_;
    std::unordered_map<std::string, std::vector<FieldInfo>> fields_;
    std::unordered_map<std::string, TypeInfo> type_infos_;
};

}  // namespace polyglot::ir
