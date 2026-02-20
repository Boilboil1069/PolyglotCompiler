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
            ReportError(brk->loc, frontends::ErrorCode::kBreakOutsideLoop,
                        "BREAK statement outside of loop");
        }
    } else if (auto cont = std::dynamic_pointer_cast<ContinueStatement>(stmt)) {
        if (loop_depth_ <= 0) {
            ReportError(cont->loc, frontends::ErrorCode::kContinueOutsideLoop,
                        "CONTINUE statement outside of loop");
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
    } else if (auto extend = std::dynamic_pointer_cast<ExtendDecl>(stmt)) {
        AnalyzeExtendDecl(extend);
    }
}

// ============================================================================
// LINK Declaration Analysis
// ============================================================================

void PloySema::AnalyzeLinkDecl(const std::shared_ptr<LinkDecl> &link) {
    // Validate languages
    if (!IsValidLanguage(link->target_language)) {
        ReportError(link->loc, frontends::ErrorCode::kInvalidLanguage,
                    "unknown target language '" + link->target_language + "'");
    }
    if (!IsValidLanguage(link->source_language)) {
        ReportError(link->loc, frontends::ErrorCode::kInvalidLanguage,
                    "unknown source language '" + link->source_language + "'");
    }

    // Validate that target and source are different
    if (link->target_language == link->source_language) {
        ReportError(link->loc, frontends::ErrorCode::kTypeMismatch,
                    "LINK target and source languages must be different");
    }

    // Validate symbol names are not empty
    if (link->target_symbol.empty()) {
        ReportError(link->loc, frontends::ErrorCode::kEmptySymbolName,
                    "LINK target symbol is empty");
    }
    if (link->source_symbol.empty()) {
        ReportError(link->loc, frontends::ErrorCode::kEmptySymbolName,
                    "LINK source symbol is empty");
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

    // Register the linked function as a known symbol so CALL can reference it
    PloySymbol link_sym;
    link_sym.kind = PloySymbol::Kind::kLinkTarget;
    link_sym.name = link->target_symbol;
    link_sym.language = link->target_language;
    link_sym.type = core::Type::Any();
    link_sym.defined_at = link->loc;
    // Do not report redefinition for link targets — they may overlap with imports
    symbols_.try_emplace(link->target_symbol, link_sym);

    // Register type mappings from the LINK body as a signature hint.
    // MAP_TYPE entries in LINK describe cross-language type conversions,
    // NOT the parameter count of the linked function. So we record the
    // type information but leave param_count_known = false to allow
    // flexible calls — the actual parameter count comes from the external
    // function definition, which is not available at .ploy compile time.
    if (!entry.param_mappings.empty()) {
        FunctionSignature sig;
        sig.name = link->target_symbol;
        sig.language = link->target_language;
        sig.param_count = 0;
        sig.param_count_known = false;
        sig.defined_at = link->loc;
        for (const auto &mapping : entry.param_mappings) {
            // Resolve the target type from the mapping
            core::Type param_type = type_system_.MapFromLanguage(
                mapping.target_language.empty() ? link->target_language : mapping.target_language,
                mapping.target_type);
            sig.param_types.push_back(param_type);
        }
        RegisterFunctionSignature(link->target_symbol, sig);
    }
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

    // Register function symbol in the OUTER scope (before entering function body)
    PloySymbol sym;
    sym.kind = PloySymbol::Kind::kFunction;
    sym.name = func->name;
    sym.type = type_system_.FunctionType(func->name, ret_type, param_types);
    sym.defined_at = func->loc;
    DeclareSymbol(sym);

    // Register function signature for parameter validation
    FunctionSignature sig;
    sig.name = func->name;
    sig.language = "ploy";
    sig.param_types = param_types;
    sig.return_type = ret_type;
    sig.param_count = func->params.size();
    sig.param_count_known = true;
    sig.defined_at = func->loc;
    RegisterFunctionSignature(func->name, sig);

    // Save the entire symbol table before entering the function body scope.
    // Local variables declared inside the function body must NOT leak into
    // the outer (module) scope. After body analysis we restore the saved
    // snapshot so that each FUNC gets its own isolated scope.
    auto saved_symbols = symbols_;

    // Register parameters as local symbols for body analysis
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

    // Restore the symbol table to the state before entering the function body.
    // This ensures local variables (LET/VAR inside the body) do not leak into
    // the module scope, so the same variable name can be reused across
    // different FUNC declarations.
    symbols_ = saved_symbols;

    // Re-register the function symbol itself (it was declared in outer scope
    // but may have been overwritten by the restore if it wasn't in saved_symbols
    // before DeclareSymbol was called). Since we declared it before the save,
    // it is already in saved_symbols — no extra action needed.
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
                ReportError(var->loc, frontends::ErrorCode::kTypeMismatch,
                            "type mismatch in variable '" + var->name +
                            "' declaration: expected '" + var_type.ToString() +
                            "' but initializer has type '" + init_type.ToString() + "'",
                            "change the type annotation or the initializer expression");
            }
        } else {
            // Infer type from initializer
            var_type = init_type;
        }
    } else if (!var->type) {
        ReportError(var->loc, frontends::ErrorCode::kMissingTypeAnnotation,
                    "variable '" + var->name + "' requires a type annotation or initializer",
                    "add a type annotation: LET " + var->name + ": TYPE;");
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
                ReportError(ret->loc, frontends::ErrorCode::kReturnTypeMismatch,
                            "return type mismatch: function expects '" +
                            current_return_type_.ToString() + "' but returning '" +
                            ret_type.ToString() + "'");
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
        ReportError(id->loc, frontends::ErrorCode::kUndefinedSymbol,
                    "undefined identifier '" + id->name + "'");
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

    if (auto del_expr = std::dynamic_pointer_cast<DeleteExpression>(expr)) {
        return AnalyzeDeleteExpression(del_expr);
    }

    return core::Type::Any();
}

core::Type PloySema::AnalyzeCallExpression(const std::shared_ptr<CallExpression> &call) {
    // Analyze the callee
    core::Type callee_type = AnalyzeExpression(call->callee);

    // Analyze arguments and collect their types
    std::vector<core::Type> arg_types;
    for (const auto &arg : call->args) {
        arg_types.push_back(AnalyzeExpression(arg));
    }

    // Extract function name for signature lookup
    std::string func_name;
    if (auto id = std::dynamic_pointer_cast<Identifier>(call->callee)) {
        func_name = id->name;
    } else if (auto qid = std::dynamic_pointer_cast<QualifiedIdentifier>(call->callee)) {
        func_name = qid->qualifier + "::" + qid->name;
    }

    // Validate parameter count and types against known signatures
    if (!func_name.empty()) {
        const FunctionSignature *sig = LookupSignature(func_name);
        ValidateCallArgCount(call->loc, func_name, call->args.size(), sig);
        ValidateCallArgTypes(call->loc, func_name, arg_types, sig);
    }

    // If the callee is a function type, validate against function type signature
    if (callee_type.kind == core::TypeKind::kFunction && !callee_type.type_args.empty()) {
        // type_args layout: [return_type, param_type_0, param_type_1, ...]
        size_t expected_params = callee_type.type_args.size() - 1;
        if (call->args.size() != expected_params) {
            ReportError(call->loc, frontends::ErrorCode::kParamCountMismatch,
                        "function '" + func_name + "' expects " +
                        std::to_string(expected_params) + " argument(s), but " +
                        std::to_string(call->args.size()) + " argument(s) provided");
        }

        // Check each argument type
        for (size_t i = 0; i < std::min(call->args.size(), expected_params); ++i) {
            const core::Type &expected = callee_type.type_args[i + 1];
            if (!AreTypesCompatible(arg_types[i], expected)) {
                ReportError(call->loc, frontends::ErrorCode::kTypeMismatch,
                            "type mismatch for argument " + std::to_string(i + 1) +
                            " of function '" + func_name + "': expected '" +
                            expected.ToString() + "' but got '" +
                            arg_types[i].ToString() + "'");
            }
        }

        return callee_type.type_args[0]; // First type_arg is return type
    }

    return core::Type::Any();
}

core::Type PloySema::AnalyzeCrossLangCall(const std::shared_ptr<CrossLangCallExpression> &call) {
    // Validate language
    if (!IsValidLanguage(call->language)) {
        ReportError(call->loc, frontends::ErrorCode::kInvalidLanguage,
                    "unknown language '" + call->language + "' in CALL");
    }

    // Analyze arguments and collect their types
    std::vector<core::Type> arg_types;
    for (const auto &arg : call->args) {
        arg_types.push_back(AnalyzeExpression(arg));
    }

    // Validate parameter count and types against known signatures
    // Try lookup by full function name first, then by short name
    const FunctionSignature *sig = LookupSignature(call->function);
    if (!sig) {
        // Try stripping module qualifier: "module::func" -> "func"
        auto pos = call->function.rfind("::");
        if (pos != std::string::npos) {
            sig = LookupSignature(call->function.substr(pos + 2));
        }
    }
    ValidateCallArgCount(call->loc, call->function, call->args.size(), sig);
    ValidateCallArgTypes(call->loc, call->function, arg_types, sig);

    // If we have a known return type from the signature, use it
    if (sig && sig->return_type.kind != core::TypeKind::kAny &&
        sig->return_type.kind != core::TypeKind::kInvalid) {
        return sig->return_type;
    }

    // Cross-language calls return Any since we cannot statically resolve the return type
    // without full module information from the target language
    return core::Type::Any();
}

core::Type PloySema::AnalyzeNewExpression(const std::shared_ptr<NewExpression> &new_expr) {
    // Validate language
    if (!IsValidLanguage(new_expr->language)) {
        ReportError(new_expr->loc, frontends::ErrorCode::kInvalidLanguage,
                    "unknown language '" + new_expr->language + "' in NEW");
    }

    // Validate class name is non-empty
    if (new_expr->class_name.empty()) {
        ReportError(new_expr->loc, frontends::ErrorCode::kEmptySymbolName,
                    "class name is empty in NEW");
    }

    // Analyze constructor arguments and collect their types
    std::vector<core::Type> arg_types;
    for (const auto &arg : new_expr->args) {
        arg_types.push_back(AnalyzeExpression(arg));
    }

    // Check constructor signature if known (e.g. via LINK declaration)
    std::string ctor_name = new_expr->class_name + "::__init__";
    const FunctionSignature *sig = LookupSignature(ctor_name);
    if (!sig) {
        // Try shorter name without module qualifier
        auto pos = new_expr->class_name.rfind("::");
        if (pos != std::string::npos) {
            ctor_name = new_expr->class_name.substr(pos + 2) + "::__init__";
            sig = LookupSignature(ctor_name);
        }
    }
    if (sig) {
        ValidateCallArgCount(new_expr->loc, new_expr->class_name + " constructor",
                             new_expr->args.size(), sig);
        ValidateCallArgTypes(new_expr->loc, new_expr->class_name + " constructor",
                             arg_types, sig);
    }

    // NEW returns an opaque object handle — typed as Any since we cannot statically
    // resolve the foreign class type without full module information
    return core::Type::Any();
}

core::Type PloySema::AnalyzeMethodCallExpression(
    const std::shared_ptr<MethodCallExpression> &method_call) {
    // Validate language
    if (!IsValidLanguage(method_call->language)) {
        ReportError(method_call->loc, frontends::ErrorCode::kInvalidLanguage,
                    "unknown language '" + method_call->language + "' in METHOD");
    }

    // Validate method name is non-empty
    if (method_call->method_name.empty()) {
        ReportError(method_call->loc, frontends::ErrorCode::kEmptySymbolName,
                    "method name is empty in METHOD");
    }

    // Analyze the receiver object
    if (method_call->object) {
        AnalyzeExpression(method_call->object);
    } else {
        ReportError(method_call->loc, frontends::ErrorCode::kMissingExpression,
                    "METHOD requires an object expression");
    }

    // Analyze arguments and collect their types
    std::vector<core::Type> arg_types;
    for (const auto &arg : method_call->args) {
        arg_types.push_back(AnalyzeExpression(arg));
    }

    // Check method signature if known (e.g. via LINK declaration)
    const FunctionSignature *sig = LookupSignature(method_call->method_name);
    if (sig) {
        ValidateCallArgCount(method_call->loc, method_call->method_name,
                             method_call->args.size(), sig);
        ValidateCallArgTypes(method_call->loc, method_call->method_name,
                             arg_types, sig);
    }

    // If we have a known return type, use it
    if (sig && sig->return_type.kind != core::TypeKind::kAny &&
        sig->return_type.kind != core::TypeKind::kInvalid) {
        return sig->return_type;
    }

    // Method calls return Any since we cannot statically resolve the return type
    return core::Type::Any();
}

core::Type PloySema::AnalyzeGetAttrExpression(const std::shared_ptr<GetAttrExpression> &get_attr) {
    // Validate language
    if (!IsValidLanguage(get_attr->language)) {
        ReportError(get_attr->loc, frontends::ErrorCode::kInvalidLanguage,
                    "unknown language '" + get_attr->language + "' in GET");
    }

    // Validate attribute name is non-empty
    if (get_attr->attr_name.empty()) {
        ReportError(get_attr->loc, frontends::ErrorCode::kEmptySymbolName,
                    "attribute name is empty in GET");
    }

    // Analyze the object expression
    if (get_attr->object) {
        AnalyzeExpression(get_attr->object);
    } else {
        ReportError(get_attr->loc, frontends::ErrorCode::kMissingExpression,
                    "GET requires an object expression");
    }

    // Attribute access returns Any since we cannot statically resolve the attribute type
    return core::Type::Any();
}

core::Type PloySema::AnalyzeSetAttrExpression(const std::shared_ptr<SetAttrExpression> &set_attr) {
    // Validate language
    if (!IsValidLanguage(set_attr->language)) {
        ReportError(set_attr->loc, frontends::ErrorCode::kInvalidLanguage,
                    "unknown language '" + set_attr->language + "' in SET");
    }

    // Validate attribute name is non-empty
    if (set_attr->attr_name.empty()) {
        ReportError(set_attr->loc, frontends::ErrorCode::kEmptySymbolName,
                    "attribute name is empty in SET");
    }

    // Analyze the object expression
    if (set_attr->object) {
        AnalyzeExpression(set_attr->object);
    } else {
        ReportError(set_attr->loc, frontends::ErrorCode::kMissingExpression,
                    "SET requires an object expression");
    }

    // Analyze the value expression
    if (set_attr->value) {
        AnalyzeExpression(set_attr->value);
    } else {
        ReportError(set_attr->loc, frontends::ErrorCode::kMissingExpression,
                    "SET requires a value expression");
    }

    // SET returns the assigned value type (Any since we cannot know the attribute type)
    return core::Type::Any();
}

void PloySema::AnalyzeWithStatement(const std::shared_ptr<WithStatement> &with_stmt) {
    // Validate language
    if (!IsValidLanguage(with_stmt->language)) {
        ReportError(with_stmt->loc, frontends::ErrorCode::kInvalidLanguage,
                    "unknown language '" + with_stmt->language + "' in WITH");
    }

    // Validate variable name
    if (with_stmt->var_name.empty()) {
        ReportError(with_stmt->loc, frontends::ErrorCode::kEmptySymbolName,
                    "WITH requires a variable name after AS");
    }

    // Analyze the resource expression
    if (with_stmt->resource_expr) {
        AnalyzeExpression(with_stmt->resource_expr);
    } else {
        ReportError(with_stmt->loc, frontends::ErrorCode::kMissingExpression,
                    "WITH requires a resource expression");
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
                ReportError(bin->loc, frontends::ErrorCode::kImmutableAssignment,
                            "cannot assign to immutable variable '" + id->name + "'",
                            "declare '" + id->name + "' with VAR instead of LET to make it mutable");
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

void PloySema::ReportError(const core::SourceLoc &loc, frontends::ErrorCode code,
                           const std::string &message) {
    diagnostics_.ReportError(loc, code, message);
}

void PloySema::ReportError(const core::SourceLoc &loc, frontends::ErrorCode code,
                           const std::string &message, const std::string &suggestion) {
    diagnostics_.ReportError(loc, code, message, suggestion);
}

void PloySema::ReportErrorWithTraceback(const core::SourceLoc &loc, frontends::ErrorCode code,
                                        const std::string &message,
                                        const core::SourceLoc &related_loc,
                                        const std::string &related_msg) {
    frontends::Diagnostic related;
    related.loc = related_loc;
    related.message = related_msg;
    related.severity = frontends::DiagnosticSeverity::kNote;
    diagnostics_.ReportErrorWithTraceback(loc, code, message, {related});
}

void PloySema::ReportWarning(const core::SourceLoc &loc, frontends::ErrorCode code,
                             const std::string &message) {
    diagnostics_.ReportWarning(loc, code, message);
}

void PloySema::ReportWarning(const core::SourceLoc &loc, frontends::ErrorCode code,
                             const std::string &message, const std::string &suggestion) {
    diagnostics_.ReportWarning(loc, code, message, suggestion);
}

bool PloySema::DeclareSymbol(const PloySymbol &symbol) {
    auto [it, inserted] = symbols_.try_emplace(symbol.name, symbol);
    if (!inserted) {
        ReportError(symbol.defined_at, frontends::ErrorCode::kRedefinedSymbol,
                    "redefinition of symbol '" + symbol.name + "'");
        return false;
    }
    return true;
}

// ============================================================================
// Function Signature Registry and Validation
// ============================================================================

void PloySema::RegisterFunctionSignature(const std::string &qualified_name,
                                         const FunctionSignature &sig) {
    known_signatures_[qualified_name] = sig;
}

const FunctionSignature *PloySema::LookupSignature(const std::string &qualified_name) const {
    auto it = known_signatures_.find(qualified_name);
    if (it != known_signatures_.end()) {
        return &it->second;
    }
    return nullptr;
}

void PloySema::ValidateCallArgCount(const core::SourceLoc &call_loc,
                                    const std::string &func_name,
                                    size_t actual_args,
                                    const FunctionSignature *sig) {
    if (!sig || !sig->param_count_known) return;

    if (actual_args != sig->param_count) {
        std::string msg = "function '" + func_name + "' expects " +
                          std::to_string(sig->param_count) + " argument(s), but " +
                          std::to_string(actual_args) + " argument(s) provided";
        std::string suggestion = "check the function signature and provide the correct number of arguments";
        ReportErrorWithTraceback(call_loc, frontends::ErrorCode::kParamCountMismatch,
                                 msg, sig->defined_at,
                                 "'" + func_name + "' declared here with " +
                                 std::to_string(sig->param_count) + " parameter(s)");
    }
}

void PloySema::ValidateCallArgTypes(const core::SourceLoc &call_loc,
                                    const std::string &func_name,
                                    const std::vector<core::Type> &actual_types,
                                    const FunctionSignature *sig) {
    if (!sig || !sig->param_count_known) return;
    if (sig->param_types.empty()) return;

    size_t check_count = std::min(actual_types.size(), sig->param_types.size());
    for (size_t i = 0; i < check_count; ++i) {
        if (!AreTypesCompatible(actual_types[i], sig->param_types[i])) {
            std::string msg = "type mismatch for argument " + std::to_string(i + 1) +
                              " of function '" + func_name + "': expected '" +
                              sig->param_types[i].ToString() + "' but got '" +
                              actual_types[i].ToString() + "'";
            ReportErrorWithTraceback(call_loc, frontends::ErrorCode::kTypeMismatch,
                                     msg, sig->defined_at,
                                     "parameter " + std::to_string(i + 1) +
                                     " declared here");
        }
    }
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

// ============================================================================
// DELETE Expression Analysis
// ============================================================================

core::Type PloySema::AnalyzeDeleteExpression(const std::shared_ptr<DeleteExpression> &del_expr) {
    // Validate language
    if (!del_expr->language.empty() && !IsValidLanguage(del_expr->language)) {
        ReportError(del_expr->loc, frontends::ErrorCode::kInvalidLanguage,
                    "unknown language '" + del_expr->language + "' in DELETE expression",
                    "supported languages: python, cpp, rust");
        return core::Type::Void();
    }

    // Analyze the object expression being deleted
    if (!del_expr->object) {
        ReportError(del_expr->loc, frontends::ErrorCode::kMissingExpression,
                    "DELETE requires an object expression");
        return core::Type::Void();
    }

    core::Type obj_type = AnalyzeExpression(del_expr->object);

    // Verify the object is a valid deletable reference (identifier or member)
    if (!std::dynamic_pointer_cast<Identifier>(del_expr->object) &&
        !std::dynamic_pointer_cast<QualifiedIdentifier>(del_expr->object) &&
        !std::dynamic_pointer_cast<MemberExpression>(del_expr->object) &&
        !std::dynamic_pointer_cast<GetAttrExpression>(del_expr->object)) {
        ReportWarning(del_expr->loc, frontends::ErrorCode::kTypeMismatch,
                      "DELETE target should be a variable or object reference",
                      "use DELETE(lang, variable) or DELETE(lang, GET(lang, obj, attr))");
    }

    return core::Type::Void();
}

// ============================================================================
// EXTEND Declaration Analysis
// ============================================================================

void PloySema::AnalyzeExtendDecl(const std::shared_ptr<ExtendDecl> &extend) {
    // Validate language
    if (!extend->language.empty() && !IsValidLanguage(extend->language)) {
        ReportError(extend->loc, frontends::ErrorCode::kInvalidLanguage,
                    "unknown language '" + extend->language + "' in EXTEND declaration",
                    "supported languages: python, cpp, rust");
        return;
    }

    // Validate base class name
    if (extend->base_class.empty()) {
        ReportError(extend->loc, frontends::ErrorCode::kEmptySymbolName,
                    "EXTEND requires a non-empty base class name");
        return;
    }

    // Validate derived name
    if (extend->derived_name.empty()) {
        ReportError(extend->loc, frontends::ErrorCode::kEmptySymbolName,
                    "EXTEND requires a derived type name after AS");
        return;
    }

    // Register the derived type as a symbol so it can be referenced later
    PloySymbol derived_sym;
    derived_sym.kind = PloySymbol::Kind::kVariable;
    derived_sym.name = extend->derived_name;
    derived_sym.type = core::Type::Any(); // The derived class type
    derived_sym.is_mutable = false;
    derived_sym.language = extend->language;
    derived_sym.defined_at = extend->loc;

    if (!DeclareSymbol(derived_sym)) {
        ReportError(extend->loc, frontends::ErrorCode::kRedefinedSymbol,
                    "type '" + extend->derived_name + "' is already defined");
    }

    // Analyze each method override within the EXTEND body
    for (const auto &method_stmt : extend->methods) {
        if (auto func = std::dynamic_pointer_cast<FuncDecl>(method_stmt)) {
            // Save symbol table before entering method scope
            auto saved_symbols = symbols_;

            // Auto-declare 'self' as a local variable in each method.
            // EXTEND methods implicitly receive 'self' referring to the
            // instance of the derived type, similar to Python's self or
            // Rust's &self. This lets METHOD/GET/SET use 'self'.
            PloySymbol self_sym;
            self_sym.kind = PloySymbol::Kind::kVariable;
            self_sym.name = "self";
            self_sym.type = core::Type::Any(); // dynamic type of the extended class
            self_sym.is_mutable = false;
            self_sym.language = extend->language;
            self_sym.defined_at = func->loc;
            symbols_["self"] = self_sym; // force-insert, no redefinition error

            // Register method with qualified name: DerivedName::method_name
            AnalyzeFuncDecl(func);

            // Restore symbol table after method analysis
            symbols_ = saved_symbols;

            // Also register as DerivedName::method for lookup
            std::string qualified = extend->derived_name + "::" + func->name;
            FunctionSignature sig;
            sig.name = qualified;
            sig.language = extend->language;
            sig.param_count = func->params.size();
            sig.param_count_known = true;
            sig.defined_at = func->loc;
            for (const auto &p : func->params) {
                sig.param_types.push_back(ResolveType(p.type));
            }
            sig.return_type = func->return_type
                                  ? ResolveType(func->return_type)
                                  : core::Type::Void();
            RegisterFunctionSignature(qualified, sig);
        } else {
            ReportError(method_stmt->loc, frontends::ErrorCode::kTypeMismatch,
                        "only FUNC declarations are allowed inside EXTEND body");
        }
    }
}

} // namespace polyglot::ploy
