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
    }
    // BREAK and CONTINUE are handled at a higher level (loop lowering)
    // MAP_TYPE is metadata only, no IR generation needed
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
    terminated_ = false;

    auto entry = builder_.CreateBlock("entry");
    builder_.SetInsertPoint(entry);

    LowerBlockStatements(pipeline->body);

    // Add implicit return if not terminated
    if (!terminated_) {
        builder_.MakeReturn();
    }

    current_function_ = nullptr;
}

// ============================================================================
// FUNC Lowering
// ============================================================================

void PloyLowering::LowerFuncDecl(const std::shared_ptr<FuncDecl> &func) {
    // Build parameter list
    std::vector<std::pair<std::string, ir::IRType>> params;
    for (const auto &p : func->params) {
        ir::IRType param_type = PloyTypeToIR(p.type);
        params.emplace_back(p.name, param_type);
    }

    ir::IRType ret_type = func->return_type ? PloyTypeToIR(func->return_type) : ir::IRType::Void();

    auto fn = ir_ctx_.CreateFunction(func->name, ret_type, params);
    current_function_ = fn;
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

    current_function_ = nullptr;
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
        env_[var->name] = EnvEntry{init_result.value, var_type};
    } else {
        // Allocate space for the variable
        auto alloca = builder_.MakeAlloca(var_type, var->name);
        env_[var->name] = EnvEntry{alloca->name, var_type};
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
    if (!terminated_) {
        builder_.MakeBranch(merge_bb.get());
    }

    // Else block
    builder_.SetInsertPoint(else_bb);
    terminated_ = false;
    if (!if_stmt->else_body.empty()) {
        LowerBlockStatements(if_stmt->else_body);
    }
    if (!terminated_) {
        builder_.MakeBranch(merge_bb.get());
    }

    // Continue in merge block
    builder_.SetInsertPoint(merge_bb);
    terminated_ = false;
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
    for (size_t i = 0; i < match_stmt->cases.size(); ++i) {
        builder_.SetInsertPoint(case_blocks[i]);
        terminated_ = false;
        LowerBlockStatements(match_stmt->cases[i].body);
        if (!terminated_) {
            builder_.MakeBranch(merge_bb.get());
        }
    }

    builder_.SetInsertPoint(merge_bb);
    terminated_ = false;
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

    return {"", ir::IRType::Invalid()};
}

PloyLowering::EvalResult PloyLowering::LowerIdentifier(const std::shared_ptr<Identifier> &id) {
    auto it = env_.find(id->name);
    if (it != env_.end()) {
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
            env_[id->name] = EnvEntry{rhs.value, rhs.type};
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
        return {inst->name, operand.type};
    }

    if (unary->op == "!") {
        // Logical not: xor with 1
        auto inst = builder_.MakeBinary(ir::BinaryInstruction::Op::kXor,
                                        operand.value, "1", "not");
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

    auto entry_block = builder_.CreateBlock("entry");
    builder_.SetInsertPoint(entry_block);

    // Register parameters in the environment
    for (const auto &[name, type] : params) {
        env_[name] = {name, type};
    }

    LowerBlockStatements(map_func->body);

    current_function_ = saved_fn;
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
        builder_.MakeStore(e.value, alloca_inst->name);
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
        builder_.MakeStore(key.value, key_alloca->name);
        auto val_alloca = builder_.MakeAlloca(val.type, "dict.val.addr");
        builder_.MakeStore(val.value, val_alloca->name);

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

    ir::IRType ret_type = ir::IRType::I64(true); // Default return type

    // For function links, create a wrapper function
    if (link.kind == LinkDecl::LinkKind::kFunction) {
        // Create a stub function that calls the source function
        // Parameters match the target function's signature
        std::vector<std::pair<std::string, ir::IRType>> params;
        params.emplace_back("arg0", ir::IRType::I64(true)); // Placeholder
        // In a full implementation, parameter types would come from the target function's
        // resolved signature. For now we use generic i64 parameters.

        auto fn = ir_ctx_.CreateFunction(stub_name, ret_type, params);
        auto saved_fn = current_function_;
        current_function_ = fn;

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
        ir_ctx_.AddStatement(assign);
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
    ir_ctx_.AddStatement(assign);
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

} // namespace polyglot::ploy
