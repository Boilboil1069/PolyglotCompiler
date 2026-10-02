#include "frontends/common/include/native_builtins.h"
/**
 * @file     lowering.cpp
 * @brief    Rust language frontend implementation
 *
 * @ingroup  Frontend / Rust
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "middle/include/ir/ir_builder.h"
#include "middle/include/ir/ir_printer.h"

#include "common/include/core/types.h"
#include "frontends/rust/include/rust_ast.h"
#include "frontends/rust/include/rust_lowering.h"

namespace polyglot::rust {
namespace {

using Name = std::string;

struct StructLayout {
  std::string name;
  ir::IRType type{ir::IRType::Invalid()};
  std::vector<std::string> field_names;
  std::unordered_map<std::string, size_t> field_indices;
};

using StructLayoutMap = std::unordered_map<std::string, StructLayout>;

// Convert Rust type to IR type - handles all common Rust types
ir::IRType ToIRType(const std::shared_ptr<TypeNode> &type,
                    const StructLayoutMap *struct_layouts = nullptr,
                    const std::string &self_type = {}) {
  if (!type)
    return ir::IRType::Void();

  if (auto path = std::dynamic_pointer_cast<TypePath>(type)) {
    std::string type_name = path->segments.empty() ? "" : path->segments.back();

    // Integer types
    if (type_name == "i8")
      return ir::IRType::I8(true);
    if (type_name == "i16")
      return ir::IRType::I16(true);
    if (type_name == "i32")
      return ir::IRType::I32(true);
    if (type_name == "i64")
      return ir::IRType::I64(true);
    if (type_name == "i128")
      return ir::IRType::Invalid();
    if (type_name == "isize")
      // Every architecture represented by the current IR DataLayout is
      // 64-bit.  Revisit this mapping when a 32-bit DataLayout is introduced.
      return ir::IRType::I64(true);

    // Unsigned integer types
    if (type_name == "u8")
      return ir::IRType::I8(false);
    if (type_name == "u16")
      return ir::IRType::I16(false);
    if (type_name == "u32")
      return ir::IRType::I32(false);
    if (type_name == "u64")
      return ir::IRType::I64(false);
    if (type_name == "u128")
      return ir::IRType::Invalid();
    if (type_name == "usize")
      return ir::IRType::I64(false);

    // Floating point types
    if (type_name == "f32")
      return ir::IRType::F32();
    if (type_name == "f64")
      return ir::IRType::F64();

    // Boolean type
    if (type_name == "bool")
      return ir::IRType::I1();

    // Character type (32-bit Unicode scalar value)
    if (type_name == "char")
      return ir::IRType::I32(false);

    // Unit type (void)
    if (type_name == "()" || path->segments.empty())
      return ir::IRType::Void();

    // `str` is a fat pointer and `String` is an owned three-word value.  A
    // thin byte pointer is not an ABI-compatible substitute.
    if (type_name == "str" || type_name == "String")
      return ir::IRType::Invalid();

    const std::string resolved_name = type_name == "Self" ? self_type : type_name;
    if (struct_layouts && !resolved_name.empty()) {
      auto layout = struct_layouts->find(resolved_name);
      if (layout != struct_layouts->end())
        return layout->second.type;
    }

    return ir::IRType::Invalid();
  }

  // Reference types become pointers
  if (auto ref = std::dynamic_pointer_cast<ReferenceType>(type)) {
    auto inner = ToIRType(ref->inner, struct_layouts, self_type);
    if (inner.kind == ir::IRTypeKind::kInvalid)
      return ir::IRType::Invalid();
    return ir::IRType::Pointer(inner);
  }

  // Slices are fat pointers and fixed arrays are by-value aggregates.  The
  // current IR boundary has no faithful representation for either ABI.
  if (std::dynamic_pointer_cast<SliceType>(type) ||
      std::dynamic_pointer_cast<ArrayType>(type))
    return ir::IRType::Invalid();

  // Tuple types - use struct representation
  if (auto tup = std::dynamic_pointer_cast<TupleType>(type)) {
    if (tup->elements.empty())
      return ir::IRType::Void();
    // `(T,)` remains a tuple in Rust; never collapse it to `T`.
    std::vector<ir::IRType> fields;
    for (const auto &elem : tup->elements) {
      auto field = ToIRType(elem, struct_layouts, self_type);
      if (field.kind == ir::IRTypeKind::kInvalid)
        return ir::IRType::Invalid();
      fields.push_back(field);
    }
    return ir::IRType::Struct("tuple", fields);
  }

  // Function types become function pointers
  if (auto fn = std::dynamic_pointer_cast<FunctionType>(type)) {
    auto ret = ToIRType(fn->return_type, struct_layouts, self_type);
    if (ret.kind == ir::IRTypeKind::kInvalid)
      return ir::IRType::Invalid();
    std::vector<ir::IRType> params;
    for (const auto &p : fn->params) {
      auto param = ToIRType(p, struct_layouts, self_type);
      if (param.kind == ir::IRTypeKind::kInvalid)
        return ir::IRType::Invalid();
      params.push_back(param);
    }
    return ir::IRType::Pointer(ir::IRType::Function(ret, params));
  }

  return ir::IRType::Invalid();
}

struct EnvEntry {
  Name value;
  ir::IRType type{ir::IRType::Invalid()};
};

struct MethodInfo {
  std::string owner;
  std::string name;
  std::string lowered_name;
  const FunctionItem *function{nullptr};
  ir::IRType return_type{ir::IRType::Invalid()};
};

struct LoweringContext {
  ir::IRContext &ir_ctx;
  frontends::Diagnostics &diags;
  std::unordered_map<Name, EnvEntry> env;
  std::unordered_map<Name, Name> local_addresses;
  std::unordered_map<Name, ir::IRType> function_returns;
  StructLayoutMap struct_layouts;
  std::unordered_map<Name, MethodInfo> methods;
  std::string current_impl;
  ir::IRBuilder builder;
  std::shared_ptr<ir::Function> fn;
  bool terminated{false};
  // Break/continue target blocks for loops
  ir::BasicBlock *loop_exit{nullptr};
  ir::BasicBlock *loop_continue{nullptr};

  LoweringContext(ir::IRContext &ctx, frontends::Diagnostics &d) :
      ir_ctx(ctx), diags(d), builder(ctx) {}
};

struct EvalResult {
  Name value;
  ir::IRType type{ir::IRType::Invalid()};
};

// Check if a string is a valid integer literal
bool IsIntegerLiteral(const std::string &text, long long *out) {
  if (text.empty())
    return false;

  std::string normalized;
  normalized.reserve(text.size());
  for (char c : text) {
    if (c != '_')
      normalized.push_back(c);
  }

  static const std::vector<std::string> suffixes = {
      "isize", "usize", "i128", "u128", "i64", "u64", "i32",
      "u32",   "i16",   "u16",  "i8",   "u8"};
  for (const auto &suffix : suffixes) {
    if (normalized.size() > suffix.size() &&
        normalized.compare(normalized.size() - suffix.size(), suffix.size(), suffix) == 0) {
      normalized.resize(normalized.size() - suffix.size());
      break;
    }
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

// Check if a string is a valid floating point literal
bool IsFloatLiteral(const std::string &text, double *out) {
  if (text.empty())
    return false;

  std::string normalized;
  normalized.reserve(text.size());
  for (char c : text) {
    if (c != '_')
      normalized.push_back(c);
  }
  for (const std::string &suffix : {std::string("f32"), std::string("f64")}) {
    if (normalized.size() > suffix.size() &&
        normalized.compare(normalized.size() - suffix.size(), suffix.size(), suffix) == 0) {
      normalized.resize(normalized.size() - suffix.size());
      break;
    }
  }

  char *end = nullptr;
  double v = std::strtod(normalized.c_str(), &end);
  if (end == normalized.c_str() || *end != '\0')
    return false;
  if (out)
    *out = v;
  return true;
}

// Forward declarations
EvalResult EvalExpr(const std::shared_ptr<Expression> &expr, LoweringContext &lc);
bool LowerStmt(const std::shared_ptr<Statement> &stmt, LoweringContext &lc);
bool LowerFunction(const FunctionItem &fn, LoweringContext &lc,
                   const std::string &impl_owner = {});

// Create an integer literal
EvalResult MakeLiteral(long long v, LoweringContext &lc) {
  (void)lc;
  return {std::to_string(v), ir::IRType::I64(true)};
}

// Create a floating point literal
EvalResult MakeFloatLiteral(double v, LoweringContext &lc) {
  auto literal = lc.builder.MakeLiteral(v);
  return {literal->name, ir::IRType::F64()};
}

// Create a boolean literal
EvalResult MakeBoolLiteral(bool v, LoweringContext &lc) {
  (void)lc;
  return {v ? "1" : "0", ir::IRType::I1()};
}

// Evaluate path expression (variable reference)
EvalResult EvalPath(const std::shared_ptr<PathExpression> &path, LoweringContext &lc) {
  std::string path_name = path->segments.empty() ? "" : path->segments.back();
  auto it = lc.env.find(path_name);
  if (it == lc.env.end()) {
    lc.diags.Report(path->loc, "undefined path: " + path_name);
    return {};
  }
  if (lc.local_addresses.count(path_name)) {
    auto load = lc.builder.MakeLoad(lc.local_addresses.at(path_name), it->second.type);
    return {load->name, load->type};
  }
  return {it->second.value, it->second.type};
}

// Map Rust binary operator to IR operator
std::optional<ir::BinaryInstruction::Op> MapBinOp(const std::string &op, bool is_signed = true,
                                                  bool is_float = false) {
  // Arithmetic operators
  if (op == "+")
    return is_float ? ir::BinaryInstruction::Op::kFAdd : ir::BinaryInstruction::Op::kAdd;
  if (op == "-")
    return is_float ? ir::BinaryInstruction::Op::kFSub : ir::BinaryInstruction::Op::kSub;
  if (op == "*")
    return is_float ? ir::BinaryInstruction::Op::kFMul : ir::BinaryInstruction::Op::kMul;
  if (op == "/") {
    if (is_float)
      return ir::BinaryInstruction::Op::kFDiv;
    return is_signed ? ir::BinaryInstruction::Op::kSDiv : ir::BinaryInstruction::Op::kUDiv;
  }
  if (op == "%") {
    if (is_float)
      return ir::BinaryInstruction::Op::kFRem;
    return is_signed ? ir::BinaryInstruction::Op::kSRem : ir::BinaryInstruction::Op::kURem;
  }

  // Comparison operators
  if (op == "==")
    return ir::BinaryInstruction::Op::kCmpEq;
  if (op == "!=")
    return ir::BinaryInstruction::Op::kCmpNe;
  if (op == "<")
    return is_float ? ir::BinaryInstruction::Op::kCmpFlt : is_signed ? ir::BinaryInstruction::Op::kCmpSlt : ir::BinaryInstruction::Op::kCmpUlt;
  if (op == "<=")
    return is_float ? ir::BinaryInstruction::Op::kCmpFle : is_signed ? ir::BinaryInstruction::Op::kCmpSle : ir::BinaryInstruction::Op::kCmpUle;
  if (op == ">")
    return is_float ? ir::BinaryInstruction::Op::kCmpFgt : is_signed ? ir::BinaryInstruction::Op::kCmpSgt : ir::BinaryInstruction::Op::kCmpUgt;
  if (op == ">=")
    return is_float ? ir::BinaryInstruction::Op::kCmpFge : is_signed ? ir::BinaryInstruction::Op::kCmpSge : ir::BinaryInstruction::Op::kCmpUge;

  // Bitwise operators
  if (op == "&")
    return ir::BinaryInstruction::Op::kAnd;
  if (op == "|")
    return ir::BinaryInstruction::Op::kOr;
  if (op == "^")
    return ir::BinaryInstruction::Op::kXor;
  if (op == "<<")
    return ir::BinaryInstruction::Op::kShl;
  if (op == ">>")
    return is_signed ? ir::BinaryInstruction::Op::kAShr : ir::BinaryInstruction::Op::kLShr;

  return std::nullopt;
}

// Check if operator is a comparison
bool IsComparisonOp(const std::string &op) {
  return op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=";
}

// Evaluate binary expression
EvalResult EvalBinary(const std::shared_ptr<BinaryExpression> &bin, LoweringContext &lc) {
  // Handle short-circuit logical operators
  if (bin->op == "&&") {
    auto lhs = EvalExpr(bin->left, lc);
    if (lhs.type.kind == ir::IRTypeKind::kInvalid)
      return {};

    auto *current_block = lc.builder.GetInsertPoint().get();
    auto *rhs_block = lc.fn->CreateBlock("and.rhs");
    auto *merge_block = lc.fn->CreateBlock("and.merge");

    lc.builder.MakeCondBranch(lhs.value, rhs_block, merge_block);

    // Evaluate RHS
    for (auto &bb : lc.fn->blocks) {
      if (bb.get() == rhs_block) {
        lc.builder.SetInsertPoint(bb);
        break;
      }
    }
    auto rhs = EvalExpr(bin->right, lc);
    auto *rhs_end_block = lc.builder.GetInsertPoint().get();
    lc.builder.MakeBranch(merge_block);

    // Merge block with PHI
    for (auto &bb : lc.fn->blocks) {
      if (bb.get() == merge_block) {
        lc.builder.SetInsertPoint(bb);
        break;
      }
    }
    std::vector<std::pair<ir::BasicBlock *, std::string>> incomings;
    incomings.push_back({current_block, "0"});       // false from short-circuit
    incomings.push_back({rhs_end_block, rhs.value}); // rhs.value from rhs_block
    auto phi = lc.builder.MakePhi(ir::IRType::I1(), incomings, "and.result");
    return {phi->name, ir::IRType::I1()};
  }

  if (bin->op == "||") {
    auto lhs = EvalExpr(bin->left, lc);
    if (lhs.type.kind == ir::IRTypeKind::kInvalid)
      return {};

    auto *current_block = lc.builder.GetInsertPoint().get();
    auto *rhs_block = lc.fn->CreateBlock("or.rhs");
    auto *merge_block = lc.fn->CreateBlock("or.merge");

    lc.builder.MakeCondBranch(lhs.value, merge_block, rhs_block);

    // Evaluate RHS
    for (auto &bb : lc.fn->blocks) {
      if (bb.get() == rhs_block) {
        lc.builder.SetInsertPoint(bb);
        break;
      }
    }
    auto rhs = EvalExpr(bin->right, lc);
    auto *rhs_end_block = lc.builder.GetInsertPoint().get();
    lc.builder.MakeBranch(merge_block);

    // Merge block with PHI
    for (auto &bb : lc.fn->blocks) {
      if (bb.get() == merge_block) {
        lc.builder.SetInsertPoint(bb);
        break;
      }
    }
    std::vector<std::pair<ir::BasicBlock *, std::string>> incomings;
    incomings.push_back({current_block, "1"});       // true from short-circuit
    incomings.push_back({rhs_end_block, rhs.value}); // rhs.value from rhs_block
    auto phi = lc.builder.MakePhi(ir::IRType::I1(), incomings, "or.result");
    return {phi->name, ir::IRType::I1()};
  }

  auto lhs = EvalExpr(bin->left, lc);
  auto rhs = EvalExpr(bin->right, lc);
  if (lhs.type.kind == ir::IRTypeKind::kInvalid || rhs.type.kind == ir::IRTypeKind::kInvalid)
    return {};
  if (lhs.type != rhs.type) {
    lc.diags.ReportError(bin->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Rust binary operands require an explicit modeled coercion");
    return {};
  }

  bool is_signed = lhs.type.is_signed;
  bool is_float = lhs.type.kind == ir::IRTypeKind::kF32 || lhs.type.kind == ir::IRTypeKind::kF64;
  auto op = MapBinOp(bin->op, is_signed, is_float);
  if (!op) {
    lc.diags.ReportError(bin->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "unsupported Rust binary operator lowering: " + bin->op);
    return {};
  }
  auto inst = lc.builder.MakeBinary(*op, lhs.value, rhs.value, "");

  // Set result type based on operation
  if (IsComparisonOp(bin->op)) {
    inst->type = ir::IRType::I1();
  } else {
    inst->type = lhs.type;
  }
  return {inst->name, inst->type};
}

// Evaluate unary expression
EvalResult EvalUnary(const std::shared_ptr<UnaryExpression> &un, LoweringContext &lc) {
  auto operand = EvalExpr(un->operand, lc);
  if (operand.type.kind == ir::IRTypeKind::kInvalid)
    return {};

  if (un->op == "-") {
    if (operand.type.IsFloat()) {
      auto negative = lc.builder.MakeFloatNegate(operand.value, operand.type);
      return {negative->name, operand.type};
    }
    auto inst = lc.builder.MakeBinary(ir::BinaryInstruction::Op::kSub, "0", operand.value, "");
    inst->type = operand.type;
    return {inst->name, inst->type};
  }

  if (un->op == "!") {
    if (!operand.type.IsInteger()) {
      lc.diags.ReportError(un->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "Rust '!' lowering requires a modeled bool/integer operand");
      return {};
    }
    const std::string mask = operand.type.kind == ir::IRTypeKind::kI1 ? "1" : "-1";
    auto inst = lc.builder.MakeBinary(ir::BinaryInstruction::Op::kXor, operand.value, mask, "");
    inst->type = operand.type;
    return {inst->name, inst->type};
  }

  if (un->op == "&" || un->op == "&mut") {
    lc.diags.ReportError(un->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Rust borrow lowering requires addressable storage and is not implemented");
    return {};
  }

  if (un->op == "*") {
    // Dereference: load from pointer
    if (operand.type.kind != ir::IRTypeKind::kPointer || operand.type.subtypes.empty()) {
      lc.diags.ReportError(un->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "Rust dereference lowering requires a modeled pointer value");
      return {};
    }
    ir::IRType inner_type = operand.type.subtypes[0];
    auto inst = lc.builder.MakeLoad(operand.value, inner_type, "");
    return {inst->name, inst->type};
  }

  lc.diags.ReportError(un->loc, frontends::ErrorCode::kUnsupportedLowering,
                       "unsupported Rust unary operator lowering: " + un->op);
  return {};
}

// Evaluate function call expression
EvalResult EvalCall(const std::shared_ptr<CallExpression> &call, LoweringContext &lc) {
  if (auto member = std::dynamic_pointer_cast<MemberExpression>(call->callee)) {
    auto receiver = EvalExpr(member->object, lc);
    if (receiver.type.kind != ir::IRTypeKind::kPointer || receiver.type.subtypes.empty() ||
        receiver.type.subtypes.front().kind != ir::IRTypeKind::kStruct) {
      lc.diags.ReportError(member->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "Rust instance method lowering requires an addressable struct receiver");
      return {};
    }

    const std::string owner = receiver.type.subtypes.front().name;
    auto method = lc.methods.find(owner + "." + member->member);
    if (method == lc.methods.end() || !method->second.function) {
      lc.diags.ReportError(member->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "Rust method '" + owner + "::" + member->member +
                               "' has no modeled receiver signature");
      return {};
    }

    const auto &target = *method->second.function;
    if (target.params.empty() || target.params.front().name != "self" ||
        target.params.size() != call->args.size() + 1) {
      lc.diags.ReportError(call->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "Rust method call argument count does not match '" + owner + "::" +
                               member->member + "'");
      return {};
    }

    auto receiver_type = ToIRType(target.params.front().type, &lc.struct_layouts, owner);
    if (receiver_type.kind != ir::IRTypeKind::kPointer ||
        receiver_type.subtypes.empty() || receiver_type.subtypes.front().name != owner) {
      lc.diags.ReportError(call->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "Rust method receiver must be &self or &mut self for aggregate lowering");
      return {};
    }

    std::vector<std::string> method_args{receiver.value};
    for (size_t i = 0; i < call->args.size(); ++i) {
      auto expected = ToIRType(target.params[i + 1].type, &lc.struct_layouts, owner);
      auto actual = EvalExpr(call->args[i], lc);
      if (expected.kind == ir::IRTypeKind::kInvalid || actual.type != expected) {
        lc.diags.ReportError(call->args[i]->loc, frontends::ErrorCode::kUnsupportedLowering,
                             "Rust method argument has no exact modeled ABI type");
        return {};
      }
      method_args.push_back(actual.value);
    }

    auto inst = lc.builder.MakeCall(method->second.lowered_name, method_args,
                                    method->second.return_type, "");
    return {inst->name, inst->type};
  }


  std::string callee_name;
  if (auto path = std::dynamic_pointer_cast<PathExpression>(call->callee)) {
    callee_name = path->segments.empty() ? "" : path->segments.back();
  } else if (auto id = std::dynamic_pointer_cast<Identifier>(call->callee)) {
    callee_name = id->name;
  } else {
    lc.diags.ReportError(call->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Rust indirect call lowering requires a modeled callable ABI");
    return {};
  }

  const auto *native_api = frontends::FindNativeBuiltin(callee_name);
  std::vector<std::string> args;
  std::vector<ir::IRType> arg_types;
  for (std::size_t index = 0; index < call->args.size(); ++index) {
    const auto &argument = call->args[index];
    if (native_api && index < native_api->params.size() &&
        native_api->params[index] == frontends::NativeType::kString) {
      if (auto literal = std::dynamic_pointer_cast<Literal>(argument);
          literal && !literal->value.empty() && literal->value.front() == '"') {
        std::string decoded;
        bool valid = literal->value.size() >= 2 && literal->value.back() == '"';
        for (std::size_t i = 1; valid && i + 1 < literal->value.size(); ++i) {
          char character = literal->value[i];
          if (character == '\\') {
            if (++i + 1 >= literal->value.size()) { valid = false; break; }
            switch (literal->value[i]) {
            case 'n': character = '\n'; break;
            case 'r': character = '\r'; break;
            case 't': character = '\t'; break;
            case '\\': character = '\\'; break;
            case '"': character = '"'; break;
            default: valid = false; break;
            }
          }
          if (character == '\0') valid = false;
          decoded += character;
        }
        if (!valid) {
          lc.diags.ReportError(argument->loc, frontends::ErrorCode::kUnsupportedLowering,
                               "Rust native text expects a NUL-free quoted literal with basic escapes");
          return {};
        }
        args.push_back(lc.builder.MakeStringLiteral(decoded, "native.text"));
        arg_types.push_back(ir::IRType::Pointer(ir::IRType::I8()));
        continue;
      }
    }
    auto value = EvalExpr(argument, lc);
    if (value.type.kind == ir::IRTypeKind::kInvalid) return {};
    args.push_back(value.value); arg_types.push_back(value.type);
  }

  if (const auto *api = frontends::FindNativeBuiltin(callee_name)) {
    auto inst = frontends::EmitNativeBuiltin(*api, args, arg_types, lc.builder, lc.ir_ctx, lc.diags, call->loc);
    if (!inst) return {};
    return {inst->name, inst->type};
  }
  auto known = lc.function_returns.find(callee_name);
  if (known == lc.function_returns.end()) {
    lc.diags.ReportError(call->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Rust call target '" + callee_name +
                             "' has no modeled function signature");
    return {};
  }
  auto inst = lc.builder.MakeCall(callee_name, args, known->second, "");
  return {inst->name, inst->type};
}

// Evaluate identifier expression
EvalResult EvalIdentifier(const std::shared_ptr<Identifier> &id, LoweringContext &lc) {
  auto it = lc.env.find(id->name);
  if (it == lc.env.end()) {
    lc.diags.Report(id->loc, "undefined identifier: " + id->name);
    return {};
  }
  if (lc.local_addresses.count(id->name)) {
    auto load = lc.builder.MakeLoad(lc.local_addresses.at(id->name), it->second.type);
    return {load->name, load->type};
  }
  return {it->second.value, it->second.type};
}

// Evaluate literal expression
EvalResult EvalLiteral(const std::shared_ptr<Literal> &lit, LoweringContext &lc) {
  // Boolean literals
  if (lit->value == "true")
    return MakeBoolLiteral(true, lc);
  if (lit->value == "false")
    return MakeBoolLiteral(false, lc);

  // Check for float literal (decimal point/exponent or an explicit suffix).
  // Do not mistake the `e` hex digit in values such as 0xdead for a decimal
  // exponent marker.
  const bool non_decimal_prefix =
      lit->value.size() > 2 && lit->value[0] == '0' &&
      (lit->value[1] == 'x' || lit->value[1] == 'X' || lit->value[1] == 'b' ||
       lit->value[1] == 'B' || lit->value[1] == 'o' || lit->value[1] == 'O');
  if (!non_decimal_prefix &&
      (lit->value.find('.') != std::string::npos || lit->value.find('e') != std::string::npos ||
       lit->value.find('E') != std::string::npos || lit->value.find("f32") != std::string::npos ||
       lit->value.find("f64") != std::string::npos)) {
    double v{};
    if (IsFloatLiteral(lit->value, &v)) {
      return MakeFloatLiteral(v, lc);
    }
  }

  // Integer literal
  long long v{};
  if (IsIntegerLiteral(lit->value, &v)) {
    return MakeLiteral(v, lc);
  }

  if (!lit->value.empty() && std::isdigit(static_cast<unsigned char>(lit->value.front()))) {
    lc.diags.ReportError(lit->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Rust numeric literal exceeds or does not match the modeled scalar ABI");
  } else if (!lit->value.empty() && lit->value.front() == '\'') {
    lc.diags.ReportError(lit->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Rust char literal escape/scalar lowering is not implemented");
  } else {
    lc.diags.ReportError(lit->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Rust string/byte-string literals require slice-aware runtime lowering");
  }
  return {};
}

struct MemberAddress {
  std::string value;
  ir::IRType field_type{ir::IRType::Invalid()};
};

MemberAddress EvalMemberAddress(const std::shared_ptr<MemberExpression> &mem,
                                LoweringContext &lc) {
  auto obj = EvalExpr(mem->object, lc);
  if (obj.type.kind == ir::IRTypeKind::kInvalid)
    return {};

  if (obj.type.kind != ir::IRTypeKind::kPointer || obj.type.subtypes.empty() ||
      obj.type.subtypes.front().kind != ir::IRTypeKind::kStruct) {
    lc.diags.ReportError(mem->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Rust field access requires an addressable struct value");
    return {};
  }

  const auto &struct_type = obj.type.subtypes.front();
  auto layout = lc.struct_layouts.find(struct_type.name);
  if (layout == lc.struct_layouts.end()) {
    lc.diags.ReportError(mem->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Rust field access has no aggregate layout for '" + struct_type.name + "'");
    return {};
  }
  auto field = layout->second.field_indices.find(mem->member);
  if (field == layout->second.field_indices.end()) {
    lc.diags.Report(mem->loc, "unknown Rust struct field: " + mem->member);
    return {};
  }

  const size_t index = field->second;
  auto gep = lc.builder.MakeGEP(obj.value, layout->second.type, {0, index}, "");
  return {gep->name, layout->second.type.subtypes[index]};
}

// Evaluate member expression (struct field access)
EvalResult EvalMember(const std::shared_ptr<MemberExpression> &mem, LoweringContext &lc) {
  auto address = EvalMemberAddress(mem, lc);
  if (address.field_type.kind == ir::IRTypeKind::kInvalid)
    return {};
  auto load = lc.builder.MakeLoad(address.value, address.field_type, "");
  return {load->name, load->type};
}

EvalResult EvalStruct(const std::shared_ptr<StructExpression> &value,
                      LoweringContext &lc) {
  if (value->has_rest) {
    lc.diags.ReportError(value->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Rust struct update syntax requires move/copy field semantics");
    return {};
  }
  if (value->path.segments.empty()) {
    lc.diags.Report(value->loc, "Rust struct expression requires a named type");
    return {};
  }
  const std::string parsed_name = value->path.segments.back();
  const std::string struct_name = parsed_name == "Self" ? lc.current_impl : parsed_name;
  auto layout = lc.struct_layouts.find(struct_name);
  if (layout == lc.struct_layouts.end()) {
    lc.diags.ReportError(value->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Rust struct expression has no modeled layout for '" + struct_name + "'");
    return {};
  }

  auto storage = lc.builder.MakeAlloca(layout->second.type, struct_name + ".value");
  std::unordered_set<std::string> initialized;
  for (const auto &initializer : value->fields) {
    auto field = layout->second.field_indices.find(initializer.name);
    if (field == layout->second.field_indices.end()) {
      lc.diags.Report(value->loc, "unknown field '" + initializer.name + "' in " + struct_name);
      return {};
    }
    if (!initialized.insert(initializer.name).second) {
      lc.diags.Report(value->loc, "field '" + initializer.name + "' initialized more than once");
      return {};
    }
    const size_t index = field->second;
    auto field_value = EvalExpr(initializer.value, lc);
    if (field_value.type != layout->second.type.subtypes[index]) {
      lc.diags.ReportError(initializer.value->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "Rust struct field initializer requires an exact modeled ABI type");
      return {};
    }
    auto address = lc.builder.MakeGEP(storage->name, layout->second.type, {0, index}, "");
    lc.builder.MakeStore(address->name, field_value.value);
  }
  if (initialized.size() != layout->second.field_names.size()) {
    lc.diags.Report(value->loc, "Rust struct expression must initialize every field");
    return {};
  }
  return {storage->name, ir::IRType::Pointer(layout->second.type)};
}

// Evaluate index expression (array/slice indexing)
EvalResult EvalIndex(const std::shared_ptr<IndexExpression> &idx, LoweringContext &lc) {
  auto obj = EvalExpr(idx->object, lc);
  auto index = EvalExpr(idx->index, lc);
  if (obj.type.kind == ir::IRTypeKind::kInvalid || index.type.kind == ir::IRTypeKind::kInvalid)
    return {};

  ir::IRType elem_type = ir::IRType::Invalid();
  if (obj.type.kind == ir::IRTypeKind::kPointer && !obj.type.subtypes.empty()) {
    elem_type = obj.type.subtypes[0];
  } else if (obj.type.kind == ir::IRTypeKind::kArray && !obj.type.subtypes.empty()) {
    elem_type = obj.type.subtypes[0];
  }
  if (elem_type.kind == ir::IRTypeKind::kInvalid) {
    lc.diags.ReportError(idx->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Rust indexing requires a modeled array/slice element layout");
    return {};
  }

  // Generate dynamic array access using arithmetic
  auto gep = lc.builder.MakeDynamicGEP(obj.value, elem_type, index.value, "");
  auto load = lc.builder.MakeLoad(gep->name, elem_type, "");
  return {load->name, load->type};
}

// Evaluate assignment expression
EvalResult EvalAssignment(const std::shared_ptr<AssignmentExpression> &assign,
                          LoweringContext &lc) {
  auto rhs = EvalExpr(assign->right, lc);
  if (rhs.type.kind == ir::IRTypeKind::kInvalid)
    return {};

  // Handle compound assignments (+=, -=, etc.)
  if (assign->op != "=") {
    auto lhs = EvalExpr(assign->left, lc);
    if (lhs.type.kind == ir::IRTypeKind::kInvalid)
      return {};

    std::string base_op = assign->op.substr(0, assign->op.size() - 1);
    auto op = MapBinOp(base_op, lhs.type.is_signed);
    if (!op) {
      lc.diags.ReportError(assign->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "unsupported Rust compound assignment lowering: " + assign->op);
      return {};
    }
    auto inst = lc.builder.MakeBinary(*op, lhs.value, rhs.value, "");
    inst->type = lhs.type;
    rhs = {inst->name, inst->type};
  }

  // Store the result
  if (auto id = std::dynamic_pointer_cast<Identifier>(assign->left)) {
    if (lc.local_addresses.count(id->name)) lc.builder.MakeStore(lc.local_addresses.at(id->name), rhs.value);
    else lc.env[id->name] = {rhs.value, rhs.type};
    return rhs;
  }
  if (auto path = std::dynamic_pointer_cast<PathExpression>(assign->left)) {
    std::string name = path->segments.empty() ? "" : path->segments.back();
    if (lc.local_addresses.count(name)) lc.builder.MakeStore(lc.local_addresses.at(name), rhs.value);
    else lc.env[name] = {rhs.value, rhs.type};
    return rhs;
  }
  if (auto member = std::dynamic_pointer_cast<MemberExpression>(assign->left)) {
    auto address = EvalMemberAddress(member, lc);
    if (address.field_type.kind == ir::IRTypeKind::kInvalid || address.field_type != rhs.type)
      return {};
    lc.builder.MakeStore(address.value, rhs.value);
    return rhs;
  }

  lc.diags.ReportError(assign->loc, frontends::ErrorCode::kUnsupportedLowering,
                       "Rust assignment target requires place-expression lowering");
  return {};
}

// Evaluate if expression (returns value)
EvalResult EvalIfExpr(const std::shared_ptr<IfExpression> &if_expr, LoweringContext &lc) {
  lc.diags.ReportError(if_expr->loc, frontends::ErrorCode::kUnsupportedLowering,
                       "value-producing Rust if expressions require branch-result PHI lowering");
  return {};

  auto cond = EvalExpr(if_expr->condition, lc);
  if (cond.type.kind == ir::IRTypeKind::kInvalid)
    return {};

  auto *then_block = lc.fn->CreateBlock("if.then");
  auto *else_block = if_expr->else_body.empty() ? nullptr : lc.fn->CreateBlock("if.else");
  auto *merge_block = lc.fn->CreateBlock("if.end");

  lc.builder.MakeCondBranch(cond.value, then_block, else_block ? else_block : merge_block);

  // Then block
  for (auto &bb : lc.fn->blocks) {
    if (bb.get() == then_block) {
      lc.builder.SetInsertPoint(bb);
      break;
    }
  }
  lc.terminated = false;
  EvalResult then_result{};
  for (auto &s : if_expr->then_body) {
    if (!LowerStmt(s, lc))
      return {};
    if (lc.terminated)
      break;
  }
  if (!lc.terminated) {
    lc.builder.MakeBranch(merge_block);
  }

  // Else block
  EvalResult else_result{};
  if (else_block) {
    for (auto &bb : lc.fn->blocks) {
      if (bb.get() == else_block) {
        lc.builder.SetInsertPoint(bb);
        break;
      }
    }
    lc.terminated = false;
    for (auto &s : if_expr->else_body) {
      if (!LowerStmt(s, lc))
        return {};
      if (lc.terminated)
        break;
    }
    if (!lc.terminated) {
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
  lc.terminated = false;

  // If used as expression, would need PHI node here
  return {merge_block->name, ir::IRType::Void()};
}

// Evaluate block expression
EvalResult EvalBlock(const std::shared_ptr<BlockExpression> &blk, LoweringContext &lc) {
  EvalResult last{};
  for (size_t i = 0; i < blk->statements.size(); ++i) {
    auto tail = std::dynamic_pointer_cast<ExprStatement>(blk->statements[i]);
    if (i + 1 == blk->statements.size() && tail && !tail->has_semicolon) {
      last = EvalExpr(tail->expr, lc);
    } else if (!LowerStmt(blk->statements[i], lc)) {
      return {};
    }
    if (lc.terminated)
      break;
  }
  // Block expression evaluates to the last expression (if any)
  return last;
}

// Evaluate match expression
EvalResult EvalMatch(const std::shared_ptr<MatchExpression> &match, LoweringContext &lc) {
  lc.diags.ReportError(match->loc, frontends::ErrorCode::kUnsupportedLowering,
                       "Rust match expressions require pattern bindings and branch-result PHI lowering");
  return {};
}

// Evaluate range expression
EvalResult EvalRange(const std::shared_ptr<RangeExpression> &range, LoweringContext &lc) {
  lc.diags.ReportError(range->loc, frontends::ErrorCode::kUnsupportedLowering,
                       "Rust range values require an aggregate representation; only direct "
                       "range for-loops are currently lowered");
  return {};
}

// Evaluate closure expression
EvalResult EvalClosure(const std::shared_ptr<ClosureExpression> &cls, LoweringContext &lc) {
  if (cls->is_async) {
    lc.diags.ReportError(
        cls->loc, frontends::ErrorCode::kUnsupportedLowering,
        "Rust async closure lowering requires generator state-machine support");
    return {};
  }
  lc.diags.ReportError(cls->loc, frontends::ErrorCode::kUnsupportedLowering,
                       "Rust closure environment and capture lowering is not implemented");
  return {};
}

// Main expression evaluation dispatcher
EvalResult EvalExpr(const std::shared_ptr<Expression> &expr, LoweringContext &lc) {
  if (!expr)
    return {};

  // Literal
  if (auto lit = std::dynamic_pointer_cast<Literal>(expr)) {
    return EvalLiteral(lit, lc);
  }

  // Identifier
  if (auto id = std::dynamic_pointer_cast<Identifier>(expr)) {
    return EvalIdentifier(id, lc);
  }

  // Path expression (variable or type path)
  if (auto path = std::dynamic_pointer_cast<PathExpression>(expr)) {
    return EvalPath(path, lc);
  }

  // Named-field struct construction.  The resulting value is represented by
  // its address so receiver calls and field accesses retain Rust lvalue
  // semantics without inventing an aggregate register ABI.
  if (auto value = std::dynamic_pointer_cast<StructExpression>(expr)) {
    return EvalStruct(value, lc);
  }

  // Binary expression
  if (auto bin = std::dynamic_pointer_cast<BinaryExpression>(expr)) {
    return EvalBinary(bin, lc);
  }

  // Unary expression
  if (auto un = std::dynamic_pointer_cast<UnaryExpression>(expr)) {
    return EvalUnary(un, lc);
  }

  // Call expression
  if (auto call = std::dynamic_pointer_cast<CallExpression>(expr)) {
    return EvalCall(call, lc);
  }

  // Member expression (field access)
  if (auto mem = std::dynamic_pointer_cast<MemberExpression>(expr)) {
    return EvalMember(mem, lc);
  }

  // Index expression (array access)
  if (auto idx = std::dynamic_pointer_cast<IndexExpression>(expr)) {
    return EvalIndex(idx, lc);
  }

  // Assignment expression
  if (auto assign = std::dynamic_pointer_cast<AssignmentExpression>(expr)) {
    return EvalAssignment(assign, lc);
  }

  // If expression
  if (auto if_expr = std::dynamic_pointer_cast<IfExpression>(expr)) {
    return EvalIfExpr(if_expr, lc);
  }

  // Block expression
  if (auto blk = std::dynamic_pointer_cast<BlockExpression>(expr)) {
    return EvalBlock(blk, lc);
  }

  // Match expression
  if (auto match = std::dynamic_pointer_cast<MatchExpression>(expr)) {
    return EvalMatch(match, lc);
  }

  // Range expression
  if (auto range = std::dynamic_pointer_cast<RangeExpression>(expr)) {
    return EvalRange(range, lc);
  }

  // Closure expression
  if (auto cls = std::dynamic_pointer_cast<ClosureExpression>(expr)) {
    return EvalClosure(cls, lc);
  }

  // While expression (as statement-like expression)
  if (auto wh = std::dynamic_pointer_cast<WhileExpression>(expr)) {
    // Lower as loop
    auto *cond_block = lc.fn->CreateBlock("while.cond");
    auto *body_block = lc.fn->CreateBlock("while.body");
    auto *exit_block = lc.fn->CreateBlock("while.exit");

    lc.builder.MakeBranch(cond_block);

    // Condition block
    for (auto &bb : lc.fn->blocks) {
      if (bb.get() == cond_block) {
        lc.builder.SetInsertPoint(bb);
        break;
      }
    }
    auto cond = EvalExpr(wh->condition, lc);
    lc.builder.MakeCondBranch(cond.value, body_block, exit_block);

    // Body block
    for (auto &bb : lc.fn->blocks) {
      if (bb.get() == body_block) {
        lc.builder.SetInsertPoint(bb);
        break;
      }
    }

    // Save and set loop targets
    auto *old_exit = lc.loop_exit;
    auto *old_continue = lc.loop_continue;
    lc.loop_exit = exit_block;
    lc.loop_continue = cond_block;

    lc.terminated = false;
    for (auto &s : wh->body) {
      if (!LowerStmt(s, lc))
        return {};
      if (lc.terminated)
        break;
    }
    if (!lc.terminated) {
      lc.builder.MakeBranch(cond_block);
    }

    lc.loop_exit = old_exit;
    lc.loop_continue = old_continue;

    // Exit block
    for (auto &bb : lc.fn->blocks) {
      if (bb.get() == exit_block) {
        lc.builder.SetInsertPoint(bb);
        break;
      }
    }
    lc.terminated = false;
    return {exit_block->name, ir::IRType::Void()};
  }

  // Macro call — expand well-known macros into IR.
  // For user-defined macros we emit a call to a runtime helper that
  // evaluates the macro body; for std macros like println!/format! we
  // expand them inline.
  if (auto macro = std::dynamic_pointer_cast<MacroCallExpression>(expr)) {
    // Construct the full macro path (e.g. "std::println", "vec")
    std::string macro_name;
    for (const auto &seg : macro->path.segments) {
      if (!macro_name.empty())
        macro_name += "::";
      macro_name += seg;
    }

    if (macro_name == "println" || macro_name == "std::println") {
      // println!("...") → call __rs_println with the string body
      std::string body_str = macro->body;
      std::string sym = lc.builder.MakeStringLiteral(body_str, "fmt");
      auto inst = lc.builder.MakeCall("__rs_println", {sym}, ir::IRType::Void(), "");
      return {inst->name, ir::IRType::Void()};
    }
    if (macro_name == "eprintln" || macro_name == "std::eprintln") {
      std::string body_str = macro->body;
      std::string sym = lc.builder.MakeStringLiteral(body_str, "fmt");
      auto inst = lc.builder.MakeCall("__rs_eprintln", {sym}, ir::IRType::Void(), "");
      return {inst->name, ir::IRType::Void()};
    }
    if (macro_name == "format" || macro_name == "std::format") {
      std::string body_str = macro->body;
      std::string sym = lc.builder.MakeStringLiteral(body_str, "fmt");
      auto inst =
          lc.builder.MakeCall("__rs_format", {sym}, ir::IRType::Pointer(ir::IRType::I8()), "");
      return {inst->name, ir::IRType::Pointer(ir::IRType::I8())};
    }
    if (macro_name == "vec" || macro_name == "std::vec") {
      // vec![...] → allocate a runtime vector from the body tokens
      std::string body_str = macro->body;
      std::string sym = lc.builder.MakeStringLiteral(body_str, "vec_init");
      auto inst = lc.builder.MakeCall("__rs_vec_from_literal", {sym},
                                      ir::IRType::Pointer(ir::IRType::I8()), "");
      return {inst->name, ir::IRType::Pointer(ir::IRType::I8())};
    }
    if (macro_name == "panic" || macro_name == "std::panic") {
      std::string body_str = macro->body;
      std::string sym = lc.builder.MakeStringLiteral(body_str, "panic_msg");
      lc.builder.MakeCall("__rs_panic", {sym}, ir::IRType::Void(), "");
      lc.builder.MakeUnreachable();
      lc.terminated = true;
      return {"", ir::IRType::Void()};
    }

    lc.diags.ReportError(macro->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "user-defined Rust macro expansion is not implemented");
    return {};
  }

  // Try expression
  if (auto try_expr = std::dynamic_pointer_cast<TryExpression>(expr)) {
    lc.diags.ReportError(try_expr->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Rust '?' propagation lowering is not implemented");
    return {};
  }

  // Await expression
  if (auto await = std::dynamic_pointer_cast<AwaitExpression>(expr)) {
    lc.diags.ReportError(await->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Rust .await lowering requires an async runtime and is not implemented");
    return {};
  }

  if (auto async = std::dynamic_pointer_cast<AsyncBlock>(expr)) {
    lc.diags.ReportError(async->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Rust async block lowering requires an async runtime and is not implemented");
    return {};
  }

  lc.diags.ReportError(expr->loc, frontends::ErrorCode::kUnsupportedLowering,
                       "unsupported Rust expression lowering");
  return {};
}

// Lower return statement
bool LowerReturn(const std::shared_ptr<ReturnStatement> &ret, LoweringContext &lc) {
  if (lc.terminated)
    return true;
  EvalResult v;
  if (ret->value) {
    v = EvalExpr(ret->value, lc);
    if (v.type.kind == ir::IRTypeKind::kInvalid)
      return false;
  }
  lc.builder.MakeReturn(v.value);
  lc.terminated = true;
  return true;
}

// Lower let binding statement
bool LowerLet(const std::shared_ptr<LetStatement> &let, LoweringContext &lc) {
  if (!let->init) {
    if (let->has_else || !std::dynamic_pointer_cast<IdentifierPattern>(let->pattern)) {
      lc.diags.ReportError(let->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "uninitialized Rust lowering supports only a simple deferred binding");
      return false;
    }
    // Do not invent Rust's nonexistent default value.  A subsequent assignment
    // creates the SSA binding; a read before then is rejected by EvalPath.
    return true;
  }

  EvalResult result;
  result = EvalExpr(let->init, lc);
  if (result.type.kind == ir::IRTypeKind::kInvalid)
    return false;

  if (let->has_else) {
    // Literal let-else patterns can be represented exactly with the current
    // scalar IR.  Enum/struct/slice patterns need a concrete Rust layout ABI;
    // rejecting them is safer than treating every pattern as a match.
    auto literal = std::dynamic_pointer_cast<LiteralPattern>(let->pattern);
    if (!literal) {
      lc.diags.ReportError(
          let->loc, frontends::ErrorCode::kUnsupportedLowering,
          "let-else lowering currently requires a scalar literal pattern; "
          "enum/struct/slice pattern layout is not implemented");
      return false;
    }

    auto literal_expr = std::make_shared<Literal>();
    literal_expr->loc = literal->loc;
    literal_expr->value = literal->value;
    auto expected = EvalExpr(literal_expr, lc);
    if (expected.type.kind == ir::IRTypeKind::kInvalid)
      return false;
    auto matches = lc.builder.MakeBinary(ir::BinaryInstruction::Op::kCmpEq, result.value,
                                         expected.value, "let_else_match");
    matches->type = ir::IRType::I1();

    auto *matched_block = lc.fn->CreateBlock("let_else.match");
    auto *else_block = lc.fn->CreateBlock("let_else.else");
    lc.builder.MakeCondBranch(matches->name, matched_block, else_block);

    for (auto &bb : lc.fn->blocks) {
      if (bb.get() == else_block) {
        lc.builder.SetInsertPoint(bb);
        break;
      }
    }
    lc.terminated = false;
    for (auto &stmt : let->else_body) {
      if (!LowerStmt(stmt, lc))
        return false;
      if (lc.terminated)
        break;
    }
    if (!lc.terminated) {
      lc.diags.ReportError(
          let->loc, frontends::ErrorCode::kUnsupportedLowering,
          "Rust let-else lowering requires a provably diverging else block");
      return false;
    }

    for (auto &bb : lc.fn->blocks) {
      if (bb.get() == matched_block) {
        lc.builder.SetInsertPoint(bb);
        break;
      }
    }
    lc.terminated = false;
    return true;
  }

  // Handle different pattern types
  if (auto pat = std::dynamic_pointer_cast<IdentifierPattern>(let->pattern)) {
    if (result.type.IsScalar()) {
      auto storage = lc.builder.MakeAlloca(result.type);
      lc.builder.MakeStore(storage->name, result.value);
      lc.local_addresses[pat->name] = storage->name;
    }
    lc.env[pat->name] = {result.value, result.type};
    return true;
  }

  if (auto tup = std::dynamic_pointer_cast<TuplePattern>(let->pattern)) {
    // Destructuring tuple pattern: let (a, b, c) = expr;
    // Extract each element from the tuple value using GEP.
    if (result.type.kind == ir::IRTypeKind::kStruct &&
        result.type.subtypes.size() == tup->elements.size()) {
      // The result is a struct type representing the tuple.
      for (size_t i = 0; i < tup->elements.size() && i < result.type.subtypes.size(); ++i) {
        auto elem_pat = std::dynamic_pointer_cast<IdentifierPattern>(tup->elements[i]);
        if (!elem_pat && !std::dynamic_pointer_cast<WildcardPattern>(tup->elements[i])) {
          lc.diags.ReportError(let->loc, frontends::ErrorCode::kUnsupportedLowering,
                               "nested Rust tuple-pattern lowering is not implemented");
          return false;
        }
        if (!elem_pat)
          continue;
        auto gep = lc.builder.MakeGEP(result.value, result.type, {i});
        auto load = lc.builder.MakeLoad(gep->name, result.type.subtypes[i]);
        lc.env[elem_pat->name] = {load->name, result.type.subtypes[i]};
      }
    } else {
      lc.diags.ReportError(let->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "Rust tuple destructuring requires an exact modeled tuple layout");
      return false;
    }
    return true;
  }

  if (auto wild = std::dynamic_pointer_cast<WildcardPattern>(let->pattern)) {
    // Wildcard pattern - just evaluate the expression for side effects
    return true;
  }

  lc.diags.ReportError(let->loc, frontends::ErrorCode::kUnsupportedLowering,
                       "unsupported Rust let-pattern lowering");
  return false;
}

// Lower if statement/expression
bool LowerIf(const std::shared_ptr<IfExpression> &if_expr, LoweringContext &lc) {
  if (lc.terminated)
    return true;

  auto cond = EvalExpr(if_expr->condition, lc);
  if (cond.type.kind == ir::IRTypeKind::kInvalid)
    return false;

  auto *then_block = lc.fn->CreateBlock("if.then");
  auto *else_block = if_expr->else_body.empty() ? nullptr : lc.fn->CreateBlock("if.else");
  auto *merge_block = lc.fn->CreateBlock("if.end");

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
  for (auto &s : if_expr->then_body) {
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
    for (auto &bb : lc.fn->blocks) {
      if (bb.get() == else_block) {
        lc.builder.SetInsertPoint(bb);
        break;
      }
    }
    lc.terminated = false;
    for (auto &s : if_expr->else_body) {
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

  for (auto &bb : lc.fn->blocks) {
    if (bb.get() == merge_block) {
      lc.builder.SetInsertPoint(bb);
      break;
    }
  }
  // Without an explicit else, the false edge reaches the merge block and
  // lowering must continue.  When both explicit branches return, keep the
  // otherwise-unreachable merge block structurally valid for IR verification.
  const bool all_paths_terminate = else_block && then_term && else_term;
  if (all_paths_terminate)
    lc.builder.MakeUnreachable();
  lc.terminated = all_paths_terminate;
  return true;
}

// Lower loop statement (infinite loop)
bool LowerLoop(const std::shared_ptr<LoopStatement> &loop, LoweringContext &lc) {
  if (lc.terminated)
    return true;

  auto *body_block = lc.fn->CreateBlock("loop.body");
  auto *exit_block = lc.fn->CreateBlock("loop.exit");

  // Save and set loop targets for break/continue
  auto *old_exit = lc.loop_exit;
  auto *old_continue = lc.loop_continue;
  lc.loop_exit = exit_block;
  lc.loop_continue = body_block;

  lc.builder.MakeBranch(body_block);
  for (auto &bb : lc.fn->blocks) {
    if (bb.get() == body_block) {
      lc.builder.SetInsertPoint(bb);
      break;
    }
  }
  lc.terminated = false;

  for (auto &s : loop->body) {
    if (!LowerStmt(s, lc))
      return false;
    if (lc.terminated)
      break;
  }

  if (!lc.terminated) {
    lc.builder.MakeBranch(body_block);
  }

  lc.loop_exit = old_exit;
  lc.loop_continue = old_continue;

  for (auto &bb : lc.fn->blocks) {
    if (bb.get() == exit_block) {
      lc.builder.SetInsertPoint(bb);
      break;
    }
  }
  lc.terminated = false;
  return true;
}

// Lower while expression as statement
bool LowerWhile(const std::shared_ptr<WhileExpression> &wh, LoweringContext &lc) {
  if (lc.terminated)
    return true;

  auto *cond_block = lc.fn->CreateBlock("while.cond");
  auto *body_block = lc.fn->CreateBlock("while.body");
  auto *exit_block = lc.fn->CreateBlock("while.exit");

  // Save and set loop targets
  auto *old_exit = lc.loop_exit;
  auto *old_continue = lc.loop_continue;
  lc.loop_exit = exit_block;
  lc.loop_continue = cond_block;

  lc.builder.MakeBranch(cond_block);

  // Condition block
  for (auto &bb : lc.fn->blocks) {
    if (bb.get() == cond_block) {
      lc.builder.SetInsertPoint(bb);
      break;
    }
  }
  auto cond = EvalExpr(wh->condition, lc);
  lc.builder.MakeCondBranch(cond.value, body_block, exit_block);

  // Body block
  for (auto &bb : lc.fn->blocks) {
    if (bb.get() == body_block) {
      lc.builder.SetInsertPoint(bb);
      break;
    }
  }
  lc.terminated = false;

  for (auto &s : wh->body) {
    if (!LowerStmt(s, lc))
      return false;
    if (lc.terminated)
      break;
  }

  if (!lc.terminated) {
    lc.builder.MakeBranch(cond_block);
  }

  lc.loop_exit = old_exit;
  lc.loop_continue = old_continue;

  // Exit block
  for (auto &bb : lc.fn->blocks) {
    if (bb.get() == exit_block) {
      lc.builder.SetInsertPoint(bb);
      break;
    }
  }
  lc.terminated = false;
  return true;
}

// Lower for statement
bool LowerFor(const std::shared_ptr<ForStatement> &fr, LoweringContext &lc) {
  if (lc.terminated)
    return true;

  auto range = std::dynamic_pointer_cast<RangeExpression>(fr->iterable);
  if (!range) {
    lc.diags.ReportError(fr->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Rust for-loop lowering currently requires a range expression; "
                         "IntoIterator lowering is not implemented");
    return false;
  }
  if (!range->start || range->kind == RangeExpression::RangeKind::kTo ||
      range->kind == RangeExpression::RangeKind::kFull) {
    lc.diags.ReportError(fr->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "this Rust range cannot be used as a lowered iterator");
    return false;
  }

  auto start = EvalExpr(range->start, lc);
  if (start.type.kind == ir::IRTypeKind::kInvalid)
    return false;
  EvalResult end;
  if (range->end) {
    end = EvalExpr(range->end, lc);
    if (end.type.kind == ir::IRTypeKind::kInvalid)
      return false;
  }

  auto pattern = std::dynamic_pointer_cast<IdentifierPattern>(fr->pattern);
  if (!pattern) {
    lc.diags.ReportError(fr->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "range for-loop lowering requires an identifier pattern");
    return false;
  }

  auto preheader = lc.builder.GetInsertPoint();
  if (!preheader) {
    lc.diags.ReportError(fr->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "range for-loop has no insertion block");
    return false;
  }

  auto *cond_block = lc.fn->CreateBlock("for.cond");
  auto *body_block = lc.fn->CreateBlock("for.body");
  auto *incr_block = lc.fn->CreateBlock("for.incr");
  auto *exit_block = lc.fn->CreateBlock("for.exit");

  // Save and set loop targets
  auto *old_exit = lc.loop_exit;
  auto *old_continue = lc.loop_continue;
  lc.loop_exit = exit_block;
  lc.loop_continue = incr_block;

  const std::string loop_var = pattern->name;
  std::optional<EnvEntry> old_binding;
  if (auto found = lc.env.find(loop_var); found != lc.env.end())
    old_binding = found->second;

  lc.builder.MakeBranch(cond_block);

  // Condition block (for range, check if counter < end)
  for (auto &bb : lc.fn->blocks) {
    if (bb.get() == cond_block) {
      lc.builder.SetInsertPoint(bb);
      break;
    }
  }
  auto counter = lc.builder.MakePhi(start.type, {{preheader.get(), start.value}}, "range.index");
  if (loop_var != "_")
    lc.env[loop_var] = {counter->name, counter->type};

  if (range->end) {
    auto cmp_op = start.type.is_signed
                      ? (range->inclusive ? ir::BinaryInstruction::Op::kCmpSle
                                          : ir::BinaryInstruction::Op::kCmpSlt)
                      : (range->inclusive ? ir::BinaryInstruction::Op::kCmpUle
                                          : ir::BinaryInstruction::Op::kCmpUlt);
    auto condition = lc.builder.MakeBinary(cmp_op, counter->name, end.value, "range.condition");
    condition->type = ir::IRType::I1();
    lc.builder.MakeCondBranch(condition->name, body_block, exit_block);
  } else {
    // `start..` is intentionally unbounded but still increments on each pass.
    lc.builder.MakeCondBranch("1", body_block, exit_block);
  }

  // Body block
  for (auto &bb : lc.fn->blocks) {
    if (bb.get() == body_block) {
      lc.builder.SetInsertPoint(bb);
      break;
    }
  }
  lc.terminated = false;

  for (auto &s : fr->body) {
    if (!LowerStmt(s, lc))
      return false;
    if (lc.terminated)
      break;
  }

  if (!lc.terminated) {
    lc.builder.MakeBranch(incr_block);
  }

  // Increment block
  for (auto &bb : lc.fn->blocks) {
    if (bb.get() == incr_block) {
      lc.builder.SetInsertPoint(bb);
      break;
    }
  }
  auto inc = lc.builder.MakeBinary(ir::BinaryInstruction::Op::kAdd, counter->name, "1",
                                   "range.next");
  inc->type = counter->type;
  lc.builder.MakeBranch(cond_block);
  lc.builder.AddPhiIncoming(counter.get(), incr_block, inc->name);

  lc.loop_exit = old_exit;
  lc.loop_continue = old_continue;

  if (old_binding) {
    lc.env[loop_var] = *old_binding;
  } else {
    lc.env.erase(loop_var);
  }

  // Exit block
  for (auto &bb : lc.fn->blocks) {
    if (bb.get() == exit_block) {
      lc.builder.SetInsertPoint(bb);
      break;
    }
  }
  lc.terminated = false;
  return true;
}

// Lower break statement
bool LowerBreak(const std::shared_ptr<BreakStatement> &brk, LoweringContext &lc) {
  if (lc.terminated)
    return true;

  if (!lc.loop_exit) {
    lc.diags.Report(brk->loc, "break outside of loop");
    return false;
  }

  // Evaluate break value if present (for loop expressions)
  if (brk->value) {
    lc.diags.ReportError(brk->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Rust break-with-value requires loop-result PHI lowering");
    return false;
  }

  lc.builder.MakeBranch(lc.loop_exit);
  lc.terminated = true;
  return true;
}

// Lower continue statement
bool LowerContinue(const std::shared_ptr<ContinueStatement> &cont, LoweringContext &lc) {
  if (lc.terminated)
    return true;

  if (!lc.loop_continue) {
    lc.diags.Report(cont->loc, "continue outside of loop");
    return false;
  }

  lc.builder.MakeBranch(lc.loop_continue);
  lc.terminated = true;
  return true;
}

// Lower block expression as statement
bool LowerBlock(const std::shared_ptr<BlockExpression> &blk, LoweringContext &lc) {
  for (auto &s : blk->statements) {
    if (!LowerStmt(s, lc))
      return false;
    if (lc.terminated)
      break;
  }
  return true;
}

// Main statement lowering dispatcher
bool LowerStmt(const std::shared_ptr<Statement> &stmt, LoweringContext &lc) {
  if (!stmt || lc.terminated)
    return true;

  // Return statement
  if (auto ret = std::dynamic_pointer_cast<ReturnStatement>(stmt)) {
    return LowerReturn(ret, lc);
  }

  // Let statement
  if (auto let = std::dynamic_pointer_cast<LetStatement>(stmt)) {
    return LowerLet(let, lc);
  }

  // Loop statement
  if (auto loop = std::dynamic_pointer_cast<LoopStatement>(stmt)) {
    return LowerLoop(loop, lc);
  }

  // For statement
  if (auto fr = std::dynamic_pointer_cast<ForStatement>(stmt)) {
    return LowerFor(fr, lc);
  }

  // Break statement
  if (auto brk = std::dynamic_pointer_cast<BreakStatement>(stmt)) {
    return LowerBreak(brk, lc);
  }

  // Continue statement
  if (auto cont = std::dynamic_pointer_cast<ContinueStatement>(stmt)) {
    return LowerContinue(cont, lc);
  }

  // Expression statement
  if (auto expr_stmt = std::dynamic_pointer_cast<ExprStatement>(stmt)) {
    // Check for specific expression types that need special handling
    if (auto if_expr = std::dynamic_pointer_cast<IfExpression>(expr_stmt->expr)) {
      return LowerIf(if_expr, lc);
    }
    if (auto wh = std::dynamic_pointer_cast<WhileExpression>(expr_stmt->expr)) {
      return LowerWhile(wh, lc);
    }
    if (auto blk = std::dynamic_pointer_cast<BlockExpression>(expr_stmt->expr)) {
      return LowerBlock(blk, lc);
    }
    // General expression - evaluate for side effects
    return EvalExpr(expr_stmt->expr, lc).type.kind != ir::IRTypeKind::kInvalid;
  }

  // If expression as statement
  if (auto if_expr = std::dynamic_pointer_cast<IfExpression>(stmt)) {
    return LowerIf(if_expr, lc);
  }

  // While expression as statement
  if (auto wh = std::dynamic_pointer_cast<WhileExpression>(stmt)) {
    return LowerWhile(wh, lc);
  }

  // Block expression as statement
  if (auto blk = std::dynamic_pointer_cast<BlockExpression>(stmt)) {
    return LowerBlock(blk, lc);
  }

  // Match expression as statement
  if (auto match = std::dynamic_pointer_cast<MatchExpression>(stmt)) {
    EvalMatch(match, lc);
    return true;
  }

  // Function item declaration — lower as a standalone function
  if (auto fn_item = std::dynamic_pointer_cast<FunctionItem>(stmt)) {
    return LowerFunction(*fn_item, lc);
  }

  // Impl block — lower each method inside it
  if (auto impl = std::dynamic_pointer_cast<ImplItem>(stmt)) {
    auto target = std::dynamic_pointer_cast<TypePath>(impl->target_type);
    const std::string owner =
        target && !target->segments.empty() ? target->segments.back() : std::string{};
    if (owner.empty()) {
      lc.diags.ReportError(impl->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "Rust impl lowering requires a concrete named struct target");
      return false;
    }
    for (const auto &m : impl->items) {
      if (auto fn_item = std::dynamic_pointer_cast<FunctionItem>(m)) {
        if (!LowerFunction(*fn_item, lc, owner))
          return false;
      }
    }
    return true;
  }

  // Const item — evaluate the initialiser and bind the result
  if (auto cst = std::dynamic_pointer_cast<ConstItem>(stmt)) {
    if (cst->value) {
      auto val = EvalExpr(cst->value, lc);
      lc.env[cst->name] = {val.value, val.type};
    }
    return true;
  }

  // Type alias, trait, mod, struct, enum, macro_rules — metadata only,
  // no IR is generated directly for these declarations.
  if (std::dynamic_pointer_cast<TypeAliasItem>(stmt) ||
      std::dynamic_pointer_cast<UseDeclaration>(stmt) ||
      std::dynamic_pointer_cast<TraitItem>(stmt) || std::dynamic_pointer_cast<ModItem>(stmt) ||
      std::dynamic_pointer_cast<StructItem>(stmt) || std::dynamic_pointer_cast<EnumItem>(stmt) ||
      std::dynamic_pointer_cast<MacroRulesItem>(stmt)) {
    return true;
  }

  lc.diags.ReportError(stmt->loc, frontends::ErrorCode::kUnsupportedLowering,
                       "unsupported Rust statement lowering");
  return false;
}

// Lower a complete function
bool LowerFunction(const FunctionItem &fn, LoweringContext &lc,
                   const std::string &impl_owner) {
  lc.current_impl = impl_owner;
  if (!fn.has_body)
    return true; // declaration only; no executable semantics to emit
  if (fn.is_async) {
    lc.diags.ReportError(fn.loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Rust async function lowering requires an async runtime and is not implemented");
    return false;
  }
  ir::IRType ret_ty = ToIRType(fn.return_type, &lc.struct_layouts, impl_owner);
  if (ret_ty.kind == ir::IRTypeKind::kInvalid) {
    lc.diags.ReportError(fn.return_type ? fn.return_type->loc : fn.loc,
                         frontends::ErrorCode::kUnsupportedLowering,
                         "unsupported Rust return type lowering (including i128/u128)");
    return false;
  }
  if (ret_ty.kind == ir::IRTypeKind::kStruct) {
    lc.diags.ReportError(fn.loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Rust aggregate return ABI is not implemented; construct the struct "
                         "in the caller and invoke receiver methods instead");
    return false;
  }

  // Process parameters with full type support
  std::vector<std::pair<std::string, ir::IRType>> params;
  params.reserve(fn.params.size());
  for (auto &param : fn.params) {
    if (!param.type) {
      lc.diags.ReportError(fn.loc, frontends::ErrorCode::kUnsupportedLowering,
                           "Rust function parameters require a modeled explicit type");
      return false;
    }
    ir::IRType param_ty = ToIRType(param.type, &lc.struct_layouts, impl_owner);
    if (param_ty.kind == ir::IRTypeKind::kInvalid) {
      lc.diags.ReportError(param.type->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "unsupported Rust parameter type lowering for '" + param.name +
                               "' (including i128/u128)");
      return false;
    }
    if (param_ty.kind == ir::IRTypeKind::kStruct) {
      lc.diags.ReportError(param.type->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "Rust by-value aggregate parameter ABI is not implemented");
      return false;
    }
    params.push_back({param.name, param_ty});
  }

  const std::string lowered_name =
      impl_owner.empty() ? fn.name : impl_owner + "." + fn.name;
  lc.fn = lc.ir_ctx.CreateFunction(lowered_name, ret_ty, params);
  auto *entry = lc.fn->CreateBlock("entry");
  lc.fn->entry = entry;
  for (auto &bb : lc.fn->blocks) {
    if (bb.get() == entry) {
      lc.builder.SetInsertPoint(bb);
      break;
    }
  }

  // Initialize environment with parameters
  lc.env.clear();
  lc.local_addresses.clear();
  lc.loop_exit = nullptr;
  lc.loop_continue = nullptr;
  for (const auto &p : params) {
    lc.env[p.first] = {p.first, p.second};
  }
  lc.terminated = false;

  // Lower function body
  for (size_t i = 0; i < fn.body.size(); ++i) {
    auto tail = std::dynamic_pointer_cast<ExprStatement>(fn.body[i]);
    if (i + 1 == fn.body.size() && tail && !tail->has_semicolon) {
      auto value = EvalExpr(tail->expr, lc);
      if (value.type.kind == ir::IRTypeKind::kInvalid)
        return false;
      lc.builder.MakeReturn(ret_ty.kind == ir::IRTypeKind::kVoid ? "" : value.value);
      lc.terminated = true;
    } else if (!LowerStmt(fn.body[i], lc)) {
      return false;
    }
    if (lc.terminated)
      break;
  }

  // Add implicit return if needed
  if (!lc.terminated) {
    if (ret_ty.kind == ir::IRTypeKind::kVoid) {
      lc.builder.MakeReturn("");
    } else {
      lc.diags.ReportError(fn.loc, frontends::ErrorCode::kUnsupportedLowering,
                           "non-void Rust function may reach the end without returning a value");
      return false;
    }
  }
  return true;
}

} // namespace

void LowerToIR(const Module &module, ir::IRContext &ctx, frontends::Diagnostics &diags) {
  LoweringContext lc(ctx, diags);

  // Build exact named-field layouts before signatures or bodies are lowered.
  // This keeps field offsets deterministic and lets every impl resolve `Self`.
  for (const auto &item : module.items) {
    auto item_struct = std::dynamic_pointer_cast<StructItem>(item);
    if (!item_struct)
      continue;
    if (item_struct->is_tuple || item_struct->is_unit) {
      // They remain parser/sema features, but this vertical slice intentionally
      // models only named fields so construction cannot silently use the wrong
      // tuple/unit ABI.
      continue;
    }

    StructLayout layout;
    layout.name = item_struct->name;
    std::vector<ir::IRType> fields;
    for (const auto &field : item_struct->fields) {
      auto field_type = ToIRType(field.type, &lc.struct_layouts);
      if (field_type.kind == ir::IRTypeKind::kInvalid ||
          field_type.kind == ir::IRTypeKind::kStruct ||
          field_type.kind == ir::IRTypeKind::kArray) {
        diags.ReportError(field.type ? field.type->loc : item_struct->loc,
                          frontends::ErrorCode::kUnsupportedLowering,
                          "Rust struct field '" + field.name +
                              "' requires a modeled scalar field ABI");
        continue;
      }
      layout.field_indices[field.name] = fields.size();
      layout.field_names.push_back(field.name);
      fields.push_back(field_type);
    }
    layout.type = ir::IRType::Struct(layout.name, fields);
    lc.struct_layouts[layout.name] = std::move(layout);
  }

  for (const auto &item : module.items) {
    if (auto fn = std::dynamic_pointer_cast<FunctionItem>(item)) {
      auto ret = ToIRType(fn->return_type, &lc.struct_layouts);
      if (ret.kind != ir::IRTypeKind::kInvalid)
        lc.function_returns[fn->name] = ret;
    }
    if (auto impl = std::dynamic_pointer_cast<ImplItem>(item)) {
      auto target = std::dynamic_pointer_cast<TypePath>(impl->target_type);
      const std::string owner =
          target && !target->segments.empty() ? target->segments.back() : std::string{};
      if (owner.empty() || lc.struct_layouts.find(owner) == lc.struct_layouts.end())
        continue;
      for (const auto &member : impl->items) {
        auto fn = std::dynamic_pointer_cast<FunctionItem>(member);
        if (!fn)
          continue;
        auto ret = ToIRType(fn->return_type, &lc.struct_layouts, owner);
        if (ret.kind == ir::IRTypeKind::kInvalid)
          continue;
        MethodInfo info;
        info.owner = owner;
        info.name = fn->name;
        info.lowered_name = owner + "." + fn->name;
        info.function = fn.get();
        info.return_type = ret;
        lc.methods[owner + "." + fn->name] = info;
        lc.function_returns[info.lowered_name] = ret;
      }
    }
  }
  for (const auto &item : module.items) {
    if (auto fn = std::dynamic_pointer_cast<FunctionItem>(item)) {
      if (!LowerFunction(*fn, lc))
        break;
      continue;
    }
    if (auto impl = std::dynamic_pointer_cast<ImplItem>(item)) {
      if (impl->trait_type) {
        diags.ReportError(impl->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "Rust trait impl lowering and virtual obligations are not implemented");
        break;
      }
      auto target = std::dynamic_pointer_cast<TypePath>(impl->target_type);
      const std::string owner =
          target && !target->segments.empty() ? target->segments.back() : std::string{};
      if (owner.empty() || lc.struct_layouts.find(owner) == lc.struct_layouts.end()) {
        diags.ReportError(impl->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "Rust impl lowering requires a modeled named-field struct target");
        break;
      }
      for (const auto &member : impl->items) {
        if (auto fn = std::dynamic_pointer_cast<FunctionItem>(member)) {
          if (!LowerFunction(*fn, lc, owner))
            break;
        } else {
          diags.ReportError(member->loc, frontends::ErrorCode::kUnsupportedLowering,
                            "Rust associated const/type lowering is not implemented");
          break;
        }
      }
      if (diags.HasErrors())
        break;
      continue;
    }
    if (auto module_item = std::dynamic_pointer_cast<ModItem>(item)) {
      if (!module_item->items.empty()) {
        diags.ReportError(module_item->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "inline Rust module item lowering and symbol qualification are not implemented");
        break;
      }
      continue;
    }
    if (auto trait = std::dynamic_pointer_cast<TraitItem>(item)) {
      const bool has_default_body = std::any_of(
          trait->items.begin(), trait->items.end(), [](const auto &member) {
            auto fn = std::dynamic_pointer_cast<FunctionItem>(member);
            return fn && fn->has_body;
          });
      if (has_default_body) {
        diags.ReportError(trait->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "Rust trait default-method lowering is not implemented");
        break;
      }
      continue;
    }
    if (std::dynamic_pointer_cast<ConstItem>(item)) {
      diags.ReportError(item->loc, frontends::ErrorCode::kUnsupportedLowering,
                        "Rust top-level const evaluation and storage lowering are not implemented");
      break;
    }
    if (std::dynamic_pointer_cast<UseDeclaration>(item) ||
        std::dynamic_pointer_cast<TypeAliasItem>(item) ||
        std::dynamic_pointer_cast<StructItem>(item) ||
        std::dynamic_pointer_cast<EnumItem>(item) ||
        std::dynamic_pointer_cast<MacroRulesItem>(item)) {
      continue; // metadata-only item
    }
    diags.ReportError(item->loc, frontends::ErrorCode::kUnsupportedLowering,
                      "unsupported Rust top-level item lowering");
    break;
  }
}

} // namespace polyglot::rust
