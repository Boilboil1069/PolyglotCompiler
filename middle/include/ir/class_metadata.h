#pragma once

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "middle/include/ir/nodes/types.h"
#include "middle/include/ir/nodes/expressions.h"

namespace polyglot::ir {

// Virtual function table entry
struct VTableEntry {
    std::string function_name;  // Function name
    size_t offset;              // Offset in the vtable (in pointer-sized units)
    bool is_pure{false};        // Whether this is a pure virtual function
};

// Virtual function table
struct VTable {
    std::string class_name;                    // Owning class name
    std::vector<VTableEntry> entries;          // Vtable entries
    std::shared_ptr<GlobalValue> global_var;   // Global variable holding the vtable
    
    // Find the offset of a virtual function within the vtable
    int FindFunctionOffset(const std::string &func_name) const {
        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].function_name == func_name) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }
};

// Class layout information
struct ClassLayout {
    std::string class_name;
    IRType struct_type;                         // IR struct type of the class
    std::vector<std::string> field_names;       // List of field names
    std::unordered_map<std::string, size_t> field_offsets;  // Field name -> offset
    std::vector<std::string> base_classes;      // Base classes (declaration order)
    std::shared_ptr<VTable> vtable;             // Primary vtable (first base or this class)
    bool has_vtable{false};                     // Whether the object holds a vtable pointer
    size_t vtable_offset{0};                    // Offset of the primary vtable pointer in the object
    
    // Multiple inheritance support: store vtable pointer offset for each base class
    std::unordered_map<std::string, size_t> base_vtable_offsets;  // Base class name -> vtable offset
    std::unordered_map<std::string, std::shared_ptr<VTable>> base_vtables;  // Base class name -> vtable
    
    // Virtual inheritance support: virtual base table (VBTable)
    std::vector<std::string> virtual_bases;      // List of virtual base classes
    std::unordered_map<std::string, size_t> virtual_base_offsets;  // Virtual base name -> offset
    bool has_vbtable{false};                     // Whether a VBTable is needed
    size_t vbtable_offset{0};                    // Offset of the VBTable pointer
    
    // Get a field offset
    size_t GetFieldOffset(const std::string &field_name) const {
        auto it = field_offsets.find(field_name);
        if (it != field_offsets.end()) {
            return it->second;
        }
        return static_cast<size_t>(-1);
    }
    
    // Get the vtable offset for a specific base class
    size_t GetBaseVTableOffset(const std::string &base_name) const {
        auto it = base_vtable_offsets.find(base_name);
        if (it != base_vtable_offsets.end()) {
            return it->second;
        }
        return static_cast<size_t>(-1);
    }
    
    // Get the offset for a virtual base class
    size_t GetVirtualBaseOffset(const std::string &base_name) const {
        auto it = virtual_base_offsets.find(base_name);
        if (it != virtual_base_offsets.end()) {
            return it->second;
        }
        return static_cast<size_t>(-1);
    }
    
    // Check if this is a virtual base class
    bool IsVirtualBase(const std::string &base_name) const {
        return std::find(virtual_bases.begin(), virtual_bases.end(), base_name) 
               != virtual_bases.end();
    }
};

// RTTI (runtime type information)
struct TypeInfo {
    std::string class_name;
    std::string mangled_name;           // Mangled type name
    std::vector<std::string> base_types;  // List of base classes
    bool has_virtual_functions{false};   // Whether the type declares virtual functions
    size_t type_info_offset{0};         // Offset of type_info before the vtable
    
    // Global variable name for the type_info object
    std::string GetTypeInfoName() const {
        return "__type_info_" + class_name;
    }
};

// Method information
struct MethodInfo {
    std::string name;
    std::string mangled_name;   // Mangled name
    IRType return_type;
    std::vector<IRType> param_types;
    bool is_virtual{false};
    bool is_pure_virtual{false};
    bool is_static{false};
    bool is_const{false};
    bool is_final{false};       // Method is final and cannot be overridden
    bool is_override{false};    // Method overrides a base class method
    std::string access;         // public/protected/private
};

// Field information
struct FieldInfo {
    std::string name;
    IRType type;
    std::string access;         // public/protected/private
    bool is_static{false};
    bool is_const{false};
    bool is_mutable{false};
};

// Class metadata manager
class ClassMetadata {
public:
    // Register a class layout
    void RegisterClass(const std::string &name, ClassLayout layout) {
        layouts_[name] = std::move(layout);
    }
    
    // Get class layout
    const ClassLayout* GetLayout(const std::string &name) const {
        auto it = layouts_.find(name);
        return it != layouts_.end() ? &it->second : nullptr;
    }
    
    ClassLayout* GetLayout(const std::string &name) {
        auto it = layouts_.find(name);
        return it != layouts_.end() ? &it->second : nullptr;
    }
    
    // Register a method
    void RegisterMethod(const std::string &class_name, MethodInfo method) {
        methods_[class_name].push_back(std::move(method));
    }
    
    // Get all methods of a class
    const std::vector<MethodInfo>* GetMethods(const std::string &class_name) const {
        auto it = methods_.find(class_name);
        return it != methods_.end() ? &it->second : nullptr;
    }
    
    // Register a field
    void RegisterField(const std::string &class_name, FieldInfo field) {
        fields_[class_name].push_back(std::move(field));
    }
    
    // Get all fields of a class
    const std::vector<FieldInfo>* GetFields(const std::string &class_name) const {
        auto it = fields_.find(class_name);
        return it != fields_.end() ? &it->second : nullptr;
    }
    
    // Register RTTI information
    void RegisterTypeInfo(const std::string &class_name, TypeInfo type_info) {
        type_infos_[class_name] = std::move(type_info);
    }
    
    // Get RTTI information
    const TypeInfo* GetTypeInfo(const std::string &class_name) const {
        auto it = type_infos_.find(class_name);
        return it != type_infos_.end() ? &it->second : nullptr;
    }
    
    // Check whether a type conversion is valid (for dynamic_cast)
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
    
    // Find a virtual method
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
