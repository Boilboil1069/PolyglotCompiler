/**
 * @file     lowering.cpp
 * @brief    Go → Polyglot IR lowering
 *
 * @ingroup  Frontend / Go
 * @author   Manning Cyrus
 * @date     2026-04-26
 *
 * Lowers the statically typed numeric/boolean subset of Go to IR:
 *   - Top-level `func` declarations with declared parameter and return
 *     types. Multiple-result functions are rejected until tuple ABI lowering
 *     is available.
 *   - Local `var` / `:=` declarations of basic types and named structs whose
 *     fields have deterministic scalar layouts.
 *   - Stack-resident structs, field selectors, pointer-receiver methods, and
 *     constructor-style functions that initialize an object through `*T`.
 *   - if / for (three-clause and condition-only) / return / assignment
 *     and the standard arithmetic/comparison/logical operators.
 *
 * Anything outside that subset is rejected with kUnsupportedLowering.  The
 * lowering must never replace modern Go semantics with placeholder zeroes,
 * truncated tuples, eager short-circuit operators, or fabricated calls.
 */
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "middle/include/ir/ir_builder.h"

#include "frontends/go/include/go_lowering.h"

namespace polyglot::go {

namespace {

struct StructFieldLayout {
  std::string name;
  ir::IRType type{ir::IRType::Invalid()};
  size_t index{0};
};

struct StructLayout {
  std::string name;
  ir::IRType type{ir::IRType::Invalid()};
  std::vector<StructFieldLayout> fields;
  std::unordered_map<std::string, size_t> field_indices;
};

using StructLayouts = std::unordered_map<std::string, StructLayout>;

ir::IRType ToIRType(const std::shared_ptr<TypeNode> &t, const StructLayouts &layouts,
                    frontends::Diagnostics &diags, const core::SourceLoc &fallback_loc) {
  if (!t) {
    diags.ReportError(fallback_loc, frontends::ErrorCode::kUnsupportedLowering,
                      "Go lowering requires an explicit or inferred type");
    return ir::IRType::Invalid();
  }
  if (t->kind == TypeKind::kPointer) {
    auto pointee = ToIRType(t->elem, layouts, diags, fallback_loc);
    if (pointee.kind == ir::IRTypeKind::kInvalid)
      return pointee;
    return ir::IRType::Pointer(pointee);
  }
  if (t->kind != TypeKind::kNamed || !t->type_args.empty()) {
    diags.ReportError(t->loc, frontends::ErrorCode::kUnsupportedLowering,
                      "Go anonymous composite/generic runtime type is outside deterministic IR "
                      "lowering");
    return ir::IRType::Invalid();
  }
  const std::string &n = t->name;
  if (auto layout = layouts.find(n); layout != layouts.end())
    return layout->second.type;
  if (n == "bool")
    return ir::IRType::I1();
  if (n == "int8")
    return ir::IRType::I8(true);
  if (n == "byte" || n == "uint8")
    return ir::IRType::I8(false);
  if (n == "int16")
    return ir::IRType::I16(true);
  if (n == "uint16")
    return ir::IRType::I16(false);
  if (n == "int32" || n == "rune")
    return ir::IRType::I32(true);
  if (n == "uint32")
    return ir::IRType::I32(false);
  if (n == "int" || n == "int64")
    return ir::IRType::I64(true);
  if (n == "uint" || n == "uint64" || n == "uintptr")
    return ir::IRType::I64(false);
  if (n == "float32")
    return ir::IRType::F32();
  if (n == "float64")
    return ir::IRType::F64();
  diags.ReportError(t->loc, frontends::ErrorCode::kUnsupportedLowering,
                    "Go named/reference type has no deterministic IR layout: " + n);
  return ir::IRType::Invalid();
}

class Lowerer {
public:
  Lowerer(const File &f, ir::IRContext &c, frontends::Diagnostics &d) :
      file_(f), ctx_(c), builder_(c), diag_(d) {}

  void Run() {
    BuildStructLayouts();
    for (const auto &decl : file_.decls) {
      if (decl.keyword == "var" || decl.keyword == "const") {
        diag_.ReportError(decl.loc, frontends::ErrorCode::kUnsupportedLowering,
                          "Go package-level value declarations are not lowered");
      } else if (decl.keyword != "import" && decl.keyword != "type") {
        diag_.ReportError(decl.loc, frontends::ErrorCode::kUnsupportedLowering,
                          "unsupported Go top-level declaration");
      }
    }
    for (auto &fn : file_.funcs)
      LowerFunc(*fn);
  }

private:
  struct Local {
    std::string addr;
    ir::IRType type;
    bool is_object{false};
  };

  struct ObjectRef {
    std::string base;
    const StructLayout *layout{nullptr};
  };

  struct FieldRef {
    std::string address;
    ir::IRType type{ir::IRType::Invalid()};
  };

  std::string NextTemp(const char *prefix) {
    return std::string(prefix) + "." + std::to_string(next_temp_++);
  }

  void BuildStructLayouts() {
    for (const auto &decl : file_.decls) {
      if (decl.keyword != "type")
        continue;
      for (const auto &spec : decl.types) {
        if (!spec.type || spec.type->kind != TypeKind::kStruct)
          continue;
        if (spec.is_alias || !spec.type_params.empty()) {
          diag_.ReportError(spec.loc, frontends::ErrorCode::kUnsupportedLowering,
                            "Go generic/alias struct layouts require type instantiation");
          continue;
        }

        StructLayout layout;
        layout.name = spec.name;
        std::vector<ir::IRType> field_types;
        bool valid = true;
        for (const auto &field : spec.type->fields) {
          if (field.names.empty()) {
            diag_.ReportError(spec.loc, frontends::ErrorCode::kUnsupportedLowering,
                              "Go embedded fields require promoted-selector lowering");
            valid = false;
            continue;
          }
          auto field_type = ToIRType(field.type, layouts_, diag_, spec.loc);
          if (!field_type.IsInteger()) {
            diag_.ReportError(
                field.type ? field.type->loc : spec.loc,
                frontends::ErrorCode::kUnsupportedLowering,
                "Go executable struct layouts currently require integer fields");
            valid = false;
            continue;
          }
          for (const auto &field_name : field.names) {
            if (field_name == "_" || layout.field_indices.count(field_name) != 0) {
              diag_.ReportError(spec.loc, frontends::ErrorCode::kUnsupportedLowering,
                                "Go struct layout requires unique named fields: " + field_name);
              valid = false;
              continue;
            }
            const size_t index = field_types.size();
            layout.field_indices.emplace(field_name, index);
            layout.fields.push_back({field_name, field_type, index});
            field_types.push_back(field_type);
          }
        }
        if (!valid)
          continue;
        layout.type = ir::IRType::Struct("go." + spec.name, std::move(field_types));
        layouts_.emplace(spec.name, std::move(layout));
      }
    }
  }

  void LowerFunc(const FuncDecl &f) {
    if (!f.body)
      return;
    if (!f.type_params.empty()) {
      diag_.ReportError(
          f.loc, frontends::ErrorCode::kUnsupportedLowering,
          "Go generic-function lowering requires monomorphization and is not implemented");
      return;
    }
    if (f.is_variadic) {
      diag_.ReportError(f.loc, frontends::ErrorCode::kUnsupportedLowering,
                        "Go variadic calling-convention lowering is not implemented");
      return;
    }
    if (f.results.size() > 1) {
      diag_.ReportError(f.loc, frontends::ErrorCode::kUnsupportedLowering,
                        "Go multiple-result function lowering is not implemented");
      return;
    }
    next_temp_ = 0;
    std::vector<std::pair<std::string, ir::IRType>> ir_params;
    if (f.receiver) {
      auto receiver_type = ToIRType(f.receiver->type, layouts_, diag_, f.loc);
      if (receiver_type.kind == ir::IRTypeKind::kInvalid)
        return;
      if (receiver_type.kind != ir::IRTypeKind::kPointer ||
          receiver_type.subtypes.empty() ||
          receiver_type.subtypes.front().kind != ir::IRTypeKind::kStruct) {
        diag_.ReportError(
            f.receiver->loc, frontends::ErrorCode::kUnsupportedLowering,
            "Go executable methods currently require a pointer receiver to a named struct");
        return;
      }
      ir_params.emplace_back(f.receiver->name.empty() ? "self" : f.receiver->name,
                             receiver_type);
    }
    int unnamed_idx = 0;
    for (auto &p : f.params) {
      std::string n = p.first.empty() ? "_arg" + std::to_string(unnamed_idx++) : p.first;
      auto type = ToIRType(p.second, layouts_, diag_, f.loc);
      if (type.kind == ir::IRTypeKind::kInvalid)
        return;
      if (type.kind == ir::IRTypeKind::kStruct) {
        diag_.ReportError(p.second ? p.second->loc : f.loc,
                          frontends::ErrorCode::kUnsupportedLowering,
                          "Go by-value struct parameters require aggregate ABI lowering; use *T");
        return;
      }
      ir_params.emplace_back(n, type);
    }
    ir::IRType ret = ir::IRType::Void();
    if (f.results.size() == 1)
      ret = ToIRType(f.results.front().second, layouts_, diag_, f.loc);
    else if (f.results.size() > 1)
      ret = ToIRType(f.results.front().second, layouts_, diag_, f.loc);
    if (ret.kind == ir::IRTypeKind::kInvalid)
      return;
    if (ret.kind == ir::IRTypeKind::kStruct) {
      diag_.ReportError(f.loc, frontends::ErrorCode::kUnsupportedLowering,
                        "Go by-value struct returns require aggregate ABI lowering; initialize "
                        "a caller-owned *T instead");
      return;
    }
    if (!f.results.empty() && !f.results.front().first.empty()) {
      diag_.ReportError(f.loc, frontends::ErrorCode::kUnsupportedLowering,
                        "Go named-result/bare-return lowering is not implemented");
      return;
    }

    std::string name = f.name;
    if (f.receiver && f.receiver->type) {
      std::string recv = f.receiver->type->name;
      if (f.receiver->type->kind == TypeKind::kPointer && f.receiver->type->elem)
        recv = f.receiver->type->elem->name;
      if (!recv.empty())
        name = recv + "." + f.name;
    }
    if (!file_.package_name.empty() && file_.package_name != "main")
      name = file_.package_name + "." + name;

    auto fn = ctx_.CreateFunction(name, ret, ir_params);
    builder_.SetCurrentFunction(fn);
    auto entry = builder_.CreateBlock("entry");
    builder_.SetInsertPoint(entry);
    locals_.clear();
    for (auto &p : ir_params) {
      builder_.MakeAlloca(p.second, p.first + ".addr");
      builder_.MakeStore(p.first + ".addr", p.first);
      locals_[p.first] = {p.first + ".addr", p.second, false};
    }
    current_ret_ = ret;
    terminated_ = false;
    if (f.body)
      for (auto &s : f.body->stmts)
        LowerStmt(s);
    if (!terminated_) {
      if (ret.kind == ir::IRTypeKind::kVoid)
        builder_.MakeReturn();
      else {
        diag_.ReportError(f.loc, frontends::ErrorCode::kUnsupportedLowering,
                          "non-void Go function can reach the end without a lowered return");
        builder_.MakeReturn(builder_.MakeLiteral((long long)0)->name);
      }
    }
    builder_.ClearCurrentFunction();
  }

  const StructLayout *FindLayoutByIRType(const ir::IRType &type) const {
    const ir::IRType *candidate = &type;
    if ((type.kind == ir::IRTypeKind::kPointer ||
         type.kind == ir::IRTypeKind::kReference) &&
        !type.subtypes.empty()) {
      candidate = &type.subtypes.front();
    }
    if (candidate->kind != ir::IRTypeKind::kStruct)
      return nullptr;
    for (const auto &[name, layout] : layouts_) {
      (void)name;
      if (layout.type.SameShape(*candidate))
        return &layout;
    }
    return nullptr;
  }

  std::optional<ObjectRef> ResolveObject(const std::shared_ptr<Expression> &expr,
                                         bool report_error) {
    if (auto paren = std::dynamic_pointer_cast<ParenExpr>(expr))
      return ResolveObject(paren->inner, report_error);
    auto id = std::dynamic_pointer_cast<Identifier>(expr);
    if (!id) {
      if (report_error) {
        diag_.ReportError(expr ? expr->loc : core::SourceLoc{},
                          frontends::ErrorCode::kUnsupportedLowering,
                          "Go selector receiver must be a statically known local object");
      }
      return std::nullopt;
    }
    auto local = locals_.find(id->name);
    if (local == locals_.end()) {
      if (report_error) {
        diag_.ReportError(id->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "Go selector receiver is unresolved: " + id->name);
      }
      return std::nullopt;
    }
    const StructLayout *layout = FindLayoutByIRType(local->second.type);
    if (!layout) {
      if (report_error) {
        diag_.ReportError(id->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "Go selector receiver has no named struct layout: " + id->name);
      }
      return std::nullopt;
    }
    if (local->second.is_object)
      return ObjectRef{local->second.addr, layout};
    if (local->second.type.kind == ir::IRTypeKind::kPointer) {
      auto pointer = builder_.MakeLoad(local->second.addr, local->second.type,
                                       NextTemp("object.ptr"));
      return ObjectRef{pointer->name, layout};
    }
    if (report_error) {
      diag_.ReportError(id->loc, frontends::ErrorCode::kUnsupportedLowering,
                        "Go by-value selector receivers are not executable in this ABI slice");
    }
    return std::nullopt;
  }

  std::optional<FieldRef> ResolveField(const std::shared_ptr<SelectorExpr> &selector,
                                       bool report_error) {
    auto object = ResolveObject(selector ? selector->x : nullptr, report_error);
    if (!object || !object->layout)
      return std::nullopt;
    auto field = object->layout->field_indices.find(selector->sel);
    if (field == object->layout->field_indices.end()) {
      if (report_error) {
        diag_.ReportError(selector->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "unknown Go field selector '" + selector->sel + "' on " +
                              object->layout->name);
      }
      return std::nullopt;
    }
    const auto &field_layout = object->layout->fields.at(field->second);
    auto gep = builder_.MakeGEP(object->base, object->layout->type,
                                {0, field_layout.index}, NextTemp("field.addr"));
    return FieldRef{gep->name, field_layout.type};
  }

  void ZeroInitializeObject(const std::string &address, const StructLayout &layout) {
    for (const auto &field : layout.fields) {
      auto gep = builder_.MakeGEP(address, layout.type, {0, field.index},
                                  NextTemp("field.zero.addr"));
      builder_.MakeStore(gep->name, "0");
    }
  }

  bool InitializeComposite(const std::string &address, const StructLayout &layout,
                           const CompositeLit &literal) {
    if (!literal.type || literal.type->kind != TypeKind::kNamed ||
        literal.type->name != layout.name) {
      diag_.ReportError(literal.loc, frontends::ErrorCode::kUnsupportedLowering,
                        "Go composite literal type does not match local object layout");
      return false;
    }
    ZeroInitializeObject(address, layout);
    std::vector<bool> initialized(layout.fields.size(), false);
    size_t positional = 0;
    for (const auto &element : literal.elements) {
      size_t field_index = positional;
      if (element.key) {
        auto key = std::dynamic_pointer_cast<Identifier>(element.key);
        if (!key) {
          diag_.ReportError(element.key->loc, frontends::ErrorCode::kUnsupportedLowering,
                            "Go struct composite keys must be static field names");
          return false;
        }
        auto found = layout.field_indices.find(key->name);
        if (found == layout.field_indices.end()) {
          diag_.ReportError(key->loc, frontends::ErrorCode::kUnsupportedLowering,
                            "unknown Go composite field: " + key->name);
          return false;
        }
        field_index = found->second;
      } else {
        ++positional;
      }
      if (field_index >= layout.fields.size() || initialized[field_index]) {
        diag_.ReportError(literal.loc, frontends::ErrorCode::kUnsupportedLowering,
                          "Go struct composite has an invalid or duplicate field initializer");
        return false;
      }
      initialized[field_index] = true;
      const auto &field = layout.fields[field_index];
      auto value = LowerExpr(element.value, field.type);
      auto gep = builder_.MakeGEP(address, layout.type, {0, field.index},
                                  NextTemp("field.init.addr"));
      builder_.MakeStore(gep->name, value);
    }
    return true;
  }

  bool DeclareObject(const std::string &name, const StructLayout &layout,
                     const std::shared_ptr<Expression> &initializer) {
    auto storage = builder_.MakeAlloca(layout.type, name + ".addr");
    locals_[name] = {storage->name, layout.type, true};
    if (!initializer) {
      ZeroInitializeObject(storage->name, layout);
      return true;
    }
    auto literal = std::dynamic_pointer_cast<CompositeLit>(initializer);
    if (!literal) {
      diag_.ReportError(initializer->loc, frontends::ErrorCode::kUnsupportedLowering,
                        "Go named structs must be initialized by a matching composite literal "
                        "or a constructor-style *T function");
      return false;
    }
    return InitializeComposite(storage->name, layout, *literal);
  }

  void LowerStmt(const std::shared_ptr<Statement> &s) {
    if (!s || terminated_)
      return;
    if (auto b = std::dynamic_pointer_cast<Block>(s)) {
      for (auto &c : b->stmts) {
        if (terminated_)
          return;
        LowerStmt(c);
      }
      return;
    }
    if (auto e = std::dynamic_pointer_cast<ExprStmt>(s)) {
      LowerExpr(e->expr, ir::IRType::I64());
      return;
    }
    if (auto a = std::dynamic_pointer_cast<AssignStmt>(s)) {
      LowerAssign(*a);
      return;
    }
    if (auto r = std::dynamic_pointer_cast<ReturnStmt>(s)) {
      if (r->results.size() > 1) {
        diag_.ReportError(r->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "Go multiple-value return lowering is not implemented");
        terminated_ = true;
        return;
      }
      if (r->results.empty()) {
        if (current_ret_.kind == ir::IRTypeKind::kVoid)
          builder_.MakeReturn();
        else {
          diag_.ReportError(r->loc, frontends::ErrorCode::kUnsupportedLowering,
                            "bare return in a non-void Go function requires named-result lowering");
          builder_.MakeReturn(builder_.MakeLiteral((long long)0)->name);
        }
      } else {
        auto v = LowerExpr(r->results.front(), current_ret_);
        builder_.MakeReturn(v);
      }
      terminated_ = true;
      return;
    }
    if (auto i = std::dynamic_pointer_cast<IfStmt>(s)) {
      LowerIf(*i);
      return;
    }
    if (auto f = std::dynamic_pointer_cast<ForStmt>(s)) {
      LowerFor(*f);
      return;
    }
    if (auto inc = std::dynamic_pointer_cast<IncDecStmt>(s)) {
      // Treat as "x = x ± 1"
      if (auto id = std::dynamic_pointer_cast<Identifier>(inc->target)) {
        auto it = locals_.find(id->name);
        if (it == locals_.end()) {
          diag_.ReportError(inc->loc, frontends::ErrorCode::kUnsupportedLowering,
                            "cannot lower increment/decrement of an unresolved Go value");
          return;
        }
        auto cur = builder_.MakeLoad(it->second.addr, it->second.type, NextTemp("load"));
        auto one = IsFloat(it->second.type) ? builder_.MakeLiteral(1.0)->name
                                            : builder_.MakeLiteral((long long)1)->name;
        using Op = ir::BinaryInstruction::Op;
        Op op = inc->inc ? (IsFloat(it->second.type) ? Op::kFAdd : Op::kAdd)
                         : (IsFloat(it->second.type) ? Op::kFSub : Op::kSub);
        auto nv = builder_.MakeBinary(op, cur->name, one, NextTemp("incdec"));
        builder_.MakeStore(it->second.addr, nv->name);
      } else {
        diag_.ReportError(inc->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "Go increment/decrement target lowering requires an identifier");
      }
      return;
    }
    if (auto ds = std::dynamic_pointer_cast<DeclStmt>(s)) {
      if (ds->decl && ds->decl->keyword == "var") {
        for (auto &v : ds->decl->values) {
          if (!v.values.empty() && v.values.size() != v.names.size()) {
            diag_.ReportError(v.loc, frontends::ErrorCode::kUnsupportedLowering,
                              "Go declaration value arity requires tuple-aware lowering");
            continue;
          }
          auto ty = ToIRType(v.type, layouts_, diag_, v.loc);
          if (ty.kind == ir::IRTypeKind::kInvalid)
            continue;
          for (size_t k = 0; k < v.names.size(); ++k) {
            if (ty.kind == ir::IRTypeKind::kStruct) {
              const auto *layout = FindLayoutByIRType(ty);
              if (!layout) {
                diag_.ReportError(v.loc, frontends::ErrorCode::kUnsupportedLowering,
                                  "Go local object has no deterministic struct layout");
                continue;
              }
              const std::shared_ptr<Expression> initializer =
                  k < v.values.size() ? v.values[k] : nullptr;
              DeclareObject(v.names[k], *layout, initializer);
              continue;
            }
            auto al = builder_.MakeAlloca(ty, v.names[k] + ".addr");
            locals_[v.names[k]] = {v.names[k] + ".addr", ty, false};
            if (k < v.values.size()) {
              auto rv = LowerExpr(v.values[k], ty);
              builder_.MakeStore(v.names[k] + ".addr", rv);
            } else if (ty.IsInteger() || ty.kind == ir::IRTypeKind::kPointer) {
              builder_.MakeStore(v.names[k] + ".addr", "0");
            } else if (IsFloat(ty)) {
              builder_.MakeStore(v.names[k] + ".addr", builder_.MakeLiteral(0.0)->name);
            }
          }
        }
      } else {
        diag_.ReportError(ds->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "only local var declarations are supported by Go lowering");
      }
      return;
    }
    diag_.ReportError(s->loc, frontends::ErrorCode::kUnsupportedLowering,
                      "unsupported Go statement reached IR lowering");
  }

  void LowerAssign(const AssignStmt &a) {
    if (a.lhs.empty() || a.rhs.empty() || a.lhs.size() != a.rhs.size()) {
      diag_.ReportError(a.loc, frontends::ErrorCode::kUnsupportedLowering,
                        "Go assignment arity requires tuple-aware lowering");
      return;
    }
    // Go's multiple assignment is represented, but this executable slice
    // lowers each statically independent 1:1 value only.
    for (size_t k = 0; k < a.lhs.size() && k < a.rhs.size(); ++k) {
      auto id = std::dynamic_pointer_cast<Identifier>(a.lhs[k]);
      auto selector = std::dynamic_pointer_cast<SelectorExpr>(a.lhs[k]);

      if (id && a.op == ":=" && locals_.find(id->name) == locals_.end()) {
        if (auto literal = std::dynamic_pointer_cast<CompositeLit>(a.rhs[k])) {
          if (!literal->type || literal->type->kind != TypeKind::kNamed) {
            diag_.ReportError(literal->loc, frontends::ErrorCode::kUnsupportedLowering,
                              "Go short struct declaration requires a named composite literal");
            continue;
          }
          auto layout = layouts_.find(literal->type->name);
          if (layout == layouts_.end()) {
            diag_.ReportError(literal->loc, frontends::ErrorCode::kUnsupportedLowering,
                              "Go composite literal has no deterministic struct layout: " +
                                  literal->type->name);
            continue;
          }
          DeclareObject(id->name, layout->second, literal);
          continue;
        }
      }

      std::string target_address;
      ir::IRType target_type = ir::IRType::Invalid();
      bool new_local = false;
      if (id) {
        auto local = locals_.find(id->name);
        if (local != locals_.end()) {
          if (local->second.is_object) {
            diag_.ReportError(id->loc, frontends::ErrorCode::kUnsupportedLowering,
                              "Go whole-struct assignment requires aggregate copy lowering");
            continue;
          }
          target_address = local->second.addr;
          target_type = local->second.type;
        } else if (a.op == ":=") {
          target_type = InferExprType(a.rhs[k]);
          new_local = true;
        } else {
          diag_.ReportError(id->loc, frontends::ErrorCode::kUnsupportedLowering,
                            "assignment to an unresolved Go local: " + id->name);
          continue;
        }
      } else if (selector) {
        if (a.op == ":=") {
          diag_.ReportError(selector->loc, frontends::ErrorCode::kUnsupportedLowering,
                            "Go short declarations cannot declare a field selector");
          continue;
        }
        auto field = ResolveField(selector, true);
        if (!field)
          continue;
        target_address = field->address;
        target_type = field->type;
      } else {
        diag_.ReportError(a.lhs[k]->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "Go assignment target must be a local or static field selector");
        continue;
      }

      if (target_type.kind == ir::IRTypeKind::kInvalid ||
          target_type.kind == ir::IRTypeKind::kStruct) {
        diag_.ReportError(a.rhs[k]->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "cannot infer an executable scalar or pointer type for Go assignment");
        continue;
      }
      auto rv = LowerExpr(a.rhs[k], target_type);
      if (new_local) {
        builder_.MakeAlloca(target_type, id->name + ".addr");
        locals_[id->name] = {id->name + ".addr", target_type, false};
        target_address = id->name + ".addr";
      }
      if (a.op != "=" && a.op != ":=") {
        auto cur = builder_.MakeLoad(target_address, target_type, NextTemp("load"));
        using Op = ir::BinaryInstruction::Op;
        Op op = Op::kAdd;
        bool supported_op = true;
        bool fp = IsFloat(target_type);
        if (a.op == "+=")
          op = fp ? Op::kFAdd : Op::kAdd;
        else if (a.op == "-=")
          op = fp ? Op::kFSub : Op::kSub;
        else if (a.op == "*=")
          op = fp ? Op::kFMul : Op::kMul;
        else if (a.op == "/=")
          op = fp ? Op::kFDiv
                  : (target_type.is_signed ? Op::kSDiv : Op::kUDiv);
        else if (a.op == "%=")
          op = fp ? Op::kFRem
                  : (target_type.is_signed ? Op::kSRem : Op::kURem);
        else if (a.op == "&=")
          op = Op::kAnd;
        else if (a.op == "|=")
          op = Op::kOr;
        else if (a.op == "^=")
          op = Op::kXor;
        else if (a.op == "<<=")
          op = Op::kShl;
        else if (a.op == ">>=")
          op = target_type.is_signed ? Op::kAShr : Op::kLShr;
        else
          supported_op = false;
        if (!supported_op) {
          diag_.ReportError(a.loc, frontends::ErrorCode::kUnsupportedLowering,
                            "unsupported Go compound assignment operator: " + a.op);
          continue;
        }
        rv = builder_.MakeBinary(op, cur->name, rv, NextTemp("augop"))->name;
      }
      builder_.MakeStore(target_address, rv);
    }
  }

  void LowerIf(const IfStmt &i) {
    if (i.init)
      LowerStmt(i.init);
    auto t_bb = builder_.CreateBlock("if.then");
    std::shared_ptr<ir::BasicBlock> e_bb =
        i.else_branch ? builder_.CreateBlock("if.else") : std::shared_ptr<ir::BasicBlock>{};
    auto c_bb = builder_.CreateBlock("if.end");
    auto cv = LowerExpr(i.cond, ir::IRType::I1());
    builder_.MakeCondBranch(cv, t_bb.get(), e_bb ? e_bb.get() : c_bb.get());
    builder_.SetInsertPoint(t_bb);
    terminated_ = false;
    if (i.body)
      for (auto &s : i.body->stmts)
        LowerStmt(s);
    if (!terminated_)
      builder_.MakeBranch(c_bb.get());
    if (e_bb) {
      builder_.SetInsertPoint(e_bb);
      terminated_ = false;
      LowerStmt(i.else_branch);
      if (!terminated_)
        builder_.MakeBranch(c_bb.get());
    }
    builder_.SetInsertPoint(c_bb);
    terminated_ = false;
  }

  void LowerFor(const ForStmt &f) {
    if (f.is_range) {
      // Range requires element/key iteration and runtime length/channel
      // semantics that this IR slice does not yet model.  Never lower it as
      // `for {}`: that silently turned finite source loops into infinite IR.
      diag_.ReportError(f.loc, frontends::ErrorCode::kUnsupportedLowering,
                        "Go range-loop lowering is not supported by the current IR backend");
      return;
    }
    if (f.init)
      LowerStmt(f.init);
    auto cb = builder_.CreateBlock("for.cond");
    auto bb = builder_.CreateBlock("for.body");
    auto pb = f.post ? builder_.CreateBlock("for.post") : std::shared_ptr<ir::BasicBlock>{};
    auto eb = builder_.CreateBlock("for.end");
    builder_.MakeBranch(cb.get());
    builder_.SetInsertPoint(cb);
    if (f.cond) {
      auto cv = LowerExpr(f.cond, ir::IRType::I1());
      builder_.MakeCondBranch(cv, bb.get(), eb.get());
    } else {
      builder_.MakeBranch(bb.get());
    }
    builder_.SetInsertPoint(bb);
    terminated_ = false;
    if (f.body)
      for (auto &s : f.body->stmts)
        LowerStmt(s);
    if (!terminated_)
      builder_.MakeBranch(pb ? pb.get() : cb.get());
    if (pb) {
      builder_.SetInsertPoint(pb);
      terminated_ = false;
      LowerStmt(f.post);
      if (!terminated_)
        builder_.MakeBranch(cb.get());
    }
    builder_.SetInsertPoint(eb);
    terminated_ = false;
  }

  static bool IsFloat(const ir::IRType &t) {
    return t.kind == ir::IRTypeKind::kF32 || t.kind == ir::IRTypeKind::kF64;
  }

  ir::IRType InferExprType(const std::shared_ptr<Expression> &e) {
    if (!e)
      return ir::IRType::Invalid();
    if (auto lit = std::dynamic_pointer_cast<BasicLit>(e)) {
      if (lit->kind == BasicLit::Kind::kInt)
        return ir::IRType::I64(true);
      if (lit->kind == BasicLit::Kind::kFloat)
        return ir::IRType::F64();
      if (lit->kind == BasicLit::Kind::kBool)
        return ir::IRType::I1();
      return ir::IRType::Invalid();
    }
    if (auto id = std::dynamic_pointer_cast<Identifier>(e)) {
      auto it = locals_.find(id->name);
      return it == locals_.end() ? ir::IRType::Invalid() : it->second.type;
    }
    if (auto paren = std::dynamic_pointer_cast<ParenExpr>(e))
      return InferExprType(paren->inner);
    if (auto composite = std::dynamic_pointer_cast<CompositeLit>(e)) {
      if (composite->type && composite->type->kind == TypeKind::kNamed) {
        auto layout = layouts_.find(composite->type->name);
        if (layout != layouts_.end())
          return layout->second.type;
      }
      return ir::IRType::Invalid();
    }
    if (auto selector = std::dynamic_pointer_cast<SelectorExpr>(e)) {
      auto receiver = std::dynamic_pointer_cast<Identifier>(selector->x);
      if (!receiver)
        return ir::IRType::Invalid();
      auto local = locals_.find(receiver->name);
      if (local == locals_.end())
        return ir::IRType::Invalid();
      const auto *layout = FindLayoutByIRType(local->second.type);
      if (!layout)
        return ir::IRType::Invalid();
      auto field = layout->field_indices.find(selector->sel);
      return field == layout->field_indices.end()
                 ? ir::IRType::Invalid()
                 : layout->fields.at(field->second).type;
    }
    if (auto binary = std::dynamic_pointer_cast<BinaryExpr>(e)) {
      if (binary->op == "==" || binary->op == "!=" || binary->op == "<" ||
          binary->op == "<=" || binary->op == ">" || binary->op == ">=" ||
          binary->op == "&&" || binary->op == "||")
        return ir::IRType::I1();
      return InferExprType(binary->left);
    }
    if (auto call = std::dynamic_pointer_cast<CallExpr>(e)) {
      auto id = std::dynamic_pointer_cast<Identifier>(call->fun);
      const FuncDecl *target = id ? FindFunction(id->name) : nullptr;
      if (!target) {
        if (auto selector = std::dynamic_pointer_cast<SelectorExpr>(call->fun)) {
          auto receiver = std::dynamic_pointer_cast<Identifier>(selector->x);
          if (receiver) {
            auto local = locals_.find(receiver->name);
            const auto *layout = local == locals_.end()
                                     ? nullptr
                                     : FindLayoutByIRType(local->second.type);
            if (layout)
              target = FindMethod(layout->name, selector->sel);
          }
        }
      }
      if (target && target->results.size() == 1 && target->type_params.empty() &&
          !target->is_variadic)
        return ToIRType(target->results.front().second, layouts_, diag_, target->loc);
    }
    return ir::IRType::Invalid();
  }

  const FuncDecl *FindFunction(const std::string &name) const {
    const FuncDecl *match = nullptr;
    for (const auto &function : file_.funcs) {
      if (!function || function->receiver || function->name != name)
        continue;
      if (match)
        return nullptr;
      match = function.get();
    }
    return match;
  }

  static std::string ReceiverTypeName(const FuncDecl &function) {
    if (!function.receiver || !function.receiver->type)
      return {};
    auto type = function.receiver->type;
    if (type->kind == TypeKind::kPointer)
      type = type->elem;
    return type && type->kind == TypeKind::kNamed ? type->name : std::string{};
  }

  const FuncDecl *FindMethod(const std::string &receiver_type,
                             const std::string &method_name) const {
    const FuncDecl *match = nullptr;
    for (const auto &function : file_.funcs) {
      if (!function || !function->receiver || function->name != method_name ||
          ReceiverTypeName(*function) != receiver_type)
        continue;
      if (match)
        return nullptr;
      match = function.get();
    }
    return match;
  }

  std::string LowerExpr(const std::shared_ptr<Expression> &e, const ir::IRType &want) {
    if (!e)
      return "";
    if (auto lit = std::dynamic_pointer_cast<BasicLit>(e)) {
      switch (lit->kind) {
      case BasicLit::Kind::kInt: {
        long long v = 0;
        try {
          v = std::stoll(lit->value, nullptr, 0);
        } catch (...) {
          diag_.ReportError(lit->loc, frontends::ErrorCode::kUnsupportedLowering,
                            "Go integer literal is outside the scalar IR parser: " + lit->value);
          return "0";
        }
        // Keep integer constants as textual immediates.  A detached
        // LiteralExpression named `cN` has no defining IR instruction and is
        // therefore indistinguishable from an undefined virtual register to
        // native instruction selection.
        return std::to_string(v);
      }
      case BasicLit::Kind::kFloat: {
        double d = 0.0;
        try {
          d = std::stod(lit->value);
        } catch (...) {
          diag_.ReportError(lit->loc, frontends::ErrorCode::kUnsupportedLowering,
                            "Go floating literal is outside the scalar IR parser: " + lit->value);
          return builder_.MakeLiteral(0.0)->name;
        }
        return builder_.MakeLiteral(d)->name;
      }
      case BasicLit::Kind::kImag:
        diag_.ReportError(lit->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "Go complex/imaginary values are not represented by scalar IR lowering");
        return builder_.MakeLiteral(0.0)->name;
      case BasicLit::Kind::kBool:
        return builder_.MakeLiteral((long long)(lit->value == "true" ? 1 : 0))->name;
      case BasicLit::Kind::kNil:
        return builder_.MakeLiteral((long long)0)->name;
      case BasicLit::Kind::kString:
      case BasicLit::Kind::kRune:
        diag_.ReportError(lit->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "Go string/rune runtime representation is outside scalar IR lowering");
        return builder_.MakeLiteral((long long)0)->name;
      }
    }
    if (auto id = std::dynamic_pointer_cast<Identifier>(e)) {
      auto it = locals_.find(id->name);
      if (it != locals_.end()) {
        if (it->second.is_object) {
          diag_.ReportError(id->loc, frontends::ErrorCode::kUnsupportedLowering,
                            "Go whole-struct values require an explicit field, method, or &T");
          return "0";
        }
        auto load = builder_.MakeLoad(it->second.addr, it->second.type, NextTemp("load"));
        return load->name;
      }
      diag_.ReportError(id->loc, frontends::ErrorCode::kUnsupportedLowering,
                        "unresolved Go value in IR lowering: " + id->name);
      return builder_.MakeLiteral((long long)0)->name;
    }
    if (auto p = std::dynamic_pointer_cast<ParenExpr>(e))
      return LowerExpr(p->inner, want);
    if (auto selector = std::dynamic_pointer_cast<SelectorExpr>(e)) {
      auto field = ResolveField(selector, true);
      if (!field)
        return "0";
      return builder_.MakeLoad(field->address, field->type, NextTemp("field.load"))->name;
    }
    if (auto composite = std::dynamic_pointer_cast<CompositeLit>(e)) {
      diag_.ReportError(composite->loc, frontends::ErrorCode::kUnsupportedLowering,
                        "Go struct composite values must initialize a stack local");
      return "0";
    }
    if (auto u = std::dynamic_pointer_cast<UnaryExpr>(e)) {
      if (u->op == "&") {
        auto id = std::dynamic_pointer_cast<Identifier>(u->operand);
        if (id) {
          auto local = locals_.find(id->name);
          if (local != locals_.end() && local->second.is_object)
            return local->second.addr;
        }
        if (auto selector = std::dynamic_pointer_cast<SelectorExpr>(u->operand)) {
          auto field = ResolveField(selector, true);
          if (field)
            return field->address;
        }
        diag_.ReportError(u->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "Go address-of lowering requires a stack object or static field");
        return "0";
      }
      auto v = LowerExpr(u->operand, want);
      using Op = ir::BinaryInstruction::Op;
      if (u->op == "-") {
        auto z = IsFloat(want) ? builder_.MakeLiteral(0.0)->name
                               : builder_.MakeLiteral((long long)0)->name;
        return builder_
            .MakeBinary(IsFloat(want) ? Op::kFSub : Op::kSub, z, v, NextTemp("neg"))
            ->name;
      }
      if (u->op == "!") {
        auto one = builder_.MakeLiteral((long long)1)->name;
        return builder_.MakeBinary(Op::kXor, v, one, NextTemp("not"))->name;
      }
      if (u->op == "^") {
        auto neg = builder_.MakeLiteral((long long)-1)->name;
        return builder_.MakeBinary(Op::kXor, v, neg, NextTemp("bnot"))->name;
      }
      if (u->op == "+")
        return v;
      diag_.ReportError(u->loc, frontends::ErrorCode::kUnsupportedLowering,
                        "unsupported Go unary operator in lowering: " + u->op);
      return builder_.MakeLiteral((long long)0)->name;
    }
    if (auto bin = std::dynamic_pointer_cast<BinaryExpr>(e)) {
      using Op = ir::BinaryInstruction::Op;
      const std::string &o = bin->op;
      if (bin->op == "&&" || bin->op == "||") {
        diag_.ReportError(bin->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "Go short-circuit boolean operators require control-flow lowering");
        return builder_.MakeLiteral((long long)0)->name;
      }
      const bool comparison = o == "==" || o == "!=" || o == "<" || o == "<=" ||
                              o == ">" || o == ">=";
      auto operand_type = comparison ? InferExprType(bin->left) : want;
      if (operand_type.kind == ir::IRTypeKind::kInvalid)
        operand_type = want;
      bool fp = IsFloat(operand_type);
      bool is_signed = operand_type.is_signed;
      auto l = LowerExpr(bin->left, operand_type);
      auto r = LowerExpr(bin->right, operand_type);
      Op op = Op::kAdd;
      bool cmp = false;
      bool supported = true;
      if (o == "+")
        op = fp ? Op::kFAdd : Op::kAdd;
      else if (o == "-")
        op = fp ? Op::kFSub : Op::kSub;
      else if (o == "*")
        op = fp ? Op::kFMul : Op::kMul;
      else if (o == "/")
        op = fp ? Op::kFDiv : (is_signed ? Op::kSDiv : Op::kUDiv);
      else if (o == "%")
        op = fp ? Op::kFRem : (is_signed ? Op::kSRem : Op::kURem);
      else if (o == "&")
        op = Op::kAnd;
      else if (o == "|")
        op = Op::kOr;
      else if (o == "^")
        op = Op::kXor;
      else if (o == "<<")
        op = Op::kShl;
      else if (o == ">>")
        op = is_signed ? Op::kAShr : Op::kLShr;
      else if (o == "==") {
        op = fp ? Op::kCmpFoe : Op::kCmpEq;
        cmp = true;
      } else if (o == "!=") {
        op = fp ? Op::kCmpFne : Op::kCmpNe;
        cmp = true;
      } else if (o == "<") {
        op = fp ? Op::kCmpFlt : (is_signed ? Op::kCmpSlt : Op::kCmpUlt);
        cmp = true;
      } else if (o == "<=") {
        op = fp ? Op::kCmpFle : (is_signed ? Op::kCmpSle : Op::kCmpUle);
        cmp = true;
      } else if (o == ">") {
        op = fp ? Op::kCmpFgt : (is_signed ? Op::kCmpSgt : Op::kCmpUgt);
        cmp = true;
      } else if (o == ">=") {
        op = fp ? Op::kCmpFge : (is_signed ? Op::kCmpSge : Op::kCmpUge);
        cmp = true;
      } else
        supported = false;
      if (!supported) {
        diag_.ReportError(bin->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "unsupported Go binary operator in lowering: " + o);
        return builder_.MakeLiteral((long long)0)->name;
      }
      auto result = builder_.MakeBinary(op, l, r, NextTemp(cmp ? "cmp" : "bop"));
      result->type = cmp ? ir::IRType::I1() : operand_type;
      return result->name;
    }
    if (auto c = std::dynamic_pointer_cast<CallExpr>(e)) {
      if (c->is_new_expression) {
        diag_.ReportError(
            c->loc, frontends::ErrorCode::kUnsupportedLowering,
            "Go 1.26 new(expression) lowering is not implemented");
        return builder_.MakeLiteral((long long)0)->name;
      }
      if (auto id = std::dynamic_pointer_cast<Identifier>(c->fun);
          id && id->name == "new") {
        diag_.ReportError(c->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "Go new allocation lowering is not implemented");
        return builder_.MakeLiteral((long long)0)->name;
      }
      if (std::dynamic_pointer_cast<TypeInstantiationExpr>(c->fun)) {
        diag_.ReportError(
            c->loc, frontends::ErrorCode::kUnsupportedLowering,
            "Go generic-call lowering requires monomorphization and is not implemented");
        return builder_.MakeLiteral((long long)0)->name;
      }
      auto id = std::dynamic_pointer_cast<Identifier>(c->fun);
      if (!id) {
        if (auto selector = std::dynamic_pointer_cast<SelectorExpr>(c->fun)) {
          auto object = ResolveObject(selector->x, true);
          if (!object || !object->layout)
            return "0";
          const auto *target = FindMethod(object->layout->name, selector->sel);
          if (!target || !target->receiver || !target->receiver->type ||
              target->receiver->type->kind != TypeKind::kPointer ||
              !target->type_params.empty() || target->is_variadic ||
              target->results.size() > 1 || target->params.size() != c->args.size()) {
            diag_.ReportError(
                c->loc, frontends::ErrorCode::kUnsupportedLowering,
                "Go method call requires a unique local pointer-receiver signature: " +
                    object->layout->name + "." + selector->sel);
            return "0";
          }
          std::vector<std::string> args{object->base};
          for (size_t i = 0; i < c->args.size(); ++i) {
            auto param_type =
                ToIRType(target->params[i].second, layouts_, diag_, target->loc);
            if (param_type.kind == ir::IRTypeKind::kInvalid ||
                param_type.kind == ir::IRTypeKind::kStruct)
              return "0";
            args.push_back(LowerExpr(c->args[i], param_type));
          }
          auto return_type = target->results.empty()
                                 ? ir::IRType::Void()
                                 : ToIRType(target->results.front().second, layouts_, diag_,
                                            target->loc);
          if (return_type.kind == ir::IRTypeKind::kInvalid ||
              return_type.kind == ir::IRTypeKind::kStruct)
            return "0";
          std::string callee = object->layout->name + "." + target->name;
          if (!file_.package_name.empty() && file_.package_name != "main")
            callee = file_.package_name + "." + callee;
          return builder_.MakeCall(callee, args, return_type, NextTemp("method.call"))->name;
        }
        diag_.ReportError(c->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "dynamic or indirect Go calls require resolved ABI metadata");
        return "0";
      }
      const auto *target = FindFunction(id->name);
      if (!target || !target->type_params.empty() || target->is_variadic ||
          target->results.size() > 1 || target->params.size() != c->args.size()) {
        diag_.ReportError(c->loc, frontends::ErrorCode::kUnsupportedLowering,
                          "Go call lowering requires a unique local executable signature: " +
                              id->name);
        return "0";
      }
      std::vector<std::string> args;
      for (size_t i = 0; i < c->args.size(); ++i) {
        auto param_type = ToIRType(target->params[i].second, layouts_, diag_, target->loc);
        if (param_type.kind == ir::IRTypeKind::kInvalid ||
            param_type.kind == ir::IRTypeKind::kStruct)
          return "0";
        args.push_back(LowerExpr(c->args[i], param_type));
      }
      auto return_type = target->results.empty()
                             ? ir::IRType::Void()
                             : ToIRType(target->results.front().second, layouts_, diag_, target->loc);
      if (return_type.kind == ir::IRTypeKind::kInvalid ||
          return_type.kind == ir::IRTypeKind::kStruct)
        return "0";
      std::string callee = id->name;
      if (!file_.package_name.empty() && file_.package_name != "main")
        callee = file_.package_name + "." + callee;
      return builder_.MakeCall(callee, args, return_type, NextTemp("call"))->name;
    }
    if (std::dynamic_pointer_cast<TypeInstantiationExpr>(e)) {
      diag_.ReportError(
          e->loc, frontends::ErrorCode::kUnsupportedLowering,
          "Go generic function values require monomorphization and are not supported");
      return builder_.MakeLiteral((long long)0)->name;
    }
    if (std::dynamic_pointer_cast<IndexExpr>(e)) {
      diag_.ReportError(e->loc, frontends::ErrorCode::kUnsupportedLowering,
                        "Go index/generic-value lowering is not implemented");
      return builder_.MakeLiteral((long long)0)->name;
    }
    diag_.ReportError(e->loc, frontends::ErrorCode::kUnsupportedLowering,
                      "unsupported Go expression reached IR lowering");
    return IsFloat(want) ? builder_.MakeLiteral(0.0)->name
                         : builder_.MakeLiteral((long long)0)->name;
  }

  const File &file_;
  ir::IRContext &ctx_;
  ir::IRBuilder builder_;
  frontends::Diagnostics &diag_;
  StructLayouts layouts_;
  std::unordered_map<std::string, Local> locals_;
  ir::IRType current_ret_{ir::IRType::Void()};
  bool terminated_{false};
  std::size_t next_temp_{0};
};

} // namespace

void LowerToIR(const File &f, ir::IRContext &c, frontends::Diagnostics &d) {
  Lowerer(f, c, d).Run();
}

} // namespace polyglot::go
