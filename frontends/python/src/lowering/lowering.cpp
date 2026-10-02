#include "frontends/common/include/native_builtins.h"
#include "frontends/common/include/native_string_literal.h"
/**
 * @file     lowering.cpp
 * @brief    Python language frontend implementation
 *
 * @ingroup  Frontend / Python
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include <cctype>
#include <cstdlib>
#include <optional>
#include <set>
#include <stack>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "middle/include/ir/ir_builder.h"
#include "middle/include/ir/ir_printer.h"

#include "common/include/core/types.h"
#include "frontends/python/include/python_ast.h"
#include "frontends/python/include/python_lowering.h"

namespace polyglot::python {
namespace {

using Name = std::string;

/** @name - */
/** @{ */
// Type mapping from Python type hints to IR types.
/** @} */

/** @name - */
/** @{ */
ir::IRType ToIRType(const std::string &type_hint) {
  if (type_hint == "int")
    return ir::IRType::I64(true);
  if (type_hint == "float")
    return ir::IRType::F64();
  if (type_hint == "bool")
    return ir::IRType::I1();
  if (type_hint == "None")
    return ir::IRType::Void();
  if (type_hint == "str")
    return ir::IRType::Pointer(ir::IRType::I8());
  return ir::IRType::Invalid();
}

/** @} */

/** @name - */
/** @{ */
// Environment entry for variable tracking.
/** @} */

/** @name - */
/** @{ */
struct EnvEntry {
  Name value;
  ir::IRType type{ir::IRType::Invalid()};
  Name alloca_name; // For mutable variables
  bool is_mutable{false};
};

/** @} */

/** @name - */
/** @{ */
// Loop context for break/continue handling.
/** @} */

/** @name - */
/** @{ */
struct LoopContext {
  ir::BasicBlock *continue_target{nullptr};
  ir::BasicBlock *break_target{nullptr};
};

/** @} */

/** @name - */
/** @{ */
// Class info for method lowering.
/** @} */

/** @name - */
/** @{ */
struct ClassInfo {
  struct MethodInfo {
    std::string lowered_name;
    ir::IRType return_type{ir::IRType::Invalid()};
    // The receiver is represented explicitly in IR, but is not part of the
    // Python source-level argument list kept here.
    std::vector<ir::IRType> parameter_types;
  };

  std::string name;
  std::vector<std::string> methods;
  std::vector<std::string> fields;
  std::unordered_map<std::string, size_t> field_indices;
  std::unordered_map<std::string, MethodInfo> method_info;
  ir::IRType struct_type{ir::IRType::Invalid()};
};

/** @} */

/** @name - */
/** @{ */
// Main lowering context.
/** @} */

/** @name - */
/** @{ */
struct LoweringContext {
  ir::IRContext &ir_ctx;
  frontends::Diagnostics &diags;
  std::unordered_map<Name, EnvEntry> env;
  ir::IRBuilder builder;
  std::shared_ptr<ir::Function> fn;
  bool terminated{false};

  // Loop stack for break/continue
  std::stack<LoopContext> loop_stack;

  // Class info for current class being lowered
  std::unordered_map<std::string, ClassInfo> classes;
  std::string current_class;

  // Async context tracking
  bool in_async_function{false};

  // Temp counter for unique names
  size_t temp_counter{0};

  // Tracks recursive lowering of nested function definitions.  Lowering a
  // nested function temporarily replaces fn/env/the builder insertion point;
  // the enclosing function state must be restored afterwards.
  size_t function_depth{0};
  std::unordered_map<std::string, ir::IRType> function_returns;

  LoweringContext(ir::IRContext &ctx, frontends::Diagnostics &d) :
      ir_ctx(ctx), diags(d), builder(ctx) {}

  std::string NextTemp(const std::string &prefix = "tmp") {
    return prefix + "." + std::to_string(temp_counter++);
  }

  void SetInsertBlock(ir::BasicBlock *bb) {
    for (auto &block : fn->blocks) {
      if (block.get() == bb) {
        builder.SetInsertPoint(block);
        return;
      }
    }
  }
};

/** @} */

/** @name - */
/** @{ */
// Evaluation result.
/** @} */

/** @name - */
/** @{ */
struct EvalResult {
  Name value;
  ir::IRType type{ir::IRType::Invalid()};

  bool IsValid() const { return type.kind != ir::IRTypeKind::kInvalid; }
  static EvalResult Invalid() { return {}; }
};

// Forward declarations
EvalResult EvalExpr(const std::shared_ptr<Expression> &expr, LoweringContext &lc);
bool LowerStmt(const std::shared_ptr<Statement> &stmt, LoweringContext &lc);
bool LowerFunction(const FunctionDef &fn, LoweringContext &lc);
bool LowerClass(const ClassDef &cls, LoweringContext &lc);

std::string ClassNameOf(const ir::IRType &type) {
  if (type.kind == ir::IRTypeKind::kStruct)
    return type.name;
  if ((type.kind == ir::IRTypeKind::kPointer ||
       type.kind == ir::IRTypeKind::kReference) &&
      !type.subtypes.empty() && type.subtypes.front().kind == ir::IRTypeKind::kStruct) {
    return type.subtypes.front().name;
  }
  return {};
}

ir::IRType DeclaredParameterType(const Parameter &parameter) {
  if (!parameter.annotation)
    return ir::IRType::I64(true);
  if (auto id = std::dynamic_pointer_cast<Identifier>(parameter.annotation))
    return ToIRType(id->name);
  return ir::IRType::Invalid();
}

ir::IRType DeclaredReturnType(const FunctionDef &fn) {
  // __init__ and __del__ are lifecycle hooks in the supported static subset.
  // They do not produce a Python object result and therefore use a void ABI.
  if (fn.name == "__init__" || fn.name == "__del__")
    return ir::IRType::Void();
  if (!fn.return_annotation)
    return ir::IRType::I64(true);
  if (auto id = std::dynamic_pointer_cast<Identifier>(fn.return_annotation))
    return ToIRType(id->name);
  if (auto literal = std::dynamic_pointer_cast<Literal>(fn.return_annotation);
      literal && literal->value == "None")
    return ir::IRType::Void();
  return ir::IRType::Invalid();
}

bool IsSelfAttribute(const std::shared_ptr<Expression> &expr, std::string *field = nullptr) {
  auto attribute = std::dynamic_pointer_cast<AttributeExpression>(expr);
  if (!attribute)
    return false;
  auto receiver = std::dynamic_pointer_cast<Identifier>(attribute->object);
  if (!receiver || receiver->name != "self")
    return false;
  if (field)
    *field = attribute->attribute;
  return true;
}

/** @} */

/** @name - */
/** @{ */
// Literal parsing helpers.
/** @} */

/** @name - */
/** @{ */
bool IsIntegerLiteral(const std::string &text, long long *out) {
  if (text.empty())
    return false;
  // Handle Python keywords as literals
  if (text == "True") {
    if (out)
      *out = 1;
    return true;
  }
  if (text == "False") {
    if (out)
      *out = 0;
    return true;
  }
  if (text == "None") {
    if (out)
      *out = 0;
    return true;
  }

  char *end = nullptr;
  long long v = std::strtoll(text.c_str(), &end, 0);
  if (end == text.c_str() || *end != '\0')
    return false;
  if (out)
    *out = v;
  return true;
}

bool IsFloatLiteral(const std::string &text, double *out) {
  if (text.empty())
    return false;
  char *end = nullptr;
  double v = std::strtod(text.c_str(), &end);
  if (end == text.c_str() || *end != '\0')
    return false;
  if (out)
    *out = v;
  return true;
}

/** @} */

/** @name - */
/** @{ */
// Make literals.
/** @} */

/** @name - */
/** @{ */
EvalResult MakeLiteral(long long v, LoweringContext &lc) {
  (void)lc;
  // Integer constants are represented textually in IR operands.  Giving a
  // detached LiteralExpression an SSA-like name (for example, `lit.0`) made
  // the verifier and native instruction selector treat it as an undefined
  // virtual register.
  return {std::to_string(v), ir::IRType::I64(true)};
}

EvalResult MakeFloatLiteral(double v, LoweringContext &lc) {
  auto lit = lc.builder.MakeLiteral(v, lc.NextTemp("flit"));
  return {lit->name, ir::IRType::F64()};
}

/** @} */

/** @name - */
/** @{ */
// Name evaluation.
/** @} */

/** @name - */
/** @{ */
EvalResult EvalName(const std::shared_ptr<Identifier> &name, LoweringContext &lc) {
  auto it = lc.env.find(name->name);
  if (it == lc.env.end()) {
    lc.diags.ReportError(name->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Python global/builtin name '" + name->name +
                             "' requires a modeled runtime value representation");
    return EvalResult::Invalid();
  }
  // Load from alloca if mutable
  if (it->second.is_mutable && !it->second.alloca_name.empty()) {
    auto load = lc.builder.MakeLoad(it->second.alloca_name, it->second.type, lc.NextTemp("load"));
    return {load->name, it->second.type};
  }
  return {it->second.value, it->second.type};
}

/** @} */

/** @name - */
/** @{ */
// Binary operator mapping.
/** @} */

/** @name - */
/** @{ */
std::optional<ir::BinaryInstruction::Op> MapBinOp(const std::string &op,
                                                  bool is_float = false) {
  if (op == "+")
    return is_float ? ir::BinaryInstruction::Op::kFAdd : ir::BinaryInstruction::Op::kAdd;
  if (op == "-")
    return is_float ? ir::BinaryInstruction::Op::kFSub : ir::BinaryInstruction::Op::kSub;
  if (op == "*")
    return is_float ? ir::BinaryInstruction::Op::kFMul : ir::BinaryInstruction::Op::kMul;
  if (op == "/")
    return is_float ? ir::BinaryInstruction::Op::kFDiv : ir::BinaryInstruction::Op::kSDiv;
  if (op == "//")
    return ir::BinaryInstruction::Op::kSDiv; // Floor division
  if (op == "%")
    return is_float ? ir::BinaryInstruction::Op::kFRem : ir::BinaryInstruction::Op::kSRem;
  if (op == "&")
    return ir::BinaryInstruction::Op::kAnd;
  if (op == "|")
    return ir::BinaryInstruction::Op::kOr;
  if (op == "^")
    return ir::BinaryInstruction::Op::kXor;
  if (op == "<<")
    return ir::BinaryInstruction::Op::kShl;
  if (op == ">>")
    return ir::BinaryInstruction::Op::kAShr;
  if (op == "==")
    return is_float ? ir::BinaryInstruction::Op::kCmpFoe : ir::BinaryInstruction::Op::kCmpEq;
  if (op == "!=")
    return ir::BinaryInstruction::Op::kCmpNe;
  if (op == "<")
    return is_float ? ir::BinaryInstruction::Op::kCmpFlt : ir::BinaryInstruction::Op::kCmpSlt;
  if (op == "<=")
    return is_float ? ir::BinaryInstruction::Op::kCmpFle : ir::BinaryInstruction::Op::kCmpSle;
  if (op == ">")
    return is_float ? ir::BinaryInstruction::Op::kCmpFgt : ir::BinaryInstruction::Op::kCmpSgt;
  if (op == ">=")
    return is_float ? ir::BinaryInstruction::Op::kCmpFge : ir::BinaryInstruction::Op::kCmpSge;
  return std::nullopt;
}

bool IsCmpOp(ir::BinaryInstruction::Op op) {
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
    return true;
  default:
    return false;
  }
}

/** @} */

/** @name - */
/** @{ */
// Binary expression evaluation.
/** @} */

/** @name - */
/** @{ */
EvalResult EvalBinOp(const std::shared_ptr<BinaryExpression> &bin, LoweringContext &lc) {
  // Handle logical and/or with short-circuit evaluation and proper PHI nodes
  if (bin->op == "and" || bin->op == "or") {
    auto lhs = EvalExpr(bin->left, lc);
    if (!lhs.IsValid())
      return EvalResult::Invalid();
    if (lhs.type.kind != ir::IRTypeKind::kI1) {
      lc.diags.ReportError(bin->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "Python and/or on non-bool values requires object-truthiness lowering");
      return EvalResult::Invalid();
    }

    auto *rhs_block = lc.fn->CreateBlock(bin->op == "and" ? "and.rhs" : "or.rhs");
    auto *merge_block = lc.fn->CreateBlock(bin->op == "and" ? "and.end" : "or.end");

    // Get current block for PHI incoming
    auto *lhs_block = lc.builder.GetInsertPoint().get();

    if (bin->op == "and") {
      // and: if lhs is false, result is lhs; otherwise evaluate rhs
      lc.builder.MakeCondBranch(lhs.value, rhs_block, merge_block);
    } else {
      // or: if lhs is true, result is lhs; otherwise evaluate rhs
      lc.builder.MakeCondBranch(lhs.value, merge_block, rhs_block);
    }

    lc.SetInsertBlock(rhs_block);
    auto rhs = EvalExpr(bin->right, lc);
    if (!rhs.IsValid())
      return EvalResult::Invalid();
    if (rhs.type.kind != ir::IRTypeKind::kI1) {
      lc.diags.ReportError(bin->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "Python and/or branch values require a common modeled bool ABI");
      return EvalResult::Invalid();
    }

    auto *rhs_end_block = lc.builder.GetInsertPoint().get();
    lc.builder.MakeBranch(merge_block);

    lc.SetInsertBlock(merge_block);

    // Create PHI node to select between lhs and rhs results
    // For 'and': if lhs was false, use lhs (false); else use rhs
    // For 'or': if lhs was true, use lhs (true); else use rhs
    ir::IRType result_type = lhs.type;
    if (result_type.kind == ir::IRTypeKind::kInvalid) {
      result_type = ir::IRType::I64(true);
    }

    std::vector<std::pair<ir::BasicBlock *, std::string>> phi_incomings = {
        {lhs_block, lhs.value}, {rhs_end_block, rhs.value}};

    auto phi = lc.builder.MakePhi(result_type, phi_incomings, lc.NextTemp("logic"));
    return {phi->name, result_type};
  }

  static const std::unordered_set<std::string> supported_ops = {
      "+",  "-",   "*",      "/",  "//", "%",  "**", "&", "|", "^", "<<", ">>",
      "==", "!=",  "<",      "<=", ">",  ">=", "is", "is not", "in", "not in"};
  if (supported_ops.count(bin->op) == 0) {
    lc.diags.ReportError(bin->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Python binary operator '" + bin->op +
                             "' has no faithful static IR lowering");
    return EvalResult::Invalid();
  }

  auto lhs = EvalExpr(bin->left, lc);
  auto rhs = EvalExpr(bin->right, lc);
  if (!lhs.IsValid() || !rhs.IsValid())
    return EvalResult::Invalid();
  if (!lhs.type.SameShape(rhs.type)) {
    lc.diags.ReportError(bin->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Python mixed-type binary coercion requires dynamic numeric semantics");
    return EvalResult::Invalid();
  }

  // These operators do not have LLVM-like primitive semantics in Python
  // (notably negative modulo/floor division and arbitrary-precision pow), so
  // preserve them through explicit runtime calls rather than approximating.
  static const std::unordered_map<std::string, std::string> runtime_ops = {
      {"/", "__py_true_div"}, {"//", "__py_floor_div"}, {"%", "__py_mod"},
      {"**", "__py_pow"},    {"is", "__py_is"},          {"is not", "__py_is_not"},
      {"in", "__py_contains"}, {"not in", "__py_not_contains"}};
  if (auto it = runtime_ops.find(bin->op); it != runtime_ops.end()) {
    ir::IRType result_type =
        (bin->op == "is" || bin->op == "is not" || bin->op == "in" || bin->op == "not in")
            ? ir::IRType::I1()
            : (bin->op == "/" ? ir::IRType::F64() : lhs.type);
    auto call = lc.builder.MakeCall(it->second, {lhs.value, rhs.value}, result_type,
                                    lc.NextTemp("bin.runtime"));
    return {call->name, result_type};
  }

  bool is_float = (lhs.type.kind == ir::IRTypeKind::kF64 || rhs.type.kind == ir::IRTypeKind::kF64);
  auto op = MapBinOp(bin->op, is_float);
  if (!op) {
    lc.diags.ReportError(bin->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Python binary operator '" + bin->op +
                             "' has no primitive IR lowering");
    return EvalResult::Invalid();
  }
  auto inst = lc.builder.MakeBinary(*op, lhs.value, rhs.value, lc.NextTemp("bin"));

  if (IsCmpOp(*op)) {
    inst->type = ir::IRType::I1();
  } else {
    inst->type = is_float ? ir::IRType::F64() : lhs.type;
  }
  return {inst->name, inst->type};
}

/** @} */

/** @name - */
/** @{ */
// Unary expression evaluation.
/** @} */

/** @name - */
/** @{ */
EvalResult EvalUnaryOp(const std::shared_ptr<UnaryExpression> &un, LoweringContext &lc) {
  auto operand = EvalExpr(un->operand, lc);
  if (!operand.IsValid())
    return EvalResult::Invalid();

  if (un->op == "not") {
    if (operand.type.kind != ir::IRTypeKind::kI1) {
      lc.diags.ReportError(un->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "Python not on non-bool values requires object-truthiness lowering");
      return EvalResult::Invalid();
    }
    // Boolean not: compare with 0
    auto zero = MakeLiteral(0, lc);
    auto inst = lc.builder.MakeBinary(ir::BinaryInstruction::Op::kCmpEq, operand.value, zero.value,
                                      lc.NextTemp("not"));
    inst->type = ir::IRType::I1();
    return {inst->name, ir::IRType::I1()};
  }
  if (un->op == "-") {
    if ((!operand.type.IsInteger() && !operand.type.IsFloat()) ||
        operand.type.kind == ir::IRTypeKind::kI1) {
      lc.diags.ReportError(un->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "Python unary minus requires numeric __neg__ runtime semantics");
      return EvalResult::Invalid();
    }
    if (operand.type.IsFloat()) {
      auto negative = lc.builder.MakeFloatNegate(operand.value, operand.type, lc.NextTemp("neg"));
      return {negative->name, operand.type};
    }
    auto zero = MakeLiteral(0, lc);
    auto op = ir::BinaryInstruction::Op::kSub;
    auto inst = lc.builder.MakeBinary(op, zero.value, operand.value, lc.NextTemp("neg"));
    inst->type = operand.type;
    return {inst->name, operand.type};
  }
  if (un->op == "+") {
    if ((!operand.type.IsInteger() && !operand.type.IsFloat()) ||
        operand.type.kind == ir::IRTypeKind::kI1) {
      lc.diags.ReportError(un->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "Python unary plus requires numeric __pos__ runtime semantics");
      return EvalResult::Invalid();
    }
    return operand;
  }
  if (un->op == "~") {
    if (!operand.type.IsInteger() || operand.type.kind == ir::IRTypeKind::kI1) {
      lc.diags.ReportError(un->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "Python bitwise not requires integer __invert__ runtime semantics");
      return EvalResult::Invalid();
    }
    // Bitwise not: XOR with -1
    auto neg_one = MakeLiteral(-1, lc);
    auto inst = lc.builder.MakeBinary(ir::BinaryInstruction::Op::kXor, operand.value, neg_one.value,
                                      lc.NextTemp("bnot"));
    inst->type = operand.type;
    return {inst->name, operand.type};
  }

  lc.diags.ReportError(un->loc, frontends::ErrorCode::kUnsupportedLowering,
                       "Python unary operator '" + un->op +
                           "' has no faithful static IR lowering");
  return EvalResult::Invalid();
}

/** @} */

/** @name - */
/** @{ */
// Call expression evaluation.
/** @} */

/** @name - */
/** @{ */
struct AttributeAddress {
  std::string address;
  ir::IRType field_type{ir::IRType::Invalid()};
  std::string class_name;

  bool IsValid() const { return field_type.kind != ir::IRTypeKind::kInvalid; }
};

AttributeAddress EvalStaticAttributeAddress(
    const std::shared_ptr<AttributeExpression> &attribute, const EvalResult &object,
    LoweringContext &lc) {
  const std::string class_name = ClassNameOf(object.type);
  if (class_name.empty())
    return {};

  auto class_it = lc.classes.find(class_name);
  if (class_it == lc.classes.end()) {
    lc.diags.ReportError(attribute->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Python object uses an unregistered static class layout: " +
                             class_name);
    return {};
  }
  auto field_it = class_it->second.field_indices.find(attribute->attribute);
  if (field_it == class_it->second.field_indices.end()) {
    if (class_it->second.method_info.count(attribute->attribute) != 0) {
      lc.diags.ReportError(
          attribute->loc, frontends::ErrorCode::kUnsupportedLowering,
          "Python method '" + class_name + "." + attribute->attribute +
              "' cannot be used as a value: bound-method descriptors are outside the "
              "static object subset");
    } else {
      lc.diags.ReportError(
          attribute->loc, frontends::ErrorCode::kUnsupportedLowering,
          "Python dynamic attribute '" + class_name + "." + attribute->attribute +
              "' is not supported; declare integer fields with unconditional "
              "self.<field> assignments in __init__");
    }
    return {};
  }

  const size_t field_index = field_it->second;
  if (field_index >= class_it->second.struct_type.subtypes.size()) {
    lc.diags.ReportError(attribute->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Python static class field index is outside its aggregate layout");
    return {};
  }
  auto gep = lc.builder.MakeGEP(object.value, class_it->second.struct_type,
                                {0, field_index}, lc.NextTemp("field.addr"));
  return {gep->name, class_it->second.struct_type.subtypes[field_index], class_name};
}

EvalResult EvalCall(const std::shared_ptr<CallExpression> &call, LoweringContext &lc) {
  for (const auto &arg : call->args) {
    if (arg.is_star || arg.is_kwstar || !arg.keyword.empty()) {
      lc.diags.ReportError(call->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "Python keyword/star arguments require descriptor-aware call lowering");
      return EvalResult::Invalid();
    }
  }

  auto evaluate_arguments = [&](const frontends::NativeBuiltin *native_api = nullptr)
      -> std::optional<std::pair<std::vector<std::string>, std::vector<ir::IRType>>> {
    std::vector<std::string> values;
    std::vector<ir::IRType> types;
    values.reserve(call->args.size());
    types.reserve(call->args.size());
    for (const auto &arg : call->args) {
      if (native_api && values.size() < native_api->params.size() &&
          native_api->params[values.size()] == frontends::NativeType::kString) {
        if (auto literal = std::dynamic_pointer_cast<Literal>(arg.value); literal && literal->is_string) {
          std::string text, error;
          if (literal->is_bytes_string) error = "Python native text APIs require str, not bytes";
          // The C-string bridge supports ordinary escapes and Unicode scalar
          // escapes. Reject other forms rather than applying C byte semantics.
          if (!literal->is_raw_string) for (size_t i = 0; i < literal->value.size() && error.empty(); ++i) {
            if (literal->value[i] != '\\') continue;
            if (++i == literal->value.size()) { error = "unfinished Python string escape"; break; }
            const char escape = literal->value[i];
            if (std::string("nrtabfv\\\"'uU").find(escape) == std::string::npos)
              error = "unsupported Python native string escape";
            if (escape == 'u' || escape == 'U') i += escape == 'u' ? 4 : 8;
          }
          if (error.empty()) frontends::DecodeNativeString(literal->value, literal->is_raw_string, false, text, error);
          if (!error.empty()) {
            lc.diags.ReportError(literal->loc, frontends::ErrorCode::kUnsupportedLowering, error);
            return std::nullopt;
          }
          values.push_back(lc.builder.MakeStringLiteral(text, lc.NextTemp("native.str")));
          types.push_back(ir::IRType::Pointer(ir::IRType::I8()));
          continue;
        }
      }
      auto evaluated = EvalExpr(arg.value, lc);
      if (!evaluated.IsValid())
        return std::nullopt;
      values.push_back(evaluated.value);
      types.push_back(evaluated.type);
    }
    return std::make_pair(std::move(values), std::move(types));
  };

  // The static subset models only direct instance calls.  The receiver is
  // evaluated before arguments, matching Python's evaluation order, then
  // passed explicitly as the first IR argument.
  if (auto attribute = std::dynamic_pointer_cast<AttributeExpression>(call->callee)) {
    auto object = EvalExpr(attribute->object, lc);
    if (!object.IsValid())
      return EvalResult::Invalid();
    const std::string class_name = ClassNameOf(object.type);
    if (class_name.empty()) {
      lc.diags.ReportError(
          attribute->loc, frontends::ErrorCode::kUnsupportedLowering,
          "Python dynamic method calls require descriptor and runtime dispatch semantics");
      return EvalResult::Invalid();
    }

    auto class_it = lc.classes.find(class_name);
    auto method_it = class_it == lc.classes.end()
                         ? ClassInfo::MethodInfo{} /* only used for a diagnostic below */
                         : [&]() {
                             auto found = class_it->second.method_info.find(attribute->attribute);
                             return found == class_it->second.method_info.end()
                                        ? ClassInfo::MethodInfo{}
                                        : found->second;
                           }();
    if (method_it.lowered_name.empty()) {
      lc.diags.ReportError(attribute->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "Python dynamic/unknown method '" + class_name + "." +
                               attribute->attribute +
                               "' is not part of the statically declared class");
      return EvalResult::Invalid();
    }

    auto evaluated = evaluate_arguments();
    if (!evaluated)
      return EvalResult::Invalid();
    auto &[args, arg_types] = *evaluated;
    if (arg_types.size() != method_it.parameter_types.size()) {
      lc.diags.ReportError(attribute->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "Python static method '" + class_name + "." +
                               attribute->attribute + "' expects " +
                               std::to_string(method_it.parameter_types.size()) +
                               " argument(s), got " + std::to_string(arg_types.size()));
      return EvalResult::Invalid();
    }
    for (size_t i = 0; i < arg_types.size(); ++i) {
      if (!arg_types[i].SameShape(method_it.parameter_types[i])) {
        lc.diags.ReportError(attribute->loc, frontends::ErrorCode::kUnsupportedLowering,
                             "Python static method argument requires an exact modeled ABI type");
        return EvalResult::Invalid();
      }
    }
    args.insert(args.begin(), object.value);
    auto inst = lc.builder.MakeCall(method_it.lowered_name, args, method_it.return_type,
                                    lc.NextTemp("method.call"));
    return {inst->name, inst->type};
  }

  auto name = std::dynamic_pointer_cast<Identifier>(call->callee);
  if (!name) {
    lc.diags.ReportError(call->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Python indirect calls require callable-object runtime semantics");
    return EvalResult::Invalid();
  }
  const std::string &callee_name = name->name;

  if (callee_name == "getattr" || callee_name == "setattr" || callee_name == "hasattr") {
    lc.diags.ReportError(
        call->loc, frontends::ErrorCode::kUnsupportedLowering,
        "Python dynamic attribute built-in '" + callee_name +
            "' is not supported by deterministic static class layouts");
    return EvalResult::Invalid();
  }

  auto evaluated = evaluate_arguments(frontends::FindNativeBuiltin(callee_name));
  if (!evaluated)
    return EvalResult::Invalid();
  auto &[args, arg_types] = *evaluated;

  // Class construction is stack based.  This deliberately does not claim
  // Python GC semantics: callers must invoke close()/__del__() explicitly if
  // the class exposes a lifecycle cleanup method.
  if (auto class_it = lc.classes.find(callee_name); class_it != lc.classes.end()) {
    auto init_it = class_it->second.method_info.find("__init__");
    if (init_it == class_it->second.method_info.end()) {
      lc.diags.ReportError(call->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "Python static class '" + callee_name +
                               "' requires an explicit __init__ method");
      return EvalResult::Invalid();
    }
    if (arg_types.size() != init_it->second.parameter_types.size()) {
      lc.diags.ReportError(call->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "Python constructor '" + callee_name + "' expects " +
                               std::to_string(init_it->second.parameter_types.size()) +
                               " argument(s), got " + std::to_string(arg_types.size()));
      return EvalResult::Invalid();
    }
    for (size_t i = 0; i < arg_types.size(); ++i) {
      if (!arg_types[i].SameShape(init_it->second.parameter_types[i])) {
        lc.diags.ReportError(call->loc, frontends::ErrorCode::kUnsupportedLowering,
                             "Python constructor argument requires an exact modeled ABI type");
        return EvalResult::Invalid();
      }
    }

    auto storage = lc.builder.MakeAlloca(class_it->second.struct_type,
                                         lc.NextTemp(callee_name + ".object"));
    args.insert(args.begin(), storage->name);
    lc.builder.MakeCall(init_it->second.lowered_name, args, ir::IRType::Void(), "");
    return {storage->name, ir::IRType::Pointer(class_it->second.struct_type)};
  }

  if (const auto *api = frontends::FindNativeBuiltin(callee_name)) {
    auto inst = frontends::EmitNativeBuiltin(*api, args, arg_types, lc.builder, lc.ir_ctx, lc.diags, call->loc);
    if (!inst) return EvalResult::Invalid();
    return {inst->name, inst->type};
  }
  // Handle built-in functions
  if (callee_name == "print") {
    // Generate call to runtime print function
    auto inst = lc.builder.MakeCall("__py_print", args, ir::IRType::Void(), "");
    return {"", ir::IRType::Void()};
  }
  if (callee_name == "len") {
    auto inst = lc.builder.MakeCall("__py_len", args, ir::IRType::I64(true), lc.NextTemp("len"));
    return {inst->name, ir::IRType::I64(true)};
  }
  if (callee_name == "range") {
    // Range returns an iterator object with start, stop, step, and current
    // __py_range creates an iterator object that can be used with for loops
    // The iterator protocol: __iter__ returns self, __next__ returns next value or raises
    // StopIteration

    ir::IRType range_iter_type = ir::IRType::Pointer(ir::IRType::I64(true));

    std::string range_fn;
    if (args.size() == 1) {
      // range(stop) -> range(0, stop, 1)
      range_fn = "__py_range_1";
    } else if (args.size() == 2) {
      // range(start, stop) -> range(start, stop, 1)
      range_fn = "__py_range_2";
    } else if (args.size() == 3) {
      // range(start, stop, step)
      range_fn = "__py_range_3";
    } else {
      lc.diags.Report(call->loc, "range() requires 1-3 arguments");
      return EvalResult::Invalid();
    }

    auto inst = lc.builder.MakeCall(range_fn, args, range_iter_type, lc.NextTemp("range"));
    return {inst->name, range_iter_type};
  }

  auto return_it = lc.function_returns.find(callee_name);
  if (return_it == lc.function_returns.end()) {
    lc.diags.ReportError(call->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Python call target '" + callee_name +
                             "' has no statically modeled signature");
    return EvalResult::Invalid();
  }
  auto inst = lc.builder.MakeCall(callee_name, args, return_it->second, lc.NextTemp("call"));
  return {inst->name, inst->type};
}

/** @} */

/** @name - */
/** @{ */
// Attribute expression evaluation.
/** @} */

/** @name - */
/** @{ */
EvalResult EvalAttribute(const std::shared_ptr<AttributeExpression> &attr, LoweringContext &lc) {
  auto obj = EvalExpr(attr->object, lc);
  if (!obj.IsValid())
    return EvalResult::Invalid();

  if (!ClassNameOf(obj.type).empty()) {
    auto address = EvalStaticAttributeAddress(attr, obj, lc);
    if (!address.IsValid())
      return EvalResult::Invalid();
    auto load = lc.builder.MakeLoad(address.address, address.field_type,
                                    lc.NextTemp("field.load"));
    return {load->name, load->type};
  }

  lc.diags.ReportError(
      attr->loc, frontends::ErrorCode::kUnsupportedLowering,
      "Python attribute access requires a statically known class object; dynamic attributes "
      "need the full Python object runtime");
  return EvalResult::Invalid();
}

/** @} */

/** @name - */
/** @{ */
// Index expression evaluation.
/** @} */

/** @name - */
/** @{ */
EvalResult EvalIndex(const std::shared_ptr<IndexExpression> &idx, LoweringContext &lc) {
  auto obj = EvalExpr(idx->object, lc);
  auto index = EvalExpr(idx->index, lc);
  if (!obj.IsValid() || !index.IsValid())
    return EvalResult::Invalid();

  auto inst = lc.builder.MakeCall("__py_getitem", {obj.value, index.value}, ir::IRType::I64(true),
                                  lc.NextTemp("idx"));
  return {inst->name, inst->type};
}

/** @} */

/** @name - */
/** @{ */
// Slice expression evaluation.
/** @} */

/** @name - */
/** @{ */
EvalResult EvalSlice(const std::shared_ptr<SliceExpression> &slice, LoweringContext &lc) {
  if (!slice->start || !slice->stop || !slice->step) {
    lc.diags.ReportError(slice->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "omitted Python slice bounds require a distinct None sentinel");
    return EvalResult::Invalid();
  }
  auto start = EvalExpr(slice->start, lc);
  auto stop = EvalExpr(slice->stop, lc);
  auto step = EvalExpr(slice->step, lc);

  if (!start.IsValid() || !stop.IsValid() || !step.IsValid())
    return EvalResult::Invalid();

  auto inst = lc.builder.MakeCall("__py_slice", {start.value, stop.value, step.value},
                                  ir::IRType::I64(true), lc.NextTemp("slice"));
  return {inst->name, inst->type};
}

/** @} */

/** @name - */
/** @{ */
// Tuple expression evaluation.
/** @} */

/** @name - */
/** @{ */
EvalResult EvalTuple(const std::shared_ptr<TupleExpression> &tup, LoweringContext &lc) {
  std::vector<std::string> elems;
  for (const auto &e : tup->elements) {
    auto ev = EvalExpr(e, lc);
    if (!ev.IsValid())
      return EvalResult::Invalid();
    elems.push_back(ev.value);
  }

  // Create tuple via runtime call
  auto inst =
      lc.builder.MakeCall("__py_make_tuple", elems, ir::IRType::I64(true), lc.NextTemp("tuple"));
  return {inst->name, inst->type};
}

/** @} */

/** @name - */
/** @{ */
// List expression evaluation.
/** @} */

/** @name - */
/** @{ */
EvalResult EvalList(const std::shared_ptr<ListExpression> &lst, LoweringContext &lc) {
  std::vector<std::string> elems;
  for (const auto &e : lst->elements) {
    auto ev = EvalExpr(e, lc);
    if (!ev.IsValid())
      return EvalResult::Invalid();
    elems.push_back(ev.value);
  }

  auto inst =
      lc.builder.MakeCall("__py_make_list", elems, ir::IRType::I64(true), lc.NextTemp("list"));
  return {inst->name, inst->type};
}

/** @} */

/** @name - */
/** @{ */
// Dict expression evaluation.
/** @} */

/** @name - */
/** @{ */
EvalResult EvalDict(const std::shared_ptr<DictExpression> &dict, LoweringContext &lc) {
  std::vector<std::string> args;
  for (const auto &[k, v] : dict->items) {
    auto key = EvalExpr(k, lc);
    auto val = EvalExpr(v, lc);
    if (!key.IsValid() || !val.IsValid())
      return EvalResult::Invalid();
    args.push_back(key.value);
    args.push_back(val.value);
  }

  auto inst =
      lc.builder.MakeCall("__py_make_dict", args, ir::IRType::I64(true), lc.NextTemp("dict"));
  return {inst->name, inst->type};
}

/** @} */

/** @name - */
/** @{ */
// Set expression evaluation.
/** @} */

/** @name - */
/** @{ */
EvalResult EvalSet(const std::shared_ptr<SetExpression> &set_expr, LoweringContext &lc) {
  std::vector<std::string> elems;
  for (const auto &e : set_expr->elements) {
    auto ev = EvalExpr(e, lc);
    if (!ev.IsValid())
      return EvalResult::Invalid();
    elems.push_back(ev.value);
  }

  auto inst =
      lc.builder.MakeCall("__py_make_set", elems, ir::IRType::I64(true), lc.NextTemp("set"));
  return {inst->name, inst->type};
}

/** @} */

/** @name - */
/** @{ */
// Comprehension expression evaluation.
// Implements full loop unrolling for list/set/dict comprehensions.
// e.g. [x*2 for x in range(10) if x > 5] generates proper loop IR.
/** @} */

/** @name - */
/** @{ */
EvalResult EvalComprehension(const std::shared_ptr<ComprehensionExpression> &comp,
                             LoweringContext &lc) {
  // Create a temporary list/set/dict container
  std::string result_name;
  ir::IRType result_type = ir::IRType::I64(true);

  switch (comp->kind) {
  case ComprehensionExpression::Kind::kList:
    result_name = lc.NextTemp("listcomp");
    break;
  case ComprehensionExpression::Kind::kSet:
    result_name = lc.NextTemp("setcomp");
    break;
  case ComprehensionExpression::Kind::kDict:
    result_name = lc.NextTemp("dictcomp");
    break;
  case ComprehensionExpression::Kind::kGenerator:
    lc.diags.ReportError(comp->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Python generator expressions require lazy generator-frame lowering");
    return EvalResult::Invalid();
  }

  // Create empty container
  std::string init_fn;
  std::string append_fn;
  switch (comp->kind) {
  case ComprehensionExpression::Kind::kList:
  case ComprehensionExpression::Kind::kGenerator:
    init_fn = "__py_make_list";
    append_fn = "__py_list_append";
    break;
  case ComprehensionExpression::Kind::kSet:
    init_fn = "__py_make_set";
    append_fn = "__py_set_add";
    break;
  case ComprehensionExpression::Kind::kDict:
    init_fn = "__py_make_dict";
    append_fn = "__py_dict_setitem";
    break;
  }

  auto container = lc.builder.MakeCall(init_fn, {}, result_type, result_name);

  if (comp->clauses.empty()) {
    return {container->name, result_type};
  }

  // Generate nested loops for each comprehension clause
  // Stack to track loop blocks for proper nesting
  struct LoopInfo {
    ir::BasicBlock *header;
    ir::BasicBlock *body;
    ir::BasicBlock *end;
    std::string iter_var;
    std::string iter_obj;
  };
  std::vector<LoopInfo> loops;

  // Process each clause (for x in iterable if condition)
  // Note: Comprehension struct uses 'ifs' for conditions and 'target'/'iterable' for loop
  for (const auto &clause : comp->clauses) {
    auto iterable = EvalExpr(clause.iterable, lc);
    if (!iterable.IsValid())
      return EvalResult::Invalid();

    // Get iterator from iterable
    auto iter =
        lc.builder.MakeCall("__py_iter", {iterable.value},
                            ir::IRType::Pointer(ir::IRType::I64(true)), lc.NextTemp("iter"));

    // Create loop blocks
    auto *header = lc.fn->CreateBlock("comp.header");
    auto *body = lc.fn->CreateBlock("comp.body");
    auto *filter_block = clause.ifs.empty() ? body : lc.fn->CreateBlock("comp.filter");
    auto *end = lc.fn->CreateBlock("comp.end");

    lc.builder.MakeBranch(header);
    lc.SetInsertBlock(header);

    // Call __py_next to get next element; returns None when exhausted
    auto next_val =
        lc.builder.MakeCall("__py_next", {iter->name}, ir::IRType::I64(true), lc.NextTemp("next"));

    // Check if iterator is exhausted (returns special sentinel value)
    auto is_done = lc.builder.MakeCall("__py_iter_done", {next_val->name}, ir::IRType::I1(),
                                       lc.NextTemp("done"));

    lc.builder.MakeCondBranch(is_done->name, end, filter_block);

    // Bind loop variable
    std::string target_name;
    if (auto id = std::dynamic_pointer_cast<Identifier>(clause.target)) {
      target_name = id->name;
    } else {
      lc.diags.ReportError(
          comp->loc, frontends::ErrorCode::kUnsupportedLowering,
          "Python destructuring comprehension targets require pattern-binding lowering");
      return EvalResult::Invalid();
    }

    // Store in filter block or body block
    lc.SetInsertBlock(filter_block);
    lc.env[target_name] = {next_val->name, ir::IRType::I64(true), "", false};

    // Evaluate filter conditions (ifs)
    if (!clause.ifs.empty()) {
      for (size_t i = 0; i < clause.ifs.size(); ++i) {
        auto cond = EvalExpr(clause.ifs[i], lc);
        if (!cond.IsValid())
          return EvalResult::Invalid();
        if (cond.type.kind != ir::IRTypeKind::kI1) {
          lc.diags.ReportError(clause.ifs[i]->loc, frontends::ErrorCode::kUnsupportedLowering,
                               "Python comprehension truthiness requires dynamic runtime semantics");
          return EvalResult::Invalid();
        }

        if (i + 1 < clause.ifs.size()) {
          // More conditions to check
          auto *next_filter = lc.fn->CreateBlock("comp.filter");
          lc.builder.MakeCondBranch(cond.value, next_filter, header);
          lc.SetInsertBlock(next_filter);
        } else {
          // Last condition, branch to body or back to header
          lc.builder.MakeCondBranch(cond.value, body, header);
        }
      }
    }

    loops.push_back({header, body, end, target_name, iter->name});
    lc.SetInsertBlock(body);
  }

  // Now in innermost body block, evaluate the element expression and append
  // ComprehensionExpression uses 'elem' for element and 'key' for dict key
  if (comp->kind == ComprehensionExpression::Kind::kDict) {
    // Dict comprehension: key: value
    if (comp->key && comp->elem) {
      auto key = EvalExpr(comp->key, lc);
      auto value = EvalExpr(comp->elem, lc);
      if (!key.IsValid() || !value.IsValid())
        return EvalResult::Invalid();
      lc.builder.MakeCall(append_fn, {container->name, key.value, value.value}, ir::IRType::Void(),
                          "");
    }
  } else {
    // List/Set/Generator: single element
    auto elem = EvalExpr(comp->elem, lc);
    if (!elem.IsValid())
      return EvalResult::Invalid();
    lc.builder.MakeCall(append_fn, {container->name, elem.value}, ir::IRType::Void(), "");
  }

  // Branch back to innermost header
  if (!loops.empty()) {
    lc.builder.MakeBranch(loops.back().header);
  }

  // Wire up end blocks - go from innermost to outermost
  for (int i = static_cast<int>(loops.size()) - 1; i >= 0; --i) {
    lc.SetInsertBlock(loops[i].end);
    if (i > 0) {
      // Continue to outer loop header
      lc.builder.MakeBranch(loops[i - 1].header);
    }
    // else: outermost end block, will be set as final block below
  }

  // Final block after all loops
  auto *final_block = lc.fn->CreateBlock("comp.done");
  if (!loops.empty()) {
    lc.SetInsertBlock(loops[0].end);
    lc.builder.MakeBranch(final_block);
  }
  lc.SetInsertBlock(final_block);

  return {container->name, result_type};
}

/** @} */

/** @name - */
/** @{ */
// Lambda expression evaluation.
/** @} */

/** @name - */
/** @{ */
EvalResult EvalLambda(const std::shared_ptr<LambdaExpression> &lambda, LoweringContext &lc) {
  lc.diags.ReportError(lambda->loc, frontends::ErrorCode::kUnsupportedLowering,
                       "Python lambda lowering requires closure-environment and function-object ABI support");
  return EvalResult::Invalid();
}

/** @} */

/** @name - */
/** @{ */
// Await expression evaluation.
/** @} */

/** @name - */
/** @{ */
EvalResult EvalAwait(const std::shared_ptr<AwaitExpression> &await, LoweringContext &lc) {
  lc.diags.ReportError(await->loc, frontends::ErrorCode::kUnsupportedLowering,
                       "Python await requires coroutine suspension and resumption lowering");
  return EvalResult::Invalid();
}

/** @} */

/** @name - */
/** @{ */
// Yield expression evaluation.
/** @} */

/** @name - */
/** @{ */
EvalResult EvalYield(const std::shared_ptr<YieldExpression> &yield, LoweringContext &lc) {
  lc.diags.ReportError(yield->loc, frontends::ErrorCode::kUnsupportedLowering,
                       "Python yield requires resumable generator-frame lowering");
  return EvalResult::Invalid();
}

/** @} */

/** @name - */
/** @{ */
// Named expression (walrus operator) evaluation.
/** @} */

/** @name - */
/** @{ */
EvalResult EvalNamedExpr(const std::shared_ptr<NamedExpression> &named, LoweringContext &lc) {
  auto val = EvalExpr(named->value, lc);
  if (!val.IsValid())
    return EvalResult::Invalid();

  // Assign to target
  if (auto id = std::dynamic_pointer_cast<Identifier>(named->target)) {
    lc.env[id->name] = {val.value, val.type, "", false};
  } else {
    lc.diags.ReportError(named->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Python named-expression target is not lowerable");
    return EvalResult::Invalid();
  }

  return val;
}

/** @} */

/** @name - */
/** @{ */
// Formatted string evaluation.
/** @} */

/** @name - */
/** @{ */
EvalResult EvalFormattedString(const std::shared_ptr<FormattedString> &fstr, LoweringContext &lc) {
  std::vector<std::string> parts;
  for (const auto &part : fstr->parts) {
    if (part.is_literal) {
      auto str_ptr = lc.builder.MakeStringLiteral(part.literal, lc.NextTemp("fstr.lit"));
      parts.push_back(str_ptr);
    } else {
      if (!part.format_spec.empty()) {
        lc.diags.ReportError(
            fstr->loc, frontends::ErrorCode::kUnsupportedLowering,
            "Python f-string conversions and format specifications require runtime formatting");
        return EvalResult::Invalid();
      }
      auto val = EvalExpr(part.expr, lc);
      if (!val.IsValid())
        return EvalResult::Invalid();
      // Convert to string if needed
      auto str_val = lc.builder.MakeCall(
          "__py_str", {val.value}, ir::IRType::Pointer(ir::IRType::I8()), lc.NextTemp("fstr.val"));
      parts.push_back(str_val->name);
    }
  }

  // Concatenate all parts
  auto inst = lc.builder.MakeCall("__py_str_concat", parts, ir::IRType::Pointer(ir::IRType::I8()),
                                  lc.NextTemp("fstr"));
  return {inst->name, inst->type};
}

/** @} */

/** @name - */
/** @{ */
// Main expression evaluator.
/** @} */

/** @name - */
/** @{ */
EvalResult EvalExpr(const std::shared_ptr<Expression> &expr, LoweringContext &lc) {
  if (!expr)
    return EvalResult::Invalid();

  // Literal
  if (auto literal = std::dynamic_pointer_cast<Literal>(expr)) {
    if (literal->is_string) {
      auto str_ptr = lc.builder.MakeStringLiteral(literal->value, lc.NextTemp("str"));
      return {str_ptr, ir::IRType::Pointer(ir::IRType::I8())};
    }
    if (literal->value == "True" || literal->value == "False")
      return {literal->value == "True" ? "1" : "0", ir::IRType::I1()};
    if (literal->value == "None") {
      lc.diags.ReportError(literal->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "Python None requires a distinct object/sentinel representation");
      return EvalResult::Invalid();
    }
    long long iv{};
    if (IsIntegerLiteral(literal->value, &iv)) {
      return MakeLiteral(iv, lc);
    }
    double fv{};
    if (IsFloatLiteral(literal->value, &fv)) {
      return MakeFloatLiteral(fv, lc);
    }
    lc.diags.Report(literal->loc, "Invalid literal: " + literal->value);
    return EvalResult::Invalid();
  }

  // Identifier
  if (auto name = std::dynamic_pointer_cast<Identifier>(expr)) {
    return EvalName(name, lc);
  }

  // Binary expression
  if (auto binop = std::dynamic_pointer_cast<BinaryExpression>(expr)) {
    return EvalBinOp(binop, lc);
  }

  // Unary expression
  if (auto unary = std::dynamic_pointer_cast<UnaryExpression>(expr)) {
    return EvalUnaryOp(unary, lc);
  }

  // Call expression
  if (auto call = std::dynamic_pointer_cast<CallExpression>(expr)) {
    return EvalCall(call, lc);
  }

  // Attribute expression
  if (auto attr = std::dynamic_pointer_cast<AttributeExpression>(expr)) {
    return EvalAttribute(attr, lc);
  }

  // Index expression
  if (auto idx = std::dynamic_pointer_cast<IndexExpression>(expr)) {
    return EvalIndex(idx, lc);
  }

  // Slice expression
  if (auto slice = std::dynamic_pointer_cast<SliceExpression>(expr)) {
    return EvalSlice(slice, lc);
  }

  // Tuple expression
  if (auto tup = std::dynamic_pointer_cast<TupleExpression>(expr)) {
    return EvalTuple(tup, lc);
  }

  // List expression
  if (auto lst = std::dynamic_pointer_cast<ListExpression>(expr)) {
    return EvalList(lst, lc);
  }

  // Dict expression
  if (auto dict = std::dynamic_pointer_cast<DictExpression>(expr)) {
    return EvalDict(dict, lc);
  }

  // Set expression
  if (auto set_expr = std::dynamic_pointer_cast<SetExpression>(expr)) {
    return EvalSet(set_expr, lc);
  }

  // Comprehension expression
  if (auto comp = std::dynamic_pointer_cast<ComprehensionExpression>(expr)) {
    return EvalComprehension(comp, lc);
  }

  // Lambda expression
  if (auto lambda = std::dynamic_pointer_cast<LambdaExpression>(expr)) {
    return EvalLambda(lambda, lc);
  }

  // Await expression
  if (auto await = std::dynamic_pointer_cast<AwaitExpression>(expr)) {
    return EvalAwait(await, lc);
  }

  // Yield expression
  if (auto yield = std::dynamic_pointer_cast<YieldExpression>(expr)) {
    return EvalYield(yield, lc);
  }

  // Named expression (:=)
  if (auto named = std::dynamic_pointer_cast<NamedExpression>(expr)) {
    return EvalNamedExpr(named, lc);
  }

  if (auto templated = std::dynamic_pointer_cast<TemplateString>(expr)) {
    lc.diags.ReportError(templated->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Python template strings require runtime Template-object lowering");
    return EvalResult::Invalid();
  }
  // Formatted string (f-string)
  if (auto fstr = std::dynamic_pointer_cast<FormattedString>(expr)) {
    return EvalFormattedString(fstr, lc);
  }

  lc.diags.ReportError(expr->loc, frontends::ErrorCode::kUnsupportedLowering,
                       "Python expression is parsed but has no faithful IR lowering");
  return EvalResult::Invalid();
}

/** @} */

/** @name - */
/** @{ */
// Statement lowering functions.
/** @} */

/** @name - */
/** @{ */

bool LowerReturn(const std::shared_ptr<ReturnStatement> &ret, LoweringContext &lc) {
  if (lc.terminated)
    return true;
  EvalResult v;
  if (ret->value) {
    v = EvalExpr(ret->value, lc);
    if (!v.IsValid())
      return false;
  } else if (lc.fn && lc.fn->ret_type.kind != ir::IRTypeKind::kVoid) {
    lc.diags.ReportError(ret->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Python bare return cannot be represented by a non-None IR return type");
    return false;
  }
  lc.builder.MakeReturn(v.value);
  lc.terminated = true;
  return true;
}

bool LowerAssign(const std::shared_ptr<Assignment> &assign, LoweringContext &lc) {
  if (assign->targets.empty()) {
    lc.diags.ReportError(assign->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Python assignment has no lowerable target");
    return false;
  }

  // Handle annotated assignment without value (just type declaration)
  if (!assign->value && assign->annotation) {
    // A variable annotation does not initialize or bind a runtime value.
    // Leaving it out of the value environment makes a later read fail closed.
    return true;
  }

  if (!assign->value)
    return false;

  auto result = EvalExpr(assign->value, lc);
  if (!result.IsValid())
    return false;

  // Handle augmented assignment
  if (assign->op != "=") {
    if (assign->targets.size() != 1) {
      lc.diags.ReportError(assign->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "Python augmented assignment requires a single static target");
      return false;
    }
    auto target = assign->targets[0];
    if (auto name = std::dynamic_pointer_cast<Identifier>(target)) {
      auto current = EvalName(name, lc);
      if (!current.IsValid())
        return false;

      std::string bin_op;
      if (assign->op == "+=")
        bin_op = "+";
      else if (assign->op == "-=")
        bin_op = "-";
      else if (assign->op == "*=")
        bin_op = "*";
      else if (assign->op == "/=")
        bin_op = "/";
      else if (assign->op == "//=")
        bin_op = "//";
      else if (assign->op == "%=")
        bin_op = "%";
      else if (assign->op == "**=")
        bin_op = "**";
      else if (assign->op == "&=")
        bin_op = "&";
      else if (assign->op == "|=")
        bin_op = "|";
      else if (assign->op == "^=")
        bin_op = "^";
      else if (assign->op == "<<=")
        bin_op = "<<";
      else if (assign->op == ">>=")
        bin_op = ">>";
      else {
        lc.diags.ReportError(assign->loc, frontends::ErrorCode::kUnsupportedLowering,
                             "Python augmented assignment operator '" + assign->op +
                                 "' has no faithful IR lowering");
        return false;
      }

      if (bin_op == "**" || bin_op == "/" || bin_op == "//" || bin_op == "%") {
        lc.diags.ReportError(assign->loc, frontends::ErrorCode::kUnsupportedLowering,
                             "Python augmented operator '" + assign->op +
                                 "' requires Python runtime numeric semantics");
        return false;
      }

      bool is_float =
          (current.type.kind == ir::IRTypeKind::kF64 || result.type.kind == ir::IRTypeKind::kF64);
      if (!current.type.SameShape(result.type)) {
        lc.diags.ReportError(assign->loc, frontends::ErrorCode::kUnsupportedLowering,
                             "Python augmented assignment coercion requires runtime semantics");
        return false;
      }
      auto op = MapBinOp(bin_op, is_float);
      if (!op) {
        lc.diags.ReportError(assign->loc, frontends::ErrorCode::kUnsupportedLowering,
                             "Python augmented assignment has no primitive IR lowering");
        return false;
      }
      auto inst = lc.builder.MakeBinary(*op, current.value, result.value, lc.NextTemp("aug"));
      inst->type = current.type;
      auto &entry = lc.env.at(name->name);
      if (entry.is_mutable) lc.builder.MakeStore(entry.alloca_name, inst->name);
      else entry = {inst->name, inst->type, "", false};
      return true;
    }
    if (auto attr = std::dynamic_pointer_cast<AttributeExpression>(target)) {
      auto object = EvalExpr(attr->object, lc);
      if (!object.IsValid())
        return false;
      if (ClassNameOf(object.type).empty()) {
        lc.diags.ReportError(
            assign->loc, frontends::ErrorCode::kUnsupportedLowering,
            "Python augmented dynamic-attribute assignment requires the full object runtime");
        return false;
      }
      auto address = EvalStaticAttributeAddress(attr, object, lc);
      if (!address.IsValid())
        return false;
      if (!address.field_type.SameShape(result.type)) {
        lc.diags.ReportError(assign->loc, frontends::ErrorCode::kUnsupportedLowering,
                             "Python static class fields have a deterministic integer ABI");
        return false;
      }

      std::string bin_op = assign->op.substr(0, assign->op.size() - 1);
      if (bin_op == "**" || bin_op == "/" || bin_op == "//" || bin_op == "%") {
        lc.diags.ReportError(assign->loc, frontends::ErrorCode::kUnsupportedLowering,
                             "Python augmented member operator '" + assign->op +
                                 "' requires Python runtime numeric semantics");
        return false;
      }
      auto op = MapBinOp(bin_op, false);
      if (!op) {
        lc.diags.ReportError(assign->loc, frontends::ErrorCode::kUnsupportedLowering,
                             "Python augmented member operator '" + assign->op +
                                 "' has no primitive IR lowering");
        return false;
      }
      auto old_value = lc.builder.MakeLoad(address.address, address.field_type,
                                           lc.NextTemp("field.old"));
      auto updated = lc.builder.MakeBinary(*op, old_value->name, result.value,
                                           lc.NextTemp("field.update"));
      updated->type = address.field_type;
      lc.builder.MakeStore(address.address, updated->name);
      return true;
    }
    lc.diags.ReportError(assign->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Python augmented subscript assignment requires runtime "
                         "read-modify-write semantics");
    return false;
  }

  // Simple assignment
  for (const auto &target : assign->targets) {
    if (auto name = std::dynamic_pointer_cast<Identifier>(target)) {
      auto existing = lc.env.find(name->name);
      if (existing != lc.env.end() && existing->second.is_mutable &&
          existing->second.type.SameShape(result.type)) {
        lc.builder.MakeStore(existing->second.alloca_name, result.value);
      } else {
        auto storage = lc.builder.MakeAlloca(result.type, lc.NextTemp("local.addr"));
        lc.builder.MakeStore(storage->name, result.value);
        lc.env[name->name] = {result.value, result.type, storage->name, true};
      }
    } else if (auto tup = std::dynamic_pointer_cast<TupleExpression>(target)) {
      // Tuple unpacking
      for (size_t i = 0; i < tup->elements.size(); ++i) {
        if (auto elem_name = std::dynamic_pointer_cast<Identifier>(tup->elements[i])) {
          auto idx_val = MakeLiteral(static_cast<long long>(i), lc);
          auto item = lc.builder.MakeCall("__py_getitem", {result.value, idx_val.value},
                                          ir::IRType::I64(true), lc.NextTemp("unpack"));
          lc.env[elem_name->name] = {item->name, item->type, "", false};
        } else {
          lc.diags.ReportError(assign->loc, frontends::ErrorCode::kUnsupportedLowering,
                               "nested Python unpacking targets require recursive lowering");
          return false;
        }
      }
    } else if (auto attr = std::dynamic_pointer_cast<AttributeExpression>(target)) {
      auto obj = EvalExpr(attr->object, lc);
      if (!obj.IsValid())
        return false;
      if (ClassNameOf(obj.type).empty()) {
        lc.diags.ReportError(
            assign->loc, frontends::ErrorCode::kUnsupportedLowering,
            "Python dynamic attribute assignment requires the full object runtime; the static "
            "subset only permits __init__-declared integer fields");
        return false;
      }
      auto address = EvalStaticAttributeAddress(attr, obj, lc);
      if (!address.IsValid())
        return false;
      if (!address.field_type.SameShape(result.type)) {
        lc.diags.ReportError(assign->loc, frontends::ErrorCode::kUnsupportedLowering,
                             "Python static class field '" + address.class_name + "." +
                                 attr->attribute + "' requires an integer value");
        return false;
      }
      lc.builder.MakeStore(address.address, result.value);
    } else if (auto idx = std::dynamic_pointer_cast<IndexExpression>(target)) {
      // Index assignment: obj[key] = value
      auto obj = EvalExpr(idx->object, lc);
      auto key = EvalExpr(idx->index, lc);
      if (!obj.IsValid() || !key.IsValid())
        return false;
      lc.builder.MakeCall("__py_setitem", {obj.value, key.value, result.value}, ir::IRType::Void(),
                          "");
    } else {
      lc.diags.ReportError(assign->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "Python assignment target has no faithful IR lowering");
      return false;
    }
  }
  return true;
}

bool LowerIf(const std::shared_ptr<IfStatement> &if_stmt, LoweringContext &lc) {
  if (lc.terminated)
    return true;

  auto cond = EvalExpr(if_stmt->condition, lc);
  if (!cond.IsValid())
    return false;
  if (cond.type.kind != ir::IRTypeKind::kI1) {
    lc.diags.ReportError(if_stmt->condition->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Python if truthiness requires dynamic runtime semantics");
    return false;
  }

  auto *then_block = lc.fn->CreateBlock("if.then");
  auto *else_block = if_stmt->else_body.empty() ? nullptr : lc.fn->CreateBlock("if.else");
  auto *merge_block = lc.fn->CreateBlock("if.end");

  // The false edge of an if-without-else comes from the block containing the
  // condition, which is not necessarily the function entry block.
  auto *condition_block = lc.builder.GetInsertPoint().get();

  // Save environment state before branches for PHI generation
  std::unordered_map<std::string, EnvEntry> env_before = lc.env;

  lc.builder.MakeCondBranch(cond.value, then_block, else_block ? else_block : merge_block);
  lc.terminated = false;

  // Then block
  lc.SetInsertBlock(then_block);
  lc.env = env_before; // Start with same environment
  bool then_term = false;
  for (auto &s : if_stmt->then_body) {
    if (!LowerStmt(s, lc))
      return false;
    if (lc.terminated) {
      then_term = true;
      break;
    }
  }
  auto *then_end_block = lc.builder.GetInsertPoint().get();
  std::unordered_map<std::string, EnvEntry> env_after_then = lc.env;
  if (!then_term) {
    lc.builder.MakeBranch(merge_block);
  }

  // Else block
  bool else_term = false;
  ir::BasicBlock *else_end_block = nullptr;
  std::unordered_map<std::string, EnvEntry> env_after_else = env_before;

  if (else_block) {
    lc.SetInsertBlock(else_block);
    lc.env = env_before; // Reset to same starting environment
    lc.terminated = false;
    for (auto &s : if_stmt->else_body) {
      if (!LowerStmt(s, lc))
        return false;
      if (lc.terminated) {
        else_term = true;
        break;
      }
    }
    else_end_block = lc.builder.GetInsertPoint().get();
    env_after_else = lc.env;
    if (!else_term) {
      lc.builder.MakeBranch(merge_block);
    }
  }

  lc.SetInsertBlock(merge_block);
  const bool both_branches_terminate = else_block && then_term && else_term;
  lc.terminated = both_branches_terminate;

  // Keep the environment associated with paths that can actually reach the
  // merge.  In particular, a terminating then branch without an else must not
  // leak its assignments onto the false path.
  if (!else_block) {
    lc.env = env_before;
  } else if (then_term && !else_term) {
    lc.env = env_after_else;
  } else if (!then_term && else_term) {
    lc.env = env_after_then;
  } else if (both_branches_terminate) {
    lc.env = env_before;
  }

  // Function verification requires every block, including an unreachable
  // synthetic merge block, to have a terminator.
  if (both_branches_terminate) {
    lc.builder.MakeUnreachable();
  }

  // Generate PHI nodes for variables modified in either branch
  // Only needed if both branches can reach merge block
  if (!then_term || (!else_term && else_block)) {
    std::set<std::string> modified_vars;

    // Find variables modified in then branch
    for (const auto &[name, info] : env_after_then) {
      auto it = env_before.find(name);
      if (it == env_before.end() || it->second.value != info.value) {
        modified_vars.insert(name);
      }
    }

    // Find variables modified in else branch
    for (const auto &[name, info] : env_after_else) {
      auto it = env_before.find(name);
      if (it == env_before.end() || it->second.value != info.value) {
        modified_vars.insert(name);
      }
    }

    // Generate PHI for each modified variable
    for (const auto &var_name : modified_vars) {
      std::string then_val, else_val;
      ir::IRType var_type = ir::IRType::I64(true);

      // Get value from then branch
      auto then_it = env_after_then.find(var_name);
      if (then_it != env_after_then.end()) {
        then_val = then_it->second.value;
        var_type = then_it->second.type;
      } else {
        auto before_it = env_before.find(var_name);
        then_val = before_it != env_before.end() ? before_it->second.value : "";
      }

      // Get value from else branch (or original if no else)
      auto else_it = env_after_else.find(var_name);
      if (else_it != env_after_else.end()) {
        else_val = else_it->second.value;
      } else {
        auto before_it = env_before.find(var_name);
        else_val = before_it != env_before.end() ? before_it->second.value : "";
      }

      // Only create PHI if both values are valid and different
      if (!then_val.empty() && !else_val.empty() && then_val != else_val) {
        ir::BasicBlock *then_pred = then_term ? nullptr : then_end_block;
        ir::BasicBlock *else_pred =
            (else_term || !else_block) ? nullptr : (else_end_block ? else_end_block : merge_block);

        // Create PHI with incomings from reachable predecessors
        std::vector<std::pair<ir::BasicBlock *, std::string>> phi_incomings;
        if (then_pred && !then_term) {
          phi_incomings.push_back({then_pred, then_val});
        }
        if (else_block && !else_term && else_pred) {
          phi_incomings.push_back({else_pred, else_val});
        } else if (!else_block && !then_term) {
          // No else branch: use the original value from the condition's
          // block, which owns the direct false edge to the merge.
          phi_incomings.push_back({condition_block, else_val});
        }

        if (phi_incomings.size() > 1) {
          auto phi = lc.builder.MakePhi(var_type, phi_incomings, lc.NextTemp("phi"));
          lc.env[var_name] = {phi->name, var_type, "", false};
        } else if (!phi_incomings.empty()) {
          // Only one incoming, use directly
          lc.env[var_name] = {phi_incomings[0].second, var_type, "", false};
        }
      } else if (!then_val.empty()) {
        // Same value, just update environment
        lc.env[var_name] = {then_val, var_type, "", false};
      }
    }
  }

  return true;
}

bool LowerWhile(const std::shared_ptr<WhileStatement> &while_stmt, LoweringContext &lc) {
  if (lc.terminated)
    return true;

  auto *cond_block = lc.fn->CreateBlock("while.cond");
  auto *body_block = lc.fn->CreateBlock("while.body");
  auto *exit_block = lc.fn->CreateBlock("while.end");

  // Save environment before loop for PHI generation
  std::unordered_map<std::string, EnvEntry> env_before = lc.env;
  auto *entry_block = lc.builder.GetInsertPoint().get();

  // Push loop context for break/continue
  lc.loop_stack.push({cond_block, exit_block});

  lc.builder.MakeBranch(cond_block);
  lc.SetInsertBlock(cond_block);
  lc.terminated = false;

  auto cond = EvalExpr(while_stmt->condition, lc);
  if (!cond.IsValid()) {
    lc.loop_stack.pop();
    return false;
  }
  if (cond.type.kind != ir::IRTypeKind::kI1) {
    lc.diags.ReportError(while_stmt->condition->loc,
                         frontends::ErrorCode::kUnsupportedLowering,
                         "Python while truthiness requires dynamic runtime semantics");
    lc.loop_stack.pop();
    return false;
  }

  lc.builder.MakeCondBranch(cond.value, body_block, exit_block);

  lc.SetInsertBlock(body_block);
  lc.terminated = false;

  // Process loop body
  for (auto &s : while_stmt->body) {
    if (!LowerStmt(s, lc)) {
      lc.loop_stack.pop();
      return false;
    }
    if (lc.terminated)
      break;
  }

  auto *body_end_block = lc.builder.GetInsertPoint().get();
  std::unordered_map<std::string, EnvEntry> env_after_body = lc.env;

  if (!lc.terminated) {
    lc.builder.MakeBranch(cond_block);
  }

  // Now go back to cond_block and create proper PHI nodes for modified variables
  lc.SetInsertBlock(cond_block);

  for (const auto &[name, info] : env_after_body) {
    auto it = env_before.find(name);
    if (it != env_before.end() && it->second.value != info.value) {
      // Variable was modified in loop body
      std::vector<std::pair<ir::BasicBlock *, std::string>> phi_incomings = {
          {entry_block, it->second.value}, // Value before loop
          {body_end_block, info.value}     // Value after loop iteration
      };

      auto phi = lc.builder.MakePhi(info.type, phi_incomings, lc.NextTemp("loop.phi"));

      // Update the environment to use PHI in subsequent iterations
      lc.env[name] = {phi->name, info.type, "", false};
    }
  }

  lc.loop_stack.pop();
  lc.SetInsertBlock(exit_block);
  lc.terminated = false;
  return true;
}

bool LowerFor(const std::shared_ptr<ForStatement> &for_stmt, LoweringContext &lc) {
  if (lc.terminated)
    return true;
  if (for_stmt->is_async) {
    lc.diags.ReportError(for_stmt->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Python async iteration has no faithful IR lowering");
    return false;
  }

  // Save environment before loop for PHI generation
  std::unordered_map<std::string, EnvEntry> env_before = lc.env;
  auto *entry_block = lc.builder.GetInsertPoint().get();

  // Evaluate iterable
  auto iterable = EvalExpr(for_stmt->iterable, lc);
  if (!iterable.IsValid())
    return false;

  // Create iterator
  auto iter = lc.builder.MakeCall("__py_iter", {iterable.value}, ir::IRType::I64(true),
                                  lc.NextTemp("iter"));

  auto *cond_block = lc.fn->CreateBlock("for.cond");
  auto *body_block = lc.fn->CreateBlock("for.body");
  auto *exit_block = lc.fn->CreateBlock("for.end");

  // Push loop context
  lc.loop_stack.push({cond_block, exit_block});

  lc.builder.MakeBranch(cond_block);
  lc.SetInsertBlock(cond_block);
  lc.terminated = false;

  // Check if iterator has next
  auto has_next = lc.builder.MakeCall("__py_iter_has_next", {iter->name}, ir::IRType::I1(),
                                      lc.NextTemp("has_next"));
  lc.builder.MakeCondBranch(has_next->name, body_block, exit_block);

  lc.SetInsertBlock(body_block);
  lc.terminated = false;

  // Get next value and bind to target
  auto next_val = lc.builder.MakeCall("__py_iter_next", {iter->name}, ir::IRType::I64(true),
                                      lc.NextTemp("next"));

  // Bind to loop variable(s)
  if (auto name = std::dynamic_pointer_cast<Identifier>(for_stmt->target)) {
    lc.env[name->name] = {next_val->name, next_val->type, "", false};
  } else if (auto tup = std::dynamic_pointer_cast<TupleExpression>(for_stmt->target)) {
    for (size_t i = 0; i < tup->elements.size(); ++i) {
      if (auto elem = std::dynamic_pointer_cast<Identifier>(tup->elements[i])) {
        auto idx_lit = MakeLiteral(static_cast<long long>(i), lc);
        auto item = lc.builder.MakeCall("__py_getitem", {next_val->name, idx_lit.value},
                                        ir::IRType::I64(true), lc.NextTemp("unpack"));
        lc.env[elem->name] = {item->name, item->type, "", false};
      } else {
        lc.diags.ReportError(tup->elements[i]->loc,
                             frontends::ErrorCode::kUnsupportedLowering,
                             "Python nested/starred loop unpacking has no faithful IR lowering");
        lc.loop_stack.pop();
        return false;
      }
    }
  } else {
    lc.diags.ReportError(for_stmt->target->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Python loop assignment target has no faithful IR lowering");
    lc.loop_stack.pop();
    return false;
  }

  for (auto &s : for_stmt->body) {
    if (!LowerStmt(s, lc)) {
      lc.loop_stack.pop();
      return false;
    }
    if (lc.terminated)
      break;
  }

  auto *body_end_block = lc.builder.GetInsertPoint().get();
  std::unordered_map<std::string, EnvEntry> env_after_body = lc.env;

  if (!lc.terminated) {
    lc.builder.MakeBranch(cond_block);
  }

  // Create PHI nodes for variables modified in for loop body
  lc.SetInsertBlock(cond_block);

  for (const auto &[name, info] : env_after_body) {
    auto it = env_before.find(name);
    if (it != env_before.end() && it->second.value != info.value) {
      // Variable was modified in loop body (skip loop variable itself)
      if (auto target_id = std::dynamic_pointer_cast<Identifier>(for_stmt->target)) {
        if (name == target_id->name)
          continue; // Skip loop variable
      }

      std::vector<std::pair<ir::BasicBlock *, std::string>> phi_incomings = {
          {entry_block, it->second.value}, {body_end_block, info.value}};

      auto phi = lc.builder.MakePhi(info.type, phi_incomings, lc.NextTemp("for.phi"));
      lc.env[name] = {phi->name, info.type, "", false};
    }
  }

  lc.loop_stack.pop();
  lc.SetInsertBlock(exit_block);
  lc.terminated = false;
  return true;
}

bool LowerWith(const std::shared_ptr<WithStatement> &with_stmt, LoweringContext &lc) {
  if (lc.terminated)
    return true;

  lc.diags.ReportError(
      with_stmt->loc, frontends::ErrorCode::kUnsupportedLowering,
      "Python with/async with cleanup across return and exception paths is not implemented");
  return false;

  // Enter context managers
  std::vector<std::pair<std::string, std::string>> contexts; // (mgr, exit_fn)
  for (const auto &item : with_stmt->items) {
    auto mgr = EvalExpr(item.context_expr, lc);
    if (!mgr.IsValid())
      return false;

    // Call __enter__
    auto enter_result = lc.builder.MakeCall("__py_context_enter", {mgr.value},
                                            ir::IRType::I64(true), lc.NextTemp("enter"));
    contexts.push_back({mgr.value, enter_result->name});

    // Bind to variable if present
    if (item.optional_vars) {
      if (auto name = std::dynamic_pointer_cast<Identifier>(item.optional_vars)) {
        lc.env[name->name] = {enter_result->name, enter_result->type, "", false};
      }
    }
  }

  // Create blocks for try-finally structure
  auto *body_block = lc.fn->CreateBlock("with.body");
  auto *cleanup_block = lc.fn->CreateBlock("with.cleanup");
  auto *exit_block = lc.fn->CreateBlock("with.end");

  lc.builder.MakeBranch(body_block);
  lc.SetInsertBlock(body_block);
  lc.terminated = false;

  // Lower body
  bool body_term = false;
  for (auto &s : with_stmt->body) {
    if (!LowerStmt(s, lc))
      return false;
    if (lc.terminated) {
      body_term = true;
      break;
    }
  }
  if (!body_term) {
    lc.builder.MakeBranch(cleanup_block);
  }

  // Cleanup block: call __exit__ for all context managers (in reverse)
  lc.SetInsertBlock(cleanup_block);
  lc.terminated = false;
  for (auto it = contexts.rbegin(); it != contexts.rend(); ++it) {
    lc.builder.MakeCall("__py_context_exit", {it->first}, ir::IRType::Void(), "");
  }
  lc.builder.MakeBranch(exit_block);

  lc.SetInsertBlock(exit_block);
  lc.terminated = false;
  return true;
}

bool LowerTry(const std::shared_ptr<TryStatement> &try_stmt, LoweringContext &lc) {
  if (lc.terminated)
    return true;

  lc.diags.ReportError(
      try_stmt->loc, frontends::ErrorCode::kUnsupportedLowering,
      "Python try/except/else/finally exception control flow is not implemented faithfully");
  return false;

  for (const auto &handler : try_stmt->handlers) {
    if (handler.is_exception_group) {
      lc.diags.ReportError(
          try_stmt->loc, frontends::ErrorCode::kUnsupportedLowering,
          "Python exception groups require except* splitting/merging runtime semantics");
      return false;
    }
  }

  auto *try_block = lc.fn->CreateBlock("try.body");
  auto *exit_block = lc.fn->CreateBlock("try.end");

  // Create handler blocks for each except clause
  std::vector<ir::BasicBlock *> handler_blocks;
  for (size_t i = 0; i < try_stmt->handlers.size(); ++i) {
    handler_blocks.push_back(lc.fn->CreateBlock("except." + std::to_string(i)));
  }

  auto *else_block = try_stmt->orelse.empty() ? nullptr : lc.fn->CreateBlock("try.else");
  auto *finally_block = try_stmt->finalbody.empty() ? nullptr : lc.fn->CreateBlock("try.finally");

  // Create landing pad block for exception dispatch
  auto *landing_pad = lc.fn->CreateBlock("try.landingpad");
  auto *unwind_block = lc.fn->CreateBlock("try.unwind");

  // Set up exception frame with proper landing pad registration
  // __py_push_exception_frame returns a context that includes:
  // - The landing pad address for setjmp/longjmp style unwinding
  // - Exception type information for RTTI-based dispatch
  auto exc_frame =
      lc.builder.MakeCall("__py_push_exception_frame", {},
                          ir::IRType::Pointer(ir::IRType::I64(true)), lc.NextTemp("excframe"));

  // Check if we're entering fresh (0) or re-entering from exception (non-zero)
  auto setjmp_result = lc.builder.MakeCall("__py_setjmp", {exc_frame->name}, ir::IRType::I32(),
                                           lc.NextTemp("setjmp"));

  // If setjmp returns 0, enter try block; otherwise go to landing pad
  auto is_exception = lc.builder.MakeBinary(ir::BinaryInstruction::Op::kCmpNe, setjmp_result->name,
                                            "0", lc.NextTemp("is_exc"));
  is_exception->type = ir::IRType::I1();

  lc.builder.MakeCondBranch(is_exception->name, landing_pad, try_block);

  /** @} */

  /** @name Try block */
  /** @{ */
  lc.SetInsertBlock(try_block);
  lc.terminated = false;

  bool try_term = false;
  for (auto &s : try_stmt->body) {
    if (!LowerStmt(s, lc))
      return false;
    if (lc.terminated) {
      try_term = true;
      break;
    }
  }

  // Normal exit from try: pop exception frame and branch to else/finally/exit
  if (!try_term) {
    lc.builder.MakeCall("__py_pop_exception_frame", {}, ir::IRType::Void(), "");
    if (else_block) {
      lc.builder.MakeBranch(else_block);
    } else if (finally_block) {
      lc.builder.MakeBranch(finally_block);
    } else {
      lc.builder.MakeBranch(exit_block);
    }
  }

  /** @} */

  /** @name Landing pad block: dispatch to appropriate handler */
  /** @{ */
  lc.SetInsertBlock(landing_pad);
  lc.terminated = false;

  // Get the current exception object and type
  auto exc_obj = lc.builder.MakeCall(
      "__py_get_exception", {}, ir::IRType::Pointer(ir::IRType::I64(true)), lc.NextTemp("exc"));
  auto exc_type = lc.builder.MakeCall("__py_get_exception_type", {}, ir::IRType::I64(true),
                                      lc.NextTemp("exctype"));

  // Chain of type checks for each handler
  ir::BasicBlock *current_check_block = landing_pad;
  for (size_t i = 0; i < try_stmt->handlers.size(); ++i) {
    const auto &handler = try_stmt->handlers[i];

    ir::BasicBlock *next_check = (i + 1 < try_stmt->handlers.size())
                                     ? lc.fn->CreateBlock("except.check." + std::to_string(i + 1))
                                     : unwind_block;

    if (i > 0) {
      lc.SetInsertBlock(current_check_block);
    }

    if (handler.type) {
      // Typed handler: check if exception matches this type
      auto handler_type = EvalExpr(handler.type, lc);
      if (!handler_type.IsValid())
        return false;

      auto type_match =
          lc.builder.MakeCall("__py_exception_isinstance", {exc_obj->name, handler_type.value},
                              ir::IRType::I1(), lc.NextTemp("typematch"));
      lc.builder.MakeCondBranch(type_match->name, handler_blocks[i], next_check);
    } else {
      // Bare except: catches all exceptions
      lc.builder.MakeBranch(handler_blocks[i]);
    }

    current_check_block = next_check;
  }

  /** @} */

  /** @name Unwind block: re-raise if no handler matched */
  /** @{ */
  lc.SetInsertBlock(unwind_block);
  lc.terminated = false;

  // Pop frame and re-raise the exception
  lc.builder.MakeCall("__py_pop_exception_frame", {}, ir::IRType::Void(), "");
  if (finally_block) {
    // Must run finally before re-raising
    // Store that we need to re-raise after finally
    lc.builder.MakeCall("__py_set_reraise_flag", {}, ir::IRType::Void(), "");
    lc.builder.MakeBranch(finally_block);
  } else {
    lc.builder.MakeCall("__py_reraise", {}, ir::IRType::Void(), "");
    lc.builder.MakeUnreachable();
    lc.terminated = true;
  }

  /** @} */

  /** @name Exception handlers */
  /** @{ */
  for (size_t i = 0; i < try_stmt->handlers.size(); ++i) {
    lc.SetInsertBlock(handler_blocks[i]);
    lc.terminated = false;

    const auto &handler = try_stmt->handlers[i];

    // Clear the exception (it's been handled)
    lc.builder.MakeCall("__py_clear_exception", {}, ir::IRType::Void(), "");
    lc.builder.MakeCall("__py_pop_exception_frame", {}, ir::IRType::Void(), "");

    // Bind exception to variable if named
    if (!handler.name.empty()) {
      lc.env[handler.name] = {exc_obj->name, exc_obj->type, "", false};
    }

    bool handler_term = false;
    for (auto &s : handler.body) {
      if (!LowerStmt(s, lc))
        return false;
      if (lc.terminated) {
        handler_term = true;
        break;
      }
    }

    if (!handler_term) {
      if (finally_block) {
        lc.builder.MakeBranch(finally_block);
      } else {
        lc.builder.MakeBranch(exit_block);
      }
    }
  }

  /** @} */

  /** @name Else block: executed if no exception occurred */
  /** @{ */
  if (else_block) {
    lc.SetInsertBlock(else_block);
    lc.terminated = false;
    for (auto &s : try_stmt->orelse) {
      if (!LowerStmt(s, lc))
        return false;
      if (lc.terminated)
        break;
    }
    if (!lc.terminated) {
      if (finally_block) {
        lc.builder.MakeBranch(finally_block);
      } else {
        lc.builder.MakeBranch(exit_block);
      }
    }
  }

  /** @} */

  /** @name Finally block: always executed */
  /** @{ */
  if (finally_block) {
    lc.SetInsertBlock(finally_block);
    lc.terminated = false;
    for (auto &s : try_stmt->finalbody) {
      if (!LowerStmt(s, lc))
        return false;
      if (lc.terminated)
        break;
    }
    if (!lc.terminated) {
      // Check if we need to re-raise after finally
      auto needs_reraise = lc.builder.MakeCall("__py_check_reraise_flag", {}, ir::IRType::I1(),
                                               lc.NextTemp("reraise"));

      auto *reraise_block = lc.fn->CreateBlock("finally.reraise");
      lc.builder.MakeCondBranch(needs_reraise->name, reraise_block, exit_block);

      lc.SetInsertBlock(reraise_block);
      lc.builder.MakeCall("__py_clear_reraise_flag", {}, ir::IRType::Void(), "");
      lc.builder.MakeCall("__py_reraise", {}, ir::IRType::Void(), "");
      lc.builder.MakeUnreachable();
    }
  }

  lc.SetInsertBlock(exit_block);
  lc.terminated = false;
  return true;
}

bool LowerMatch(const std::shared_ptr<MatchStatement> &match_stmt, LoweringContext &lc) {
  if (lc.terminated)
    return true;

  // The compact runtime API cannot currently carry the pattern tree or bind
  // captures. Calling a pattern matcher with only the subject would silently
  // turn every case into the same test, so fail closed until that ABI exists.
  lc.diags.ReportError(match_stmt->loc, frontends::ErrorCode::kUnsupportedLowering,
                       "Python structural pattern matching requires pattern-aware runtime "
                       "lowering and capture binding");
  return false;
}

bool LowerRaise(const std::shared_ptr<RaiseStatement> &raise, LoweringContext &lc) {
  if (lc.terminated)
    return true;

  std::vector<std::string> args;
  if (raise->value) {
    auto exc = EvalExpr(raise->value, lc);
    if (!exc.IsValid())
      return false;
    args.push_back(exc.value);
  }
  if (raise->from_expr) {
    auto from = EvalExpr(raise->from_expr, lc);
    if (!from.IsValid())
      return false;
    args.push_back(from.value);
  }

  lc.builder.MakeCall("__py_raise", args, ir::IRType::Void(), "");
  lc.builder.MakeUnreachable();
  lc.terminated = true;
  return true;
}

bool LowerAssert(const std::shared_ptr<AssertStatement> &assert_stmt, LoweringContext &lc) {
  if (lc.terminated)
    return true;

  auto cond = EvalExpr(assert_stmt->test, lc);
  if (!cond.IsValid())
    return false;
  if (cond.type.kind != ir::IRTypeKind::kI1) {
    lc.diags.ReportError(assert_stmt->test->loc,
                         frontends::ErrorCode::kUnsupportedLowering,
                         "Python assert truthiness requires dynamic runtime semantics");
    return false;
  }

  auto *fail_block = lc.fn->CreateBlock("assert.fail");
  auto *pass_block = lc.fn->CreateBlock("assert.pass");

  lc.builder.MakeCondBranch(cond.value, pass_block, fail_block);

  lc.SetInsertBlock(fail_block);
  std::vector<std::string> args;
  if (assert_stmt->msg) {
    auto msg = EvalExpr(assert_stmt->msg, lc);
    if (msg.IsValid()) {
      args.push_back(msg.value);
    }
  }
  lc.builder.MakeCall("__py_assert_fail", args, ir::IRType::Void(), "");
  lc.builder.MakeUnreachable();

  lc.SetInsertBlock(pass_block);
  lc.terminated = false;
  return true;
}

bool LowerBreak(LoweringContext &lc) {
  if (lc.terminated)
    return true;
  if (lc.loop_stack.empty()) {
    lc.diags.Report(core::SourceLoc{}, "'break' outside loop");
    return false;
  }
  lc.builder.MakeBranch(lc.loop_stack.top().break_target);
  lc.terminated = true;
  return true;
}

bool LowerContinue(LoweringContext &lc) {
  if (lc.terminated)
    return true;
  if (lc.loop_stack.empty()) {
    lc.diags.Report(core::SourceLoc{}, "'continue' outside loop");
    return false;
  }
  lc.builder.MakeBranch(lc.loop_stack.top().continue_target);
  lc.terminated = true;
  return true;
}

bool LowerPass(LoweringContext &lc) {
  // Pass is a no-op
  (void)lc;
  return true;
}

/** @} */

/** @name - */
/** @{ */
// Built-in module function table for static linking.
// Maps module name to a set of known function signatures.
/** @} */

/** @name - */
/** @{ */
struct BuiltinFunctionInfo {
  ir::IRType return_type;
  std::vector<ir::IRType> param_types;
  std::string runtime_name; // RT function to call
};

std::unordered_map<std::string, std::unordered_map<std::string, BuiltinFunctionInfo>>
GetBuiltinModuleFunctions() {
  return {
      {"math",
       {
           {"sin", {ir::IRType::F64(), {ir::IRType::F64()}, "__py_math_sin"}},
           {"cos", {ir::IRType::F64(), {ir::IRType::F64()}, "__py_math_cos"}},
           {"sqrt", {ir::IRType::F64(), {ir::IRType::F64()}, "__py_math_sqrt"}},
           {"floor", {ir::IRType::I64(true), {ir::IRType::F64()}, "__py_math_floor"}},
           {"ceil", {ir::IRType::I64(true), {ir::IRType::F64()}, "__py_math_ceil"}},
           {"pow", {ir::IRType::F64(), {ir::IRType::F64(), ir::IRType::F64()}, "__py_math_pow"}},
       }},
      {"os",
       {
           {"getcwd", {ir::IRType::Pointer(ir::IRType::I8()), {}, "__py_os_getcwd"}},
       }},
      {"sys",
       {
           {"exit", {ir::IRType::Void(), {ir::IRType::I64(true)}, "__py_sys_exit"}},
       }},
      {"json",
       {
           {"dumps",
            {ir::IRType::Pointer(ir::IRType::I8()), {ir::IRType::I64(true)}, "__py_json_dumps"}},
           {"loads",
            {ir::IRType::I64(true), {ir::IRType::Pointer(ir::IRType::I8())}, "__py_json_loads"}},
       }},
  };
}

// Check if module/function is built-in and can be statically linked
bool IsBuiltinFunction(const std::string &module, const std::string &func) {
  auto modules = GetBuiltinModuleFunctions();
  if (auto it = modules.find(module); it != modules.end()) {
    return it->second.count(func) > 0;
  }
  return false;
}

// Get runtime function name for built-in
std::optional<std::string> GetBuiltinRuntimeName(const std::string &module,
                                                 const std::string &func) {
  auto modules = GetBuiltinModuleFunctions();
  if (auto it = modules.find(module); it != modules.end()) {
    if (auto it2 = it->second.find(func); it2 != it->second.end()) {
      return it2->second.runtime_name;
    }
  }
  return std::nullopt;
}

bool LowerImport(const std::shared_ptr<ImportStatement> &import_stmt, LoweringContext &lc) {
  // Generate runtime import calls and create global module handles
  auto builtin_modules = GetBuiltinModuleFunctions();

  if (import_stmt->is_from) {
    // from module import names
    std::string modname = import_stmt->module;
    bool is_builtin = builtin_modules.count(modname) > 0;

    for (const auto &alias : import_stmt->names) {
      std::string export_name = alias.name;
      std::string bind_name = alias.alias.empty() ? alias.name : alias.alias;

      if (is_builtin && IsBuiltinFunction(modname, export_name)) {
        // Static linking: create global reference to built-in function
        auto rt_name = GetBuiltinRuntimeName(modname, export_name);
        if (!rt_name) {
          lc.diags.ReportError(import_stmt->loc, frontends::ErrorCode::kUnsupportedLowering,
                               "Python built-in import has no registered runtime signature");
          return false;
        }

        // Create a global function pointer
        auto global = lc.ir_ctx.CreateGlobal("@__mod_" + modname + "_" + export_name,
                                             ir::IRType::Pointer(ir::IRType::I8()), true, *rt_name);

        lc.env[bind_name] = {global->name, global->type, "", false};
      } else {
        // Dynamic import: generate runtime call
        // Create string constant for module and name
        auto mod_str =
            lc.ir_ctx.CreateGlobal(".str.mod." + modname, ir::IRType::Pointer(ir::IRType::I8()),
                                   true, "\"" + modname + "\"");

        auto name_str = lc.ir_ctx.CreateGlobal(".str.name." + export_name,
                                               ir::IRType::Pointer(ir::IRType::I8()), true,
                                               "\"" + export_name + "\"");

        // Call __py_import_from(module_name, symbol_name) -> PyObject*
        auto result = lc.builder.MakeCall("__py_import_from", {mod_str->name, name_str->name},
                                          ir::IRType::I64(true), lc.NextTemp("import"));

        lc.env[bind_name] = {result->name, result->type, "", false};
      }
    }
  } else {
    // import module [as alias]
    for (const auto &alias : import_stmt->names) {
      std::string modname = alias.name;
      std::string bind_name = alias.alias.empty() ? alias.name : alias.alias;

      bool is_builtin = builtin_modules.count(modname) > 0;

      // Create global module handle
      std::string global_name = "@__module_" + modname;

      if (is_builtin) {
        // Built-in module: create a module descriptor global
        auto global = lc.ir_ctx.CreateGlobal(global_name, ir::IRType::I64(true), true,
                                             "/* builtin:" + modname + " */");

        lc.env[bind_name] = {global->name, global->type, "", false};
      } else {
        // Dynamic module: generate runtime import
        auto mod_str =
            lc.ir_ctx.CreateGlobal(".str.mod." + modname, ir::IRType::Pointer(ir::IRType::I8()),
                                   true, "\"" + modname + "\"");

        // Call __py_import(module_name) -> PyModuleHandle
        auto result = lc.builder.MakeCall("__py_import", {mod_str->name}, ir::IRType::I64(true),
                                          lc.NextTemp("module"));

        // Store in global for later access
        auto global = lc.ir_ctx.CreateGlobal(global_name, ir::IRType::I64(true), false, "0");

        // Store the imported module handle
        lc.builder.MakeStore(result->name, global->name);

        lc.env[bind_name] = {global->name, global->type, "", false};
      }
    }
  }
  return true;
}

bool LowerGlobal(const std::shared_ptr<GlobalStatement> &global_stmt, LoweringContext &lc) {
  lc.diags.ReportError(
      global_stmt->loc, frontends::ErrorCode::kUnsupportedLowering,
      "Python global rebinding requires a shared module-object environment and initialization order");
  return false;
}

bool LowerNonlocal(const std::shared_ptr<NonlocalStatement> &nonlocal_stmt, LoweringContext &lc) {
  lc.diags.ReportError(nonlocal_stmt->loc, frontends::ErrorCode::kUnsupportedLowering,
                       "Python nonlocal rebinding requires closure-cell lowering");
  return false;
}

/** @} */

/** @name - */
/** @{ */
// Main statement lowering dispatcher.
/** @} */

/** @name - */
/** @{ */
bool LowerStmt(const std::shared_ptr<Statement> &stmt, LoweringContext &lc) {
  if (!stmt || lc.terminated)
    return true;

  if (auto ret = std::dynamic_pointer_cast<ReturnStatement>(stmt)) {
    return LowerReturn(ret, lc);
  }
  if (auto assign = std::dynamic_pointer_cast<Assignment>(stmt)) {
    return LowerAssign(assign, lc);
  }
  if (auto if_stmt = std::dynamic_pointer_cast<IfStatement>(stmt)) {
    return LowerIf(if_stmt, lc);
  }
  if (auto while_stmt = std::dynamic_pointer_cast<WhileStatement>(stmt)) {
    return LowerWhile(while_stmt, lc);
  }
  if (auto for_stmt = std::dynamic_pointer_cast<ForStatement>(stmt)) {
    return LowerFor(for_stmt, lc);
  }
  if (auto with_stmt = std::dynamic_pointer_cast<WithStatement>(stmt)) {
    return LowerWith(with_stmt, lc);
  }
  if (auto try_stmt = std::dynamic_pointer_cast<TryStatement>(stmt)) {
    return LowerTry(try_stmt, lc);
  }
  if (auto match_stmt = std::dynamic_pointer_cast<MatchStatement>(stmt)) {
    return LowerMatch(match_stmt, lc);
  }
  if (auto raise_stmt = std::dynamic_pointer_cast<RaiseStatement>(stmt)) {
    return LowerRaise(raise_stmt, lc);
  }
  if (auto assert_stmt = std::dynamic_pointer_cast<AssertStatement>(stmt)) {
    return LowerAssert(assert_stmt, lc);
  }
  if (std::dynamic_pointer_cast<BreakStatement>(stmt)) {
    return LowerBreak(lc);
  }
  if (std::dynamic_pointer_cast<ContinueStatement>(stmt)) {
    return LowerContinue(lc);
  }
  if (std::dynamic_pointer_cast<PassStatement>(stmt)) {
    return LowerPass(lc);
  }
  if (auto import_stmt = std::dynamic_pointer_cast<ImportStatement>(stmt)) {
    return LowerImport(import_stmt, lc);
  }
  if (auto global_stmt = std::dynamic_pointer_cast<GlobalStatement>(stmt)) {
    return LowerGlobal(global_stmt, lc);
  }
  if (auto nonlocal_stmt = std::dynamic_pointer_cast<NonlocalStatement>(stmt)) {
    return LowerNonlocal(nonlocal_stmt, lc);
  }
  if (std::dynamic_pointer_cast<TypeAlias>(stmt)) {
    // Type aliases are compile-time-only declarations.
    return true;
  }
  if (auto expr_stmt = std::dynamic_pointer_cast<ExprStatement>(stmt)) {
    (void)EvalExpr(expr_stmt->expr, lc);
    return true;
  }
  // Nested function/class definitions that appear inside a block scope
  // are lowered in-place as local declarations.  Forward declarations are
  // provided so the functions can be referenced before their definition.
  if (auto fn_def = std::dynamic_pointer_cast<FunctionDef>(stmt)) {
    // Lower the nested function as a standalone IR function.
    // This makes it callable from subsequent statements in the same scope.
    return LowerFunction(*fn_def, lc);
  }
  if (auto cls_def = std::dynamic_pointer_cast<ClassDef>(stmt)) {
    // Lower the nested class declaration so its methods are available.
    return LowerClass(*cls_def, lc);
  }

  lc.diags.ReportError(stmt->loc, frontends::ErrorCode::kUnsupportedLowering,
                       "Python statement is parsed but has no faithful IR lowering");
  return false;
}

/** @} */

/** @name - */
/** @{ */
// Function lowering.
// Handles parameters with default values using PHI nodes for proper SSA.
/** @} */

/** @name - */
/** @{ */
bool LowerFunction(const FunctionDef &fn, LoweringContext &lc) {
  if (fn.is_async) {
    lc.diags.ReportError(fn.loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Python async functions/coroutines have no faithful IR lowering");
    return false;
  }
  if (!fn.type_parameters.empty()) {
    lc.diags.ReportError(
        fn.loc, frontends::ErrorCode::kUnsupportedLowering,
        "Python generic functions require type-parameter specialization and runtime semantics");
    return false;
  }
  for (const auto &arg : fn.params) {
    if (arg.default_value) {
      lc.diags.ReportError(
          arg.default_value->loc, frontends::ErrorCode::kUnsupportedLowering,
          "Python default arguments require definition-time evaluation and call binding");
      return false;
    }
  }

  const bool nested_function = lc.function_depth != 0;
  const auto saved_fn = lc.fn;
  const auto saved_insert_block = lc.builder.GetInsertPoint();
  const auto saved_env = lc.env;
  const auto saved_loop_stack = lc.loop_stack;
  const bool saved_terminated = lc.terminated;

  // Determine return type from type hints.  Lifecycle hooks use a void ABI
  // even when conventional Python source omits `-> None`.
  ir::IRType ret_ty = DeclaredReturnType(fn);
  if (ret_ty.kind == ir::IRTypeKind::kInvalid) {
    std::string annotation = "composite";
    if (auto id = std::dynamic_pointer_cast<Identifier>(fn.return_annotation))
      annotation = "'" + id->name + "'";
    lc.diags.ReportError(fn.return_annotation ? fn.return_annotation->loc : fn.loc,
                         frontends::ErrorCode::kUnsupportedLowering,
                         "Python return annotation " + annotation +
                             " has no modeled IR ABI");
    return false;
  }
  if ((fn.name == "__init__" || fn.name == "__del__") && fn.return_annotation) {
    auto id = std::dynamic_pointer_cast<Identifier>(fn.return_annotation);
    auto literal = std::dynamic_pointer_cast<Literal>(fn.return_annotation);
    if ((!id || id->name != "None") && (!literal || literal->value != "None")) {
      lc.diags.ReportError(fn.return_annotation->loc,
                           frontends::ErrorCode::kUnsupportedLowering,
                           "Python lifecycle hook '" + fn.name +
                               "' must return None in the static object ABI");
      return false;
    }
  }

  // Build parameter list
  std::vector<std::pair<std::string, ir::IRType>> params;
  params.reserve(fn.params.size());
  for (size_t parameter_index = 0; parameter_index < fn.params.size(); ++parameter_index) {
    const auto &arg = fn.params[parameter_index];
    ir::IRType param_ty;
    if (!lc.current_class.empty() && parameter_index == 0) {
      auto class_it = lc.classes.find(lc.current_class);
      if (arg.name != "self" || class_it == lc.classes.end()) {
        lc.diags.ReportError(fn.loc, frontends::ErrorCode::kUnsupportedLowering,
                             "Python static instance methods require 'self' as their first "
                             "parameter");
        return false;
      }
      param_ty = ir::IRType::Pointer(class_it->second.struct_type);
    } else {
      param_ty = DeclaredParameterType(arg);
      if (param_ty.kind == ir::IRTypeKind::kInvalid) {
        std::string annotation = "composite";
        if (auto id = std::dynamic_pointer_cast<Identifier>(arg.annotation))
          annotation = "'" + id->name + "'";
        lc.diags.ReportError(arg.annotation ? arg.annotation->loc : fn.loc,
                             frontends::ErrorCode::kUnsupportedLowering,
                             "Python parameter annotation " + annotation +
                                 " has no modeled IR ABI");
        return false;
      }
    }
    params.push_back({arg.name, param_ty});
  }

  // Handle async functions
  bool was_async = lc.in_async_function;
  lc.in_async_function = fn.is_async;
  ++lc.function_depth;

  const auto finish = [&](bool result) {
    lc.in_async_function = was_async;
    --lc.function_depth;
    if (nested_function) {
      lc.fn = saved_fn;
      lc.env = saved_env;
      lc.loop_stack = saved_loop_stack;
      lc.terminated = saved_terminated;
      lc.builder.SetCurrentFunction(saved_fn);
      lc.builder.SetInsertPoint(saved_insert_block);
    }
    return result;
  };

  // Mangle function name for methods
  std::string func_name = fn.name;
  if (!lc.current_class.empty()) {
    func_name = lc.current_class + "." + fn.name;
  }

  lc.fn = lc.ir_ctx.CreateFunction(func_name, ret_ty, params);
  lc.builder.SetCurrentFunction(lc.fn);
  auto *entry = lc.fn->CreateBlock("entry");
  lc.fn->entry = entry;
  lc.SetInsertBlock(entry);

  lc.env.clear();
  lc.loop_stack = std::stack<LoopContext>{};
  for (const auto &p : params) {
    lc.env[p.first] = {p.first, p.second, "", false};
  }

  lc.terminated = false;

  // Lower function body
  for (const auto &stmt : fn.body) {
    if (!LowerStmt(stmt, lc)) {
      return finish(false);
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
                           "Python non-None function may fall through without a return value");
      lc.builder.MakeUnreachable();
      return finish(false);
    }
  }

  return finish(true);
}

/** @} */

/** @name - */
/** @{ */
// Class lowering.
/** @} */

/** @name - */
/** @{ */
bool RegisterClassInfo(const ClassDef &cls, LoweringContext &lc) {
  if (!cls.bases.empty()) {
    lc.diags.ReportError(
        cls.loc, frontends::ErrorCode::kUnsupportedLowering,
        "Python inheritance is outside the deterministic static object subset; class '" +
            cls.name + "' must not declare base classes");
    return false;
  }
  if (!cls.keywords.empty()) {
    lc.diags.ReportError(
        cls.loc, frontends::ErrorCode::kUnsupportedLowering,
        "Python metaclass/class keyword arguments require the dynamic object runtime");
    return false;
  }
  if (!cls.type_parameters.empty()) {
    lc.diags.ReportError(cls.loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Python generic classes require runtime type-parameter semantics");
    return false;
  }
  if (!cls.decorators.empty()) {
    lc.diags.ReportError(
        cls.decorators.front()->loc, frontends::ErrorCode::kUnsupportedLowering,
        "Python class decorators (including dataclass transforms) are not descriptors in the "
        "static object subset");
    return false;
  }

  ClassInfo info;
  info.name = cls.name;
  const FunctionDef *initializer = nullptr;

  for (const auto &stmt : cls.body) {
    if (auto method = std::dynamic_pointer_cast<FunctionDef>(stmt)) {
      if (method->is_async) {
        lc.diags.ReportError(method->loc, frontends::ErrorCode::kUnsupportedLowering,
                             "Python async methods require coroutine object semantics");
        return false;
      }
      if (!method->decorators.empty()) {
        lc.diags.ReportError(
            method->decorators.front()->loc, frontends::ErrorCode::kUnsupportedLowering,
            "Python method decorators/properties require descriptor binding and are not "
            "supported by static instance dispatch");
        return false;
      }
      if (method->params.empty() || method->params.front().name != "self") {
        lc.diags.ReportError(method->loc, frontends::ErrorCode::kUnsupportedLowering,
                             "Python static instance method '" + cls.name + "." +
                                 method->name + "' must declare self first");
        return false;
      }
      for (const auto &parameter : method->params) {
        if (parameter.is_vararg || parameter.is_kwarg || parameter.is_kwonly ||
            parameter.default_value) {
          lc.diags.ReportError(
              method->loc, frontends::ErrorCode::kUnsupportedLowering,
              "Python static methods do not support defaults, keyword-only, *args, or **kwargs "
              "binding");
          return false;
        }
      }

      ClassInfo::MethodInfo method_info;
      method_info.lowered_name = cls.name + "." + method->name;
      method_info.return_type = DeclaredReturnType(*method);
      if (method_info.return_type.kind == ir::IRTypeKind::kInvalid) {
        lc.diags.ReportError(method->loc, frontends::ErrorCode::kUnsupportedLowering,
                             "Python method return annotation has no modeled static ABI");
        return false;
      }
      for (size_t i = 1; i < method->params.size(); ++i) {
        auto parameter_type = DeclaredParameterType(method->params[i]);
        if (parameter_type.kind == ir::IRTypeKind::kInvalid) {
          lc.diags.ReportError(method->params[i].annotation
                                   ? method->params[i].annotation->loc
                                   : method->loc,
                               frontends::ErrorCode::kUnsupportedLowering,
                               "Python method parameter annotation has no modeled static ABI");
          return false;
        }
        method_info.parameter_types.push_back(parameter_type);
      }
      if (!info.method_info.emplace(method->name, std::move(method_info)).second) {
        lc.diags.ReportError(method->loc, frontends::ErrorCode::kUnsupportedLowering,
                             "Python method replacement/overloading is dynamic and unsupported: " +
                                 cls.name + "." + method->name);
        return false;
      }
      info.methods.push_back(method->name);
      if (method->name == "__init__")
        initializer = method.get();
      continue;
    }
    if (std::dynamic_pointer_cast<PassStatement>(stmt))
      continue;

    lc.diags.ReportError(
        stmt->loc, frontends::ErrorCode::kUnsupportedLowering,
        "Python class variables, nested declarations, and descriptors require the dynamic "
        "class object runtime; only instance methods are supported");
    return false;
  }

  if (!initializer) {
    lc.diags.ReportError(cls.loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Python static class '" + cls.name +
                             "' requires an explicit __init__ constructor");
    return false;
  }

  // A field enters the layout only through an unconditional top-level
  // self.<field> assignment in __init__.  Source order defines the stable
  // aggregate order; every field has the current integer ABI (i64).
  for (const auto &stmt : initializer->body) {
    auto assignment = std::dynamic_pointer_cast<Assignment>(stmt);
    if (!assignment)
      continue;
    for (const auto &target : assignment->targets) {
      std::string field;
      if (!IsSelfAttribute(target, &field))
        continue;
      if (assignment->op != "=") {
        lc.diags.ReportError(
            assignment->loc, frontends::ErrorCode::kUnsupportedLowering,
            "Python constructor fields must be initialized with plain '=' before mutation");
        return false;
      }
      if (assignment->annotation) {
        auto annotation = std::dynamic_pointer_cast<Identifier>(assignment->annotation);
        if (!annotation || annotation->name != "int") {
          lc.diags.ReportError(
              assignment->annotation->loc, frontends::ErrorCode::kUnsupportedLowering,
              "Python static class fields currently require the int annotation/ABI");
          return false;
        }
      }
      if (info.field_indices.count(field) == 0) {
        const size_t index = info.fields.size();
        info.field_indices.emplace(field, index);
        info.fields.push_back(field);
      }
    }
  }
  if (info.fields.empty()) {
    lc.diags.ReportError(
        initializer->loc, frontends::ErrorCode::kUnsupportedLowering,
        "Python static __init__ must declare at least one integer field with self.<field> = value");
    return false;
  }

  std::vector<ir::IRType> fields(info.fields.size(), ir::IRType::I64(true));
  info.struct_type = ir::IRType::Struct(cls.name, std::move(fields));
  for (const auto &[method_name, method] : info.method_info)
    lc.function_returns[cls.name + "." + method_name] = method.return_type;
  lc.classes[cls.name] = std::move(info);
  return true;
}

bool LowerClass(const ClassDef &cls, LoweringContext &lc) {
  auto class_it = lc.classes.find(cls.name);
  if (class_it == lc.classes.end() ||
      class_it->second.struct_type.kind != ir::IRTypeKind::kStruct) {
    lc.diags.ReportError(cls.loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Python class has no registered deterministic aggregate layout: " +
                             cls.name);
    return false;
  }

  // Save current class context
  std::string saved_class = lc.current_class;
  lc.current_class = cls.name;

  for (const auto &stmt : cls.body) {
    if (auto method = std::dynamic_pointer_cast<FunctionDef>(stmt)) {
      if (!LowerFunction(*method, lc)) {
        lc.current_class = saved_class;
        return false;
      }
    }
  }

  lc.current_class = saved_class;
  return true;
}

/** @} */

/** @name - */
/** @{ */
// Decorator handling.
/** @} */

/** @name - */
/** @{ */
bool ApplyDecorators(const std::vector<std::shared_ptr<Expression>> &decorators,
                     const std::string &func_name, LoweringContext &lc) {
  (void)func_name;
  if (!decorators.empty()) {
    lc.diags.ReportError(decorators.front()->loc, frontends::ErrorCode::kUnsupportedLowering,
                         "Python decorator evaluation and rebinding are not implemented faithfully");
    return false;
  }
  return true;
}

} // namespace

/** @} */

/** @name - */
/** @{ */
// Public API: Lower Python module to IR.
/** @} */

/** @name - */
/** @{ */
void LowerToIR(const Module &module, ir::IRContext &ctx, frontends::Diagnostics &diags) {
  LoweringContext lc(ctx, diags);
  const size_t errors_before_declaration_scan = diags.ErrorCount();

  // First pass: collect class and function declarations/signatures so calls
  // never have to invent a placeholder return ABI.
  for (const auto &stmt : module.body) {
    if (auto cls = std::dynamic_pointer_cast<ClassDef>(stmt)) {
      if (!RegisterClassInfo(*cls, lc))
        continue;
    } else if (auto fn = std::dynamic_pointer_cast<FunctionDef>(stmt)) {
      ir::IRType return_type = DeclaredReturnType(*fn);
      if (return_type.kind != ir::IRTypeKind::kInvalid)
        lc.function_returns[fn->name] = return_type;
    }
  }
  if (diags.ErrorCount() != errors_before_declaration_scan)
    return;

  // Second pass: lower all top-level definitions
  for (const auto &stmt : module.body) {
    if (auto fn = std::dynamic_pointer_cast<FunctionDef>(stmt)) {
      (void)LowerFunction(*fn, lc);
      // Apply decorators
      if (!fn->decorators.empty()) {
        ApplyDecorators(fn->decorators, fn->name, lc);
      }
    } else if (auto cls = std::dynamic_pointer_cast<ClassDef>(stmt)) {
      (void)LowerClass(*cls, lc);
      // Apply class decorators
      if (!cls->decorators.empty()) {
        ApplyDecorators(cls->decorators, cls->name, lc);
      }
    } else if (auto assign = std::dynamic_pointer_cast<Assignment>(stmt)) {
      diags.ReportError(
          assign->loc, frontends::ErrorCode::kUnsupportedLowering,
          "Python module-level assignment requires persistent global storage and init ordering");
    } else if (auto import_stmt = std::dynamic_pointer_cast<ImportStatement>(stmt)) {
      // Module-level import
      if (!lc.fn) {
        lc.fn = ctx.CreateFunction("__module_init__", ir::IRType::Void(), {});
        auto *entry = lc.fn->CreateBlock("entry");
        lc.fn->entry = entry;
        lc.SetInsertBlock(entry);
        lc.terminated = false;
      }
      LowerImport(import_stmt, lc);
    } else if (!std::dynamic_pointer_cast<TypeAlias>(stmt)) {
      diags.ReportError(stmt->loc, frontends::ErrorCode::kUnsupportedLowering,
                        "Python top-level statement has no faithful module-initializer lowering");
    }
  }

  // Finalize module init function if created
  if (lc.fn && lc.fn->name == "__module_init__" && !lc.terminated) {
    lc.builder.MakeReturn("");
  }
}

} // namespace polyglot::python

/** @} */
