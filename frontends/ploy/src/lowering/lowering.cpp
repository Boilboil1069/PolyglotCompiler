/**
 * @file     lowering.cpp
 * @brief    Ploy language frontend implementation
 *
 * @ingroup  Frontend / Ploy
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
  std::string mangled = "__ploy_bridge_" + target_lang + "_" + source_lang + "_";
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
// spellings; the latter exists so a hand-written .ploy that wants to spell
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
  }
  // BREAK and CONTINUE are handled at a higher level (loop lowering)
  // MAP_TYPE is metadata only, no IR generation needed
  // CONFIG VENV/CONDA/UV/PIPENV/POETRY is metadata only, processed during semantic analysis
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
    module_sym += import->language + "_";
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
      // The function is already created �?mark it for export
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
  // Save outer context �?nested functions (e.g. inside PIPELINE) must not
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

  LowerBlockStatements(func->body);

  // Add implicit void return if not terminated
  if (!terminated_) {
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
  // the sema symbol table to get the inferred type.  Fall back to I64 only
  // as a last resort.
  ir::IRType var_type = ir::IRType::I64(true);
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
    if (init_result.type.kind != ir::IRTypeKind::kInvalid) {
      var_type = init_result.type;
    }
    if (var->is_mutable) {
      // Mutable VAR: use alloca/store/load pattern so that
      // re-assignments inside loops produce correct SSA.
      auto alloca_inst = builder_.MakeAlloca(var_type, var->name);
      builder_.MakeStore(alloca_inst->name, init_result.value);
      env_[var->name] = EnvEntry{alloca_inst->name, var_type, true};
    } else {
      // Immutable LET: bind the SSA value directly.
      env_[var->name] = EnvEntry{init_result.value, var_type, false};
    }
  } else {
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
std::string PloyLowering::EnsureI1(const EvalResult &val) {
  const ir::IRType &t = val.type;
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

  // Invalid / placeholder — treat as always-true (non-zero) for safety.
  // This avoids verifier failures for opaque cross-lang return values.
  if (t.kind == ir::IRTypeKind::kInvalid || t.is_placeholder) {
    auto cmp = builder_.MakeBinary(ir::BinaryInstruction::Op::kCmpNe, val.value, "0", "tobool");
    cmp->type = ir::IRType::I1();
    return cmp->name;
  }

  // Fallthrough: return as-is and let the verifier decide.
  return val.value;
}

void PloyLowering::LowerIfStatement(const std::shared_ptr<IfStatement> &if_stmt) {
  EvalResult cond = LowerExpression(if_stmt->condition);
  std::string cond_i1 = EnsureI1(cond);

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

void PloyLowering::LowerWhileStatement(const std::shared_ptr<WhileStatement> &while_stmt) {
  auto cond_bb = builder_.CreateBlock("while.cond");
  auto body_bb = builder_.CreateBlock("while.body");
  auto exit_bb = builder_.CreateBlock("while.exit");

  builder_.MakeBranch(cond_bb.get());

  // Condition block
  builder_.SetInsertPoint(cond_bb);
  EvalResult cond = LowerExpression(while_stmt->condition);
  std::string cond_i1 = EnsureI1(cond);
  builder_.MakeCondBranch(cond_i1, body_bb.get(), exit_bb.get());

  // Body block
  builder_.SetInsertPoint(body_bb);
  terminated_ = false;
  LowerBlockStatements(while_stmt->body);
  if (!terminated_) {
    builder_.MakeBranch(cond_bb.get());
  }

  // Exit
  builder_.SetInsertPoint(exit_bb);
  terminated_ = false;
}

void PloyLowering::LowerForStatement(const std::shared_ptr<ForStatement> &for_stmt) {
  // Lower FOR as a WHILE over the iterable
  // For range iterables (0..10), generate an index-based loop
  EvalResult iter = LowerExpression(for_stmt->iterable);

  auto cond_bb = builder_.CreateBlock("for.cond");
  auto body_bb = builder_.CreateBlock("for.body");
  auto exit_bb = builder_.CreateBlock("for.exit");

  // Initialize iterator variable
  auto idx_alloca = builder_.MakeAlloca(ir::IRType::I64(true), for_stmt->iterator_name + ".idx");
  builder_.MakeStore(idx_alloca->name, "0");
  builder_.MakeBranch(cond_bb.get());

  // Condition: check if index is within range
  builder_.SetInsertPoint(cond_bb);
  auto idx_load = builder_.MakeLoad(idx_alloca->name, ir::IRType::I64(true), "idx.val");
  // Comparison against the iterable length (simplified: use the iter value as upper bound)
  auto cmp = builder_.MakeBinary(ir::BinaryInstruction::Op::kCmpSlt, idx_load->name, iter.value,
                                 "for.cond.cmp");
  cmp->type = ir::IRType::I1();
  builder_.MakeCondBranch(cmp->name, body_bb.get(), exit_bb.get());

  // Body block
  builder_.SetInsertPoint(body_bb);
  env_[for_stmt->iterator_name] = EnvEntry{idx_load->name, ir::IRType::I64(true)};
  terminated_ = false;
  LowerBlockStatements(for_stmt->body);

  // Increment
  if (!terminated_) {
    auto idx_reload = builder_.MakeLoad(idx_alloca->name, ir::IRType::I64(true), "idx.next.load");
    auto inc =
        builder_.MakeBinary(ir::BinaryInstruction::Op::kAdd, idx_reload->name, "1", "idx.inc");
    inc->type = ir::IRType::I64(true);
    builder_.MakeStore(idx_alloca->name, inc->name);
    builder_.MakeBranch(cond_bb.get());
  }

  builder_.SetInsertPoint(exit_bb);
  env_.erase(for_stmt->iterator_name);
  terminated_ = false;
}

void PloyLowering::LowerMatchStatement(const std::shared_ptr<MatchStatement> &match_stmt) {
  EvalResult match_val = LowerExpression(match_stmt->value);
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
      if (!match_stmt->cases[i].pattern) {
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
  // value (no copy needed since `.ploy` is immutable-by-default).

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
    if (auto tp = std::dynamic_pointer_cast<TypePattern>(pat)) {
      // Static type-guard: refinement is enforced by sema; at runtime the
      // scrutinee's IR is reused without cast (this is sound because the
      // outer flow guarantees the scrutinee's dynamic type matches when
      // the static type system already proved it does).
      if (!tp->name.empty()) bindings.emplace_back(tp->name, match_val);
      return "1";
    }
    if (auto ctor = std::dynamic_pointer_cast<ConstructorPattern>(pat)) {
      // OPTION lowering: Some / None compare against a sentinel `0` for
      // None and `1` for Some.  When `Some(sub)` is used, bindings from
      // the inner pattern are derived from the scrutinee (the boxed
      // payload representation is opaque to ploy at this stage).
      if (ctor->name == "None") {
        auto cmp = builder_.MakeBinary(ir::BinaryInstruction::Op::kCmpEq,
                                       match_val.value, "0", "match.is_none");
        cmp->type = ir::IRType::I1();
        return cmp->name;
      }
      if (ctor->name == "Some") {
        auto cmp = builder_.MakeBinary(ir::BinaryInstruction::Op::kCmpNe,
                                       match_val.value, "0", "match.is_some");
        cmp->type = ir::IRType::I1();
        if (!ctor->args.empty()) {
          // For now we forward the scrutinee SSA value into any inner
          // binding; richer payload extraction is deferred until the
          // OPTION layout is finalised in the runtime ABI.
          std::vector<std::pair<std::string, EvalResult>> inner_b;
          (void) lower_predicate(ctor->args.front(), inner_b);
          for (auto &b : inner_b) bindings.push_back(std::move(b));
        }
        return cmp->name;
      }
      // Generic nominal constructors: conservatively always-true; the
      // structural check is delegated to the foreign runtime.
      return "1";
    }
    if (auto tup = std::dynamic_pointer_cast<TuplePattern>(pat)) {
      // No tuple ABI yet; lower to always-true and delegate to the body
      // (the bindings still receive the whole scrutinee).
      for (const auto &e : tup->elements) {
        std::vector<std::pair<std::string, EvalResult>> inner_b;
        (void) lower_predicate(e, inner_b);
        for (auto &b : inner_b) bindings.push_back(std::move(b));
      }
      return "1";
    }
    if (auto sp = std::dynamic_pointer_cast<StructPattern>(pat)) {
      for (const auto &fp : sp->fields) {
        if (fp.sub) {
          std::vector<std::pair<std::string, EvalResult>> inner_b;
          (void) lower_predicate(fp.sub, inner_b);
          for (auto &b : inner_b) bindings.push_back(std::move(b));
        } else {
          // Shorthand `field` => `field: <bind>`; reuse the scrutinee.
          bindings.emplace_back(fp.name, match_val);
        }
      }
      return "1";
    }
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
      std::string guard_i1 = EnsureI1(guard_val);
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
    builder_.MakeReturn(val.value);
  } else {
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

// Decode the small, fixed set of backslash escapes that .ploy promises to
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
  if (auto bin = std::dynamic_pointer_cast<BinaryExpression>(expr)) {
    return LowerBinaryExpression(bin);
  }
  if (auto unary = std::dynamic_pointer_cast<UnaryExpression>(expr)) {
    return LowerUnaryExpression(unary);
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
    // Range expression �?lower the end value as the iteration bound
    return LowerExpression(range->end);
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
        // Mutable VAR: store to the alloca address
        builder_.MakeStore(it->second.ir_name, rhs.value);
        // Keep the env entry pointing to the alloca (type may change)
        it->second.type = rhs.type;
      } else {
        // Immutable LET or unknown: direct SSA rebind
        env_[id->name] = EnvEntry{rhs.value, rhs.type, false};
      }
      return rhs;
    }
    return rhs;
  }

  EvalResult left = LowerExpression(bin->left);
  EvalResult right = LowerExpression(bin->right);

  /** @name Smart type inference for binary operands */
  /** @{ */
  // When one operand comes from a cross-language call, its IR type may be
  // opaque pointer (Pointer(I8)), I64 placeholder, or Invalid.  We need to
  // infer the correct numeric type from the other operand or from the
  // AST-level type annotation to select integer vs float operations.

  // Helper: determine if a type is "opaque" (needs type inference)
  auto is_opaque = [](const ir::IRType &t) -> bool {
    if (t.is_placeholder)
      return true;
    if (t.kind == ir::IRTypeKind::kPointer)
      return true;
    if (t.kind == ir::IRTypeKind::kInvalid)
      return true;
    return false;
  };

  // Helper: determine if a type is definitely float
  auto is_float_type = [](const ir::IRType &t) -> bool {
    return t.kind == ir::IRTypeKind::kF32 || t.kind == ir::IRTypeKind::kF64;
  };

  // Helper: determine if a type is definitely integer
  [[maybe_unused]] auto is_int_type = [](const ir::IRType &t) -> bool {
    return t.kind == ir::IRTypeKind::kI1 || t.kind == ir::IRTypeKind::kI8 ||
           t.kind == ir::IRTypeKind::kI16 || t.kind == ir::IRTypeKind::kI32 ||
           t.kind == ir::IRTypeKind::kI64;
  };

  // Resolve opaque types: if one side is opaque, adopt the other side's type
  ir::IRType effective_left = left.type;
  ir::IRType effective_right = right.type;
  if (is_opaque(left.type) && !is_opaque(right.type)) {
    effective_left = right.type;
  } else if (is_opaque(right.type) && !is_opaque(left.type)) {
    effective_right = left.type;
  } else if (is_opaque(left.type) && is_opaque(right.type)) {
    // Both opaque: fall back to I64 (integer) unless the AST expression
    // contains a float literal hint
    effective_left = ir::IRType::I64(true);
    effective_right = ir::IRType::I64(true);
  }

  // Insert casts for opaque operands.
  // Strategy: always convert opaque (Pointer / placeholder) to I64 via
  // PtrToInt.  If the other side is float, we still use integer arithmetic
  // because the opaque value has no well-defined float representation.
  // This keeps the cast chain legal (PtrToInt is valid, Bitcast Ptr→Float
  // is NOT).
  std::string left_val = left.value;
  std::string right_val = right.value;
  ir::IRType resolved_left = effective_left;
  ir::IRType resolved_right = effective_right;
  if (is_opaque(left.type)) {
    if (left.type.kind == ir::IRTypeKind::kPointer ||
        left.type.kind == ir::IRTypeKind::kReference) {
      auto cast = builder_.MakeCast(ir::CastInstruction::CastKind::kPtrToInt, left.value,
                                    ir::IRType::I64(true));
      left_val = cast->name;
    }
    resolved_left = ir::IRType::I64(true);
  }
  if (is_opaque(right.type)) {
    if (right.type.kind == ir::IRTypeKind::kPointer ||
        right.type.kind == ir::IRTypeKind::kReference) {
      auto cast = builder_.MakeCast(ir::CastInstruction::CastKind::kPtrToInt, right.value,
                                    ir::IRType::I64(true));
      right_val = cast->name;
    }
    resolved_right = ir::IRType::I64(true);
  }

  // If one side was opaque (now I64) and the other is float, downgrade to
  // integer arithmetic so that we don't need an unavailable SIToFP cast.
  if (is_opaque(left.type) || is_opaque(right.type)) {
    effective_left = resolved_left;
    effective_right = resolved_right;
  }

  // Determine operation
  ir::BinaryInstruction::Op op;
  ir::IRType result_type = effective_left;
  bool is_float = is_float_type(effective_left) || is_float_type(effective_right);

  if (bin->op == "+") {
    op = is_float ? ir::BinaryInstruction::Op::kFAdd : ir::BinaryInstruction::Op::kAdd;
    if (is_float)
      result_type = ir::IRType::F64();
  } else if (bin->op == "-") {
    op = is_float ? ir::BinaryInstruction::Op::kFSub : ir::BinaryInstruction::Op::kSub;
    if (is_float)
      result_type = ir::IRType::F64();
  } else if (bin->op == "*") {
    op = is_float ? ir::BinaryInstruction::Op::kFMul : ir::BinaryInstruction::Op::kMul;
    if (is_float)
      result_type = ir::IRType::F64();
  } else if (bin->op == "/") {
    op = is_float ? ir::BinaryInstruction::Op::kFDiv : ir::BinaryInstruction::Op::kSDiv;
    if (is_float)
      result_type = ir::IRType::F64();
  } else if (bin->op == "%") {
    op = is_float ? ir::BinaryInstruction::Op::kFRem : ir::BinaryInstruction::Op::kSRem;
    if (is_float)
      result_type = ir::IRType::F64();
  } else if (bin->op == "==") {
    op = is_float ? ir::BinaryInstruction::Op::kCmpFoe : ir::BinaryInstruction::Op::kCmpEq;
    result_type = ir::IRType::I1();
  } else if (bin->op == "!=") {
    op = is_float ? ir::BinaryInstruction::Op::kCmpFne : ir::BinaryInstruction::Op::kCmpNe;
    result_type = ir::IRType::I1();
  } else if (bin->op == "<") {
    op = is_float ? ir::BinaryInstruction::Op::kCmpFlt : ir::BinaryInstruction::Op::kCmpSlt;
    result_type = ir::IRType::I1();
  } else if (bin->op == ">") {
    op = is_float ? ir::BinaryInstruction::Op::kCmpFgt : ir::BinaryInstruction::Op::kCmpSgt;
    result_type = ir::IRType::I1();
  } else if (bin->op == "<=") {
    op = is_float ? ir::BinaryInstruction::Op::kCmpFle : ir::BinaryInstruction::Op::kCmpSle;
    result_type = ir::IRType::I1();
  } else if (bin->op == ">=") {
    op = is_float ? ir::BinaryInstruction::Op::kCmpFge : ir::BinaryInstruction::Op::kCmpSge;
    result_type = ir::IRType::I1();
  } else if (bin->op == "&&") {
    op = ir::BinaryInstruction::Op::kAnd;
    result_type = ir::IRType::I1();
  } else if (bin->op == "||") {
    op = ir::BinaryInstruction::Op::kOr;
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
  // the parameter name in the signature.
  auto sig_it = sema_.KnownSignatures().find(callee_name);
  if (sig_it != sema_.KnownSignatures().end() && !sig_it->second.param_names.empty()) {
    const auto &param_names = sig_it->second.param_names;
    std::vector<std::string> reordered(arg_names.size());
    // First pass: place named args in their correct positions.
    std::vector<bool> placed(arg_names.size(), false);
    for (size_t i = 0; i < arg_names.size(); ++i) {
      auto lbl_it = named_arg_labels_.find(arg_names[i]);
      if (lbl_it != named_arg_labels_.end()) {
        for (size_t j = 0; j < param_names.size(); ++j) {
          if (param_names[j] == lbl_it->second && j < reordered.size()) {
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
        while (pos < reordered.size() && placed[pos])
          ++pos;
        if (pos < reordered.size()) {
          reordered[pos] = arg_names[i];
          ++pos;
        }
      }
    }
    arg_names = reordered;
  }

  // Resolve the return type from sema's known signatures instead of
  // always using I64.  If no signature is found, fall back to I64.
  ir::IRType call_ret_type = ir::IRType::I64(true);
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

  auto inst = builder_.MakeCall(callee_name, arg_names, call_ret_type, "");
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

  // Generate the stub name for the cross-language call.
  // Look up the LINK entry matching the call target to use the correct
  // language pair for name mangling: __ploy_bridge_<target_lang>_<source_lang>_<sym>.
  std::string stub_name;
  {
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
      // No matching LINK entry — fall back to ploy→<language> naming.
      stub_name = MangleStubName("ploy", call->language, call->function, call->lang_version_pin);
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
            call->loc, frontends::ErrorCode::kTypeMismatch,
            "cross-language call to '" + call->function +
                "' has unknown return type/signature (strict mode rejects fallback lowering)");
      } else {
        diagnostics_.ReportWarning(call->loc, frontends::ErrorCode::kGenericWarning,
                                   "cross-language call to '" + call->function +
                                       "' has unknown return type; defaulting to opaque pointer");
      }
    }
  }

  // Record the cross-language call descriptor
  CrossLangCallDescriptor desc;
  desc.stub_name = stub_name;
  desc.source_language = call->language;
  desc.target_language = "ploy";
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
      MangleStubName("ploy", new_expr->language, new_expr->class_name + "::__init__",
                     new_expr->lang_version_pin);

  // Resolve the object type from sema �?the sema now performs full type
  // resolution via ResolveObjectType, so we can trust the symbol table.
  ir::IRType obj_type = ir::IRType::Pointer(ir::IRType::Void());
  {
    auto sym_it = sema_.Symbols().find(new_expr->class_name);
    if (sym_it != sema_.Symbols().end() && sym_it->second.type.kind != core::TypeKind::kAny &&
        sym_it->second.type.kind != core::TypeKind::kUnknown &&
        sym_it->second.type.kind != core::TypeKind::kInvalid) {
      ir::IRType inner = CoreTypeToIR(sym_it->second.type);
      if (inner.kind != ir::IRTypeKind::kInvalid) {
        obj_type = ir::IRType::Pointer(inner);
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
  desc.target_language = "ploy";
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

  // Lower method arguments �?the object is passed as the first argument
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
  std::string stub_name = MangleStubName("ploy", method_call->language, method_call->method_name,
                                         method_call->lang_version_pin);

  // Resolve return type from sema known signatures.  Try the method name
  // directly, then try qualified with the object type if available.
  ir::IRType method_ret_type = ir::IRType::Pointer(ir::IRType::Void());
  const FunctionSignature *sig = nullptr;
  {
    auto sig_it = sema_.KnownSignatures().find(method_call->method_name);
    if (sig_it != sema_.KnownSignatures().end() &&
        sig_it->second.return_type.kind != core::TypeKind::kAny &&
        sig_it->second.return_type.kind != core::TypeKind::kUnknown &&
        sig_it->second.return_type.kind != core::TypeKind::kInvalid) {
      method_ret_type = CoreTypeToIR(sig_it->second.return_type);
    }
  }

  // Record the cross-language call descriptor for the method
  CrossLangCallDescriptor desc;
  desc.stub_name = stub_name;
  desc.source_language = method_call->language;
  desc.target_language = "ploy";
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
      MangleStubName("ploy", get_attr->language, "__getattr__" + get_attr->attr_name,
                     get_attr->lang_version_pin);

  // Attribute access returns an opaque pointer by default �?the exact
  // type depends on the foreign object's schema which is unknown at
  // compile time.  Use Pointer(I8) as a generic handle.
  ir::IRType attr_ret_type = ir::IRType::Pointer(ir::IRType::I8());

  // Record the cross-language call descriptor
  CrossLangCallDescriptor desc;
  desc.stub_name = stub_name;
  desc.source_language = get_attr->language;
  desc.target_language = "ploy";
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
      MangleStubName("ploy", set_attr->language, "__setattr__" + set_attr->attr_name,
                     set_attr->lang_version_pin);

  // Record the cross-language call descriptor
  CrossLangCallDescriptor desc;
  desc.stub_name = stub_name;
  desc.source_language = set_attr->language;
  desc.target_language = "ploy";
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

  // Emit the call �?returns void but we return the value for expression chaining
  builder_.MakeCall(stub_name, arg_names, ir::IRType::Void(), "");
  return {val.value, val.type};
}

// ============================================================================
// WITH Statement Lowering
// ============================================================================

void PloyLowering::LowerWithStatement(const std::shared_ptr<WithStatement> &with_stmt) {
  // WITH(lang, resource) AS name { body }
  // Lowered to:
  //   1. Evaluate the resource expression
  //   2. Call __enter__ on the resource
  //   3. Bind the result to 'name'
  //   4. Execute body
  //   5. Call __exit__ on the resource (even on error)

  // Step 1: Evaluate resource
  EvalResult resource = LowerExpression(with_stmt->resource_expr);

  // Step 2: Call __enter__ on the resource
  std::string enter_stub = MangleStubName("ploy", with_stmt->language, "__enter__",
                                          with_stmt->lang_version_pin);
  std::vector<std::string> enter_args = {resource.value};

  // Resolve the enter return type �?use typed pointer if class is known
  ir::IRType enter_ret_type = ir::IRType::Pointer(ir::IRType::Void());
  {
    // Check if sema resolved a concrete type for the __enter__ return
    auto sym_it = sema_.Symbols().find(with_stmt->var_name);
    if (sym_it != sema_.Symbols().end() && sym_it->second.type.kind != core::TypeKind::kAny &&
        sym_it->second.type.kind != core::TypeKind::kUnknown &&
        sym_it->second.type.kind != core::TypeKind::kInvalid) {
      ir::IRType resolved = CoreTypeToIR(sym_it->second.type);
      if (resolved.kind != ir::IRTypeKind::kInvalid) {
        enter_ret_type = resolved;
      }
    }
  }
  auto enter_result = builder_.MakeCall(enter_stub, enter_args, enter_ret_type, "");

  // Record __enter__ call descriptor
  CrossLangCallDescriptor enter_desc;
  enter_desc.stub_name = enter_stub;
  enter_desc.source_language = with_stmt->language;
  enter_desc.target_language = "ploy";
  enter_desc.source_function = "__enter__";
  enter_desc.target_function = enter_stub;
  enter_desc.source_param_types = {resource.type};
  enter_desc.source_return_type = enter_ret_type;
  enter_desc.target_return_type = enter_ret_type;
  CrossLangCallDescriptor::MarshalOp enter_marshal;
  enter_marshal.kind = CrossLangCallDescriptor::MarshalOp::Kind::kDirect;
  enter_marshal.from = resource.type;
  enter_marshal.to = resource.type;
  enter_desc.param_marshal.push_back(enter_marshal);
  enter_desc.return_marshal.kind = CrossLangCallDescriptor::MarshalOp::Kind::kDirect;
  enter_desc.return_marshal.from = enter_ret_type;
  enter_desc.return_marshal.to = enter_ret_type;
  enter_desc.lang_version = with_stmt->lang_version_pin;
  call_descriptors_.push_back(enter_desc);

  // Step 3: Bind the result to the variable name
  env_[with_stmt->var_name] = EnvEntry{enter_result->name, enter_result->type};

  // Step 4: Execute body
  LowerBlockStatements(with_stmt->body);

  // Step 5: Call __exit__ on the resource
  std::string exit_stub = MangleStubName("ploy", with_stmt->language, "__exit__",
                                         with_stmt->lang_version_pin);
  std::vector<std::string> exit_args = {resource.value};
  builder_.MakeCall(exit_stub, exit_args, ir::IRType::Void(), "");

  // Record __exit__ call descriptor (shared by normal and unwind paths)
  CrossLangCallDescriptor exit_desc;
  exit_desc.stub_name = exit_stub;
  exit_desc.source_language = with_stmt->language;
  exit_desc.target_language = "ploy";
  exit_desc.source_function = "__exit__";
  exit_desc.target_function = exit_stub;
  exit_desc.source_param_types = {resource.type};
  exit_desc.source_return_type = ir::IRType::Void();
  exit_desc.target_return_type = ir::IRType::Void();
  CrossLangCallDescriptor::MarshalOp exit_marshal;
  exit_marshal.kind = CrossLangCallDescriptor::MarshalOp::Kind::kDirect;
  exit_marshal.from = resource.type;
  exit_marshal.to = resource.type;
  exit_desc.param_marshal.push_back(exit_marshal);
  exit_desc.return_marshal.kind = CrossLangCallDescriptor::MarshalOp::Kind::kDirect;
  exit_desc.return_marshal.from = ir::IRType::Void();
  exit_desc.return_marshal.to = ir::IRType::Void();
  exit_desc.lang_version = with_stmt->lang_version_pin;
  call_descriptors_.push_back(exit_desc);
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

  // If source and target types match, pass through directly
  if (src.type.kind == target_type.kind) {
    return src;
  }

  // Generate conversion code based on types
  std::string dst_name = src.value + ".converted";
  GenerateMarshalCode(src.value, src.type, target_type, dst_name);
  return {dst_name, target_type};
}

PloyLowering::EvalResult PloyLowering::LowerListLiteral(const std::shared_ptr<ListLiteral> &list) {
  // Create a runtime list via __ploy_rt_list_create
  ir::IRType ptr_type = ir::IRType::Pointer(ir::IRType::I8());

  // Determine element size (default to 8 bytes for i64)
  std::string elem_size_val = "8";

  // Call __ploy_rt_list_create(elem_size, initial_capacity)
  std::string capacity_val = std::to_string(list->elements.size());
  auto create_call = builder_.MakeCall("__ploy_rt_list_create", {elem_size_val, capacity_val},
                                       ptr_type, "list.ptr");

  // Push each element
  for (const auto &elem : list->elements) {
    EvalResult e = LowerExpression(elem);
    // Allocate space for the element and store it
    auto alloca_inst = builder_.MakeAlloca(e.type, e.value + ".addr");
    builder_.MakeStore(alloca_inst->name, e.value);
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
    elem_types.push_back(e.type);
    elem_values.push_back(e.value);
  }

  ir::IRType tuple_type = ir::IRType::Struct("tuple", elem_types);

  // Allocate the tuple on the stack
  auto alloca_inst = builder_.MakeAlloca(tuple_type, "tuple.addr");

  // Store each element at its field offset
  for (size_t i = 0; i < elem_values.size(); ++i) {
    std::string field_ptr = alloca_inst->name + ".field" + std::to_string(i);
    // GEP to the field, then store
    auto gep = builder_.MakeGEP(alloca_inst->name, tuple_type, {i}, field_ptr);
    builder_.MakeStore(gep->name, elem_values[i]);
  }

  return {alloca_inst->name, tuple_type};
}

PloyLowering::EvalResult PloyLowering::LowerDictLiteral(const std::shared_ptr<DictLiteral> &dict) {
  // Create a runtime dict via __ploy_rt_dict_create
  ir::IRType ptr_type = ir::IRType::Pointer(ir::IRType::I8());

  // Default key and value sizes (8 bytes each for i64)
  std::string key_size_val = "8";
  std::string value_size_val = "8";

  auto create_call = builder_.MakeCall("__ploy_rt_dict_create", {key_size_val, value_size_val},
                                       ptr_type, "dict.ptr");

  // Insert each entry
  for (const auto &entry : dict->entries) {
    EvalResult key = LowerExpression(entry.key);
    EvalResult val = LowerExpression(entry.value);

    // Allocate and store key and value for passing by pointer
    auto key_alloca = builder_.MakeAlloca(key.type, "dict.key.addr");
    builder_.MakeStore(key_alloca->name, key.value);
    auto val_alloca = builder_.MakeAlloca(val.type, "dict.val.addr");
    builder_.MakeStore(val_alloca->name, val.value);

    builder_.MakeCall("__ploy_rt_dict_insert",
                      {create_call->name, key_alloca->name, val_alloca->name}, ir::IRType::Void(),
                      "");
  }

  return {create_call->name, ptr_type};
}

PloyLowering::EvalResult PloyLowering::LowerStructLiteral(
    const std::shared_ptr<StructLiteral> &struct_lit) {
  // Look up the struct type from the environment
  auto it = env_.find(struct_lit->struct_name);
  ir::IRType struct_type = ir::IRType::I64(true); // Default fallback
  if (it != env_.end()) {
    struct_type = it->second.type;
  }

  // Allocate the struct on the stack
  auto alloca_inst = builder_.MakeAlloca(struct_type, struct_lit->struct_name + ".val");

  // Lower and store each field
  for (size_t i = 0; i < struct_lit->fields.size(); ++i) {
    EvalResult field_val = LowerExpression(struct_lit->fields[i].value);
    std::string field_ptr = alloca_inst->name + "." + struct_lit->fields[i].name;
    auto gep = builder_.MakeGEP(alloca_inst->name, struct_type, {i}, field_ptr);
    builder_.MakeStore(gep->name, field_val.value);
  }

  return {alloca_inst->name, struct_type};
}

// ============================================================================
// Link Stub Generation
// ============================================================================

void PloyLowering::GenerateLinkStub(const LinkEntry &link) {
  std::string stub_name =
      MangleStubName(link.target_language, link.source_language, link.target_symbol);

  // Create the bridge function
  // The stub marshals arguments from target calling convention to source,
  // calls the source function, and marshals the return value back.

  // Resolve the return type from the known function signature registered
  // during semantic analysis.
  ir::IRType ret_type = ir::IRType::Pointer(ir::IRType::Void());
  // Look up the function signature from the sema public accessor.
  const FunctionSignature *sig = nullptr;
  {
    auto it = sema_.KnownSignatures().find(link.target_symbol);
    if (it != sema_.KnownSignatures().end()) {
      sig = &it->second;
    }
  }
  if (sig && sig->return_type.kind != core::TypeKind::kAny &&
      sig->return_type.kind != core::TypeKind::kUnknown &&
      sig->return_type.kind != core::TypeKind::kInvalid) {
    ret_type = CoreTypeToIR(sig->return_type);
  } else if (sema_.IsStrictMode()) {
    diagnostics_.ReportError(link.defined_at, frontends::ErrorCode::kSignatureMissing,
                             "LINK stub for '" + link.target_symbol +
                                 "' has no resolved return type in strict mode");
    return;
  }

  // For function links, create a wrapper function
  if (link.kind == LinkDecl::LinkKind::kFunction) {
    // Build the parameter list from the function signature registered
    // during semantic analysis.  Each parameter gets its resolved type
    // so the stub faithfully mirrors the real calling convention.
    std::vector<std::pair<std::string, ir::IRType>> params;
    if (sig && sig->param_count_known && sig->param_count > 0) {
      for (size_t i = 0; i < sig->param_count; ++i) {
        ir::IRType pt = ir::IRType::I64(true);
        if (i < sig->param_types.size()) {
          pt = CoreTypeToIR(sig->param_types[i]);
        }
        params.emplace_back("arg" + std::to_string(i), pt);
      }
    } else if (!link.param_mappings.empty()) {
      // Fall back to MAP_TYPE entries when no explicit signature is available.
      for (size_t i = 0; i < link.param_mappings.size(); ++i) {
        ir::IRType pt = ir::IRType::I64(true);
        if (!link.param_mappings[i].target_type.empty()) {
          core::Type ct =
              core::TypeSystem().MapFromLanguage(link.param_mappings[i].target_language.empty()
                                                     ? link.target_language
                                                     : link.param_mappings[i].target_language,
                                                 link.param_mappings[i].target_type);
          pt = CoreTypeToIR(ct);
        }
        params.emplace_back("arg" + std::to_string(i), pt);
      }
    } else {
      // No signature information �?in strict mode this is an error because
      // the generated stub will have an incorrect calling convention.
      // In permissive mode, fall back to a single opaque i64 argument
      // with a warning so the pipeline can continue.
      if (sema_.IsStrictMode()) {
        diagnostics_.ReportError(
            link.defined_at, frontends::ErrorCode::kTypeMismatch,
            "LINK stub for '" + link.target_symbol +
                "' has no signature information (strict mode rejects opaque fallback)");
        return;
      } else {
        diagnostics_.ReportWarning(
            link.defined_at, frontends::ErrorCode::kGenericWarning,
            "LINK stub for '" + link.target_symbol +
                "' has no signature information; defaulting to single opaque i64 argument");
        params.emplace_back("arg0", ir::IRType::I64(true));
      }
    }

    auto fn = ir_ctx_.CreateFunction(stub_name, ret_type, params);
    auto saved_fn = current_function_;
    current_function_ = fn;
    builder_.SetCurrentFunction(fn);

    auto entry = builder_.CreateBlock("stub.entry");
    builder_.SetInsertPoint(entry);

    // Marshal arguments (type conversion if needed)
    std::vector<std::string> call_args;
    for (const auto &p : params) {
      // Apply marshalling based on MAP_TYPE entries
      bool marshalled = false;
      for (const auto &mapping : link.param_mappings) {
        // If a mapping exists for this parameter type, generate conversion code
        if (!mapping.source_type.empty() && !mapping.target_type.empty()) {
          GenerateMarshalCode(p.first, p.second, p.second, p.first + ".marshalled");
          call_args.push_back(p.first + ".marshalled");
          marshalled = true;
          break;
        }
      }
      if (!marshalled) {
        call_args.push_back(p.first);
      }
    }

    // Construct the source function's mangled name
    std::string source_func = link.source_symbol;
    for (char &c : source_func) {
      if (c == ':')
        c = '_';
    }

    // Call the source function
    auto call_inst = builder_.MakeCall(source_func, call_args, ret_type, "result");

    // Marshal return value back
    builder_.MakeReturn(call_inst->name);

    current_function_ = saved_fn;
    builder_.SetCurrentFunction(saved_fn);
  } else if (link.kind == LinkDecl::LinkKind::kVariable) {
    // For variable links, create a global alias
    std::string source_var = link.source_symbol;
    for (char &c : source_var) {
      if (c == ':')
        c = '_';
    }
    ir_ctx_.CreateGlobal(stub_name, ir::IRType::I64(true), false, source_var);
  } else if (link.kind == LinkDecl::LinkKind::kStruct) {
    // For struct links, create type conversion functions
    std::string convert_name = stub_name + "_convert";
    auto fn = ir_ctx_.CreateFunction(convert_name, ir::IRType::Pointer(ir::IRType::I8()),
                                     {{"src", ir::IRType::Pointer(ir::IRType::I8())}});
    auto saved_fn = current_function_;
    current_function_ = fn;
    builder_.SetCurrentFunction(fn);

    auto entry = builder_.CreateBlock("struct.convert.entry");
    builder_.SetInsertPoint(entry);

    // Field-by-field conversion based on MAP_TYPE entries in the LINK body
    for (size_t i = 0; i < link.param_mappings.size(); ++i) {
      // Each mapping represents a field conversion
      // Generate load-convert-store sequences for each field
      std::string field_name = "field" + std::to_string(i);
      auto field_load = builder_.MakeLoad("src", ir::IRType::I64(true), field_name);
      (void)field_load; // Field conversion would store into the destination struct
    }

    builder_.MakeReturn("src");
    current_function_ = saved_fn;
    builder_.SetCurrentFunction(saved_fn);
  }
}

// ============================================================================
// Marshal Code Generation
// ============================================================================

void PloyLowering::GenerateMarshalCode(const std::string &src_val, const ir::IRType &src_type,
                                       const ir::IRType &dst_type, const std::string &dst_name) {
  // Determine marshalling strategy based on source and destination types
  if (src_type.kind == dst_type.kind) {
    // Same type kind �?direct copy (assign instruction or move)
    auto assign = std::make_shared<ir::AssignInstruction>();
    assign->name = dst_name;
    assign->type = dst_type;
    assign->operands.push_back(src_val);
    auto bb = builder_.GetInsertPoint();
    if (bb)
      bb->AddInstruction(assign);
    return;
  }

  // Integer to float conversion
  if (src_type.IsInteger() && dst_type.IsFloat()) {
    builder_.MakeCast(ir::CastInstruction::CastKind::kBitcast, src_val, dst_type, dst_name);
    return;
  }

  // Float to integer conversion
  if (src_type.IsFloat() && dst_type.IsInteger()) {
    builder_.MakeCast(ir::CastInstruction::CastKind::kBitcast, src_val, dst_type, dst_name);
    return;
  }

  // Integer width conversion
  if (src_type.IsInteger() && dst_type.IsInteger()) {
    // Determine if widening or narrowing
    int src_bits = 64, dst_bits = 64;
    if (src_type.kind == ir::IRTypeKind::kI8)
      src_bits = 8;
    else if (src_type.kind == ir::IRTypeKind::kI16)
      src_bits = 16;
    else if (src_type.kind == ir::IRTypeKind::kI32)
      src_bits = 32;
    if (dst_type.kind == ir::IRTypeKind::kI8)
      dst_bits = 8;
    else if (dst_type.kind == ir::IRTypeKind::kI16)
      dst_bits = 16;
    else if (dst_type.kind == ir::IRTypeKind::kI32)
      dst_bits = 32;

    if (dst_bits > src_bits) {
      auto cast_kind = src_type.is_signed ? ir::CastInstruction::CastKind::kSExt
                                          : ir::CastInstruction::CastKind::kZExt;
      builder_.MakeCast(cast_kind, src_val, dst_type, dst_name);
    } else if (dst_bits < src_bits) {
      builder_.MakeCast(ir::CastInstruction::CastKind::kTrunc, src_val, dst_type, dst_name);
    }
    return;
  }

  // Pointer-to-pointer: direct bitcast
  if (src_type.kind == ir::IRTypeKind::kPointer && dst_type.kind == ir::IRTypeKind::kPointer) {
    builder_.MakeCast(ir::CastInstruction::CastKind::kBitcast, src_val, dst_type, dst_name);
    return;
  }

  // Default: attempt direct use (the linker will validate)
  auto assign = std::make_shared<ir::AssignInstruction>();
  assign->name = dst_name;
  assign->type = dst_type;
  assign->operands.push_back(src_val);
  auto bb = builder_.GetInsertPoint();
  if (bb)
    bb->AddInstruction(assign);
}

// ============================================================================
// Type Conversion Helpers
// ============================================================================

ir::IRType PloyLowering::PloyTypeToIR(const std::shared_ptr<TypeNode> &type_node) {
  if (!type_node)
    return ir::IRType::I64(true);

  if (auto st = std::dynamic_pointer_cast<SimpleType>(type_node)) {
    // Support both upper-case Ploy keywords and lower-case C-style aliases
    if (st->name == "INT" || st->name == "i32" || st->name == "i64" || st->name == "int" ||
        st->name == "int32" || st->name == "int64")
      return ir::IRType::I64(true);
    if (st->name == "FLOAT" || st->name == "f32" || st->name == "f64" || st->name == "float" ||
        st->name == "float32" || st->name == "float64" || st->name == "double")
      return ir::IRType::F64();
    if (st->name == "BOOL" || st->name == "bool")
      return ir::IRType::I1();
    if (st->name == "STRING" || st->name == "string" || st->name == "str")
      return ir::IRType::Pointer(ir::IRType::I8());
    if (st->name == "VOID" || st->name == "void")
      return ir::IRType::Void();
    if (st->name == "u8" || st->name == "byte")
      return ir::IRType::I8();
    if (st->name == "u32" || st->name == "u64")
      return ir::IRType::I64(false);
    // Check if it is a known struct name in the environment
    auto it = env_.find(st->name);
    if (it != env_.end() && it->second.type.kind == ir::IRTypeKind::kStruct) {
      return it->second.type;
    }
    // Unknown type name �?log a diagnostic and fall back to I64
    diagnostics_.ReportWarning(core::SourceLoc{}, frontends::ErrorCode::kGenericWarning,
                               "unknown type '" + st->name +
                                   "' in PloyTypeToIR; falling back to i64");
    return ir::IRType::I64(true);
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
  }

  if (auto qt = std::dynamic_pointer_cast<QualifiedType>(type_node)) {
    core::Type ct = core::TypeSystem().MapFromLanguage(qt->language, qt->type_name);
    return CoreTypeToIR(ct);
  }

  diagnostics_.ReportWarning(core::SourceLoc{}, frontends::ErrorCode::kGenericWarning,
                             "unrecognized type node in PloyTypeToIR; falling back to i64");
  return ir::IRType::I64(true);
}

ir::IRType PloyLowering::CoreTypeToIR(const core::Type &ct) {
  switch (ct.kind) {
  case core::TypeKind::kInt:
    return ir::IRType::I64(true);
  case core::TypeKind::kFloat:
    return ir::IRType::F64();
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
    // Any type maps to opaque pointer (i8*) �?more accurate than I64
    return ir::IRType::Pointer(ir::IRType::I8());
  case core::TypeKind::kUnknown:
    // Unknown type reached lowering without being resolved �?this is a
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
  // DELETE(language, object) �?generate a destructor / cleanup call
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
    delete_func = "__ploy_delete_" + lang;
  }

  // Emit the call to the language-specific cleanup function
  auto call_inst = builder_.MakeCall(delete_func, {obj.value}, ir::IRType::Void());

  // Record the cross-language call descriptor for the linker
  CrossLangCallDescriptor desc;
  desc.stub_name = delete_func;
  desc.source_language = lang;
  desc.target_language = "ploy";
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
  desc.target_language = "ploy";
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

} // namespace polyglot::ploy

/** @} */