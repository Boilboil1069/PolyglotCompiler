#include "frontends/ploy/include/ploy_sema.h"

#include <algorithm>
#include <unordered_set>

namespace polyglot::ploy {

// ============================================================================
// Public Interface
// ============================================================================

bool PloySema::Analyze(const std::shared_ptr<Module> &module) {
    for (const auto &decl : module->declarations) {
        AnalyzeStatement(decl);
    }
    return !diagnostics_.HasErrors();
}

// ============================================================================
// Statement Analysis
// ============================================================================

void PloySema::AnalyzeStatement(const std::shared_ptr<Statement> &stmt) {
    if (!stmt) return;

    if (auto link = std::dynamic_pointer_cast<LinkDecl>(stmt)) {
        AnalyzeLinkDecl(link);
    } else if (auto import_decl = std::dynamic_pointer_cast<ImportDecl>(stmt)) {
        AnalyzeImportDecl(import_decl);
    } else if (auto export_decl = std::dynamic_pointer_cast<ExportDecl>(stmt)) {
        AnalyzeExportDecl(export_decl);
    } else if (auto map_type = std::dynamic_pointer_cast<MapTypeDecl>(stmt)) {
        AnalyzeMapTypeDecl(map_type);
    } else if (auto pipeline = std::dynamic_pointer_cast<PipelineDecl>(stmt)) {
        AnalyzePipelineDecl(pipeline);
    } else if (auto func = std::dynamic_pointer_cast<FuncDecl>(stmt)) {
        AnalyzeFuncDecl(func);
    } else if (auto var = std::dynamic_pointer_cast<VarDecl>(stmt)) {
        AnalyzeVarDecl(var);
    } else if (auto if_stmt = std::dynamic_pointer_cast<IfStatement>(stmt)) {
        AnalyzeIfStatement(if_stmt);
    } else if (auto while_stmt = std::dynamic_pointer_cast<WhileStatement>(stmt)) {
        AnalyzeWhileStatement(while_stmt);
    } else if (auto for_stmt = std::dynamic_pointer_cast<ForStatement>(stmt)) {
        AnalyzeForStatement(for_stmt);
    } else if (auto match_stmt = std::dynamic_pointer_cast<MatchStatement>(stmt)) {
        AnalyzeMatchStatement(match_stmt);
    } else if (auto ret = std::dynamic_pointer_cast<ReturnStatement>(stmt)) {
        AnalyzeReturnStatement(ret);
    } else if (auto expr_stmt = std::dynamic_pointer_cast<ExprStatement>(stmt)) {
        if (expr_stmt->expr) {
            AnalyzeExpression(expr_stmt->expr);
        }
    } else if (auto brk = std::dynamic_pointer_cast<BreakStatement>(stmt)) {
        if (loop_depth_ <= 0) {
            Report(brk->loc, "BREAK statement outside of loop");
        }
    } else if (auto cont = std::dynamic_pointer_cast<ContinueStatement>(stmt)) {
        if (loop_depth_ <= 0) {
            Report(cont->loc, "CONTINUE statement outside of loop");
        }
    } else if (auto block = std::dynamic_pointer_cast<BlockStatement>(stmt)) {
        AnalyzeBlockStatements(block->statements);
    } else if (auto struct_decl = std::dynamic_pointer_cast<StructDecl>(stmt)) {
        AnalyzeStructDecl(struct_decl);
    } else if (auto map_func = std::dynamic_pointer_cast<MapFuncDecl>(stmt)) {
        AnalyzeMapFuncDecl(map_func);
    }
}

// ============================================================================
// LINK Declaration Analysis
// ============================================================================

void PloySema::AnalyzeLinkDecl(const std::shared_ptr<LinkDecl> &link) {
    // Validate languages
    if (!IsValidLanguage(link->target_language)) {
        Report(link->loc, "unknown target language '" + link->target_language + "'");
    }
    if (!IsValidLanguage(link->source_language)) {
        Report(link->loc, "unknown source language '" + link->source_language + "'");
    }

    // Validate that target and source are different
    if (link->target_language == link->source_language) {
        Report(link->loc, "LINK target and source languages must be different");
    }

    // Validate symbol names are not empty
    if (link->target_symbol.empty()) {
        Report(link->loc, "LINK target symbol is empty");
    }
    if (link->source_symbol.empty()) {
        Report(link->loc, "LINK source symbol is empty");
    }

    // Build link entry
    LinkEntry entry;
    entry.kind = link->link_kind;
    entry.target_language = link->target_language;
    entry.source_language = link->source_language;
    entry.target_symbol = link->target_symbol;
    entry.source_symbol = link->source_symbol;
    entry.defined_at = link->loc;

    // Process MAP_TYPE directives in body
    for (const auto &body_stmt : link->body) {
        if (auto map_type = std::dynamic_pointer_cast<MapTypeDecl>(body_stmt)) {
            TypeMappingEntry mapping;
            // Resolve source type
            if (auto qt = std::dynamic_pointer_cast<QualifiedType>(map_type->source_type)) {
                mapping.source_language = qt->language;
                mapping.source_type = qt->type_name;
            } else if (auto st = std::dynamic_pointer_cast<SimpleType>(map_type->source_type)) {
                mapping.source_type = st->name;
            }
            // Resolve target type
            if (auto qt = std::dynamic_pointer_cast<QualifiedType>(map_type->target_type)) {
                mapping.target_language = qt->language;
                mapping.target_type = qt->type_name;
            } else if (auto st = std::dynamic_pointer_cast<SimpleType>(map_type->target_type)) {
                mapping.target_type = st->name;
            }
            mapping.defined_at = map_type->loc;
            entry.param_mappings.push_back(mapping);
        }
    }

    links_.push_back(entry);
}

// ============================================================================
// IMPORT Declaration Analysis
// ============================================================================

void PloySema::AnalyzeImportDecl(const std::shared_ptr<ImportDecl> &import) {
    if (import->module_path.empty() && import->package_name.empty()) {
        Report(import->loc, "IMPORT module path is empty");
        return;
    }

    // Validate language if specified
    if (!import->language.empty() && !IsValidLanguage(import->language)) {
        Report(import->loc, "unknown language '" + import->language + "' in IMPORT");
    }

    // Determine the symbol name to register
    std::string sym_name;
    if (!import->alias.empty()) {
        sym_name = import->alias;
    } else if (!import->package_name.empty()) {
        // For IMPORT python PACKAGE numpy;  -> register as "numpy"
        // For IMPORT python PACKAGE numpy.linalg; -> register as "numpy.linalg"
        sym_name = import->package_name;
    } else {
        sym_name = import->module_path;
    }

    // Register the import as a symbol
    PloySymbol sym;
    sym.kind = PloySymbol::Kind::kImport;
    sym.name = sym_name;
    sym.language = import->language;
    sym.type = core::Type::Module(sym_name, import->language);
    sym.defined_at = import->loc;
    DeclareSymbol(sym);
}

// ============================================================================
// EXPORT Declaration Analysis
// ============================================================================

void PloySema::AnalyzeExportDecl(const std::shared_ptr<ExportDecl> &export_decl) {
    // Verify the symbol exists
    auto it = symbols_.find(export_decl->symbol_name);
    if (it == symbols_.end()) {
        Report(export_decl->loc,
               "EXPORT references undefined symbol '" + export_decl->symbol_name + "'");
        return;
    }

    // Mark the symbol as exported
    it->second.external_name =
        export_decl->external_name.empty() ? export_decl->symbol_name
                                           : export_decl->external_name;
}

// ============================================================================
// MAP_TYPE Declaration Analysis
// ============================================================================

void PloySema::AnalyzeMapTypeDecl(const std::shared_ptr<MapTypeDecl> &map_type) {
    TypeMappingEntry mapping;

    if (auto qt = std::dynamic_pointer_cast<QualifiedType>(map_type->source_type)) {
        mapping.source_language = qt->language;
        mapping.source_type = qt->type_name;
        if (!IsValidLanguage(qt->language)) {
            Report(map_type->loc, "unknown language '" + qt->language + "' in MAP_TYPE source");
        }
    } else if (auto st = std::dynamic_pointer_cast<SimpleType>(map_type->source_type)) {
        mapping.source_type = st->name;
    }

    if (auto qt = std::dynamic_pointer_cast<QualifiedType>(map_type->target_type)) {
        mapping.target_language = qt->language;
        mapping.target_type = qt->type_name;
        if (!IsValidLanguage(qt->language)) {
            Report(map_type->loc, "unknown language '" + qt->language + "' in MAP_TYPE target");
        }
    } else if (auto st = std::dynamic_pointer_cast<SimpleType>(map_type->target_type)) {
        mapping.target_type = st->name;
    }

    mapping.defined_at = map_type->loc;
    type_mappings_.push_back(mapping);
}

// ============================================================================
// PIPELINE Declaration Analysis
// ============================================================================

void PloySema::AnalyzePipelineDecl(const std::shared_ptr<PipelineDecl> &pipeline) {
    if (pipeline->name.empty()) {
        Report(pipeline->loc, "PIPELINE name is empty");
    }

    PloySymbol sym;
    sym.kind = PloySymbol::Kind::kPipeline;
    sym.name = pipeline->name;
    sym.type = core::Type::Void();
    sym.defined_at = pipeline->loc;
    DeclareSymbol(sym);

    AnalyzeBlockStatements(pipeline->body);
}

// ============================================================================
// FUNC Declaration Analysis
// ============================================================================

void PloySema::AnalyzeFuncDecl(const std::shared_ptr<FuncDecl> &func) {
    // Resolve return type
    core::Type ret_type = func->return_type ? ResolveType(func->return_type) : core::Type::Void();

    // Build function type with parameter types
    std::vector<core::Type> param_types;
    for (const auto &param : func->params) {
        core::Type pt = param.type ? ResolveType(param.type) : core::Type::Any();
        param_types.push_back(pt);
    }

    // Register function symbol
    PloySymbol sym;
    sym.kind = PloySymbol::Kind::kFunction;
    sym.name = func->name;
    sym.type = type_system_.FunctionType(func->name, ret_type, param_types);
    sym.defined_at = func->loc;
    DeclareSymbol(sym);

    // Register parameters as local symbols for body analysis
    auto saved_symbols = symbols_;
    for (size_t i = 0; i < func->params.size(); ++i) {
        PloySymbol param_sym;
        param_sym.kind = PloySymbol::Kind::kVariable;
        param_sym.name = func->params[i].name;
        param_sym.type = param_types[i];
        param_sym.is_mutable = false;
        param_sym.defined_at = func->loc;
        DeclareSymbol(param_sym);
    }

    // Analyze body
    auto saved_return = current_return_type_;
    current_return_type_ = ret_type;
    AnalyzeBlockStatements(func->body);
    current_return_type_ = saved_return;

    // Restore symbol scope (simple scope management)
    // Keep function-level symbols but remove parameters
    for (const auto &param : func->params) {
        symbols_.erase(param.name);
    }
    // Re-add any symbols that were in the outer scope
    for (const auto &[name, s] : saved_symbols) {
        if (symbols_.find(name) == symbols_.end()) {
            symbols_[name] = s;
        }
    }
}

// ============================================================================
// Variable Declaration Analysis
// ============================================================================

void PloySema::AnalyzeVarDecl(const std::shared_ptr<VarDecl> &var) {
    core::Type var_type = core::Type::Any();

    // Resolve explicit type if given
    if (var->type) {
        var_type = ResolveType(var->type);
    }

    // Check initializer
    if (var->init) {
        core::Type init_type = AnalyzeExpression(var->init);

        if (var->type) {
            // Check type compatibility
            if (!AreTypesCompatible(init_type, var_type)) {
                Report(var->loc,
                       "type mismatch in variable '" + var->name + "' declaration");
            }
        } else {
            // Infer type from initializer
            var_type = init_type;
        }
    } else if (!var->type) {
        Report(var->loc, "variable '" + var->name + "' requires a type annotation or initializer");
    }

    PloySymbol sym;
    sym.kind = PloySymbol::Kind::kVariable;
    sym.name = var->name;
    sym.type = var_type;
    sym.is_mutable = var->is_mutable;
    sym.defined_at = var->loc;
    DeclareSymbol(sym);
}

// ============================================================================
// Control Flow Analysis
// ============================================================================

void PloySema::AnalyzeIfStatement(const std::shared_ptr<IfStatement> &if_stmt) {
    core::Type cond_type = AnalyzeExpression(if_stmt->condition);
    if (cond_type.kind != core::TypeKind::kBool && cond_type.kind != core::TypeKind::kAny) {
        Report(if_stmt->loc, "IF condition must be a boolean expression");
    }
    AnalyzeBlockStatements(if_stmt->then_body);
    AnalyzeBlockStatements(if_stmt->else_body);
}

void PloySema::AnalyzeWhileStatement(const std::shared_ptr<WhileStatement> &while_stmt) {
    core::Type cond_type = AnalyzeExpression(while_stmt->condition);
    if (cond_type.kind != core::TypeKind::kBool && cond_type.kind != core::TypeKind::kAny) {
        Report(while_stmt->loc, "WHILE condition must be a boolean expression");
    }
    loop_depth_++;
    AnalyzeBlockStatements(while_stmt->body);
    loop_depth_--;
}

void PloySema::AnalyzeForStatement(const std::shared_ptr<ForStatement> &for_stmt) {
    core::Type iter_type = AnalyzeExpression(for_stmt->iterable);

    // Register iterator variable
    PloySymbol sym;
    sym.kind = PloySymbol::Kind::kVariable;
    sym.name = for_stmt->iterator_name;
    sym.is_mutable = false;
    sym.defined_at = for_stmt->loc;

    // Infer element type from iterable
    if (iter_type.kind == core::TypeKind::kArray && !iter_type.type_args.empty()) {
        sym.type = iter_type.type_args[0];
    } else {
        sym.type = core::Type::Any();
    }
    DeclareSymbol(sym);

    loop_depth_++;
    AnalyzeBlockStatements(for_stmt->body);
    loop_depth_--;

    // Remove iterator from scope
    symbols_.erase(for_stmt->iterator_name);
}

void PloySema::AnalyzeMatchStatement(const std::shared_ptr<MatchStatement> &match_stmt) {
    AnalyzeExpression(match_stmt->value);

    bool has_default = false;
    for (const auto &match_case : match_stmt->cases) {
        if (match_case.pattern) {
            AnalyzeExpression(match_case.pattern);
        } else {
            if (has_default) {
                Report(match_stmt->loc, "MATCH has multiple DEFAULT cases");
            }
            has_default = true;
        }
        AnalyzeBlockStatements(match_case.body);
    }
}

void PloySema::AnalyzeReturnStatement(const std::shared_ptr<ReturnStatement> &ret) {
    if (ret->value) {
        core::Type ret_type = AnalyzeExpression(ret->value);
        if (current_return_type_.kind != core::TypeKind::kInvalid &&
            current_return_type_.kind != core::TypeKind::kAny) {
            if (!AreTypesCompatible(ret_type, current_return_type_)) {
                Report(ret->loc, "return type mismatch");
            }
        }
    }
}

void PloySema::AnalyzeBlockStatements(const std::vector<std::shared_ptr<Statement>> &stmts) {
    for (const auto &stmt : stmts) {
        AnalyzeStatement(stmt);
    }
}

// ============================================================================
// Expression Analysis
// ============================================================================

core::Type PloySema::AnalyzeExpression(const std::shared_ptr<Expression> &expr) {
    if (!expr) return core::Type::Invalid();

    if (auto id = std::dynamic_pointer_cast<Identifier>(expr)) {
        auto it = symbols_.find(id->name);
        if (it != symbols_.end()) {
            return it->second.type;
        }
        Report(id->loc, "undefined identifier '" + id->name + "'");
        return core::Type::Any();
    }

    if (auto qid = std::dynamic_pointer_cast<QualifiedIdentifier>(expr)) {
        // Qualified identifiers refer to imported module symbols
        // For now, return Any since we cannot fully resolve cross-module types
        return core::Type::Any();
    }

    if (auto lit = std::dynamic_pointer_cast<Literal>(expr)) {
        switch (lit->kind) {
            case Literal::Kind::kInteger: return core::Type::Int();
            case Literal::Kind::kFloat:   return core::Type::Float();
            case Literal::Kind::kString:  return core::Type::String();
            case Literal::Kind::kBool:    return core::Type::Bool();
            case Literal::Kind::kNull:    return core::Type::Any();
        }
        return core::Type::Any();
    }

    if (auto bin = std::dynamic_pointer_cast<BinaryExpression>(expr)) {
        return AnalyzeBinaryExpression(bin);
    }

    if (auto unary = std::dynamic_pointer_cast<UnaryExpression>(expr)) {
        return AnalyzeUnaryExpression(unary);
    }

    if (auto call = std::dynamic_pointer_cast<CallExpression>(expr)) {
        return AnalyzeCallExpression(call);
    }

    if (auto cross_call = std::dynamic_pointer_cast<CrossLangCallExpression>(expr)) {
        return AnalyzeCrossLangCall(cross_call);
    }

    if (auto member = std::dynamic_pointer_cast<MemberExpression>(expr)) {
        AnalyzeExpression(member->object);
        return core::Type::Any(); // Member type resolution requires full module info
    }

    if (auto index = std::dynamic_pointer_cast<IndexExpression>(expr)) {
        core::Type obj_type = AnalyzeExpression(index->object);
        AnalyzeExpression(index->index);
        if (obj_type.kind == core::TypeKind::kArray && !obj_type.type_args.empty()) {
            return obj_type.type_args[0];
        }
        return core::Type::Any();
    }

    if (auto range = std::dynamic_pointer_cast<RangeExpression>(expr)) {
        AnalyzeExpression(range->start);
        AnalyzeExpression(range->end);
        return core::Type::Array(core::Type::Int());
    }

    if (auto conv = std::dynamic_pointer_cast<ConvertExpression>(expr)) {
        return AnalyzeConvertExpression(conv);
    }

    if (auto list_lit = std::dynamic_pointer_cast<ListLiteral>(expr)) {
        return AnalyzeListLiteral(list_lit);
    }

    if (auto tuple_lit = std::dynamic_pointer_cast<TupleLiteral>(expr)) {
        return AnalyzeTupleLiteral(tuple_lit);
    }

    if (auto dict_lit = std::dynamic_pointer_cast<DictLiteral>(expr)) {
        return AnalyzeDictLiteral(dict_lit);
    }

    if (auto struct_lit = std::dynamic_pointer_cast<StructLiteral>(expr)) {
        return AnalyzeStructLiteral(struct_lit);
    }

    return core::Type::Any();
}

core::Type PloySema::AnalyzeCallExpression(const std::shared_ptr<CallExpression> &call) {
    // Analyze the callee
    core::Type callee_type = AnalyzeExpression(call->callee);

    // Analyze arguments
    for (const auto &arg : call->args) {
        AnalyzeExpression(arg);
    }

    // If the callee is a function type, return its return type
    if (callee_type.kind == core::TypeKind::kFunction && !callee_type.type_args.empty()) {
        return callee_type.type_args[0]; // First type_arg is return type
    }

    return core::Type::Any();
}

core::Type PloySema::AnalyzeCrossLangCall(const std::shared_ptr<CrossLangCallExpression> &call) {
    // Validate language
    if (!IsValidLanguage(call->language)) {
        Report(call->loc, "unknown language '" + call->language + "' in CALL");
    }

    // Analyze arguments
    for (const auto &arg : call->args) {
        AnalyzeExpression(arg);
    }

    // Cross-language calls return Any since we cannot statically resolve the return type
    // without full module information from the target language
    return core::Type::Any();
}

core::Type PloySema::AnalyzeBinaryExpression(const std::shared_ptr<BinaryExpression> &bin) {
    core::Type left = AnalyzeExpression(bin->left);
    core::Type right = AnalyzeExpression(bin->right);

    // Assignment
    if (bin->op == "=") {
        // Check that left-hand side is assignable
        if (auto id = std::dynamic_pointer_cast<Identifier>(bin->left)) {
            auto it = symbols_.find(id->name);
            if (it != symbols_.end() && !it->second.is_mutable) {
                Report(bin->loc, "cannot assign to immutable variable '" + id->name + "'");
            }
        }
        return left;
    }

    // Comparison operators return bool
    if (bin->op == "==" || bin->op == "!=" || bin->op == "<" ||
        bin->op == ">" || bin->op == "<=" || bin->op == ">=") {
        return core::Type::Bool();
    }

    // Logical operators return bool
    if (bin->op == "&&" || bin->op == "||") {
        return core::Type::Bool();
    }

    // Arithmetic operators
    if (bin->op == "+" || bin->op == "-" || bin->op == "*" ||
        bin->op == "/" || bin->op == "%") {
        // Numeric promotion
        if (left.kind == core::TypeKind::kFloat || right.kind == core::TypeKind::kFloat) {
            return core::Type::Float();
        }
        if (left.kind == core::TypeKind::kInt && right.kind == core::TypeKind::kInt) {
            return core::Type::Int();
        }
        // String concatenation
        if (bin->op == "+" && (left.kind == core::TypeKind::kString ||
                               right.kind == core::TypeKind::kString)) {
            return core::Type::String();
        }
        return core::Type::Any();
    }

    return core::Type::Any();
}

core::Type PloySema::AnalyzeUnaryExpression(const std::shared_ptr<UnaryExpression> &unary) {
    core::Type operand_type = AnalyzeExpression(unary->operand);

    if (unary->op == "!") {
        return core::Type::Bool();
    }
    if (unary->op == "-") {
        return operand_type;
    }

    return operand_type;
}

// ============================================================================
// Type Resolution
// ============================================================================

core::Type PloySema::ResolveType(const std::shared_ptr<TypeNode> &type_node) {
    if (!type_node) return core::Type::Invalid();

    if (auto st = std::dynamic_pointer_cast<SimpleType>(type_node)) {
        if (st->name == "INT" || st->name == "i32" || st->name == "i64" || st->name == "int") return core::Type::Int();
        if (st->name == "FLOAT" || st->name == "f32" || st->name == "f64" || st->name == "float") return core::Type::Float();
        if (st->name == "BOOL" || st->name == "bool") return core::Type::Bool();
        if (st->name == "STRING" || st->name == "str" || st->name == "string") return core::Type::String();
        if (st->name == "VOID" || st->name == "void") return core::Type::Void();
        if (st->name == "ptr" || st->name == "PTR" || st->name == "pointer") return core::Type::Any(); // opaque pointer
        // User-defined type
        return core::Type::Struct(st->name);
    }

    if (auto pt = std::dynamic_pointer_cast<ParameterizedType>(type_node)) {
        if (pt->name == "ARRAY" && !pt->type_args.empty()) {
            core::Type elem = ResolveType(pt->type_args[0]);
            return core::Type::Array(elem);
        }
        if (pt->name == "LIST" && !pt->type_args.empty()) {
            core::Type elem = ResolveType(pt->type_args[0]);
            return core::Type::Array(elem); // LIST is a dynamic array
        }
        if (pt->name == "TUPLE" && !pt->type_args.empty()) {
            std::vector<core::Type> elems;
            for (const auto &arg : pt->type_args) {
                elems.push_back(ResolveType(arg));
            }
            return core::Type::Tuple(elems);
        }
        if (pt->name == "DICT" && pt->type_args.size() >= 2) {
            core::Type key_type = ResolveType(pt->type_args[0]);
            core::Type value_type = ResolveType(pt->type_args[1]);
            return core::Type::GenericInstance("dict", {key_type, value_type});
        }
        if (pt->name == "OPTION" && !pt->type_args.empty()) {
            core::Type inner = ResolveType(pt->type_args[0]);
            return core::Type::Optional(inner);
        }
        return core::Type::GenericInstance(pt->name, {});
    }

    if (auto qt = std::dynamic_pointer_cast<QualifiedType>(type_node)) {
        // Map from the specified language
        return type_system_.MapFromLanguage(qt->language, qt->type_name);
    }

    if (auto ft = std::dynamic_pointer_cast<FunctionType>(type_node)) {
        core::Type ret = ft->return_type ? ResolveType(ft->return_type) : core::Type::Void();
        std::vector<core::Type> params;
        for (const auto &p : ft->param_types) {
            params.push_back(ResolveType(p));
        }
        return type_system_.FunctionType("", ret, params);
    }

    return core::Type::Invalid();
}

bool PloySema::IsValidLanguage(const std::string &lang) const {
    return lang == "cpp" || lang == "python" || lang == "rust" ||
           lang == "c" || lang == "ploy";
}

bool PloySema::AreTypesCompatible(const core::Type &from, const core::Type &to) const {
    if (from.kind == core::TypeKind::kAny || to.kind == core::TypeKind::kAny) {
        return true;
    }
    return type_system_.IsCompatible(from, to);
}

// ============================================================================
// STRUCT Declaration Analysis
// ============================================================================

void PloySema::AnalyzeStructDecl(const std::shared_ptr<StructDecl> &struct_decl) {
    if (struct_decl->name.empty()) {
        Report(struct_decl->loc, "struct name cannot be empty");
        return;
    }

    // Check for duplicate struct definition
    if (struct_defs_.count(struct_decl->name)) {
        Report(struct_decl->loc, "redefinition of struct '" + struct_decl->name + "'");
        return;
    }

    // Validate fields
    std::vector<std::pair<std::string, core::Type>> resolved_fields;
    std::unordered_set<std::string> field_names;
    for (const auto &field : struct_decl->fields) {
        if (field.name.empty()) {
            Report(struct_decl->loc, "field name cannot be empty in struct '" + struct_decl->name + "'");
            continue;
        }
        if (!field_names.insert(field.name).second) {
            Report(struct_decl->loc, "duplicate field '" + field.name +
                                     "' in struct '" + struct_decl->name + "'");
            continue;
        }
        core::Type field_type = field.type ? ResolveType(field.type) : core::Type::Any();
        resolved_fields.emplace_back(field.name, field_type);
    }

    struct_defs_[struct_decl->name] = resolved_fields;

    // Register the struct as a type symbol
    PloySymbol sym;
    sym.kind = PloySymbol::Kind::kVariable; // Struct types act as type symbols
    sym.name = struct_decl->name;
    sym.type = core::Type::Struct(struct_decl->name);
    sym.defined_at = struct_decl->loc;
    DeclareSymbol(sym);
}

// ============================================================================
// MAP_FUNC Declaration Analysis
// ============================================================================

void PloySema::AnalyzeMapFuncDecl(const std::shared_ptr<MapFuncDecl> &map_func) {
    if (map_func->name.empty()) {
        Report(map_func->loc, "MAP_FUNC name cannot be empty");
        return;
    }

    // Resolve return type
    core::Type ret_type = map_func->return_type ? ResolveType(map_func->return_type) : core::Type::Any();

    // Declare function symbol
    PloySymbol sym;
    sym.kind = PloySymbol::Kind::kFunction;
    sym.name = map_func->name;
    sym.type = ret_type;
    sym.defined_at = map_func->loc;
    DeclareSymbol(sym);

    // Register as mapping function
    map_funcs_[map_func->name] = ret_type;

    // Analyze parameters
    auto saved_return_type = current_return_type_;
    current_return_type_ = ret_type;

    for (const auto &param : map_func->params) {
        core::Type pt = param.type ? ResolveType(param.type) : core::Type::Any();
        PloySymbol param_sym;
        param_sym.kind = PloySymbol::Kind::kVariable;
        param_sym.name = param.name;
        param_sym.type = pt;
        param_sym.defined_at = map_func->loc;
        DeclareSymbol(param_sym);
    }

    // Analyze body
    AnalyzeBlockStatements(map_func->body);
    current_return_type_ = saved_return_type;
}

// ============================================================================
// Complex Expression Analysis
// ============================================================================

core::Type PloySema::AnalyzeConvertExpression(const std::shared_ptr<ConvertExpression> &conv) {
    // Analyze the source expression
    core::Type src_type = AnalyzeExpression(conv->expr);
    (void)src_type;

    // Resolve the target type
    core::Type target = conv->target_type ? ResolveType(conv->target_type) : core::Type::Any();
    return target;
}

core::Type PloySema::AnalyzeListLiteral(const std::shared_ptr<ListLiteral> &list) {
    core::Type elem_type = core::Type::Any();
    for (const auto &elem : list->elements) {
        core::Type t = AnalyzeExpression(elem);
        if (elem_type.kind == core::TypeKind::kAny) {
            elem_type = t;
        }
        // All elements should have compatible types (allow coercion)
    }
    return core::Type::Array(elem_type);
}

core::Type PloySema::AnalyzeTupleLiteral(const std::shared_ptr<TupleLiteral> &tuple) {
    std::vector<core::Type> elem_types;
    for (const auto &elem : tuple->elements) {
        elem_types.push_back(AnalyzeExpression(elem));
    }
    return core::Type::Tuple(elem_types);
}

core::Type PloySema::AnalyzeDictLiteral(const std::shared_ptr<DictLiteral> &dict) {
    core::Type key_type = core::Type::Any();
    core::Type value_type = core::Type::Any();
    for (const auto &entry : dict->entries) {
        core::Type kt = AnalyzeExpression(entry.key);
        core::Type vt = AnalyzeExpression(entry.value);
        if (key_type.kind == core::TypeKind::kAny) key_type = kt;
        if (value_type.kind == core::TypeKind::kAny) value_type = vt;
    }
    return core::Type::GenericInstance("dict", {key_type, value_type});
}

core::Type PloySema::AnalyzeStructLiteral(const std::shared_ptr<StructLiteral> &struct_lit) {
    // Verify the struct type exists
    auto it = struct_defs_.find(struct_lit->struct_name);
    if (it == struct_defs_.end()) {
        Report(struct_lit->loc, "unknown struct type '" + struct_lit->struct_name + "'");
        return core::Type::Any();
    }

    const auto &defined_fields = it->second;

    // Verify all provided fields exist in the struct definition
    for (const auto &field : struct_lit->fields) {
        bool found = false;
        for (const auto &[def_name, def_type] : defined_fields) {
            if (def_name == field.name) {
                found = true;
                core::Type init_type = AnalyzeExpression(field.value);
                if (!AreTypesCompatible(init_type, def_type)) {
                    Report(struct_lit->loc, "field '" + field.name +
                                            "' type mismatch in struct literal");
                }
                break;
            }
        }
        if (!found) {
            Report(struct_lit->loc, "unknown field '" + field.name +
                                    "' in struct '" + struct_lit->struct_name + "'");
        }
    }

    return core::Type::Struct(struct_lit->struct_name);
}

// ============================================================================
// Helpers
// ============================================================================

void PloySema::Report(const core::SourceLoc &loc, const std::string &message) {
    diagnostics_.Report(loc, message);
}

bool PloySema::DeclareSymbol(const PloySymbol &symbol) {
    auto [it, inserted] = symbols_.try_emplace(symbol.name, symbol);
    if (!inserted) {
        Report(symbol.defined_at,
               "redefinition of symbol '" + symbol.name + "'");
        return false;
    }
    return true;
}

} // namespace polyglot::ploy
