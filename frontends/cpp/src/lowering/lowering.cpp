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

// 类型辅助函数保持不变

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
    ir::ClassMetadata class_metadata;     // 类元数据管理
    ir::TemplateInstantiator template_instantiator;  // 模板实例化管理
    std::string current_class;            // 当前正在处理的类名
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

// ==================== 成员访问和虚函数调用 ====================

// 处理数组下标/operator[] 重载
EvalResult EvalIndexAccess(const std::shared_ptr<IndexExpression> &idx, LoweringContext &lc) {
    auto obj = EvalExpr(idx->object, lc);
    auto index = EvalExpr(idx->index, lc);
    
    if (obj.type.kind == ir::IRTypeKind::kInvalid || 
        index.type.kind == ir::IRTypeKind::kInvalid) {
        return {};
    }
    
    // 检查是否是类类型（可能重载了 operator[]）
    std::string class_name;
    if (obj.type.kind == ir::IRTypeKind::kStruct) {
        class_name = obj.type.name;
    } else if (obj.type.kind == ir::IRTypeKind::kPointer && 
               !obj.type.subtypes.empty() && 
               obj.type.subtypes[0].kind == ir::IRTypeKind::kStruct) {
        class_name = obj.type.subtypes[0].name;
    }
    
    // 如果是类类型，查找 operator[] 重载
    if (!class_name.empty()) {
        auto *methods = lc.class_metadata.GetMethods(class_name);
        if (methods) {
            for (const auto &method : *methods) {
                if (method.name == "operator[]") {
                    // 调用重载的 operator[]
                    std::vector<std::string> args = {obj.value, index.value};
                    auto call = lc.builder.MakeCall(method.mangled_name, args, 
                                                    method.return_type, "");
                    return {call->name, call->type};
                }
            }
        }
    }
    
    // 普通数组访问或指针运算
    if (obj.type.kind == ir::IRTypeKind::kPointer) {
        // 计算元素类型
        ir::IRType elem_type = ir::IRType::I64(true);  // 默认
        if (!obj.type.subtypes.empty()) {
            elem_type = obj.type.subtypes[0];
        }
        
        // 使用指针算术: ptr + index * elem_size
        // 简化实现：直接使用 GEP 与单个动态索引
        // 注意：这里需要运行时索引，暂时使用简化方式
        // TODO: 实现真正的动态 GEP 或指针算术
        
        // 对于简化实现，假设可以直接访问
        // 生成一个临时值表示数组访问结果
        std::string result_name = "arr_elem_" + std::to_string(
            reinterpret_cast<uintptr_t>(idx.get()));
        return {result_name, elem_type};
    }
    
    lc.diags.Report(idx->loc, "Subscript operator requires array or class with operator[]");
    return {};
}

// 处理 new 表达式
EvalResult EvalNew(const std::shared_ptr<NewExpression> &new_expr, LoweringContext &lc) {
    // 获取要分配的类型
    ir::IRType alloc_type = ToIRType(new_expr->type);
    if (alloc_type.kind == ir::IRTypeKind::kInvalid) {
        lc.diags.Report(new_expr->loc, "Invalid type for new expression");
        return {};
    }
    
    // 处理数组 new
    if (new_expr->is_array) {
        if (new_expr->args.empty()) {
            lc.diags.Report(new_expr->loc, "Array new requires size argument");
            return {};
        }
        
        auto size_result = EvalExpr(new_expr->args[0], lc);
        if (size_result.type.kind == ir::IRTypeKind::kInvalid) return {};
        
        // 调用 __builtin_new_array(size, element_size)
        // 简化实现：假设 element_size 固定
        std::vector<std::string> args = {size_result.value, "8"};  // 假设每个元素 8 字节
        auto call = lc.builder.MakeCall("__builtin_new_array", args, 
                                       ir::IRType::Pointer(alloc_type), "");
        return {call->name, call->type};
    }
    
    // 单对象 new
    // 1. 调用 __builtin_new 分配内存
    auto ptr_type = ir::IRType::Pointer(alloc_type);
    auto alloc_call = lc.builder.MakeCall("__builtin_new", {}, ptr_type, "");
    
    // 2. 如果是类类型，需要调用构造函数
    std::string class_name;
    if (alloc_type.kind == ir::IRTypeKind::kStruct) {
        class_name = alloc_type.name;
        
        // 检查是否有构造函数
        auto *layout = lc.class_metadata.GetLayout(class_name);
        if (layout) {
            // 3. 初始化 vtable 指针（如果有）
            if (layout->has_vtable && layout->vtable) {
                // 生成 GEP 访问 __vptr 字段（偏移 0）
                auto vptr_gep = lc.builder.MakeGEP(alloc_call->name, ptr_type, {0, 0});
                
                // 获取 vtable 全局变量地址
                std::string vtable_name = "__vtable_" + class_name;
                
                // 存储 vtable 指针
                lc.builder.MakeStore(vtable_name, vptr_gep->name);
            }
            
            // 4. 调用构造函数（如果存在）
            std::string ctor_name = class_name + "::" + class_name;  // ClassName::ClassName
            
            // 准备构造函数参数：this 指针 + 用户提供的参数
            std::vector<std::string> ctor_args = {alloc_call->name};
            for (const auto &arg : new_expr->args) {
                auto arg_result = EvalExpr(arg, lc);
                if (arg_result.type.kind == ir::IRTypeKind::kInvalid) return {};
                ctor_args.push_back(arg_result.value);
            }
            
            // 调用构造函数（如果存在的话）
            // 注意：这里不检查构造函数是否存在，lowering 时会处理
            lc.builder.MakeCall(ctor_name, ctor_args, ir::IRType::Void(), "");
        }
    }
    
    return {alloc_call->name, ptr_type};
}

// 处理 delete 表达式
EvalResult EvalDelete(const std::shared_ptr<DeleteExpression> &del_expr, LoweringContext &lc) {
    auto obj = EvalExpr(del_expr->operand, lc);
    if (obj.type.kind == ir::IRTypeKind::kInvalid) {
        return {};
    }
    
    if (obj.type.kind != ir::IRTypeKind::kPointer) {
        lc.diags.Report(del_expr->loc, "Delete requires pointer type");
        return {};
    }
    
    // 处理数组 delete
    if (del_expr->is_array) {
        lc.builder.MakeCall("__builtin_delete_array", {obj.value}, ir::IRType::Void(), "");
        return {obj.value, ir::IRType::Void()};
    }
    
    // 单对象 delete
    // 1. 如果是类类型，调用析构函数
    if (!obj.type.subtypes.empty() && obj.type.subtypes[0].kind == ir::IRTypeKind::kStruct) {
        std::string class_name = obj.type.subtypes[0].name;
        std::string dtor_name = class_name + "::~" + class_name;
        
        // 调用析构函数
        lc.builder.MakeCall(dtor_name, {obj.value}, ir::IRType::Void(), "");
    }
    
    // 2. 调用 __builtin_delete 释放内存
    lc.builder.MakeCall("__builtin_delete", {obj.value}, ir::IRType::Void(), "");
    
    return {obj.value, ir::IRType::Void()};
}

// 处理 typeid 表达式
EvalResult EvalTypeid(const std::shared_ptr<TypeidExpression> &typeid_expr, LoweringContext &lc) {
    std::string class_name;
    
    if (typeid_expr->is_type) {
        // typeid(Type)
        // 从类型节点提取类名
        if (auto simple = std::dynamic_pointer_cast<SimpleType>(typeid_expr->type_arg)) {
            class_name = simple->name;
        } else {
            lc.diags.Report(typeid_expr->loc, "typeid requires class type");
            return {};
        }
    } else {
        // typeid(expression)
        // 评估表达式并获取其类型
        auto val = EvalExpr(typeid_expr->expr_arg, lc);
        if (val.type.kind == ir::IRTypeKind::kInvalid) {
            return {};
        }
        
        // 提取类名
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
    
    // 获取type_info对象
    auto *type_info = lc.class_metadata.GetTypeInfo(class_name);
    if (!type_info) {
        // 如果还没有注册，现在注册
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
    
    // 返回指向type_info的指针
    std::string type_info_name = type_info->GetTypeInfoName();
    
    // 创建type_info类型（简化为指针类型）
    ir::IRType type_info_ptr = ir::IRType::Pointer(ir::IRType::I8());
    
    return {type_info_name, type_info_ptr};
}

// 处理 dynamic_cast 表达式
EvalResult EvalDynamicCast(const std::shared_ptr<DynamicCastExpression> &cast_expr, LoweringContext &lc) {
    // 获取源对象
    auto src = EvalExpr(cast_expr->operand, lc);
    if (src.type.kind == ir::IRTypeKind::kInvalid) {
        return {};
    }
    
    // 获取目标类型
    ir::IRType target_type = ToIRType(cast_expr->target_type);
    if (target_type.kind == ir::IRTypeKind::kInvalid) {
        lc.diags.Report(cast_expr->loc, "Invalid target type for dynamic_cast");
        return {};
    }
    
    // 提取源类型和目标类型的类名
    std::string src_class, target_class;
    
    // 源类型应该是指针或引用
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
    
    // 检查类型关系
    bool is_upcast = lc.class_metadata.IsBaseOf(target_class, src_class);    // 向上转换
    bool is_downcast = lc.class_metadata.IsBaseOf(src_class, target_class);  // 向下转换
    
    if (!is_upcast && !is_downcast) {
        lc.diags.Report(cast_expr->loc, "Invalid dynamic_cast: no inheritance relationship");
        return {};
    }
    
    // 对于向上转换（派生→基类），在编译期即可确定成功
    if (is_upcast) {
        // 简化实现：直接返回源指针（在真实实现中需要调整指针偏移）
        return {src.value, target_type};
    }
    
    // 对于向下转换（基类→派生），需要运行时检查
    // 调用运行时辅助函数 __dynamic_cast_check
    std::vector<std::string> args = {
        src.value,                                // 源对象指针
        "\"" + src_class + "\"",                  // 源类型名
        "\"" + target_class + "\""                // 目标类型名
    };
    
    auto result = lc.builder.MakeCall(
        "__dynamic_cast_check",
        args,
        target_type,
        "dyn_cast_result"
    );
    
    return {result->name, target_type};
}

// 处理 static_cast 表达式
EvalResult EvalStaticCast(const std::shared_ptr<StaticCastExpression> &cast_expr, LoweringContext &lc) {
    // 获取源对象
    auto src = EvalExpr(cast_expr->operand, lc);
    if (src.type.kind == ir::IRTypeKind::kInvalid) {
        return {};
    }
    
    // 获取目标类型
    ir::IRType target_type = ToIRType(cast_expr->target_type);
    if (target_type.kind == ir::IRTypeKind::kInvalid) {
        lc.diags.Report(cast_expr->loc, "Invalid target type for static_cast");
        return {};
    }
    
    // static_cast 在编译期检查，不需要运行时检查
    // 简化实现：直接返回源值和目标类型
    // TODO: 添加更完善的类型转换逻辑（整数/浮点转换、指针转换等）
    
    return {src.value, target_type};
}

// 处理成员访问表达式
EvalResult EvalMemberAccess(const std::shared_ptr<MemberExpression> &mem, LoweringContext &lc) {
    // 评估对象表达式
    auto obj = EvalExpr(mem->object, lc);
    if (obj.type.kind == ir::IRTypeKind::kInvalid) {
        return {};
    }
    
    // 获取对象的类型名称
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
    
    // 查找类布局
    auto *layout = lc.class_metadata.GetLayout(class_name);
    if (!layout) {
        lc.diags.Report(mem->loc, "Unknown class: " + class_name);
        return {};
    }
    
    // 获取字段偏移
    size_t field_offset = layout->GetFieldOffset(mem->member);
    if (field_offset == static_cast<size_t>(-1)) {
        lc.diags.Report(mem->loc, "Unknown field: " + mem->member);
        return {};
    }
    
    // 访问控制检查
    // 查找字段的访问修饰符
    std::string field_access = "public";  // 默认 public
    auto *fields = lc.class_metadata.GetFields(class_name);
    if (fields) {
        for (const auto &field : *fields) {
            if (field.name == mem->member) {
                field_access = field.access;
                break;
            }
        }
    }
    
    // 检查访问权限
    bool access_allowed = true;
    if (field_access == "private") {
        // private: 仅本类可访问
        if (lc.current_class != class_name) {
            access_allowed = false;
        }
    } else if (field_access == "protected") {
        // protected: 本类和派生类可访问
        if (lc.current_class != class_name) {
            // 检查 lc.current_class 是否是 class_name 的派生类
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
    // public: 总是允许
    
    if (!access_allowed) {
        lc.diags.Report(mem->loc, "Cannot access " + field_access + " member '" + 
                       mem->member + "' of class '" + class_name + "'");
        return {};
    }
    
    // 生成 GEP 指令访问字段
    std::string ptr_value = obj.value;
    
    // 如果是 arrow 访问或已经是指针，直接使用
    // 否则需要取地址
    bool is_ptr = (obj.type.kind == ir::IRTypeKind::kPointer) || mem->is_arrow;
    
    if (!is_ptr) {
        // 需要获取对象地址（简化实现：假设对象已在栈上）
        // 实际应该使用 alloca
        lc.diags.Report(mem->loc, "Member access on non-pointer not fully implemented");
        return {};
    }
    
    // 使用 GEP 访问字段
    auto gep = lc.builder.MakeGEP(ptr_value, obj.type, {0, field_offset});
    
    // 从类布局获取字段类型（完整类型解析）
    ir::IRType field_type = ir::IRType::I64(true);  // 默认
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
    
    // 运算符重载检查：如果左操作数是类类型，查找重载的运算符
    if (lhs.type.kind == ir::IRTypeKind::kStruct || 
        (lhs.type.kind == ir::IRTypeKind::kPointer && 
         !lhs.type.subtypes.empty() && lhs.type.subtypes[0].kind == ir::IRTypeKind::kStruct)) {
        
        std::string class_name;
        if (lhs.type.kind == ir::IRTypeKind::kStruct) {
            class_name = lhs.type.name;
        } else {
            class_name = lhs.type.subtypes[0].name;
        }
        
        // 查找 operator+ 等方法
        std::string operator_name = "operator" + bin->op;
        auto *methods = lc.class_metadata.GetMethods(class_name);
        if (methods) {
            for (const auto &method : *methods) {
                if (method.name == operator_name) {
                    // 找到重载运算符，调用它
                    std::vector<std::string> args = {lhs.value, rhs.value};
                    auto call = lc.builder.MakeCall(method.mangled_name, args, 
                                                    method.return_type, "");
                    return {call->name, call->type};
                }
            }
        }
    }
    
    // 没有找到运算符重载，使用内置运算
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

// ==================== 类和继承 Lowering ====================

// 构建类的 vtable
std::shared_ptr<ir::VTable> BuildVTable(const std::shared_ptr<RecordDecl> &record, 
                                        LoweringContext &lc) {
    auto vtable = std::make_shared<ir::VTable>();
    vtable->class_name = record->name;
    
    // 收集虚函数
    std::vector<std::shared_ptr<FunctionDecl>> virtual_methods;
    for (auto &method_stmt : record->methods) {
        if (auto func = std::dynamic_pointer_cast<FunctionDecl>(method_stmt)) {
            // 使用 FunctionDecl 的 is_virtual 标志（已由 parser 在解析 "virtual" 关键字时设置）
            if (func->is_virtual) {
                virtual_methods.push_back(func);
                ir::VTableEntry entry;
                entry.function_name = record->name + "::" + func->name;
                entry.offset = vtable->entries.size();
                entry.is_pure = func->is_pure_virtual; // 使用 is_pure_virtual 标志
                vtable->entries.push_back(entry);
            }
        }
    }
    
    // 如果没有虚函数，返回空
    if (vtable->entries.empty()) {
        return nullptr;
    }
    
    // 创建 vtable 全局变量（数组of函数指针）
    std::string vtable_name = "__vtable_" + record->name;
    vtable->global_var = lc.ir_ctx.CreateGlobal(
        vtable_name,
        ir::IRType::Array(ir::IRType::Pointer(ir::IRType::I8()), vtable->entries.size()),
        true  // const
    );
    
    return vtable;
}

// Lower 类声明
bool LowerRecord(const std::shared_ptr<RecordDecl> &record, LoweringContext &lc) {
    if (record->is_forward) {
        // 前向声明，暂不处理
        return true;
    }
    
    lc.current_class = record->name;
    
    // 构建类布局
    ir::ClassLayout layout;
    layout.class_name = record->name;
    
    // 处理基类（支持多继承和虚继承）
    size_t current_field_offset = 0;
    size_t base_index = 0;
    
    // 第一遍：处理虚基类
    std::unordered_set<std::string> processed_virtual_bases;
    for (auto &base : record->bases) {
        if (base.is_virtual) {
            // 虚基类
            layout.virtual_bases.push_back(base.name);
            
            // 检查是否已经处理过（菱形继承中，虚基类只存储一次）
            if (processed_virtual_bases.count(base.name) > 0) {
                continue;
            }
            processed_virtual_bases.insert(base.name);
            
            auto *base_layout = lc.class_metadata.GetLayout(base.name);
            if (base_layout) {
                // 虚基类放在对象的最后
                // 先记录，稍后添加
                layout.virtual_base_offsets[base.name] = static_cast<size_t>(-1);  // 稍后更新
            }
        }
    }
    
    // 如果有虚基类，需要虚基类表（vbtable）
    if (!layout.virtual_bases.empty()) {
        layout.has_vbtable = true;
        layout.vbtable_offset = current_field_offset;
        
        // 添加 vbtable 指针
        layout.field_names.push_back("__vbtable");
        layout.field_offsets["__vbtable"] = current_field_offset;
        current_field_offset++;
    }
    
    // 第二遍：处理普通基类
    for (auto &base : record->bases) {
        if (base.is_virtual) {
            // 虚基类已处理
            continue;
        }
        
        layout.base_classes.push_back(base.name);
        
        auto *base_layout = lc.class_metadata.GetLayout(base.name);
        if (base_layout) {
            // 为每个基类添加 vtable 指针（如果有）
            if (base_layout->has_vtable) {
                std::string vptr_name = "__vptr_" + base.name;
                layout.field_names.push_back(vptr_name);
                layout.field_offsets[vptr_name] = current_field_offset;
                layout.base_vtable_offsets[base.name] = current_field_offset;
                
                // 第一个基类的 vtable 作为主 vtable
                if (base_index == 0) {
                    layout.has_vtable = true;
                    layout.vtable_offset = current_field_offset;
                }
                
                // 保存基类的 vtable（用于多继承）
                if (base_layout->vtable) {
                    layout.base_vtables[base.name] = base_layout->vtable;
                }
                
                current_field_offset++;
            }
            
            // 复制基类的非 vtable 字段
            for (size_t i = 0; i < base_layout->field_names.size(); ++i) {
                const auto &field_name = base_layout->field_names[i];
                // 跳过 vtable 指针字段
                if (field_name.find("__vptr") == 0) continue;
                
                layout.field_names.push_back(field_name);
                layout.field_offsets[field_name] = current_field_offset++;
            }
        }
        
        base_index++;
    }
    
    // 构建 vtable（如果有虚函数）
    layout.vtable = BuildVTable(record, lc);
    if (layout.vtable && !layout.has_vtable) {
        // 这个类首次引入 vtable（没有虚基类）
        layout.has_vtable = true;
        std::string vptr_name = "__vptr";
        layout.field_names.insert(layout.field_names.begin(), vptr_name);
        layout.vtable_offset = 0;
        
        // 调整其他字段的偏移
        for (auto &kv : layout.field_offsets) {
            kv.second += 1;
        }
        layout.field_offsets[vptr_name] = 0;
        current_field_offset++;
    }
    
    // 添加本类的字段
    std::vector<ir::IRType> field_types;
    
    // 为每个基类的 vtable 指针添加类型
    for (const auto &base : record->bases) {
        auto *base_layout = lc.class_metadata.GetLayout(base.name);
        if (base_layout && base_layout->has_vtable) {
            field_types.push_back(ir::IRType::Pointer(ir::IRType::I8()));
        }
        // 添加基类的字段类型
        if (base_layout) {
            for (const auto &base_field_type : base_layout->struct_type.subtypes) {
                // 跳过 vtable 指针（已单独处理）
                if (base_field_type.kind == ir::IRTypeKind::kPointer) continue;
                field_types.push_back(base_field_type);
            }
        }
    }
    
    // 如果本类引入了 vtable 但没有基类
    if (layout.has_vtable && record->bases.empty()) {
        field_types.push_back(ir::IRType::Pointer(ir::IRType::I8()));
    }
    
    // 添加本类声明的字段
    for (auto &field : record->fields) {
        layout.field_names.push_back(field.name);
        layout.field_offsets[field.name] = current_field_offset++;
        
        ir::IRType field_type = ToIRType(field.type);
        field_types.push_back(field_type);
        
        // 注册字段信息（用于访问控制）
        ir::FieldInfo field_info;
        field_info.name = field.name;
        field_info.type = field_type;
        field_info.access = field.access.empty() ? "public" : field.access;  // 默认 public
        field_info.is_static = field.is_static;
        field_info.is_const = field.is_constexpr;
        field_info.is_mutable = field.is_mutable;
        lc.class_metadata.RegisterField(record->name, field_info);
    }
    
    // 添加虚基类的字段（放在最后）
    for (const auto &vbase_name : layout.virtual_bases) {
        auto *vbase_layout = lc.class_metadata.GetLayout(vbase_name);
        if (vbase_layout) {
            // 更新虚基类偏移
            layout.virtual_base_offsets[vbase_name] = current_field_offset;
            
            // 添加虚基类的字段
            for (size_t i = 0; i < vbase_layout->field_names.size(); ++i) {
                const auto &field_name = vbase_layout->field_names[i];
                if (field_name.find("__vptr") == 0 || field_name == "__vbtable") continue;
                
                layout.field_names.push_back("__vbase_" + vbase_name + "_" + field_name);
                layout.field_offsets["__vbase_" + vbase_name + "_" + field_name] = current_field_offset++;
                
                // 添加字段类型
                if (i < vbase_layout->struct_type.subtypes.size()) {
                    field_types.push_back(vbase_layout->struct_type.subtypes[i]);
                }
            }
        }
    }
    
    // 创建结构类型
    layout.struct_type = ir::IRType::Struct(record->name, field_types);
    
    // 注册类布局
    lc.class_metadata.RegisterClass(record->name, layout);
    
    // Lower 方法
    for (auto &method_stmt : record->methods) {
        if (auto func = std::dynamic_pointer_cast<FunctionDecl>(method_stmt)) {
            // 注册方法信息
            ir::MethodInfo method_info;
            method_info.name = func->name;
            method_info.mangled_name = record->name + "::" + func->name;
            method_info.return_type = ToIRType(func->return_type);
            
            // 使用 FunctionDecl 的标志（已由 parser 解析）
            method_info.is_virtual = func->is_virtual;
            method_info.is_pure_virtual = func->is_pure_virtual;
            method_info.is_static = func->is_static;
            method_info.is_const = func->is_const_qualified;
            method_info.access = func->access;
            
            lc.class_metadata.RegisterMethod(record->name, method_info);
            
            // Lower 非纯虚方法
            if (!method_info.is_pure_virtual) {
                // 创建修饰后的函数
                FunctionDecl mangled_func = *func;
                mangled_func.name = method_info.mangled_name;
                
                // 添加隐式 this 参数（如果不是静态方法）
                if (!method_info.is_static) {
                    // this 指针类型
                    auto this_type = std::make_shared<PointerType>();
                    this_type->pointee = std::make_shared<SimpleType>();
                    std::dynamic_pointer_cast<SimpleType>(this_type->pointee)->name = record->name;
                    
                    FunctionDecl::Param this_param;
                    this_param.name = "this";
                    this_param.type = this_type;
                    mangled_func.params.insert(mangled_func.params.begin(), this_param);
                }
                
                // 特殊处理：构造函数需要初始化 vtable 指针
                if (func->is_constructor && layout.has_vtable && layout.vtable) {
                    // 在构造函数体前插入 vtable 初始化代码
                    // 创建赋值语句：this->__vptr = &__vtable_ClassName
                    
                    // 这需要在实际的函数 lowering 前准备好
                    // 简化实现：在 LowerFunction 中检测构造函数并添加初始化
                }
                
                if (!LowerFunction(mangled_func, lc)) {
                    return false;
                }
            }
        }
    }
    
    // 注册 RTTI TypeInfo
    ir::TypeInfo type_info;
    type_info.class_name = record->name;
    type_info.mangled_name = "_ZTI" + std::to_string(record->name.length()) + record->name;
    type_info.base_types = layout.base_classes;
    type_info.has_virtual_functions = layout.has_vtable;
    lc.class_metadata.RegisterTypeInfo(record->name, type_info);
    
    lc.current_class.clear();
    return true;
}

// 处理模板声明
bool LowerTemplate(const std::shared_ptr<TemplateDecl> &tmpl, LoweringContext &lc) {
    // 提取模板参数
    std::vector<ir::TemplateParameter> params;
    for (const auto &param_name : tmpl->params) {
        ir::TemplateParameter param;
        param.name = param_name;
        param.is_typename = true;  // 简化：假设所有参数都是类型参数
        params.push_back(param);
    }
    
    // 检查内部声明的类型
    if (auto func = std::dynamic_pointer_cast<FunctionDecl>(tmpl->inner)) {
        // 函数模板
        lc.template_instantiator.RegisterFunctionTemplate(
            func->name,
            params,
            tmpl->inner.get()
        );
        
        // 不立即lower，等待实例化时再处理
        return true;
        
    } else if (auto record = std::dynamic_pointer_cast<RecordDecl>(tmpl->inner)) {
        // 类模板
        lc.template_instantiator.RegisterClassTemplate(
            record->name,
            params,
            tmpl->inner.get()
        );
        
        // 不立即lower，等待实例化时再处理
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
