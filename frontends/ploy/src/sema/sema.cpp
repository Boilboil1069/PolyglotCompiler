#include "frontends/ploy/include/ploy_sema.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>
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
    } else if (auto venv_config = std::dynamic_pointer_cast<VenvConfigDecl>(stmt)) {
        AnalyzeVenvConfigDecl(venv_config);
    } else if (auto with_stmt = std::dynamic_pointer_cast<WithStatement>(stmt)) {
        AnalyzeWithStatement(with_stmt);
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

    // Validate version constraint if present
    if (!import->version_op.empty()) {
        if (!IsValidVersionOp(import->version_op)) {
            Report(import->loc,
                   "invalid version operator '" + import->version_op + "' in IMPORT");
        }
        if (import->version_constraint.empty()) {
            Report(import->loc, "version operator without version number in IMPORT");
        } else if (!IsValidVersionString(import->version_constraint)) {
            Report(import->loc,
                   "invalid version string '" + import->version_constraint + "' in IMPORT");
        }
        if (import->package_name.empty()) {
            Report(import->loc,
                   "version constraints are only valid for PACKAGE imports");
        }
    }

    // Validate selective imports
    if (!import->selected_symbols.empty()) {
        if (import->package_name.empty()) {
            Report(import->loc,
                   "selective imports (::) are only valid for PACKAGE imports");
        }
        // Selective imports and alias cannot be used together because
        // the alias target is ambiguous (does it refer to symbol A or B?)
        if (!import->alias.empty()) {
            Report(import->loc,
                   "selective import and AS alias cannot be combined — "
                   "alias '" + import->alias + "' is ambiguous when "
                   "multiple symbols are selected");
        }
        // Check for duplicate symbols in the selection list
        std::unordered_set<std::string> seen;
        for (const auto &sym : import->selected_symbols) {
            if (!seen.insert(sym).second) {
                Report(import->loc,
                       "duplicate symbol '" + sym + "' in selective import list");
            }
        }
    }

    // Run package auto-discovery for the specified language if not done yet
    if (!import->language.empty() && !import->package_name.empty()) {
        if (discovery_completed_.find(import->language) == discovery_completed_.end()) {
            // Find the venv path and manager kind for this language, if configured
            std::string venv_path;
            VenvConfigDecl::ManagerKind manager = VenvConfigDecl::ManagerKind::kVenv;
            for (const auto &vc : venv_configs_) {
                if (vc.language == import->language) {
                    venv_path = vc.venv_path;
                    manager = vc.manager;
                    break;
                }
            }
            DiscoverPackages(import->language, venv_path, manager);
            discovery_completed_.insert(import->language);
        }

        // If package discovery found this package, validate the version constraint
        std::string pkg_key = import->language + "::" + import->package_name;
        auto pkg_it = discovered_packages_.find(pkg_key);
        if (pkg_it != discovered_packages_.end()) {
            // Package found — check version constraint if specified
            if (!import->version_op.empty() && !import->version_constraint.empty()) {
                if (!CompareVersions(pkg_it->second.version,
                                     import->version_constraint,
                                     import->version_op)) {
                    Report(import->loc,
                           "installed package '" + import->package_name +
                           "' version " + pkg_it->second.version +
                           " does not satisfy constraint " +
                           import->version_op + " " + import->version_constraint);
                }
            }
        }
        // Note: if the package is not discovered, we do NOT emit an error,
        // as the package might be available at link-time or in a different
        // environment. The discovery is best-effort.
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

    // If selective imports are specified, register each selected symbol individually
    for (const auto &selected : import->selected_symbols) {
        PloySymbol sel_sym;
        sel_sym.kind = PloySymbol::Kind::kImport;
        sel_sym.name = selected;
        sel_sym.language = import->language;
        sel_sym.type = core::Type::Any();
        sel_sym.defined_at = import->loc;
        DeclareSymbol(sel_sym);
    }
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

    if (auto new_expr = std::dynamic_pointer_cast<NewExpression>(expr)) {
        return AnalyzeNewExpression(new_expr);
    }

    if (auto method_call = std::dynamic_pointer_cast<MethodCallExpression>(expr)) {
        return AnalyzeMethodCallExpression(method_call);
    }

    if (auto get_attr = std::dynamic_pointer_cast<GetAttrExpression>(expr)) {
        return AnalyzeGetAttrExpression(get_attr);
    }

    if (auto set_attr = std::dynamic_pointer_cast<SetAttrExpression>(expr)) {
        return AnalyzeSetAttrExpression(set_attr);
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

core::Type PloySema::AnalyzeNewExpression(const std::shared_ptr<NewExpression> &new_expr) {
    // Validate language
    if (!IsValidLanguage(new_expr->language)) {
        Report(new_expr->loc, "unknown language '" + new_expr->language + "' in NEW");
    }

    // Validate class name is non-empty
    if (new_expr->class_name.empty()) {
        Report(new_expr->loc, "class name is empty in NEW");
    }

    // Analyze constructor arguments
    for (const auto &arg : new_expr->args) {
        AnalyzeExpression(arg);
    }

    // NEW returns an opaque object handle — typed as Any since we cannot statically
    // resolve the foreign class type without full module information
    return core::Type::Any();
}

core::Type PloySema::AnalyzeMethodCallExpression(
    const std::shared_ptr<MethodCallExpression> &method_call) {
    // Validate language
    if (!IsValidLanguage(method_call->language)) {
        Report(method_call->loc, "unknown language '" + method_call->language + "' in METHOD");
    }

    // Validate method name is non-empty
    if (method_call->method_name.empty()) {
        Report(method_call->loc, "method name is empty in METHOD");
    }

    // Analyze the receiver object
    if (method_call->object) {
        AnalyzeExpression(method_call->object);
    } else {
        Report(method_call->loc, "METHOD requires an object expression");
    }

    // Analyze arguments
    for (const auto &arg : method_call->args) {
        AnalyzeExpression(arg);
    }

    // Method calls return Any since we cannot statically resolve the return type
    return core::Type::Any();
}

core::Type PloySema::AnalyzeGetAttrExpression(const std::shared_ptr<GetAttrExpression> &get_attr) {
    // Validate language
    if (!IsValidLanguage(get_attr->language)) {
        Report(get_attr->loc, "unknown language '" + get_attr->language + "' in GET");
    }

    // Validate attribute name is non-empty
    if (get_attr->attr_name.empty()) {
        Report(get_attr->loc, "attribute name is empty in GET");
    }

    // Analyze the object expression
    if (get_attr->object) {
        AnalyzeExpression(get_attr->object);
    } else {
        Report(get_attr->loc, "GET requires an object expression");
    }

    // Attribute access returns Any since we cannot statically resolve the attribute type
    return core::Type::Any();
}

core::Type PloySema::AnalyzeSetAttrExpression(const std::shared_ptr<SetAttrExpression> &set_attr) {
    // Validate language
    if (!IsValidLanguage(set_attr->language)) {
        Report(set_attr->loc, "unknown language '" + set_attr->language + "' in SET");
    }

    // Validate attribute name is non-empty
    if (set_attr->attr_name.empty()) {
        Report(set_attr->loc, "attribute name is empty in SET");
    }

    // Analyze the object expression
    if (set_attr->object) {
        AnalyzeExpression(set_attr->object);
    } else {
        Report(set_attr->loc, "SET requires an object expression");
    }

    // Analyze the value expression
    if (set_attr->value) {
        AnalyzeExpression(set_attr->value);
    } else {
        Report(set_attr->loc, "SET requires a value expression");
    }

    // SET returns the assigned value type (Any since we cannot know the attribute type)
    return core::Type::Any();
}

void PloySema::AnalyzeWithStatement(const std::shared_ptr<WithStatement> &with_stmt) {
    // Validate language
    if (!IsValidLanguage(with_stmt->language)) {
        Report(with_stmt->loc, "unknown language '" + with_stmt->language + "' in WITH");
    }

    // Validate variable name
    if (with_stmt->var_name.empty()) {
        Report(with_stmt->loc, "WITH requires a variable name after AS");
    }

    // Analyze the resource expression
    if (with_stmt->resource_expr) {
        AnalyzeExpression(with_stmt->resource_expr);
    } else {
        Report(with_stmt->loc, "WITH requires a resource expression");
    }

    // Declare the bound variable in scope
    PloySymbol sym;
    sym.kind = PloySymbol::Kind::kVariable;
    sym.name = with_stmt->var_name;
    sym.type = core::Type::Any();  // Resource object is opaque
    sym.is_mutable = false;
    sym.defined_at = with_stmt->loc;
    DeclareSymbol(sym);

    // Analyze the body
    AnalyzeBlockStatements(with_stmt->body);
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

// ============================================================================
// CONFIG VENV Analysis
// ============================================================================

void PloySema::AnalyzeVenvConfigDecl(const std::shared_ptr<VenvConfigDecl> &venv_config) {
    // Validate the language
    if (!IsValidLanguage(venv_config->language)) {
        Report(venv_config->loc,
               "unknown language '" + venv_config->language + "' in CONFIG");
        return;
    }

    // Check for duplicate venv config for the same language
    for (const auto &existing : venv_configs_) {
        if (existing.language == venv_config->language) {
            Report(venv_config->loc,
                   "duplicate CONFIG for language '" + venv_config->language +
                   "' (previously defined at " + existing.defined_at.file +
                   ":" + std::to_string(existing.defined_at.line) + ")");
            return;
        }
    }

    if (venv_config->venv_path.empty()) {
        Report(venv_config->loc, "CONFIG environment path is empty");
        return;
    }

    VenvConfig vc;
    vc.manager = venv_config->manager;
    vc.language = venv_config->language;
    vc.venv_path = venv_config->venv_path;
    vc.defined_at = venv_config->loc;
    venv_configs_.push_back(vc);
}

// ============================================================================
// Version Constraint Validation
// ============================================================================

bool PloySema::IsValidVersionOp(const std::string &op) const {
    return op == ">=" || op == "<=" || op == "==" ||
           op == ">"  || op == "<"  || op == "~=";
}

bool PloySema::IsValidVersionString(const std::string &version) const {
    if (version.empty()) return false;
    // Version strings consist of digits separated by dots: 1.20, 2.0.0, etc.
    // Also allow pre-release suffixes like 1.0.0-beta, 1.0.0rc1
    bool has_digit = false;
    for (size_t i = 0; i < version.size(); ++i) {
        char c = version[i];
        if (std::isdigit(static_cast<unsigned char>(c))) {
            has_digit = true;
        } else if (c == '.') {
            // Dot must be preceded and followed by a digit (or end of version)
            if (i == 0 || i == version.size() - 1) return false;
        } else if (std::isalpha(static_cast<unsigned char>(c)) || c == '-') {
            // Allow pre-release identifiers after a digit
            if (!has_digit) return false;
        } else {
            return false;
        }
    }
    return has_digit;
}

std::vector<int> PloySema::ParseVersionParts(const std::string &version) const {
    std::vector<int> parts;
    std::string current;
    for (char c : version) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            current += c;
        } else if (c == '.') {
            if (!current.empty()) {
                parts.push_back(std::stoi(current));
                current.clear();
            }
        } else {
            // Stop at non-numeric, non-dot characters (pre-release suffix)
            break;
        }
    }
    if (!current.empty()) {
        parts.push_back(std::stoi(current));
    }
    return parts;
}

bool PloySema::CompareVersions(const std::string &installed, const std::string &required,
                                const std::string &op) const {
    auto inst_parts = ParseVersionParts(installed);
    auto req_parts = ParseVersionParts(required);

    // Pad the shorter vector with zeros for comparison
    size_t max_len = std::max(inst_parts.size(), req_parts.size());
    while (inst_parts.size() < max_len) inst_parts.push_back(0);
    while (req_parts.size() < max_len) req_parts.push_back(0);

    // Lexicographic comparison
    int cmp = 0;
    for (size_t i = 0; i < max_len; ++i) {
        if (inst_parts[i] < req_parts[i]) { cmp = -1; break; }
        if (inst_parts[i] > req_parts[i]) { cmp = 1; break; }
    }

    if (op == ">=") return cmp >= 0;
    if (op == "<=") return cmp <= 0;
    if (op == "==") return cmp == 0;
    if (op == ">")  return cmp > 0;
    if (op == "<")  return cmp < 0;
    if (op == "~=") {
        // Compatible release: ~= 1.20 means >= 1.20, < 2.0
        //                     ~= 1.20.3 means >= 1.20.3, < 1.21.0
        if (cmp < 0) return false; // installed < required
        // Check upper bound: the second-to-last segment must match
        auto upper = req_parts;
        if (upper.size() >= 2) {
            upper[upper.size() - 2] += 1;
            upper[upper.size() - 1] = 0;
            for (size_t i = 0; i < upper.size(); ++i) {
                if (inst_parts[i] < upper[i]) return true;
                if (inst_parts[i] > upper[i]) return false;
            }
            return false; // exactly at upper bound, which is exclusive
        }
        return cmp >= 0;
    }

    return false;
}

// ============================================================================
// Package Auto-Discovery
// ============================================================================

void PloySema::DiscoverPackages(const std::string &language, const std::string &venv_path,
                                VenvConfigDecl::ManagerKind manager) {
    if (language == "python") {
        DiscoverPythonPackages(venv_path, manager);
    } else if (language == "rust") {
        DiscoverRustCrates();
    } else if (language == "cpp" || language == "c") {
        DiscoverCppPackages();
    }
    // Other languages can be added here
}

void PloySema::DiscoverPythonPackages(const std::string &venv_path,
                                      VenvConfigDecl::ManagerKind manager) {
    switch (manager) {
        case VenvConfigDecl::ManagerKind::kConda:
            DiscoverPythonPackagesViaConda(venv_path);
            return;
        case VenvConfigDecl::ManagerKind::kUv:
            DiscoverPythonPackagesViaUv(venv_path);
            return;
        case VenvConfigDecl::ManagerKind::kPipenv:
            DiscoverPythonPackagesViaPipenv(venv_path);
            return;
        case VenvConfigDecl::ManagerKind::kPoetry:
            DiscoverPythonPackagesViaPoetry(venv_path);
            return;
        case VenvConfigDecl::ManagerKind::kVenv:
        default:
            break;
    }

    // Default: standard venv/virtualenv via pip
    std::string python_cmd;
    if (!venv_path.empty()) {
#ifdef _WIN32
        python_cmd = "\"" + venv_path + "\\Scripts\\python.exe\" -m pip list --format=freeze";
#else
        python_cmd = "\"" + venv_path + "/bin/python\" -m pip list --format=freeze";
#endif
    } else {
        python_cmd = "python -m pip list --format=freeze";
    }

    DiscoverPythonPackagesViaPip(python_cmd);
}

void PloySema::DiscoverPythonPackagesViaPip(const std::string &pip_cmd) {
    // Execute the command and capture output
    std::string output;
#ifdef _WIN32
    FILE *pipe = _popen(pip_cmd.c_str(), "r");
#else
    FILE *pipe = popen(pip_cmd.c_str(), "r");
#endif
    if (!pipe) {
        // pip not available — silently skip discovery
        return;
    }

    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }

#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif

    // Parse pip freeze output: each line is "package==version"
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        // Skip empty lines and comment lines
        if (line.empty() || line[0] == '#') continue;

        // Format: package==version or package===version
        size_t eq_pos = line.find("==");
        if (eq_pos == std::string::npos) continue;

        std::string pkg_name = line.substr(0, eq_pos);
        std::string pkg_version = line.substr(eq_pos + 2);

        // Trim trailing whitespace/newlines
        while (!pkg_version.empty() &&
               (pkg_version.back() == '\n' || pkg_version.back() == '\r' ||
                pkg_version.back() == ' ')) {
            pkg_version.pop_back();
        }
        while (!pkg_name.empty() &&
               (pkg_name.back() == '\n' || pkg_name.back() == '\r' ||
                pkg_name.back() == ' ')) {
            pkg_name.pop_back();
        }

        // Normalize package name (pip uses hyphens, Python uses underscores)
        std::string normalized_name = pkg_name;
        for (char &c : normalized_name) {
            if (c == '-') c = '_';
        }

        PackageInfo info;
        info.name = normalized_name;
        info.version = pkg_version;
        info.language = "python";

        std::string key = "python::" + normalized_name;
        discovered_packages_[key] = info;

        // Also register with original name (case-insensitive matching)
        if (normalized_name != pkg_name) {
            std::string alt_key = "python::" + pkg_name;
            discovered_packages_[alt_key] = info;
        }
    }
}

void PloySema::DiscoverPythonPackagesViaConda(const std::string &env_name) {
    // Build the conda list command targeting the specified environment
    std::string conda_cmd;
    if (!env_name.empty()) {
        conda_cmd = "conda list -n " + env_name + " --export 2>nul";
    } else {
        conda_cmd = "conda list --export 2>nul";
    }

    std::string output;
#ifdef _WIN32
    FILE *pipe = _popen(conda_cmd.c_str(), "r");
#else
    // Redirect stderr on Unix
    std::string unix_cmd = env_name.empty()
        ? "conda list --export 2>/dev/null"
        : "conda list -n " + env_name + " --export 2>/dev/null";
    FILE *pipe = popen(unix_cmd.c_str(), "r");
#endif
    if (!pipe) return;

    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif

    // Parse conda list --export output: each line is "package=version=build_string"
    // Lines starting with '#' are comments
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == '#' || line[0] == '@') continue;

        // Format: package=version=build
        size_t first_eq = line.find('=');
        if (first_eq == std::string::npos) continue;

        std::string pkg_name = line.substr(0, first_eq);
        std::string rest = line.substr(first_eq + 1);

        size_t second_eq = rest.find('=');
        std::string pkg_version = (second_eq != std::string::npos)
                                      ? rest.substr(0, second_eq)
                                      : rest;

        // Trim whitespace
        while (!pkg_version.empty() &&
               (pkg_version.back() == '\n' || pkg_version.back() == '\r' ||
                pkg_version.back() == ' ')) {
            pkg_version.pop_back();
        }
        while (!pkg_name.empty() &&
               (pkg_name.back() == '\n' || pkg_name.back() == '\r' ||
                pkg_name.back() == ' ')) {
            pkg_name.pop_back();
        }

        // Normalize package name
        std::string normalized_name = pkg_name;
        for (char &c : normalized_name) {
            if (c == '-') c = '_';
        }

        PackageInfo info;
        info.name = normalized_name;
        info.version = pkg_version;
        info.language = "python";

        std::string key = "python::" + normalized_name;
        discovered_packages_[key] = info;

        if (normalized_name != pkg_name) {
            std::string alt_key = "python::" + pkg_name;
            discovered_packages_[alt_key] = info;
        }
    }
}

void PloySema::DiscoverPythonPackagesViaUv(const std::string &venv_path) {
    // uv manages venvs similarly to pip, but uses its own command
    // uv pip list --format=freeze (within a specific venv)
    std::string uv_cmd;
    if (!venv_path.empty()) {
#ifdef _WIN32
        // Use the python from the uv-managed venv
        uv_cmd = "\"" + venv_path + "\\Scripts\\python.exe\" -m pip list --format=freeze";
#else
        uv_cmd = "\"" + venv_path + "/bin/python\" -m pip list --format=freeze";
#endif
    } else {
        // Try uv pip list directly
        uv_cmd = "uv pip list --format=freeze 2>nul";
    }

    DiscoverPythonPackagesViaPip(uv_cmd);
}

void PloySema::DiscoverPythonPackagesViaPipenv(const std::string &project_path) {
    // pipenv run pip list --format=freeze
    // If project_path is specified, run from that directory
    std::string pipenv_cmd;
    if (!project_path.empty()) {
#ifdef _WIN32
        pipenv_cmd = "cmd /c \"cd /d " + project_path + " && pipenv run pip list --format=freeze 2>nul\"";
#else
        pipenv_cmd = "cd " + project_path + " && pipenv run pip list --format=freeze 2>/dev/null";
#endif
    } else {
        pipenv_cmd = "pipenv run pip list --format=freeze 2>nul";
    }

    DiscoverPythonPackagesViaPip(pipenv_cmd);
}

void PloySema::DiscoverPythonPackagesViaPoetry(const std::string &project_path) {
    // poetry run pip list --format=freeze
    // If project_path is specified, run from that directory
    std::string poetry_cmd;
    if (!project_path.empty()) {
#ifdef _WIN32
        poetry_cmd = "cmd /c \"cd /d " + project_path + " && poetry run pip list --format=freeze 2>nul\"";
#else
        poetry_cmd = "cd " + project_path + " && poetry run pip list --format=freeze 2>/dev/null";
#endif
    } else {
        poetry_cmd = "poetry run pip list --format=freeze 2>nul";
    }

    DiscoverPythonPackagesViaPip(poetry_cmd);
}

void PloySema::DiscoverRustCrates() {
    // Use cargo to list installed crates
    std::string output;
#ifdef _WIN32
    FILE *pipe = _popen("cargo install --list 2>nul", "r");
#else
    FILE *pipe = popen("cargo install --list 2>/dev/null", "r");
#endif
    if (!pipe) return;

    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif

    // Parse cargo install --list output: "crate_name v1.2.3:"
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == ' ') continue;

        // Lines look like: "crate_name v1.2.3:"
        size_t space_pos = line.find(' ');
        if (space_pos == std::string::npos) continue;

        std::string crate_name = line.substr(0, space_pos);
        std::string version_str;

        size_t v_pos = line.find('v', space_pos);
        size_t colon_pos = line.find(':', v_pos);
        if (v_pos != std::string::npos && colon_pos != std::string::npos) {
            version_str = line.substr(v_pos + 1, colon_pos - v_pos - 1);
        }

        PackageInfo info;
        info.name = crate_name;
        info.version = version_str;
        info.language = "rust";

        std::string key = "rust::" + crate_name;
        discovered_packages_[key] = info;
    }
}

void PloySema::DiscoverCppPackages() {
    // C++ package discovery via pkg-config
    std::string output;
#ifdef _WIN32
    FILE *pipe = _popen("pkg-config --list-all 2>nul", "r");
#else
    FILE *pipe = popen("pkg-config --list-all 2>/dev/null", "r");
#endif
    if (!pipe) return;

    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif

    // Parse pkg-config output: "package_name  description"
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;

        size_t space_pos = line.find(' ');
        std::string pkg_name = (space_pos != std::string::npos)
                                    ? line.substr(0, space_pos)
                                    : line;

        PackageInfo info;
        info.name = pkg_name;
        info.language = "cpp";

        std::string key = "cpp::" + pkg_name;
        discovered_packages_[key] = info;
    }
}

} // namespace polyglot::ploy
