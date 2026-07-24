/**
 * @file     lowering.cpp
 * @brief    Poly language frontend implementation
 *
 * @ingroup  Frontend / Poly
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>

#include "common/include/core/types.h"
#include "frontends/ploy/include/ploy_lowering.h"

namespace polyglot::ploy {
namespace {

std::string AbiLanguageToken(const std::string &language) {
  std::string folded = language;
  std::transform(folded.begin(), folded.end(), folded.begin(),
                 [](unsigned char c) {
                   return static_cast<char>(std::tolower(c));
                 });
  return (folded == "poly" || folded == "ploy") ? "ploy" : language;
}

// Mangle a cross-language stub name. Without a pinned version the name is
//   __ploy_bridge_<target_lang>_<source_lang>_<symbol>
// When the call carries a `LANG <lang> = <version>;` pin (or is inside a
// `WITH LANG`/`@LANG` scope) the version is woven in as a separate segment:
//   __ploy_bridge_<target_lang>_<source_lang>_v<sanitized_version>_<symbol>
// `.`, `-` and other punctuation in the version are normalised to `_` so the
// resulting symbol is a valid C identifier and survives every supported
// object-file format. The unversioned form is preserved as the fallback for
// older descriptors that predate version-aware ABI routing (Phase 2 Track C).
std::string MangleStubName(const std::string &target_lang, const std::string &source_lang,
                           const std::string &symbol, const std::string &lang_version = "") {
  std::string mangled = "__ploy_bridge_" + AbiLanguageToken(target_lang) + "_" +
                        AbiLanguageToken(source_lang) + "_";
  if (!lang_version.empty()) {
    mangled += "v";
    for (char c : lang_version) {
      mangled.push_back((std::isalnum(static_cast<unsigned char>(c))) ? c : '_');
    }
    mangled.push_back('_');
  }
  for (char c : symbol) {
    if (c == ':') {
      mangled.push_back('_');
    } else {
      mangled.push_back(c);
    }
  }
  return mangled;
}

} // anonymous namespace

// ============================================================================
// Public Interface
// ============================================================================

namespace {

// Top-level executable statements (PRINTLN, IF, WHILE, FOR, MATCH, RETURN,
// VAR, ExprStatement, raw blocks, WITH, WITH LANG, @LANG-wrapped statements)
// must live inside a function body before the IR verifier accepts them — an
// orphan PRINTLN at file scope used to land in the implicit `entry_fn`
// fallback whose entry block was never properly terminated, which made the
// "block missing terminator" verifier rule reject the module and aborted the
// whole polyc → polyld → exe pipeline before any backend got a chance to emit
// the literal bytes. Definitional declarations (FUNC / PIPELINE / STRUCT /
// EXTEND / MAPFUNC / LINK / IMPORT / EXPORT / @LANG wrapping a definition)
// stay at the top level and are lowered as before.
bool IsTopLevelExecutable(const std::shared_ptr<Statement> &stmt) {
  if (!stmt)
    return false;
  if (std::dynamic_pointer_cast<FuncDecl>(stmt))
    return false;
  if (std::dynamic_pointer_cast<PipelineDecl>(stmt))
    return false;
  if (std::dynamic_pointer_cast<StructDecl>(stmt))
    return false;
  if (std::dynamic_pointer_cast<ExtendDecl>(stmt))
    return false;
  if (std::dynamic_pointer_cast<MapFuncDecl>(stmt))
    return false;
  if (std::dynamic_pointer_cast<LinkDecl>(stmt))
    return false;
  if (std::dynamic_pointer_cast<ImportDecl>(stmt))
    return false;
  if (std::dynamic_pointer_cast<ExportDecl>(stmt))
    return false;
  // TYPE alias is sema-only metadata — definitional, never executable.
  if (std::dynamic_pointer_cast<TypeAliasDecl>(stmt))
    return false;
  // CLASS schema (demand 2026-04-28-9) is also sema-only metadata: it
  // populates the foreign-class signature registry and emits no IR, so
  // it must NOT be hoisted into the synthetic __ploy_main wrapper.
  if (std::dynamic_pointer_cast<ClassDecl>(stmt))
    return false;
  // A @LANG annotation that wraps a FUNC/STRUCT/etc. is itself definitional;
  // when it wraps an executable statement (e.g. a cross-language CALL) we
  // recurse into the target's classification.
  if (auto anno = std::dynamic_pointer_cast<LangAnnotation>(stmt))
    return IsTopLevelExecutable(anno->target);
  return true;
}

// True iff the user already provided a function with a name we recognise as
// an entry point. We treat `main` and `__ploy_main` as the two canonical
// spellings; the latter exists so a hand-written .poly that wants to spell
// out the synthetic wrapper itself is still respected.
bool ModuleDefinesEntryPoint(const std::shared_ptr<Module> &module) {
  for (const auto &decl : module->declarations) {
    if (auto fn = std::dynamic_pointer_cast<FuncDecl>(decl)) {
      if (fn->name == "main" || fn->name == "__ploy_main")
        return true;
    }
  }
  return false;
}

} // anonymous namespace

bool PloyLowering::Lower(const std::shared_ptr<Module> &module) {
  // Decide up-front whether we need to synthesise a `__ploy_main` wrapper.
  // The synthesis is conditional on two facts:
  //   * the module does NOT already declare a `main` / `__ploy_main`
  //     function (we never overwrite a user-supplied entry point);
  //   * the module contains at least one top-level executable statement
  //     that would otherwise be orphaned outside any function body.
  // Both must hold; otherwise we lower exactly as before.
  const bool has_user_entry = ModuleDefinesEntryPoint(module);
  bool needs_synthetic_entry = false;
  if (!has_user_entry) {
    for (const auto &decl : module->declarations) {
      if (IsTopLevelExecutable(decl)) {
        needs_synthetic_entry = true;
        break;
      }
    }
  }

  if (!needs_synthetic_entry) {
    for (const auto &decl : module->declarations) {
      LowerStatement(decl);
    }
  } else {
    // Phase 1: lower every definitional declaration first so the synthetic
    // entry's body can call into user-defined functions / reference
    // user-defined globals without forward-declaration headaches.
    std::vector<std::shared_ptr<Statement>> deferred;
    deferred.reserve(module->declarations.size());
    for (const auto &decl : module->declarations) {
      if (IsTopLevelExecutable(decl)) {
        deferred.push_back(decl);
      } else {
        LowerStatement(decl);
      }
    }

    // Phase 2: synthesise `i32 __ploy_main()` and lower the deferred
    // statements into its entry block. The function is created via the
    // standard CreateFunction path so it shows up in `ctx.Functions()`
    // and participates in every downstream pass (verifier, LTO, backend
    // emit) on equal footing with user-written functions.
    auto synth_fn = ir_ctx_.CreateFunction("__ploy_main", ir::IRType::I32(true), {});
    auto saved_fn = current_function_;
    auto saved_insert = builder_.GetInsertPoint();
    bool saved_terminated = terminated_;

    current_function_ = synth_fn;
    builder_.SetCurrentFunction(synth_fn);
    terminated_ = false;
    auto entry = builder_.CreateBlock("entry");
    builder_.SetInsertPoint(entry);

    for (const auto &stmt : deferred) {
      LowerStatement(stmt);
      if (terminated_) {
        // A top-level RETURN already terminated the block; further
        // statements would be unreachable, mirroring the behaviour of
        // user-written `FUNC main` bodies.
        break;
      }
    }

    // Always close the block with `RETURN i32 0` when the user's code
    // didn't terminate it explicitly. We pass the literal text "0"
    // directly because the IR uses the literal-as-name convention for
    // integer constants (mirroring LowerLiteral's kInteger branch); the
    // verifier accepts this without requiring a defining instruction.
    if (!terminated_) {
      builder_.MakeReturn("0");
      terminated_ = true;
    }

    current_function_ = saved_fn;
    if (saved_fn) {
      builder_.SetCurrentFunction(saved_fn);
    } else {
      builder_.ClearCurrentFunction();
    }
    builder_.SetInsertPoint(saved_insert);
    terminated_ = saved_terminated;
  }

  // Generate link stubs for all LINK entries registered in sema
  for (const auto &link : sema_.Links()) {
    GenerateLinkStub(link);
  }

  return !diagnostics_.HasErrors();
}

// ============================================================================
// Statement Lowering
// ============================================================================

void PloyLowering::LowerStatement(const std::shared_ptr<Statement> &stmt) {
  if (!stmt)
    return;

  if (auto link = std::dynamic_pointer_cast<LinkDecl>(stmt)) {
    LowerLinkDecl(link);
  } else if (auto import_decl = std::dynamic_pointer_cast<ImportDecl>(stmt)) {
    LowerImportDecl(import_decl);
  } else if (auto export_decl = std::dynamic_pointer_cast<ExportDecl>(stmt)) {
    LowerExportDecl(export_decl);
  } else if (auto pipeline = std::dynamic_pointer_cast<PipelineDecl>(stmt)) {
    LowerPipelineDecl(pipeline);
  } else if (auto func = std::dynamic_pointer_cast<FuncDecl>(stmt)) {
    LowerFuncDecl(func);
  } else if (auto var = std::dynamic_pointer_cast<VarDecl>(stmt)) {
    LowerVarDecl(var);
  } else if (auto const_decl = std::dynamic_pointer_cast<ConstDecl>(stmt)) {
    // CONST lowers as an immutable VAR: sema has already folded the
    // initializer and validated its type, so we re-use the existing
    // VarDecl path to emit a single LocalDecl + assignment.  This keeps
    // the IR shape identical to a `LET` declaration, which is exactly
    // what the middle-layer const-propagation pass expects.
    auto synthetic_var = std::make_shared<VarDecl>();
    synthetic_var->loc = const_decl->loc;
    synthetic_var->name = const_decl->name;
    synthetic_var->is_mutable = false;
    synthetic_var->type = const_decl->type;
    synthetic_var->init = const_decl->value;
    LowerVarDecl(synthetic_var);
  } else if (auto alias_decl = std::dynamic_pointer_cast<TypeAliasDecl>(stmt)) {
    // TYPE alias is a sema-only construct: ResolveType has already
    // substituted the aliased type at every use-site, so lowering needs
    // to emit nothing.  Suppress the unused-variable warning explicitly.
    (void) alias_decl;
  } else if (auto cls_decl = std::dynamic_pointer_cast<ClassDecl>(stmt)) {
    // CLASS schema (demand 2026-04-28-9) is sema-only: it populates the
    // foreign-class signature registry that NEW / METHOD / GET / SET
    // expressions consult during lowering, but it emits no IR of its
    // own.  Reaching this branch in LowerStatement is rare (the top-
    // level loop already filters via IsTopLevelExecutable), but we
    // still no-op explicitly so a stray reachable path can't drop into
    // the unhandled-statement diagnostic below.
    (void) cls_decl;
  } else if (auto if_stmt = std::dynamic_pointer_cast<IfStatement>(stmt)) {
    LowerIfStatement(if_stmt);
  } else if (auto if_let = std::dynamic_pointer_cast<IfLetStatement>(stmt)) {
    LowerIfLetStatement(if_let);
  } else if (auto while_stmt = std::dynamic_pointer_cast<WhileStatement>(stmt)) {
    LowerWhileStatement(while_stmt);
  } else if (auto for_stmt = std::dynamic_pointer_cast<ForStatement>(stmt)) {
    LowerForStatement(for_stmt);
  } else if (auto match_stmt = std::dynamic_pointer_cast<MatchStatement>(stmt)) {
    LowerMatchStatement(match_stmt);
  } else if (auto ret = std::dynamic_pointer_cast<ReturnStatement>(stmt)) {
    LowerReturnStatement(ret);
  } else if (auto println = std::dynamic_pointer_cast<PrintlnStmt>(stmt)) {
    LowerPrintlnStatement(println);
  } else if (auto expr_stmt = std::dynamic_pointer_cast<ExprStatement>(stmt)) {
    if (expr_stmt->expr) {
      LowerExpression(expr_stmt->expr);
    }
  } else if (auto block = std::dynamic_pointer_cast<BlockStatement>(stmt)) {
    LowerBlockStatements(block->statements);
  } else if (auto struct_decl = std::dynamic_pointer_cast<StructDecl>(stmt)) {
    LowerStructDecl(struct_decl);
  } else if (auto map_func = std::dynamic_pointer_cast<MapFuncDecl>(stmt)) {
    LowerMapFuncDecl(map_func);
  } else if (auto with_stmt = std::dynamic_pointer_cast<WithStatement>(stmt)) {
    LowerWithStatement(with_stmt);
  } else if (auto extend = std::dynamic_pointer_cast<ExtendDecl>(stmt)) {
    LowerExtendDecl(extend);
  } else if (auto with_lang = std::dynamic_pointer_cast<WithLangBlock>(stmt)) {
    // The pins inside `with_lang` were already pushed onto inner CALL /
    // NEW / METHOD / GETATTR / SETATTR / DELETE / EXTEND nodes by sema.
    // Lowering only needs to recurse into the body so the inner cross-
    // language nodes get translated into call descriptors.
    LowerBlockStatements(with_lang->body);
  } else if (auto lang_anno = std::dynamic_pointer_cast<LangAnnotation>(stmt)) {
    // Same story: sema stamped the per-call pin onto the wrapped statement;
    // lowering must descend through the wrapper.
    if (lang_anno->target) {
      LowerStatement(lang_anno->target);
    }
  } else if (auto try_stmt = std::dynamic_pointer_cast<TryStatement>(stmt)) {
    LowerTryStatement(try_stmt);
  } else if (auto throw_stmt = std::dynamic_pointer_cast<ThrowStatement>(stmt)) {
    LowerThrowStatement(throw_stmt);
  } else if (std::dynamic_pointer_cast<BreakStatement>(stmt)) {
    if (break_targets_.empty()) {
      diagnostics_.ReportError(stmt->loc, frontends::ErrorCode::kUnsupportedLowering,
                               "cannot lower BREAK without an active loop target");
      return;
    }
    builder_.MakeBranch(break_targets_.back());
    terminated_ = true;
  } else if (std::dynamic_pointer_cast<ContinueStatement>(stmt)) {
    if (continue_targets_.empty()) {
      diagnostics_.ReportError(stmt->loc, frontends::ErrorCode::kUnsupportedLowering,
                               "cannot lower CONTINUE without an active loop target");
      return;
    }
    builder_.MakeBranch(continue_targets_.back());
    terminated_ = true;
  } else if (std::dynamic_pointer_cast<MapTypeDecl>(stmt) ||
             std::dynamic_pointer_cast<VenvConfigDecl>(stmt) ||
             std::dynamic_pointer_cast<LangPragma>(stmt)) {
    // Sema-only/module metadata: intentionally no runtime IR.
  } else {
    diagnostics_.ReportError(stmt->loc, frontends::ErrorCode::kUnsupportedLowering,
                             "Poly statement has no semantics-preserving IR lowering");
  }
}

// ============================================================================
// LINK Lowering
// ============================================================================

void PloyLowering::LowerLinkDecl(const std::shared_ptr<LinkDecl> &link) {
  // LINK directives are processed in bulk after all statements,
  // using the validated LinkEntry structures from sema.
  // No per-statement IR is generated here.
  (void)link;
}

// ============================================================================
// IMPORT Lowering
// ============================================================================

void PloyLowering::LowerImportDecl(const std::shared_ptr<ImportDecl> &import) {
  // IMPORT creates a declaration for an external module.
  // Generate an external symbol reference so the linker can resolve it.
  std::string module_sym = "__ploy_module_";
  if (!import->language.empty()) {
    module_sym += AbiLanguageToken(import->language) + "_";
  }
  module_sym += import->module_path;
  for (char &c : module_sym) {
    if (c == ':' || c == '/' || c == '\\' || c == '.')
      c = '_';
  }

  // Declare as an external global (opaque pointer to the module descriptor)
  ir_ctx_.CreateGlobal(module_sym, ir::IRType::Pointer(ir::IRType::I8()), false, "external");

  // If version constraint is specified, emit a version metadata global.
  // The linker uses this to verify package compatibility.
  if (!import->version_op.empty() && !import->version_constraint.empty()) {
    std::string ver_sym = module_sym + "_version_constraint";
    std::string ver_data = import->version_op + " " + import->version_constraint;
    ir_ctx_.CreateGlobal(ver_sym, ir::IRType::Pointer(ir::IRType::I8()), false, ver_data);
  }

  // If selective imports are specified, emit a symbol list metadata global.
  // The linker uses this to generate targeted bindings for selected symbols only.
  if (!import->selected_symbols.empty()) {
    std::string sel_sym = module_sym + "_selected_symbols";
    std::string sel_data;
    for (size_t i = 0; i < import->selected_symbols.size(); ++i) {
      if (i > 0)
        sel_data += ",";
      sel_data += import->selected_symbols[i];
    }
    ir_ctx_.CreateGlobal(sel_sym, ir::IRType::Pointer(ir::IRType::I8()), false, sel_data);

    // Also declare individual external symbols for each selected import
    for (const auto &sym : import->selected_symbols) {
      std::string sym_name = module_sym + "_" + sym;
      ir_ctx_.CreateGlobal(sym_name, ir::IRType::Pointer(ir::IRType::I8()), false, "external");
    }
  }
}

// ============================================================================
// EXPORT Lowering
// ============================================================================

void PloyLowering::LowerExportDecl(const std::shared_ptr<ExportDecl> &export_decl) {
  // Mark the corresponding IR function/global as externally visible
  // Find the function in the IR context and set its linkage
  for (const auto &fn : ir_ctx_.Functions()) {
    if (fn->name == export_decl->symbol_name) {
      // The function is already created - mark it for export
      // We record this via a global symbol alias
      std::string ext_name = export_decl->external_name.empty() ? export_decl->symbol_name
                                                                : export_decl->external_name;
      if (ext_name != fn->name) {
        ir_ctx_.CreateGlobal("__ploy_export_alias_" + ext_name,
                             ir::IRType::Pointer(ir::IRType::Void()), false, fn->name);
      }
      return;
    }
  }
}

// ============================================================================
// PIPELINE Lowering
// ============================================================================

void PloyLowering::LowerPipelineDecl(const std::shared_ptr<PipelineDecl> &pipeline) {
  // A pipeline is lowered as a regular function named __ploy_pipeline_<name>
  std::string fn_name = "__ploy_pipeline_" + pipeline->name;

  auto fn = ir_ctx_.CreateFunction(fn_name, ir::IRType::Void(), {});
  current_function_ = fn;
  builder_.SetCurrentFunction(fn);
  terminated_ = false;

  auto entry = builder_.CreateBlock("entry");
  builder_.SetInsertPoint(entry);

  LowerBlockStatements(pipeline->body);

  // Add implicit return if not terminated
  if (!terminated_) {
    builder_.MakeReturn();
  }

  current_function_ = nullptr;
  builder_.ClearCurrentFunction();
}

// ============================================================================
// FUNC Lowering
// ============================================================================

void PloyLowering::LowerFuncDecl(const std::shared_ptr<FuncDecl> &func) {
  // Save outer context - nested functions (e.g. inside PIPELINE) must not
  // clobber the enclosing function's state.
  auto saved_fn = current_function_;
  auto saved_insert = builder_.GetInsertPoint();
  bool saved_terminated = terminated_;

  // Build parameter list
  std::vector<std::pair<std::string, ir::IRType>> params;
  for (const auto &p : func->params) {
    ir::IRType param_type = PloyTypeToIR(p.type);
    params.emplace_back(p.name, param_type);
  }

  ir::IRType ret_type = func->return_type ? PloyTypeToIR(func->return_type) : ir::IRType::Void();

  auto fn = ir_ctx_.CreateFunction(func->name, ret_type, params);
  current_function_ = fn;
  builder_.SetCurrentFunction(fn);
  terminated_ = false;

  auto entry = builder_.CreateBlock("entry");
  builder_.SetInsertPoint(entry);

  // Register parameters in the environment
  for (const auto &p : func->params) {
    ir::IRType pt = PloyTypeToIR(p.type);
    env_[p.name] = EnvEntry{p.name, pt};
  }

  // Async-function prologue: bracket the body with a runtime task-enter
  // marker so the cooperative scheduler can register the current frame
  // as a `Future<T>` producer (since v1.14.0).
  if (func->is_async) {
    builder_.MakeCall("__ploy_rt_async_enter", {}, ir::IRType::Void());
  }

  LowerBlockStatements(func->body);

  // Add implicit void return if not terminated
  if (!terminated_) {
    if (func->is_async) {
      builder_.MakeCall("__ploy_rt_async_complete", {}, ir::IRType::Void());
    }
    builder_.MakeReturn();
  }

  // Clean up parameter entries from environment
  for (const auto &p : func->params) {
    env_.erase(p.name);
  }

  // Restore outer context
  current_function_ = saved_fn;
  if (saved_fn) {
    builder_.SetCurrentFunction(saved_fn);
  } else {
    builder_.ClearCurrentFunction();
  }
  builder_.SetInsertPoint(saved_insert);
  terminated_ = saved_terminated;
}

// ============================================================================
// Variable Declaration Lowering
// ============================================================================

void PloyLowering::LowerVarDecl(const std::shared_ptr<VarDecl> &var) {
  // Resolve the type from the AST annotation if present, otherwise consult
  // the sema symbol table to get the inferred type.  An unresolved type is
  // not an integer: retain Invalid until an initializer supplies a concrete
  // type, otherwise fail closed below.
  ir::IRType var_type = ir::IRType::Invalid();
  if (var->type) {
    var_type = PloyTypeToIR(var->type);
  } else {
    auto sym_it = sema_.Symbols().find(var->name);
    if (sym_it != sema_.Symbols().end() && sym_it->second.type.kind != core::TypeKind::kAny &&
        sym_it->second.type.kind != core::TypeKind::kUnknown &&
        sym_it->second.type.kind != core::TypeKind::kInvalid) {
      var_type = CoreTypeToIR(sym_it->second.type);
    }
  }

  if (var->init) {
    EvalResult init_result = LowerExpression(var->init);
    if (init_result.type.kind == ir::IRTypeKind::kInvalid ||
        (init_result.type.is_placeholder && var->is_mutable)) {
      diagnostics_.ReportError(
          var->loc, frontends::ErrorCode::kUnsupportedLowering,
          "cannot lower variable '" + var->name +
              "' with an invalid initializer or mutable unresolved ABI storage");
      return;
    }
    if (var_type.kind == ir::IRTypeKind::kInvalid) {
      var_type = init_result.type;
    }
    std::string init_value = init_result.value;
    if (!init_result.type.SameShape(var_type)) {
      init_value = var->name + ".init.converted." + std::to_string(generated_name_index_++);
      if (!GenerateMarshalCode(init_result.value, init_result.type, var_type, init_value, var->loc,
                               "initializer for variable '" + var->name + "'")) {
        return;
      }
    }
    if (var->is_mutable) {
      // Mutable VAR: use alloca/store/load pattern so that
      // re-assignments inside loops produce correct SSA.
      auto alloca_inst = builder_.MakeAlloca(var_type, var->name);
      builder_.MakeStore(alloca_inst->name, init_value);
      env_[var->name] = EnvEntry{alloca_inst->name, var_type, true};
    } else {
      // Immutable LET: bind the SSA value directly.
      env_[var->name] = EnvEntry{init_value, var_type, false};
    }
  } else {
    if (var_type.kind == ir::IRTypeKind::kInvalid || var_type.is_placeholder) {
      diagnostics_.ReportError(
          var->loc, frontends::ErrorCode::kUnsupportedLowering,
          "cannot lower uninitialized variable '" + var->name +
              "' without a concrete type");
      return;
    }
    // Allocate space for the variable (no initializer)
    auto alloca_inst = builder_.MakeAlloca(var_type, var->name);
    env_[var->name] = EnvEntry{alloca_inst->name, var_type, var->is_mutable};
  }
}

// ============================================================================
// Control Flow Lowering
// ============================================================================

// Ensure a value is I1 (boolean) for use as a branch condition.
// If the value is already I1, return it unchanged.
// For integers: emit  icmp ne %val, 0
// For pointers: emit  ptrtoint %val to i64  then  icmp ne %tmp, 0
// For floats:   emit  fcmp one %val, 0.0       (ordered not-equal)
std::string PloyLowering::EnsureI1(const EvalResult &val, const core::SourceLoc &loc) {
  const ir::IRType &t = val.type;
  // A placeholder is an explicitly unknown ABI value.  Its bits have no
  // language-level truthiness, even when the permissive compatibility mode
  // allowed the opaque call itself to be emitted.
  if (t.kind == ir::IRTypeKind::kInvalid || t.is_placeholder || val.value.empty()) {
    diagnostics_.ReportError(
        loc, frontends::ErrorCode::kUnsupportedLowering,
        "cannot lower a condition whose value type or ABI is unresolved");
    return {};
  }

  // Already I1 — nothing to do.
  if (t.kind == ir::IRTypeKind::kI1)
    return val.value;

  // Integer types — compare != 0
  if (t.IsInteger()) {
    auto cmp = builder_.MakeBinary(ir::BinaryInstruction::Op::kCmpNe, val.value, "0", "tobool");
    cmp->type = ir::IRType::I1();
    return cmp->name;
  }

  // Float types — ordered not-equal to 0.0
  if (t.kind == ir::IRTypeKind::kF32 || t.kind == ir::IRTypeKind::kF64) {
    auto cmp = builder_.MakeBinary(ir::BinaryInstruction::Op::kCmpFne, val.value, "0.0", "tobool");
    cmp->type = ir::IRType::I1();
    return cmp->name;
  }

  // Pointer / Reference — ptrtoint then compare != 0
  if (t.kind == ir::IRTypeKind::kPointer || t.kind == ir::IRTypeKind::kReference) {
    auto cast = builder_.MakeCast(ir::CastInstruction::CastKind::kPtrToInt, val.value,
                                  ir::IRType::I64(true));
    auto cmp = builder_.MakeBinary(ir::BinaryInstruction::Op::kCmpNe, cast->name, "0", "tobool");
    cmp->type = ir::IRType::I1();
    return cmp->name;
  }

  diagnostics_.ReportError(loc, frontends::ErrorCode::kUnsupportedLowering,
                           "condition type '" + t.name +
                               "' has no defined Poly truthiness lowering");
  return {};
}

void PloyLowering::LowerIfStatement(const std::shared_ptr<IfStatement> &if_stmt) {
  EvalResult cond = LowerExpression(if_stmt->condition);
  std::string cond_i1 = EnsureI1(cond, if_stmt->condition ? if_stmt->condition->loc : if_stmt->loc);
  if (cond_i1.empty())
    return;

  auto then_bb = builder_.CreateBlock("if.then");
  auto else_bb = builder_.CreateBlock("if.else");
  auto merge_bb = builder_.CreateBlock("if.merge");

  builder_.MakeCondBranch(cond_i1, then_bb.get(), else_bb.get());

  // Then block
  builder_.SetInsertPoint(then_bb);
  terminated_ = false;
  LowerBlockStatements(if_stmt->then_body);
  bool then_terminated = terminated_;
  if (!terminated_) {
    builder_.MakeBranch(merge_bb.get());
  }

  // Else block
  builder_.SetInsertPoint(else_bb);
  terminated_ = false;
  if (!if_stmt->else_body.empty()) {
    LowerBlockStatements(if_stmt->else_body);
  }
  bool else_terminated = terminated_;
  if (!terminated_) {
    builder_.MakeBranch(merge_bb.get());
  }

  // Continue in merge block
  builder_.SetInsertPoint(merge_bb);
  // If both branches terminated (e.g., both have RETURN), the merge block
  // is unreachable.  Mark terminated so the caller won't emit a void return.
  terminated_ = then_terminated && else_terminated;
  if (terminated_) {
    builder_.MakeUnreachable();
  }
}

void PloyLowering::LowerIfLetStatement(const std::shared_ptr<IfLetStatement> &if_let) {
  diagnostics_.ReportError(
      if_let->loc, frontends::ErrorCode::kUnsupportedLowering,
      "IF LET requires OPTION tag and payload extraction, which the Poly IR ABI does not yet "
      "represent");
}

void PloyLowering::LowerWhileStatement(const std::shared_ptr<WhileStatement> &while_stmt) {
  auto cond_bb = builder_.CreateBlock("while.cond");
  auto body_bb = builder_.CreateBlock("while.body");
  auto exit_bb = builder_.CreateBlock("while.exit");

  builder_.MakeBranch(cond_bb.get());

  // Condition block
  builder_.SetInsertPoint(cond_bb);
  EvalResult cond = LowerExpression(while_stmt->condition);
  std::string cond_i1 =
      EnsureI1(cond, while_stmt->condition ? while_stmt->condition->loc : while_stmt->loc);
  if (cond_i1.empty()) {
    builder_.MakeBranch(exit_bb.get());
    builder_.SetInsertPoint(exit_bb);
    terminated_ = false;
    return;
  }
  builder_.MakeCondBranch(cond_i1, body_bb.get(), exit_bb.get());

  // Body block
  builder_.SetInsertPoint(body_bb);
  terminated_ = false;
  break_targets_.push_back(exit_bb.get());
  continue_targets_.push_back(cond_bb.get());
  LowerBlockStatements(while_stmt->body);
  continue_targets_.pop_back();
  break_targets_.pop_back();
  if (!terminated_) {
    builder_.MakeBranch(cond_bb.get());
  }

  // Exit
  builder_.SetInsertPoint(exit_bb);
  terminated_ = false;
}

void PloyLowering::LowerForStatement(const std::shared_ptr<ForStatement> &for_stmt) {
  // The current IR has no iterable protocol (next/element/end) contract.
  // A RangeExpression, however, has exact scalar semantics and can be lowered
  // without pretending an arbitrary collection value is a numeric bound.
  auto range = std::dynamic_pointer_cast<RangeExpression>(for_stmt->iterable);
  if (!range || !range->start || !range->end) {
    diagnostics_.ReportError(
        for_stmt->loc, frontends::ErrorCode::kUnsupportedLowering,
        "FOR IN over a general iterable requires an iterator ABI; only explicit integer ranges "
        "can currently be lowered faithfully");
    return;
  }

  EvalResult start = LowerExpression(range->start);
  EvalResult end = LowerExpression(range->end);
  if (start.type.kind == ir::IRTypeKind::kInvalid || end.type.kind == ir::IRTypeKind::kInvalid ||
      start.type.is_placeholder || end.type.is_placeholder || !start.type.IsInteger() ||
      !end.type.IsInteger() || start.type.kind != end.type.kind) {
    diagnostics_.ReportError(range->loc, frontends::ErrorCode::kUnsupportedLowering,
                             "range bounds must lower to the same concrete integer IR type");
    return;
  }

  auto cond_bb = builder_.CreateBlock("for.cond");
  auto body_bb = builder_.CreateBlock("for.body");
  auto step_bb = builder_.CreateBlock("for.step");
  auto exit_bb = builder_.CreateBlock("for.exit");

  // Initialize iterator variable
  auto idx_alloca = builder_.MakeAlloca(start.type, for_stmt->iterator_name + ".idx");
  builder_.MakeStore(idx_alloca->name, start.value);
  builder_.MakeBranch(cond_bb.get());

  // Condition: check if index is within range
  builder_.SetInsertPoint(cond_bb);
  auto idx_load = builder_.MakeLoad(idx_alloca->name, start.type, "idx.val");
  auto cmp = builder_.MakeBinary(ir::BinaryInstruction::Op::kCmpSlt, idx_load->name, end.value,
                                 "for.cond.cmp");
  cmp->type = ir::IRType::I1();
  builder_.MakeCondBranch(cmp->name, body_bb.get(), exit_bb.get());

  // Body block
  builder_.SetInsertPoint(body_bb);
  auto shadowed_iterator = env_.find(for_stmt->iterator_name);
  const bool had_shadowed_iterator = shadowed_iterator != env_.end();
  EnvEntry saved_iterator;
  if (had_shadowed_iterator)
    saved_iterator = shadowed_iterator->second;
  env_[for_stmt->iterator_name] = EnvEntry{idx_load->name, start.type};
  terminated_ = false;
  break_targets_.push_back(exit_bb.get());
  continue_targets_.push_back(step_bb.get());
  LowerBlockStatements(for_stmt->body);
  continue_targets_.pop_back();
  break_targets_.pop_back();

  // A normal fallthrough and CONTINUE both execute the range step.
  if (!terminated_) {
    builder_.MakeBranch(step_bb.get());
  }

  builder_.SetInsertPoint(step_bb);
  auto idx_reload = builder_.MakeLoad(idx_alloca->name, start.type, "idx.next.load");
  auto inc = builder_.MakeBinary(ir::BinaryInstruction::Op::kAdd, idx_reload->name, "1", "idx.inc");
  inc->type = start.type;
  builder_.MakeStore(idx_alloca->name, inc->name);
  builder_.MakeBranch(cond_bb.get());

  builder_.SetInsertPoint(exit_bb);
  if (had_shadowed_iterator)
    env_[for_stmt->iterator_name] = saved_iterator;
  else
    env_.erase(for_stmt->iterator_name);
  terminated_ = false;
}

void PloyLowering::LowerMatchStatement(const std::shared_ptr<MatchStatement> &match_stmt) {
  // Admit only patterns whose predicate and bindings can be represented
  // exactly. OPTION/constructor, tuple, struct, and runtime type patterns
  // need layout or type-tag operations that this IR layer does not expose.
  // Reject them before emitting control flow; fabricating an always-true
  // predicate or binding the whole scrutinee would silently change semantics.
  std::function<const Pattern *(const std::shared_ptr<Pattern> &)> first_unsupported_pattern;
  first_unsupported_pattern = [&](const std::shared_ptr<Pattern> &pat) -> const Pattern * {
    if (!pat || std::dynamic_pointer_cast<WildcardPattern>(pat) ||
        std::dynamic_pointer_cast<IdentifierPattern>(pat) ||
        std::dynamic_pointer_cast<LiteralPattern>(pat) ||
        std::dynamic_pointer_cast<RangePattern>(pat)) {
      return nullptr;
    }
    if (auto bind = std::dynamic_pointer_cast<BindingPattern>(pat))
      return first_unsupported_pattern(bind->sub);
    if (auto or_pattern = std::dynamic_pointer_cast<OrPattern>(pat)) {
      for (const auto &alternative : or_pattern->alternatives) {
        if (const Pattern *unsupported = first_unsupported_pattern(alternative))
          return unsupported;
      }
      return nullptr;
    }
    return pat.get();
  };
  for (const auto &match_case : match_stmt->cases) {
    if (const Pattern *unsupported = first_unsupported_pattern(match_case.pattern)) {
      diagnostics_.ReportError(
          unsupported->loc, frontends::ErrorCode::kUnsupportedLowering,
          "pattern requires constructor, payload, aggregate, or runtime type-test lowering that "
          "the Poly IR ABI does not yet represent");
      return;
    }
  }

  EvalResult match_val = LowerExpression(match_stmt->value);
  if (match_val.type.kind == ir::IRTypeKind::kInvalid || match_val.type.is_placeholder) {
    diagnostics_.ReportError(match_stmt->loc, frontends::ErrorCode::kUnsupportedLowering,
                             "MATCH scrutinee has an unresolved type or ABI");
    return;
  }
  auto merge_bb = builder_.CreateBlock("match.merge");

  // Fast path: when every CASE pattern is a plain integer literal (and at
  // most one DEFAULT) we still emit an `ir::SwitchStatement` so that the
  // existing dense-switch tests in this suite continue to lower into a
  // jump table.  Anything richer (range, tuple, OR, binding, …) drops to
  // the structural if/else cascade below.
  auto is_simple_int_literal = [](const std::shared_ptr<Pattern> &p) -> bool {
    auto lit = std::dynamic_pointer_cast<LiteralPattern>(p);
    return lit && lit->literal && lit->literal->kind == Literal::Kind::kInteger;
  };

  bool all_simple = true;
  for (const auto &c : match_stmt->cases) {
    if (c.guard) { all_simple = false; break; }
    if (!c.pattern) continue; // DEFAULT
    // A wildcard `_` arm is fast-path-compatible: it serves as the
    // default target of the switch table, just like a `DEFAULT` arm.
    if (std::dynamic_pointer_cast<WildcardPattern>(c.pattern)) continue;
    if (!is_simple_int_literal(c.pattern)) { all_simple = false; break; }
  }

  if (all_simple) {
    std::vector<ir::SwitchStatement::Case> ir_cases;
    ir::BasicBlock *default_bb = merge_bb.get();
    std::vector<std::shared_ptr<ir::BasicBlock>> case_blocks;
    case_blocks.reserve(match_stmt->cases.size());
    for (size_t i = 0; i < match_stmt->cases.size(); ++i) {
      auto case_bb = builder_.CreateBlock("match.case." + std::to_string(i));
      case_blocks.push_back(case_bb);
      // Both `DEFAULT` and `CASE _` map to the switch's default target.
      if (!match_stmt->cases[i].pattern ||
          std::dynamic_pointer_cast<WildcardPattern>(
              match_stmt->cases[i].pattern)) {
        default_bb = case_bb.get();
        continue;
      }
      auto lit = std::dynamic_pointer_cast<LiteralPattern>(match_stmt->cases[i].pattern);
      char *end = nullptr;
      long long case_val = std::strtoll(lit->literal->value.c_str(), &end, 0);
      ir::SwitchStatement::Case sc;
      sc.value = case_val;
      sc.target = case_bb.get();
      ir_cases.push_back(sc);
    }
    builder_.MakeSwitch(match_val.value, ir_cases, default_bb);
    bool all_terminated = true;
    for (size_t i = 0; i < match_stmt->cases.size(); ++i) {
      builder_.SetInsertPoint(case_blocks[i]);
      terminated_ = false;
      LowerBlockStatements(match_stmt->cases[i].body);
      if (!terminated_) {
        all_terminated = false;
        builder_.MakeBranch(merge_bb.get());
      }
    }
    builder_.SetInsertPoint(merge_bb);
    terminated_ = all_terminated;
    if (terminated_) builder_.MakeUnreachable();
    return;
  }

  // Generic path (demand 2026-04-28-10): for every CASE we synthesise an
  // i1 predicate that says "does the scrutinee match this pattern?".
  // Cases are chained as nested `match.try.N` blocks with the body in
  // `match.body.N`; control falls through to the next `try` on a miss
  // and joins at `match.merge` on success.  Pattern-introduced bindings
  // are materialised before the body block by reusing the scrutinee SSA
  // value (no copy needed since `.poly` is immutable-by-default).

  // Helper that lowers a pattern into an i1 predicate against `match_val`.
  // Returns the SSA name of the predicate.  Bindings are recorded into
  // `bindings` for later application inside the body block.
  std::function<std::string(const std::shared_ptr<Pattern> &,
                            std::vector<std::pair<std::string, EvalResult>> &)>
      lower_predicate = [&](const std::shared_ptr<Pattern> &pat,
                            std::vector<std::pair<std::string, EvalResult>> &bindings) -> std::string {
    if (!pat || std::dynamic_pointer_cast<WildcardPattern>(pat)) {
      // Always-true literal i1.
      return "1";
    }
    if (auto id = std::dynamic_pointer_cast<IdentifierPattern>(pat)) {
      bindings.emplace_back(id->name, match_val);
      return "1";
    }
    if (auto lit = std::dynamic_pointer_cast<LiteralPattern>(pat)) {
      EvalResult lit_val = LowerLiteral(lit->literal);
      auto cmp = builder_.MakeBinary(ir::BinaryInstruction::Op::kCmpEq,
                                     match_val.value, lit_val.value, "match.eq");
      cmp->type = ir::IRType::I1();
      return cmp->name;
    }
    if (auto rng = std::dynamic_pointer_cast<RangePattern>(pat)) {
      EvalResult lo = LowerLiteral(rng->low);
      EvalResult hi = LowerLiteral(rng->high);
      auto ge = builder_.MakeBinary(ir::BinaryInstruction::Op::kCmpSge,
                                    match_val.value, lo.value, "match.ge");
      ge->type = ir::IRType::I1();
      auto cmp_hi_op = rng->inclusive ? ir::BinaryInstruction::Op::kCmpSle
                                       : ir::BinaryInstruction::Op::kCmpSlt;
      auto le = builder_.MakeBinary(cmp_hi_op, match_val.value, hi.value,
                                    rng->inclusive ? "match.le" : "match.lt");
      le->type = ir::IRType::I1();
      auto andv = builder_.MakeBinary(ir::BinaryInstruction::Op::kAnd, ge->name,
                                      le->name, "match.in_range");
      andv->type = ir::IRType::I1();
      return andv->name;
    }
    if (auto orp = std::dynamic_pointer_cast<OrPattern>(pat)) {
      // Bindings produced by an or-pattern come from the *first* branch
      // (sema has already verified all alternatives bind the same names);
      // for the body the binding source value is the scrutinee, so the
      // discriminant is uniform regardless of which branch matched.
      std::string acc;
      for (size_t i = 0; i < orp->alternatives.size(); ++i) {
        std::vector<std::pair<std::string, EvalResult>> alt_bindings;
        std::string alt = lower_predicate(orp->alternatives[i], alt_bindings);
        if (i == 0) {
          acc = alt;
          for (auto &b : alt_bindings) bindings.push_back(std::move(b));
        } else {
          auto orv = builder_.MakeBinary(ir::BinaryInstruction::Op::kOr, acc, alt,
                                         "match.or");
          orv->type = ir::IRType::I1();
          acc = orv->name;
        }
      }
      return acc.empty() ? std::string("0") : acc;
    }
    if (auto bind = std::dynamic_pointer_cast<BindingPattern>(pat)) {
      bindings.emplace_back(bind->name, match_val);
      if (bind->sub) return lower_predicate(bind->sub, bindings);
      return "1";
    }
    // Constructor/aggregate/type patterns are unreachable after preflight.
    return "0";
  };

  bool all_terminated = true;
  for (size_t i = 0; i < match_stmt->cases.size(); ++i) {
    const auto &mc = match_stmt->cases[i];
    auto body_bb = builder_.CreateBlock("match.body." + std::to_string(i));
    auto next_bb = (i + 1 == match_stmt->cases.size())
                       ? merge_bb
                       : builder_.CreateBlock("match.try." + std::to_string(i + 1));

    std::vector<std::pair<std::string, EvalResult>> bindings;
    std::string pred;
    if (!mc.pattern) {
      // DEFAULT: unconditional branch into body.
      pred = "1";
    } else {
      pred = lower_predicate(mc.pattern, bindings);
    }
    if (mc.guard) {
      // The guard expression executes inside a *probe* block so any
      // bindings introduced by the pattern can be referenced from the
      // guard.  We materialise bindings, evaluate the guard, then use
      // (pred AND guard_i1) as the actual branch predicate.
      auto guard_bb = builder_.CreateBlock("match.guard." + std::to_string(i));
      builder_.MakeCondBranch(pred, guard_bb.get(), next_bb.get());
      builder_.SetInsertPoint(guard_bb);
      // Install bindings into `env_` so the guard sees them.
      std::vector<std::pair<std::string, EnvEntry>> shadowed;
      for (auto &b : bindings) {
        auto it = env_.find(b.first);
        if (it != env_.end()) shadowed.emplace_back(b.first, it->second);
        env_[b.first] = EnvEntry{b.second.value, b.second.type, false};
      }
      EvalResult guard_val = LowerExpression(mc.guard);
      std::string guard_i1 = EnsureI1(guard_val, mc.guard->loc);
      builder_.MakeCondBranch(guard_i1, body_bb.get(), next_bb.get());
      // Restore env_ before falling through to the next try block.
      for (auto &b : bindings) env_.erase(b.first);
      for (auto &kv : shadowed) env_[kv.first] = kv.second;
    } else {
      builder_.MakeCondBranch(pred, body_bb.get(), next_bb.get());
    }

    // Body block.
    builder_.SetInsertPoint(body_bb);
    terminated_ = false;
    std::vector<std::pair<std::string, EnvEntry>> shadowed_body;
    for (auto &b : bindings) {
      auto it = env_.find(b.first);
      if (it != env_.end()) shadowed_body.emplace_back(b.first, it->second);
      env_[b.first] = EnvEntry{b.second.value, b.second.type, false};
    }
    LowerBlockStatements(mc.body);
    for (auto &b : bindings) env_.erase(b.first);
    for (auto &kv : shadowed_body) env_[kv.first] = kv.second;
    if (!terminated_) {
      all_terminated = false;
      builder_.MakeBranch(merge_bb.get());
    }

    if (next_bb != merge_bb) {
      builder_.SetInsertPoint(next_bb);
    }
  }

  builder_.SetInsertPoint(merge_bb);
  terminated_ = all_terminated && match_stmt->cases.size() > 0 &&
                std::all_of(match_stmt->cases.begin(), match_stmt->cases.end(),
                            [](const MatchStatement::Case &c) {
                              return c.pattern == nullptr ||
                                     std::dynamic_pointer_cast<WildcardPattern>(c.pattern) ||
                                     std::dynamic_pointer_cast<IdentifierPattern>(c.pattern);
                            });
  if (terminated_) builder_.MakeUnreachable();
}

void PloyLowering::LowerReturnStatement(const std::shared_ptr<ReturnStatement> &ret) {
  if (ret->value) {
    EvalResult val = LowerExpression(ret->value);
    if (!current_function_ || current_function_->ret_type.kind == ir::IRTypeKind::kVoid ||
        current_function_->ret_type.kind == ir::IRTypeKind::kInvalid) {
      diagnostics_.ReportError(ret->loc, frontends::ErrorCode::kUnsupportedLowering,
                               "cannot return a value without a concrete non-void return ABI");
      return;
    }
    if (val.type.kind == ir::IRTypeKind::kInvalid || val.type.is_placeholder) {
      diagnostics_.ReportError(ret->loc, frontends::ErrorCode::kUnsupportedLowering,
                               "cannot return a value whose type or ABI is unresolved");
      return;
    }
    std::string return_value = val.value;
    if (!val.type.SameShape(current_function_->ret_type)) {
      return_value = "return.converted." + std::to_string(generated_name_index_++);
      if (!GenerateMarshalCode(val.value, val.type, current_function_->ret_type, return_value,
                               ret->loc, "function return")) {
        return;
      }
    }
    builder_.MakeReturn(return_value);
  } else {
    if (current_function_ && current_function_->ret_type.kind != ir::IRTypeKind::kVoid) {
      diagnostics_.ReportError(ret->loc, frontends::ErrorCode::kUnsupportedLowering,
                               "cannot emit a value-less return from a non-void function");
      return;
    }
    builder_.MakeReturn();
  }
  terminated_ = true;
}

// PRINTLN "literal";  — Stage B3 of the runtime-stdout pipeline
// (demand 2026-04-28-49).
//
// We lower a `PrintlnStmt` into:
//   1. A *decoded* interned string constant in the global pool (the front-end
//      kept escape sequences verbatim by design — the codegen-side decoder
//      lives here so the IR carries true bytes, not source spellings).
//   2. A direct call into the runtime: `polyrt_println(i8* msg, i64 len)`.
//      The callee is intentionally external; B5 will wire it up to the real
//      runtime DLL via polyld's import table.
//
// Pointer + length is preferred over a NUL-terminated convention so embedded
// `\0` bytes round-trip cleanly and so empty literals (`PRINTLN "";`) still
// produce a single, unambiguous zero-length WriteFile call downstream.
namespace {

// Decode the small, fixed set of backslash escapes that .poly promises to
// support today (\n, \r, \t, \\, \", \0, plus `\xHH` two-digit hex). Any
// unrecognised escape after a backslash is preserved verbatim so the IR
// dump still resembles the source — the alternative (silently dropping
// the backslash) would mask front-end bugs. Diagnostics are reported on
// the supplied `report` callback so the lowering layer can attach the
// PRINTLN's source location uniformly.
std::string DecodePrintlnLiteral(const std::string &raw,
                                 const std::function<void(const std::string &)> &report) {
  std::string out;
  out.reserve(raw.size());
  for (size_t i = 0; i < raw.size(); ++i) {
    char c = raw[i];
    if (c != '\\' || i + 1 >= raw.size()) {
      out.push_back(c);
      continue;
    }
    char esc = raw[++i];
    switch (esc) {
    case 'n':
      out.push_back('\n');
      break;
    case 'r':
      out.push_back('\r');
      break;
    case 't':
      out.push_back('\t');
      break;
    case '\\':
      out.push_back('\\');
      break;
    case '"':
      out.push_back('"');
      break;
    case '0':
      out.push_back('\0');
      break;
    case 'x': {
      // Exactly two hex digits required.
      if (i + 2 >= raw.size() || !std::isxdigit(static_cast<unsigned char>(raw[i + 1])) ||
          !std::isxdigit(static_cast<unsigned char>(raw[i + 2]))) {
        report("malformed \\xHH escape in PRINTLN literal");
        out.push_back('\\');
        out.push_back(esc);
        break;
      }
      auto hex_val = [](char h) -> int {
        if (h >= '0' && h <= '9')
          return h - '0';
        if (h >= 'a' && h <= 'f')
          return 10 + (h - 'a');
        return 10 + (h - 'A');
      };
      int byte = (hex_val(raw[i + 1]) << 4) | hex_val(raw[i + 2]);
      out.push_back(static_cast<char>(byte));
      i += 2;
      break;
    }
    default:
      report("unknown escape '\\" + std::string(1, esc) + "' in PRINTLN literal");
      out.push_back('\\');
      out.push_back(esc);
      break;
    }
  }
  return out;
}

} // namespace

void PloyLowering::LowerPrintlnStatement(const std::shared_ptr<PrintlnStmt> &println) {
  if (!println)
    return;

  const std::string decoded = DecodePrintlnLiteral(
      println->message, [&](const std::string &msg) {
        // Unknown / malformed escape: emit a non-fatal warning so the program
        // still lowers (the bytes are preserved verbatim) but the user sees
        // the issue. We piggy-back on the generic warning code rather than
        // mint a new one for B3.
        diagnostics_.ReportWarning(println->loc,
                                   frontends::ErrorCode::kGenericWarning, msg);
      });

  // 1) Intern the bytes as a global; MakeStringLiteral returns the i8* symbol.
  const std::string ptr_name = builder_.MakeStringLiteral(decoded, "println.msg");

  // 2) Emit the runtime call. Length is passed as an integer immediate using
  //    the literal-as-name convention shared with LowerLiteral(kInteger); the
  //    callee_type is left invalid since the symbol is resolved by the linker.
  std::vector<std::string> args;
  args.push_back(ptr_name);
  args.push_back(std::to_string(decoded.size()));
  builder_.MakeCall("polyrt_println", args, ir::IRType::Void());
}

void PloyLowering::LowerBlockStatements(const std::vector<std::shared_ptr<Statement>> &stmts) {
  for (const auto &stmt : stmts) {
    if (terminated_)
      break;
    LowerStatement(stmt);
  }
}

// ============================================================================
// Expression Lowering
// ============================================================================

PloyLowering::EvalResult PloyLowering::LowerExpression(const std::shared_ptr<Expression> &expr) {
  if (!expr)
    return {"", ir::IRType::Invalid()};

  if (auto id = std::dynamic_pointer_cast<Identifier>(expr)) {
    return LowerIdentifier(id);
  }
  if (auto lit = std::dynamic_pointer_cast<Literal>(expr)) {
    return LowerLiteral(lit);
  }
  // Template strings are folded only when every interpolation is a literal.
  // Dropping runtime-valued parts would produce a different string, so that
  // path is a lowering error until a formatting runtime ABI exists.
  if (auto tmpl = std::dynamic_pointer_cast<TemplateString>(expr)) {
    std::string formatted;
    bool runtime_seen = false;
    auto strip_quotes = [](const std::string &q) {
      if (q.size() >= 2 && q.front() == '"' && q.back() == '"')
        return q.substr(1, q.size() - 2);
      return q;
    };
    for (const auto &part : tmpl->parts) {
      if (part.is_text) {
        formatted += strip_quotes(part.text);
        continue;
      }
      auto inner = std::dynamic_pointer_cast<Literal>(part.expr);
      if (!inner) {
        runtime_seen = true;
        continue;
      }
      switch (inner->kind) {
      case Literal::Kind::kInteger:
      case Literal::Kind::kFloat:
      case Literal::Kind::kBool:
        formatted += inner->value;
        break;
      case Literal::Kind::kString:
        formatted += strip_quotes(inner->value);
        break;
      case Literal::Kind::kNull:
        formatted += "null";
        break;
      }
    }
    if (runtime_seen) {
      diagnostics_.ReportError(
          tmpl->loc, frontends::ErrorCode::kUnsupportedLowering,
          "template string runtime interpolation requires a formatting ABI; refusing to drop "
          "the interpolated value");
      return {"", ir::IRType::Invalid()};
    }
    std::string sym = builder_.MakeStringLiteral(formatted, "template.str");
    return {sym, ir::IRType::Pointer(ir::IRType::I8())};
  }
  if (auto bin = std::dynamic_pointer_cast<BinaryExpression>(expr)) {
    return LowerBinaryExpression(bin);
  }
  if (auto unary = std::dynamic_pointer_cast<UnaryExpression>(expr)) {
    return LowerUnaryExpression(unary);
  }
  if (auto await_expr = std::dynamic_pointer_cast<AwaitExpression>(expr)) {
    return LowerAwaitExpression(await_expr);
  }
  if (auto unwrap_expr = std::dynamic_pointer_cast<OptionUnwrapExpression>(expr)) {
    return LowerOptionUnwrapExpression(unwrap_expr);
  }
  if (auto call = std::dynamic_pointer_cast<CallExpression>(expr)) {
    return LowerCallExpression(call);
  }
  if (auto cross_call = std::dynamic_pointer_cast<CrossLangCallExpression>(expr)) {
    return LowerCrossLangCall(cross_call);
  }
  if (auto new_expr = std::dynamic_pointer_cast<NewExpression>(expr)) {
    return LowerNewExpression(new_expr);
  }
  if (auto method_call = std::dynamic_pointer_cast<MethodCallExpression>(expr)) {
    return LowerMethodCallExpression(method_call);
  }
  if (auto get_attr = std::dynamic_pointer_cast<GetAttrExpression>(expr)) {
    return LowerGetAttrExpression(get_attr);
  }
  if (auto set_attr = std::dynamic_pointer_cast<SetAttrExpression>(expr)) {
    return LowerSetAttrExpression(set_attr);
  }
  if (std::dynamic_pointer_cast<MemberExpression>(expr)) {
    diagnostics_.ReportError(expr->loc, frontends::ErrorCode::kUnsupportedLowering,
                             "member access has no semantics-preserving Poly IR lowering");
    return {"", ir::IRType::Invalid()};
  }
  if (std::dynamic_pointer_cast<IndexExpression>(expr)) {
    diagnostics_.ReportError(expr->loc, frontends::ErrorCode::kUnsupportedLowering,
                             "index access requires a container layout/access ABI");
    return {"", ir::IRType::Invalid()};
  }
  if (auto qid = std::dynamic_pointer_cast<QualifiedIdentifier>(expr)) {
    // Qualified identifiers are treated as external references.
    // Resolve the type from the sema symbol table if available.
    std::string sym = qid->qualifier + "_" + qid->name;
    for (char &c : sym) {
      if (c == ':')
        c = '_';
    }
    ir::IRType qid_type = ir::IRType::I64(true);
    auto sym_it = sema_.Symbols().find(qid->qualifier + "::" + qid->name);
    if (sym_it != sema_.Symbols().end() && sym_it->second.type.kind != core::TypeKind::kAny &&
        sym_it->second.type.kind != core::TypeKind::kUnknown &&
        sym_it->second.type.kind != core::TypeKind::kInvalid) {
      qid_type = CoreTypeToIR(sym_it->second.type);
    }
    return {sym, qid_type};
  }
  if (auto range = std::dynamic_pointer_cast<RangeExpression>(expr)) {
    diagnostics_.ReportError(range->loc, frontends::ErrorCode::kUnsupportedLowering,
                             "range values are only representable as the iterable of FOR IN");
    return {"", ir::IRType::Invalid()};
  }

  if (auto conv = std::dynamic_pointer_cast<ConvertExpression>(expr)) {
    return LowerConvertExpression(conv);
  }
  if (auto list_lit = std::dynamic_pointer_cast<ListLiteral>(expr)) {
    return LowerListLiteral(list_lit);
  }
  if (auto tuple_lit = std::dynamic_pointer_cast<TupleLiteral>(expr)) {
    return LowerTupleLiteral(tuple_lit);
  }
  if (auto dict_lit = std::dynamic_pointer_cast<DictLiteral>(expr)) {
    return LowerDictLiteral(dict_lit);
  }
  if (auto struct_lit = std::dynamic_pointer_cast<StructLiteral>(expr)) {
    return LowerStructLiteral(struct_lit);
  }
  if (auto del_expr = std::dynamic_pointer_cast<DeleteExpression>(expr)) {
    return LowerDeleteExpression(del_expr);
  }
  if (auto named_arg = std::dynamic_pointer_cast<NamedArgument>(expr)) {
    // Lower the value expression.  Attach the argument name as IR metadata
    // so that the linker / call-site can reorder arguments to match the
    // target function's parameter order.
    EvalResult val = LowerExpression(named_arg->value);
    // Record the mapping from this SSA value to the named-arg label.
    // The name is stored in a side table keyed by SSA value name.
    named_arg_labels_[val.value] = named_arg->name;
    return val;
  }

  diagnostics_.ReportError(expr->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "Poly expression has no semantics-preserving IR lowering");
  return {"", ir::IRType::Invalid()};
}

PloyLowering::EvalResult PloyLowering::LowerIdentifier(const std::shared_ptr<Identifier> &id) {
  auto it = env_.find(id->name);
  if (it != env_.end()) {
    if (it->second.is_mutable) {
      // Mutable VAR: load the current value from its alloca
      auto load = builder_.MakeLoad(it->second.ir_name, it->second.type);
      return {load->name, it->second.type};
    }
    return {it->second.ir_name, it->second.type};
  }
  Report(id->loc, "undefined variable '" + id->name + "' during lowering");
  return {"undef", ir::IRType::Invalid()};
}

PloyLowering::EvalResult PloyLowering::LowerLiteral(const std::shared_ptr<Literal> &lit) {
  switch (lit->kind) {
  case Literal::Kind::kInteger: {
    return {lit->value, ir::IRType::I64(true)};
  }
  case Literal::Kind::kFloat: {
    char *end = nullptr;
    double dval = std::strtod(lit->value.c_str(), &end);
    auto flit = builder_.MakeLiteral(dval);
    return {flit->name, ir::IRType::F64()};
  }
  case Literal::Kind::kString: {
    // Strip quotes and intern the string
    std::string str_val = lit->value;
    if (str_val.size() >= 2 && str_val.front() == '"' && str_val.back() == '"') {
      str_val = str_val.substr(1, str_val.size() - 2);
    }
    std::string sym = builder_.MakeStringLiteral(str_val, "str");
    return {sym, ir::IRType::Pointer(ir::IRType::I8())};
  }
  case Literal::Kind::kBool: {
    std::string val = (lit->value == "true") ? "1" : "0";
    return {val, ir::IRType::I1()};
  }
  case Literal::Kind::kNull: {
    return {"0", ir::IRType::Pointer(ir::IRType::Void())};
  }
  }
  return {"undef", ir::IRType::Invalid()};
}

PloyLowering::EvalResult PloyLowering::LowerBinaryExpression(
    const std::shared_ptr<BinaryExpression> &bin) {
  // Handle assignment specially
  if (bin->op == "=") {
    EvalResult rhs = LowerExpression(bin->right);
    if (auto id = std::dynamic_pointer_cast<Identifier>(bin->left)) {
      auto it = env_.find(id->name);
      if (it != env_.end() && it->second.is_mutable) {
        if (rhs.type.kind == ir::IRTypeKind::kInvalid || rhs.type.is_placeholder) {
          diagnostics_.ReportError(bin->loc, frontends::ErrorCode::kUnsupportedLowering,
                                   "assignment to '" + id->name +
                                       "' has an unresolved value type or ABI");
          return {"", ir::IRType::Invalid()};
        }
        std::string stored_value = rhs.value;
        if (!rhs.type.SameShape(it->second.type)) {
          stored_value = id->name + ".assigned.converted." +
                         std::to_string(generated_name_index_++);
          if (!GenerateMarshalCode(rhs.value, rhs.type, it->second.type, stored_value, bin->loc,
                                   "assignment to variable '" + id->name + "'")) {
            return {"", ir::IRType::Invalid()};
          }
        }
        // Mutable VAR: store to the alloca address
        builder_.MakeStore(it->second.ir_name, stored_value);
        // A mutable binding's declared storage type never changes.
        return {stored_value, it->second.type};
      } else {
        // Immutable LET or unknown: direct SSA rebind
        env_[id->name] = EnvEntry{rhs.value, rhs.type, false};
      }
      return rhs;
    }
    return rhs;
  }

  // Logical operators are control-flow operations, not eager bitwise
  // operators.  The RHS block is only reachable when its value is needed.
  if (bin->op == "&&" || bin->op == "||") {
    EvalResult left = LowerExpression(bin->left);
    std::string left_bool =
        EnsureI1(left, bin->left ? bin->left->loc : bin->loc);
    if (left_bool.empty())
      return {"", ir::IRType::Invalid()};

    auto result_slot = builder_.MakeAlloca(
        ir::IRType::I1(), "logic.result.addr." + std::to_string(generated_name_index_++));
    builder_.MakeStore(result_slot->name, bin->op == "&&" ? "0" : "1");
    auto rhs_bb = builder_.CreateBlock(bin->op == "&&" ? "logic.and.rhs" : "logic.or.rhs");
    auto merge_bb =
        builder_.CreateBlock(bin->op == "&&" ? "logic.and.merge" : "logic.or.merge");

    if (bin->op == "&&")
      builder_.MakeCondBranch(left_bool, rhs_bb.get(), merge_bb.get());
    else
      builder_.MakeCondBranch(left_bool, merge_bb.get(), rhs_bb.get());

    builder_.SetInsertPoint(rhs_bb);
    EvalResult right = LowerExpression(bin->right);
    std::string right_bool =
        EnsureI1(right, bin->right ? bin->right->loc : bin->loc);
    if (right_bool.empty()) {
      builder_.MakeBranch(merge_bb.get());
      builder_.SetInsertPoint(merge_bb);
      return {"", ir::IRType::Invalid()};
    }
    builder_.MakeStore(result_slot->name, right_bool);
    builder_.MakeBranch(merge_bb.get());

    builder_.SetInsertPoint(merge_bb);
    auto result = builder_.MakeLoad(
        result_slot->name, ir::IRType::I1(),
        "logic.result." + std::to_string(generated_name_index_++));
    return {result->name, ir::IRType::I1()};
  }

  EvalResult left = LowerExpression(bin->left);
  EvalResult right = LowerExpression(bin->right);

  // Unknown cross-language ABI values may be carried through permissive
  // compatibility mode, but interpreting their bits as a number changes the
  // program's meaning. Require a concrete type before selecting an operator.
  if (left.type.kind == ir::IRTypeKind::kInvalid ||
      right.type.kind == ir::IRTypeKind::kInvalid || left.type.is_placeholder ||
      right.type.is_placeholder || left.value.empty() || right.value.empty()) {
    diagnostics_.ReportError(
        bin->loc, frontends::ErrorCode::kUnsupportedLowering,
        "binary operator '" + bin->op + "' cannot interpret an unresolved value or ABI");
    return {"", ir::IRType::Invalid()};
  }

  auto is_float_type = [](const ir::IRType &t) -> bool {
    return t.kind == ir::IRTypeKind::kF32 || t.kind == ir::IRTypeKind::kF64;
  };
  auto is_pointer_type = [](const ir::IRType &t) -> bool {
    return t.kind == ir::IRTypeKind::kPointer || t.kind == ir::IRTypeKind::kReference;
  };

  ir::IRType effective_left = left.type;
  ir::IRType effective_right = right.type;
  std::string left_val = left.value;
  std::string right_val = right.value;

  if (is_pointer_type(left.type) || is_pointer_type(right.type)) {
    if (!is_pointer_type(left.type) || !is_pointer_type(right.type) ||
        (bin->op != "==" && bin->op != "!=")) {
      diagnostics_.ReportError(
          bin->loc, frontends::ErrorCode::kUnsupportedLowering,
          "only identity equality is defined for pointer/handle values in Poly IR lowering");
      return {"", ir::IRType::Invalid()};
    }
    auto left_int = builder_.MakeCast(ir::CastInstruction::CastKind::kPtrToInt, left.value,
                                      ir::IRType::I64(false));
    auto right_int = builder_.MakeCast(ir::CastInstruction::CastKind::kPtrToInt, right.value,
                                       ir::IRType::I64(false));
    auto cmp = builder_.MakeBinary(bin->op == "==" ? ir::BinaryInstruction::Op::kCmpEq
                                                   : ir::BinaryInstruction::Op::kCmpNe,
                                   left_int->name, right_int->name, "ptr.eq");
    cmp->type = ir::IRType::I1();
    return {cmp->name, cmp->type};
  }

  if ((!left.type.IsInteger() && !left.type.IsFloat()) ||
      (!right.type.IsInteger() && !right.type.IsFloat()) ||
      (left.type.IsInteger() != right.type.IsInteger())) {
    diagnostics_.ReportError(
        bin->loc, frontends::ErrorCode::kUnsupportedLowering,
        "binary operator '" + bin->op +
            "' requires both operands to be integers or both operands to be floats; "
            "the IR has no semantics-preserving implicit integer/float cast");
    return {"", ir::IRType::Invalid()};
  }

  // Promote operands to one concrete width before selecting the operation.
  // Mixed signed integers follow the usual value-preserving rule: a wider
  // signed type wins; otherwise the common width is unsigned.
  ir::IRType common_type = left.type;
  if (left.type.IsInteger()) {
    const int left_bits = left.type.BitWidth();
    const int right_bits = right.type.BitWidth();
    const int common_bits = std::max(left_bits, right_bits);
    bool common_signed = left.type.is_signed == right.type.is_signed
                             ? left.type.is_signed
                             : ((left.type.is_signed && left_bits > right_bits) ||
                                (right.type.is_signed && right_bits > left_bits));
    switch (common_bits) {
    case 1:
      common_type = ir::IRType::I1();
      break;
    case 8:
      common_type = ir::IRType::I8(common_signed);
      break;
    case 16:
      common_type = ir::IRType::I16(common_signed);
      break;
    case 32:
      common_type = ir::IRType::I32(common_signed);
      break;
    case 64:
      common_type = ir::IRType::I64(common_signed);
      break;
    default:
      diagnostics_.ReportError(bin->loc, frontends::ErrorCode::kUnsupportedLowering,
                               "integer operation uses a width not represented by Poly IR");
      return {"", ir::IRType::Invalid()};
    }
  } else {
    common_type = left.type.BitWidth() >= right.type.BitWidth() ? left.type : right.type;
  }

  if (!left.type.SameShape(common_type)) {
    left_val = "binary.left.converted." + std::to_string(generated_name_index_++);
    if (!GenerateMarshalCode(left.value, left.type, common_type, left_val, bin->loc,
                             "left operand of '" + bin->op + "'"))
      return {"", ir::IRType::Invalid()};
    effective_left = common_type;
  }
  if (!right.type.SameShape(common_type)) {
    right_val = "binary.right.converted." + std::to_string(generated_name_index_++);
    if (!GenerateMarshalCode(right.value, right.type, common_type, right_val, bin->loc,
                             "right operand of '" + bin->op + "'"))
      return {"", ir::IRType::Invalid()};
    effective_right = common_type;
  }

  // Determine operation
  ir::BinaryInstruction::Op op;
  ir::IRType result_type = common_type;
  bool is_float = is_float_type(common_type);
  bool is_signed = common_type.IsSigned();

  if (bin->op == "+") {
    op = is_float ? ir::BinaryInstruction::Op::kFAdd : ir::BinaryInstruction::Op::kAdd;
  } else if (bin->op == "-") {
    op = is_float ? ir::BinaryInstruction::Op::kFSub : ir::BinaryInstruction::Op::kSub;
  } else if (bin->op == "*") {
    op = is_float ? ir::BinaryInstruction::Op::kFMul : ir::BinaryInstruction::Op::kMul;
  } else if (bin->op == "/") {
    op = is_float ? ir::BinaryInstruction::Op::kFDiv
                  : (is_signed ? ir::BinaryInstruction::Op::kSDiv
                               : ir::BinaryInstruction::Op::kUDiv);
  } else if (bin->op == "%") {
    op = is_float ? ir::BinaryInstruction::Op::kFRem
                  : (is_signed ? ir::BinaryInstruction::Op::kSRem
                               : ir::BinaryInstruction::Op::kURem);
  } else if (bin->op == "==") {
    op = is_float ? ir::BinaryInstruction::Op::kCmpFoe : ir::BinaryInstruction::Op::kCmpEq;
    result_type = ir::IRType::I1();
  } else if (bin->op == "!=") {
    op = is_float ? ir::BinaryInstruction::Op::kCmpFne : ir::BinaryInstruction::Op::kCmpNe;
    result_type = ir::IRType::I1();
  } else if (bin->op == "<") {
    op = is_float ? ir::BinaryInstruction::Op::kCmpFlt
                  : (is_signed ? ir::BinaryInstruction::Op::kCmpSlt
                               : ir::BinaryInstruction::Op::kCmpUlt);
    result_type = ir::IRType::I1();
  } else if (bin->op == ">") {
    op = is_float ? ir::BinaryInstruction::Op::kCmpFgt
                  : (is_signed ? ir::BinaryInstruction::Op::kCmpSgt
                               : ir::BinaryInstruction::Op::kCmpUgt);
    result_type = ir::IRType::I1();
  } else if (bin->op == "<=") {
    op = is_float ? ir::BinaryInstruction::Op::kCmpFle
                  : (is_signed ? ir::BinaryInstruction::Op::kCmpSle
                               : ir::BinaryInstruction::Op::kCmpUle);
    result_type = ir::IRType::I1();
  } else if (bin->op == ">=") {
    op = is_float ? ir::BinaryInstruction::Op::kCmpFge
                  : (is_signed ? ir::BinaryInstruction::Op::kCmpSge
                               : ir::BinaryInstruction::Op::kCmpUge);
    result_type = ir::IRType::I1();
  } else {
    Report(bin->loc, "unsupported binary operator '" + bin->op + "'");
    return {"undef", ir::IRType::Invalid()};
  }

  auto inst = builder_.MakeBinary(op, left_val, right_val, "");
  inst->type = result_type;
  return {inst->name, result_type};
}

PloyLowering::EvalResult PloyLowering::LowerUnaryExpression(
    const std::shared_ptr<UnaryExpression> &unary) {
  EvalResult operand = LowerExpression(unary->operand);

  if (unary->op == "-") {
    // Negate: 0 - operand
    bool is_float =
        (operand.type.kind == ir::IRTypeKind::kF32 || operand.type.kind == ir::IRTypeKind::kF64);
    auto op = is_float ? ir::BinaryInstruction::Op::kFSub : ir::BinaryInstruction::Op::kSub;
    auto inst = builder_.MakeBinary(op, "0", operand.value, "neg");
    inst->type = operand.type;
    return {inst->name, operand.type};
  }

  if (unary->op == "!") {
    // Logical not: xor with 1
    auto inst = builder_.MakeBinary(ir::BinaryInstruction::Op::kXor, operand.value, "1", "not");
    inst->type = ir::IRType::I1();
    return {inst->name, ir::IRType::I1()};
  }

  return operand;
}

PloyLowering::EvalResult PloyLowering::LowerCallExpression(
    const std::shared_ptr<CallExpression> &call) {
  // Lower arguments
  std::vector<std::string> arg_names;
  for (const auto &arg : call->args) {
    EvalResult a = LowerExpression(arg);
    arg_names.push_back(a.value);
  }

  // Get callee name
  std::string callee_name;
  if (auto id = std::dynamic_pointer_cast<Identifier>(call->callee)) {
    callee_name = id->name;
  } else if (auto qid = std::dynamic_pointer_cast<QualifiedIdentifier>(call->callee)) {
    callee_name = qid->qualifier + "_" + qid->name;
    for (char &c : callee_name) {
      if (c == ':')
        c = '_';
    }
  } else {
    EvalResult callee = LowerExpression(call->callee);
    callee_name = callee.value;
  }

  // Reorder arguments based on named-argument labels when the target
  // function's signature is known.  Positional arguments keep their
  // original order; named arguments are placed at the position matching
  // the parameter name in the signature.  When the call site omits a
  // trailing parameter that carries a default value, the default
  // expression is materialised inline at this call site (sema has
  // already validated that the default is a constant-foldable
  // expression so re-evaluating it here is observably equivalent).
  auto sig_it = sema_.KnownSignatures().find(callee_name);
  if (sig_it != sema_.KnownSignatures().end() && !sig_it->second.param_names.empty()) {
    const auto &param_names = sig_it->second.param_names;
    const auto &param_defaults = sig_it->second.param_default_values;
    const size_t total = param_names.size();
    std::vector<std::string> reordered(total);
    std::vector<bool> placed(total, false);

    // First pass: route named arguments to their declared slot.
    for (size_t i = 0; i < arg_names.size(); ++i) {
      auto lbl_it = named_arg_labels_.find(arg_names[i]);
      if (lbl_it != named_arg_labels_.end()) {
        for (size_t j = 0; j < total; ++j) {
          if (param_names[j] == lbl_it->second) {
            reordered[j] = arg_names[i];
            placed[j] = true;
            break;
          }
        }
      }
    }
    // Second pass: fill remaining slots with positional args in order.
    size_t pos = 0;
    for (size_t i = 0; i < arg_names.size(); ++i) {
      auto lbl_it = named_arg_labels_.find(arg_names[i]);
      if (lbl_it == named_arg_labels_.end()) {
        while (pos < total && placed[pos])
          ++pos;
        if (pos < total) {
          reordered[pos] = arg_names[i];
          placed[pos] = true;
          ++pos;
        }
      }
    }
    // Third pass: any still-empty slot must be covered by a default.
    for (size_t j = 0; j < total; ++j) {
      if (!placed[j] && j < param_defaults.size() && param_defaults[j]) {
        EvalResult def = LowerExpression(param_defaults[j]);
        reordered[j] = def.value;
        placed[j] = true;
      }
    }
    arg_names = reordered;
  }

  // Resolve the return type from sema's known signatures. In the documented
  // opt-in non-strict mode an unresolved call may still be emitted as a
  // placeholder so legacy code can pass or discard it, but consumers may not
  // interpret that placeholder as a concrete value.
  ir::IRType call_ret_type = ir::IRType::Invalid();
  {
    auto sig_it = sema_.KnownSignatures().find(callee_name);
    if (sig_it != sema_.KnownSignatures().end() &&
        sig_it->second.return_type.kind != core::TypeKind::kAny &&
        sig_it->second.return_type.kind != core::TypeKind::kUnknown &&
        sig_it->second.return_type.kind != core::TypeKind::kInvalid) {
      call_ret_type = CoreTypeToIR(sig_it->second.return_type);
    } else {
      // Also check the sema symbol table for function types
      auto sym_it = sema_.Symbols().find(callee_name);
      if (sym_it != sema_.Symbols().end() &&
          sym_it->second.type.kind == core::TypeKind::kFunction &&
          !sym_it->second.type.type_args.empty()) {
        // The last type_arg of a function type is the return type
        call_ret_type = CoreTypeToIR(sym_it->second.type.type_args.back());
      }
    }
  }
  if (call_ret_type.kind == ir::IRTypeKind::kInvalid) {
    if (sema_.IsStrictMode()) {
      diagnostics_.ReportError(call->loc, frontends::ErrorCode::kUnsupportedLowering,
                               "call to '" + callee_name +
                                   "' has no resolved return ABI in strict mode");
      return {"", ir::IRType::Invalid()};
    }
    call_ret_type = ir::IRType::I64(true);
    call_ret_type.is_placeholder = true;
    diagnostics_.ReportWarning(
        call->loc, frontends::ErrorCode::kOpaqueTypeFallback,
        "call to '" + callee_name +
            "' has no resolved return ABI; emitting an opt-in non-strict placeholder");
  }

  // Source-facing file helpers use friendly names, while every frontend and
  // target shares a stable C ABI symbol.  The backend sees these names and
  // automatically appends the syscall-only implementation to the object.
  std::string native_callee = callee_name;
  const auto unwrap_string_literal = [](std::string &arg) {
    // MakeStringLiteral retains a `.ptr` ConstantGEP alias for the historical
    // println linker.  Native calls need the address of the character bytes,
    // not the address of that pointer slot.  A direct literal is identifiable
    // by this suffix, so route it to the underlying data symbol.  Non-literal
    // string expressions already evaluate to an actual pointer and stay
    // untouched.
    if (arg.size() > 4 && arg.compare(arg.size() - 4, 4, ".ptr") == 0)
      arg.resize(arg.size() - 4);
  };
  if (callee_name == "file_open_ints") {
    native_callee = "polyrt_open_read";
    if (!arg_names.empty())
      unwrap_string_literal(arg_names.front());
  } else if (callee_name == "file_open_write") {
    native_callee = "polyrt_open_write";
    if (!arg_names.empty())
      unwrap_string_literal(arg_names.front());
  } else if (callee_name == "file_next_int")
    native_callee = "polyrt_read_i64_or";
  else if (callee_name == "file_write_text") {
    native_callee = "polyrt_write_text";
    if (arg_names.size() > 1)
      unwrap_string_literal(arg_names[1]);
  } else if (callee_name == "file_write_int")
    native_callee = "polyrt_write_i64";
  else if (callee_name == "file_close")
    native_callee = "polyrt_close_read";

  auto inst = builder_.MakeCall(native_callee, arg_names, call_ret_type, "");
  return {inst->name, inst->type};
}

PloyLowering::EvalResult PloyLowering::LowerCrossLangCall(
    const std::shared_ptr<CrossLangCallExpression> &call) {
  // Lower arguments
  std::vector<std::string> arg_names;
  std::vector<ir::IRType> arg_types;
  for (const auto &arg : call->args) {
    EvalResult a = LowerExpression(arg);
    arg_names.push_back(a.value);
    arg_types.push_back(a.type);
  }

  // Source modules imported with `IMPORT <lang>::<module>` are compiled by
  // polyc's own frontends into the same native ABI as Poly.  Call their
  // qualified symbol directly; the packaging stage materialises a portable
  // alias to the language frontend's native symbol.  Runtime bridges remain
  // reserved for PACKAGE/VM-backed calls and explicit LINK mappings.
  const bool direct_native_import =
      sema_.IsLocalSourceCall(call->language, call->function);

  // Generate the stub name for the cross-language call.
  // Look up the LINK entry matching the call target to use the correct
  // language pair for name mangling: __ploy_bridge_<target_lang>_<source_lang>_<sym>.
  std::string stub_name;
  if (direct_native_import) {
    stub_name = call->function;
  } else {
    const LinkEntry *link_match = nullptr;
    for (const auto &le : sema_.Links()) {
      if (le.target_language == call->language && le.target_symbol == call->function) {
        link_match = &le;
        break;
      }
    }
    if (link_match) {
      stub_name = MangleStubName(link_match->target_language, link_match->source_language,
                                 link_match->target_symbol, call->lang_version_pin);
    } else {
      // No matching LINK entry — fall back to poly→<language> naming.
      stub_name = MangleStubName("poly", call->language, call->function, call->lang_version_pin);
    }
  }

  // Resolve the return type from sema's known signatures.
  // In strict mode, unknown signatures are hard errors and we avoid i64
  // fallback to prevent fake-success ABI assumptions.
  ir::IRType call_ret_type = ir::IRType::Pointer(ir::IRType::Void());
  call_ret_type.is_placeholder = true;
  {
    auto sig_it = sema_.KnownSignatures().find(call->function);
    if (sig_it != sema_.KnownSignatures().end() &&
        sig_it->second.return_type.kind != core::TypeKind::kAny &&
        sig_it->second.return_type.kind != core::TypeKind::kUnknown &&
        sig_it->second.return_type.kind != core::TypeKind::kInvalid) {
      call_ret_type = CoreTypeToIR(sig_it->second.return_type);
      call_ret_type.is_placeholder = false; // resolved successfully
    } else {
      // Cross-language targets often lack explicit return type info.
      // In strict mode this is an error; in permissive mode a warning.
      if (sema_.IsStrictMode()) {
        diagnostics_.ReportError(
            call->loc, frontends::ErrorCode::kUnsupportedLowering,
            "cross-language call to '" + call->function +
                "' has unknown return type/signature (strict mode rejects fallback lowering)");
        return {"", ir::IRType::Invalid()};
      } else {
        diagnostics_.ReportWarning(call->loc, frontends::ErrorCode::kOpaqueTypeFallback,
                                   "cross-language call to '" + call->function +
                                       "' has unknown return type; defaulting to opaque pointer");
      }
    }
  }

  // Record a runtime bridge descriptor only for non-native calls.  Local
  // source imports are represented in the staged driver's marshal plan and
  // linked directly.
  if (!direct_native_import) {
    CrossLangCallDescriptor desc;
    desc.stub_name = stub_name;
    desc.source_language = call->language;
    desc.target_language = "poly";
    desc.source_function = call->function;
    desc.target_function = stub_name;
    desc.source_param_types = arg_types;
    desc.source_return_type = call_ret_type;
    desc.target_return_type = call_ret_type;

    // Generate marshalling descriptors for each argument
    for (const auto &at : arg_types) {
      CrossLangCallDescriptor::MarshalOp marshal;
      marshal.kind = CrossLangCallDescriptor::MarshalOp::Kind::kDirect;
      marshal.from = at;
      marshal.to = at; // Same type by default; overridden by MAP_TYPE
      desc.param_marshal.push_back(marshal);
    }
    desc.return_marshal.kind = CrossLangCallDescriptor::MarshalOp::Kind::kDirect;
    desc.return_marshal.from = call_ret_type;
    desc.return_marshal.to = call_ret_type;
    desc.lang_version = call->lang_version_pin;

    call_descriptors_.push_back(desc);
  }

  // Emit the call instruction to the stub
  auto inst = builder_.MakeCall(stub_name, arg_names, call_ret_type, "");
  return {inst->name, inst->type};
}

PloyLowering::EvalResult PloyLowering::LowerNewExpression(
    const std::shared_ptr<NewExpression> &new_expr) {
  // Lower constructor arguments
  std::vector<std::string> arg_names;
  std::vector<ir::IRType> arg_types;
  for (const auto &arg : new_expr->args) {
    EvalResult a = LowerExpression(arg);
    arg_names.push_back(a.value);
    arg_types.push_back(a.type);
  }

  // Generate the stub name for the constructor call
  std::string stub_name =
      MangleStubName("poly", new_expr->language, new_expr->class_name + "::__init__",
                     new_expr->lang_version_pin);

  // Resolve the object type from sema - the sema now performs full type
  // resolution via ResolveObjectType, so we can trust the symbol table.
  ir::IRType obj_type = ir::IRType::Pointer(ir::IRType::Void());
  {
    const std::string schema_key = new_expr->language + "::" + new_expr->class_name;
    if (sema_.LookupClassSchema(schema_key)) {
      obj_type = ir::IRType::Pointer(ir::IRType::I8());
    } else {
      auto sym_it = sema_.Symbols().find(new_expr->class_name);
      if (sym_it != sema_.Symbols().end() && sym_it->second.type.kind != core::TypeKind::kAny &&
          sym_it->second.type.kind != core::TypeKind::kUnknown &&
          sym_it->second.type.kind != core::TypeKind::kInvalid) {
        obj_type = CoreTypeToIR(sym_it->second.type);
      }
    }
  }

  // Consult the sema ABI signature for the constructor to determine the
  // correct parameter types at the ABI level, replacing blind direct marshal.
  std::string ctor_name = new_expr->class_name + "::__init__";
  const FunctionSignature *sig = nullptr;
  {
    auto it = sema_.KnownSignatures().find(ctor_name);
    if (it != sema_.KnownSignatures().end()) {
      sig = &it->second;
    }
  }

  // Record the cross-language call descriptor for the constructor
  CrossLangCallDescriptor desc;
  desc.stub_name = stub_name;
  desc.source_language = new_expr->language;
  desc.target_language = "poly";
  desc.source_function = new_expr->class_name + "::__init__";
  desc.target_function = stub_name;
  desc.source_return_type = obj_type;
  desc.target_return_type = obj_type;

  // Generate marshalling descriptors using ABI-resolved types from sema
  // when available, falling back to direct marshal only when necessary.
  for (size_t i = 0; i < arg_types.size(); ++i) {
    ir::IRType expected_type = arg_types[i];
    if (sig && i < sig->param_types.size() && sig->param_types[i].kind != core::TypeKind::kAny &&
        sig->param_types[i].kind != core::TypeKind::kUnknown &&
        sig->param_types[i].kind != core::TypeKind::kInvalid) {
      expected_type = CoreTypeToIR(sig->param_types[i]);
    }
    desc.source_param_types.push_back(expected_type);

    CrossLangCallDescriptor::MarshalOp marshal;
    if (expected_type.kind == arg_types[i].kind) {
      marshal.kind = CrossLangCallDescriptor::MarshalOp::Kind::kDirect;
    } else {
      marshal.kind = CrossLangCallDescriptor::MarshalOp::Kind::kCast;
    }
    marshal.from = arg_types[i];
    marshal.to = expected_type;
    desc.param_marshal.push_back(marshal);
  }
  desc.return_marshal.kind = CrossLangCallDescriptor::MarshalOp::Kind::kDirect;
  desc.return_marshal.from = obj_type;
  desc.return_marshal.to = obj_type;
  desc.lang_version = new_expr->lang_version_pin;

  call_descriptors_.push_back(desc);

  // Emit the call instruction to the constructor stub
  auto inst = builder_.MakeCall(stub_name, arg_names, obj_type, "");
  return {inst->name, inst->type};
}

PloyLowering::EvalResult PloyLowering::LowerMethodCallExpression(
    const std::shared_ptr<MethodCallExpression> &method_call) {
  // Lower the receiver object
  EvalResult obj = LowerExpression(method_call->object);

  // Lower method arguments - the object is passed as the first argument
  std::vector<std::string> arg_names;
  std::vector<ir::IRType> arg_types;
  arg_names.push_back(obj.value);
  arg_types.push_back(obj.type);
  for (const auto &arg : method_call->args) {
    EvalResult a = LowerExpression(arg);
    arg_names.push_back(a.value);
    arg_types.push_back(a.type);
  }

  // Generate the stub name for the method call
  std::string stub_name = MangleStubName("poly", method_call->language, method_call->method_name,
                                         method_call->lang_version_pin);

  // Resolve return type from sema known signatures.  Try the method name
  // directly, then try qualified with the object type if available.
  ir::IRType method_ret_type = ir::IRType::Pointer(ir::IRType::Void());
  method_ret_type.is_placeholder = true;
  const FunctionSignature *sig = nullptr;
  {
    // A typed receiver identifies the exact CLASS schema and avoids guessing
    // from a bare method name that may occur on multiple classes.
    if (auto id = std::dynamic_pointer_cast<Identifier>(method_call->object)) {
      auto symbol = sema_.Symbols().find(id->name);
      if (symbol != sema_.Symbols().end() &&
          symbol->second.type.kind == core::TypeKind::kClass) {
        const std::string schema_key = symbol->second.type.language + "::" +
                                       symbol->second.type.name;
        if (const ForeignClassSchema *schema = sema_.LookupClassSchema(schema_key)) {
          auto method = schema->methods.find(method_call->method_name);
          if (method != schema->methods.end())
            sig = &method->second;
        }
      }
    }
    auto sig_it = sema_.KnownSignatures().find(method_call->method_name);
    if (!sig && sig_it != sema_.KnownSignatures().end())
      sig = &sig_it->second;
    if (sig && sig->return_type.kind != core::TypeKind::kAny &&
        sig->return_type.kind != core::TypeKind::kUnknown &&
        sig->return_type.kind != core::TypeKind::kInvalid)
      method_ret_type = CoreTypeToIR(sig->return_type);
  }

  // Record the cross-language call descriptor for the method
  CrossLangCallDescriptor desc;
  desc.stub_name = stub_name;
  desc.source_language = method_call->language;
  desc.target_language = "poly";
  desc.source_function = method_call->method_name;
  desc.target_function = stub_name;
  desc.source_return_type = method_ret_type;
  desc.target_return_type = method_ret_type;

  // Generate ABI-aware marshalling descriptors using sema signature
  for (size_t i = 0; i < arg_types.size(); ++i) {
    ir::IRType expected_type = arg_types[i];
    // Skip index 0 (receiver object) for param_types lookup since sema
    // signatures don't include the implicit self parameter.
    if (sig && i > 0 && (i - 1) < sig->param_types.size() &&
        sig->param_types[i - 1].kind != core::TypeKind::kAny &&
        sig->param_types[i - 1].kind != core::TypeKind::kUnknown &&
        sig->param_types[i - 1].kind != core::TypeKind::kInvalid) {
      expected_type = CoreTypeToIR(sig->param_types[i - 1]);
    }
    desc.source_param_types.push_back(expected_type);

    CrossLangCallDescriptor::MarshalOp marshal;
    if (expected_type.kind == arg_types[i].kind) {
      marshal.kind = CrossLangCallDescriptor::MarshalOp::Kind::kDirect;
    } else {
      marshal.kind = CrossLangCallDescriptor::MarshalOp::Kind::kCast;
    }
    marshal.from = arg_types[i];
    marshal.to = expected_type;
    desc.param_marshal.push_back(marshal);
  }
  desc.return_marshal.kind = CrossLangCallDescriptor::MarshalOp::Kind::kDirect;
  desc.return_marshal.from = method_ret_type;
  desc.return_marshal.to = method_ret_type;
  desc.lang_version = method_call->lang_version_pin;

  call_descriptors_.push_back(desc);

  // Emit the call instruction to the method stub
  auto inst = builder_.MakeCall(stub_name, arg_names, method_ret_type, "");
  return {inst->name, inst->type};
}

PloyLowering::EvalResult PloyLowering::LowerGetAttrExpression(
    const std::shared_ptr<GetAttrExpression> &get_attr) {
  // Lower the receiver object
  EvalResult obj = LowerExpression(get_attr->object);

  // GET is lowered as a call to __getattr__ bridge stub
  // The object is passed as the first argument
  std::vector<std::string> arg_names;
  std::vector<ir::IRType> arg_types;
  arg_names.push_back(obj.value);
  arg_types.push_back(obj.type);

  // Generate the stub name for the getattr call
  std::string stub_name =
      MangleStubName("poly", get_attr->language, "__getattr__" + get_attr->attr_name,
                     get_attr->lang_version_pin);

  // Attribute access returns an opaque pointer by default - the exact
  // type depends on the foreign object's schema which is unknown at
  // compile time.  Use Pointer(I8) as a generic handle.
  ir::IRType attr_ret_type = ir::IRType::Pointer(ir::IRType::I8());

  // Record the cross-language call descriptor
  CrossLangCallDescriptor desc;
  desc.stub_name = stub_name;
  desc.source_language = get_attr->language;
  desc.target_language = "poly";
  desc.source_function = "__getattr__::" + get_attr->attr_name;
  desc.target_function = stub_name;
  desc.source_param_types = arg_types;
  desc.source_return_type = attr_ret_type;
  desc.target_return_type = attr_ret_type;

  // Generate marshalling descriptors
  for (const auto &at : arg_types) {
    CrossLangCallDescriptor::MarshalOp marshal;
    marshal.kind = CrossLangCallDescriptor::MarshalOp::Kind::kDirect;
    marshal.from = at;
    marshal.to = at;
    desc.param_marshal.push_back(marshal);
  }
  desc.return_marshal.kind = CrossLangCallDescriptor::MarshalOp::Kind::kDirect;
  desc.return_marshal.from = attr_ret_type;
  desc.return_marshal.to = attr_ret_type;
  desc.lang_version = get_attr->lang_version_pin;

  call_descriptors_.push_back(desc);

  // Emit the call
  auto inst = builder_.MakeCall(stub_name, arg_names, attr_ret_type, "");
  return {inst->name, inst->type};
}

PloyLowering::EvalResult PloyLowering::LowerSetAttrExpression(
    const std::shared_ptr<SetAttrExpression> &set_attr) {
  // Lower the receiver object and the value
  EvalResult obj = LowerExpression(set_attr->object);
  EvalResult val = LowerExpression(set_attr->value);

  // SET is lowered as a call to __setattr__ bridge stub
  // object and value are passed as arguments
  std::vector<std::string> arg_names;
  std::vector<ir::IRType> arg_types;
  arg_names.push_back(obj.value);
  arg_types.push_back(obj.type);

  // Resolve expected receiver/value types from setter signatures first,
  // then fall back to struct field definitions.
  ir::IRType expected_obj_type = obj.type;
  ir::IRType expected_val_type = val.type;
  {
    std::string setter_name = "__setattr__::" + set_attr->attr_name;
    auto sig_it = sema_.KnownSignatures().find(setter_name);
    if (sig_it != sema_.KnownSignatures().end()) {
      if (!sig_it->second.param_types.empty() &&
          sig_it->second.param_types[0].kind != core::TypeKind::kAny &&
          sig_it->second.param_types[0].kind != core::TypeKind::kUnknown &&
          sig_it->second.param_types[0].kind != core::TypeKind::kInvalid) {
        expected_obj_type = CoreTypeToIR(sig_it->second.param_types[0]);
      }
      if (sig_it->second.param_types.size() > 1 &&
          sig_it->second.param_types[1].kind != core::TypeKind::kAny &&
          sig_it->second.param_types[1].kind != core::TypeKind::kUnknown &&
          sig_it->second.param_types[1].kind != core::TypeKind::kInvalid) {
        expected_val_type = CoreTypeToIR(sig_it->second.param_types[1]);
      }
    }
  }
  {
    for (const auto &[struct_name, fields] : sema_.StructDefs()) {
      for (const auto &[field_name, field_type] : fields) {
        if (field_name == set_attr->attr_name && field_type.kind != core::TypeKind::kAny &&
            field_type.kind != core::TypeKind::kUnknown &&
            field_type.kind != core::TypeKind::kInvalid) {
          expected_val_type = CoreTypeToIR(field_type);
          break;
        }
      }
    }
  }
  arg_types[0] = expected_obj_type;
  arg_names.push_back(val.value);
  arg_types.push_back(expected_val_type);

  // Generate the stub name for the setattr call
  std::string stub_name =
      MangleStubName("poly", set_attr->language, "__setattr__" + set_attr->attr_name,
                     set_attr->lang_version_pin);

  // Record the cross-language call descriptor
  CrossLangCallDescriptor desc;
  desc.stub_name = stub_name;
  desc.source_language = set_attr->language;
  desc.target_language = "poly";
  desc.source_function = "__setattr__::" + set_attr->attr_name;
  desc.target_function = stub_name;
  desc.source_param_types = arg_types;
  desc.source_return_type = ir::IRType::Void();
  desc.target_return_type = ir::IRType::Void();

  // Generate marshalling descriptors
  CrossLangCallDescriptor::MarshalOp obj_marshal;
  obj_marshal.kind = (obj.type.kind == expected_obj_type.kind)
                         ? CrossLangCallDescriptor::MarshalOp::Kind::kDirect
                         : CrossLangCallDescriptor::MarshalOp::Kind::kCast;
  obj_marshal.from = obj.type;
  obj_marshal.to = expected_obj_type;
  desc.param_marshal.push_back(obj_marshal);

  CrossLangCallDescriptor::MarshalOp val_marshal;
  val_marshal.kind = (val.type.kind == expected_val_type.kind)
                         ? CrossLangCallDescriptor::MarshalOp::Kind::kDirect
                         : CrossLangCallDescriptor::MarshalOp::Kind::kCast;
  val_marshal.from = val.type;
  val_marshal.to = expected_val_type;
  desc.param_marshal.push_back(val_marshal);
  desc.return_marshal.kind = CrossLangCallDescriptor::MarshalOp::Kind::kDirect;
  desc.return_marshal.from = ir::IRType::Void();
  desc.return_marshal.to = ir::IRType::Void();
  desc.lang_version = set_attr->lang_version_pin;

  call_descriptors_.push_back(desc);

  // Emit the call - returns void but we return the value for expression chaining
  builder_.MakeCall(stub_name, arg_names, ir::IRType::Void(), "");
  return {val.value, val.type};
}

// ============================================================================
// WITH Statement Lowering
// ============================================================================

void PloyLowering::LowerWithStatement(const std::shared_ptr<WithStatement> &with_stmt) {
  // Correct context-manager lowering needs four-argument __exit__(self,
  // exc_type, exc_value, traceback), suppression handling, and cleanup edges
  // for RETURN/THROW/BREAK/CONTINUE.  The current IR builder has no cleanup
  // region abstraction, so emitting only a normal-path one-argument call
  // would silently violate the language contract.
  diagnostics_.ReportError(
      with_stmt->loc, frontends::ErrorCode::kUnsupportedLowering,
      "WITH requires exception-aware cleanup edges and the four-argument __exit__ ABI, which "
      "Poly IR lowering does not yet represent");
}

// ============================================================================
// Structured Exception Handling (since v1.13.0)
//
// TRY/CATCH/FINALLY/THROW lower to direct calls into the polyrt error
// bridge.  The IR shape mirrors a setjmp/longjmp pair: `__ploy_rt_try_begin`
// returns 0 on first entry and 1 once `__ploy_rt_throw` has unwound to it
// via longjmp.  A conditional branch on that return value picks between
// the protected body (zero) and the catch dispatch (non-zero).  The
// FINALLY block is unconditional and joined from both predecessors.
//
// Cross-language interception (Python `Exception`, C++ `std::exception`,
// Java `Throwable`, .NET `Exception`, Rust `Result::Err`) is performed
// inside the runtime bridge: each language adapter wraps the foreign
// exception, calls `__ploy_rt_throw`, and the longjmp lands here.
// ============================================================================

void PloyLowering::LowerThrowStatement(const std::shared_ptr<ThrowStatement> &throw_stmt) {
  if (!throw_stmt) return;

  // Lower the raised value.  For a string literal (e.g. `THROW "boom";`)
  // this is the interned pointer; for an existing Error handle it is the
  // handle's pointer value.  The runtime accepts either via the
  // `__ploy_rt_throw` C ABI; richer typed-throw is tracked under future
  // work.
  EvalResult val = LowerExpression(throw_stmt->value);
  std::string payload = val.value.empty() ? std::string("0") : val.value;

  std::vector<std::string> args = {payload};
  builder_.MakeCall("__ploy_rt_throw", args, ir::IRType::Void());

  // `__ploy_rt_throw` does not return; emit `unreachable` so the
  // verifier knows the current block has terminated and downstream
  // statements are dead code.
  builder_.MakeUnreachable();
  terminated_ = true;
}

void PloyLowering::LowerTryStatement(const std::shared_ptr<TryStatement> &try_stmt) {
  if (!try_stmt) return;

  if (try_stmt->catches.size() > 1) {
    diagnostics_.ReportError(
        try_stmt->loc, frontends::ErrorCode::kUnsupportedLowering,
        "multiple CATCH clauses require a typed runtime error discriminator; executing every "
        "handler sequentially is not semantics-preserving");
    return;
  }
  if (try_stmt->has_finally) {
    diagnostics_.ReportError(
        try_stmt->loc, frontends::ErrorCode::kUnsupportedLowering,
        "FINALLY requires cleanup edges for every RETURN/THROW/BREAK/CONTINUE path, which Poly "
        "IR lowering does not yet represent");
    return;
  }
  if (try_stmt->catches.empty()) {
    diagnostics_.ReportError(try_stmt->loc, frontends::ErrorCode::kUnsupportedLowering,
                             "TRY without a CATCH has no representable handler target");
    return;
  }

  // 1) try.entry — push a handler and branch on the return value.
  auto body_bb     = builder_.CreateBlock("try.body");
  auto catch_bb    = builder_.CreateBlock("try.catch");
  auto merge_bb    = builder_.CreateBlock("try.merge");

  // `__ploy_rt_try_begin` returns i32 (0 = first entry, 1 = unwound here).
  auto begin_call = builder_.MakeCall("__ploy_rt_try_begin", {},
                                      ir::IRType::I32(true), "try.tag");
  std::string tag = begin_call ? begin_call->name : std::string("0");

  // Compare the tag against zero: equal -> body, non-zero -> catch.
  auto cmp = builder_.MakeBinary(ir::BinaryInstruction::Op::kCmpNe, tag, "0", "try.thrown");
  if (cmp)
    cmp->type = ir::IRType::I1();
  builder_.MakeCondBranch(cmp ? cmp->name : tag, catch_bb.get(), body_bb.get());

  // 2) try.body — the protected statements.  On normal completion we
  // pop the handler and fall through to FINALLY (or directly to merge).
  builder_.SetInsertPoint(body_bb);
  terminated_ = false;
  LowerBlockStatements(try_stmt->body);
  bool body_terminated = terminated_;
  if (!terminated_) {
    builder_.MakeCall("__ploy_rt_try_end", {}, ir::IRType::Void());
    builder_.MakeBranch(merge_bb.get());
  }

  // 3) try.catch — load the current Error handle, bind it to the
  // declared name (when present), run the catch body.
  builder_.SetInsertPoint(catch_bb);
  terminated_ = false;
  auto err_call = builder_.MakeCall("__ploy_rt_current_error", {},
                                    ir::IRType::Pointer(ir::IRType::I8(true)), "try.err");
  std::string err_value = err_call ? err_call->name : std::string("0");

  const auto &clause = try_stmt->catches.front();
  auto shadowed_error = env_.find(clause.var_name);
  const bool had_shadowed_error = !clause.var_name.empty() && shadowed_error != env_.end();
  EnvEntry saved_error;
  if (had_shadowed_error)
    saved_error = shadowed_error->second;
  if (!clause.var_name.empty()) {
    env_[clause.var_name] =
        EnvEntry{err_value, ir::IRType::Pointer(ir::IRType::I8(true)), false};
  }
  LowerBlockStatements(clause.body);
  if (!clause.var_name.empty()) {
    if (had_shadowed_error)
      env_[clause.var_name] = saved_error;
    else
      env_.erase(clause.var_name);
  }
  // Mark the error consumed once the catch body completes.
  bool catch_terminated = terminated_;
  if (!terminated_) {
    builder_.MakeCall("__ploy_rt_clear_error", {}, ir::IRType::Void());
    builder_.MakeBranch(merge_bb.get());
  }

  // 5) try.merge — continuation point for the surrounding block.  The
  // merge block is unreachable only when both arms terminate without
  // a fall-through (e.g. body `RETURN` and catch `THROW`); in that
  // case the surrounding lowering will mark it terminated.
  builder_.SetInsertPoint(merge_bb);
  terminated_ = body_terminated && catch_terminated;
  if (terminated_) {
    builder_.MakeUnreachable();
  }
}

// ============================================================================
// STRUCT Declaration Lowering
// ============================================================================

void PloyLowering::LowerStructDecl(const std::shared_ptr<StructDecl> &struct_decl) {
  // Struct declarations create an IR struct type definition.
  // Build the field type list for the struct.
  std::vector<ir::IRType> field_types;
  for (const auto &field : struct_decl->fields) {
    ir::IRType ft = PloyTypeToIR(field.type);
    field_types.push_back(ft);
  }

  // Register the struct as a named type in the IR context.
  // The struct layout is: { field0_type, field1_type, ... }
  ir::IRType struct_type = ir::IRType::Struct(struct_decl->name, field_types);
  // Store the struct name mapping for later struct literal lowering.
  EnvEntry entry;
  entry.ir_name = struct_decl->name;
  entry.type = struct_type;
  env_[struct_decl->name] = entry;
}

// ============================================================================
// MAP_FUNC Lowering
// ============================================================================

void PloyLowering::LowerMapFuncDecl(const std::shared_ptr<MapFuncDecl> &map_func) {
  // MAP_FUNC is lowered identically to a regular FUNC declaration.
  // It produces a callable IR function that can be referenced during
  // cross-language marshalling.

  ir::IRType ret_type =
      map_func->return_type ? PloyTypeToIR(map_func->return_type) : ir::IRType::Void();

  std::vector<std::pair<std::string, ir::IRType>> params;
  for (const auto &p : map_func->params) {
    // Consult the sema known-signatures for parameter types when the
    // AST annotation is absent, instead of blindly using I64.
    ir::IRType pt = ir::IRType::I64(true);
    if (p.type) {
      pt = PloyTypeToIR(p.type);
    } else {
      auto sig_it = sema_.KnownSignatures().find(map_func->name);
      if (sig_it != sema_.KnownSignatures().end()) {
        size_t idx = static_cast<size_t>(&p - &map_func->params[0]);
        if (idx < sig_it->second.param_types.size()) {
          pt = CoreTypeToIR(sig_it->second.param_types[idx]);
        }
      }
    }
    params.emplace_back(p.name, pt);
  }

  std::string func_name = "__ploy_mapfunc_" + map_func->name;
  auto fn = ir_ctx_.CreateFunction(func_name, ret_type, params);
  auto saved_fn = current_function_;
  current_function_ = fn;
  builder_.SetCurrentFunction(fn);

  auto entry_block = builder_.CreateBlock("entry");
  builder_.SetInsertPoint(entry_block);

  // Register parameters in the environment
  for (const auto &[name, type] : params) {
    env_[name] = {name, type};
  }

  LowerBlockStatements(map_func->body);

  current_function_ = saved_fn;
  builder_.SetCurrentFunction(saved_fn);
}

// ============================================================================
// Complex Expression Lowering
// ============================================================================

PloyLowering::EvalResult PloyLowering::LowerConvertExpression(
    const std::shared_ptr<ConvertExpression> &conv) {
  // Lower the source expression
  EvalResult src = LowerExpression(conv->expr);

  // Determine the target IR type
  ir::IRType target_type =
      conv->target_type ? PloyTypeToIR(conv->target_type) : ir::IRType::I64(true);

  // If source and target types match exactly, pass through directly.
  if (src.type.SameShape(target_type)) {
    return src;
  }

  // Generate conversion code based on types
  std::string dst_name = "convert.result." + std::to_string(generated_name_index_++);
  if (!GenerateMarshalCode(src.value, src.type, target_type, dst_name, conv->loc,
                           "CONVERT expression")) {
    return {"", ir::IRType::Invalid()};
  }
  return {dst_name, target_type};
}

PloyLowering::EvalResult PloyLowering::LowerListLiteral(const std::shared_ptr<ListLiteral> &list) {
  ir::IRType ptr_type = ir::IRType::Pointer(ir::IRType::I8());
  if (list->elements.empty()) {
    diagnostics_.ReportError(list->loc, frontends::ErrorCode::kUnsupportedLowering,
                             "an empty LIST literal has no element layout; add a typed "
                             "constructor once the runtime exposes one");
    return {"", ir::IRType::Invalid()};
  }

  std::vector<EvalResult> elements;
  elements.reserve(list->elements.size());
  for (const auto &element : list->elements) {
    EvalResult lowered = LowerExpression(element);
    if (lowered.type.kind == ir::IRTypeKind::kInvalid || lowered.type.is_placeholder) {
      diagnostics_.ReportError(list->loc, frontends::ErrorCode::kUnsupportedLowering,
                               "LIST element has an unresolved type or ABI layout");
      return {"", ir::IRType::Invalid()};
    }
    elements.push_back(lowered);
  }

  const ir::IRType elem_type = elements.front().type;
  for (size_t i = 1; i < elements.size(); ++i) {
    if (elements[i].type.SameShape(elem_type))
      continue;
    std::string converted =
        "list.element.converted." + std::to_string(generated_name_index_++);
    if (!GenerateMarshalCode(elements[i].value, elements[i].type, elem_type, converted, list->loc,
                             "LIST element")) {
      return {"", ir::IRType::Invalid()};
    }
    elements[i] = {converted, elem_type};
  }

  const size_t elem_size = ir_ctx_.Layout().SizeOf(elem_type);
  if (elem_size == 0) {
    diagnostics_.ReportError(list->loc, frontends::ErrorCode::kUnsupportedLowering,
                             "LIST element type '" + elem_type.name + "' is unsized in Poly IR");
    return {"", ir::IRType::Invalid()};
  }

  // Call __ploy_rt_list_create(elem_size, initial_capacity)
  std::string elem_size_val = std::to_string(elem_size);
  std::string capacity_val = std::to_string(list->elements.size());
  auto create_call = builder_.MakeCall("__ploy_rt_list_create", {elem_size_val, capacity_val},
                                       ptr_type,
                                       "list.ptr." + std::to_string(generated_name_index_++));

  // Push each element
  for (const EvalResult &element : elements) {
    // Allocate space for the element and store it
    auto alloca_inst = builder_.MakeAlloca(
        elem_type, "list.element.addr." + std::to_string(generated_name_index_++));
    builder_.MakeStore(alloca_inst->name, element.value);
    builder_.MakeCall("__ploy_rt_list_push", {create_call->name, alloca_inst->name},
                      ir::IRType::Void(), "");
  }

  return {create_call->name, ptr_type};
}

PloyLowering::EvalResult PloyLowering::LowerTupleLiteral(
    const std::shared_ptr<TupleLiteral> &tuple) {
  // Tuples are lowered as IR struct types with each element as a field
  std::vector<ir::IRType> elem_types;
  std::vector<std::string> elem_values;

  for (const auto &elem : tuple->elements) {
    EvalResult e = LowerExpression(elem);
    if (e.type.kind == ir::IRTypeKind::kInvalid || e.type.is_placeholder) {
      diagnostics_.ReportError(tuple->loc, frontends::ErrorCode::kUnsupportedLowering,
                               "TUPLE element has an unresolved type or ABI layout");
      return {"", ir::IRType::Invalid()};
    }
    elem_types.push_back(e.type);
    elem_values.push_back(e.value);
  }

  ir::IRType tuple_type = ir::IRType::Struct("tuple", elem_types);

  // Allocate the tuple on the stack
  auto alloca_inst = builder_.MakeAlloca(
      tuple_type, "tuple.addr." + std::to_string(generated_name_index_++));

  // Store each element at its field offset
  for (size_t i = 0; i < elem_values.size(); ++i) {
    std::string field_ptr = alloca_inst->name + ".field" + std::to_string(i);
    // GEP to the field, then store
    auto gep = builder_.MakeGEP(alloca_inst->name, tuple_type, {i}, field_ptr);
    builder_.MakeStore(gep->name, elem_values[i]);
  }

  auto value = builder_.MakeLoad(
      alloca_inst->name, tuple_type,
      "tuple.value." + std::to_string(generated_name_index_++));
  return {value->name, tuple_type};
}

PloyLowering::EvalResult PloyLowering::LowerDictLiteral(const std::shared_ptr<DictLiteral> &dict) {
  ir::IRType ptr_type = ir::IRType::Pointer(ir::IRType::I8());
  if (dict->entries.empty()) {
    diagnostics_.ReportError(dict->loc, frontends::ErrorCode::kUnsupportedLowering,
                             "an empty DICT literal has no key/value layout; add a typed "
                             "constructor once the runtime exposes one");
    return {"", ir::IRType::Invalid()};
  }

  std::vector<std::pair<EvalResult, EvalResult>> entries;
  entries.reserve(dict->entries.size());
  for (const auto &entry : dict->entries) {
    EvalResult key = LowerExpression(entry.key);
    EvalResult value = LowerExpression(entry.value);
    if (key.type.kind == ir::IRTypeKind::kInvalid || value.type.kind == ir::IRTypeKind::kInvalid ||
        key.type.is_placeholder || value.type.is_placeholder) {
      diagnostics_.ReportError(dict->loc, frontends::ErrorCode::kUnsupportedLowering,
                               "DICT key or value has an unresolved type or ABI layout");
      return {"", ir::IRType::Invalid()};
    }
    entries.emplace_back(key, value);
  }

  const ir::IRType key_type = entries.front().first.type;
  const ir::IRType value_type = entries.front().second.type;
  for (size_t i = 1; i < entries.size(); ++i) {
    if (!entries[i].first.type.SameShape(key_type)) {
      std::string converted =
          "dict.key.converted." + std::to_string(generated_name_index_++);
      if (!GenerateMarshalCode(entries[i].first.value, entries[i].first.type, key_type, converted,
                               dict->loc, "DICT key")) {
        return {"", ir::IRType::Invalid()};
      }
      entries[i].first = {converted, key_type};
    }
    if (!entries[i].second.type.SameShape(value_type)) {
      std::string converted =
          "dict.value.converted." + std::to_string(generated_name_index_++);
      if (!GenerateMarshalCode(entries[i].second.value, entries[i].second.type, value_type,
                               converted, dict->loc, "DICT value")) {
        return {"", ir::IRType::Invalid()};
      }
      entries[i].second = {converted, value_type};
    }
  }

  const size_t key_size = ir_ctx_.Layout().SizeOf(key_type);
  const size_t value_size = ir_ctx_.Layout().SizeOf(value_type);
  if (key_size == 0 || value_size == 0) {
    diagnostics_.ReportError(dict->loc, frontends::ErrorCode::kUnsupportedLowering,
                             "DICT key or value type is unsized in Poly IR");
    return {"", ir::IRType::Invalid()};
  }

  auto create_call = builder_.MakeCall(
      "__ploy_rt_dict_create", {std::to_string(key_size), std::to_string(value_size)}, ptr_type,
      "dict.ptr." + std::to_string(generated_name_index_++));

  // Insert each entry
  for (const auto &[key, value] : entries) {
    // Allocate and store key and value for passing by pointer
    auto key_alloca = builder_.MakeAlloca(
        key_type, "dict.key.addr." + std::to_string(generated_name_index_++));
    builder_.MakeStore(key_alloca->name, key.value);
    auto val_alloca = builder_.MakeAlloca(
        value_type, "dict.val.addr." + std::to_string(generated_name_index_++));
    builder_.MakeStore(val_alloca->name, value.value);

    builder_.MakeCall("__ploy_rt_dict_insert",
                      {create_call->name, key_alloca->name, val_alloca->name}, ir::IRType::Void(),
                      "");
  }

  return {create_call->name, ptr_type};
}

PloyLowering::EvalResult PloyLowering::LowerStructLiteral(
    const std::shared_ptr<StructLiteral> &struct_lit) {
  // Both the concrete IR layout and sema's ordered field schema are required.
  // A missing definition is not an i64 value, and source initializer order is
  // unrelated to the declaration's ABI field order.
  auto env_it = env_.find(struct_lit->struct_name);
  auto def_it = sema_.StructDefs().find(struct_lit->struct_name);
  if (env_it == env_.end() || env_it->second.type.kind != ir::IRTypeKind::kStruct ||
      def_it == sema_.StructDefs().end()) {
    diagnostics_.ReportError(struct_lit->loc, frontends::ErrorCode::kUnsupportedLowering,
                             "struct literal '" + struct_lit->struct_name +
                                 "' has no concrete ordered IR layout");
    return {"", ir::IRType::Invalid()};
  }
  const ir::IRType &struct_type = env_it->second.type;
  const auto &defined_fields = def_it->second;

  std::unordered_map<std::string, size_t> field_indices;
  for (size_t i = 0; i < defined_fields.size(); ++i)
    field_indices.emplace(defined_fields[i].first, i);
  std::vector<bool> initialized(defined_fields.size(), false);

  for (const auto &field : struct_lit->fields) {
    auto field_it = field_indices.find(field.name);
    if (field_it == field_indices.end() || initialized[field_it->second]) {
      diagnostics_.ReportError(
          struct_lit->loc, frontends::ErrorCode::kUnsupportedLowering,
          "struct literal '" + struct_lit->struct_name +
              "' has an unknown or duplicate field '" + field.name + "'");
      return {"", ir::IRType::Invalid()};
    }
    initialized[field_it->second] = true;
  }
  if (std::any_of(initialized.begin(), initialized.end(), [](bool present) { return !present; })) {
    diagnostics_.ReportError(struct_lit->loc, frontends::ErrorCode::kUnsupportedLowering,
                             "struct literal '" + struct_lit->struct_name +
                                 "' omits fields required by its concrete layout");
    return {"", ir::IRType::Invalid()};
  }

  // Allocate the struct on the stack
  auto alloca_inst = builder_.MakeAlloca(struct_type, struct_lit->struct_name + ".val");

  // Lower and store each field
  for (const auto &field : struct_lit->fields) {
    EvalResult field_val = LowerExpression(field.value);
    if (field_val.type.kind == ir::IRTypeKind::kInvalid || field_val.type.is_placeholder) {
      diagnostics_.ReportError(struct_lit->loc, frontends::ErrorCode::kUnsupportedLowering,
                               "struct field '" + field.name +
                                   "' has an unresolved lowering type or ABI");
      return {"", ir::IRType::Invalid()};
    }
    const size_t field_index = field_indices.at(field.name);
    ir::IRType expected_type = CoreTypeToIR(defined_fields[field_index].second);
    if (expected_type.kind == ir::IRTypeKind::kInvalid || expected_type.is_placeholder) {
      diagnostics_.ReportError(struct_lit->loc, frontends::ErrorCode::kUnsupportedLowering,
                               "struct field '" + field.name +
                                   "' has no concrete declared IR layout");
      return {"", ir::IRType::Invalid()};
    }
    std::string stored_value = field_val.value;
    if (!field_val.type.SameShape(expected_type)) {
      stored_value = "struct.field.converted." + std::to_string(generated_name_index_++);
      if (!GenerateMarshalCode(field_val.value, field_val.type, expected_type, stored_value,
                               field.value ? field.value->loc : struct_lit->loc,
                               "struct field '" + field.name + "'")) {
        return {"", ir::IRType::Invalid()};
      }
    }
    std::string field_ptr = alloca_inst->name + "." + field.name;
    auto gep = builder_.MakeGEP(alloca_inst->name, struct_type, {field_index}, field_ptr);
    builder_.MakeStore(gep->name, stored_value);
  }

  auto value = builder_.MakeLoad(alloca_inst->name, struct_type, struct_lit->struct_name + ".load");
  return {value->name, struct_type};
}

// ============================================================================
// Link Stub Generation
// ============================================================================

void PloyLowering::GenerateLinkStub(const LinkEntry &link) {
  std::string stub_name =
      MangleStubName(link.target_language, link.source_language, link.target_symbol);

  if (link.kind != LinkDecl::LinkKind::kFunction) {
    diagnostics_.ReportError(
        link.defined_at, frontends::ErrorCode::kUnsupportedLowering,
        "LINK AS VAR/STRUCT requires a concrete foreign data layout; MAP_TYPE entries are type "
        "relations, not field offsets or global storage ABIs");
    return;
  }

  auto find_signature = [&](const std::string &name) -> const FunctionSignature * {
    auto it = sema_.KnownSignatures().find(name);
    return it == sema_.KnownSignatures().end() ? nullptr : &it->second;
  };
  auto concrete_core_type = [](const core::Type &type) {
    return type.kind != core::TypeKind::kAny && type.kind != core::TypeKind::kUnknown &&
           type.kind != core::TypeKind::kInvalid;
  };
  auto complete_signature = [&](const FunctionSignature *signature) {
    if (!signature || !signature->param_count_known ||
        signature->param_types.size() < signature->param_count ||
        !concrete_core_type(signature->return_type)) {
      return false;
    }
    return std::all_of(signature->param_types.begin(),
                       signature->param_types.begin() + signature->param_count,
                       concrete_core_type);
  };

  const FunctionSignature *target_sig = find_signature(link.target_symbol);
  const FunctionSignature *source_sig = find_signature(link.source_symbol);
  const bool exact_signatures = complete_signature(target_sig) && complete_signature(source_sig) &&
                                target_sig->param_count == source_sig->param_count;

  auto saved_fn = current_function_;
  auto saved_insert = builder_.GetInsertPoint();

  if (!exact_signatures) {
    if (sema_.IsStrictMode()) {
      diagnostics_.ReportError(
          link.defined_at, frontends::ErrorCode::kUnsupportedLowering,
          "LINK stub for '" + link.target_symbol +
              "' needs complete target and source signatures; MAP_TYPE entries cannot define "
              "function arity");
      return;
    }

    diagnostics_.ReportWarning(
        link.defined_at, frontends::ErrorCode::kOpaqueTypeFallback,
        "LINK stub for '" + link.target_symbol +
            "' has incomplete endpoint signatures; emitting an explicitly opaque compatibility "
            "stub and ignoring MAP_TYPE for executable marshalling");

    ir::IRType opaque_param = ir::IRType::I64(true);
    opaque_param.is_placeholder = true;
    ir::IRType opaque_return = ir::IRType::Pointer(ir::IRType::Void());
    opaque_return.is_placeholder = true;
    if (target_sig && concrete_core_type(target_sig->return_type)) {
      ir::IRType resolved_return = CoreTypeToIR(target_sig->return_type);
      if (resolved_return.kind != ir::IRTypeKind::kInvalid)
        opaque_return = resolved_return;
    }

    auto fn = ir_ctx_.CreateFunction(stub_name, opaque_return, {{"arg0", opaque_param}});
    current_function_ = fn;
    builder_.SetCurrentFunction(fn);
    auto entry = builder_.CreateBlock("stub.entry");
    builder_.SetInsertPoint(entry);

    std::string source_func = link.source_symbol;
    std::replace(source_func.begin(), source_func.end(), ':', '_');
    auto call = builder_.MakeCall(source_func, {"arg0"}, opaque_return, "opaque.result");
    if (opaque_return.kind == ir::IRTypeKind::kVoid)
      builder_.MakeReturn();
    else
      builder_.MakeReturn(call->name);

    current_function_ = saved_fn;
    if (saved_fn)
      builder_.SetCurrentFunction(saved_fn);
    else
      builder_.ClearCurrentFunction();
    builder_.SetInsertPoint(saved_insert);
    return;
  }

  std::vector<std::pair<std::string, ir::IRType>> params;
  std::vector<ir::IRType> source_param_types;
  params.reserve(target_sig->param_count);
  source_param_types.reserve(source_sig->param_count);
  for (size_t i = 0; i < target_sig->param_count; ++i) {
    ir::IRType target_type = CoreTypeToIR(target_sig->param_types[i]);
    ir::IRType source_type = CoreTypeToIR(source_sig->param_types[i]);
    if (target_type.kind == ir::IRTypeKind::kInvalid ||
        source_type.kind == ir::IRTypeKind::kInvalid) {
      diagnostics_.ReportError(link.defined_at, frontends::ErrorCode::kUnsupportedLowering,
                               "LINK parameter has no concrete endpoint IR type");
      return;
    }
    params.emplace_back("arg" + std::to_string(i), target_type);
    source_param_types.push_back(source_type);
  }

  ir::IRType target_return = CoreTypeToIR(target_sig->return_type);
  ir::IRType source_return = CoreTypeToIR(source_sig->return_type);
  if (target_return.kind == ir::IRTypeKind::kInvalid ||
      source_return.kind == ir::IRTypeKind::kInvalid ||
      (target_return.kind == ir::IRTypeKind::kVoid) !=
          (source_return.kind == ir::IRTypeKind::kVoid)) {
    diagnostics_.ReportError(link.defined_at, frontends::ErrorCode::kUnsupportedLowering,
                             "LINK return types have no compatible concrete endpoint ABI");
    return;
  }

  auto fn = ir_ctx_.CreateFunction(stub_name, target_return, params);
  current_function_ = fn;
  builder_.SetCurrentFunction(fn);
  auto entry = builder_.CreateBlock("stub.entry");
  builder_.SetInsertPoint(entry);

  std::vector<std::string> call_args;
  call_args.reserve(params.size());
  for (size_t i = 0; i < params.size(); ++i) {
    if (params[i].second.SameShape(source_param_types[i])) {
      call_args.push_back(params[i].first);
      continue;
    }
    std::string marshalled =
        params[i].first + ".marshalled." + std::to_string(generated_name_index_++);
    if (!GenerateMarshalCode(params[i].first, params[i].second, source_param_types[i], marshalled,
                             link.defined_at,
                             "LINK parameter " + std::to_string(i) + " for '" +
                                 link.target_symbol + "'")) {
      current_function_ = saved_fn;
      if (saved_fn)
        builder_.SetCurrentFunction(saved_fn);
      else
        builder_.ClearCurrentFunction();
      builder_.SetInsertPoint(saved_insert);
      return;
    }
    call_args.push_back(marshalled);
  }

  std::string source_func = link.source_symbol;
  std::replace(source_func.begin(), source_func.end(), ':', '_');
  auto call = builder_.MakeCall(source_func, call_args, source_return, "source.result");
  if (target_return.kind == ir::IRTypeKind::kVoid) {
    builder_.MakeReturn();
  } else if (source_return.SameShape(target_return)) {
    builder_.MakeReturn(call->name);
  } else {
    std::string marshalled_return =
        "link.return.marshalled." + std::to_string(generated_name_index_++);
    if (!GenerateMarshalCode(call->name, source_return, target_return, marshalled_return,
                             link.defined_at, "LINK return for '" + link.target_symbol + "'")) {
      current_function_ = saved_fn;
      if (saved_fn)
        builder_.SetCurrentFunction(saved_fn);
      else
        builder_.ClearCurrentFunction();
      builder_.SetInsertPoint(saved_insert);
      return;
    }
    builder_.MakeReturn(marshalled_return);
  }

  current_function_ = saved_fn;
  if (saved_fn)
    builder_.SetCurrentFunction(saved_fn);
  else
    builder_.ClearCurrentFunction();
  builder_.SetInsertPoint(saved_insert);
}

// ============================================================================
// Marshal Code Generation
// ============================================================================

bool PloyLowering::GenerateMarshalCode(const std::string &src_val, const ir::IRType &src_type,
                                       const ir::IRType &dst_type, const std::string &dst_name,
                                       const core::SourceLoc &loc,
                                       const std::string &context) {
  if (src_val.empty() || src_type.kind == ir::IRTypeKind::kInvalid ||
      dst_type.kind == ir::IRTypeKind::kInvalid || src_type.is_placeholder ||
      dst_type.is_placeholder) {
    diagnostics_.ReportError(loc, frontends::ErrorCode::kUnsupportedLowering,
                             context + " has an unresolved source or destination ABI type");
    return false;
  }

  auto emit_assign = [&]() {
    auto assign = std::make_shared<ir::AssignInstruction>();
    assign->name = dst_name;
    assign->type = dst_type;
    assign->operands.push_back(src_val);
    auto bb = builder_.GetInsertPoint();
    if (bb)
      bb->AddInstruction(assign);
  };

  // Identical layouts and integer signedness are a direct copy.
  if (src_type.SameShape(dst_type)) {
    emit_assign();
    return true;
  }

  // Integer width conversion
  if (src_type.IsInteger() && dst_type.IsInteger()) {
    const int src_bits = src_type.BitWidth();
    const int dst_bits = dst_type.BitWidth();

    if (dst_bits > src_bits) {
      auto cast_kind = src_type.is_signed ? ir::CastInstruction::CastKind::kSExt
                                          : ir::CastInstruction::CastKind::kZExt;
      builder_.MakeCast(cast_kind, src_val, dst_type, dst_name);
    } else if (dst_bits < src_bits) {
      builder_.MakeCast(ir::CastInstruction::CastKind::kTrunc, src_val, dst_type, dst_name);
    } else {
      // Equal-width signedness conversion preserves the bits and changes the
      // operation-selection metadata used by signed/unsigned comparisons.
      emit_assign();
    }
    return true;
  }

  if (src_type.IsFloat() && dst_type.IsFloat()) {
    const bool widen = src_type.kind == ir::IRTypeKind::kF32 &&
                       dst_type.kind == ir::IRTypeKind::kF64;
    const bool narrow = src_type.kind == ir::IRTypeKind::kF64 &&
                        dst_type.kind == ir::IRTypeKind::kF32;
    if (widen) {
      builder_.MakeCast(ir::CastInstruction::CastKind::kFpExt, src_val, dst_type, dst_name);
      return true;
    }
    if (narrow) {
      builder_.MakeCast(ir::CastInstruction::CastKind::kFpTrunc, src_val, dst_type, dst_name);
      return true;
    }
  }

  // Pointer-to-pointer: direct bitcast
  const bool src_pointer = src_type.kind == ir::IRTypeKind::kPointer ||
                           src_type.kind == ir::IRTypeKind::kReference;
  const bool dst_pointer = dst_type.kind == ir::IRTypeKind::kPointer ||
                           dst_type.kind == ir::IRTypeKind::kReference;
  if (src_pointer && dst_pointer) {
    builder_.MakeCast(ir::CastInstruction::CastKind::kBitcast, src_val, dst_type, dst_name);
    return true;
  }
  if (src_pointer && dst_type.IsInteger()) {
    builder_.MakeCast(ir::CastInstruction::CastKind::kPtrToInt, src_val, dst_type, dst_name);
    return true;
  }
  if (src_type.IsInteger() && dst_pointer) {
    builder_.MakeCast(ir::CastInstruction::CastKind::kIntToPtr, src_val, dst_type, dst_name);
    return true;
  }

  diagnostics_.ReportError(
      loc, frontends::ErrorCode::kUnsupportedLowering,
      context + " cannot marshal '" + src_type.name + "' to '" + dst_type.name +
          "' because the IR has no semantics-preserving conversion operation");
  return false;
}

// ============================================================================
// Type Conversion Helpers
// ============================================================================

ir::IRType PloyLowering::PloyTypeToIR(const std::shared_ptr<TypeNode> &type_node) {
  if (!type_node) {
    diagnostics_.ReportError(core::SourceLoc{}, frontends::ErrorCode::kUnsupportedLowering,
                             "missing Poly type reached IR lowering");
    return ir::IRType::Invalid();
  }

  if (auto st = std::dynamic_pointer_cast<SimpleType>(type_node)) {
    // Support both upper-case Poly keywords and lower-case C-style aliases
    if (st->name == "INT" || st->name == "I64" || st->name == "ISIZE" ||
        st->name == "i64" || st->name == "int" || st->name == "int64")
      return ir::IRType::I64(true);
    if (st->name == "I8" || st->name == "i8")
      return ir::IRType::I8(true);
    if (st->name == "I16" || st->name == "i16")
      return ir::IRType::I16(true);
    if (st->name == "I32" || st->name == "i32" || st->name == "int32")
      return ir::IRType::I32(true);
    if (st->name == "FLOAT" || st->name == "F64" || st->name == "f64" ||
        st->name == "float" || st->name == "float64" || st->name == "double")
      return ir::IRType::F64();
    if (st->name == "F32" || st->name == "f32" || st->name == "float32")
      return ir::IRType::F32();
    if (st->name == "BOOL" || st->name == "bool")
      return ir::IRType::I1();
    if (st->name == "STRING" || st->name == "string" || st->name == "str")
      return ir::IRType::Pointer(ir::IRType::I8());
    if (st->name == "VOID" || st->name == "void")
      return ir::IRType::Void();
    if (st->name == "PTR" || st->name == "ptr" || st->name == "pointer")
      return ir::IRType::Pointer(ir::IRType::I8());
    if (st->name == "U8" || st->name == "u8" || st->name == "byte")
      return ir::IRType::I8(false);
    if (st->name == "U16" || st->name == "u16")
      return ir::IRType::I16(false);
    if (st->name == "U32" || st->name == "u32")
      return ir::IRType::I32(false);
    if (st->name == "U64" || st->name == "USIZE" || st->name == "u64")
      return ir::IRType::I64(false);
    auto alias = sema_.TypeAliases().find(st->name);
    if (alias != sema_.TypeAliases().end())
      return CoreTypeToIR(alias->second);
    // Check if it is a known struct name in the environment
    auto it = env_.find(st->name);
    if (it != env_.end() && it->second.type.kind == ir::IRTypeKind::kStruct) {
      return it->second.type;
    }
    diagnostics_.ReportError(type_node->loc, frontends::ErrorCode::kUnsupportedLowering,
                             "type '" + st->name +
                                 "' has no concrete Poly IR layout at this lowering boundary");
    return ir::IRType::Invalid();
  }

  if (auto pt = std::dynamic_pointer_cast<ParameterizedType>(type_node)) {
    if (pt->name == "ARRAY" && !pt->type_args.empty()) {
      ir::IRType elem = PloyTypeToIR(pt->type_args[0]);
      return ir::IRType::Pointer(elem); // Arrays are pointers in IR
    }
    if (pt->name == "LIST" && !pt->type_args.empty()) {
      // Lists are represented as opaque pointers to runtime descriptors
      return ir::IRType::Pointer(ir::IRType::I8());
    }
    if (pt->name == "TUPLE" && !pt->type_args.empty()) {
      // Tuples are struct types with each element as a field
      std::vector<ir::IRType> field_types;
      for (const auto &arg : pt->type_args) {
        field_types.push_back(PloyTypeToIR(arg));
      }
      return ir::IRType::Struct("tuple", field_types);
    }
    if (pt->name == "DICT" && pt->type_args.size() >= 2) {
      // Dicts are opaque pointers to runtime dict descriptors
      return ir::IRType::Pointer(ir::IRType::I8());
    }
    if (pt->name == "OPTION" && !pt->type_args.empty()) {
      // Options are struct { i1 has_value, T value }
      ir::IRType inner = PloyTypeToIR(pt->type_args[0]);
      return ir::IRType::Struct("option", {ir::IRType::I1(), inner});
    }
    diagnostics_.ReportError(type_node->loc, frontends::ErrorCode::kUnsupportedLowering,
                             "parameterized type '" + pt->name +
                                 "' has no concrete Poly IR representation");
    return ir::IRType::Invalid();
  }

  if (auto qt = std::dynamic_pointer_cast<QualifiedType>(type_node)) {
    core::Type ct = core::TypeSystem().MapFromLanguage(qt->language, qt->type_name);
    return CoreTypeToIR(ct);
  }

  if (std::dynamic_pointer_cast<HandleType>(type_node))
    return ir::IRType::Pointer(ir::IRType::I8());

  if (auto fn = std::dynamic_pointer_cast<FunctionType>(type_node)) {
    std::vector<ir::IRType> params;
    params.reserve(fn->param_types.size());
    for (const auto &param : fn->param_types) {
      ir::IRType lowered = PloyTypeToIR(param);
      if (lowered.kind == ir::IRTypeKind::kInvalid)
        return lowered;
      params.push_back(lowered);
    }
    ir::IRType result = fn->return_type ? PloyTypeToIR(fn->return_type) : ir::IRType::Void();
    if (result.kind == ir::IRTypeKind::kInvalid)
      return result;
    return ir::IRType::Pointer(ir::IRType::Function(result, params));
  }

  diagnostics_.ReportError(type_node->loc, frontends::ErrorCode::kUnsupportedLowering,
                           "unrecognized Poly type node has no IR representation");
  return ir::IRType::Invalid();
}

ir::IRType PloyLowering::CoreTypeToIR(const core::Type &ct) {
  switch (ct.kind) {
  case core::TypeKind::kInt:
    switch (ct.bit_width) {
    case 0:
      return ir::IRType::I64(ct.is_signed);
    case 8:
      return ir::IRType::I8(ct.is_signed);
    case 16:
      return ir::IRType::I16(ct.is_signed);
    case 32:
      return ir::IRType::I32(ct.is_signed);
    case 64:
      return ir::IRType::I64(ct.is_signed);
    default:
      diagnostics_.ReportError(core::SourceLoc{}, frontends::ErrorCode::kUnsupportedLowering,
                               "integer width " + std::to_string(ct.bit_width) +
                                   " is not represented by Poly IR");
      return ir::IRType::Invalid();
    }
  case core::TypeKind::kFloat:
    if (ct.bit_width == 0 || ct.bit_width == 64)
      return ir::IRType::F64();
    if (ct.bit_width == 32)
      return ir::IRType::F32();
    diagnostics_.ReportError(core::SourceLoc{}, frontends::ErrorCode::kUnsupportedLowering,
                             "floating-point width " + std::to_string(ct.bit_width) +
                                 " is not represented by Poly IR");
    return ir::IRType::Invalid();
  case core::TypeKind::kBool:
    return ir::IRType::I1();
  case core::TypeKind::kVoid:
    return ir::IRType::Void();
  case core::TypeKind::kString:
    return ir::IRType::Pointer(ir::IRType::I8());
  case core::TypeKind::kPointer:
    if (!ct.type_args.empty())
      return ir::IRType::Pointer(CoreTypeToIR(ct.type_args[0]));
    return ir::IRType::Pointer(ir::IRType::I8());
  case core::TypeKind::kArray:
    // Dynamic arrays / lists are opaque pointers
    return ir::IRType::Pointer(ir::IRType::I8());
  case core::TypeKind::kTuple: {
    // Tuples map to struct types
    std::vector<ir::IRType> fields;
    for (const auto &arg : ct.type_args) {
      fields.push_back(CoreTypeToIR(arg));
    }
    return ir::IRType::Struct("tuple", fields);
  }
  case core::TypeKind::kOptional: {
    // Optional[T] maps to struct { i1, T }
    ir::IRType inner = ct.type_args.empty() ? ir::IRType::I64(true) : CoreTypeToIR(ct.type_args[0]);
    return ir::IRType::Struct("option", {ir::IRType::I1(), inner});
  }
  case core::TypeKind::kStruct:
    // Named structs are opaque pointers unless resolved
    return ir::IRType::Pointer(ir::IRType::I8());
  case core::TypeKind::kGenericInstance:
    // Generic containers (e.g. dict) are opaque pointers
    return ir::IRType::Pointer(ir::IRType::I8());
  case core::TypeKind::kModule:
    // Module references are opaque handles
    return ir::IRType::Pointer(ir::IRType::I8());
  case core::TypeKind::kClass:
    // Foreign class instances are opaque pointers
    return ir::IRType::Pointer(ir::IRType::I8());
  case core::TypeKind::kReference:
    // References are pointers
    if (!ct.type_args.empty())
      return ir::IRType::Pointer(CoreTypeToIR(ct.type_args[0]));
    return ir::IRType::Pointer(ir::IRType::I8());
  case core::TypeKind::kSlice:
    // Slices are opaque pointers (ptr + len at runtime)
    return ir::IRType::Pointer(ir::IRType::I8());
  case core::TypeKind::kEnum:
    // Enum values are represented as integers
    return ir::IRType::I64(true);
  case core::TypeKind::kUnion:
    // Unions are opaque pointers
    return ir::IRType::Pointer(ir::IRType::I8());
  case core::TypeKind::kGenericParam:
    // Unresolved generic parameters map to opaque pointer
    return ir::IRType::Pointer(ir::IRType::I8());
  case core::TypeKind::kAny:
    // Any type maps to opaque pointer (i8*) - more accurate than I64
    return ir::IRType::Pointer(ir::IRType::I8());
  case core::TypeKind::kUnknown:
    // Unknown type reached lowering without being resolved - this is a
    // hard error: the programmer must add an explicit type annotation or
    // LINK declaration with MAP_TYPE.
    diagnostics_.ReportError(core::SourceLoc{}, frontends::ErrorCode::kTypeMismatch,
                             "type '<unknown>' reached IR lowering unresolved �?"
                             "add an explicit type annotation or LINK with MAP_TYPE");
    return ir::IRType::Invalid();
  case core::TypeKind::kFunction:
    // Function types are pointers to function descriptors
    return ir::IRType::Pointer(ir::IRType::I8());
  default:
    diagnostics_.ReportError(core::SourceLoc{}, frontends::ErrorCode::kTypeMismatch,
                             "unrecognized core type kind " +
                                 std::to_string(static_cast<int>(ct.kind)) +
                                 " in CoreTypeToIR �?add explicit type annotation or MAP_TYPE");
    return ir::IRType::Invalid();
  }
}

void PloyLowering::Report(const core::SourceLoc &loc, const std::string &message) {
  diagnostics_.Report(loc, message);
}

// ============================================================================
// DELETE Expression Lowering
// ============================================================================

PloyLowering::EvalResult PloyLowering::LowerDeleteExpression(
    const std::shared_ptr<DeleteExpression> &del_expr) {
  // DELETE(language, object) - generate a destructor / cleanup call
  // For Python objects: calls __del__ / del
  // For C++ objects: calls destructor
  // For Rust objects: calls drop

  EvalResult obj = LowerExpression(del_expr->object);

  std::string lang = del_expr->language;
  std::string delete_func;
  if (lang == "python") {
    delete_func = "__ploy_py_del";
  } else if (lang == "cpp") {
    delete_func = "__ploy_cpp_delete";
  } else if (lang == "rust") {
    delete_func = "__ploy_rust_drop";
  } else if (lang == "java") {
    delete_func = "__ploy_java_release";
  } else if (lang == "dotnet" || lang == "csharp") {
    delete_func = "__ploy_dotnet_dispose";
  } else {
    delete_func = "__ploy_delete_" + AbiLanguageToken(lang);
  }

  // Emit the call to the language-specific cleanup function
  auto call_inst = builder_.MakeCall(delete_func, {obj.value}, ir::IRType::Void());

  // Record the cross-language call descriptor for the linker
  CrossLangCallDescriptor desc;
  desc.stub_name = delete_func;
  desc.source_language = lang;
  desc.target_language = "poly";
  desc.source_function = delete_func;
  desc.target_function = delete_func;
  desc.source_param_types = {obj.type};
  desc.source_return_type = ir::IRType::Void();
  desc.target_return_type = ir::IRType::Void();
  CrossLangCallDescriptor::MarshalOp marshal;
  marshal.kind = CrossLangCallDescriptor::MarshalOp::Kind::kDirect;
  marshal.from = obj.type;
  marshal.to = obj.type;
  desc.param_marshal.push_back(marshal);
  desc.return_marshal.kind = CrossLangCallDescriptor::MarshalOp::Kind::kDirect;
  desc.return_marshal.from = ir::IRType::Void();
  desc.return_marshal.to = ir::IRType::Void();
  desc.lang_version = del_expr->lang_version_pin;
  call_descriptors_.push_back(desc);

  return {call_inst->name, ir::IRType::Void()};
}

// ============================================================================
// EXTEND Declaration Lowering
// ============================================================================

void PloyLowering::LowerExtendDecl(const std::shared_ptr<ExtendDecl> &extend) {
  // EXTEND(language, base_class) AS DerivedName { methods... }
  // Generate vtable dispatch stubs for method overrides.
  // Each method becomes a bridge function: DerivedName_methodname

  for (const auto &method_stmt : extend->methods) {
    if (auto func = std::dynamic_pointer_cast<FuncDecl>(method_stmt)) {
      // Generate a uniquely named bridge function for the override
      std::string bridge_name = "__ploy_extend_" + extend->derived_name + "_" + func->name;

      // Build IR parameter types
      std::vector<std::pair<std::string, ir::IRType>> params;
      // First param is always 'self' pointer for the object
      params.emplace_back("self_ptr", ir::IRType::Pointer(ir::IRType::I8()));
      for (const auto &p : func->params) {
        params.emplace_back(p.name, PloyTypeToIR(p.type));
      }

      ir::IRType ret_type =
          func->return_type ? PloyTypeToIR(func->return_type) : ir::IRType::Void();

      // Create the bridge function
      auto ir_func = ir_ctx_.CreateFunction(bridge_name, ret_type, params);
      auto prev_func = current_function_;
      current_function_ = ir_func;
      builder_.SetCurrentFunction(ir_func);

      auto entry = builder_.CreateBlock("entry");
      builder_.SetInsertPoint(entry);

      // Set up parameter environment
      std::unordered_map<std::string, EnvEntry> saved_env = env_;
      env_.clear();

      // self pointer (implicit first arg)
      env_["self"] = {"self_ptr", ir::IRType::Pointer(ir::IRType::I8())};
      for (const auto &p : func->params) {
        ir::IRType pt = PloyTypeToIR(p.type);
        env_[p.name] = {p.name, pt};
      }

      // Lower the function body
      bool prev_terminated = terminated_;
      terminated_ = false;
      LowerBlockStatements(func->body);

      // Ensure function has a terminator
      if (!terminated_) {
        builder_.MakeReturn();
      }

      current_function_ = prev_func;
      builder_.SetCurrentFunction(prev_func);
      env_ = saved_env;
      terminated_ = prev_terminated;
    }
  }

  // Generate vtable registration call: __ploy_extend_register(lang, base, derived)
  std::string reg_func = "__ploy_extend_register";
  std::string lang_str = builder_.MakeStringLiteral(extend->language, "ext_lang");
  std::string base_str = builder_.MakeStringLiteral(extend->base_class, "ext_base");
  std::string derived_str = builder_.MakeStringLiteral(extend->derived_name, "ext_derived");

  builder_.MakeCall(reg_func, {lang_str, base_str, derived_str}, ir::IRType::Void());

  // Record the registration call descriptor for the linker
  CrossLangCallDescriptor desc;
  desc.stub_name = reg_func;
  desc.source_language = extend->language;
  desc.target_language = "poly";
  desc.source_function = reg_func;
  desc.target_function = reg_func;
  desc.source_return_type = ir::IRType::Void();
  desc.target_return_type = ir::IRType::Void();
  desc.return_marshal.kind = CrossLangCallDescriptor::MarshalOp::Kind::kDirect;
  desc.return_marshal.from = ir::IRType::Void();
  desc.return_marshal.to = ir::IRType::Void();
  desc.lang_version = extend->lang_version_pin;
  call_descriptors_.push_back(desc);
}

// ============================================================================
// AWAIT Lowering
// ============================================================================
//
// Lowers `AWAIT <expr>` to a call into the async runtime bridge.  The
// operand is evaluated to a future-handle pointer (opaque `i8*`) and
// passed to `__ploy_rt_await`, which yields back to the cooperative
// scheduler until the future resolves and then returns the resolved
// payload pointer.  Sema has already enforced that the surrounding
// FUNC carries the `ASYNC` modifier (since v1.14.0).
PloyLowering::EvalResult
PloyLowering::LowerAwaitExpression(const std::shared_ptr<AwaitExpression> &await) {
  if (!await || !await->operand) {
    return {"", ir::IRType::Invalid()};
  }
  EvalResult operand = LowerExpression(await->operand);
  std::string handle = operand.value.empty() ? std::string("0") : operand.value;
  auto call = builder_.MakeCall("__ploy_rt_await", {handle},
                                ir::IRType::Pointer(ir::IRType::I8(true)),
                                "await.value");
  std::string result = call ? call->name : std::string("0");
  return {result, ir::IRType::Pointer(ir::IRType::I8(true))};
}

// Postfix `?` needs a stable OPTION tag/payload representation and an exact
// early-return value. Treating the aggregate as a scalar truth value and
// returning the whole aggregate as its payload is not a compatibility mode.
PloyLowering::EvalResult
PloyLowering::LowerOptionUnwrapExpression(
    const std::shared_ptr<OptionUnwrapExpression> &unwrap) {
  diagnostics_.ReportError(
      unwrap ? unwrap->loc : core::SourceLoc{}, frontends::ErrorCode::kUnsupportedLowering,
      "postfix '?' requires OPTION tag/payload extraction that the Poly IR ABI does not yet "
      "represent");
  return {"", ir::IRType::Invalid()};
}

} // namespace polyglot::ploy

/** @} */
