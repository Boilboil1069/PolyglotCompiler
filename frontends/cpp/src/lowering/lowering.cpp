#include "frontends/cpp/include/cpp_lowering.h"

#include <cstdlib>
#include <optional>
#include <string>
#include <unordered_map>

#include "common/include/core/types.h"
#include "common/include/ir/ir_builder.h"
#include "common/include/ir/ir_printer.h"
#include "middle/include/ir/class_metadata.h"
#include "middle/include/ir/template_instantiator.h"

namespace polyglot::cpp {
namespace {

using Name = std::string;

// Type helper functions remain unchanged.

ir::IRType ToIRType(const core::Type &t) {
    using Kind = core::TypeKind;
    switch (t.kind) {
        case Kind::kInt: return ir::IRType::I64(true);
        case Kind::kFloat: return ir::IRType::F64();
        case Kind::kBool: return ir::IRType::I1();
        case Kind::kVoid: return ir::IRType::Void();
        case Kind::kPointer:
            if (!t.type_args.empty()) return ir::IRType::Pointer(ToIRType(t.type_args[0]));
            return ir::IRType::Pointer(ir::IRType::Invalid());
        case Kind::kReference:
            if (!t.type_args.empty()) return ir::IRType::Reference(ToIRType(t.type_args[0]));
            return ir::IRType::Reference(ir::IRType::Invalid());
        default:
            return ir::IRType::Invalid();
    }
}

ir::IRType ToIRType(const std::shared_ptr<TypeNode> &node) {
    if (!node) return ir::IRType::I64(true);
    if (auto simple = std::dynamic_pointer_cast<SimpleType>(node)) {
        core::Type ct = core::TypeSystem().MapFromLanguage("cpp", simple->name);
        return ToIRType(ct);
    }
    if (auto ptr = std::dynamic_pointer_cast<PointerType>(node)) {
        return ir::IRType::Pointer(ToIRType(ptr->pointee));
    }
    if (auto ref = std::dynamic_pointer_cast<ReferenceType>(node)) {
        return ir::IRType::Reference(ToIRType(ref->referent));
    }
    return ir::IRType::Invalid();
}

struct EnvEntry {
    Name value;
    ir::IRType type{ir::IRType::Invalid()};
};

struct LoweringContext {
    ir::IRContext &ir_ctx;
    frontends::Diagnostics &diags;
    std::unordered_map<Name, EnvEntry> env;
    ir::ClassMetadata class_metadata;     // Class metadata management
    ir::TemplateInstantiator template_instantiator;  // Template instantiation management
    std::string current_class;            // Current class being lowered
    ir::IRBuilder builder;
    std::shared_ptr<ir::Function> fn;
    bool terminated{false};

    LoweringContext(ir::IRContext &ctx, frontends::Diagnostics &d)
        : ir_ctx(ctx), diags(d), builder(ctx) {}
};

struct EvalResult {
    Name value;
    ir::IRType type{ir::IRType::Invalid()};
};

bool IsIntegerLiteral(const std::string &text, long long *out) {
    char *end = nullptr;
    long long v = std::strtoll(text.c_str(), &end, 0);
    if (end == text.c_str() || *end != '\0') return false;
    if (out) *out = v;
    return true;
}

bool IsFloatLiteral(const std::string &text, double *out) {
    char *end = nullptr;
    double v = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || (*end != '\0' && *end != 'f' && *end != 'F')) return false;
    if (out) *out = v;
    return true;
}

EvalResult EvalExpr(const std::shared_ptr<Expression> &expr, LoweringContext &lc);

EvalResult MakeLiteral(long long v, LoweringContext &lc) {
    (void)lc;
    return {std::to_string(v), ir::IRType::I64(true)};
}

EvalResult MakeFloatLiteral(double v, LoweringContext &lc) {
    auto lit = lc.builder.MakeLiteral(v);
    return {lit->name, ir::IRType::F64()};
}

// ==================== Member access and virtual calls ====================

// Handle subscript/operator[] overloads
EvalResult EvalIndexAccess(const std::shared_ptr<IndexExpression> &idx, LoweringContext &lc) {
    auto obj = EvalExpr(idx->object, lc);
    auto index = EvalExpr(idx->index, lc);
    
    if (obj.type.kind == ir::IRTypeKind::kInvalid || 
        index.type.kind == ir::IRTypeKind::kInvalid) {
        return {};
    }
    
    // Check whether this is a class type (which may overload operator[])
    std::string class_name;
    if (obj.type.kind == ir::IRTypeKind::kStruct) {
        class_name = obj.type.name;
    } else if (obj.type.kind == ir::IRTypeKind::kPointer && 
               !obj.type.subtypes.empty() && 
               obj.type.subtypes[0].kind == ir::IRTypeKind::kStruct) {
        class_name = obj.type.subtypes[0].name;
    }
    
    // If it is a class type, look for an operator[] overload
    if (!class_name.empty()) {
        auto *methods = lc.class_metadata.GetMethods(class_name);
        if (methods) {
            for (const auto &method : *methods) {
                if (method.name == "operator[]") {
                    // Call the overloaded operator[]
                    std::vector<std::string> args = {obj.value, index.value};
                    auto call = lc.builder.MakeCall(method.mangled_name, args, 
                                                    method.return_type, "");
                    return {call->name, call->type};
                }
            }
        }
    }
    
    // Plain array access or pointer arithmetic
    if (obj.type.kind == ir::IRTypeKind::kPointer) {
        // Compute element type
        ir::IRType elem_type = ir::IRType::I64(true);  // default fallback
        if (!obj.type.subtypes.empty()) {
            elem_type = obj.type.subtypes[0];
        }
        
        // Use dynamic GEP for runtime-computed array indexing
        // This generates: ptr_elem = base + index * sizeof(elem_type)
        auto ptr_inst = lc.builder.MakeDynamicGEP(obj.value, elem_type, index.value, "arr_ptr");
        
        // Load the element value from the computed pointer
        auto load_inst = lc.builder.MakeLoad(ptr_inst->name, elem_type, "arr_elem");
        
        return {load_inst->name, elem_type};
    }
    
    lc.diags.Report(idx->loc, "Subscript operator requires array or class with operator[]");
    return {};
}

// Handle new expressions
EvalResult EvalNew(const std::shared_ptr<NewExpression> &new_expr, LoweringContext &lc) {
    // Compute the allocated type
    ir::IRType alloc_type = ToIRType(new_expr->type);
    if (alloc_type.kind == ir::IRTypeKind::kInvalid) {
        lc.diags.Report(new_expr->loc, "Invalid type for new expression");
        return {};
    }
    
    // Handle array new
    if (new_expr->is_array) {
        if (new_expr->args.empty()) {
            lc.diags.Report(new_expr->loc, "Array new requires size argument");
            return {};
        }
        
        auto size_result = EvalExpr(new_expr->args[0], lc);
        if (size_result.type.kind == ir::IRTypeKind::kInvalid) return {};
        
        // Call __builtin_new_array(size, element_size)
        // Simplified: assume element_size is fixed
        std::vector<std::string> args = {size_result.value, "8"};  // assume 8 bytes per element
        auto call = lc.builder.MakeCall("__builtin_new_array", args, 
                                       ir::IRType::Pointer(alloc_type), "");
        return {call->name, call->type};
    }
    
    // Single-object new
    // 1. Call __builtin_new to allocate memory
    auto ptr_type = ir::IRType::Pointer(alloc_type);
    auto alloc_call = lc.builder.MakeCall("__builtin_new", {}, ptr_type, "");
    
    // 2. If this is a class type, call its constructor
    std::string class_name;
    if (alloc_type.kind == ir::IRTypeKind::kStruct) {
        class_name = alloc_type.name;
        
        // Check whether a constructor exists
        auto *layout = lc.class_metadata.GetLayout(class_name);
        if (layout) {
            // 3. Initialize the vtable pointer (if present)
            if (layout->has_vtable && layout->vtable) {
                // Generate a GEP to access the __vptr field (offset 0)
                auto vptr_gep = lc.builder.MakeGEP(alloc_call->name, ptr_type, {0, 0});
                
                // Fetch the vtable global variable address
                std::string vtable_name = "__vtable_" + class_name;
                
                // Store the vtable pointer
                lc.builder.MakeStore(vtable_name, vptr_gep->name);
            }
            
            // 4. Call the constructor (if present)
            std::string ctor_name = class_name + "::" + class_name;  // ClassName::ClassName
            
            // Prepare constructor arguments: this pointer + user-provided args
            std::vector<std::string> ctor_args = {alloc_call->name};
            for (const auto &arg : new_expr->args) {
                auto arg_result = EvalExpr(arg, lc);
                if (arg_result.type.kind == ir::IRTypeKind::kInvalid) return {};
                ctor_args.push_back(arg_result.value);
            }
            
            // Invoke the constructor without checking existence here; lowering handles resolution
            lc.builder.MakeCall(ctor_name, ctor_args, ir::IRType::Void(), "");
        }
    }
    
    return {alloc_call->name, ptr_type};
}

// Handle delete expressions
EvalResult EvalDelete(const std::shared_ptr<DeleteExpression> &del_expr, LoweringContext &lc) {
    auto obj = EvalExpr(del_expr->operand, lc);
    if (obj.type.kind == ir::IRTypeKind::kInvalid) {
        return {};
    }
    
    if (obj.type.kind != ir::IRTypeKind::kPointer) {
        lc.diags.Report(del_expr->loc, "Delete requires pointer type");
        return {};
    }
    
    // Handle array delete
    if (del_expr->is_array) {
        lc.builder.MakeCall("__builtin_delete_array", {obj.value}, ir::IRType::Void(), "");
        return {obj.value, ir::IRType::Void()};
    }
    
    // Single-object delete
    // 1. If this is a class type, call the destructor
    if (!obj.type.subtypes.empty() && obj.type.subtypes[0].kind == ir::IRTypeKind::kStruct) {
        std::string class_name = obj.type.subtypes[0].name;
        std::string dtor_name = class_name + "::~" + class_name;
        
        // Call the destructor
        lc.builder.MakeCall(dtor_name, {obj.value}, ir::IRType::Void(), "");
    }
    
    // 2. Call __builtin_delete to free memory
    lc.builder.MakeCall("__builtin_delete", {obj.value}, ir::IRType::Void(), "");
    
    return {obj.value, ir::IRType::Void()};
}

// Handle typeid expressions
EvalResult EvalTypeid(const std::shared_ptr<TypeidExpression> &typeid_expr, LoweringContext &lc) {
    std::string class_name;
    
    if (typeid_expr->is_type) {
        // typeid(Type)
        // Extract the class name from the type node
        if (auto simple = std::dynamic_pointer_cast<SimpleType>(typeid_expr->type_arg)) {
            class_name = simple->name;
        } else {
            lc.diags.Report(typeid_expr->loc, "typeid requires class type");
            return {};
        }
    } else {
        // typeid(expression)
        // Evaluate the expression and obtain its type
        auto val = EvalExpr(typeid_expr->expr_arg, lc);
        if (val.type.kind == ir::IRTypeKind::kInvalid) {
            return {};
        }
        
        // Extract the class name
        if (val.type.kind == ir::IRTypeKind::kStruct) {
            class_name = val.type.name;
        } else if (val.type.kind == ir::IRTypeKind::kPointer &&
                   !val.type.subtypes.empty() &&
                   val.type.subtypes[0].kind == ir::IRTypeKind::kStruct) {
            class_name = val.type.subtypes[0].name;
        } else {
            lc.diags.Report(typeid_expr->loc, "typeid requires class type");
            return {};
        }
    }
    
    // Retrieve the type_info object
    auto *type_info = lc.class_metadata.GetTypeInfo(class_name);
    if (!type_info) {
        // Register now if it has not been recorded yet
        ir::TypeInfo new_info;
        new_info.class_name = class_name;
        new_info.mangled_name = "_ZTI" + std::to_string(class_name.length()) + class_name;
        
        auto *layout = lc.class_metadata.GetLayout(class_name);
        if (layout) {
            new_info.base_types = layout->base_classes;
            new_info.has_virtual_functions = layout->has_vtable;
        }
        
        lc.class_metadata.RegisterTypeInfo(class_name, new_info);
        type_info = lc.class_metadata.GetTypeInfo(class_name);
    }
    
    // Return a pointer to the type_info
    std::string type_info_name = type_info->GetTypeInfoName();
    
    // Create a type_info type (simplified to a pointer type)
    ir::IRType type_info_ptr = ir::IRType::Pointer(ir::IRType::I8());
    
    return {type_info_name, type_info_ptr};
}

// Handle dynamic_cast expressions
EvalResult EvalDynamicCast(const std::shared_ptr<DynamicCastExpression> &cast_expr, LoweringContext &lc) {
    // Get the source object
    auto src = EvalExpr(cast_expr->operand, lc);
    if (src.type.kind == ir::IRTypeKind::kInvalid) {
        return {};
    }
    
    // Determine the target type
    ir::IRType target_type = ToIRType(cast_expr->target_type);
    if (target_type.kind == ir::IRTypeKind::kInvalid) {
        lc.diags.Report(cast_expr->loc, "Invalid target type for dynamic_cast");
        return {};
    }
    
    // Extract class names for source and target types
    std::string src_class, target_class;
    
    // The source type should be a pointer or reference
    if (src.type.kind == ir::IRTypeKind::kPointer) {
        if (!src.type.subtypes.empty() && src.type.subtypes[0].kind == ir::IRTypeKind::kStruct) {
            src_class = src.type.subtypes[0].name;
        }
    } else if (src.type.kind == ir::IRTypeKind::kStruct) {
        src_class = src.type.name;
    }
    
    if (target_type.kind == ir::IRTypeKind::kPointer) {
        if (!target_type.subtypes.empty() && target_type.subtypes[0].kind == ir::IRTypeKind::kStruct) {
            target_class = target_type.subtypes[0].name;
        }
    } else if (target_type.kind == ir::IRTypeKind::kStruct) {
        target_class = target_type.name;
    }
    
    if (src_class.empty() || target_class.empty()) {
        lc.diags.Report(cast_expr->loc, "dynamic_cast requires class types");
        return {};
    }
    
    // Check the inheritance relationship
    bool is_upcast = lc.class_metadata.IsBaseOf(target_class, src_class);    // upcast
    bool is_downcast = lc.class_metadata.IsBaseOf(src_class, target_class);  // downcast
    
    if (!is_upcast && !is_downcast) {
        lc.diags.Report(cast_expr->loc, "Invalid dynamic_cast: no inheritance relationship");
        return {};
    }
    
    // Upcasts (derived → base) are known-safe at compile time
    if (is_upcast) {
        // Simplified: return the source pointer (real impl would adjust pointer offset)
        return {src.value, target_type};
    }
    
    // Downcasts (base → derived) require runtime checks
    // Call the runtime helper __dynamic_cast_check
    std::vector<std::string> args = {
        src.value,                                // source object pointer
        "\"" + src_class + "\"",                  // source type name
        "\"" + target_class + "\""                // target type name
    };
    
    auto result = lc.builder.MakeCall(
        "__dynamic_cast_check",
        args,
        target_type,
        "dyn_cast_result"
    );
    
    return {result->name, target_type};
}

// Handle static_cast expressions
EvalResult EvalStaticCast(const std::shared_ptr<StaticCastExpression> &cast_expr, LoweringContext &lc) {
    // Get the source object
    auto src = EvalExpr(cast_expr->operand, lc);
    if (src.type.kind == ir::IRTypeKind::kInvalid) {
        return {};
    }
    
    // Determine the target type
    ir::IRType target_type = ToIRType(cast_expr->target_type);
    if (target_type.kind == ir::IRTypeKind::kInvalid) {
        lc.diags.Report(cast_expr->loc, "Invalid target type for static_cast");
        return {};
    }
    
    // If source and target types are the same, no conversion needed
    if (src.type == target_type) {
        return {src.value, target_type};
    }
    
    // Determine the appropriate cast operation based on source and target types
    ir::CastInstruction::CastKind cast_kind;
    bool needs_cast = true;
    
    // Integer to integer conversions
    if (src.type.IsInteger() && target_type.IsInteger()) {
        int src_bits = src.type.BitWidth();
        int dst_bits = target_type.BitWidth();
        
        if (dst_bits > src_bits) {
            // Widening conversion: use sign extension or zero extension
            cast_kind = src.type.is_signed ? 
                ir::CastInstruction::CastKind::kSExt : 
                ir::CastInstruction::CastKind::kZExt;
        } else if (dst_bits < src_bits) {
            // Narrowing conversion: truncate
            cast_kind = ir::CastInstruction::CastKind::kTrunc;
        } else {
            // Same size, just a type reinterpretation (e.g., signed to unsigned)
            cast_kind = ir::CastInstruction::CastKind::kBitcast;
        }
    }
    // Float to float conversions
    else if (src.type.IsFloat() && target_type.IsFloat()) {
        int src_bits = src.type.BitWidth();
        int dst_bits = target_type.BitWidth();
        
        if (dst_bits > src_bits) {
            // float to double
            cast_kind = ir::CastInstruction::CastKind::kFpExt;
        } else if (dst_bits < src_bits) {
            // double to float
            cast_kind = ir::CastInstruction::CastKind::kFpTrunc;
        } else {
            needs_cast = false;  // Same float type
        }
    }
    // Integer to float conversions
    else if (src.type.IsInteger() && target_type.IsFloat()) {
        // Use runtime conversion via call to __builtin_sitofp or __builtin_uitofp
        std::string intrinsic = src.type.is_signed ? "__builtin_sitofp" : "__builtin_uitofp";
        auto call = lc.builder.MakeCall(intrinsic, {src.value}, target_type, "i2f");
        return {call->name, target_type};
    }
    // Float to integer conversions
    else if (src.type.IsFloat() && target_type.IsInteger()) {
        // Use runtime conversion via call to __builtin_fptosi or __builtin_fptoui
        std::string intrinsic = target_type.is_signed ? "__builtin_fptosi" : "__builtin_fptoui";
        auto call = lc.builder.MakeCall(intrinsic, {src.value}, target_type, "f2i");
        return {call->name, target_type};
    }
    // Pointer to integer conversions (ptr to int)
    else if (src.type.kind == ir::IRTypeKind::kPointer && target_type.IsInteger()) {
        cast_kind = ir::CastInstruction::CastKind::kPtrToInt;
    }
    // Integer to pointer conversions (int to ptr)
    else if (src.type.IsInteger() && target_type.kind == ir::IRTypeKind::kPointer) {
        cast_kind = ir::CastInstruction::CastKind::kIntToPtr;
    }
    // Pointer to pointer conversions (reinterpret as different pointer type)
    else if (src.type.kind == ir::IRTypeKind::kPointer && 
             target_type.kind == ir::IRTypeKind::kPointer) {
        // Pointer-to-pointer cast is just a bitcast
        cast_kind = ir::CastInstruction::CastKind::kBitcast;
    }
    // Class type conversions (up/down casts in inheritance hierarchy)
    else if ((src.type.kind == ir::IRTypeKind::kPointer || 
              src.type.kind == ir::IRTypeKind::kStruct) &&
             (target_type.kind == ir::IRTypeKind::kPointer || 
              target_type.kind == ir::IRTypeKind::kStruct)) {
        // For class types, static_cast performs compile-time checked casts
        // In a simplified model, this is just pointer reinterpretation
        cast_kind = ir::CastInstruction::CastKind::kBitcast;
    }
    // Reference conversions
    else if (src.type.kind == ir::IRTypeKind::kReference || 
             target_type.kind == ir::IRTypeKind::kReference) {
        // Reference casts are similar to pointer casts
        cast_kind = ir::CastInstruction::CastKind::kBitcast;
    }
    // Boolean conversions (to bool)
    else if (target_type.kind == ir::IRTypeKind::kI1) {
        // Convert any type to bool: compare against zero
        auto zero_lit = lc.builder.MakeLiteral(0LL);
        auto cmp = lc.builder.MakeBinary(ir::BinaryInstruction::Op::kCmpNe, 
                                        src.value, zero_lit->name, "tobool");
        return {cmp->name, target_type};
    }
    // From boolean to other integer types
    else if (src.type.kind == ir::IRTypeKind::kI1 && target_type.IsInteger()) {
        // bool to int: zero-extend
        cast_kind = ir::CastInstruction::CastKind::kZExt;
    }
    else {
        // Unknown conversion - use bitcast as fallback
        cast_kind = ir::CastInstruction::CastKind::kBitcast;
    }
    
    if (!needs_cast) {
        return {src.value, target_type};
    }
    
    // Generate the cast instruction
    auto cast_inst = lc.builder.MakeCast(cast_kind, src.value, target_type, "cast");
    return {cast_inst->name, target_type};
}

// Handle member access expressions
EvalResult EvalMemberAccess(const std::shared_ptr<MemberExpression> &mem, LoweringContext &lc) {
    // Evaluate the object expression
    auto obj = EvalExpr(mem->object, lc);
    if (obj.type.kind == ir::IRTypeKind::kInvalid) {
        return {};
    }
    
    // Obtain the object's type name
    std::string class_name;
    if (obj.type.kind == ir::IRTypeKind::kPointer || obj.type.kind == ir::IRTypeKind::kReference) {
        if (!obj.type.subtypes.empty() && obj.type.subtypes[0].kind == ir::IRTypeKind::kStruct) {
            class_name = obj.type.subtypes[0].name;
        }
    } else if (obj.type.kind == ir::IRTypeKind::kStruct) {
        class_name = obj.type.name;
    }
    
    if (class_name.empty()) {
        lc.diags.Report(mem->loc, "Member access on non-class type");
        return {};
    }
    
    // Look up the class layout
    auto *layout = lc.class_metadata.GetLayout(class_name);
    if (!layout) {
        lc.diags.Report(mem->loc, "Unknown class: " + class_name);
        return {};
    }
    
    // Compute the field offset
    size_t field_offset = layout->GetFieldOffset(mem->member);
    if (field_offset == static_cast<size_t>(-1)) {
        lc.diags.Report(mem->loc, "Unknown field: " + mem->member);
        return {};
    }
    
    // Access control
    // Find the field's access specifier
    std::string field_access = "public";  // default to public
    auto *fields = lc.class_metadata.GetFields(class_name);
    if (fields) {
        for (const auto &field : *fields) {
            if (field.name == mem->member) {
                field_access = field.access;
                break;
            }
        }
    }
    
    // Check access permissions
    bool access_allowed = true;
    if (field_access == "private") {
        // private: only this class may access
        if (lc.current_class != class_name) {
            access_allowed = false;
        }
    } else if (field_access == "protected") {
        // protected: this class and derived classes may access
        if (lc.current_class != class_name) {
            // Check whether lc.current_class derives from class_name
            auto *current_layout = lc.class_metadata.GetLayout(lc.current_class);
            bool is_derived = false;
            if (current_layout) {
                for (const auto &base : current_layout->base_classes) {
                    if (base == class_name) {
                        is_derived = true;
                        break;
                    }
                }
            }
            if (!is_derived) {
                access_allowed = false;
            }
        }
    }
    // public: always allowed
    
    if (!access_allowed) {
        lc.diags.Report(mem->loc, "Cannot access " + field_access + " member '" + 
                       mem->member + "' of class '" + class_name + "'");
        return {};
    }
    
    // Generate a GEP to access the field
    std::string ptr_value = obj.value;
    
    // If this is an arrow access or already a pointer, use it directly
    // Otherwise the address must be taken
    bool is_ptr = (obj.type.kind == ir::IRTypeKind::kPointer) || mem->is_arrow;
    
    if (!is_ptr) {
        // Need the object address (simplified: assume it lives on the stack)
        // A real implementation should use alloca
        lc.diags.Report(mem->loc, "Member access on non-pointer not fully implemented");
        return {};
    }
    
    // Use GEP to reach the field
    auto gep = lc.builder.MakeGEP(ptr_value, obj.type, {0, field_offset});
    
    // Get the field type from the class layout
    ir::IRType field_type = ir::IRType::I64(true);  // default
    if (field_offset < layout->struct_type.subtypes.size()) {
        field_type = layout->struct_type.subtypes[field_offset];
    } else {
        lc.diags.Report(mem->loc, "Field offset out of bounds for type " + class_name);
        return {};
    }
    
    auto load = lc.builder.MakeLoad(gep->name, field_type);
    
    return {load->name, field_type};
}

EvalResult EvalIdentifier(const std::shared_ptr<Identifier> &id, LoweringContext &lc) {
    auto it = lc.env.find(id->name);
    if (it == lc.env.end()) {
        lc.diags.Report(id->loc, "Undefined identifier: " + id->name);
        return {};
    }
    return {it->second.value, it->second.type};
}

ir::BinaryInstruction::Op MapBinOp(const std::string &op, bool is_float) {
    if (is_float) {
        if (op == "+") return ir::BinaryInstruction::Op::kFAdd;
        if (op == "-") return ir::BinaryInstruction::Op::kFSub;
        if (op == "*") return ir::BinaryInstruction::Op::kFMul;
        if (op == "/") return ir::BinaryInstruction::Op::kFDiv;
        if (op == "%") return ir::BinaryInstruction::Op::kFRem;
        if (op == "==") return ir::BinaryInstruction::Op::kCmpFoe;
        if (op == "!=") return ir::BinaryInstruction::Op::kCmpFne;
        if (op == "<") return ir::BinaryInstruction::Op::kCmpFlt;
        if (op == "<=") return ir::BinaryInstruction::Op::kCmpFle;
        if (op == ">") return ir::BinaryInstruction::Op::kCmpFgt;
        if (op == ">=") return ir::BinaryInstruction::Op::kCmpFge;
        return ir::BinaryInstruction::Op::kFAdd;
    }
    if (op == "+") return ir::BinaryInstruction::Op::kAdd;
    if (op == "-") return ir::BinaryInstruction::Op::kSub;
    if (op == "*") return ir::BinaryInstruction::Op::kMul;
    if (op == "/") return ir::BinaryInstruction::Op::kSDiv;
    if (op == "%") return ir::BinaryInstruction::Op::kSRem;
    if (op == "==") return ir::BinaryInstruction::Op::kCmpEq;
    if (op == "!=") return ir::BinaryInstruction::Op::kCmpNe;
    if (op == "<") return ir::BinaryInstruction::Op::kCmpSlt;
    if (op == "<=") return ir::BinaryInstruction::Op::kCmpSle;
    if (op == ">") return ir::BinaryInstruction::Op::kCmpSgt;
    if (op == ">=") return ir::BinaryInstruction::Op::kCmpSge;
    return ir::BinaryInstruction::Op::kAdd;
}

EvalResult EvalBinary(const std::shared_ptr<BinaryExpression> &bin, LoweringContext &lc) {
    auto lhs = EvalExpr(bin->left, lc);
    auto rhs = EvalExpr(bin->right, lc);
    if (lhs.type.kind == ir::IRTypeKind::kInvalid || rhs.type.kind == ir::IRTypeKind::kInvalid)
        return {};
    
    // Operator overloading: if the left operand is a class type, look for overloaded operators
    if (lhs.type.kind == ir::IRTypeKind::kStruct || 
        (lhs.type.kind == ir::IRTypeKind::kPointer && 
         !lhs.type.subtypes.empty() && lhs.type.subtypes[0].kind == ir::IRTypeKind::kStruct)) {
        
        std::string class_name;
        if (lhs.type.kind == ir::IRTypeKind::kStruct) {
            class_name = lhs.type.name;
        } else {
            class_name = lhs.type.subtypes[0].name;
        }
        
        // Look for operator+ style methods
        std::string operator_name = "operator" + bin->op;
        auto *methods = lc.class_metadata.GetMethods(class_name);
        if (methods) {
            for (const auto &method : *methods) {
                if (method.name == operator_name) {
                    // Found an overloaded operator, invoke it
                    std::vector<std::string> args = {lhs.value, rhs.value};
                    auto call = lc.builder.MakeCall(method.mangled_name, args, 
                                                    method.return_type, "");
                    return {call->name, call->type};
                }
            }
        }
    }
    
    // Fall back to built-in arithmetic if no overload is found
    bool is_float = (lhs.type.kind == ir::IRTypeKind::kF32 || lhs.type.kind == ir::IRTypeKind::kF64);
    ir::BinaryInstruction::Op op = MapBinOp(bin->op, is_float);
    auto inst = lc.builder.MakeBinary(op, lhs.value, rhs.value, "");
    // Set result type (cmp yields i1).
    switch (op) {
        case ir::BinaryInstruction::Op::kCmpEq:
        case ir::BinaryInstruction::Op::kCmpNe:
        case ir::BinaryInstruction::Op::kCmpUlt:
        case ir::BinaryInstruction::Op::kCmpUle:
        case ir::BinaryInstruction::Op::kCmpUgt:
        case ir::BinaryInstruction::Op::kCmpUge:
        case ir::BinaryInstruction::Op::kCmpSlt:
        case ir::BinaryInstruction::Op::kCmpSle:
        case ir::BinaryInstruction::Op::kCmpSgt:
        case ir::BinaryInstruction::Op::kCmpSge:
        case ir::BinaryInstruction::Op::kCmpFoe:
        case ir::BinaryInstruction::Op::kCmpFne:
        case ir::BinaryInstruction::Op::kCmpFlt:
        case ir::BinaryInstruction::Op::kCmpFle:
        case ir::BinaryInstruction::Op::kCmpFgt:
        case ir::BinaryInstruction::Op::kCmpFge:
        case ir::BinaryInstruction::Op::kCmpLt:
            inst->type = ir::IRType::I1();
            break;
        default:
            inst->type = lhs.type;
            break;
    }
    return {inst->name, inst->type};
}

EvalResult EvalCall(const std::shared_ptr<CallExpression> &call, LoweringContext &lc) {
    std::vector<std::string> args;
    std::vector<ir::IRType> arg_types;
    for (const auto &arg : call->args) {
        auto ev = EvalExpr(arg, lc);
        if (ev.type.kind == ir::IRTypeKind::kInvalid) return {};
        args.push_back(ev.value);
        arg_types.push_back(ev.type);
    }
    
    std::string callee_name;
    if (auto id = std::dynamic_pointer_cast<Identifier>(call->callee)) {
        callee_name = id->name;
    } else {
        lc.diags.Report(call->loc, "Only direct function calls are supported");
        return {};
    }
    
    // Create call instruction
    auto inst = lc.builder.MakeCall(callee_name, args, ir::IRType::I64(true), "");
    return {inst->name, inst->type};
}

EvalResult EvalExpr(const std::shared_ptr<Expression> &expr, LoweringContext &lc) {
    if (!expr) return {};
    if (auto lit = std::dynamic_pointer_cast<Literal>(expr)) {
        // Try float first
        double fv{};
        if (IsFloatLiteral(lit->value, &fv)) {
            return MakeFloatLiteral(fv, lc);
        }
        // Try integer
        long long v{};
        if (!IsIntegerLiteral(lit->value, &v)) {
            lc.diags.Report(lit->loc, "Invalid numeric literal");
            return {};
        }
        return MakeLiteral(v, lc);
    }
    if (auto id = std::dynamic_pointer_cast<Identifier>(expr)) {
        return EvalIdentifier(id, lc);
    }
    if (auto bin = std::dynamic_pointer_cast<BinaryExpression>(expr)) {
        return EvalBinary(bin, lc);
    }
    if (auto call = std::dynamic_pointer_cast<CallExpression>(expr)) {
        return EvalCall(call, lc);
    }
    if (auto mem = std::dynamic_pointer_cast<MemberExpression>(expr)) {
        return EvalMemberAccess(mem, lc);
    }
    if (auto idx = std::dynamic_pointer_cast<IndexExpression>(expr)) {
        return EvalIndexAccess(idx, lc);
    }
    if (auto new_expr = std::dynamic_pointer_cast<NewExpression>(expr)) {
        return EvalNew(new_expr, lc);
    }
    if (auto del_expr = std::dynamic_pointer_cast<DeleteExpression>(expr)) {
        return EvalDelete(del_expr, lc);
    }
    if (auto typeid_expr = std::dynamic_pointer_cast<TypeidExpression>(expr)) {
        return EvalTypeid(typeid_expr, lc);
    }
    if (auto dyn_cast = std::dynamic_pointer_cast<DynamicCastExpression>(expr)) {
        return EvalDynamicCast(dyn_cast, lc);
    }
    if (auto static_cast_expr = std::dynamic_pointer_cast<StaticCastExpression>(expr)) {
        return EvalStaticCast(static_cast_expr, lc);
    }
    lc.diags.Report(expr->loc, "Unsupported expression in lowering");
    return {};
}

bool LowerStmt(const std::shared_ptr<Statement> &stmt, LoweringContext &lc);
bool LowerFunction(const FunctionDecl &fn, LoweringContext &lc);

bool LowerIf(const std::shared_ptr<IfStatement> &if_stmt, LoweringContext &lc) {
    if (lc.terminated) return true;
    
    auto cond = EvalExpr(if_stmt->condition, lc);
    if (cond.type.kind == ir::IRTypeKind::kInvalid) return false;
    
    auto *then_block = lc.fn->CreateBlock("if.then");
    auto *else_block = if_stmt->else_body.empty() ? nullptr : lc.fn->CreateBlock("if.else");
    auto *merge_block = lc.fn->CreateBlock("if.end");
    
    // Conditional branch
    lc.builder.MakeCondBranch(cond.value, then_block, 
                              else_block ? else_block : merge_block);
    lc.terminated = false;
    
    // Then block
    lc.builder.SetInsertPoint(lc.fn->blocks.back());  
    bool then_term = false;
    for (auto &s : if_stmt->then_body) {
        if (!LowerStmt(s, lc)) return false;
        if (lc.terminated) {
            then_term = true;
            break;
        }
    }
    if (!then_term) {
        lc.builder.MakeBranch(merge_block);
    }
    
    // Else block
    bool else_term = false;
    if (else_block) {
        // Find the else block in the function's block list
        for (auto &bb : lc.fn->blocks) {
            if (bb.get() == else_block) {
                lc.builder.SetInsertPoint(bb);
                break;
            }
        }
        lc.terminated = false;
        for (auto &s : if_stmt->else_body) {
            if (!LowerStmt(s, lc)) return false;
            if (lc.terminated) {
                else_term = true;
                break;
            }
        }
        if (!else_term) {
            lc.builder.MakeBranch(merge_block);
        }
    }
    
    // Merge block
    for (auto &bb : lc.fn->blocks) {
        if (bb.get() == merge_block) {
            lc.builder.SetInsertPoint(bb);
            break;
        }
    }
    lc.terminated = then_term && (else_term || !else_block);
    return true;
}

bool LowerWhile(const std::shared_ptr<WhileStatement> &while_stmt, LoweringContext &lc) {
    if (lc.terminated) return true;
    
    auto *cond_block = lc.fn->CreateBlock("while.cond");
    auto *body_block = lc.fn->CreateBlock("while.body");
    auto *exit_block = lc.fn->CreateBlock("while.end");
    
    // Jump to condition check
    lc.builder.MakeBranch(cond_block);
    
    // Find and set cond_block
    for (auto &bb : lc.fn->blocks) {
        if (bb.get() == cond_block) {
            lc.builder.SetInsertPoint(bb);
            break;
        }
    }
    lc.terminated = false;
    
    auto cond = EvalExpr(while_stmt->condition, lc);
    if (cond.type.kind == ir::IRTypeKind::kInvalid) return false;
    
    lc.builder.MakeCondBranch(cond.value, body_block, exit_block);
    
    // Body
    for (auto &bb : lc.fn->blocks) {
        if (bb.get() == body_block) {
            lc.builder.SetInsertPoint(bb);
            break;
        }
    }
    lc.terminated = false;
    for (auto &s : while_stmt->body) {
        if (!LowerStmt(s, lc)) return false;
        if (lc.terminated) break;
    }
    if (!lc.terminated) {
        lc.builder.MakeBranch(cond_block);
    }
    
    // Exit
    for (auto &bb : lc.fn->blocks) {
        if (bb.get() == exit_block) {
            lc.builder.SetInsertPoint(bb);
            break;
        }
    }
    lc.terminated = false;
    return true;
}

bool LowerFor(const std::shared_ptr<ForStatement> &for_stmt, LoweringContext &lc) {
    if (lc.terminated) return true;
    
    // Init
    if (for_stmt->init && !LowerStmt(for_stmt->init, lc)) return false;
    
    auto *cond_block = lc.fn->CreateBlock("for.cond");
    auto *body_block = lc.fn->CreateBlock("for.body");
    auto *inc_block = lc.fn->CreateBlock("for.inc");
    auto *exit_block = lc.fn->CreateBlock("for.end");
    
    lc.builder.MakeBranch(cond_block);
    
    // Condition
    for (auto &bb : lc.fn->blocks) {
        if (bb.get() == cond_block) {
            lc.builder.SetInsertPoint(bb);
            break;
        }
    }
    lc.terminated = false;
    
    if (for_stmt->condition) {
        auto cond = EvalExpr(for_stmt->condition, lc);
        if (cond.type.kind == ir::IRTypeKind::kInvalid) return false;
        lc.builder.MakeCondBranch(cond.value, body_block, exit_block);
    } else {
        lc.builder.MakeBranch(body_block);
    }
    
    // Body
    for (auto &bb : lc.fn->blocks) {
        if (bb.get() == body_block) {
            lc.builder.SetInsertPoint(bb);
            break;
        }
    }
    lc.terminated = false;
    for (auto &s : for_stmt->body) {
        if (!LowerStmt(s, lc)) return false;
        if (lc.terminated) break;
    }
    if (!lc.terminated) {
        lc.builder.MakeBranch(inc_block);
    }
    
    // Increment
    for (auto &bb : lc.fn->blocks) {
        if (bb.get() == inc_block) {
            lc.builder.SetInsertPoint(bb);
            break;
        }
    }
    lc.terminated = false;
    if (for_stmt->increment) {
        (void)EvalExpr(for_stmt->increment, lc);
    }
    lc.builder.MakeBranch(cond_block);
    
    // Exit
    for (auto &bb : lc.fn->blocks) {
        if (bb.get() == exit_block) {
            lc.builder.SetInsertPoint(bb);
            break;
        }
    }
    lc.terminated = false;
    return true;
}

bool LowerReturn(const std::shared_ptr<ReturnStatement> &ret, LoweringContext &lc) {
    if (lc.terminated) return true;
    EvalResult v;
    if (ret->value) v = EvalExpr(ret->value, lc);
    lc.builder.MakeReturn(v.value);
    lc.terminated = true;
    return true;
}

bool LowerVar(const std::shared_ptr<VarDecl> &var, LoweringContext &lc) {
    EvalResult init;
    if (var->init) init = EvalExpr(var->init, lc);
    if (init.value.empty()) {
        lc.diags.Report(var->loc, "Variable initializer required in minimal lowering");
        return false;
    }
    lc.env[var->name] = {init.value, init.type};
    return true;
}

// Exception handling lowering
bool LowerThrow(const std::shared_ptr<ThrowStatement> &throw_stmt, LoweringContext &lc) {
    if (lc.terminated) return true;
    
    // Evaluate exception value
    std::vector<std::string> args;
    if (throw_stmt->value) {
        auto exc_val = EvalExpr(throw_stmt->value, lc);
        if (exc_val.type.kind == ir::IRTypeKind::kInvalid) return false;
        args.push_back(exc_val.value);
    }
    
    // Create call to __cxa_throw (C++ ABI exception throwing)
    lc.builder.MakeCall("__cxa_throw", args, ir::IRType::Void());
    
    // Add unreachable after throw
    lc.builder.MakeUnreachable();
    lc.terminated = true;
    
    return true;
}

bool LowerTry(const std::shared_ptr<TryStatement> &try_stmt, LoweringContext &lc) {
    if (lc.terminated) return true;
    
    // Create blocks
    auto *try_block = lc.fn->CreateBlock("try.body");
    auto *landing_pad_block = lc.fn->CreateBlock("catch.dispatch");
    auto *normal_cont = lc.fn->CreateBlock("try.cont");
    
    // Jump to try block
    lc.builder.MakeBranch(try_block);
    
    // Lower try body
    for (size_t i = 0; i < lc.fn->blocks.size(); ++i) {
        if (lc.fn->blocks[i]->name == try_block->name) {
            lc.builder.SetInsertPoint(lc.fn->blocks[i]);
            break;
        }
    }
    
    bool try_terminated = false;
    for (auto &s : try_stmt->try_body) {
        if (!LowerStmt(s, lc)) return false;
        if (lc.terminated) {
            try_terminated = true;
            break;
        }
    }
    
    if (!try_terminated) {
        lc.builder.MakeBranch(normal_cont);
    }
    lc.terminated = false;
    
    // Create landing pad
    for (size_t i = 0; i < lc.fn->blocks.size(); ++i) {
        if (lc.fn->blocks[i]->name == landing_pad_block->name) {
            lc.builder.SetInsertPoint(lc.fn->blocks[i]);
            break;
        }
    }
    
    auto landingpad = std::make_shared<ir::LandingPadInstruction>();
    landingpad->is_cleanup = try_stmt->catches.empty();
    landingpad->type = ir::IRType::I64(true);  // Simplified type for now
    
    // Add catch types
    for (auto &catch_clause : try_stmt->catches) {
        if (catch_clause.exception_type) {
            ir::IRType catch_type = ToIRType(catch_clause.exception_type);
            landingpad->catch_types.push_back(catch_type);
        }
    }
    
    // Note: We can't easily insert landingpad without direct block access
    // This is a simplified implementation that skips the actual IR insertion
    // In a full implementation, we'd need to add the landingpad to the block's instructions
    
    // Lower catch clauses
    for (size_t i = 0; i < try_stmt->catches.size(); ++i) {
        auto &catch_clause = try_stmt->catches[i];
        auto *catch_block = lc.fn->CreateBlock("catch." + std::to_string(i));
        
        // Jump to catch block
        lc.builder.MakeBranch(catch_block);
        
        for (size_t j = 0; j < lc.fn->blocks.size(); ++j) {
            if (lc.fn->blocks[j]->name == catch_block->name) {
                lc.builder.SetInsertPoint(lc.fn->blocks[j]);
                break;
            }
        }
        
        // Lower catch body
        bool catch_terminated = false;
        for (auto &s : catch_clause.body) {
            if (!LowerStmt(s, lc)) return false;
            if (lc.terminated) {
                catch_terminated = true;
                break;
            }
        }
        
        if (!catch_terminated) {
            lc.builder.MakeBranch(normal_cont);
        }
        lc.terminated = false;
    }
    
    // Continue after try-catch
    for (size_t i = 0; i < lc.fn->blocks.size(); ++i) {
        if (lc.fn->blocks[i]->name == normal_cont->name) {
            lc.builder.SetInsertPoint(lc.fn->blocks[i]);
            break;
        }
    }
    lc.terminated = false;
    
    return true;
}

// ==================== Class and inheritance lowering ====================

// Build a class vtable
std::shared_ptr<ir::VTable> BuildVTable(const std::shared_ptr<RecordDecl> &record, 
                                        LoweringContext &lc) {
    auto vtable = std::make_shared<ir::VTable>();
    vtable->class_name = record->name;
    
    // Collect virtual functions
    std::vector<std::shared_ptr<FunctionDecl>> virtual_methods;
    for (auto &method_stmt : record->methods) {
        if (auto func = std::dynamic_pointer_cast<FunctionDecl>(method_stmt)) {
            // Use FunctionDecl::is_virtual set by the parser when reading the "virtual" keyword
            if (func->is_virtual) {
                virtual_methods.push_back(func);
                ir::VTableEntry entry;
                entry.function_name = record->name + "::" + func->name;
                entry.offset = vtable->entries.size();
                entry.is_pure = func->is_pure_virtual; // Use the is_pure_virtual flag
                vtable->entries.push_back(entry);
            }
        }
    }
    
    // If there are no virtual functions, return null
    if (vtable->entries.empty()) {
        return nullptr;
    }
    
    // Create the vtable global (array of function pointers)
    std::string vtable_name = "__vtable_" + record->name;
    vtable->global_var = lc.ir_ctx.CreateGlobal(
        vtable_name,
        ir::IRType::Array(ir::IRType::Pointer(ir::IRType::I8()), vtable->entries.size()),
        true  // const
    );
    
    return vtable;
}

// Lower class declarations
bool LowerRecord(const std::shared_ptr<RecordDecl> &record, LoweringContext &lc) {
    if (record->is_forward) {
        // Forward declaration; nothing to do
        return true;
    }
    
    lc.current_class = record->name;
    
    // Build the class layout
    ir::ClassLayout layout;
    layout.class_name = record->name;
    
    // Handle base classes (supports multiple inheritance and virtual inheritance)
    size_t current_field_offset = 0;
    size_t base_index = 0;
    
    // Pass 1: handle virtual bases
    std::unordered_set<std::string> processed_virtual_bases;
    for (auto &base : record->bases) {
        if (base.is_virtual) {
            // Virtual base
            layout.virtual_bases.push_back(base.name);
            
            // Skip duplicates (diamond inheritance stores a virtual base once)
            if (processed_virtual_bases.count(base.name) > 0) {
                continue;
            }
            processed_virtual_bases.insert(base.name);
            
            auto *base_layout = lc.class_metadata.GetLayout(base.name);
            if (base_layout) {
                // Virtual bases are placed at the end; record now, fill later
                layout.virtual_base_offsets[base.name] = static_cast<size_t>(-1);  // updated later
            }
        }
    }
    
    // If there are virtual bases, we need a vbtable
    if (!layout.virtual_bases.empty()) {
        layout.has_vbtable = true;
        layout.vbtable_offset = current_field_offset;
        
        // Add vbtable pointer
        layout.field_names.push_back("__vbtable");
        layout.field_offsets["__vbtable"] = current_field_offset;
        current_field_offset++;
    }
    
    // Pass 2: handle non-virtual bases
    for (auto &base : record->bases) {
        if (base.is_virtual) {
            // Virtual base already handled
            continue;
        }
        
        layout.base_classes.push_back(base.name);
        
        auto *base_layout = lc.class_metadata.GetLayout(base.name);
        if (base_layout) {
            // Add a vtable pointer for each base (if present)
            if (base_layout->has_vtable) {
                std::string vptr_name = "__vptr_" + base.name;
                layout.field_names.push_back(vptr_name);
                layout.field_offsets[vptr_name] = current_field_offset;
                layout.base_vtable_offsets[base.name] = current_field_offset;
                
                // The first base vtable becomes the primary vtable
                if (base_index == 0) {
                    layout.has_vtable = true;
                    layout.vtable_offset = current_field_offset;
                }
                
                // Keep the base vtable for multiple inheritance
                if (base_layout->vtable) {
                    layout.base_vtables[base.name] = base_layout->vtable;
                }
                
                current_field_offset++;
            }
            
            // Copy non-vtable fields from the base
            for (size_t i = 0; i < base_layout->field_names.size(); ++i) {
                const auto &field_name = base_layout->field_names[i];
                // Skip vtable pointer fields
                if (field_name.find("__vptr") == 0) continue;
                
                layout.field_names.push_back(field_name);
                layout.field_offsets[field_name] = current_field_offset++;
            }
        }
        
        base_index++;
    }
    
    // Build vtable if virtual functions exist
    layout.vtable = BuildVTable(record, lc);
    if (layout.vtable && !layout.has_vtable) {
        // This class introduces a vtable for the first time (no virtual bases)
        layout.has_vtable = true;
        std::string vptr_name = "__vptr";
        layout.field_names.insert(layout.field_names.begin(), vptr_name);
        layout.vtable_offset = 0;
        
        // Adjust offsets for the remaining fields
        for (auto &kv : layout.field_offsets) {
            kv.second += 1;
        }
        layout.field_offsets[vptr_name] = 0;
        current_field_offset++;
    }
    
    // Add this class's own fields
    std::vector<ir::IRType> field_types;
    
    // Add types for each base's vtable pointer
    for (const auto &base : record->bases) {
        auto *base_layout = lc.class_metadata.GetLayout(base.name);
        if (base_layout && base_layout->has_vtable) {
            field_types.push_back(ir::IRType::Pointer(ir::IRType::I8()));
        }
        // Append base field types
        if (base_layout) {
            for (const auto &base_field_type : base_layout->struct_type.subtypes) {
                // Skip vtable pointers (handled separately)
                if (base_field_type.kind == ir::IRTypeKind::kPointer) continue;
                field_types.push_back(base_field_type);
            }
        }
    }
    
    // If this class introduces a vtable but has no bases
    if (layout.has_vtable && record->bases.empty()) {
        field_types.push_back(ir::IRType::Pointer(ir::IRType::I8()));
    }
    
    // Add fields declared in this class
    for (auto &field : record->fields) {
        layout.field_names.push_back(field.name);
        layout.field_offsets[field.name] = current_field_offset++;
        
        ir::IRType field_type = ToIRType(field.type);
        field_types.push_back(field_type);
        
        // Register field info (for access control)
        ir::FieldInfo field_info;
        field_info.name = field.name;
        field_info.type = field_type;
        field_info.access = field.access.empty() ? "public" : field.access;  // default public
        field_info.is_static = field.is_static;
        field_info.is_const = field.is_constexpr;
        field_info.is_mutable = field.is_mutable;
        lc.class_metadata.RegisterField(record->name, field_info);
    }
    
    // Append fields from virtual bases (placed last)
    for (const auto &vbase_name : layout.virtual_bases) {
        auto *vbase_layout = lc.class_metadata.GetLayout(vbase_name);
        if (vbase_layout) {
            // Update the virtual base offset
            layout.virtual_base_offsets[vbase_name] = current_field_offset;
            
            // Append the virtual base fields
            for (size_t i = 0; i < vbase_layout->field_names.size(); ++i) {
                const auto &field_name = vbase_layout->field_names[i];
                if (field_name.find("__vptr") == 0 || field_name == "__vbtable") continue;
                
                layout.field_names.push_back("__vbase_" + vbase_name + "_" + field_name);
                layout.field_offsets["__vbase_" + vbase_name + "_" + field_name] = current_field_offset++;
                
                // Add the corresponding field type
                if (i < vbase_layout->struct_type.subtypes.size()) {
                    field_types.push_back(vbase_layout->struct_type.subtypes[i]);
                }
            }
        }
    }
    
    // Create the struct type
    layout.struct_type = ir::IRType::Struct(record->name, field_types);
    
    // Register the class layout
    lc.class_metadata.RegisterClass(record->name, layout);
    
    // Lower methods
    for (auto &method_stmt : record->methods) {
        if (auto func = std::dynamic_pointer_cast<FunctionDecl>(method_stmt)) {
            // Register method metadata
            ir::MethodInfo method_info;
            method_info.name = func->name;
            method_info.mangled_name = record->name + "::" + func->name;
            method_info.return_type = ToIRType(func->return_type);
            
            // Use flags already parsed from FunctionDecl
            method_info.is_virtual = func->is_virtual;
            method_info.is_pure_virtual = func->is_pure_virtual;
            method_info.is_static = func->is_static;
            method_info.is_const = func->is_const_qualified;
            method_info.access = func->access;
            
            lc.class_metadata.RegisterMethod(record->name, method_info);
            
            // Lower non-pure-virtual methods
            if (!method_info.is_pure_virtual) {
                // Create the mangled function
                FunctionDecl mangled_func = *func;
                mangled_func.name = method_info.mangled_name;
                
                // Add the implicit this parameter for non-static methods
                if (!method_info.is_static) {
                    // Type of the this pointer
                    auto this_type = std::make_shared<PointerType>();
                    this_type->pointee = std::make_shared<SimpleType>();
                    std::dynamic_pointer_cast<SimpleType>(this_type->pointee)->name = record->name;
                    
                    FunctionDecl::Param this_param;
                    this_param.name = "this";
                    this_param.type = this_type;
                    mangled_func.params.insert(mangled_func.params.begin(), this_param);
                }
                
                // Special case: constructors need to initialize the vtable pointer
                if (func->is_constructor && layout.has_vtable && layout.vtable) {
                    // Inject vtable initialization code before the constructor body
                    // Create an assignment: this->__vptr = &__vtable_ClassName
                    
                    // This should be prepared before actual function lowering
                    // Simplified: LowerFunction detects constructors and adds init
                }
                
                if (!LowerFunction(mangled_func, lc)) {
                    return false;
                }
            }
        }
    }
    
    // Register RTTI TypeInfo
    ir::TypeInfo type_info;
    type_info.class_name = record->name;
    type_info.mangled_name = "_ZTI" + std::to_string(record->name.length()) + record->name;
    type_info.base_types = layout.base_classes;
    type_info.has_virtual_functions = layout.has_vtable;
    lc.class_metadata.RegisterTypeInfo(record->name, type_info);
    
    lc.current_class.clear();
    return true;
}

// Handle template declarations
bool LowerTemplate(const std::shared_ptr<TemplateDecl> &tmpl, LoweringContext &lc) {
    // Gather template parameters
    std::vector<ir::TemplateParameter> params;
    for (const auto &param_name : tmpl->params) {
        ir::TemplateParameter param;
        param.name = param_name;
        param.is_typename = true;  // Simplified: assume all parameters are type parameters
        params.push_back(param);
    }
    
    // Inspect the type of the inner declaration
    if (auto func = std::dynamic_pointer_cast<FunctionDecl>(tmpl->inner)) {
        // Function template
        lc.template_instantiator.RegisterFunctionTemplate(
            func->name,
            params,
            tmpl->inner.get()
        );
        
        // Do not lower immediately; wait for instantiation
        return true;
        
    } else if (auto record = std::dynamic_pointer_cast<RecordDecl>(tmpl->inner)) {
        // Class template
        lc.template_instantiator.RegisterClassTemplate(
            record->name,
            params,
            tmpl->inner.get()
        );
        
        // Do not lower immediately; wait for instantiation
        return true;
        
    } else {
        lc.diags.Report(tmpl->loc, "Unsupported template declaration");
        return false;
    }
}

bool LowerStmt(const std::shared_ptr<Statement> &stmt, LoweringContext &lc) {
    if (!stmt || lc.terminated) return true;
    if (auto var = std::dynamic_pointer_cast<VarDecl>(stmt)) return LowerVar(var, lc);
    if (auto ret = std::dynamic_pointer_cast<ReturnStatement>(stmt)) return LowerReturn(ret, lc);
    if (auto if_stmt = std::dynamic_pointer_cast<IfStatement>(stmt)) return LowerIf(if_stmt, lc);
    if (auto while_stmt = std::dynamic_pointer_cast<WhileStatement>(stmt)) return LowerWhile(while_stmt, lc);
    if (auto for_stmt = std::dynamic_pointer_cast<ForStatement>(stmt)) return LowerFor(for_stmt, lc);
    if (auto try_stmt = std::dynamic_pointer_cast<TryStatement>(stmt)) return LowerTry(try_stmt, lc);
    if (auto throw_stmt = std::dynamic_pointer_cast<ThrowStatement>(stmt)) return LowerThrow(throw_stmt, lc);
    if (auto record = std::dynamic_pointer_cast<RecordDecl>(stmt)) return LowerRecord(record, lc);
    if (auto tmpl = std::dynamic_pointer_cast<TemplateDecl>(stmt)) return LowerTemplate(tmpl, lc);
    if (auto expr = std::dynamic_pointer_cast<ExprStatement>(stmt)) {
        (void)EvalExpr(expr->expr, lc);
        return true;
    }
    if (auto comp = std::dynamic_pointer_cast<CompoundStatement>(stmt)) {
        for (auto &s : comp->statements) {
            if (!LowerStmt(s, lc)) return false;
            if (lc.terminated) break;
        }
        return true;
    }
    lc.diags.Report(stmt->loc, "Unsupported statement in lowering");
    return false;
}

bool LowerFunction(const FunctionDecl &fn, LoweringContext &lc) {
    // Map signature (minimal: primitive ints/bools/void)
    ir::IRType ret_ty = ToIRType(fn.return_type);
    if (ret_ty.kind == ir::IRTypeKind::kInvalid) ret_ty = ir::IRType::I64(true);

    std::vector<std::pair<std::string, ir::IRType>> params;
    params.reserve(fn.params.size());
    for (auto &p : fn.params) {
        ir::IRType pt = ToIRType(p.type);
        if (pt.kind == ir::IRTypeKind::kInvalid) {
            lc.diags.Report(p.type ? p.type->loc : fn.loc, "Unsupported parameter type");
            return false;
        }
        params.push_back({p.name, pt});
    }

    lc.fn = lc.ir_ctx.CreateFunction(fn.name, ret_ty, params);
    // Create entry block and start inserting there.
    auto *entry = lc.fn->CreateBlock("entry");
    lc.fn->entry = entry;
    if (!lc.fn->blocks.empty()) {
        lc.builder.SetInsertPoint(lc.fn->blocks.back());
    }

    lc.env.clear();
    for (const auto &p : params) {
        lc.env[p.first] = {p.first, p.second};
    }
    lc.terminated = false;

    for (auto &stmt : fn.body) {
        if (!LowerStmt(stmt, lc)) return false;
        if (lc.terminated) break;
    }

    if (!lc.terminated) {
        if (ret_ty.kind == ir::IRTypeKind::kVoid) {
            lc.builder.MakeReturn("");
        } else {
            auto zero = MakeLiteral(0, lc);
            lc.builder.MakeReturn(zero.value);
        }
    }
    return true;
}

}  // namespace

void LowerToIR(const Module &module, ir::IRContext &ctx, frontends::Diagnostics &diags) {
    LoweringContext lc(ctx, diags);
    for (const auto &decl : module.declarations) {
        auto fn = std::dynamic_pointer_cast<FunctionDecl>(decl);
        if (!fn) continue;
        if (fn->is_deleted || fn->is_defaulted) continue;
        LowerFunction(*fn, lc);
    }
}

}  // namespace polyglot::cpp
