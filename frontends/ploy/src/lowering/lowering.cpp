#include "frontends/ploy/include/ploy_lowering.h"

#include <cstdlib>

#include "common/include/core/types.h"

namespace polyglot::ploy {
namespace {

// Mangle a cross-language stub name: __ploy_bridge_<target_lang>_<source_lang>_<symbol>
std::string MangleStubName(const std::string &target_lang, const std::string &source_lang,
                           const std::string &symbol) {
    std::string mangled = "__ploy_bridge_" + target_lang + "_" + source_lang + "_";
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

bool PloyLowering::Lower(const std::shared_ptr<Module> &module) {
    for (const auto &decl : module->declarations) {
        LowerStatement(decl);
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
    if (!stmt) return;

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
        if (c == ':' || c == '/' || c == '\\' || c == '.') c = '_';
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
            if (i > 0) sel_data += ",";
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
            // The function is already created — mark it for export
            // We record this via a global symbol alias
            std::string ext_name = export_decl->external_name.empty()
                                       ? export_decl->symbol_name
                                       : export_decl->external_name;
            if (ext_name != fn->name) {
                ir_ctx_.CreateGlobal("__ploy_export_alias_" + ext_name,
                                     ir::IRType::Pointer(ir::IRType::Void()), false,
                                     fn->name);
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
    // Save outer context — nested functions (e.g. inside PIPELINE) must not
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
    ir::IRType var_type = var->type ? PloyTypeToIR(var->type) : ir::IRType::I64(true);

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

void PloyLowering::LowerIfStatement(const std::shared_ptr<IfStatement> &if_stmt) {
    EvalResult cond = LowerExpression(if_stmt->condition);

    auto then_bb = builder_.CreateBlock("if.then");
    auto else_bb = builder_.CreateBlock("if.else");
    auto merge_bb = builder_.CreateBlock("if.merge");

    builder_.MakeCondBranch(cond.value, then_bb.get(), else_bb.get());

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
    builder_.MakeCondBranch(cond.value, body_bb.get(), exit_bb.get());

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
    auto cmp = builder_.MakeBinary(ir::BinaryInstruction::Op::kCmpSlt,
                                   idx_load->name, iter.value, "for.cond.cmp");
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
        auto inc = builder_.MakeBinary(ir::BinaryInstruction::Op::kAdd,
                                       idx_reload->name, "1", "idx.inc");
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

    // Build switch cases
    std::vector<ir::SwitchStatement::Case> ir_cases;
    ir::BasicBlock *default_bb = merge_bb.get();

    std::vector<std::shared_ptr<ir::BasicBlock>> case_blocks;
    for (size_t i = 0; i < match_stmt->cases.size(); ++i) {
        auto case_bb = builder_.CreateBlock("match.case." + std::to_string(i));
        case_blocks.push_back(case_bb);

        if (!match_stmt->cases[i].pattern) {
            // DEFAULT case
            default_bb = case_bb.get();
        } else {
            // Evaluate pattern as a literal for switch
            EvalResult pat = LowerExpression(match_stmt->cases[i].pattern);
            char *end = nullptr;
            long long case_val = std::strtoll(pat.value.c_str(), &end, 0);
            ir::SwitchStatement::Case sc;
            sc.value = case_val;
            sc.target = case_bb.get();
            ir_cases.push_back(sc);
        }
    }

    builder_.MakeSwitch(match_val.value, ir_cases, default_bb);

    // Lower each case body
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
    // If every case terminated (e.g., RETURN / BREAK), merge is unreachable
    terminated_ = all_terminated;
    if (terminated_) {
        builder_.MakeUnreachable();
    }
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

void PloyLowering::LowerBlockStatements(const std::vector<std::shared_ptr<Statement>> &stmts) {
    for (const auto &stmt : stmts) {
        if (terminated_) break;
        LowerStatement(stmt);
    }
}

// ============================================================================
// Expression Lowering
// ============================================================================

PloyLowering::EvalResult PloyLowering::LowerExpression(const std::shared_ptr<Expression> &expr) {
    if (!expr) return {"", ir::IRType::Invalid()};

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
        // Qualified identifiers are treated as external references
        std::string sym = qid->qualifier + "_" + qid->name;
        for (char &c : sym) {
            if (c == ':') c = '_';
        }
        return {sym, ir::IRType::I64(true)};
    }
    if (auto range = std::dynamic_pointer_cast<RangeExpression>(expr)) {
        // Range expression — lower the end value as the iteration bound
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

    // Determine operation
    ir::BinaryInstruction::Op op;
    ir::IRType result_type = left.type;
    bool is_float = (left.type.kind == ir::IRTypeKind::kF32 ||
                     left.type.kind == ir::IRTypeKind::kF64 ||
                     right.type.kind == ir::IRTypeKind::kF32 ||
                     right.type.kind == ir::IRTypeKind::kF64);

    if (bin->op == "+") {
        op = is_float ? ir::BinaryInstruction::Op::kFAdd : ir::BinaryInstruction::Op::kAdd;
        if (is_float) result_type = ir::IRType::F64();
    } else if (bin->op == "-") {
        op = is_float ? ir::BinaryInstruction::Op::kFSub : ir::BinaryInstruction::Op::kSub;
        if (is_float) result_type = ir::IRType::F64();
    } else if (bin->op == "*") {
        op = is_float ? ir::BinaryInstruction::Op::kFMul : ir::BinaryInstruction::Op::kMul;
        if (is_float) result_type = ir::IRType::F64();
    } else if (bin->op == "/") {
        op = is_float ? ir::BinaryInstruction::Op::kFDiv : ir::BinaryInstruction::Op::kSDiv;
        if (is_float) result_type = ir::IRType::F64();
    } else if (bin->op == "%") {
        op = is_float ? ir::BinaryInstruction::Op::kFRem : ir::BinaryInstruction::Op::kSRem;
        if (is_float) result_type = ir::IRType::F64();
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

    auto inst = builder_.MakeBinary(op, left.value, right.value, "");
    inst->type = result_type;
    return {inst->name, result_type};
}

PloyLowering::EvalResult PloyLowering::LowerUnaryExpression(
    const std::shared_ptr<UnaryExpression> &unary) {
    EvalResult operand = LowerExpression(unary->operand);

    if (unary->op == "-") {
        // Negate: 0 - operand
        bool is_float = (operand.type.kind == ir::IRTypeKind::kF32 ||
                         operand.type.kind == ir::IRTypeKind::kF64);
        auto op = is_float ? ir::BinaryInstruction::Op::kFSub : ir::BinaryInstruction::Op::kSub;
        auto inst = builder_.MakeBinary(op, "0", operand.value, "neg");
        inst->type = operand.type;
        return {inst->name, operand.type};
    }

    if (unary->op == "!") {
        // Logical not: xor with 1
        auto inst = builder_.MakeBinary(ir::BinaryInstruction::Op::kXor,
                                        operand.value, "1", "not");
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
            if (c == ':') c = '_';
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
    if (sig_it != sema_.KnownSignatures().end() &&
        !sig_it->second.param_names.empty()) {
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
                while (pos < reordered.size() && placed[pos]) ++pos;
                if (pos < reordered.size()) {
                    reordered[pos] = arg_names[i];
                    ++pos;
                }
            }
        }
        arg_names = reordered;
    }

    auto inst = builder_.MakeCall(callee_name, arg_names, ir::IRType::I64(true), "");
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

    // Generate the stub name for the cross-language call
    std::string stub_name = MangleStubName("ploy", call->language, call->function);

    // Record the cross-language call descriptor
    CrossLangCallDescriptor desc;
    desc.stub_name = stub_name;
    desc.source_language = call->language;
    desc.target_language = "ploy";
    desc.source_function = call->function;
    desc.target_function = stub_name;
    desc.source_param_types = arg_types;
    desc.source_return_type = ir::IRType::I64(true); // Default; refined by sema
    desc.target_return_type = ir::IRType::I64(true);

    // Generate marshalling descriptors for each argument
    for (const auto &at : arg_types) {
        CrossLangCallDescriptor::MarshalOp marshal;
        marshal.kind = CrossLangCallDescriptor::MarshalOp::Kind::kDirect;
        marshal.from = at;
        marshal.to = at; // Same type by default; overridden by MAP_TYPE
        desc.param_marshal.push_back(marshal);
    }
    desc.return_marshal.kind = CrossLangCallDescriptor::MarshalOp::Kind::kDirect;
    desc.return_marshal.from = ir::IRType::I64(true);
    desc.return_marshal.to = ir::IRType::I64(true);

    call_descriptors_.push_back(desc);

    // Emit the call instruction to the stub
    auto inst = builder_.MakeCall(stub_name, arg_names, ir::IRType::I64(true), "");
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
    std::string stub_name = MangleStubName("ploy", new_expr->language,
                                           new_expr->class_name + "::__init__");

    // Record the cross-language call descriptor for the constructor
    CrossLangCallDescriptor desc;
    desc.stub_name = stub_name;
    desc.source_language = new_expr->language;
    desc.target_language = "ploy";
    desc.source_function = new_expr->class_name + "::__init__";
    desc.target_function = stub_name;
    desc.source_param_types = arg_types;
    // Constructor returns an opaque object handle (pointer-sized)
    desc.source_return_type = ir::IRType::Pointer(ir::IRType::Void());
    desc.target_return_type = ir::IRType::Pointer(ir::IRType::Void());

    // Generate marshalling descriptors for each argument
    for (const auto &at : arg_types) {
        CrossLangCallDescriptor::MarshalOp marshal;
        marshal.kind = CrossLangCallDescriptor::MarshalOp::Kind::kDirect;
        marshal.from = at;
        marshal.to = at;
        desc.param_marshal.push_back(marshal);
    }
    desc.return_marshal.kind = CrossLangCallDescriptor::MarshalOp::Kind::kDirect;
    desc.return_marshal.from = ir::IRType::Pointer(ir::IRType::Void());
    desc.return_marshal.to = ir::IRType::Pointer(ir::IRType::Void());

    call_descriptors_.push_back(desc);

    // Emit the call instruction to the constructor stub
    auto inst = builder_.MakeCall(stub_name, arg_names,
                                  ir::IRType::Pointer(ir::IRType::Void()), "");
    return {inst->name, inst->type};
}

PloyLowering::EvalResult PloyLowering::LowerMethodCallExpression(
    const std::shared_ptr<MethodCallExpression> &method_call) {
    // Lower the receiver object
    EvalResult obj = LowerExpression(method_call->object);

    // Lower method arguments — the object is passed as the first argument
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
    std::string stub_name = MangleStubName("ploy", method_call->language,
                                           method_call->method_name);

    // Record the cross-language call descriptor for the method
    CrossLangCallDescriptor desc;
    desc.stub_name = stub_name;
    desc.source_language = method_call->language;
    desc.target_language = "ploy";
    desc.source_function = method_call->method_name;
    desc.target_function = stub_name;
    desc.source_param_types = arg_types;
    desc.source_return_type = ir::IRType::I64(true);
    desc.target_return_type = ir::IRType::I64(true);

    // Generate marshalling descriptors for each argument (including object)
    for (const auto &at : arg_types) {
        CrossLangCallDescriptor::MarshalOp marshal;
        marshal.kind = CrossLangCallDescriptor::MarshalOp::Kind::kDirect;
        marshal.from = at;
        marshal.to = at;
        desc.param_marshal.push_back(marshal);
    }
    desc.return_marshal.kind = CrossLangCallDescriptor::MarshalOp::Kind::kDirect;
    desc.return_marshal.from = ir::IRType::I64(true);
    desc.return_marshal.to = ir::IRType::I64(true);

    call_descriptors_.push_back(desc);

    // Emit the call instruction to the method stub
    auto inst = builder_.MakeCall(stub_name, arg_names, ir::IRType::I64(true), "");
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
    std::string stub_name = MangleStubName("ploy", get_attr->language,
                                           "__getattr__" + get_attr->attr_name);

    // Record the cross-language call descriptor
    CrossLangCallDescriptor desc;
    desc.stub_name = stub_name;
    desc.source_language = get_attr->language;
    desc.target_language = "ploy";
    desc.source_function = "__getattr__::" + get_attr->attr_name;
    desc.target_function = stub_name;
    desc.source_param_types = arg_types;
    desc.source_return_type = ir::IRType::I64(true);
    desc.target_return_type = ir::IRType::I64(true);

    // Generate marshalling descriptors
    for (const auto &at : arg_types) {
        CrossLangCallDescriptor::MarshalOp marshal;
        marshal.kind = CrossLangCallDescriptor::MarshalOp::Kind::kDirect;
        marshal.from = at;
        marshal.to = at;
        desc.param_marshal.push_back(marshal);
    }
    desc.return_marshal.kind = CrossLangCallDescriptor::MarshalOp::Kind::kDirect;
    desc.return_marshal.from = ir::IRType::I64(true);
    desc.return_marshal.to = ir::IRType::I64(true);

    call_descriptors_.push_back(desc);

    // Emit the call
    auto inst = builder_.MakeCall(stub_name, arg_names, ir::IRType::I64(true), "");
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
    arg_names.push_back(val.value);
    arg_types.push_back(val.type);

    // Generate the stub name for the setattr call
    std::string stub_name = MangleStubName("ploy", set_attr->language,
                                           "__setattr__" + set_attr->attr_name);

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
    for (const auto &at : arg_types) {
        CrossLangCallDescriptor::MarshalOp marshal;
        marshal.kind = CrossLangCallDescriptor::MarshalOp::Kind::kDirect;
        marshal.from = at;
        marshal.to = at;
        desc.param_marshal.push_back(marshal);
    }
    desc.return_marshal.kind = CrossLangCallDescriptor::MarshalOp::Kind::kDirect;
    desc.return_marshal.from = ir::IRType::Void();
    desc.return_marshal.to = ir::IRType::Void();

    call_descriptors_.push_back(desc);

    // Emit the call — returns void but we return the value for expression chaining
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
    std::string enter_stub = MangleStubName("ploy", with_stmt->language, "__enter__");
    std::vector<std::string> enter_args = {resource.value};
    auto enter_result = builder_.MakeCall(enter_stub, enter_args,
                                          ir::IRType::Pointer(ir::IRType::Void()), "");

    // Record __enter__ call descriptor
    CrossLangCallDescriptor enter_desc;
    enter_desc.stub_name = enter_stub;
    enter_desc.source_language = with_stmt->language;
    enter_desc.target_language = "ploy";
    enter_desc.source_function = "__enter__";
    enter_desc.target_function = enter_stub;
    enter_desc.source_param_types = {resource.type};
    enter_desc.source_return_type = ir::IRType::Pointer(ir::IRType::Void());
    enter_desc.target_return_type = ir::IRType::Pointer(ir::IRType::Void());
    CrossLangCallDescriptor::MarshalOp enter_marshal;
    enter_marshal.kind = CrossLangCallDescriptor::MarshalOp::Kind::kDirect;
    enter_marshal.from = resource.type;
    enter_marshal.to = resource.type;
    enter_desc.param_marshal.push_back(enter_marshal);
    enter_desc.return_marshal.kind = CrossLangCallDescriptor::MarshalOp::Kind::kDirect;
    enter_desc.return_marshal.from = ir::IRType::Pointer(ir::IRType::Void());
    enter_desc.return_marshal.to = ir::IRType::Pointer(ir::IRType::Void());
    call_descriptors_.push_back(enter_desc);

    // Step 3: Bind the result to the variable name
    env_[with_stmt->var_name] = EnvEntry{enter_result->name, enter_result->type};

    // Step 4: Execute body
    LowerBlockStatements(with_stmt->body);

    // Step 5: Call __exit__ on the resource
    std::string exit_stub = MangleStubName("ploy", with_stmt->language, "__exit__");
    std::vector<std::string> exit_args = {resource.value};
    builder_.MakeCall(exit_stub, exit_args, ir::IRType::Void(), "");

    // Record __exit__ call descriptor
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

    ir::IRType ret_type = map_func->return_type ? PloyTypeToIR(map_func->return_type)
                                                : ir::IRType::Void();

    std::vector<std::pair<std::string, ir::IRType>> params;
    for (const auto &p : map_func->params) {
        ir::IRType pt = p.type ? PloyTypeToIR(p.type) : ir::IRType::I64(true);
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
    ir::IRType target_type = conv->target_type ? PloyTypeToIR(conv->target_type)
                                               : ir::IRType::I64(true);

    // If source and target types match, pass through directly
    if (src.type.kind == target_type.kind) {
        return src;
    }

    // Generate conversion code based on types
    std::string dst_name = src.value + ".converted";
    GenerateMarshalCode(src.value, src.type, target_type, dst_name);
    return {dst_name, target_type};
}

PloyLowering::EvalResult PloyLowering::LowerListLiteral(
    const std::shared_ptr<ListLiteral> &list) {
    // Create a runtime list via __ploy_rt_list_create
    ir::IRType ptr_type = ir::IRType::Pointer(ir::IRType::I8());

    // Determine element size (default to 8 bytes for i64)
    std::string elem_size_val = "8";

    // Call __ploy_rt_list_create(elem_size, initial_capacity)
    std::string capacity_val = std::to_string(list->elements.size());
    auto create_call = builder_.MakeCall("__ploy_rt_list_create",
                                          {elem_size_val, capacity_val},
                                          ptr_type, "list.ptr");

    // Push each element
    for (const auto &elem : list->elements) {
        EvalResult e = LowerExpression(elem);
        // Allocate space for the element and store it
        auto alloca_inst = builder_.MakeAlloca(e.type, e.value + ".addr");
        builder_.MakeStore(alloca_inst->name, e.value);
        builder_.MakeCall("__ploy_rt_list_push",
                          {create_call->name, alloca_inst->name},
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
        auto gep = builder_.MakeGEP(alloca_inst->name, tuple_type,
                                     {i}, field_ptr);
        builder_.MakeStore(gep->name, elem_values[i]);
    }

    return {alloca_inst->name, tuple_type};
}

PloyLowering::EvalResult PloyLowering::LowerDictLiteral(
    const std::shared_ptr<DictLiteral> &dict) {
    // Create a runtime dict via __ploy_rt_dict_create
    ir::IRType ptr_type = ir::IRType::Pointer(ir::IRType::I8());

    // Default key and value sizes (8 bytes each for i64)
    std::string key_size_val = "8";
    std::string value_size_val = "8";

    auto create_call = builder_.MakeCall("__ploy_rt_dict_create",
                                          {key_size_val, value_size_val},
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
                          {create_call->name, key_alloca->name, val_alloca->name},
                          ir::IRType::Void(), "");
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
        auto gep = builder_.MakeGEP(alloca_inst->name, struct_type,
                                     {i}, field_ptr);
        builder_.MakeStore(gep->name, field_val.value);
    }

    return {alloca_inst->name, struct_type};
}

// ============================================================================
// Link Stub Generation
// ============================================================================

void PloyLowering::GenerateLinkStub(const LinkEntry &link) {
    std::string stub_name = MangleStubName(link.target_language, link.source_language,
                                           link.target_symbol);

    // Create the bridge function
    // The stub marshals arguments from target calling convention to source,
    // calls the source function, and marshals the return value back.

    // Resolve the return type from the known function signature registered
    // during semantic analysis.  If no signature is known, fall back to i64.
    ir::IRType ret_type = ir::IRType::I64(true);
    // Look up the function signature from the sema public accessor.
    const FunctionSignature *sig = nullptr;
    {
        auto it = sema_.KnownSignatures().find(link.target_symbol);
        if (it != sema_.KnownSignatures().end()) {
            sig = &it->second;
        }
    }
    if (sig && sig->return_type.kind != core::TypeKind::kAny &&
        sig->return_type.kind != core::TypeKind::kInvalid) {
        ret_type = CoreTypeToIR(sig->return_type);
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
                    core::Type ct = core::TypeSystem().MapFromLanguage(
                        link.param_mappings[i].target_language.empty()
                            ? link.target_language
                            : link.param_mappings[i].target_language,
                        link.param_mappings[i].target_type);
                    pt = CoreTypeToIR(ct);
                }
                params.emplace_back("arg" + std::to_string(i), pt);
            }
        } else {
            // No signature information at all — generate a single i64 parameter
            // as a last-resort fallback.
            params.emplace_back("arg0", ir::IRType::I64(true));
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
            if (c == ':') c = '_';
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
            if (c == ':') c = '_';
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
        // Same type kind — direct copy (assign instruction or move)
        auto assign = std::make_shared<ir::AssignInstruction>();
        assign->name = dst_name;
        assign->type = dst_type;
        assign->operands.push_back(src_val);
        auto bb = builder_.GetInsertPoint();
        if (bb) bb->AddInstruction(assign);
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
        if (src_type.kind == ir::IRTypeKind::kI8) src_bits = 8;
        else if (src_type.kind == ir::IRTypeKind::kI16) src_bits = 16;
        else if (src_type.kind == ir::IRTypeKind::kI32) src_bits = 32;
        if (dst_type.kind == ir::IRTypeKind::kI8) dst_bits = 8;
        else if (dst_type.kind == ir::IRTypeKind::kI16) dst_bits = 16;
        else if (dst_type.kind == ir::IRTypeKind::kI32) dst_bits = 32;

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
    if (bb) bb->AddInstruction(assign);
}

// ============================================================================
// Type Conversion Helpers
// ============================================================================

ir::IRType PloyLowering::PloyTypeToIR(const std::shared_ptr<TypeNode> &type_node) {
    if (!type_node) return ir::IRType::I64(true);

    if (auto st = std::dynamic_pointer_cast<SimpleType>(type_node)) {
        if (st->name == "INT") return ir::IRType::I64(true);
        if (st->name == "FLOAT") return ir::IRType::F64();
        if (st->name == "BOOL") return ir::IRType::I1();
        if (st->name == "STRING") return ir::IRType::Pointer(ir::IRType::I8());
        if (st->name == "VOID") return ir::IRType::Void();
        // Check if it is a known struct name in the environment
        auto it = env_.find(st->name);
        if (it != env_.end() && it->second.type.kind == ir::IRTypeKind::kStruct) {
            return it->second.type;
        }
        return ir::IRType::I64(true); // Default for unknown types
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

    return ir::IRType::I64(true);
}

ir::IRType PloyLowering::CoreTypeToIR(const core::Type &ct) {
    switch (ct.kind) {
        case core::TypeKind::kInt:    return ir::IRType::I64(true);
        case core::TypeKind::kFloat:  return ir::IRType::F64();
        case core::TypeKind::kBool:   return ir::IRType::I1();
        case core::TypeKind::kVoid:   return ir::IRType::Void();
        case core::TypeKind::kString: return ir::IRType::Pointer(ir::IRType::I8());
        case core::TypeKind::kPointer:
            if (!ct.type_args.empty()) return ir::IRType::Pointer(CoreTypeToIR(ct.type_args[0]));
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
            ir::IRType inner = ct.type_args.empty() ? ir::IRType::I64(true)
                                                     : CoreTypeToIR(ct.type_args[0]);
            return ir::IRType::Struct("option", {ir::IRType::I1(), inner});
        }
        case core::TypeKind::kStruct:
            // Named structs are opaque pointers unless resolved
            return ir::IRType::Pointer(ir::IRType::I8());
        case core::TypeKind::kGenericInstance:
            // Generic containers (e.g. dict) are opaque pointers
            return ir::IRType::Pointer(ir::IRType::I8());
        default:
            return ir::IRType::I64(true);
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
    // DELETE(language, object) — generate a destructor / cleanup call
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
            std::string bridge_name = "__ploy_extend_" + extend->derived_name +
                                      "_" + func->name;

            // Build IR parameter types
            std::vector<std::pair<std::string, ir::IRType>> params;
            // First param is always 'self' pointer for the object
            params.emplace_back("self_ptr", ir::IRType::Pointer(ir::IRType::I8()));
            for (const auto &p : func->params) {
                params.emplace_back(p.name, PloyTypeToIR(p.type));
            }

            ir::IRType ret_type = func->return_type
                                      ? PloyTypeToIR(func->return_type)
                                      : ir::IRType::Void();

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
    call_descriptors_.push_back(desc);
}

} // namespace polyglot::ploy
