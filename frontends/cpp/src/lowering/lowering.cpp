#include "frontends/common/include/native_string_literal.h"
#include "frontends/common/include/native_builtins.h"
/**
 * @file     lowering.cpp
 * @brief    C++ language frontend implementation
 *
 * @ingroup  Frontend / C++
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <optional>
#include <string>
#include <unordered_map>

#include "middle/include/ir/class_metadata.h"
#include "middle/include/ir/ir_builder.h"
#include "middle/include/ir/ir_printer.h"
#include "middle/include/ir/template_instantiator.h"

#include "common/include/core/types.h"
#include "frontends/cpp/include/cpp_lowering.h"

namespace polyglot::cpp {
namespace {

using Name = std::string;

// Type helper functions remain unchanged.

ir::IRType ToIRType(const core::Type &t) {
  using Kind = core::TypeKind;
  switch (t.kind) {
  case Kind::kInt: {
    const int bits = t.bit_width == 0 ? 64 : t.bit_width;
    if (bits == 8)
      return ir::IRType::I8(t.is_signed);
    if (bits == 16)
      return ir::IRType::I16(t.is_signed);
    if (bits == 32)
      return ir::IRType::I32(t.is_signed);
    if (bits == 64)
      return ir::IRType::I64(t.is_signed);
    return ir::IRType::Invalid();
  }
  case Kind::kFloat:
    if (t.bit_width == 0 || t.bit_width == 64)
      return ir::IRType::F64();
    if (t.bit_width == 32)
      return ir::IRType::F32();
    return ir::IRType::Invalid();
  case Kind::kBool:
    return ir::IRType::I1();
  case Kind::kVoid:
    return ir::IRType::Void();
  case Kind::kPointer:
    if (!t.type_args.empty())
      return ir::IRType::Pointer(ToIRType(t.type_args[0]));
    return ir::IRType::Pointer(ir::IRType::Invalid());
  case Kind::kReference:
    if (!t.type_args.empty())
      return ir::IRType::Reference(ToIRType(t.type_args[0]));
    return ir::IRType::Reference(ir::IRType::Invalid());
  case Kind::kStruct:
  case Kind::kClass:
    // A named aggregate may not have been laid out yet.  Keep its identity in
    // the IR type; layout-aware sites replace this forward shape with the
    // registered ClassLayout below.
    return ir::IRType::Struct(t.name, {});
  default:
    return ir::IRType::Invalid();
  }
}

ir::IRType ToIRType(const std::shared_ptr<TypeNode> &node) {
  if (!node)
    return ir::IRType::I64(true);
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
  std::unordered_map<Name, Name> local_addresses;
  std::unordered_map<Name, ir::IRType> function_returns;
  ir::ClassMetadata class_metadata;               // Class metadata management
  ir::TemplateInstantiator template_instantiator; // Template instantiation management
  std::string current_class;                      // Current class being lowered
  ir::IRBuilder builder;
  std::shared_ptr<ir::Function> fn;
  bool terminated{false};
  ir::BasicBlock *loop_exit{nullptr};     // Target block for BREAK
  ir::BasicBlock *loop_continue{nullptr}; // Target block for CONTINUE

  struct LocalObject {
    std::string address;
    std::string class_name;
  };
  // Stack objects whose lifetime is the current function body.  They are
  // destroyed in reverse declaration order before every return.
  std::vector<LocalObject> local_objects;

  LoweringContext(ir::IRContext &ctx, frontends::Diagnostics &d) :
      ir_ctx(ctx), diags(d), builder(ctx) {}
};

ir::IRType ResolveIRType(const std::shared_ptr<TypeNode> &node, const LoweringContext &lc) {
  if (!node)
    return ir::IRType::I64(true);
  if (auto simple = std::dynamic_pointer_cast<SimpleType>(node)) {
    if (const auto *layout = lc.class_metadata.GetLayout(simple->name))
      return layout->struct_type;
    return ToIRType(node);
  }
  if (auto ptr = std::dynamic_pointer_cast<PointerType>(node))
    return ir::IRType::Pointer(ResolveIRType(ptr->pointee, lc));
  if (auto ref = std::dynamic_pointer_cast<ReferenceType>(node))
    return ir::IRType::Reference(ResolveIRType(ref->referent, lc));
  if (auto qualified = std::dynamic_pointer_cast<QualifiedType>(node))
    return ResolveIRType(qualified->inner, lc);
  return ToIRType(node);
}

struct EvalResult {
  Name value;
  ir::IRType type{ir::IRType::Invalid()};
};

bool IsIntegerLiteral(const std::string &text, long long *out) {
  std::string normalized;
  normalized.reserve(text.size());
  for (char c : text) {
    // The lexer accepts '_' as a digit separator.  Also accept the standard
    // C++ apostrophe separator when literals are constructed directly in AST
    // tests or by another frontend stage.
    if (c != '_' && c != '\'')
      normalized.push_back(c);
  }

  // Strip the built-in integer suffix.  This deliberately does not accept an
  // arbitrary user-defined suffix: such a literal needs overload resolution.
  while (!normalized.empty()) {
    const char suffix = normalized.back();
    if (suffix != 'u' && suffix != 'U' && suffix != 'l' && suffix != 'L' && suffix != 'z' &&
        suffix != 'Z') {
      break;
    }
    normalized.pop_back();
  }
  if (normalized.empty())
    return false;

  const char *digits = normalized.c_str();
  int base = 0;
  if (normalized.size() > 2 && normalized[0] == '0' &&
      (normalized[1] == 'b' || normalized[1] == 'B')) {
    digits += 2;
    base = 2;
  } else if (normalized.size() > 2 && normalized[0] == '0' &&
             (normalized[1] == 'o' || normalized[1] == 'O')) {
    digits += 2;
    base = 8;
  }

  char *end = nullptr;
  long long v = std::strtoll(digits, &end, base);
  if (end == digits || *end != '\0')
    return false;
  if (out)
    *out = v;
  return true;
}

bool IsFloatLiteral(const std::string &text, double *out) {
  char *end = nullptr;
  double v = std::strtod(text.c_str(), &end);
  if (end == text.c_str() || (*end != '\0' && *end != 'f' && *end != 'F'))
    return false;
  if (out)
    *out = v;
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

/** @name Member access and virtual calls */
/** @{ */

// Handle subscript/operator[] overloads
EvalResult EvalIndexAccess(const std::shared_ptr<IndexExpression> &idx, LoweringContext &lc) {
  auto obj = EvalExpr(idx->object, lc);
  auto index = EvalExpr(idx->index, lc);

  if (obj.type.kind == ir::IRTypeKind::kInvalid || index.type.kind == ir::IRTypeKind::kInvalid) {
    return {};
  }

  // Check whether this is a class type (which may overload operator[])
  std::string class_name;
  if (obj.type.kind == ir::IRTypeKind::kStruct) {
    class_name = obj.type.name;
  } else if (obj.type.kind == ir::IRTypeKind::kPointer && !obj.type.subtypes.empty() &&
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
          auto call = lc.builder.MakeCall(method.mangled_name, args, method.return_type, "");
          return {call->name, call->type};
        }
      }
    }
  }

  // Plain array access or pointer arithmetic
  if (obj.type.kind == ir::IRTypeKind::kPointer) {
    if (obj.type.subtypes.empty() ||
        obj.type.subtypes[0].kind == ir::IRTypeKind::kInvalid) {
      lc.diags.ReportError(idx->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "C++ subscript requires a modeled pointer element type");
      return {};
    }
    ir::IRType elem_type = obj.type.subtypes[0];

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
    if (size_result.type.kind == ir::IRTypeKind::kInvalid)
      return {};

    const size_t element_size = lc.ir_ctx.Layout().SizeOf(alloc_type);
    if (element_size == 0) {
      lc.diags.ReportError(new_expr->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "C++ array-new element has no modeled storage size");
      return {};
    }
    std::vector<std::string> args = {size_result.value, std::to_string(element_size)};
    auto call =
        lc.builder.MakeCall("__builtin_new_array", args, ir::IRType::Pointer(alloc_type), "");
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
      std::string ctor_name = class_name + "::" + class_name; // ClassName::ClassName

      // Prepare constructor arguments: this pointer + user-provided args
      std::vector<std::string> ctor_args = {alloc_call->name};
      for (const auto &arg : new_expr->args) {
        auto arg_result = EvalExpr(arg, lc);
        if (arg_result.type.kind == ir::IRTypeKind::kInvalid)
          return {};
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
  lc.diags.ReportError(
      typeid_expr->loc, frontends::ErrorCode::kUnsupportedLowering,
      "C++ typeid requires ABI-specific std::type_info identity and polymorphic RTTI lowering");
  return {};
}

// Handle dynamic_cast expressions
bool HasZeroOffsetBasePath(const std::string &base, const std::string &derived,
                           const ir::ClassMetadata &metadata) {
  if (base == derived)
    return true;
  const auto *layout = metadata.GetLayout(derived);
  if (!layout || !layout->virtual_bases.empty() || layout->base_classes.size() != 1)
    return false;
  return HasZeroOffsetBasePath(base, layout->base_classes.front(), metadata);
}

EvalResult EvalDynamicCast(const std::shared_ptr<DynamicCastExpression> &cast_expr,
                           LoweringContext &lc) {
  // Get the source object
  auto src = EvalExpr(cast_expr->operand, lc);
  if (src.type.kind == ir::IRTypeKind::kInvalid) {
    return {};
  }

  // Determine the target type
  ir::IRType target_type = ToIRType(cast_expr->target_type);
  if (target_type.kind == ir::IRTypeKind::kInvalid) {
    lc.diags.ReportError(cast_expr->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "C++ dynamic_cast target has no modeled IR ABI type");
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
  bool is_upcast = lc.class_metadata.IsBaseOf(target_class, src_class);   // upcast
  bool is_downcast = lc.class_metadata.IsBaseOf(src_class, target_class); // downcast

  if (!is_upcast && !is_downcast) {
    lc.diags.Report(cast_expr->loc, "Invalid dynamic_cast: no inheritance relationship");
    return {};
  }

  // Upcasts (derived → base) are known-safe at compile time
  if (is_upcast) {
    if (!HasZeroOffsetBasePath(target_class, src_class, lc.class_metadata)) {
      lc.diags.ReportError(
          cast_expr->loc, frontends::ErrorCode::kUnsupportedLowering,
          "C++ multiple/virtual-inheritance upcast requires pointer adjustment lowering");
      return {};
    }
    // A chain of first, non-virtual bases has offset zero.
    return {src.value, target_type};
  }

  // A checked downcast needs the platform RTTI graph, complete-object pointer
  // adjustment, and null/failure semantics.  A name-based helper is not an
  // ABI-compatible substitute.
  lc.diags.ReportError(cast_expr->loc, frontends::ErrorCode::kUnsupportedLowering,
                       "C++ checked dynamic_cast downcast requires ABI-specific RTTI lowering");
  return {};
}

// Handle static_cast expressions
EvalResult EvalStaticCast(const std::shared_ptr<StaticCastExpression> &cast_expr,
                          LoweringContext &lc) {
  // Get the source object
  auto src = EvalExpr(cast_expr->operand, lc);
  if (src.type.kind == ir::IRTypeKind::kInvalid) {
    return {};
  }

  // Determine the target type
  ir::IRType target_type = ToIRType(cast_expr->target_type);
  if (target_type.kind == ir::IRTypeKind::kInvalid) {
    lc.diags.ReportError(cast_expr->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "unsupported C++ static_cast target type lowering");
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
      cast_kind = src.type.is_signed ? ir::CastInstruction::CastKind::kSExt
                                     : ir::CastInstruction::CastKind::kZExt;
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
      needs_cast = false; // Same float type
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
    if (src.type.subtypes.empty() || target_type.subtypes.empty() ||
        src.type.subtypes[0].kind == ir::IRTypeKind::kInvalid ||
        target_type.subtypes[0].kind == ir::IRTypeKind::kInvalid) {
      lc.diags.ReportError(cast_expr->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "C++ class-pointer static_cast requires layout-aware pointer adjustment");
      return {};
    }
    // Pointer-to-pointer cast is just a bitcast
    cast_kind = ir::CastInstruction::CastKind::kBitcast;
  }
  // Class type conversions (up/down casts in inheritance hierarchy)
  else if ((src.type.kind == ir::IRTypeKind::kPointer ||
            src.type.kind == ir::IRTypeKind::kStruct) &&
           (target_type.kind == ir::IRTypeKind::kPointer ||
            target_type.kind == ir::IRTypeKind::kStruct)) {
    lc.diags.ReportError(cast_expr->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "C++ class static_cast requires layout-aware base adjustment");
    return {};
  }
  // Reference conversions
  else if (src.type.kind == ir::IRTypeKind::kReference ||
           target_type.kind == ir::IRTypeKind::kReference) {
    lc.diags.ReportError(cast_expr->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "C++ reference static_cast lowering is not implemented");
    return {};
  }
  // Boolean conversions (to bool)
  else if (target_type.kind == ir::IRTypeKind::kI1) {
    // Convert any type to bool: compare against zero
    auto cmp = lc.builder.MakeBinary(ir::BinaryInstruction::Op::kCmpNe, src.value, "0", "");
    cmp->type = ir::IRType::I1();
    return {cmp->name, target_type};
  }
  // From boolean to other integer types
  else if (src.type.kind == ir::IRTypeKind::kI1 && target_type.IsInteger()) {
    // bool to int: zero-extend
    cast_kind = ir::CastInstruction::CastKind::kZExt;
  } else {
    lc.diags.ReportError(cast_expr->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "unsupported C++ static_cast conversion lowering");
    return {};
  }

  if (!needs_cast) {
    return {src.value, target_type};
  }

  // Generate the cast instruction
  auto cast_inst = lc.builder.MakeCast(cast_kind, src.value, target_type, "");
  return {cast_inst->name, target_type};
}

std::string ClassNameOf(const EvalResult &obj) {
  if ((obj.type.kind == ir::IRTypeKind::kPointer ||
       obj.type.kind == ir::IRTypeKind::kReference) &&
      !obj.type.subtypes.empty() && obj.type.subtypes[0].kind == ir::IRTypeKind::kStruct) {
    return obj.type.subtypes[0].name;
  }
  if (obj.type.kind == ir::IRTypeKind::kStruct)
    return obj.type.name;
  return {};
}

struct MemberAddressResult {
  std::string address;
  ir::IRType field_type{ir::IRType::Invalid()};
  std::string class_name;
};

MemberAddressResult EvalMemberAddress(const std::shared_ptr<MemberExpression> &mem,
                                      LoweringContext &lc) {
  auto obj = EvalExpr(mem->object, lc);
  if (obj.type.kind == ir::IRTypeKind::kInvalid)
    return {};

  const std::string class_name = ClassNameOf(obj);
  if (class_name.empty()) {
    lc.diags.Report(mem->loc, "Member access on non-class type");
    return {};
  }

  const auto *layout = lc.class_metadata.GetLayout(class_name);
  if (!layout) {
    lc.diags.Report(mem->loc, "Unknown class: " + class_name);
    return {};
  }

  const size_t field_offset = layout->GetFieldOffset(mem->member);
  if (field_offset == static_cast<size_t>(-1)) {
    lc.diags.Report(mem->loc, "Unknown field: " + mem->member);
    return {};
  }

  std::optional<std::string> field_access;
  const auto *fields = lc.class_metadata.GetFields(class_name);
  if (fields) {
    for (const auto &field : *fields) {
      if (field.name == mem->member) {
        field_access = field.access;
        break;
      }
    }
  }
  if (!field_access) {
    lc.diags.ReportError(mem->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "C++ field access metadata is unavailable; access cannot be proven");
    return {};
  }

  bool access_allowed = true;
  if (*field_access == "private") {
    // private: only this class may access
    if (lc.current_class != class_name) {
      access_allowed = false;
    }
  } else if (*field_access == "protected") {
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
  if (!access_allowed) {
    lc.diags.Report(mem->loc, "Cannot access " + *field_access + " member '" + mem->member +
                                  "' of class '" + class_name + "'");
    return {};
  }

  std::string ptr_value = obj.value;
  if (obj.type.kind != ir::IRTypeKind::kPointer &&
      obj.type.kind != ir::IRTypeKind::kReference) {
    // Rvalue aggregate member access is outside the current C++ value model,
    // but materialising an already-lowered aggregate keeps ordinary lvalues
    // representable. Stack object declarations themselves already carry a
    // pointer and do not take this path.
    auto alloca_inst = lc.builder.MakeAlloca(layout->struct_type, obj.value + ".addr");
    lc.builder.MakeStore(alloca_inst->name, obj.value);
    ptr_value = alloca_inst->name;
  }

  if (field_offset >= layout->struct_type.subtypes.size()) {
    lc.diags.Report(mem->loc, "Field offset out of bounds for type " + class_name);
    return {};
  }
  const ir::IRType field_type = layout->struct_type.subtypes[field_offset];

  // IR GEP source_type is the pointer's pointee, not the pointer itself.
  auto gep = lc.builder.MakeGEP(ptr_value, layout->struct_type, {0, field_offset});
  return {gep->name, field_type, class_name};
}

// Handle member access expressions
EvalResult EvalMemberAccess(const std::shared_ptr<MemberExpression> &mem, LoweringContext &lc) {
  auto address = EvalMemberAddress(mem, lc);
  if (address.field_type.kind == ir::IRTypeKind::kInvalid)
    return {};
  auto load = lc.builder.MakeLoad(address.address, address.field_type);
  return {load->name, address.field_type};
}

EvalResult EvalIdentifier(const std::shared_ptr<Identifier> &id, LoweringContext &lc) {
  auto it = lc.env.find(id->name);
  if (it == lc.env.end()) {
    lc.diags.Report(id->loc, "Undefined identifier: " + id->name);
    return {};
  }
  auto address = lc.local_addresses.find(id->name);
  if (address != lc.local_addresses.end()) {
    auto load = lc.builder.MakeLoad(address->second, it->second.type);
    return {load->name, load->type};
  }
  return {it->second.value, it->second.type};
}

std::optional<ir::BinaryInstruction::Op> MapBinOp(const std::string &op, bool is_float,
                                                  bool is_signed) {
  if (is_float) {
    if (op == "+")
      return ir::BinaryInstruction::Op::kFAdd;
    if (op == "-")
      return ir::BinaryInstruction::Op::kFSub;
    if (op == "*")
      return ir::BinaryInstruction::Op::kFMul;
    if (op == "/")
      return ir::BinaryInstruction::Op::kFDiv;
    if (op == "%")
      return ir::BinaryInstruction::Op::kFRem;
    if (op == "==")
      return ir::BinaryInstruction::Op::kCmpFoe;
    if (op == "!=")
      return ir::BinaryInstruction::Op::kCmpFne;
    if (op == "<")
      return ir::BinaryInstruction::Op::kCmpFlt;
    if (op == "<=")
      return ir::BinaryInstruction::Op::kCmpFle;
    if (op == ">")
      return ir::BinaryInstruction::Op::kCmpFgt;
    if (op == ">=")
      return ir::BinaryInstruction::Op::kCmpFge;
    return std::nullopt;
  }
  if (op == "+")
    return ir::BinaryInstruction::Op::kAdd;
  if (op == "-")
    return ir::BinaryInstruction::Op::kSub;
  if (op == "*")
    return ir::BinaryInstruction::Op::kMul;
  if (op == "/")
    return is_signed ? ir::BinaryInstruction::Op::kSDiv : ir::BinaryInstruction::Op::kUDiv;
  if (op == "%")
    return is_signed ? ir::BinaryInstruction::Op::kSRem : ir::BinaryInstruction::Op::kURem;
  if (op == "==")
    return ir::BinaryInstruction::Op::kCmpEq;
  if (op == "!=")
    return ir::BinaryInstruction::Op::kCmpNe;
  if (op == "<")
    return is_signed ? ir::BinaryInstruction::Op::kCmpSlt : ir::BinaryInstruction::Op::kCmpUlt;
  if (op == "<=")
    return is_signed ? ir::BinaryInstruction::Op::kCmpSle : ir::BinaryInstruction::Op::kCmpUle;
  if (op == ">")
    return is_signed ? ir::BinaryInstruction::Op::kCmpSgt : ir::BinaryInstruction::Op::kCmpUgt;
  if (op == ">=")
    return is_signed ? ir::BinaryInstruction::Op::kCmpSge : ir::BinaryInstruction::Op::kCmpUge;
  return std::nullopt;
}

EvalResult ToCondition(const EvalResult &value, LoweringContext &lc,
                       const core::SourceLoc &loc) {
  if (value.type.kind == ir::IRTypeKind::kI1) return value;
  if (!value.type.IsScalar() && value.type.kind != ir::IRTypeKind::kPointer) {
    lc.diags.ReportError(loc, frontends::ErrorCode::kUnsupportedLowering,
                         "C++ contextual boolean conversion requires a scalar value");
    return {};
  }
  const auto zero = value.type.IsFloat() ? MakeFloatLiteral(0.0, lc).value : "0";
  const auto op = value.type.IsFloat() ? ir::BinaryInstruction::Op::kCmpFne
                                      : ir::BinaryInstruction::Op::kCmpNe;
  auto comparison = lc.builder.MakeBinary(op, value.value, zero, "truth");
  comparison->type = ir::IRType::I1();
  return {comparison->name, comparison->type};
}

EvalResult EvalBinary(const std::shared_ptr<BinaryExpression> &bin, LoweringContext &lc) {
  if (bin->op == "&&" || bin->op == "||") {
    const bool conjunction = bin->op == "&&";
    auto lhs = ToCondition(EvalExpr(bin->left, lc), lc, bin->loc);
    if (lhs.type.kind == ir::IRTypeKind::kInvalid) return {};
    auto *lhs_end = lc.builder.GetInsertPoint().get();
    auto rhs_block = lc.builder.CreateBlock("logic.rhs");
    auto merge_block = lc.builder.CreateBlock("logic.merge");
    lc.builder.MakeCondBranch(lhs.value, conjunction ? rhs_block.get() : merge_block.get(),
                             conjunction ? merge_block.get() : rhs_block.get());
    lc.builder.SetInsertPoint(rhs_block);
    auto rhs = ToCondition(EvalExpr(bin->right, lc), lc, bin->loc);
    if (rhs.type.kind == ir::IRTypeKind::kInvalid) return {};
    // Nested logical expressions may end in a different block from rhs_block.
    auto *rhs_end = lc.builder.GetInsertPoint().get();
    lc.builder.MakeBranch(merge_block.get());
    lc.builder.SetInsertPoint(merge_block);
    auto phi = lc.builder.MakePhi(ir::IRType::I1(),
        {{lhs_end, conjunction ? "0" : "1"}, {rhs_end, rhs.value}}, "logic.result");
    return {phi->name, phi->type};
  }

  // A three-way comparison produces a comparison-category object, not a
  // boolean or arithmetic value.  Until that ABI type is represented in IR,
  // reject it explicitly instead of MapBinOp's historical fallback to add.
  if (bin->op == "<=>") {
    lc.diags.ReportError(bin->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "C++ three-way comparison lowering is not implemented");
    return {};
  }

  const bool is_assignment =
      bin->op == "=" || bin->op == "+=" || bin->op == "-=" || bin->op == "*=" ||
      bin->op == "/=" || bin->op == "%=";
  if (is_assignment) {
    if (auto id = std::dynamic_pointer_cast<Identifier>(bin->left);
        id && lc.local_addresses.count(id->name)) {
      auto rhs = EvalExpr(bin->right, lc);
      if (rhs.type.kind == ir::IRTypeKind::kInvalid) return {};
      auto type = lc.env.at(id->name).type;
      if (bin->op != "=") {
        auto old = lc.builder.MakeLoad(lc.local_addresses.at(id->name), type);
        auto op = MapBinOp(bin->op.substr(0, bin->op.size() - 1), type.IsFloat(), type.IsSigned());
        if (!op) return {};
        auto value = lc.builder.MakeBinary(*op, old->name, rhs.value, "");
        value->type = type; rhs = {value->name, type};
      }
      lc.builder.MakeStore(lc.local_addresses.at(id->name), rhs.value);
      return rhs;
    }
    auto member = std::dynamic_pointer_cast<MemberExpression>(bin->left);
    if (!member) {
      lc.diags.ReportError(bin->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "C++ assignment lowering currently requires a data member lvalue");
      return {};
    }
    auto address = EvalMemberAddress(member, lc);
    auto rhs = EvalExpr(bin->right, lc);
    if (address.field_type.kind == ir::IRTypeKind::kInvalid ||
        rhs.type.kind == ir::IRTypeKind::kInvalid) {
      return {};
    }

    EvalResult assigned = rhs;
    if (bin->op != "=") {
      auto old_value = lc.builder.MakeLoad(address.address, address.field_type);
      const std::string arithmetic_op = bin->op.substr(0, bin->op.size() - 1);
      const bool is_float = address.field_type.IsFloat();
      auto op = MapBinOp(arithmetic_op, is_float, address.field_type.is_signed);
      if (!op) {
        lc.diags.ReportError(bin->loc, frontends::ErrorCode::kUnsupportedLowering,
                             "unsupported C++ compound assignment lowering: " + bin->op);
        return {};
      }
      auto value = lc.builder.MakeBinary(*op, old_value->name, rhs.value, "");
      value->type = address.field_type;
      assigned = {value->name, address.field_type};
    }
    lc.builder.MakeStore(address.address, assigned.value);
    return assigned;
  }

  auto lhs = EvalExpr(bin->left, lc);
  auto rhs = EvalExpr(bin->right, lc);
  if (lhs.type.kind == ir::IRTypeKind::kInvalid || rhs.type.kind == ir::IRTypeKind::kInvalid)
    return {};

  // Operator overloading: if the left operand is a class type, look for overloaded operators
  if (lhs.type.kind == ir::IRTypeKind::kStruct ||
      (lhs.type.kind == ir::IRTypeKind::kPointer && !lhs.type.subtypes.empty() &&
       lhs.type.subtypes[0].kind == ir::IRTypeKind::kStruct)) {
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
          auto call = lc.builder.MakeCall(method.mangled_name, args, method.return_type, "");
          return {call->name, call->type};
        }
      }
    }
  }

  // Fall back to built-in arithmetic if no overload is found
  bool is_float = (lhs.type.kind == ir::IRTypeKind::kF32 || lhs.type.kind == ir::IRTypeKind::kF64);
  auto op = MapBinOp(bin->op, is_float, lhs.type.is_signed);
  if (!op) {
    lc.diags.ReportError(bin->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "unsupported C++ binary operator lowering: " + bin->op);
    return {};
  }
  auto inst = lc.builder.MakeBinary(*op, lhs.value, rhs.value, "");
  // Set result type (cmp yields i1).
  switch (*op) {
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
  if (auto id = std::dynamic_pointer_cast<Identifier>(call->callee)) {
    if (const auto *api = frontends::FindNativeBuiltin(id->name)) {
      std::vector<std::string> values;
      std::vector<ir::IRType> types;
      for (size_t i = 0; i < call->args.size(); ++i) {
        auto literal = std::dynamic_pointer_cast<Literal>(call->args[i]);
        if (literal && i < api->params.size() && api->params[i] == frontends::NativeType::kString) {
          const auto &text = literal->value;
          std::string body, decoded, error;
          bool raw = false;
          if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
            body = text.substr(1, text.size() - 2);
          } else if (text.rfind("R\"", 0) == 0) {
            auto open = text.find('(', 2);
            const auto delimiter = open == std::string::npos ? "" : text.substr(2, open - 2);
            const auto end = ")" + delimiter + "\"";
            if (open == std::string::npos || !text.ends_with(end)) {
              lc.diags.ReportError(literal->loc, frontends::ErrorCode::kUnsupportedLowering,
                                   "invalid native raw string literal");
              return {};
            }
            body = text.substr(open + 1, text.size() - end.size() - open - 1); raw = true;
          } else {
            lc.diags.ReportError(literal->loc, frontends::ErrorCode::kUnsupportedLowering,
                                 "native text APIs require a narrow string literal or arg_text result");
            return {};
          }
          if (!frontends::DecodeNativeString(body, raw, false, decoded, error)) {
            lc.diags.ReportError(literal->loc, frontends::ErrorCode::kUnsupportedLowering, error);
            return {};
          }
          values.push_back(lc.builder.MakeStringLiteral(decoded, "cpp.native.text"));
          types.push_back(frontends::NativeIRType(frontends::NativeType::kString));
        } else {
          auto value = EvalExpr(call->args[i], lc);
          if (value.type.kind == ir::IRTypeKind::kInvalid) return {};
          values.push_back(value.value); types.push_back(value.type);
        }
      }
      auto value = frontends::EmitNativeBuiltin(*api, values, types, lc.builder, lc.ir_ctx, lc.diags, call->loc);
      return value ? EvalResult{value->name, value->type} : EvalResult{};
    }
  }

  std::vector<std::string> args;
  std::vector<ir::IRType> arg_types;
  for (const auto &arg : call->args) {
    auto ev = EvalExpr(arg, lc);
    if (ev.type.kind == ir::IRTypeKind::kInvalid)
      return {};
    args.push_back(ev.value);
    arg_types.push_back(ev.type);
  }

  if (auto member = std::dynamic_pointer_cast<MemberExpression>(call->callee)) {
    auto object = EvalExpr(member->object, lc);
    const std::string class_name = ClassNameOf(object);
    if (class_name.empty()) {
      lc.diags.ReportError(member->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "C++ member call requires a modeled class object");
      return {};
    }
    const auto *layout = lc.class_metadata.GetLayout(class_name);
    const auto *methods = lc.class_metadata.GetMethods(class_name);
    if (!layout || !methods) {
      lc.diags.Report(member->loc, "Unknown class method: " + class_name + "::" + member->member);
      return {};
    }

    const ir::MethodInfo *selected = nullptr;
    for (const auto &method : *methods) {
      if (method.name != member->member)
        continue;
      if (method.param_types.size() != arg_types.size())
        continue;
      selected = &method;
      break;
    }
    if (!selected) {
      lc.diags.Report(member->loc, "Unknown class method: " + class_name + "::" + member->member);
      return {};
    }
    if (selected->access != "public" && lc.current_class != class_name) {
      lc.diags.Report(member->loc, "Cannot call " + selected->access + " method '" +
                                       member->member + "' of class '" + class_name + "'");
      return {};
    }

    if (!selected->is_static) {
      std::string this_value = object.value;
      if (object.type.kind != ir::IRTypeKind::kPointer &&
          object.type.kind != ir::IRTypeKind::kReference) {
        auto address = lc.builder.MakeAlloca(layout->struct_type, object.value + ".addr");
        lc.builder.MakeStore(address->name, object.value);
        this_value = address->name;
      }
      args.insert(args.begin(), this_value);
    }
    auto inst = lc.builder.MakeCall(selected->mangled_name, args, selected->return_type, "");
    return {inst->name, inst->type};
  }

  std::string callee_name;
  if (auto id = std::dynamic_pointer_cast<Identifier>(call->callee)) {
    callee_name = id->name;
  } else if (auto tid = std::dynamic_pointer_cast<TemplateIdExpression>(call->callee)) {
    lc.diags.ReportError(
        tid->loc, frontends::ErrorCode::kUnsupportedLowering,
        "explicit C++ template call lowering requires monomorphization and is not implemented");
    return {};
  } else {
    lc.diags.ReportError(
        call->loc, frontends::ErrorCode::kUnsupportedLowering,
        "C++ indirect and virtual function-call lowering requires a modeled callable ABI");
    return {};
  }

  if (const auto *api = frontends::FindNativeBuiltin(callee_name)) {
    auto inst = frontends::EmitNativeBuiltin(*api, args, arg_types, lc.builder, lc.ir_ctx, lc.diags, call->loc);
    if (!inst) return {};
    return {inst->name, inst->type};
  }
  auto known = lc.function_returns.find(callee_name);
  if (known == lc.function_returns.end()) {
    lc.diags.ReportError(call->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "C++ call target '" + callee_name +
                             "' has no modeled function signature");
    return {};
  }

  auto inst = lc.builder.MakeCall(callee_name, args, known->second, "");
  return {inst->name, inst->type};
}

EvalResult EvalExpr(const std::shared_ptr<Expression> &expr, LoweringContext &lc) {
  if (!expr)
    return {};
  if (auto unary = std::dynamic_pointer_cast<UnaryExpression>(expr)) {
    if (unary->op == "co_await" || unary->op == "co_yield") {
      lc.diags.ReportError(
          unary->loc, frontends::ErrorCode::kUnsupportedLowering,
          std::string("C++ coroutine operator '") + unary->op +
              "' requires coroutine frame lowering, which is not implemented");
      return {};
    }
    auto operand = EvalExpr(unary->operand, lc);
    if (operand.type.kind == ir::IRTypeKind::kInvalid) return {};
    using Op = ir::BinaryInstruction::Op;
    if (unary->op == "+" && operand.type.IsScalar()) return operand;
    if (unary->op == "-" && operand.type.IsScalar()) {
      if (operand.type.IsFloat()) {
        auto value = lc.builder.MakeFloatNegate(operand.value, operand.type, "neg");
        return {value->name, value->type};
      }
      auto value = lc.builder.MakeBinary(Op::kSub, "0", operand.value, "neg");
      value->type = operand.type;
      return {value->name, value->type};
    }
    if (unary->op == "!") {
      auto condition = ToCondition(operand, lc, unary->loc);
      if (condition.type.kind == ir::IRTypeKind::kInvalid) return {};
      auto value = lc.builder.MakeBinary(Op::kXor, condition.value, "1", "not");
      value->type = ir::IRType::I1();
      return {value->name, value->type};
    }
    if (unary->op == "~" && operand.type.IsInteger()) {
      auto value = lc.builder.MakeBinary(Op::kXor, operand.value, "-1", "bitnot");
      value->type = operand.type;
      return {value->name, value->type};
    }
    lc.diags.ReportError(unary->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "unsupported C++ unary operator lowering: " + unary->op);
    return {};
  }
  if (auto lit = std::dynamic_pointer_cast<Literal>(expr)) {
    if (lit->value == "true" || lit->value == "false")
      return {lit->value == "true" ? "1" : "0", ir::IRType::I1()};
    // strtod accepts integer spellings such as "0" and "24".  Integer
    // recognition must therefore run first or ordinary business constants
    // become detached cfN values that the native backend treats as vregs.
    long long v{};
    if (IsIntegerLiteral(lit->value, &v))
      return MakeLiteral(v, lc);

    double fv{};
    if (IsFloatLiteral(lit->value, &fv))
      return MakeFloatLiteral(fv, lc);

    lc.diags.Report(lit->loc, "Invalid numeric literal");
    return {};
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
  lc.diags.ReportError(expr->loc, frontends::ErrorCode::kUnsupportedLowering,
                       "unsupported C++ expression lowering");
  return {};
}

bool LowerStmt(const std::shared_ptr<Statement> &stmt, LoweringContext &lc);
bool LowerFunction(const FunctionDecl &fn, LoweringContext &lc);

bool LowerIf(const std::shared_ptr<IfStatement> &if_stmt, LoweringContext &lc) {
  if (lc.terminated)
    return true;

  if (if_stmt->is_consteval) {
    lc.diags.ReportError(
        if_stmt->loc, frontends::ErrorCode::kUnsupportedLowering,
        "C++23 if consteval requires immediate-function evaluation before IR lowering");
    return false;
  }

  if (if_stmt->init && !LowerStmt(if_stmt->init, lc))
    return false;

  auto cond = ToCondition(EvalExpr(if_stmt->condition, lc), lc, if_stmt->loc);
  if (cond.type.kind == ir::IRTypeKind::kInvalid)
    return false;

  auto *then_block = lc.fn->CreateBlock("if.then");
  auto *else_block = if_stmt->else_body.empty() ? nullptr : lc.fn->CreateBlock("if.else");
  auto *merge_block = lc.fn->CreateBlock("if.end");

  // Conditional branch
  lc.builder.MakeCondBranch(cond.value, then_block, else_block ? else_block : merge_block);
  lc.terminated = false;

  // Then block
  for (auto &bb : lc.fn->blocks) {
    if (bb.get() == then_block) {
      lc.builder.SetInsertPoint(bb);
      break;
    }
  }
  bool then_term = false;
  for (auto &s : if_stmt->then_body) {
    if (!LowerStmt(s, lc))
      return false;
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
      if (!LowerStmt(s, lc))
        return false;
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
  // With no else branch, the false edge always reaches the merge block, so
  // statements after the if must still be lowered.  When both explicit
  // branches terminate, keep the otherwise-unreachable merge block valid and
  // propagate termination to the enclosing statement list.
  const bool all_paths_terminate = else_block && then_term && else_term;
  if (all_paths_terminate)
    lc.builder.MakeUnreachable();
  lc.terminated = all_paths_terminate;
  return true;
}

bool LowerWhile(const std::shared_ptr<WhileStatement> &while_stmt, LoweringContext &lc) {
  if (lc.terminated)
    return true;

  auto *cond_block = lc.fn->CreateBlock("while.cond");
  auto *body_block = lc.fn->CreateBlock("while.body");
  auto *exit_block = lc.fn->CreateBlock("while.end");
  auto *old_exit = lc.loop_exit;
  auto *old_continue = lc.loop_continue;
  lc.loop_exit = exit_block;
  lc.loop_continue = cond_block;

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

  auto cond = ToCondition(EvalExpr(while_stmt->condition, lc), lc, while_stmt->loc);
  if (cond.type.kind == ir::IRTypeKind::kInvalid)
    return false;

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
    if (!LowerStmt(s, lc))
      return false;
    if (lc.terminated)
      break;
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
  lc.loop_exit = old_exit;
  lc.loop_continue = old_continue;
  return true;
}

bool LowerFor(const std::shared_ptr<ForStatement> &for_stmt, LoweringContext &lc) {
  if (lc.terminated)
    return true;

  // Init
  if (for_stmt->init && !LowerStmt(for_stmt->init, lc))
    return false;

  auto *cond_block = lc.fn->CreateBlock("for.cond");
  auto *body_block = lc.fn->CreateBlock("for.body");
  auto *inc_block = lc.fn->CreateBlock("for.inc");
  auto *exit_block = lc.fn->CreateBlock("for.end");
  auto *old_exit = lc.loop_exit;
  auto *old_continue = lc.loop_continue;
  lc.loop_exit = exit_block;
  lc.loop_continue = inc_block;

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
    auto cond = ToCondition(EvalExpr(for_stmt->condition, lc), lc, for_stmt->loc);
    if (cond.type.kind == ir::IRTypeKind::kInvalid)
      return false;
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
    if (!LowerStmt(s, lc))
      return false;
    if (lc.terminated)
      break;
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
  lc.loop_exit = old_exit;
  lc.loop_continue = old_continue;
  return true;
}

void EmitLocalDestructors(LoweringContext &lc) {
  for (auto it = lc.local_objects.rbegin(); it != lc.local_objects.rend(); ++it) {
    const auto *methods = lc.class_metadata.GetMethods(it->class_name);
    if (!methods)
      continue;
    const std::string destructor_name = "~" + it->class_name;
    auto destructor = std::find_if(methods->begin(), methods->end(), [&](const auto &method) {
      return method.name == destructor_name && method.param_types.empty();
    });
    if (destructor != methods->end())
      lc.builder.MakeCall(destructor->mangled_name, {it->address}, ir::IRType::Void(), "");
  }
}

bool LowerReturn(const std::shared_ptr<ReturnStatement> &ret, LoweringContext &lc) {
  if (lc.terminated)
    return true;
  if (ret->is_co_return) {
    lc.diags.ReportError(ret->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "C++ co_return requires coroutine frame lowering, which is not implemented");
    return false;
  }
  EvalResult v;
  if (ret->value) {
    v = EvalExpr(ret->value, lc);
    if (v.type.kind == ir::IRTypeKind::kInvalid)
      return false;
  }
  EmitLocalDestructors(lc);
  lc.builder.MakeReturn(v.value);
  lc.terminated = true;
  return true;
}

bool LowerVar(const std::shared_ptr<VarDecl> &var, LoweringContext &lc) {
  if (auto simple = std::dynamic_pointer_cast<SimpleType>(var->type)) {
    if (const auto *layout = lc.class_metadata.GetLayout(simple->name)) {
      if (var->init) {
        lc.diags.ReportError(var->loc, frontends::ErrorCode::kUnsupportedLowering,
                             "C++ class copy/list assignment initialization is not implemented");
        return false;
      }

      auto storage = lc.builder.MakeAlloca(layout->struct_type, var->name + ".addr");
      lc.env[var->name] = {storage->name, ir::IRType::Pointer(layout->struct_type)};

      std::vector<std::string> constructor_args{storage->name};
      std::vector<ir::IRType> argument_types;
      for (const auto &arg : var->direct_init_args) {
        auto value = EvalExpr(arg, lc);
        if (value.type.kind == ir::IRTypeKind::kInvalid)
          return false;
        constructor_args.push_back(value.value);
        argument_types.push_back(value.type);
      }

      const auto *methods = lc.class_metadata.GetMethods(simple->name);
      const ir::MethodInfo *constructor = nullptr;
      if (methods) {
        auto found = std::find_if(methods->begin(), methods->end(), [&](const auto &method) {
          return method.name == simple->name &&
                 method.param_types.size() == argument_types.size();
        });
        if (found != methods->end())
          constructor = &*found;
      }
      if (constructor) {
        lc.builder.MakeCall(constructor->mangled_name, constructor_args, ir::IRType::Void(), "");
      } else if (var->has_direct_init || !argument_types.empty()) {
        lc.diags.ReportError(var->loc, frontends::ErrorCode::kUnsupportedLowering,
                             "no modeled constructor for C++ class '" + simple->name +
                                 "' with " + std::to_string(argument_types.size()) + " argument(s)");
        return false;
      }

      if (methods) {
        const std::string destructor_name = "~" + simple->name;
        if (std::any_of(methods->begin(), methods->end(), [&](const auto &method) {
              return method.name == destructor_name && method.param_types.empty();
            })) {
          lc.local_objects.push_back({storage->name, simple->name});
        }
      }
      return true;
    }
  }

  EvalResult init;
  if (var->has_direct_init) {
    if (var->direct_init_args.size() != 1) {
      lc.diags.ReportError(var->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "scalar C++ direct initialization requires exactly one argument");
      return false;
    }
    init = EvalExpr(var->direct_init_args.front(), lc);
  } else if (var->init) {
    init = EvalExpr(var->init, lc);
  }
  if (init.value.empty()) {
    lc.diags.ReportError(var->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "C++ variable initializer could not be lowered");
    return false;
  }
  auto storage = lc.builder.MakeAlloca(init.type);
  lc.builder.MakeStore(storage->name, init.value);
  lc.local_addresses[var->name] = storage->name;
  lc.env[var->name] = {init.value, init.type};
  return true;
}

// Exception handling lowering
bool LowerThrow(const std::shared_ptr<ThrowStatement> &throw_stmt, LoweringContext &lc) {
  if (lc.terminated)
    return true;

  lc.diags.ReportError(throw_stmt->loc, frontends::ErrorCode::kUnsupportedLowering,
                       "C++ throw requires typed exception-object and unwinding ABI lowering");
  return false;
}

bool LowerTry(const std::shared_ptr<TryStatement> &try_stmt, LoweringContext &lc) {
  if (lc.terminated)
    return true;

  lc.diags.ReportError(try_stmt->loc, frontends::ErrorCode::kUnsupportedLowering,
                       "C++ try/catch requires invoke edges and typed catch-dispatch lowering");
  return false;
}

/** @} */

/** @name Class and inheritance lowering */
/** @{ */

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
      vtable_name, ir::IRType::Array(ir::IRType::Pointer(ir::IRType::I8()), vtable->entries.size()),
      true // const
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
        layout.virtual_base_offsets[base.name] = static_cast<size_t>(-1); // updated later
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
        if (field_name.find("__vptr") == 0)
          continue;

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
        if (base_field_type.kind == ir::IRTypeKind::kPointer)
          continue;
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
    field_info.access =
        field.access.empty() ? (record->kind == "struct" ? "public" : "private") : field.access;
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
        if (field_name.find("__vptr") == 0 || field_name == "__vbtable")
          continue;

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
      method_info.return_type = (func->is_constructor || func->is_destructor)
                                    ? ir::IRType::Void()
                                    : ResolveIRType(func->return_type, lc);
      for (const auto &param : func->params)
        method_info.param_types.push_back(ResolveIRType(param.type, lc));

      // Use flags already parsed from FunctionDecl
      method_info.is_virtual = func->is_virtual;
      method_info.is_pure_virtual = func->is_pure_virtual;
      method_info.is_static = func->is_static;
      method_info.is_const = func->is_const_qualified;
      method_info.access = func->access;

      lc.class_metadata.RegisterMethod(record->name, method_info);

      // Lower non-pure-virtual methods
      if (!method_info.is_pure_virtual) {
        if (func->is_defaulted) {
          lc.diags.ReportError(func->loc, frontends::ErrorCode::kUnsupportedLowering,
                               "defaulted C++ member lowering is not implemented");
          return false;
        }
        if (func->is_deleted || !func->has_body)
          continue;
        // Create the mangled function
        FunctionDecl mangled_func = *func;
        mangled_func.name = method_info.mangled_name;

        // Add the implicit this parameter for non-static methods
        if (!method_info.is_static && !mangled_func.has_explicit_object_parameter) {
          // Type of the this pointer
          auto this_type = std::make_shared<PointerType>();
          this_type->pointee = std::make_shared<SimpleType>();
          std::dynamic_pointer_cast<SimpleType>(this_type->pointee)->name = record->name;

          FunctionDecl::Param this_param;
          this_param.name = "this";
          this_param.type = this_type;
          mangled_func.params.insert(mangled_func.params.begin(), this_param);
        }

        if (func->is_constructor && layout.has_vtable && layout.vtable) {
          lc.diags.ReportError(
              func->loc, frontends::ErrorCode::kUnsupportedLowering,
              "C++ polymorphic constructors require ABI-specific vptr initialization");
          return false;
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
  if (!tmpl->requires_clause.empty()) {
    lc.diags.ReportError(tmpl->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "constrained C++ template lowering is not implemented");
    return false;
  }
  // Gather template parameters
  std::vector<ir::TemplateParameter> params;
  for (const auto &parameter_text : tmpl->params) {
    const bool is_type_parameter = parameter_text.rfind("typename ", 0) == 0 ||
                                   parameter_text.rfind("class ", 0) == 0;
    const auto last_space = parameter_text.find_last_of(" \t");
    const std::string param_name =
        last_space == std::string::npos ? std::string{} : parameter_text.substr(last_space + 1);
    const bool valid_name = !param_name.empty() &&
                            std::all_of(param_name.begin(), param_name.end(), [](char ch) {
                              return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
                            });
    if (!is_type_parameter || !valid_name || parameter_text.find('=') != std::string::npos ||
        parameter_text.find("...") != std::string::npos) {
      lc.diags.ReportError(
          tmpl->loc, frontends::ErrorCode::kUnsupportedLowering,
          "C++ non-type, template-template, pack, constrained, and defaulted template "
          "parameters are not represented by the IR template model");
      return false;
    }
    ir::TemplateParameter param;
    param.name = param_name;
    param.is_typename = true;
    params.push_back(param);
  }

  // Inspect the type of the inner declaration
  if (auto func = std::dynamic_pointer_cast<FunctionDecl>(tmpl->inner)) {
    if (!func->requires_clause.empty()) {
      lc.diags.ReportError(func->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "constrained C++ function template lowering is not implemented");
      return false;
    }
    // Function template
    lc.template_instantiator.RegisterFunctionTemplate(func->name, params, tmpl->inner.get());

    // Do not lower immediately; wait for instantiation
    return true;

  } else if (auto record = std::dynamic_pointer_cast<RecordDecl>(tmpl->inner)) {
    // Class template
    lc.template_instantiator.RegisterClassTemplate(record->name, params, tmpl->inner.get());

    // Do not lower immediately; wait for instantiation
    return true;

  } else if (auto var = std::dynamic_pointer_cast<VarDecl>(tmpl->inner)) {
    lc.diags.ReportError(var->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "C++ variable-template instantiation and storage lowering is not implemented");
    return false;

  } else if (auto alias = std::dynamic_pointer_cast<UsingAliasDeclaration>(tmpl->inner)) {
    lc.diags.ReportError(alias->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "C++ alias-template substitution lowering is not implemented");
    return false;

  } else {
    lc.diags.ReportError(tmpl->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "unsupported C++ template declaration lowering");
    return false;
  }
}

bool LowerStmt(const std::shared_ptr<Statement> &stmt, LoweringContext &lc) {
  if (!stmt || lc.terminated)
    return true;
  if (auto var = std::dynamic_pointer_cast<VarDecl>(stmt))
    return LowerVar(var, lc);
  if (auto ret = std::dynamic_pointer_cast<ReturnStatement>(stmt))
    return LowerReturn(ret, lc);
  if (auto if_stmt = std::dynamic_pointer_cast<IfStatement>(stmt))
    return LowerIf(if_stmt, lc);
  if (auto while_stmt = std::dynamic_pointer_cast<WhileStatement>(stmt))
    return LowerWhile(while_stmt, lc);
  if (auto for_stmt = std::dynamic_pointer_cast<ForStatement>(stmt))
    return LowerFor(for_stmt, lc);
  if (auto try_stmt = std::dynamic_pointer_cast<TryStatement>(stmt))
    return LowerTry(try_stmt, lc);
  if (auto throw_stmt = std::dynamic_pointer_cast<ThrowStatement>(stmt))
    return LowerThrow(throw_stmt, lc);
  if (auto record = std::dynamic_pointer_cast<RecordDecl>(stmt))
    return LowerRecord(record, lc);
  if (auto tmpl = std::dynamic_pointer_cast<TemplateDecl>(stmt))
    return LowerTemplate(tmpl, lc);
  if (auto expr = std::dynamic_pointer_cast<ExprStatement>(stmt)) {
    return EvalExpr(expr->expr, lc).type.kind != ir::IRTypeKind::kInvalid;
  }
  if (auto comp = std::dynamic_pointer_cast<CompoundStatement>(stmt)) {
    for (auto &s : comp->statements) {
      if (!LowerStmt(s, lc))
        return false;
      if (lc.terminated)
        break;
    }
    return true;
  }
  if (std::dynamic_pointer_cast<BreakStatement>(stmt)) {
    // BREAK: jump to the loop exit block (recorded in lc.loop_exit)
    if (!lc.loop_exit) {
      lc.diags.ReportError(stmt->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "C++ break has no active lowering target");
      return false;
    }
    lc.builder.MakeBranch(lc.loop_exit);
    lc.terminated = true;
    return true;
  }
  if (std::dynamic_pointer_cast<ContinueStatement>(stmt)) {
    // CONTINUE: jump to the loop header / condition block
    if (!lc.loop_continue) {
      lc.diags.ReportError(stmt->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "C++ continue has no active lowering target");
      return false;
    }
    lc.builder.MakeBranch(lc.loop_continue);
    lc.terminated = true;
    return true;
  }
  if (auto do_while = std::dynamic_pointer_cast<DoWhileStatement>(stmt)) {
    // do { body } while (cond);
    auto *body_block = lc.fn->CreateBlock("do.body");
    auto *cond_block = lc.fn->CreateBlock("do.cond");
    auto *exit_block = lc.fn->CreateBlock("do.exit");

    auto *old_exit = lc.loop_exit;
    auto *old_cont = lc.loop_continue;
    lc.loop_exit = exit_block;
    lc.loop_continue = cond_block;

    lc.builder.MakeBranch(body_block);
    for (auto &bb : lc.fn->blocks) {
      if (bb.get() == body_block) {
        lc.builder.SetInsertPoint(bb);
        break;
      }
    }
    lc.terminated = false;
    for (auto &s : do_while->body) {
      if (!LowerStmt(s, lc))
        return false;
      if (lc.terminated)
        break;
    }
    if (!lc.terminated)
      lc.builder.MakeBranch(cond_block);

    for (auto &bb : lc.fn->blocks) {
      if (bb.get() == cond_block) {
        lc.builder.SetInsertPoint(bb);
        break;
      }
    }
    lc.terminated = false;
    auto cond = ToCondition(EvalExpr(do_while->condition, lc), lc, do_while->loc);
    lc.builder.MakeCondBranch(cond.value, body_block, exit_block);

    for (auto &bb : lc.fn->blocks) {
      if (bb.get() == exit_block) {
        lc.builder.SetInsertPoint(bb);
        break;
      }
    }
    lc.terminated = false;
    lc.loop_exit = old_exit;
    lc.loop_continue = old_cont;
    return true;
  }
  if (auto fn_decl = std::dynamic_pointer_cast<FunctionDecl>(stmt)) {
    // Nested / local function declaration — lower as a standalone function
    return LowerFunction(*fn_decl, lc);
  }
  // Using, namespace, typedef and import declarations are metadata-only
  if (std::dynamic_pointer_cast<UsingDeclaration>(stmt) ||
      std::dynamic_pointer_cast<UsingNamespaceDeclaration>(stmt) ||
      std::dynamic_pointer_cast<UsingEnumDeclaration>(stmt) ||
      std::dynamic_pointer_cast<NamespaceAliasDeclaration>(stmt) ||
      std::dynamic_pointer_cast<TypedefDeclaration>(stmt) ||
      std::dynamic_pointer_cast<UsingAliasDeclaration>(stmt) ||
      std::dynamic_pointer_cast<ImportDeclaration>(stmt) ||
      std::dynamic_pointer_cast<ModuleDeclaration>(stmt)) {
    return true;
  }
  lc.diags.ReportError(stmt->loc, frontends::ErrorCode::kUnsupportedLowering,
                       "unsupported C++ statement lowering");
  return false;
}

bool LowerFunction(const FunctionDecl &fn, LoweringContext &lc) {
  if (std::any_of(fn.params.begin(), fn.params.end(),
                  [](const FunctionDecl::Param &param) { return param.default_value != nullptr; })) {
    lc.diags.ReportError(fn.loc, frontends::ErrorCode::kUnsupportedLowering,
                         "C++ default arguments require call-site substitution lowering");
    return false;
  }
  if (!fn.has_body)
    return true; // declaration only; no executable semantics to emit
  if (fn.is_coroutine) {
    lc.diags.ReportError(fn.loc, frontends::ErrorCode::kUnsupportedLowering,
                         "C++ coroutine lowering is not implemented");
    return false;
  }
  if (fn.has_explicit_object_parameter ||
      std::any_of(fn.params.begin(), fn.params.end(),
                  [](const FunctionDecl::Param &param) { return param.is_explicit_object; })) {
    lc.diags.ReportError(
        fn.loc, frontends::ErrorCode::kUnsupportedLowering,
        "C++23 explicit object parameter lowering requires member-call ABI support");
    return false;
  }
  // Map signature (minimal: primitive ints/bools/void)
  ir::IRType ret_ty = (fn.is_constructor || fn.is_destructor)
                          ? ir::IRType::Void()
                          : ResolveIRType(fn.return_type, lc);
  if (ret_ty.kind == ir::IRTypeKind::kInvalid) {
    lc.diags.ReportError(fn.return_type ? fn.return_type->loc : fn.loc,
                         frontends::ErrorCode::kUnsupportedLowering,
                         "unsupported C++ return type lowering");
    return false;
  }

  std::vector<std::pair<std::string, ir::IRType>> params;
  params.reserve(fn.params.size());
  for (auto &p : fn.params) {
    ir::IRType pt = ResolveIRType(p.type, lc);
    if (pt.kind == ir::IRTypeKind::kInvalid) {
      lc.diags.ReportError(p.type ? p.type->loc : fn.loc,
                           frontends::ErrorCode::kUnsupportedLowering,
                           "unsupported C++ parameter type lowering");
      return false;
    }
    params.push_back({p.name, pt});
  }

  lc.fn = lc.ir_ctx.CreateFunction(fn.name, ret_ty, params);
  lc.builder.SetCurrentFunction(lc.fn);
  // Create entry block and start inserting there.
  auto *entry = lc.fn->CreateBlock("entry");
  lc.fn->entry = entry;
  if (!lc.fn->blocks.empty()) {
    lc.builder.SetInsertPoint(lc.fn->blocks.back());
  }

  lc.env.clear();
  lc.local_addresses.clear();
  lc.local_objects.clear();
  for (const auto &p : params) {
    lc.env[p.first] = {p.first, p.second};
    // Scalar parameters are local variables in C++; assigning one must not
    // overwrite the incoming SSA definition or be rejected as a non-lvalue.
    if (p.second.IsScalar()) {
      auto storage = lc.builder.MakeAlloca(p.second, p.first + ".addr");
      lc.builder.MakeStore(storage->name, p.first);
      lc.local_addresses[p.first] = storage->name;
    }
  }
  lc.terminated = false;

  for (auto &stmt : fn.body) {
    if (!LowerStmt(stmt, lc))
      return false;
    if (lc.terminated)
      break;
  }

  if (!lc.terminated) {
    if (ret_ty.kind == ir::IRTypeKind::kVoid) {
      EmitLocalDestructors(lc);
      lc.builder.MakeReturn("");
    } else {
      lc.diags.ReportError(fn.loc, frontends::ErrorCode::kUnsupportedLowering,
                           "non-void C++ function may reach the end without returning a value");
      return false;
    }
  }
  return true;
}

} // namespace

void LowerToIR(const Module &module, ir::IRContext &ctx, frontends::Diagnostics &diags) {
  LoweringContext lc(ctx, diags);
  const auto collect_signatures = [&](const auto &self,
                                      const std::shared_ptr<Statement> &decl) -> void {
    if (!decl)
      return;
    if (auto fn = std::dynamic_pointer_cast<FunctionDecl>(decl)) {
      auto ret = (fn->is_constructor || fn->is_destructor) ? ir::IRType::Void()
                                                            : ToIRType(fn->return_type);
      if (ret.kind != ir::IRTypeKind::kInvalid)
        lc.function_returns[fn->name] = ret;
      return;
    }
    if (auto ns = std::dynamic_pointer_cast<NamespaceDecl>(decl)) {
      for (const auto &member : ns->members)
        self(self, member);
      return;
    }
    if (auto record = std::dynamic_pointer_cast<RecordDecl>(decl)) {
      for (const auto &member : record->methods) {
        if (auto fn = std::dynamic_pointer_cast<FunctionDecl>(member)) {
          auto ret = (fn->is_constructor || fn->is_destructor) ? ir::IRType::Void()
                                                                : ToIRType(fn->return_type);
          if (ret.kind != ir::IRTypeKind::kInvalid)
            lc.function_returns[record->name + "::" + fn->name] = ret;
        }
      }
    }
  };
  for (const auto &decl : module.declarations)
    collect_signatures(collect_signatures, decl);

  auto lower_top_level = [&](const auto &self, const std::shared_ptr<Statement> &decl) -> bool {
    if (!decl)
      return true;
    if (auto fn = std::dynamic_pointer_cast<FunctionDecl>(decl)) {
      if (fn->is_defaulted) {
        diags.ReportError(fn->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "defaulted C++ function lowering is not implemented");
        return false;
      }
      if (fn->is_deleted || !fn->has_body)
        return true;
      return LowerFunction(*fn, lc);
    }
    if (auto record = std::dynamic_pointer_cast<RecordDecl>(decl))
      return LowerRecord(record, lc);
    if (auto tmpl = std::dynamic_pointer_cast<TemplateDecl>(decl))
      return LowerTemplate(tmpl, lc);
    if (auto ns = std::dynamic_pointer_cast<NamespaceDecl>(decl)) {
      for (const auto &member : ns->members) {
        if (!self(self, member))
          return false;
      }
      return true;
    }
    if (std::dynamic_pointer_cast<UsingDeclaration>(decl) ||
        std::dynamic_pointer_cast<UsingNamespaceDeclaration>(decl) ||
        std::dynamic_pointer_cast<UsingEnumDeclaration>(decl) ||
        std::dynamic_pointer_cast<NamespaceAliasDeclaration>(decl) ||
        std::dynamic_pointer_cast<TypedefDeclaration>(decl) ||
        std::dynamic_pointer_cast<UsingAliasDeclaration>(decl) ||
        std::dynamic_pointer_cast<ImportDeclaration>(decl) ||
        std::dynamic_pointer_cast<ModuleDeclaration>(decl) ||
        std::dynamic_pointer_cast<EnumDecl>(decl) ||
        std::dynamic_pointer_cast<ForwardDecl>(decl) ||
        std::dynamic_pointer_cast<ConceptDecl>(decl) ||
        std::dynamic_pointer_cast<FriendDecl>(decl)) {
      return true; // metadata-only declaration
    }
    if (std::dynamic_pointer_cast<VarDecl>(decl) ||
        std::dynamic_pointer_cast<StructuredBindingDecl>(decl) ||
        std::dynamic_pointer_cast<ExprStatement>(decl)) {
      diags.ReportError(decl->loc, frontends::ErrorCode::kUnsupportedLowering,
                        "C++ top-level executable initialization is not implemented");
      return false;
    }
    diags.ReportError(decl->loc, frontends::ErrorCode::kUnsupportedLowering,
                      "unsupported C++ top-level declaration lowering");
    return false;
  };
  for (const auto &decl : module.declarations) {
    if (!lower_top_level(lower_top_level, decl))
      break;
  }
}

} // namespace polyglot::cpp

/** @} */
