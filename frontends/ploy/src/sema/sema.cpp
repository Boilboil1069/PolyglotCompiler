/**
 * @file     sema.cpp
 * @brief    Ploy language frontend implementation
 *
 * @ingroup  Frontend / Ploy
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <functional>
#include <sstream>
#include <unordered_set>

#include "frontends/ploy/include/command_runner.h"
#include "frontends/ploy/include/package_discovery_cache.h"
#include "frontends/ploy/include/ploy_sema.h"

namespace polyglot::ploy {

namespace {

// Returns true when `expr` is a constant-foldable expression — i.e. a
// literal, a unary/binary expression whose operands are themselves
// constant-foldable, or a pure CallExpression whose callee resolves to a
// plain identifier and whose every argument is itself constant-foldable.
// Used by AnalyzeFuncDecl to validate that named-parameter default
// values can be safely materialised inline at every call site without
// observing runtime state.
bool IsConstFoldableExpression(const std::shared_ptr<Expression> &expr) {
  if (!expr)
    return false;
  if (std::dynamic_pointer_cast<Literal>(expr))
    return true;
  if (auto un = std::dynamic_pointer_cast<UnaryExpression>(expr))
    return IsConstFoldableExpression(un->operand);
  if (auto bin = std::dynamic_pointer_cast<BinaryExpression>(expr))
    return IsConstFoldableExpression(bin->left) &&
           IsConstFoldableExpression(bin->right);
  if (auto call = std::dynamic_pointer_cast<CallExpression>(expr)) {
    // Only intra-Ploy calls qualify; cross-language calls observe
    // foreign runtime state and may have side effects.
    if (!std::dynamic_pointer_cast<Identifier>(call->callee) &&
        !std::dynamic_pointer_cast<QualifiedIdentifier>(call->callee))
      return false;
    for (const auto &arg : call->args) {
      if (!IsConstFoldableExpression(arg))
        return false;
    }
    return true;
  }
  return false;
}

} // namespace

// ============================================================================
// Constructor
// ============================================================================

PloySema::PloySema(frontends::Diagnostics &diagnostics, const PloySemaOptions &options) :
    diagnostics_(diagnostics), strict_mode_(options.strict_mode),
    discovery_enabled_(options.enable_package_discovery),
    discovery_cache_(options.discovery_cache ? options.discovery_cache
                                             : std::make_shared<PackageDiscoveryCache>()),
    command_runner_(options.command_runner ? options.command_runner
                                           : std::make_shared<DefaultCommandRunner>()) {}

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
  if (!stmt)
    return;

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
  } else if (auto if_let = std::dynamic_pointer_cast<IfLetStatement>(stmt)) {
    AnalyzeIfLetStatement(if_let);
  } else if (auto while_stmt = std::dynamic_pointer_cast<WhileStatement>(stmt)) {
    AnalyzeWhileStatement(while_stmt);
  } else if (auto for_stmt = std::dynamic_pointer_cast<ForStatement>(stmt)) {
    AnalyzeForStatement(for_stmt);
  } else if (auto match_stmt = std::dynamic_pointer_cast<MatchStatement>(stmt)) {
    AnalyzeMatchStatement(match_stmt);
  } else if (auto ret = std::dynamic_pointer_cast<ReturnStatement>(stmt)) {
    AnalyzeReturnStatement(ret);
  } else if (auto println = std::dynamic_pointer_cast<PrintlnStmt>(stmt)) {
    // PRINTLN "literal";  �?Stage B2 of the runtime-stdout pipeline.
    // The parser already guarantees that `message` came from a well-formed
    // string literal, so there is nothing to validate semantically. An empty
    // message is intentionally allowed (it lowers to a zero-byte WriteFile
    // call once B3/B4 land). Suppress unused-variable warnings explicitly.
    (void) println;
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
  } else if (auto stage = std::dynamic_pointer_cast<StageDecl>(stmt)) {
    // Validate stage declaration (only semantic-level checks here).
    if (stage->call_target.empty()) {
      ReportError(stage->loc, frontends::ErrorCode::kEmptySymbolName, "STAGE call target is empty");
    }
    // Register stage as a pipeline-local symbol so later stages can
    // reference it via the pipeline symbol table if needed. For now we
    // expose it as a PloySymbol of kind kPipeline (name + location).
    PloySymbol sym;
    sym.kind = PloySymbol::Kind::kPipeline;
    sym.name = stage->name.empty() ? stage->call_target : stage->name;
    sym.type = core::Type::Void();
    sym.defined_at = stage->loc;
    // Declare symbol in the current scope.
    DeclareSymbol(sym);
  } else if (auto venv_config = std::dynamic_pointer_cast<VenvConfigDecl>(stmt)) {
    AnalyzeVenvConfigDecl(venv_config);
  } else if (auto with_stmt = std::dynamic_pointer_cast<WithStatement>(stmt)) {
    AnalyzeWithStatement(with_stmt);
  } else if (auto extend = std::dynamic_pointer_cast<ExtendDecl>(stmt)) {
    AnalyzeExtendDecl(extend);
  } else if (auto cls_decl = std::dynamic_pointer_cast<ClassDecl>(stmt)) {
    // Foreign-class signature block (demand 2026-04-28-9).
    AnalyzeClassDecl(cls_decl);
  } else if (auto alias_decl = std::dynamic_pointer_cast<TypeAliasDecl>(stmt)) {
    AnalyzeTypeAliasDecl(alias_decl);
  } else if (auto const_decl = std::dynamic_pointer_cast<ConstDecl>(stmt)) {
    AnalyzeConstDecl(const_decl);
  } else if (auto lang_pragma = std::dynamic_pointer_cast<LangPragma>(stmt)) {
    // Module-wide language version pin.
    AnalyzeLangPragma(lang_pragma);
  } else if (auto with_lang = std::dynamic_pointer_cast<WithLangBlock>(stmt)) {
    // Scoped language version pin.
    AnalyzeWithLangBlock(with_lang);
  } else if (auto lang_anno = std::dynamic_pointer_cast<LangAnnotation>(stmt)) {
    // Single-statement language version pin.
    AnalyzeLangAnnotation(lang_anno);
  } else if (auto try_stmt = std::dynamic_pointer_cast<TryStatement>(stmt)) {
    AnalyzeTryStatement(try_stmt);
  } else if (auto throw_stmt = std::dynamic_pointer_cast<ThrowStatement>(stmt)) {
    AnalyzeThrowStatement(throw_stmt);
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
    ReportError(link->loc, frontends::ErrorCode::kEmptySymbolName, "LINK target symbol is empty");
  }
  if (link->source_symbol.empty()) {
    ReportError(link->loc, frontends::ErrorCode::kEmptySymbolName, "LINK source symbol is empty");
  }

  // Build link entry

  // Deprecation: the legacy comma-style LINK(...) form is deprecated.
  // Prefer the signed/canonical form: LINK lang::module::func AS FUNC(...) -> ...;
  if (link->is_legacy_form) {
    ReportWarning(link->loc, frontends::ErrorCode::kDeprecatedKeyword,
                  "legacy LINK(...) form is deprecated; use 'LINK <lang>::<module>::<func> AS FUNC(...) -> <ret>' instead");
  }
  LinkEntry entry;
  entry.kind = link->link_kind;
  entry.target_language = link->target_language;
  entry.source_language = link->source_language;
  entry.target_symbol = link->target_symbol;
  entry.source_symbol = link->source_symbol;
  entry.defined_at = link->loc;
  // Pin the foreign-side language version to whatever LANG / WITH LANG /
  // @LANG scope the LINK statement is enclosed in.  Conventionally a LINK
  // is written `LINK(<foreign>, <ploy|host>, <foreign_sym>, <local_sym>)`,
  // so the version that matters for the bridge ABI is `target_language`.
  // Fall back to source_language only when target has no active pin.
  entry.lang_version = ResolveLangVersion(link->target_language);
  if (entry.lang_version.empty()) {
    entry.lang_version = ResolveLangVersion(link->source_language);
  }

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

  // Register the linked function as a known symbol so CALL can reference it.
  // Build a proper function type from the MAP_TYPE entries so that downstream
  // type checking can reason about the linked function precisely.
  PloySymbol link_sym;
  link_sym.kind = PloySymbol::Kind::kLinkTarget;
  link_sym.name = link->target_symbol;
  link_sym.language = link->target_language;

  // Derive parameter types from MAP_TYPE entries and build a function type.
  // MAP_TYPE(first_arg, second_arg): first_arg (parser's source_*) is the
  // target function's native type, second_arg (parser's target_*) is the
  // source function's native type.
  std::vector<core::Type> link_param_types;
  for (const auto &mapping : entry.param_mappings) {
    core::Type param_type = type_system_.MapFromLanguage(
        mapping.source_language.empty() ? link->target_language : mapping.source_language,
        mapping.source_type);
    link_param_types.push_back(param_type);
  }
  // Resolve the return type from the RETURNS clause if present
  core::Type link_return_type = core::Type::Unknown();
  if (link->return_type) {
    link_return_type = ResolveType(link->return_type);
  }

  if (!link_param_types.empty()) {
    // Synthesize a function type: (param_types...) -> return_type
    link_sym.type =
        type_system_.FunctionType(link->target_symbol, link_return_type, link_param_types);
  } else if (link->return_type) {
    link_sym.type = type_system_.FunctionType(link->target_symbol, link_return_type, {});
  } else {
    link_sym.type = core::Type::Unknown();
  }
  link_sym.defined_at = link->loc;
  // Do not report redefinition for link targets �?they may overlap with imports
  symbols_.try_emplace(link->target_symbol, link_sym);

  // Register the LINK target as a known function signature so that the
  // checker can validate types and parameter counts at call sites.
  // MAP_TYPE entries define the type-conversion table for the cross-language
  // boundary.  However, they do not reliably define parameter counts because
  // METHOD calls (instance methods) have an implicit receiver that is not
  // listed in MAP_TYPE.  Keep param_count_known false to avoid false errors.
  if (!entry.param_mappings.empty()) {
    FunctionSignature sig;
    sig.name = link->target_symbol;
    sig.language = link->target_language;
    sig.param_count = entry.param_mappings.size();
    sig.param_count_known = false;
    sig.defined_at = link->loc;
    for (const auto &mapping : entry.param_mappings) {
      // The first arg of MAP_TYPE (parser's source_*) is the target
      // function's native type (matching link->target_language).
      core::Type param_type = type_system_.MapFromLanguage(
          mapping.source_language.empty() ? link->target_language : mapping.source_language,
          mapping.source_type);
      sig.param_types.push_back(param_type);
      sig.param_names.push_back(mapping.target_type);
    }

    // Resolve return type from RETURNS clause if available.
    if (link->return_type) {
      sig.return_type = link_return_type;
    }

    // Build ABI signature for cross-language validation
    sig.abi = BuildABISignature(sig, link->target_language);
    abi_signatures_[link->target_symbol] = sig.abi;

    // Also build the source-side ABI for compatibility checking
    if (!link->source_language.empty()) {
      FunctionSignature source_sig;
      source_sig.name = link->source_symbol;
      source_sig.language = link->source_language;
      source_sig.param_count = entry.param_mappings.size();
      source_sig.param_count_known = false;
      source_sig.defined_at = link->loc;
      for (const auto &mapping : entry.param_mappings) {
        // The second arg of MAP_TYPE (parser's target_*) is the source
        // function's native type (matching link->source_language).
        core::Type source_type = type_system_.MapFromLanguage(
            mapping.target_language.empty() ? link->source_language : mapping.target_language,
            mapping.target_type);
        source_sig.param_types.push_back(source_type);
      }
      auto source_abi = BuildABISignature(source_sig, link->source_language);
      abi_signatures_[link->source_symbol] = source_abi;

      // Cross-validate ABI compatibility between target and source.
      // MAP_TYPE entries declare explicit marshalling, so ABI size/pointer
      // mismatches are expected and handled by the generated conversion
      // code.  Report them as warnings rather than hard errors.
      if (sig.abi->is_complete && source_abi->is_complete) {
        std::string compat_err = sig.abi->ValidateCompatibility(*source_abi);
        if (!compat_err.empty()) {
          ReportWarning(link->loc, frontends::ErrorCode::kTypeMismatch,
                        "ABI difference in LINK '" + link->target_symbol + "' <-> '" +
                            link->source_symbol + "': " + compat_err,
                        "MAP_TYPE entries will generate marshalling code for this conversion");
        }
      }
    }

    RegisterFunctionSignature(link->target_symbol, sig);
    // Also register the source-side signature so the linker can build
    // CrossLangSymbol param descriptors for both sides of the LINK.
    if (!link->source_language.empty()) {
      FunctionSignature src_reg_sig;
      src_reg_sig.name = link->source_symbol;
      src_reg_sig.language = link->source_language;
      src_reg_sig.param_count = entry.param_mappings.size();
      src_reg_sig.param_count_known = false;
      src_reg_sig.defined_at = link->loc;
      for (const auto &mapping : entry.param_mappings) {
        core::Type st = type_system_.MapFromLanguage(
            mapping.target_language.empty() ? link->source_language : mapping.target_language,
            mapping.target_type);
        src_reg_sig.param_types.push_back(st);
      }
      src_reg_sig.abi = BuildABISignature(src_reg_sig, link->source_language);
      RegisterFunctionSignature(link->source_symbol, src_reg_sig);
    }
  } else {
    // LINK without MAP_TYPE entries is valid �?it simply means that no
    // per-parameter ABI mapping is declared.  Parameter validation at call
    // sites will be skipped for this link.  Emit an informational warning
    // so developers are aware, but never treat this as an error.
    ReportWarning(link->loc, frontends::ErrorCode::kTypeMismatch,
                  "LINK '" + link->target_symbol +
                      "' has no MAP_TYPE entries; parameter validation disabled");
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
      Report(import->loc, "invalid version operator '" + import->version_op + "' in IMPORT");
    }
    if (import->version_constraint.empty()) {
      Report(import->loc, "version operator without version number in IMPORT");
    } else if (!IsValidVersionString(import->version_constraint)) {
      Report(import->loc, "invalid version string '" + import->version_constraint + "' in IMPORT");
    }
    if (import->package_name.empty()) {
      Report(import->loc, "version constraints are only valid for PACKAGE imports");
    }
  }

  // Validate selective imports
  if (!import->selected_symbols.empty()) {
    if (import->package_name.empty()) {
      Report(import->loc, "selective imports (::) are only valid for PACKAGE imports");
    }
    // Selective imports and alias cannot be used together because
    // the alias target is ambiguous (does it refer to symbol A or B?)
    if (!import->alias.empty()) {
      Report(import->loc, "selective import and AS alias cannot be combined �?"
                          "alias '" +
                              import->alias +
                              "' is ambiguous when "
                              "multiple symbols are selected");
    }
    // Check for duplicate symbols in the selection list
    std::unordered_set<std::string> seen;
    for (const auto &sym : import->selected_symbols) {
      if (!seen.insert(sym).second) {
        Report(import->loc, "duplicate symbol '" + sym + "' in selective import list");
      }
    }
  }

  // Run package auto-discovery for the specified language if not done yet
  if (!import->language.empty() && !import->package_name.empty() && discovery_enabled_) {
    // Build the canonical cache key from language, manager kind, env path
    std::string venv_path;
    VenvConfigDecl::ManagerKind manager = VenvConfigDecl::ManagerKind::kVenv;
    for (const auto &vc : venv_configs_) {
      if (vc.language == import->language) {
        venv_path = vc.venv_path;
        manager = vc.manager;
        break;
      }
    }

    std::string manager_str;
    switch (manager) {
    case VenvConfigDecl::ManagerKind::kConda:
      manager_str = "conda";
      break;
    case VenvConfigDecl::ManagerKind::kUv:
      manager_str = "uv";
      break;
    case VenvConfigDecl::ManagerKind::kPipenv:
      manager_str = "pipenv";
      break;
    case VenvConfigDecl::ManagerKind::kPoetry:
      manager_str = "poetry";
      break;
    default:
      manager_str = "venv";
      break;
    }
    std::string cache_key =
        PackageDiscoveryCache::MakeKey(import->language, manager_str, venv_path);

    if (!discovery_cache_->HasDiscovered(cache_key)) {
      // Cache miss �?run external commands and store results
      DiscoverPackages(import->language, venv_path, manager);
      discovery_cache_->Store(cache_key, discovered_packages_);
    } else {
      // Cache hit �?merge cached results into instance-local map
      auto cached = discovery_cache_->Retrieve(cache_key);
      for (const auto &[k, v] : cached) {
        discovered_packages_.try_emplace(k, v);
      }
    }

    // If package discovery found this package, validate the version constraint
    std::string pkg_key = import->language + "::" + import->package_name;
    auto pkg_it = discovered_packages_.find(pkg_key);
    if (pkg_it != discovered_packages_.end()) {
      // Package found �?check version constraint if specified
      if (!import->version_op.empty() && !import->version_constraint.empty()) {
        if (!CompareVersions(pkg_it->second.version, import->version_constraint,
                             import->version_op)) {
          Report(import->loc, "installed package '" + import->package_name + "' version " +
                                  pkg_it->second.version + " does not satisfy constraint " +
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
    sel_sym.type = core::Type::Unknown(); // type unknown until call-site resolves it
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

  // Visibility check (since v1.16.0): only PUB symbols may be EXPORTed.
  // An *explicit* PRIVATE on the declaration is a hard error; a symbol
  // that still carries the default kPrivate (no PUB / PRIVATE keyword in
  // the source) is auto-promoted to kPub with a deprecation warning so
  // pre-v1.16.0 sources keep compiling.
  if (it->second.visibility != Visibility::kPub) {
    if (it->second.visibility_explicit) {
      ReportError(export_decl->loc, frontends::ErrorCode::kUnknown,
                  "EXPORT requires the symbol '" + export_decl->symbol_name +
                      "' to be declared PUB; remove PRIVATE or change it to PUB");
      return;
    }
    diagnostics_.ReportWarning(
        export_decl->loc, frontends::ErrorCode::kDeprecatedKeyword,
        "EXPORT of symbol '" + export_decl->symbol_name +
            "' without an explicit PUB modifier; mark the declaration "
            "PUB to silence this warning (auto-promoted for compatibility)");
    it->second.visibility = Visibility::kPub;
  }

  // Mark the symbol as exported
  it->second.external_name =
      export_decl->external_name.empty() ? export_decl->symbol_name : export_decl->external_name;
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
  // Validate annotations and type parameter bounds before resolving any
  // signature/body types so unknown attributes surface as a warning and
  // unknown bounds as an error (since v1.15.0 / v1.16.0).
  ValidateAttributes(func->attributes);
  ValidateTypeParamBounds(func->type_params);
  std::vector<std::string> tp_added;
  tp_added.reserve(func->type_params.size());
  for (const auto &tp : func->type_params) {
    if (active_type_params_.insert(tp.name).second) {
      tp_added.push_back(tp.name);
    }
  }

  // Resolve return type
  core::Type ret_type = func->return_type ? ResolveType(func->return_type) : core::Type::Void();

  // Build function type with parameter types
  std::vector<core::Type> param_types;
  for (const auto &param : func->params) {
    core::Type pt = param.type ? ResolveType(param.type) : core::Type::Unknown();
    if (!param.type) {
      ReportStrictDiag(func->loc, frontends::ErrorCode::kTypeMismatch,
                       "parameter '" + param.name +
                           "' has no type annotation; "
                           "defaults to Unknown �?add explicit type annotation");
    }
    param_types.push_back(pt);
  }

  // Register function symbol in the OUTER scope (before entering function body)
  PloySymbol sym;
  sym.kind = PloySymbol::Kind::kFunction;
  sym.name = func->name;
  sym.type = type_system_.FunctionType(func->name, ret_type, param_types);
  sym.visibility = func->visibility;
  sym.visibility_explicit = func->visibility_explicit;
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
  for (const auto &param : func->params) {
    sig.param_names.push_back(param.name);
    sig.param_has_default.push_back(static_cast<bool>(param.default_value));
    sig.param_default_values.push_back(param.default_value);
  }
  // Validate each default-value expression is constant-foldable so the
  // lowering pass can safely materialise it inline at every call site.
  // Also re-check the structural rule that no required parameter follows
  // a defaulted one (the parser has already reported this, but a
  // hand-built AST or a recovery path could still produce one).
  bool seen_default = false;
  for (const auto &param : func->params) {
    if (param.default_value) {
      seen_default = true;
      if (!IsConstFoldableExpression(param.default_value)) {
        ReportError(param.default_value->loc, frontends::ErrorCode::kTypeMismatch,
                    "default value for parameter '" + param.name +
                        "' must be a constant expression (literal, unary or "
                        "binary of literals, or a pure intra-Ploy call)",
                    "replace the default with a CONST-foldable expression or "
                    "a pure function call");
      }
    } else if (seen_default) {
      ReportError(func->loc, frontends::ErrorCode::kTypeMismatch,
                  "required parameter '" + param.name +
                      "' cannot follow a parameter with a default value");
    }
  }
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
  if (func->is_async) ++async_depth_;
  AnalyzeBlockStatements(func->body);
  if (func->is_async) --async_depth_;
  current_return_type_ = saved_return;

  // Restore the symbol table to the state before entering the function body.
  // This ensures local variables (LET/VAR inside the body) do not leak into
  // the module scope, so the same variable name can be reused across
  // different FUNC declarations.
  symbols_ = saved_symbols;

  // Pop the generic type-parameter names brought into scope above.
  for (const auto &name : tp_added) {
    active_type_params_.erase(name);
  }

  // Re-register the function symbol itself (it was declared in outer scope
  // but may have been overwritten by the restore if it wasn't in saved_symbols
  // before DeclareSymbol was called). Since we declared it before the save,
  // it is already in saved_symbols �?no extra action needed.
}

// ============================================================================
// Variable Declaration Analysis
// ============================================================================

void PloySema::AnalyzeVarDecl(const std::shared_ptr<VarDecl> &var) {
  core::Type var_type = core::Type::Unknown();

  // Resolve explicit type if given
  if (var->type) {
    var_type = ResolveType(var->type);
  }

  // Check initializer
  if (var->init) {
    // NULL is reserved for raw-pointer interop; using it to initialise an
    // OPTION<T> is a common bug ported from null-pointer languages.  Emit
    // a targeted diagnostic suggesting `None` (since v1.18.0).
    if (var->type && var_type.kind == core::TypeKind::kOptional) {
      if (auto lit = std::dynamic_pointer_cast<Literal>(var->init)) {
        if (lit->kind == Literal::Kind::kNull) {
          ReportError(var->loc, frontends::ErrorCode::kTypeMismatch,
                      "cannot initialise OPTION<T> with NULL; use 'None' instead",
                      "OPTION<T> is a tagged union — NULL is reserved for raw-pointer interop");
        }
      }
    }
    core::Type init_type = AnalyzeExpression(var->init);

    if (var->type) {
      // Check type compatibility
      if (!AreTypesCompatible(init_type, var_type)) {
        ReportError(var->loc, frontends::ErrorCode::kTypeMismatch,
                    "type mismatch in variable '" + var->name + "' declaration: expected '" +
                        var_type.ToString() + "' but initializer has type '" +
                        init_type.ToString() + "'",
                    "change the type annotation or the initializer expression");
      }

      // If the initializer is a cross-language call, validate the return
      // type against the declared variable type for early mismatch detection.
      if (auto cross_call = std::dynamic_pointer_cast<CrossLangCallExpression>(var->init)) {
        const FunctionSignature *sig = LookupSignature(cross_call->function);
        if (!sig) {
          auto pos = cross_call->function.rfind("::");
          if (pos != std::string::npos) {
            sig = LookupSignature(cross_call->function.substr(pos + 2));
          }
        }
        ValidateReturnType(var->loc, cross_call->function, var_type, sig);
      } else if (auto call = std::dynamic_pointer_cast<CallExpression>(var->init)) {
        std::string func_name;
        if (auto id = std::dynamic_pointer_cast<Identifier>(call->callee)) {
          func_name = id->name;
        } else if (auto qid = std::dynamic_pointer_cast<QualifiedIdentifier>(call->callee)) {
          func_name = qid->qualifier + "::" + qid->name;
        }
        if (!func_name.empty()) {
          const FunctionSignature *sig = LookupSignature(func_name);
          ValidateReturnType(var->loc, func_name, var_type, sig);
        }
      } else if (auto method_call = std::dynamic_pointer_cast<MethodCallExpression>(var->init)) {
        const FunctionSignature *sig = LookupSignature(method_call->method_name);
        ValidateReturnType(var->loc, method_call->method_name, var_type, sig);
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

  if (var_type.kind == core::TypeKind::kAny || var_type.kind == core::TypeKind::kUnknown) {
    ReportStrictDiag(var->loc, frontends::ErrorCode::kTypeMismatch,
                     "variable '" + var->name +
                         "' resolved to type Any/Unknown; "
                         "consider adding explicit type annotation");
  }

  DeclareSymbol(sym);
}

// ============================================================================
// Control Flow Analysis
// ============================================================================

void PloySema::AnalyzeIfStatement(const std::shared_ptr<IfStatement> &if_stmt) {
  core::Type cond_type = AnalyzeExpression(if_stmt->condition);
  // Allow Bool, Any, Unknown, Int (truthy conversion), and Pointer/Class
  // types as IF conditions.  Cross-language calls often return Unknown or
  // Any when the return type cannot be statically inferred; these should
  // be treated as implicitly convertible to bool (zero/null = false,
  // non-zero/non-null = true).  Only types that are clearly non-boolean
  // (e.g. String, Array, Struct, Void) produce an error.
  bool is_truthy =
      (cond_type.kind == core::TypeKind::kBool || cond_type.kind == core::TypeKind::kAny ||
       cond_type.kind == core::TypeKind::kUnknown || cond_type.kind == core::TypeKind::kInt ||
       cond_type.kind == core::TypeKind::kFloat || cond_type.kind == core::TypeKind::kPointer ||
       cond_type.kind == core::TypeKind::kClass || cond_type.kind == core::TypeKind::kReference ||
       cond_type.kind == core::TypeKind::kOptional);
  if (!is_truthy) {
    Report(if_stmt->loc, "IF condition must be a boolean expression");
  }
  AnalyzeBlockStatements(if_stmt->then_body);
  AnalyzeBlockStatements(if_stmt->else_body);
}

void PloySema::AnalyzeIfLetStatement(const std::shared_ptr<IfLetStatement> &if_let) {
  core::Type scrutinee = AnalyzeExpression(if_let->scrutinee);

  // Scrutinee must be OPTION<T> (or fall back to Any/Unknown for cross-
  // language values that did not resolve statically).
  if (scrutinee.kind != core::TypeKind::kOptional &&
      scrutinee.kind != core::TypeKind::kAny &&
      scrutinee.kind != core::TypeKind::kUnknown) {
    ReportError(if_let->loc, frontends::ErrorCode::kTypeMismatch,
                "IF LET scrutinee must be an OPTION<T> value; got '" +
                    scrutinee.ToString() + "'");
  }

  core::Type inner = core::Type::Unknown();
  if (scrutinee.kind == core::TypeKind::kOptional && !scrutinee.type_args.empty()) {
    inner = scrutinee.type_args[0];
  }

  // Constructor must be Some / None.  Some takes one binding; None takes none.
  std::vector<std::string> introduced;
  if (if_let->ctor == "Some") {
    if (if_let->bindings.size() != 1) {
      ReportError(if_let->loc, frontends::ErrorCode::kTypeMismatch,
                  "IF LET Some(...) requires exactly one binding name");
    } else {
      PloySymbol sym;
      sym.kind = PloySymbol::Kind::kVariable;
      sym.name = if_let->bindings[0];
      sym.type = inner;
      sym.is_mutable = false;
      sym.defined_at = if_let->loc;
      DeclareSymbol(sym);
      introduced.push_back(sym.name);
    }
  } else if (if_let->ctor == "None") {
    if (!if_let->bindings.empty()) {
      ReportError(if_let->loc, frontends::ErrorCode::kTypeMismatch,
                  "IF LET None takes no bindings");
    }
  } else {
    ReportError(if_let->loc, frontends::ErrorCode::kTypeMismatch,
                "IF LET constructor must be 'Some' or 'None'; got '" + if_let->ctor + "'");
  }

  AnalyzeBlockStatements(if_let->then_body);
  for (const auto &name : introduced) symbols_.erase(name);
  AnalyzeBlockStatements(if_let->else_body);
}

void PloySema::AnalyzeWhileStatement(const std::shared_ptr<WhileStatement> &while_stmt) {
  core::Type cond_type = AnalyzeExpression(while_stmt->condition);
  // Same truthy-conversion rules as IF: allow Bool, Any, Unknown, Int,
  // Float, Pointer, etc.
  bool is_truthy =
      (cond_type.kind == core::TypeKind::kBool || cond_type.kind == core::TypeKind::kAny ||
       cond_type.kind == core::TypeKind::kUnknown || cond_type.kind == core::TypeKind::kInt ||
       cond_type.kind == core::TypeKind::kFloat || cond_type.kind == core::TypeKind::kPointer ||
       cond_type.kind == core::TypeKind::kClass || cond_type.kind == core::TypeKind::kReference ||
       cond_type.kind == core::TypeKind::kOptional);
  if (!is_truthy) {
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
    sym.type = core::Type::Unknown();
    ReportStrictDiag(for_stmt->loc, frontends::ErrorCode::kTypeMismatch,
                     "FOR iterator '" + for_stmt->iterator_name +
                         "' type could not be inferred from iterable; defaults to Unknown �?"
                         "add explicit type annotation or ensure iterable is typed");
  }
  DeclareSymbol(sym);

  loop_depth_++;
  AnalyzeBlockStatements(for_stmt->body);
  loop_depth_--;

  // Remove iterator from scope
  symbols_.erase(for_stmt->iterator_name);
}

void PloySema::AnalyzeMatchStatement(const std::shared_ptr<MatchStatement> &match_stmt) {
  // Type-check the scrutinee once; every CASE pattern is checked against
  // this static type.  Cross-language `Any` / `Unknown` scrutinees disable
  // the structural compatibility checks below (they are reported once via
  // strict-mode diagnostics on the scrutinee itself).
  core::Type scrutinee = AnalyzeExpression(match_stmt->value);

  // Track previous arms for reachability analysis (demand 2026-04-28-10).
  // We collect:
  //   * literal_seen      : exact literal patterns seen so far;
  //   * has_irrefutable   : whether any preceding arm matches every value;
  //   * bool_seen         : which boolean literals have been covered;
  //   * option_some_seen  : whether `Some(_)` has been covered;
  //   * option_none_seen  : whether `None` has been covered.
  std::unordered_set<std::string> literal_seen;
  bool has_irrefutable = false;
  bool bool_true_seen = false;
  bool bool_false_seen = false;
  bool option_some_seen = false;
  bool option_none_seen = false;
  bool has_default = false;

  // Helper that records the simple coverage facts contributed by `pattern`.
  // Recurses into OrPattern only (other compound patterns do not add to the
  // global coverage table because they carry sub-bindings).
  std::function<void(const std::shared_ptr<Pattern> &)> record_coverage =
      [&](const std::shared_ptr<Pattern> &pattern) {
        if (!pattern) return;
        if (auto lit = std::dynamic_pointer_cast<LiteralPattern>(pattern)) {
          if (lit->literal) {
            literal_seen.insert(lit->literal->value);
            if (lit->literal->kind == Literal::Kind::kBool) {
              if (lit->literal->value == "true") bool_true_seen = true;
              if (lit->literal->value == "false") bool_false_seen = true;
            }
          }
        } else if (auto ctor = std::dynamic_pointer_cast<ConstructorPattern>(pattern)) {
          if (ctor->name == "Some") option_some_seen = true;
          if (ctor->name == "None") option_none_seen = true;
        } else if (auto orp = std::dynamic_pointer_cast<OrPattern>(pattern)) {
          for (const auto &alt : orp->alternatives) record_coverage(alt);
        }
      };

  for (const auto &match_case : match_stmt->cases) {
    if (!match_case.pattern) {
      // DEFAULT case.
      if (has_default) {
        Report(match_stmt->loc, "MATCH has multiple DEFAULT cases");
      }
      if (has_irrefutable || has_default) {
        ReportWarning(match_case.body.empty() ? match_stmt->loc
                                              : match_case.body.front()->loc,
                      frontends::ErrorCode::kUnreachableCode,
                      "MATCH arm is unreachable: a previous arm already "
                      "covers every value");
      }
      has_default = true;
      AnalyzeBlockStatements(match_case.body);
      continue;
    }

    if (has_irrefutable || has_default) {
      ReportWarning(match_case.pattern->loc, frontends::ErrorCode::kUnreachableCode,
                    "MATCH arm is unreachable: a previous arm already "
                    "covers every value");
    }

    // Detect duplicate literal arms �?these are statically unreachable
    // even before the irrefutable arm appears.
    if (auto lit = std::dynamic_pointer_cast<LiteralPattern>(match_case.pattern)) {
      if (lit->literal && literal_seen.count(lit->literal->value) != 0U) {
        ReportWarning(lit->loc, frontends::ErrorCode::kUnreachableCode,
                      "MATCH arm is unreachable: literal '" + lit->literal->value +
                          "' is already matched by a previous arm");
      }
    }

    // Type-check the pattern and collect bindings into a fresh per-arm
    // table.  We then install the bindings, analyse guard + body, and
    // restore the surrounding scope.
    std::unordered_map<std::string, core::Type> bindings;
    AnalyzePattern(match_case.pattern, scrutinee, bindings);

    // Stash any pre-existing symbols the bindings would shadow so the
    // outer scope is restored cleanly after the arm.
    std::vector<std::pair<std::string, PloySymbol>> shadowed;
    std::vector<std::string> introduced;
    for (const auto &kv : bindings) {
      auto it = symbols_.find(kv.first);
      if (it != symbols_.end()) {
        shadowed.emplace_back(kv.first, it->second);
      } else {
        introduced.push_back(kv.first);
      }
      PloySymbol sym;
      sym.kind = PloySymbol::Kind::kVariable;
      sym.name = kv.first;
      sym.type = kv.second;
      sym.is_mutable = false;
      sym.defined_at = match_case.pattern->loc;
      symbols_[kv.first] = sym;
    }

    if (match_case.guard) {
      core::Type gt = AnalyzeExpression(match_case.guard);
      if (gt.kind != core::TypeKind::kBool && gt.kind != core::TypeKind::kAny &&
          gt.kind != core::TypeKind::kUnknown && gt.kind != core::TypeKind::kInvalid) {
        ReportError(match_case.guard->loc, frontends::ErrorCode::kTypeMismatch,
                    "MATCH guard must be a boolean expression, got '" + gt.ToString() + "'");
      }
    }

    AnalyzeBlockStatements(match_case.body);

    // Restore scope (remove introduced bindings, reinstall shadowed).
    for (const auto &name : introduced) symbols_.erase(name);
    for (auto &kv : shadowed) symbols_[kv.first] = std::move(kv.second);

    // A pattern is irrefutable only when it has no guard *and* it
    // unconditionally accepts every value of the scrutinee type.
    if (!match_case.guard && IsIrrefutablePattern(match_case.pattern)) {
      has_irrefutable = true;
    }
    record_coverage(match_case.pattern);
  }

  // Exhaustiveness check.  Hard error when the static type has a finite,
  // statically-known cover and the arms missed it; suggestion is to add
  // `_` (the wildcard) which is the canonical universal arm in `.ploy`.
  if (!has_default && !has_irrefutable) {
    if (scrutinee.kind == core::TypeKind::kBool) {
      if (!(bool_true_seen && bool_false_seen)) {
        ReportError(match_stmt->loc, frontends::ErrorCode::kTypeMismatch,
                    "non-exhaustive MATCH on bool: missing " +
                        std::string(bool_true_seen ? "FALSE"
                                    : bool_false_seen ? "TRUE"
                                                     : "TRUE and FALSE") +
                        " arm",
                    "add a `CASE _` arm or a `DEFAULT` block");
      }
    } else if (scrutinee.kind == core::TypeKind::kOptional) {
      if (!(option_some_seen && option_none_seen)) {
        ReportError(match_stmt->loc, frontends::ErrorCode::kTypeMismatch,
                    "non-exhaustive MATCH on OPTION: missing " +
                        std::string(option_some_seen ? "None"
                                    : option_none_seen ? "Some(_)"
                                                       : "Some(_) and None") +
                        " arm",
                    "add a `CASE _` arm or cover both Some and None");
      }
    } else if (scrutinee.kind != core::TypeKind::kAny &&
               scrutinee.kind != core::TypeKind::kUnknown &&
               scrutinee.kind != core::TypeKind::kInvalid) {
      // For open types (i32, string, �? we cannot prove exhaustiveness
      // without a wildcard / default.
      ReportError(match_stmt->loc, frontends::ErrorCode::kTypeMismatch,
                  "non-exhaustive MATCH on '" + scrutinee.ToString() +
                      "': no `_` wildcard or `DEFAULT` arm",
                  "add `CASE _ { �?}` or `DEFAULT { �?}` to cover the "
                  "remaining values");
    }
  }
}

bool PloySema::IsIrrefutablePattern(const std::shared_ptr<Pattern> &pattern) const {
  if (!pattern) return false;
  if (std::dynamic_pointer_cast<WildcardPattern>(pattern)) return true;
  if (std::dynamic_pointer_cast<IdentifierPattern>(pattern)) return true;
  // Type-guard pattern (`name : Type`) is statically refined: at this
  // point sema has already validated the scrutinee type, so the runtime
  // check is always satisfied for the matched arm and the pattern is
  // irrefutable for the scrutinee's static type.
  if (std::dynamic_pointer_cast<TypePattern>(pattern)) return true;
  if (auto bind = std::dynamic_pointer_cast<BindingPattern>(pattern)) {
    return IsIrrefutablePattern(bind->sub);
  }
  if (auto orp = std::dynamic_pointer_cast<OrPattern>(pattern)) {
    for (const auto &alt : orp->alternatives) {
      if (IsIrrefutablePattern(alt)) return true;
    }
  }
  // Tuple destructuring `(a, b, ...)` is irrefutable iff every element
  // sub-pattern is itself irrefutable.  A single literal element is
  // enough to make the whole tuple pattern refutable.
  if (auto tup = std::dynamic_pointer_cast<TuplePattern>(pattern)) {
    for (const auto &elt : tup->elements) {
      if (!IsIrrefutablePattern(elt)) return false;
    }
    return true;
  }
  // Struct destructuring `Name { f1, f2, ... }` is irrefutable iff every
  // field sub-pattern is irrefutable (shorthand bindings without an
  // explicit sub are always irrefutable).  The `..` rest specifier does
  // not affect irrefutability.
  if (auto st = std::dynamic_pointer_cast<StructPattern>(pattern)) {
    for (const auto &fp : st->fields) {
      if (fp.sub && !IsIrrefutablePattern(fp.sub)) return false;
    }
    return true;
  }
  return false;
}

bool PloySema::AnalyzePattern(const std::shared_ptr<Pattern> &pattern,
                              const core::Type &scrutinee_type,
                              std::unordered_map<std::string, core::Type> &out_bindings) {
  if (!pattern) return false;

  // Wildcard `_` �?accepts everything, binds nothing.
  if (std::dynamic_pointer_cast<WildcardPattern>(pattern)) {
    return true;
  }

  // Bare identifier �?binds the entire scrutinee.
  if (auto id = std::dynamic_pointer_cast<IdentifierPattern>(pattern)) {
    out_bindings[id->name] = scrutinee_type;
    return true;
  }

  // Literal �?must be assignment-compatible with the scrutinee type.
  if (auto lit = std::dynamic_pointer_cast<LiteralPattern>(pattern)) {
    core::Type lit_type;
    if (lit->literal) {
      switch (lit->literal->kind) {
      case Literal::Kind::kInteger: lit_type = core::Type::Int(); break;
      case Literal::Kind::kFloat:   lit_type = core::Type::Float(); break;
      case Literal::Kind::kString:  lit_type = core::Type::String(); break;
      case Literal::Kind::kBool:    lit_type = core::Type::Bool(); break;
      case Literal::Kind::kNull:    lit_type = core::Type::Unknown(); break;
      }
    }
    if (scrutinee_type.kind != core::TypeKind::kAny &&
        scrutinee_type.kind != core::TypeKind::kUnknown &&
        scrutinee_type.kind != core::TypeKind::kInvalid &&
        lit_type.kind != core::TypeKind::kInvalid &&
        lit_type.kind != core::TypeKind::kUnknown &&
        !AreTypesCompatible(lit_type, scrutinee_type)) {
      ReportError(pattern->loc, frontends::ErrorCode::kTypeMismatch,
                  "literal pattern of type '" + lit_type.ToString() +
                      "' cannot match scrutinee of type '" +
                      scrutinee_type.ToString() + "'");
      return false;
    }
    return true;
  }

  // Range �?both endpoints must be numeric and compatible with scrutinee.
  if (auto rng = std::dynamic_pointer_cast<RangePattern>(pattern)) {
    if (scrutinee_type.kind != core::TypeKind::kInt &&
        scrutinee_type.kind != core::TypeKind::kFloat &&
        scrutinee_type.kind != core::TypeKind::kAny &&
        scrutinee_type.kind != core::TypeKind::kUnknown &&
        scrutinee_type.kind != core::TypeKind::kInvalid) {
      ReportError(pattern->loc, frontends::ErrorCode::kTypeMismatch,
                  "range pattern requires a numeric scrutinee, got '" +
                      scrutinee_type.ToString() + "'");
      return false;
    }
    return true;
  }

  // Or-pattern �?every alternative must check against the same scrutinee
  // and must bind the same set of names with compatible types.
  if (auto orp = std::dynamic_pointer_cast<OrPattern>(pattern)) {
    if (orp->alternatives.empty()) return true;
    std::unordered_map<std::string, core::Type> first_bindings;
    AnalyzePattern(orp->alternatives.front(), scrutinee_type, first_bindings);
    for (size_t i = 1; i < orp->alternatives.size(); ++i) {
      std::unordered_map<std::string, core::Type> alt_bindings;
      AnalyzePattern(orp->alternatives[i], scrutinee_type, alt_bindings);
      // Reject bindings that are not present in every branch �?using such
      // a binding inside the body would be unsound.
      for (const auto &kv : first_bindings) {
        if (alt_bindings.find(kv.first) == alt_bindings.end()) {
          ReportError(orp->alternatives[i]->loc, frontends::ErrorCode::kTypeMismatch,
                      "or-pattern alternative does not bind '" + kv.first +
                          "' which is bound by a sibling alternative");
        }
      }
      for (const auto &kv : alt_bindings) {
        if (first_bindings.find(kv.first) == first_bindings.end()) {
          ReportError(orp->alternatives[i]->loc, frontends::ErrorCode::kTypeMismatch,
                      "or-pattern alternative binds '" + kv.first +
                          "' which is not bound by sibling alternatives");
        }
      }
    }
    out_bindings.insert(first_bindings.begin(), first_bindings.end());
    return true;
  }

  // Binding `name @ sub`.
  if (auto bind = std::dynamic_pointer_cast<BindingPattern>(pattern)) {
    out_bindings[bind->name] = scrutinee_type;
    if (bind->sub) AnalyzePattern(bind->sub, scrutinee_type, out_bindings);
    return true;
  }

  // Type-guard: `name : T`.  The binding is introduced with the *refined*
  // type T regardless of the scrutinee's static type �?this is the whole
  // point of type-guard patterns.
  if (auto tp = std::dynamic_pointer_cast<TypePattern>(pattern)) {
    core::Type refined = ResolveType(tp->type_node);
    if (!tp->name.empty()) {
      out_bindings[tp->name] = refined;
    }
    return true;
  }

  // Tuple �?element-wise check against tuple component types when known.
  if (auto tup = std::dynamic_pointer_cast<TuplePattern>(pattern)) {
    bool have_components = scrutinee_type.kind == core::TypeKind::kTuple &&
                           scrutinee_type.type_args.size() == tup->elements.size();
    if (scrutinee_type.kind == core::TypeKind::kTuple &&
        scrutinee_type.type_args.size() != tup->elements.size()) {
      ReportError(pattern->loc, frontends::ErrorCode::kTypeMismatch,
                  "tuple pattern arity " + std::to_string(tup->elements.size()) +
                      " does not match scrutinee arity " +
                      std::to_string(scrutinee_type.type_args.size()));
      return false;
    }
    for (size_t i = 0; i < tup->elements.size(); ++i) {
      core::Type elem_type =
          have_components ? scrutinee_type.type_args[i] : core::Type::Unknown();
      AnalyzePattern(tup->elements[i], elem_type, out_bindings);
    }
    return true;
  }

  // Struct �?bindings get the component type when the struct schema is
  // known, otherwise Unknown is recorded so the body still type-checks.
  if (auto sp = std::dynamic_pointer_cast<StructPattern>(pattern)) {
    for (const auto &fp : sp->fields) {
      // Struct schemas are tracked elsewhere; for the binding we currently
      // only know the field name, so use Unknown unless a sub-pattern
      // supplies a more specific shape via a nested type pattern.
      core::Type field_type = core::Type::Unknown();
      if (!fp.sub) {
        out_bindings[fp.name] = field_type;
      } else {
        AnalyzePattern(fp.sub, field_type, out_bindings);
      }
    }
    return true;
  }

  // Constructor �?Some(x) / None for OPTION, or any nominal ctor.  The
  // important static guarantee is the inner-pattern check on Some(_).
  if (auto ctor = std::dynamic_pointer_cast<ConstructorPattern>(pattern)) {
    core::Type inner = core::Type::Unknown();
    if (scrutinee_type.kind == core::TypeKind::kOptional &&
        !scrutinee_type.type_args.empty()) {
      inner = scrutinee_type.type_args[0];
    }
    if (ctor->name == "Some") {
      if (ctor->args.size() != 1) {
        ReportError(pattern->loc, frontends::ErrorCode::kParamCountMismatch,
                    "Some(...) pattern expects exactly one sub-pattern, got " +
                        std::to_string(ctor->args.size()));
        return false;
      }
      AnalyzePattern(ctor->args.front(), inner, out_bindings);
      return true;
    }
    if (ctor->name == "None") {
      if (!ctor->args.empty()) {
        ReportError(pattern->loc, frontends::ErrorCode::kParamCountMismatch,
                    "None pattern takes no sub-patterns, got " +
                        std::to_string(ctor->args.size()));
        return false;
      }
      return true;
    }
    // Generic constructor �?accept and recurse with Unknown component types.
    for (const auto &a : ctor->args) {
      AnalyzePattern(a, core::Type::Unknown(), out_bindings);
    }
    return true;
  }

  return true;
}

void PloySema::AnalyzeReturnStatement(const std::shared_ptr<ReturnStatement> &ret) {
  if (ret->value) {
    core::Type ret_type = AnalyzeExpression(ret->value);
    if (current_return_type_.kind != core::TypeKind::kInvalid &&
        current_return_type_.kind != core::TypeKind::kAny &&
        current_return_type_.kind != core::TypeKind::kUnknown) {
      if (!AreTypesCompatible(ret_type, current_return_type_)) {
        ReportError(ret->loc, frontends::ErrorCode::kReturnTypeMismatch,
                    "return type mismatch: function expects '" + current_return_type_.ToString() +
                        "' but returning '" + ret_type.ToString() + "'");
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
// Structured Exception Handling (since v1.13.0)
// ============================================================================

void PloySema::AnalyzeTryStatement(const std::shared_ptr<TryStatement> &try_stmt) {
  if (!try_stmt) return;

  // Analyse the protected body first.  Any THROW inside lowers to a
  // runtime call; from sema's point of view the body is just a sequence
  // of statements that must individually type-check.
  AnalyzeBlockStatements(try_stmt->body);

  // Each CATCH clause introduces a fresh binding for the caught Error
  // handle.  We accept either the canonical built-in `Error` or a
  // user-declared subtype name; further refinement is left to future
  // work (typed-catch dispatch is tracked separately).
  for (auto &clause : try_stmt->catches) {
    if (clause.var_name.empty()) {
      ReportError(clause.loc, frontends::ErrorCode::kEmptySymbolName,
                  "CATCH requires a binding name");
      continue;
    }
    std::string type_name = "Error";
    if (auto simple = std::dynamic_pointer_cast<SimpleType>(clause.var_type)) {
      if (!simple->name.empty()) type_name = simple->name;
    }
    if (type_name != "Error") {
      ReportWarning(clause.loc, frontends::ErrorCode::kTypeMismatch,
                    "CATCH binding type '" + type_name +
                        "' is not the built-in Error; treating as Error");
    }

    PloySymbol sym;
    sym.kind = PloySymbol::Kind::kVariable;
    sym.name = clause.var_name;
    // The Error handle surface is opaque at the type-system level; we
    // mark it as Any so attribute access (`.message`, `.source_lang`,
    // `.stacktrace`) does not produce spurious diagnostics until the
    // dedicated handle type is wired through demand 2026-04-28-9's
    // typed-handle pipeline.
    sym.type = core::Type::Any();
    sym.is_mutable = false;
    sym.defined_at = clause.loc;
    DeclareSymbol(sym);

    AnalyzeBlockStatements(clause.body);
  }

  if (try_stmt->has_finally) {
    AnalyzeBlockStatements(try_stmt->finally_body);
  }
}

void PloySema::AnalyzeThrowStatement(const std::shared_ptr<ThrowStatement> &throw_stmt) {
  if (!throw_stmt) return;
  if (!throw_stmt->value) {
    ReportError(throw_stmt->loc, frontends::ErrorCode::kMissingExpression,
                "THROW requires a value (Error handle, string, or Error-shaped struct)");
    return;
  }
  // The expression must type-check; the runtime accepts anything that
  // can be coerced to an `Error` handle (string literal, existing
  // Error, or struct with a `message` field).  Accepting a wider set
  // here keeps THROW ergonomic for early users; a stricter check is
  // tracked under future work.
  (void) AnalyzeExpression(throw_stmt->value);
}

// AWAIT analysis: enforces that the expression appears inside an
// `ASYNC FUNC` body and returns the inner type of the awaited future.
// The current type system does not yet model `Future<T>` parametrically,
// so the inner type is returned as `core::Type::Any()` while the value
// flows through; future work tightens this once generics land.
core::Type PloySema::AnalyzeAwaitExpression(const std::shared_ptr<AwaitExpression> &await) {
  if (!await) return core::Type::Invalid();
  if (async_depth_ <= 0) {
    ReportError(await->loc, frontends::ErrorCode::kTypeMismatch,
                "AWAIT may only appear inside an ASYNC function body",
                "wrap the surrounding FUNC with the ASYNC keyword");
  }
  if (!await->operand) {
    ReportError(await->loc, frontends::ErrorCode::kMissingExpression,
                "AWAIT requires an operand expression");
    return core::Type::Invalid();
  }
  (void) AnalyzeExpression(await->operand);
  return core::Type::Any();
}

// Postfix `?` short-circuit unwrap of an `OPTION<T>` operand
// (since v1.19.0).  Verifies that the operand is actually an
// `OPTION<T>` value, that the use site lives inside a function
// body, and that the enclosing function returns an `OPTION<U>`
// whose `U` is assignment-compatible with `T`.  The analysis
// returns the inner `T` type (or `Unknown` if it cannot be
// resolved) so further uses participate in normal type checking.
core::Type PloySema::AnalyzeOptionUnwrapExpression(
    const std::shared_ptr<OptionUnwrapExpression> &unwrap) {
  if (!unwrap || !unwrap->operand) return core::Type::Invalid();

  core::Type operand_ty = AnalyzeExpression(unwrap->operand);

  // The operand must be an OPTION<T>.  Unknown / Any propagate without
  // a hard error so unresolved cross-language calls remain compilable
  // in permissive mode while still surfacing a strict-mode diagnostic.
  if (operand_ty.kind != core::TypeKind::kOptional) {
    if (operand_ty.kind != core::TypeKind::kUnknown &&
        operand_ty.kind != core::TypeKind::kAny &&
        operand_ty.kind != core::TypeKind::kInvalid) {
      ReportError(unwrap->loc, frontends::ErrorCode::kTypeMismatch,
                  "postfix '?' requires an OPTION<T> operand, got '" +
                      operand_ty.ToString() + "'",
                  "wrap the value with Some(...) or change the function "
                  "signature to return OPTION<T>");
      return core::Type::Invalid();
    }
    ReportStrictDiag(unwrap->loc, frontends::ErrorCode::kTypeMismatch,
                     "postfix '?' applied to value of type '" +
                         operand_ty.ToString() +
                         "' falls back to Unknown; provide a typed OPTION<T>");
    return core::Type::Unknown();
  }

  core::Type inner =
      operand_ty.type_args.empty() ? core::Type::Unknown() : operand_ty.type_args.front();

  // The enclosing function must itself return an OPTION<U>; otherwise the
  // implicit early-return cannot be synthesised.  Use of `?` at module
  // scope (no enclosing function) is rejected outright.
  if (current_return_type_.kind == core::TypeKind::kInvalid) {
    ReportError(unwrap->loc, frontends::ErrorCode::kTypeMismatch,
                "postfix '?' may only appear inside a function body",
                "move the expression into a FUNC that returns OPTION<T>");
    return inner;
  }
  if (current_return_type_.kind != core::TypeKind::kOptional) {
    ReportError(unwrap->loc, frontends::ErrorCode::kTypeMismatch,
                "postfix '?' requires the enclosing function to return "
                "OPTION<U>, but it returns '" +
                    current_return_type_.ToString() + "'",
                "change the FUNC return type to OPTION<U> or replace '?' "
                "with an explicit IF LET pattern");
    return inner;
  }
  // Best-effort compatibility check between T and U.  Unknown / Any on
  // either side simply skips the warning so partially typed code is
  // not blocked.
  if (!current_return_type_.type_args.empty() &&
      inner.kind != core::TypeKind::kUnknown &&
      inner.kind != core::TypeKind::kAny &&
      current_return_type_.type_args.front().kind != core::TypeKind::kUnknown &&
      current_return_type_.type_args.front().kind != core::TypeKind::kAny &&
      !AreTypesCompatible(inner, current_return_type_.type_args.front())) {
    ReportStrictDiag(unwrap->loc, frontends::ErrorCode::kTypeMismatch,
                     "postfix '?' inner type '" + inner.ToString() +
                         "' is not assignment-compatible with the enclosing "
                         "function's OPTION<" +
                         current_return_type_.type_args.front().ToString() +
                         "> return type");
  }

  return inner;
}

// ============================================================================
// Expression Analysis
// ============================================================================

core::Type PloySema::AnalyzeExpression(const std::shared_ptr<Expression> &expr) {
  if (!expr)
    return core::Type::Invalid();

  if (auto id = std::dynamic_pointer_cast<Identifier>(expr)) {
    auto it = symbols_.find(id->name);
    if (it != symbols_.end()) {
      return it->second.type;
    }
    ReportError(id->loc, frontends::ErrorCode::kUndefinedSymbol,
                "undefined identifier '" + id->name + "'");
    ReportStrictDiag(id->loc, frontends::ErrorCode::kTypeMismatch,
                     "unresolved identifier '" + id->name + "' falls back to Unknown");
    return core::Type::Unknown();
  }

  if (auto qid = std::dynamic_pointer_cast<QualifiedIdentifier>(expr)) {
    // Qualified identifiers refer to imported module symbols.
    ReportStrictDiag(qid->loc, frontends::ErrorCode::kTypeMismatch,
                     "qualified identifier type cannot be resolved; "
                     "defaults to Unknown �?add a LINK or IMPORT with type mapping");
    return core::Type::Unknown();
  }

  if (auto lit = std::dynamic_pointer_cast<Literal>(expr)) {
    switch (lit->kind) {
    case Literal::Kind::kInteger:
      return core::Type::Int();
    case Literal::Kind::kFloat:
      return core::Type::Float();
    case Literal::Kind::kString:
      return core::Type::String();
    case Literal::Kind::kBool:
      return core::Type::Bool();
    case Literal::Kind::kNull:
      return core::Type::Any();
    }
    return core::Type::Unknown(); // unrecognized literal kind
  }

  // Template (interpolated) string literal (since v1.17.0).  Each
  // interpolated expression must have a *formattable* type — currently
  // Int / Float / String / Bool — and the overall expression types as
  // String.  Unknown / Any / Invalid are accepted so unresolved cross-
  // language references do not block compilation.
  if (auto tmpl = std::dynamic_pointer_cast<TemplateString>(expr)) {
    for (const auto &part : tmpl->parts) {
      if (part.is_text || !part.expr) continue;
      core::Type pt = AnalyzeExpression(part.expr);
      switch (pt.kind) {
      case core::TypeKind::kInt:
      case core::TypeKind::kFloat:
      case core::TypeKind::kString:
      case core::TypeKind::kBool:
      case core::TypeKind::kAny:
      case core::TypeKind::kUnknown:
      case core::TypeKind::kInvalid:
        break;
      default:
        ReportError(part.loc, frontends::ErrorCode::kTypeMismatch,
                    "template string interpolation has non-formattable type '" +
                        pt.ToString() +
                        "'; only Int / Float / String / Bool may be interpolated");
        break;
      }
    }
    return core::Type::String();
  }

  if (auto bin = std::dynamic_pointer_cast<BinaryExpression>(expr)) {
    return AnalyzeBinaryExpression(bin);
  }

  if (auto unary = std::dynamic_pointer_cast<UnaryExpression>(expr)) {
    return AnalyzeUnaryExpression(unary);
  }

  if (auto await_expr = std::dynamic_pointer_cast<AwaitExpression>(expr)) {
    return AnalyzeAwaitExpression(await_expr);
  }

  if (auto unwrap_expr = std::dynamic_pointer_cast<OptionUnwrapExpression>(expr)) {
    return AnalyzeOptionUnwrapExpression(unwrap_expr);
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
    return core::Type::Unknown(); // Member type resolution requires full module info
  }

  if (auto index = std::dynamic_pointer_cast<IndexExpression>(expr)) {
    core::Type obj_type = AnalyzeExpression(index->object);
    AnalyzeExpression(index->index);
    if (obj_type.kind == core::TypeKind::kArray && !obj_type.type_args.empty()) {
      return obj_type.type_args[0];
    }
    return core::Type::Unknown(); // index type could not be resolved
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

  if (auto named_arg = std::dynamic_pointer_cast<NamedArgument>(expr)) {
    // Named arguments are transparent for type analysis �?the type is
    // determined by the value expression.  However we validate that the
    // name corresponds to a known parameter so typos are caught early.
    core::Type val_type = AnalyzeExpression(named_arg->value);

    // The actual name validation happens at the call-site level inside
    // AnalyzeCallExpression / AnalyzeCrossLangCall where the signature
    // is available.  Here we just return the value type.
    return val_type;
  }

  return core::Type::Unknown(); // unrecognized expression kind �?type cannot be determined
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

    // Validate named arguments against the known parameter names.
    if (sig && !sig->param_names.empty()) {
      for (const auto &arg : call->args) {
        if (auto named_arg = std::dynamic_pointer_cast<NamedArgument>(arg)) {
          bool found = false;
          for (const auto &pn : sig->param_names) {
            if (pn == named_arg->name) {
              found = true;
              break;
            }
          }
          if (!found) {
            ReportError(named_arg->loc, frontends::ErrorCode::kTypeMismatch,
                        "unknown named argument '" + named_arg->name + "' in call to '" +
                            func_name + "'");
          }
        }
      }

      // Coverage check: count positional args, then make sure every
      // required (no-default) parameter is either supplied positionally
      // or named explicitly.  Without this, `f(y: 5)` for
      // `FUNC f(x: i32, y: i32 = 0)` would silently lose the required
      // `x` and only get caught at lowering / runtime.
      size_t positional = 0;
      std::vector<std::string> named_keys;
      for (const auto &arg : call->args) {
        if (auto na = std::dynamic_pointer_cast<NamedArgument>(arg))
          named_keys.push_back(na->name);
        else
          ++positional;
      }
      for (size_t i = positional; i < sig->param_names.size(); ++i) {
        bool has_default =
            i < sig->param_has_default.size() && sig->param_has_default[i];
        bool named = false;
        for (const auto &k : named_keys) {
          if (k == sig->param_names[i]) {
            named = true;
            break;
          }
        }
        if (!has_default && !named) {
          ReportError(call->loc, frontends::ErrorCode::kParamCountMismatch,
                      "required parameter '" + sig->param_names[i] +
                          "' of '" + func_name +
                          "' is not supplied (no positional argument and no "
                          "matching named argument)");
        }
      }
    }
  }

  // If the callee is a function type AND no signature was found above,
  // fall back to validating against the function-type's parameter list.
  // (When a signature is registered, ValidateCallArgCount/Types is the
  // authoritative check and already handles default values + named args.)
  bool sig_already_checked = !func_name.empty() && LookupSignature(func_name) != nullptr;
  if (!sig_already_checked && callee_type.kind == core::TypeKind::kFunction &&
      !callee_type.type_args.empty()) {
    // type_args layout: [return_type, param_type_0, param_type_1, ...]
    size_t expected_params = callee_type.type_args.size() - 1;
    if (call->args.size() != expected_params) {
      ReportError(call->loc, frontends::ErrorCode::kParamCountMismatch,
                  "function '" + func_name + "' expects " + std::to_string(expected_params) +
                      " argument(s), but " + std::to_string(call->args.size()) +
                      " argument(s) provided");
    }

    // Check each argument type
    for (size_t i = 0; i < std::min(call->args.size(), expected_params); ++i) {
      const core::Type &expected = callee_type.type_args[i + 1];
      if (!AreTypesCompatible(arg_types[i], expected)) {
        ReportError(call->loc, frontends::ErrorCode::kTypeMismatch,
                    "type mismatch for argument " + std::to_string(i + 1) + " of function '" +
                        func_name + "': expected '" + expected.ToString() + "' but got '" +
                        arg_types[i].ToString() + "'");
      }
    }

    return callee_type.type_args[0]; // First type_arg is return type
  }

  // Recover the return type from the registered signature when available.
  if (sig_already_checked) {
    if (const FunctionSignature *sig = LookupSignature(func_name)) {
      if (sig->return_type.kind != core::TypeKind::kInvalid)
        return sig->return_type;
    }
  }

  return core::Type::Unknown(); // return type unknown �?no function type or signature found
}

core::Type PloySema::AnalyzeCrossLangCall(const std::shared_ptr<CrossLangCallExpression> &call) {
  // Validate language
  if (!IsValidLanguage(call->language)) {
    ReportError(call->loc, frontends::ErrorCode::kInvalidLanguage,
                "unknown language '" + call->language + "' in CALL");
  }
  // Stamp the resolved language version, if any.
  call->lang_version_pin = ResolveLangVersion(call->language);

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
      sig->return_type.kind != core::TypeKind::kInvalid &&
      sig->return_type.kind != core::TypeKind::kUnknown) {
    return sig->return_type;
  }

  // Check the symbol table for a function type registered by LINK.
  // If the symbol has a function type, extract the return type from it.
  // Try both the full name, the short name (e.g. "numpy::mean" -> "mean"),
  // and the module prefix (e.g. "numpy::mean" -> "numpy") to handle
  // cross-language calls targeting imported modules.
  auto sym_it = symbols_.find(call->function);
  if (sym_it == symbols_.end()) {
    auto pos = call->function.rfind("::");
    if (pos != std::string::npos) {
      // Try short name
      sym_it = symbols_.find(call->function.substr(pos + 2));
      if (sym_it == symbols_.end()) {
        // Try module prefix (handles IMPORT cpp::module_name)
        sym_it = symbols_.find(call->function.substr(0, pos));
      }
    }
  }
  if (sym_it != symbols_.end() && sym_it->second.type.kind == core::TypeKind::kFunction &&
      !sym_it->second.type.type_args.empty()) {
    return sym_it->second.type.type_args[0]; // First type_arg is return type
  }

  // If the symbol is completely unregistered (no LINK, no IMPORT), this is
  // an unconditional error regardless of strict mode �?the function simply
  // does not exist in the current compilation context.
  if (!sig && sym_it == symbols_.end()) {
    ReportError(call->loc, frontends::ErrorCode::kTypeMismatch,
                "CALL to '" + call->function + "' (language: " + call->language +
                    ") references an unregistered cross-language symbol �?"
                    "add a LINK declaration to connect it");
    return core::Type::Unknown();
  }

  // Cross-language calls whose target is registered but has no precise return
  // type �?in strict mode this is an error, in permissive mode a warning.
  ReportStrictDiag(call->loc, frontends::ErrorCode::kTypeMismatch,
                   "CALL to '" + call->function + "' (language: " + call->language +
                       ") has no known return type; defaults to Unknown �?"
                       "add MAP_TYPE to enable type checking");
  return core::Type::Unknown();
}

core::Type PloySema::AnalyzeNewExpression(const std::shared_ptr<NewExpression> &new_expr) {
  // Validate language
  if (!IsValidLanguage(new_expr->language)) {
    ReportError(new_expr->loc, frontends::ErrorCode::kInvalidLanguage,
                "unknown language '" + new_expr->language + "' in NEW");
  }
  //: stamp the resolved language version, if any.
  new_expr->lang_version_pin = ResolveLangVersion(new_expr->language);

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

  // Demand 2026-04-28-9: when a CLASS schema is registered for this
  // language+path, validate the constructor against it and return a
  // statically-typed HANDLE<lang::T>.  The schema-aware path runs
  // BEFORE the legacy LINK-based lookup so that explicit class blocks
  // always win over LINK-derived signatures.
  const std::string schema_key = new_expr->language + "::" + new_expr->class_name;
  if (const ForeignClassSchema *schema = LookupClassSchema(schema_key)) {
    if (schema->has_constructor) {
      ValidateCallArgCount(new_expr->loc, schema_key + " constructor",
                           new_expr->args.size(), &schema->constructor_sig);
      ValidateCallArgTypes(new_expr->loc, schema_key + " constructor", arg_types,
                           &schema->constructor_sig);
    }
    return core::Type::Class(new_expr->class_name, new_expr->language);
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
    ValidateCallArgTypes(new_expr->loc, new_expr->class_name + " constructor", arg_types, sig);
  }

  // NEW returns an opaque object handle.  If the class is known from a LINK
  // declaration, return a struct-typed reference; otherwise Unknown
  // because in cross-language contexts the class hierarchy is not available.
  auto sym_it = symbols_.find(new_expr->class_name);
  if (sym_it != symbols_.end() && sym_it->second.type.kind != core::TypeKind::kAny &&
      sym_it->second.type.kind != core::TypeKind::kUnknown &&
      sym_it->second.type.kind != core::TypeKind::kInvalid) {
    return sym_it->second.type;
  }
  return core::Type::Unknown(); // class type not resolved �?add LINK declaration
}

core::Type PloySema::AnalyzeMethodCallExpression(
    const std::shared_ptr<MethodCallExpression> &method_call) {
  // Validate language
  if (!IsValidLanguage(method_call->language)) {
    ReportError(method_call->loc, frontends::ErrorCode::kInvalidLanguage,
                "unknown language '" + method_call->language + "' in METHOD");
  }
  // Stamp the resolved language version, if any.
  method_call->lang_version_pin = ResolveLangVersion(method_call->language);

  // Validate method name is non-empty
  if (method_call->method_name.empty()) {
    ReportError(method_call->loc, frontends::ErrorCode::kEmptySymbolName,
                "method name is empty in METHOD");
  }

  // Analyze the receiver object
  core::Type receiver_type = core::Type::Unknown();
  if (method_call->object) {
    receiver_type = AnalyzeExpression(method_call->object);
  } else {
    ReportError(method_call->loc, frontends::ErrorCode::kMissingExpression,
                "METHOD requires an object expression");
  }

  // Analyze arguments and collect their types
  std::vector<core::Type> arg_types;
  for (const auto &arg : method_call->args) {
    arg_types.push_back(AnalyzeExpression(arg));
  }

  // Demand 2026-04-28-9: when the receiver is a statically-typed
  // HANDLE<lang::T>, prefer the registered CLASS schema over any
  // legacy LookupSignature() result.  Unknown methods on a typed
  // handle become a *warning* (not an error) so existing dynamic
  // dispatch code keeps compiling without retrofitting every method.
  if (receiver_type.kind == core::TypeKind::kClass && !receiver_type.name.empty()) {
    const std::string schema_key = receiver_type.language + "::" + receiver_type.name;
    if (const ForeignClassSchema *schema = LookupClassSchema(schema_key)) {
      auto m_it = schema->methods.find(method_call->method_name);
      if (m_it != schema->methods.end()) {
        const FunctionSignature &msig = m_it->second;
        ValidateCallArgCount(method_call->loc, method_call->method_name,
                             method_call->args.size(), &msig);
        ValidateCallArgTypes(method_call->loc, method_call->method_name, arg_types, &msig);
        return msig.return_type;
      }
      ReportWarning(method_call->loc, frontends::ErrorCode::kSignatureMissing,
                    "unknown method '" + method_call->method_name + "' on HANDLE<" +
                        schema_key + ">; falling back to dynamic dispatch");
      return core::Type::Unknown();
    }
  }

  // Check method signature if known (e.g. via LINK declaration)
  // Try qualified lookup first: ClassName::method_name
  std::string qualified_method = method_call->method_name;
  if (receiver_type.kind != core::TypeKind::kAny &&
      receiver_type.kind != core::TypeKind::kInvalid && !receiver_type.name.empty()) {
    qualified_method = receiver_type.name + "::" + method_call->method_name;
  }
  const FunctionSignature *sig = LookupSignature(qualified_method);
  if (!sig) {
    sig = LookupSignature(method_call->method_name);
  }
  if (sig) {
    ValidateCallArgCount(method_call->loc, method_call->method_name, method_call->args.size(), sig);
    ValidateCallArgTypes(method_call->loc, method_call->method_name, arg_types, sig);
  } else {
    // No method signature registered �?report so the user knows
    // parameter validation is skipped for this METHOD call.
    ReportStrictDiag(method_call->loc, frontends::ErrorCode::kSignatureMissing,
                     "METHOD '" + method_call->method_name +
                         "' (language: " + method_call->language +
                         ") has no signature registered; "
                         "parameter and return type validation skipped");
  }

  // Determine return type from signature
  core::Type ret_type = core::Type::Unknown();
  if (sig && sig->return_type.kind != core::TypeKind::kAny &&
      sig->return_type.kind != core::TypeKind::kUnknown &&
      sig->return_type.kind != core::TypeKind::kInvalid) {
    return sig->return_type;
  }

  // Method calls return Unknown since we cannot statically resolve the return type
  return core::Type::Unknown();
}

core::Type PloySema::AnalyzeGetAttrExpression(const std::shared_ptr<GetAttrExpression> &get_attr) {
  // Validate language
  if (!IsValidLanguage(get_attr->language)) {
    ReportError(get_attr->loc, frontends::ErrorCode::kInvalidLanguage,
                "unknown language '" + get_attr->language + "' in GET");
  }
  // Stamp the resolved language version, if any.
  get_attr->lang_version_pin = ResolveLangVersion(get_attr->language);

  // Validate attribute name is non-empty
  if (get_attr->attr_name.empty()) {
    ReportError(get_attr->loc, frontends::ErrorCode::kEmptySymbolName,
                "attribute name is empty in GET");
  }

  // Analyze the object expression
  core::Type obj_type = core::Type::Unknown();
  if (get_attr->object) {
    obj_type = AnalyzeExpression(get_attr->object);
  } else {
    ReportError(get_attr->loc, frontends::ErrorCode::kMissingExpression,
                "GET requires an object expression");
  }

  // Try to resolve the attribute type from struct definitions or known signatures
  if (obj_type.kind == core::TypeKind::kStruct && !obj_type.name.empty()) {
    auto struct_it = struct_defs_.find(obj_type.name);
    if (struct_it != struct_defs_.end()) {
      for (const auto &[field_name, field_type] : struct_it->second) {
        if (field_name == get_attr->attr_name) {
          return field_type;
        }
      }
      ReportError(get_attr->loc, frontends::ErrorCode::kUnknownField,
                  "struct '" + obj_type.name + "' has no field '" + get_attr->attr_name + "'");
    }
  }

  // Demand 2026-04-28-9: typed-handle attribute lookup via CLASS schema.
  // Unknown attributes on a typed handle warn rather than error so existing
  // dynamic-attribute code keeps compiling.
  if (obj_type.kind == core::TypeKind::kClass && !obj_type.name.empty()) {
    const std::string schema_key = obj_type.language + "::" + obj_type.name;
    if (const ForeignClassSchema *schema = LookupClassSchema(schema_key)) {
      auto a_it = schema->attributes.find(get_attr->attr_name);
      if (a_it != schema->attributes.end()) {
        return a_it->second;
      }
      ReportWarning(get_attr->loc, frontends::ErrorCode::kUnknownField,
                    "HANDLE<" + schema_key + "> has no declared ATTR '" +
                        get_attr->attr_name + "'; falling back to dynamic lookup");
      return core::Type::Unknown();
    }
  }

  // Try getter signature lookup: qualified ClassName::__getattr__::attr_name
  // first, then unqualified __getattr__::attr_name.
  std::string getter_name;
  const FunctionSignature *sig = nullptr;
  if (obj_type.kind != core::TypeKind::kAny && obj_type.kind != core::TypeKind::kUnknown &&
      obj_type.kind != core::TypeKind::kInvalid && !obj_type.name.empty()) {
    getter_name = obj_type.name + "::__getattr__::" + get_attr->attr_name;
    sig = LookupSignature(getter_name);
  }
  if (!sig) {
    getter_name = "__getattr__::" + get_attr->attr_name;
    sig = LookupSignature(getter_name);
  }
  if (sig && sig->return_type.kind != core::TypeKind::kAny &&
      sig->return_type.kind != core::TypeKind::kUnknown &&
      sig->return_type.kind != core::TypeKind::kInvalid) {
    // Validate ABI for the getter call (receiver is the only argument)
    ValidateObjCallABI(get_attr->loc, getter_name, get_attr->language, {obj_type},
                       sig->return_type);
    return sig->return_type;
  }

  // Attribute access returns Unknown since we cannot statically resolve the attribute type
  return core::Type::Unknown();
}

core::Type PloySema::AnalyzeSetAttrExpression(const std::shared_ptr<SetAttrExpression> &set_attr) {
  // Validate language
  if (!IsValidLanguage(set_attr->language)) {
    ReportError(set_attr->loc, frontends::ErrorCode::kInvalidLanguage,
                "unknown language '" + set_attr->language + "' in SET");
  }
  // Stamp the resolved language version, if any.
  set_attr->lang_version_pin = ResolveLangVersion(set_attr->language);

  // Validate attribute name is non-empty
  if (set_attr->attr_name.empty()) {
    ReportError(set_attr->loc, frontends::ErrorCode::kEmptySymbolName,
                "attribute name is empty in SET");
  }

  // Analyze the object expression
  core::Type obj_type = core::Type::Unknown();
  if (set_attr->object) {
    obj_type = AnalyzeExpression(set_attr->object);
  } else {
    ReportError(set_attr->loc, frontends::ErrorCode::kMissingExpression,
                "SET requires an object expression");
  }

  // Analyze the value expression
  core::Type value_type = core::Type::Unknown();
  if (set_attr->value) {
    value_type = AnalyzeExpression(set_attr->value);
  } else {
    ReportError(set_attr->loc, frontends::ErrorCode::kMissingExpression,
                "SET requires a value expression");
  }

  // Demand 2026-04-28-9: typed-handle attribute write via CLASS schema.
  // When both the receiver is a HANDLE<lang::T> with a registered schema
  // and the attribute name is declared, validate that the value is
  // assignment-compatible with the declared attribute type.  Unknown
  // attributes downgrade to a warning to preserve dynamic-attribute
  // ergonomics.
  if (obj_type.kind == core::TypeKind::kClass && !obj_type.name.empty()) {
    const std::string schema_key = obj_type.language + "::" + obj_type.name;
    if (const ForeignClassSchema *schema = LookupClassSchema(schema_key)) {
      auto a_it = schema->attributes.find(set_attr->attr_name);
      if (a_it != schema->attributes.end()) {
        if (!AreTypesCompatible(value_type, a_it->second)) {
          ReportError(set_attr->loc, frontends::ErrorCode::kTypeMismatch,
                      "cannot assign value of type '" + value_type.ToString() +
                          "' to ATTR '" + set_attr->attr_name + "' of HANDLE<" +
                          schema_key + "> declared as '" + a_it->second.ToString() + "'");
        }
        return a_it->second;
      }
      ReportWarning(set_attr->loc, frontends::ErrorCode::kUnknownField,
                    "HANDLE<" + schema_key + "> has no declared ATTR '" +
                        set_attr->attr_name + "'; falling back to dynamic assignment");
    }
  }

  // SET returns the assigned value type (Unknown since we cannot know the attribute type)
  return core::Type::Unknown();
}

void PloySema::AnalyzeWithStatement(const std::shared_ptr<WithStatement> &with_stmt) {
  // Validate language
  if (!IsValidLanguage(with_stmt->language)) {
    ReportError(with_stmt->loc, frontends::ErrorCode::kInvalidLanguage,
                "unknown language '" + with_stmt->language + "' in WITH");
  }
  // Stamp the resolved language version, if any.
  with_stmt->lang_version_pin = ResolveLangVersion(with_stmt->language);

  // Validate variable name
  if (with_stmt->var_name.empty()) {
    ReportError(with_stmt->loc, frontends::ErrorCode::kEmptySymbolName,
                "WITH requires a variable name after AS");
  }

  // Analyze the resource expression and resolve its type
  core::Type resource_type = core::Type::Unknown();
  if (with_stmt->resource_expr) {
    resource_type = AnalyzeExpression(with_stmt->resource_expr);
  } else {
    ReportError(with_stmt->loc, frontends::ErrorCode::kMissingExpression,
                "WITH requires a resource expression");
  }

  // Validate that the resource type supports the context manager protocol
  // (__enter__ and __exit__ signatures must be registered)
  ValidateContextManagerProtocol(with_stmt->loc, with_stmt->language, resource_type);

  // Declare the bound variable in scope with the resolved type from
  // __enter__ if available, instead of defaulting to opaque Unknown.
  core::Type bound_type = core::Type::Unknown();
  std::string type_name =
      resource_type.name.empty() ? resource_type.ToString() : resource_type.name;
  std::string enter_name = type_name + "::__enter__";
  const FunctionSignature *enter_sig = LookupSignature(enter_name);
  if (!enter_sig) {
    enter_sig = LookupSignature("__enter__");
  }
  if (enter_sig && enter_sig->return_type.kind != core::TypeKind::kAny &&
      enter_sig->return_type.kind != core::TypeKind::kInvalid) {
    bound_type = enter_sig->return_type;
  }

  PloySymbol sym;
  sym.kind = PloySymbol::Kind::kVariable;
  sym.name = with_stmt->var_name;
  sym.type = bound_type;
  sym.is_mutable = false;
  sym.defined_at = with_stmt->loc;
  DeclareSymbol(sym);

  // Validate ABI compatibility for __enter__ and __exit__ calls.
  // __enter__ takes (self) and returns the bound value.
  if (enter_sig) {
    ValidateObjCallABI(with_stmt->loc, enter_name, with_stmt->language, {resource_type},
                       bound_type);
  }
  // __exit__ takes (self, exc_type, exc_val, exc_tb) and returns void/bool.
  std::string exit_name = type_name + "::__exit__";
  const FunctionSignature *exit_sig = LookupSignature(exit_name);
  if (!exit_sig) {
    exit_sig = LookupSignature("__exit__");
  }
  if (exit_sig) {
    // Validate __exit__ return type �?should be void or bool (suppress flag)
    if (exit_sig->return_type.kind != core::TypeKind::kAny &&
        exit_sig->return_type.kind != core::TypeKind::kInvalid &&
        exit_sig->return_type.kind != core::TypeKind::kVoid &&
        exit_sig->return_type.kind != core::TypeKind::kBool) {
      ReportWarning(with_stmt->loc, frontends::ErrorCode::kReturnTypeMismatch,
                    "__exit__ for '" + type_name + "' returns '" +
                        exit_sig->return_type.ToString() +
                        "'; expected void or bool (exception suppression flag)");
    }
  }

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
  if (bin->op == "==" || bin->op == "!=" || bin->op == "<" || bin->op == ">" || bin->op == "<=" ||
      bin->op == ">=") {
    return core::Type::Bool();
  }

  // Logical operators return bool
  if (bin->op == "&&" || bin->op == "||") {
    return core::Type::Bool();
  }

  // Arithmetic operators
  if (bin->op == "+" || bin->op == "-" || bin->op == "*" || bin->op == "/" || bin->op == "%") {
    // Numeric promotion
    if (left.kind == core::TypeKind::kFloat || right.kind == core::TypeKind::kFloat) {
      return core::Type::Float();
    }
    if (left.kind == core::TypeKind::kInt && right.kind == core::TypeKind::kInt) {
      return core::Type::Int();
    }
    // String concatenation
    if (bin->op == "+" &&
        (left.kind == core::TypeKind::kString || right.kind == core::TypeKind::kString)) {
      return core::Type::String();
    }
    return core::Type::Unknown(); // arithmetic result type could not be resolved
  }

  return core::Type::Unknown(); // unrecognized binary operator
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
  if (!type_node)
    return core::Type::Invalid();

  if (auto st = std::dynamic_pointer_cast<SimpleType>(type_node)) {
    // 0. Generic type parameter currently in scope (since v1.15.0).
    //    Type-erased to `Any` for the MVP lowering path; full
    //    monomorphisation per concrete instantiation is tracked in the
    //    realization document as follow-up work.
    if (active_type_params_.count(st->name)) {
      return core::Type::Any();
    }
    // 1. Type-alias lookup takes precedence over every built-in below.
    //    Aliases for built-in primitives (e.g. `TYPE Length = i64;`) thus
    //    correctly inherit width information from the aliased type rather
    //    than re-folding through the primitive branch.
    {
      auto it = type_aliases_.find(st->name);
      if (it != type_aliases_.end()) {
        return it->second;
      }
    }
    // 2. Width-aware integer keywords (canonicalised by the lexer to
    //    upper-case).  `INT` and `int` are kept as legacy aliases of
    //    `i64` per the language spec §2.9 (demand 2026-04-28-7).
    if (st->name == "I8")    return core::Type::Int(8,  true);
    if (st->name == "I16")   return core::Type::Int(16, true);
    if (st->name == "I32")   return core::Type::Int(32, true);
    if (st->name == "I64")   return core::Type::Int(64, true);
    if (st->name == "U8")    return core::Type::Int(8,  false);
    if (st->name == "U16")   return core::Type::Int(16, false);
    if (st->name == "U32")   return core::Type::Int(32, false);
    if (st->name == "U64")   return core::Type::Int(64, false);
    // ISIZE / USIZE follow the host pointer width; we model them as 64-bit
    // since every supported host (x86_64, arm64, wasm64) uses a 64-bit
    // address space.  The IR emitters re-stamp them with the actual
    // pointer size when the target is 32-bit.
    if (st->name == "ISIZE") return core::Type::Int(64, true);
    if (st->name == "USIZE") return core::Type::Int(64, false);
    if (st->name == "INT" || st->name == "int") {
      // INT is a legacy alias of i64 �?see language_spec §2.9.
      return core::Type::Int(64, true);
    }
    // 3. Width-aware floating-point keywords.
    if (st->name == "F32")   return core::Type::Float(32);
    if (st->name == "F64")   return core::Type::Float(64);
    if (st->name == "FLOAT" || st->name == "float") {
      // FLOAT is a legacy alias of f64 �?see language_spec §2.9.
      return core::Type::Float(64);
    }
    if (st->name == "BOOL" || st->name == "bool")
      return core::Type::Bool();
    if (st->name == "STRING" || st->name == "str" || st->name == "string")
      return core::Type::String();
    if (st->name == "VOID" || st->name == "void")
      return core::Type::Void();
    if (st->name == "ptr" || st->name == "PTR" || st->name == "pointer")
      return core::Type::Any(); // opaque pointer
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
    // User-defined generic struct instantiation (since v1.15.0): resolve
    // every type argument so any unknown bound or out-of-scope name is
    // surfaced, then collapse to the underlying struct identity.  Full
    // monomorphisation is recorded as follow-up work.
    if (struct_defs_.count(pt->name)) {
      for (const auto &arg : pt->type_args) {
        (void)ResolveType(arg);
      }
      return core::Type::Struct(pt->name);
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

  // HANDLE<lang::class_path> �?the demand-9 statically-typed cross-language
  // object handle.  The kClass type carries both the class path (as `name`)
  // and the originating language so AreTypesCompatible can reject any
  // implicit conversion across languages.
  if (auto ht = std::dynamic_pointer_cast<HandleType>(type_node)) {
    return core::Type::Class(ht->class_path, ht->language);
  }

  return core::Type::Invalid();
}

bool PloySema::IsValidLanguage(const std::string &lang) const {
  return lang == "cpp" || lang == "python" || lang == "rust" || lang == "c" || lang == "ploy" ||
         lang == "java" || lang == "dotnet" || lang == "csharp" || lang == "javascript" ||
         lang == "js" || lang == "typescript" || lang == "ts" || lang == "ruby" || lang == "rb" ||
         lang == "go" || lang == "golang";
}

bool PloySema::AreTypesCompatible(const core::Type &from, const core::Type &to) const {
  if (from.kind == core::TypeKind::kAny || to.kind == core::TypeKind::kAny) {
    return true;
  }
  // Unknown is treated as compatible during sema; boundary checking happens in lowering.
  if (from.kind == core::TypeKind::kUnknown || to.kind == core::TypeKind::kUnknown) {
    return true;
  }
  // Cross-language object handles are statically distinct (demand
  // 2026-04-28-9): HANDLE<a::T> never silently converts to HANDLE<b::U>.
  // Both the qualified class path (carried in `name`) and the originating
  // language must match; explicit conversion has to go through CONVERT +
  // MAP_FUNC, which produces a fresh kClass value with the target tag.
  if (from.kind == core::TypeKind::kClass && to.kind == core::TypeKind::kClass) {
    return from.name == to.name && from.language == to.language;
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

  // Validate annotations, type parameter bounds, and bring each parameter
  // name into scope before resolving field types so a SimpleType named
  // `T` inside a field annotation resolves to `Any` (since v1.15.0 /
  // v1.16.0).
  ValidateAttributes(struct_decl->attributes);
  ValidateTypeParamBounds(struct_decl->type_params);
  std::vector<std::string> tp_added;
  tp_added.reserve(struct_decl->type_params.size());
  for (const auto &tp : struct_decl->type_params) {
    if (active_type_params_.insert(tp.name).second) {
      tp_added.push_back(tp.name);
    }
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
      Report(struct_decl->loc,
             "duplicate field '" + field.name + "' in struct '" + struct_decl->name + "'");
      continue;
    }
    core::Type field_type = field.type ? ResolveType(field.type) : core::Type::Unknown();
    if (!field.type) {
      ReportStrictDiag(struct_decl->loc, frontends::ErrorCode::kTypeMismatch,
                       "struct field '" + field.name +
                           "' has no type annotation; "
                           "defaults to Unknown");
    }
    resolved_fields.emplace_back(field.name, field_type);
  }

  struct_defs_[struct_decl->name] = resolved_fields;

  // Register the struct as a type symbol
  PloySymbol sym;
  sym.kind = PloySymbol::Kind::kVariable; // Struct types act as type symbols
  sym.name = struct_decl->name;
  sym.type = core::Type::Struct(struct_decl->name);
  sym.visibility = struct_decl->visibility;
  sym.visibility_explicit = struct_decl->visibility_explicit;
  sym.defined_at = struct_decl->loc;
  DeclareSymbol(sym);

  // Pop the generic type-parameter names brought into scope above.
  for (const auto &name : tp_added) {
    active_type_params_.erase(name);
  }
}

// Validates each declared bound name against the built-in trait registry
// and reports unknown bounds as a `kTypeMismatch` diagnostic
// (since v1.15.0).
void PloySema::ValidateTypeParamBounds(const std::vector<FuncDecl::TypeParam> &params) {
  static const std::unordered_set<std::string> kBuiltInBounds = {
      "Comparable", "Hashable", "Numeric", "Iterable", "Display"};
  for (const auto &tp : params) {
    for (const auto &bound : tp.bounds) {
      if (!kBuiltInBounds.count(bound)) {
        ReportError(tp.loc, frontends::ErrorCode::kTypeMismatch,
                    "unknown trait bound '" + bound + "' on type parameter '" +
                        tp.name +
                        "'; built-in bounds are Comparable, Hashable, "
                        "Numeric, Iterable, Display");
      }
    }
  }
}

// Validates `@name(args)` annotations against the built-in attribute
// catalog (since v1.16.0).  Unknown annotations produce a warning;
// recognised ones are accepted as inert metadata at this stage.
void PloySema::ValidateAttributes(const std::vector<Attribute> &attrs) {
  static const std::unordered_set<std::string> kBuiltInAttrs = {
      "inline",     "noinline",   "always_inline", "hot",       "cold",
      "profile",    "no_profile", "deprecated",    "link_name", "target"};
  for (const auto &attr : attrs) {
    if (!kBuiltInAttrs.count(attr.name)) {
      diagnostics_.ReportWarning(
          attr.loc, frontends::ErrorCode::kGenericWarning,
          "unknown attribute '@" + attr.name +
              "'; recognised attributes are @inline, @noinline, "
              "@always_inline, @hot, @cold, @profile, @no_profile, "
              "@deprecated, @link_name, @target");
    }
  }
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
  core::Type ret_type =
      map_func->return_type ? ResolveType(map_func->return_type) : core::Type::Unknown();

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
    core::Type pt = param.type ? ResolveType(param.type) : core::Type::Unknown();
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
  core::Type target = conv->target_type ? ResolveType(conv->target_type) : core::Type::Unknown();
  return target;
}

core::Type PloySema::AnalyzeListLiteral(const std::shared_ptr<ListLiteral> &list) {
  core::Type elem_type = core::Type::Unknown();
  for (const auto &elem : list->elements) {
    core::Type t = AnalyzeExpression(elem);
    if (elem_type.kind == core::TypeKind::kUnknown) {
      elem_type = t;
    }
    // All elements should have compatible types (allow coercion)
  }
  // If still Unknown (empty list), use Unknown element type
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
  core::Type key_type = core::Type::Unknown();
  core::Type value_type = core::Type::Unknown();
  for (const auto &entry : dict->entries) {
    core::Type kt = AnalyzeExpression(entry.key);
    core::Type vt = AnalyzeExpression(entry.value);
    if (key_type.kind == core::TypeKind::kUnknown)
      key_type = kt;
    if (value_type.kind == core::TypeKind::kUnknown)
      value_type = vt;
  }
  return core::Type::GenericInstance("dict", {key_type, value_type});
}

core::Type PloySema::AnalyzeStructLiteral(const std::shared_ptr<StructLiteral> &struct_lit) {
  // Verify the struct type exists
  auto it = struct_defs_.find(struct_lit->struct_name);
  if (it == struct_defs_.end()) {
    Report(struct_lit->loc, "unknown struct type '" + struct_lit->struct_name + "'");
    return core::Type::Unknown();
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
          Report(struct_lit->loc, "field '" + field.name + "' type mismatch in struct literal");
        }
        break;
      }
    }
    if (!found) {
      Report(struct_lit->loc,
             "unknown field '" + field.name + "' in struct '" + struct_lit->struct_name + "'");
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

void PloySema::ReportStrictDiag(const core::SourceLoc &loc, frontends::ErrorCode code,
                                const std::string &message) {
  if (strict_mode_) {
    diagnostics_.ReportError(loc, code, message);
  } else {
    diagnostics_.ReportWarning(loc, code, message);
  }
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

void PloySema::InjectForeignSignatures(
    const std::unordered_map<std::string, FunctionSignature> &foreign_sigs) {
  for (const auto &[name, sig] : foreign_sigs) {
    auto it = known_signatures_.find(name);
    if (it == known_signatures_.end()) {
      // No existing signature �?insert the foreign one directly.
      known_signatures_[name] = sig;
    } else if (sig.param_count_known && !it->second.param_count_known) {
      // The foreign signature is more complete (has real parameter count
      // and types from source inspection) while the existing one was
      // registered from a LINK declaration without full info.  Upgrade
      // the existing signature with the foreign details, but keep any
      // LINK-specific metadata (ABI, defined_at) that was already set.
      auto saved_abi = it->second.abi;
      auto saved_loc = it->second.defined_at;
      auto saved_lang = it->second.language;
      it->second = sig;
      if (saved_abi)
        it->second.abi = saved_abi;
      if (!saved_lang.empty())
        it->second.language = saved_lang;
      if (saved_loc.line != 0)
        it->second.defined_at = saved_loc;
    }
  }
}

const FunctionSignature *PloySema::LookupSignature(const std::string &qualified_name) const {
  auto it = known_signatures_.find(qualified_name);
  if (it != known_signatures_.end()) {
    return &it->second;
  }
  return nullptr;
}

void PloySema::ValidateCallArgCount(const core::SourceLoc &call_loc, const std::string &func_name,
                                    size_t actual_args, const FunctionSignature *sig) {
  if (!sig || !sig->param_count_known)
    return;

  // Compute the minimum required argument count: total parameters minus
  // the number of trailing parameters that carry a default value.
  size_t required = sig->param_count;
  for (auto it = sig->param_has_default.rbegin();
       it != sig->param_has_default.rend(); ++it) {
    if (*it)
      --required;
    else
      break;
  }

  if (actual_args < required || actual_args > sig->param_count) {
    std::string range = (required == sig->param_count)
                            ? std::to_string(sig->param_count)
                            : std::to_string(required) + ".." +
                                  std::to_string(sig->param_count);
    std::string msg = "function '" + func_name + "' expects " + range +
                      " argument(s), but " + std::to_string(actual_args) +
                      " argument(s) provided";
    ReportErrorWithTraceback(call_loc, frontends::ErrorCode::kParamCountMismatch, msg,
                             sig->defined_at,
                             "'" + func_name + "' declared here with " +
                                 std::to_string(sig->param_count) + " parameter(s)");
  }
}

void PloySema::ValidateCallArgTypes(const core::SourceLoc &call_loc, const std::string &func_name,
                                    const std::vector<core::Type> &actual_types,
                                    const FunctionSignature *sig) {
  if (!sig || !sig->param_count_known)
    return;
  if (sig->param_types.empty())
    return;

  // When the signature comes from a cross-language LINK declaration with
  // MAP_TYPE entries (indicated by a non-null ABI descriptor), the parameter
  // types stored in the signature are the foreign function's native types.
  // Ploy call-site arguments use Ploy-native types which are intentionally
  // different �?the MAP_TYPE marshalling code bridges the gap at runtime.
  // Therefore skip strict type checking for these signatures.
  if (sig->abi)
    return;

  size_t check_count = std::min(actual_types.size(), sig->param_types.size());
  for (size_t i = 0; i < check_count; ++i) {
    if (!AreTypesCompatible(actual_types[i], sig->param_types[i])) {
      std::string msg = "type mismatch for argument " + std::to_string(i + 1) + " of function '" +
                        func_name + "': expected '" + sig->param_types[i].ToString() +
                        "' but got '" + actual_types[i].ToString() + "'";
      ReportErrorWithTraceback(call_loc, frontends::ErrorCode::kTypeMismatch, msg, sig->defined_at,
                               "parameter " + std::to_string(i + 1) + " declared here");
    }
  }
}

// ============================================================================
// Return Type Validation
// ============================================================================

void PloySema::ValidateReturnType(const core::SourceLoc &call_loc, const std::string &func_name,
                                  const core::Type &expected_type, const FunctionSignature *sig) {
  if (!sig)
    return;
  if (sig->return_type.kind == core::TypeKind::kAny ||
      sig->return_type.kind == core::TypeKind::kInvalid)
    return;
  if (expected_type.kind == core::TypeKind::kAny || expected_type.kind == core::TypeKind::kInvalid)
    return;

  if (!AreTypesCompatible(sig->return_type, expected_type)) {
    std::string msg = "return type mismatch: function '" + func_name + "' returns '" +
                      sig->return_type.ToString() + "' but context expects '" +
                      expected_type.ToString() + "'";
    ReportErrorWithTraceback(call_loc, frontends::ErrorCode::kTypeMismatch, msg, sig->defined_at,
                             "'" + func_name + "' declared here");
  }
}

// ============================================================================
// ABI Signature Construction and Validation
// ============================================================================

std::string ABISignature::ValidateCompatibility(const ABISignature &other) const {
  // Check parameter count
  if (params.size() != other.params.size()) {
    return "parameter count mismatch: target '" + function_name + "' has " +
           std::to_string(params.size()) + " parameter(s) but source '" + other.function_name +
           "' has " + std::to_string(other.params.size());
  }

  // Check each parameter for ABI compatibility
  for (size_t i = 0; i < params.size(); ++i) {
    const auto &tp = params[i];
    const auto &sp = other.params[i];

    // Size mismatch is a hard error �?the calling convention will misalign
    if (tp.size_bytes != 0 && sp.size_bytes != 0 && tp.size_bytes != sp.size_bytes) {
      return "parameter " + std::to_string(i + 1) + " size mismatch: " + "target expects " +
             std::to_string(tp.size_bytes) + " bytes but source provides " +
             std::to_string(sp.size_bytes) + " bytes";
    }

    // Pointer vs value mismatch
    if (tp.is_pointer != sp.is_pointer) {
      return "parameter " + std::to_string(i + 1) + " passing convention mismatch: " +
             (tp.is_pointer ? "target expects pointer" : "target expects value") + " but " +
             (sp.is_pointer ? "source provides pointer" : "source provides value");
    }
  }

  // Check return type compatibility
  if (return_desc.size_bytes != 0 && other.return_desc.size_bytes != 0 &&
      return_desc.size_bytes != other.return_desc.size_bytes) {
    return "return type size mismatch: target returns " + std::to_string(return_desc.size_bytes) +
           " bytes but source returns " + std::to_string(other.return_desc.size_bytes) + " bytes";
  }

  return ""; // Compatible
}

std::string PloySema::CallingConventionForLanguage(const std::string &language) {
  if (language == "cpp" || language == "c" || language == "rust") {
#ifdef _WIN32
    return "win64";
#else
    return "sysv";
#endif
  }
  if (language == "python")
    return "python_c";
  if (language == "java")
    return "jni";
  if (language == "dotnet" || language == "csharp")
    return "dotnet_pinvoke";
  if (language == "ploy") {
#ifdef _WIN32
    return "win64";
#else
    return "sysv";
#endif
  }
  return "unknown";
}

ABIParamDesc PloySema::TypeToABIParam(const core::Type &type) const {
  ABIParamDesc desc;
  desc.semantic_type = type;

  switch (type.kind) {
  case core::TypeKind::kBool:
    desc.abi_type_name = "i1";
    desc.size_bytes = 1;
    desc.alignment = 1;
    break;
  case core::TypeKind::kInt:
    if (type.bit_width > 0) {
      desc.abi_type_name = (type.is_signed ? "i" : "u") + std::to_string(type.bit_width);
      desc.size_bytes = static_cast<size_t>(type.bit_width / 8);
    } else {
      desc.abi_type_name = "i64";
      desc.size_bytes = 8;
    }
    desc.alignment = desc.size_bytes;
    break;
  case core::TypeKind::kFloat:
    if (type.bit_width == 32) {
      desc.abi_type_name = "f32";
      desc.size_bytes = 4;
    } else {
      desc.abi_type_name = "f64";
      desc.size_bytes = 8;
    }
    desc.alignment = desc.size_bytes;
    break;
  case core::TypeKind::kString:
    desc.abi_type_name = "ptr";
    desc.size_bytes = 8;
    desc.alignment = 8;
    desc.is_pointer = true;
    break;
  case core::TypeKind::kPointer:
  case core::TypeKind::kReference:
  case core::TypeKind::kClass:
  case core::TypeKind::kArray:
  case core::TypeKind::kSlice:
    desc.abi_type_name = "ptr";
    desc.size_bytes = 8;
    desc.alignment = 8;
    desc.is_pointer = true;
    break;
  case core::TypeKind::kStruct:
    desc.abi_type_name = "struct";
    desc.is_by_value = true;
    // Size unknown without struct layout �?leave as 0
    break;
  case core::TypeKind::kVoid:
    desc.abi_type_name = "void";
    desc.size_bytes = 0;
    desc.alignment = 0;
    break;
  default:
    desc.abi_type_name = "opaque";
    desc.size_bytes = 8;
    desc.alignment = 8;
    break;
  }
  return desc;
}

std::shared_ptr<ABISignature> PloySema::BuildABISignature(const FunctionSignature &sig,
                                                          const std::string &language) const {
  auto abi = std::make_shared<ABISignature>();
  abi->function_name = sig.name;
  abi->language = language;
  abi->calling_convention = CallingConventionForLanguage(language);
  abi->defined_at = sig.defined_at;

  // Build parameter descriptors
  bool all_resolved = true;
  for (const auto &pt : sig.param_types) {
    ABIParamDesc pd = TypeToABIParam(pt);
    if (pt.kind == core::TypeKind::kAny || pt.kind == core::TypeKind::kInvalid) {
      all_resolved = false;
    }
    abi->params.push_back(pd);
  }

  // Build return descriptor
  abi->return_desc = TypeToABIParam(sig.return_type);
  if (sig.return_type.kind == core::TypeKind::kAny ||
      sig.return_type.kind == core::TypeKind::kInvalid) {
    all_resolved = false;
  }

  abi->is_complete = all_resolved;
  return abi;
}

void PloySema::ValidateObjCallABI(const core::SourceLoc &loc, const std::string &callee_name,
                                  const std::string &language,
                                  const std::vector<core::Type> &arg_types,
                                  const core::Type &return_type) {
  FunctionSignature sig;
  sig.name = callee_name;
  sig.language = language;
  sig.param_count = arg_types.size();
  sig.param_count_known = true;
  sig.param_types = arg_types;
  sig.return_type = return_type;
  sig.defined_at = loc;

  auto current_abi = BuildABISignature(sig, language);
  if (!current_abi) {
    ReportStrictDiag(loc, frontends::ErrorCode::kABIIncompatible,
                     "failed to build ABI signature for '" + callee_name + "'");
    return;
  }

  auto it = abi_signatures_.find(callee_name);
  if (it == abi_signatures_.end() || !it->second) {
    abi_signatures_[callee_name] = current_abi;
    return;
  }

  // Compare against the previously observed ABI shape for this symbol.
  std::string compat_err = it->second->ValidateCompatibility(*current_abi);
  if (!compat_err.empty()) {
    ReportStrictDiag(loc, frontends::ErrorCode::kABIIncompatible,
                     "ABI mismatch for '" + callee_name + "': " + compat_err);
    return;
  }

  // Keep the freshest source location/signature info.
  abi_signatures_[callee_name] = current_abi;
}

void PloySema::ValidateContextManagerProtocol(const core::SourceLoc &loc,
                                              const std::string &language,
                                              const core::Type &resource_type) {
  (void)language;

  if (resource_type.kind == core::TypeKind::kInvalid) {
    ReportStrictDiag(loc, frontends::ErrorCode::kSignatureMissing,
                     "WITH resource has invalid type");
    return;
  }

  std::string type_name =
      resource_type.name.empty() ? resource_type.ToString() : resource_type.name;
  if (type_name.empty()) {
    type_name = "<resource>";
  }

  std::string enter_name = type_name + "::__enter__";
  std::string exit_name = type_name + "::__exit__";

  const FunctionSignature *enter_sig = LookupSignature(enter_name);
  if (!enter_sig) {
    enter_sig = LookupSignature("__enter__");
  }

  const FunctionSignature *exit_sig = LookupSignature(exit_name);
  if (!exit_sig) {
    exit_sig = LookupSignature("__exit__");
  }

  if (!enter_sig) {
    ReportStrictDiag(loc, frontends::ErrorCode::kSignatureMissing,
                     "WITH resource '" + type_name + "' does not provide __enter__ signature");
  }
  if (!exit_sig) {
    ReportStrictDiag(loc, frontends::ErrorCode::kSignatureMissing,
                     "WITH resource '" + type_name + "' does not provide __exit__ signature");
  } else if (exit_sig->param_count_known && exit_sig->param_count != 4) {
    ReportStrictDiag(loc, frontends::ErrorCode::kParamCountMismatch,
                     "__exit__ for '" + type_name +
                         "' must accept 4 parameters (self, exc_type, exc_val, exc_tb), got " +
                         std::to_string(exit_sig->param_count));
  }
}

void PloySema::AnalyzeVenvConfigDecl(const std::shared_ptr<VenvConfigDecl> &venv_config) {
  // Validate the language
  if (!IsValidLanguage(venv_config->language)) {
    Report(venv_config->loc, "unknown language '" + venv_config->language + "' in CONFIG");
    return;
  }

  // Validate the (language, manager) pair against the registry.  An
  // unresolved pair surfaces as a hard error pointing at the canonical
  // form so authors do not silently lose package discovery.
  if (venv_config->manager == VenvConfigDecl::ManagerKind::kUnknown) {
    std::string msg = "unknown package manager '" + venv_config->manager_name +
                      "' for language '" + venv_config->language +
                      "' in CONFIG; canonical form is `CONFIG <lang> "
                      "\"<package_manager>\" \"<path_or_env>\";`";
    Report(venv_config->loc, msg);
    return;
  }

  // Legacy keyword-driven form was used — surface a deprecation warning
  // pointing at the new stringified form so users have a concrete
  // rewrite target.  Every legacy keyword maps to a Python manager so
  // the suggestion is mechanical.
  if (venv_config->is_legacy_form) {
    std::string suggestion = "rewrite as `CONFIG " + venv_config->language + " \"" +
                             venv_config->manager_name + "\" \"" + venv_config->venv_path +
                             "\";` (since v1.12.0)";
    ReportWarning(venv_config->loc, frontends::ErrorCode::kDeprecatedKeyword,
                  "legacy `CONFIG <KEYWORD>` form is deprecated", suggestion);
  }

  // Check for duplicate venv config for the same language
  for (const auto &existing : venv_configs_) {
    if (existing.language == venv_config->language) {
      Report(venv_config->loc, "duplicate CONFIG for language '" + venv_config->language +
                                   "' (previously defined at " + existing.defined_at.file + ":" +
                                   std::to_string(existing.defined_at.line) + ")");
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
  return op == ">=" || op == "<=" || op == "==" || op == ">" || op == "<" || op == "~=";
}

bool PloySema::IsValidVersionString(const std::string &version) const {
  if (version.empty())
    return false;
  // Version strings consist of digits separated by dots: 1.20, 2.0.0, etc.
  // Also allow pre-release suffixes like 1.0.0-beta, 1.0.0rc1
  bool has_digit = false;
  for (size_t i = 0; i < version.size(); ++i) {
    char c = version[i];
    if (std::isdigit(static_cast<unsigned char>(c))) {
      has_digit = true;
    } else if (c == '.') {
      // Dot must be preceded and followed by a digit (or end of version)
      if (i == 0 || i == version.size() - 1)
        return false;
    } else if (std::isalpha(static_cast<unsigned char>(c)) || c == '-') {
      // Allow pre-release identifiers after a digit
      if (!has_digit)
        return false;
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
  while (inst_parts.size() < max_len)
    inst_parts.push_back(0);
  while (req_parts.size() < max_len)
    req_parts.push_back(0);

  // Lexicographic comparison
  int cmp = 0;
  for (size_t i = 0; i < max_len; ++i) {
    if (inst_parts[i] < req_parts[i]) {
      cmp = -1;
      break;
    }
    if (inst_parts[i] > req_parts[i]) {
      cmp = 1;
      break;
    }
  }

  if (op == ">=")
    return cmp >= 0;
  if (op == "<=")
    return cmp <= 0;
  if (op == "==")
    return cmp == 0;
  if (op == ">")
    return cmp > 0;
  if (op == "<")
    return cmp < 0;
  if (op == "~=") {
    // Compatible release: ~= 1.20 means >= 1.20, < 2.0
    //                     ~= 1.20.3 means >= 1.20.3, < 1.21.0
    if (cmp < 0)
      return false; // installed < required
    // Check upper bound: the second-to-last segment must match
    auto upper = req_parts;
    if (upper.size() >= 2) {
      upper[upper.size() - 2] += 1;
      upper[upper.size() - 1] = 0;
      for (size_t i = 0; i < upper.size(); ++i) {
        if (inst_parts[i] < upper[i])
          return true;
        if (inst_parts[i] > upper[i])
          return false;
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
  } else if (language == "java") {
    DiscoverJavaPackages(venv_path);
  } else if (language == "dotnet" || language == "csharp") {
    DiscoverDotnetPackages();
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
  // Execute the command via the command runner abstraction
  std::string output = command_runner_->Run(pip_cmd);
  if (output.empty())
    return;

  // Parse pip freeze output: each line is "package==version"
  std::istringstream stream(output);
  std::string line;
  while (std::getline(stream, line)) {
    // Skip empty lines and comment lines
    if (line.empty() || line[0] == '#')
      continue;

    // Format: package==version or package===version
    size_t eq_pos = line.find("==");
    if (eq_pos == std::string::npos)
      continue;

    std::string pkg_name = line.substr(0, eq_pos);
    std::string pkg_version = line.substr(eq_pos + 2);

    // Trim trailing whitespace/newlines
    while (!pkg_version.empty() && (pkg_version.back() == '\n' || pkg_version.back() == '\r' ||
                                    pkg_version.back() == ' ')) {
      pkg_version.pop_back();
    }
    while (!pkg_name.empty() &&
           (pkg_name.back() == '\n' || pkg_name.back() == '\r' || pkg_name.back() == ' ')) {
      pkg_name.pop_back();
    }

    // Normalize package name (pip uses hyphens, Python uses underscores)
    std::string normalized_name = pkg_name;
    for (char &c : normalized_name) {
      if (c == '-')
        c = '_';
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
#ifdef _WIN32
    conda_cmd = "conda list -n " + env_name + " --export 2>nul";
#else
    conda_cmd = "conda list -n " + env_name + " --export 2>/dev/null";
#endif
  } else {
#ifdef _WIN32
    conda_cmd = "conda list --export 2>nul";
#else
    conda_cmd = "conda list --export 2>/dev/null";
#endif
  }

  std::string output = command_runner_->Run(conda_cmd);
  if (output.empty())
    return;

  // Parse conda list --export output: each line is "package=version=build_string"
  // Lines starting with '#' are comments
  std::istringstream stream(output);
  std::string line;
  while (std::getline(stream, line)) {
    if (line.empty() || line[0] == '#' || line[0] == '@')
      continue;

    // Format: package=version=build
    size_t first_eq = line.find('=');
    if (first_eq == std::string::npos)
      continue;

    std::string pkg_name = line.substr(0, first_eq);
    std::string rest = line.substr(first_eq + 1);

    size_t second_eq = rest.find('=');
    std::string pkg_version = (second_eq != std::string::npos) ? rest.substr(0, second_eq) : rest;

    // Trim whitespace
    while (!pkg_version.empty() && (pkg_version.back() == '\n' || pkg_version.back() == '\r' ||
                                    pkg_version.back() == ' ')) {
      pkg_version.pop_back();
    }
    while (!pkg_name.empty() &&
           (pkg_name.back() == '\n' || pkg_name.back() == '\r' || pkg_name.back() == ' ')) {
      pkg_name.pop_back();
    }

    // Normalize package name
    std::string normalized_name = pkg_name;
    for (char &c : normalized_name) {
      if (c == '-')
        c = '_';
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
    pipenv_cmd =
        "cmd /c \"cd /d " + project_path + " && pipenv run pip list --format=freeze 2>nul\"";
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
    poetry_cmd =
        "cmd /c \"cd /d " + project_path + " && poetry run pip list --format=freeze 2>nul\"";
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
#ifdef _WIN32
  std::string output = command_runner_->Run("cargo install --list 2>nul");
#else
  std::string output = command_runner_->Run("cargo install --list 2>/dev/null");
#endif
  if (output.empty())
    return;

  // Parse cargo install --list output: "crate_name v1.2.3:"
  std::istringstream stream(output);
  std::string line;
  while (std::getline(stream, line)) {
    if (line.empty() || line[0] == ' ')
      continue;

    // Lines look like: "crate_name v1.2.3:"
    size_t space_pos = line.find(' ');
    if (space_pos == std::string::npos)
      continue;

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
#ifdef _WIN32
  std::string output = command_runner_->Run("pkg-config --list-all 2>nul");
#else
  std::string output = command_runner_->Run("pkg-config --list-all 2>/dev/null");
#endif
  if (output.empty())
    return;

  // Parse pkg-config output: "package_name  description"
  std::istringstream stream(output);
  std::string line;
  while (std::getline(stream, line)) {
    if (line.empty())
      continue;

    size_t space_pos = line.find(' ');
    std::string pkg_name = (space_pos != std::string::npos) ? line.substr(0, space_pos) : line;

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
  // Stamp the resolved language version, if any.
  if (!del_expr->language.empty()) {
    del_expr->lang_version_pin = ResolveLangVersion(del_expr->language);
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
                "supported languages: python, ruby, javascript");
    return;
  }

  // EXTEND is restricted to dynamically-typed host languages where
  // monkey-patching is a first-class concept.  Static languages
  // (C, C++, Rust, Java, .NET / C#, Go) cannot have a class extended
  // from outside their own type system without breaking soundness, so
  // sema rejects EXTEND on them and points the user at the local
  // wrapper-function workflow instead.
  static const auto is_dynamic_host = [](const std::string &lang) {
    return lang == "python" || lang == "ruby" || lang == "rb" ||
           lang == "javascript" || lang == "js" || lang == "typescript" ||
           lang == "ts";
  };
  if (!extend->language.empty() && !is_dynamic_host(extend->language)) {
    ReportError(extend->loc, frontends::ErrorCode::kInvalidLanguage,
                "EXTEND is not allowed on statically-typed language '" +
                    extend->language +
                    "' — its type system cannot accept an out-of-source "
                    "subclass without breaking soundness",
                "wrap the foreign API in a local Ploy FUNC and use "
                "CALL / METHOD instead, or move the EXTEND target to a "
                "dynamic host (python / ruby / javascript)");
    return;
  }

  // Stamp the resolved language version, if any.
  extend->lang_version_pin = ResolveLangVersion(extend->language);

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
      sig.return_type = func->return_type ? ResolveType(func->return_type) : core::Type::Void();
      RegisterFunctionSignature(qualified, sig);

      // Also register the short method name so that METHOD(lang, obj, name, ...)
      // calls can find the signature during lowering.
      RegisterFunctionSignature(func->name, sig);

      // Create a LinkEntry for each EXTEND method so that the marshal
      // plan and bridge generation stages can produce bridge stubs.
      // Convention: target = foreign language method, source = ploy impl.
      LinkEntry method_link;
      method_link.kind = LinkDecl::LinkKind::kFunction;
      method_link.target_language = extend->language;
      method_link.source_language = "ploy";
      method_link.target_symbol = func->name;
      method_link.source_symbol = qualified;
      method_link.defined_at = func->loc;
      links_.push_back(method_link);
    } else {
      ReportError(method_stmt->loc, frontends::ErrorCode::kTypeMismatch,
                  "only FUNC declarations are allowed inside EXTEND body");
    }
  }
}

// ============================================================================
// TYPE alias and CONST analysis (demand 2026-04-28-7)
// ============================================================================

namespace {

// Render a width-aware type as the user would write it.  Falls back to
// `core::Type::ToString()` for non-width types so that diagnostics keep
// the existing wording for structs, generics, etc.
std::string FormatTypeForDiag(const core::Type &t) {
  if (t.kind == core::TypeKind::kInt && t.bit_width != 0) {
    return std::string(t.is_signed ? "i" : "u") + std::to_string(t.bit_width);
  }
  if (t.kind == core::TypeKind::kFloat && t.bit_width != 0) {
    return "f" + std::to_string(t.bit_width);
  }
  return t.ToString();
}

// Try to fold an expression to its constant textual representation.  The
// folder is intentionally limited to what the spec calls a "compile-time
// constant initializer": literals, the unary `-` / `!` operators, binary
// arithmetic and comparison on integer / floating-point operands, string
// concatenation via `+`, and references to previously declared CONSTs.
//
// Returns `true` and writes the folded text into `out_text` plus the
// inferred type into `out_type` on success.  On failure the reason is
// written to `out_reason` so the caller can produce a precise diagnostic.
struct ConstFoldResult {
  bool ok{false};
  core::Type type{core::Type::Invalid()};
  std::string text;
  std::string reason;
};

ConstFoldResult FoldConst(
    const std::shared_ptr<polyglot::ploy::Expression> &expr,
    const std::unordered_map<std::string, polyglot::ploy::PloySema::ConstValue> &consts);

ConstFoldResult FoldConst(
    const std::shared_ptr<polyglot::ploy::Expression> &expr,
    const std::unordered_map<std::string, polyglot::ploy::PloySema::ConstValue> &consts) {
  using namespace polyglot::ploy;
  ConstFoldResult r;
  if (!expr) {
    r.reason = "missing initializer expression";
    return r;
  }
  if (auto lit = std::dynamic_pointer_cast<Literal>(expr)) {
    switch (lit->kind) {
    case Literal::Kind::kInteger:
      r.type = core::Type::Int(64, true);
      r.text = lit->value;
      r.ok = true;
      return r;
    case Literal::Kind::kFloat:
      r.type = core::Type::Float(64);
      r.text = lit->value;
      r.ok = true;
      return r;
    case Literal::Kind::kString:
      r.type = core::Type::String();
      r.text = lit->value;
      r.ok = true;
      return r;
    case Literal::Kind::kBool:
      r.type = core::Type::Bool();
      r.text = lit->value;
      r.ok = true;
      return r;
    case Literal::Kind::kNull:
      r.type = core::Type::Any();
      r.text = "NULL";
      r.ok = true;
      return r;
    }
    r.reason = "unsupported literal kind in CONST initializer";
    return r;
  }
  if (auto id = std::dynamic_pointer_cast<Identifier>(expr)) {
    auto it = consts.find(id->name);
    if (it == consts.end()) {
      r.reason = "CONST initializer references '" + id->name +
                 "' which is not a previously declared CONST";
      return r;
    }
    r.type = it->second.type;
    r.text = it->second.folded_text;
    r.ok = true;
    return r;
  }
  if (auto un = std::dynamic_pointer_cast<UnaryExpression>(expr)) {
    auto inner = FoldConst(un->operand, consts);
    if (!inner.ok) {
      r.reason = inner.reason;
      return r;
    }
    if (un->op == "-") {
      if (inner.type.kind != core::TypeKind::kInt &&
          inner.type.kind != core::TypeKind::kFloat) {
        r.reason = "unary '-' requires a numeric CONST operand";
        return r;
      }
      r.type = inner.type;
      r.text = "-" + inner.text;
      r.ok = true;
      return r;
    }
    if (un->op == "!" || un->op == "NOT") {
      if (inner.type.kind != core::TypeKind::kBool) {
        r.reason = "unary '!' requires a boolean CONST operand";
        return r;
      }
      r.type = core::Type::Bool();
      r.text = (inner.text == "true" || inner.text == "TRUE") ? "false" : "true";
      r.ok = true;
      return r;
    }
    r.reason = "unsupported unary operator '" + un->op + "' in CONST";
    return r;
  }
  if (auto bin = std::dynamic_pointer_cast<BinaryExpression>(expr)) {
    auto left = FoldConst(bin->left, consts);
    if (!left.ok) {
      r.reason = left.reason;
      return r;
    }
    auto right = FoldConst(bin->right, consts);
    if (!right.ok) {
      r.reason = right.reason;
      return r;
    }
    // String concatenation via `+`.
    if (bin->op == "+" && left.type.kind == core::TypeKind::kString &&
        right.type.kind == core::TypeKind::kString) {
      r.type = core::Type::String();
      r.text = left.text + right.text;
      r.ok = true;
      return r;
    }
    // Numeric arithmetic �?promote width to the wider of the two
    // operands, preserve floating-pointness if either side is float.
    bool both_numeric = left.type.IsNumeric() && right.type.IsNumeric();
    if (both_numeric &&
        (bin->op == "+" || bin->op == "-" || bin->op == "*" || bin->op == "/" ||
         bin->op == "%")) {
      bool result_float =
          left.type.kind == core::TypeKind::kFloat ||
          right.type.kind == core::TypeKind::kFloat;
      int bits = std::max(left.type.bit_width != 0 ? left.type.bit_width : 64,
                          right.type.bit_width != 0 ? right.type.bit_width : 64);
      r.type = result_float ? core::Type::Float(bits)
                             : core::Type::Int(bits, left.type.is_signed && right.type.is_signed);
      r.text = "(" + left.text + " " + bin->op + " " + right.text + ")";
      r.ok = true;
      return r;
    }
    if ((bin->op == "==" || bin->op == "!=" || bin->op == "<" || bin->op == "<=" ||
         bin->op == ">" || bin->op == ">=") && both_numeric) {
      r.type = core::Type::Bool();
      r.text = "(" + left.text + " " + bin->op + " " + right.text + ")";
      r.ok = true;
      return r;
    }
    if ((bin->op == "&&" || bin->op == "||" || bin->op == "AND" || bin->op == "OR") &&
        left.type.kind == core::TypeKind::kBool &&
        right.type.kind == core::TypeKind::kBool) {
      r.type = core::Type::Bool();
      r.text = "(" + left.text + " " + bin->op + " " + right.text + ")";
      r.ok = true;
      return r;
    }
    r.reason = "unsupported binary operator '" + bin->op +
               "' or operand types in CONST initializer";
    return r;
  }
  r.reason = "CONST initializer is not a compile-time constant expression";
  return r;
}

} // namespace

// ============================================================================
// Foreign Class Schema Registration (demand 2026-04-28-9)
// ============================================================================

void PloySema::RegisterClassSchema(const std::string &qualified_name,
                                   ForeignClassSchema schema) {
  class_schemas_[qualified_name] = std::move(schema);
}

const ForeignClassSchema *
PloySema::LookupClassSchema(const std::string &qualified_name) const {
  auto it = class_schemas_.find(qualified_name);
  return it == class_schemas_.end() ? nullptr : &it->second;
}

void PloySema::AnalyzeClassDecl(const std::shared_ptr<ClassDecl> &cls_decl) {
  if (!cls_decl) {
    return;
  }
  // Header-level validation.
  if (cls_decl->language.empty()) {
    ReportError(cls_decl->loc, frontends::ErrorCode::kInvalidLanguage,
                "CLASS declaration is missing a language qualifier");
    return;
  }
  if (!IsValidLanguage(cls_decl->language)) {
    ReportError(cls_decl->loc, frontends::ErrorCode::kInvalidLanguage,
                "unknown language '" + cls_decl->language + "' in CLASS");
  }
  if (cls_decl->class_path.empty()) {
    ReportError(cls_decl->loc, frontends::ErrorCode::kEmptySymbolName,
                "CLASS declaration is missing a class path");
    return;
  }
  // Stamp any active language-version pin so backends can route the
  // schema to the correct interpreter / runtime version.
  cls_decl->lang_version_pin = ResolveLangVersion(cls_decl->language);

  // The qualified key uses the same form that AnalyzeNewExpression and
  // AnalyzeMethodCallExpression produce when consulting the registry.
  const std::string qualified_name = cls_decl->language + "::" + cls_decl->class_path;

  // Reject duplicate schemas; otherwise multiple definitions silently
  // overwrite each other and produce confusing dispatch behavior.
  if (class_schemas_.count(qualified_name)) {
    ReportError(cls_decl->loc, frontends::ErrorCode::kRedefinedSymbol,
                "redefinition of CLASS schema '" + qualified_name + "'");
    return;
  }

  ForeignClassSchema schema;
  schema.class_name = cls_decl->class_path;
  schema.language = cls_decl->language;
  schema.defined_at = cls_decl->loc;

  // Resolve attribute rows.
  std::unordered_set<std::string> seen_attrs;
  for (const auto &attr : cls_decl->attrs) {
    if (attr.name.empty()) {
      ReportError(attr.loc, frontends::ErrorCode::kEmptySymbolName,
                  "ATTR row in CLASS '" + qualified_name + "' has no name");
      continue;
    }
    if (!seen_attrs.insert(attr.name).second) {
      ReportError(attr.loc, frontends::ErrorCode::kDuplicateField,
                  "duplicate ATTR '" + attr.name + "' in CLASS '" + qualified_name + "'");
      continue;
    }
    core::Type t = attr.type ? ResolveType(attr.type) : core::Type::Unknown();
    schema.attributes[attr.name] = t;
  }

  // Resolve method rows.  Each method becomes a FunctionSignature and is
  // also published to the global signature registry under both the
  // qualified (`lang::Class::method`) and short (`Class::method`) names
  // so existing LookupSignature paths in AnalyzeMethodCallExpression
  // keep working without special-casing.
  std::unordered_set<std::string> seen_methods;
  for (const auto &m : cls_decl->methods) {
    if (m.name.empty()) {
      ReportError(m.loc, frontends::ErrorCode::kEmptySymbolName,
                  "METHOD row in CLASS '" + qualified_name + "' has no name");
      continue;
    }
    if (!seen_methods.insert(m.name).second) {
      ReportError(m.loc, frontends::ErrorCode::kRedefinedSymbol,
                  "duplicate METHOD '" + m.name + "' in CLASS '" + qualified_name + "'");
      continue;
    }
    FunctionSignature sig;
    sig.name = qualified_name + "::" + m.name;
    sig.language = cls_decl->language;
    sig.defined_at = m.loc;
    for (const auto &p : m.params) {
      sig.param_names.push_back(p.first);
      sig.param_types.push_back(p.second ? ResolveType(p.second) : core::Type::Unknown());
    }
    sig.param_count = sig.param_types.size();
    sig.param_count_known = true;
    sig.return_type = m.return_type ? ResolveType(m.return_type) : core::Type::Void();

    if (m.name == "__init__" || m.name == "new" || m.name == "ctor") {
      schema.has_constructor = true;
      schema.constructor_sig = sig;
    }
    schema.methods[m.name] = sig;

    // Publish to the signature registry under both qualified and short
    // names.  This lets the existing LookupSignature() inside
    // AnalyzeMethodCallExpression continue to find the method without
    // a schema-specific code path.
    RegisterFunctionSignature(sig.name, sig);
    RegisterFunctionSignature(cls_decl->class_path + "::" + m.name, sig);
  }

  RegisterClassSchema(qualified_name, std::move(schema));
}

void PloySema::AnalyzeTypeAliasDecl(const std::shared_ptr<TypeAliasDecl> &alias_decl) {
  if (!alias_decl) {
    return;
  }
  if (alias_decl->name.empty()) {
    Report(alias_decl->loc, "TYPE alias name cannot be empty");
    return;
  }
  // Reject collisions with existing aliases, structs, and built-in
  // primitive keywords (the latter would shadow language fundamentals).
  if (type_aliases_.count(alias_decl->name) != 0) {
    ReportError(alias_decl->loc, frontends::ErrorCode::kRedefinedSymbol,
                "redefinition of TYPE alias '" + alias_decl->name + "'");
    return;
  }
  if (struct_defs_.count(alias_decl->name) != 0) {
    ReportError(alias_decl->loc, frontends::ErrorCode::kRedefinedSymbol,
                "TYPE alias '" + alias_decl->name +
                    "' conflicts with a struct of the same name");
    return;
  }
  static const std::unordered_set<std::string> kReservedTypeNames = {
      "I8",  "I16", "I32",   "I64",   "U8",  "U16", "U32",   "U64", "F32", "F64",
      "INT", "FLOAT","BOOL", "STRING","VOID","ANY", "USIZE","ISIZE"};
  if (kReservedTypeNames.count(alias_decl->name) != 0) {
    ReportError(alias_decl->loc, frontends::ErrorCode::kRedefinedSymbol,
                "TYPE alias '" + alias_decl->name +
                    "' shadows a built-in primitive type keyword");
    return;
  }
  core::Type resolved = ResolveType(alias_decl->aliased_type);
  if (resolved.kind == core::TypeKind::kInvalid) {
    Report(alias_decl->loc, "TYPE alias '" + alias_decl->name +
                                "' refers to an unresolvable type expression");
    return;
  }
  type_aliases_[alias_decl->name] = resolved;
  // Remember the alias-target relationship so width-mismatch diagnostics
  // can render `T (alias of i32)` rather than a bare `i32`.
  type_alias_origin_[FormatTypeForDiag(resolved)] = alias_decl->name;
}

void PloySema::AnalyzeConstDecl(const std::shared_ptr<ConstDecl> &const_decl) {
  if (!const_decl) {
    return;
  }
  if (const_decl->name.empty()) {
    Report(const_decl->loc, "CONST name cannot be empty");
    return;
  }
  if (constants_.count(const_decl->name) != 0 ||
      symbols_.count(const_decl->name) != 0) {
    ReportError(const_decl->loc, frontends::ErrorCode::kRedefinedSymbol,
                "redefinition of CONST '" + const_decl->name + "'");
    return;
  }
  if (!const_decl->type) {
    ReportError(const_decl->loc, frontends::ErrorCode::kMissingTypeAnnotation,
                "CONST '" + const_decl->name +
                    "' requires an explicit ': <type>' annotation");
    return;
  }
  core::Type declared = ResolveType(const_decl->type);
  if (declared.kind == core::TypeKind::kInvalid) {
    Report(const_decl->loc, "CONST '" + const_decl->name +
                                "' has an unresolvable declared type");
    return;
  }
  ConstFoldResult fold = FoldConst(const_decl->value, constants_);
  if (!fold.ok) {
    ReportError(const_decl->loc, frontends::ErrorCode::kTypeMismatch,
                "CONST '" + const_decl->name +
                    "' initializer is not a compile-time constant: " + fold.reason,
                "rewrite the initializer using only literals, previously "
                "declared CONSTs, and the supported arithmetic/boolean operators");
    return;
  }
  // Width-mismatch diagnostic: when both declared and folded types are
  // integers (or both floats) but their bit widths differ, surface the
  // mismatch.  Emit a warning rather than an error so that the common
  // pattern `CONST K: i32 = 1;` (literal folds to i64) keeps compiling.
  if (declared.kind == core::TypeKind::kInt && fold.type.kind == core::TypeKind::kInt &&
      declared.bit_width != 0 && fold.type.bit_width != 0 &&
      (declared.bit_width != fold.type.bit_width ||
       declared.is_signed != fold.type.is_signed)) {
    std::string declared_text = FormatTypeForDiag(declared);
    auto orig = type_alias_origin_.find(declared_text);
    if (orig != type_alias_origin_.end()) {
      declared_text = orig->second + " (alias of " + FormatTypeForDiag(declared) + ")";
    }
    ReportWarning(const_decl->loc, frontends::ErrorCode::kTypeMismatch,
                  "CONST '" + const_decl->name + "' declared as '" + declared_text +
                      "' but initializer folds to '" + FormatTypeForDiag(fold.type) +
                      "'; the value will be narrowed at lowering",
                  "annotate the literal explicitly or change the declared width");
  }
  if (declared.kind == core::TypeKind::kFloat && fold.type.kind == core::TypeKind::kFloat &&
      declared.bit_width != 0 && fold.type.bit_width != 0 &&
      declared.bit_width != fold.type.bit_width) {
    ReportWarning(const_decl->loc, frontends::ErrorCode::kTypeMismatch,
                  "CONST '" + const_decl->name + "' declared as '" +
                      FormatTypeForDiag(declared) + "' but initializer folds to '" +
                      FormatTypeForDiag(fold.type) +
                      "'; the value will be rounded at lowering");
  }
  // Hard error when the folded type is incompatible at all (e.g. string
  // assigned to an integer CONST).
  if (!AreTypesCompatible(fold.type, declared)) {
    ReportError(const_decl->loc, frontends::ErrorCode::kTypeMismatch,
                "CONST '" + const_decl->name + "' declared as '" +
                    FormatTypeForDiag(declared) +
                    "' but initializer has incompatible type '" +
                    FormatTypeForDiag(fold.type) + "'");
    return;
  }
  // Register both as a regular (immutable) symbol so identifier lookup in
  // expression analysis succeeds, and as a foldable constant for the
  // const-propagation table consumed by the middle layer.
  PloySymbol sym;
  sym.kind = PloySymbol::Kind::kVariable;
  sym.name = const_decl->name;
  sym.type = declared;
  sym.is_mutable = false;
  sym.defined_at = const_decl->loc;
  DeclareSymbol(sym);

  ConstValue cv;
  cv.type = declared;
  cv.folded_text = fold.text;
  constants_[const_decl->name] = cv;
}

// ============================================================================
// Java Package Discovery
// ============================================================================

void PloySema::DiscoverJavaPackages(const std::string &classpath) {
  // Discover Java packages using 'java -version' to verify installation
  // and optionally scanning the CLASSPATH or Maven/Gradle dependencies.

  // First, verify Java is available and detect version
#ifdef _WIN32
  std::string output = command_runner_->Run("java -version 2>&1");
#else
  std::string output = command_runner_->Run("java -version 2>&1");
#endif
  if (output.empty())
    return;

  // Parse Java version from output (e.g., "openjdk version \"17.0.1\"")
  std::string java_version;
  size_t ver_start = output.find('"');
  if (ver_start != std::string::npos) {
    size_t ver_end = output.find('"', ver_start + 1);
    if (ver_end != std::string::npos) {
      java_version = output.substr(ver_start + 1, ver_end - ver_start - 1);
    }
  }

  // Register the Java runtime as a discovered package
  if (!java_version.empty()) {
    PackageInfo info;
    info.name = "java.lang";
    info.version = java_version;
    info.language = "java";
    discovered_packages_["java::java.lang"] = info;

    // Register common Java standard library packages
    static const char *java_std_packages[] = {"java.util",         "java.io",
                                              "java.nio",          "java.net",
                                              "java.math",         "java.sql",
                                              "java.time",         "java.text",
                                              "java.security",     "java.util.concurrent",
                                              "java.util.stream",  "java.util.function",
                                              "java.lang.reflect", "java.lang.invoke",
                                              "javax.crypto",      "javax.net",
                                              "javax.sql"};
    for (const char *pkg : java_std_packages) {
      PackageInfo std_info;
      std_info.name = pkg;
      std_info.version = java_version;
      std_info.language = "java";
      discovered_packages_["java::" + std::string(pkg)] = std_info;
    }
  }

  // If a classpath or project path is specified, scan Maven/Gradle deps
  if (!classpath.empty()) {
    DiscoverJavaPackagesViaMaven(classpath);
    DiscoverJavaPackagesViaGradle(classpath);
  }
}

void PloySema::DiscoverJavaPackagesViaMaven(const std::string &project_path) {
  // Try to list dependencies from a Maven project
  std::string cmd;
#ifdef _WIN32
  cmd = "cd /d \"" + project_path +
        "\" && mvn dependency:list -DoutputAbsoluteArtifactFilename=true -q 2>nul";
#else
  cmd = "cd \"" + project_path +
        "\" && mvn dependency:list -DoutputAbsoluteArtifactFilename=true -q 2>/dev/null";
#endif

  std::string output = command_runner_->Run(cmd);
  if (output.empty())
    return;

  // Parse Maven dependency:list output
  // Lines like: "    com.google.guava:guava:jar:31.1-jre:compile"
  std::istringstream stream(output);
  std::string line;
  while (std::getline(stream, line)) {
    // Trim leading whitespace
    size_t start = line.find_first_not_of(" \t");
    if (start == std::string::npos)
      continue;
    line = line.substr(start);

    // Split by ':'
    std::vector<std::string> parts;
    std::istringstream part_stream(line);
    std::string part;
    while (std::getline(part_stream, part, ':')) {
      parts.push_back(part);
    }

    // Expected format: groupId:artifactId:packaging:version:scope
    if (parts.size() >= 4) {
      PackageInfo info;
      info.name = parts[0] + "." + parts[1]; // groupId.artifactId
      info.version = parts[3];
      info.language = "java";
      std::string key = "java::" + info.name;
      discovered_packages_[key] = info;
    }
  }
}

void PloySema::DiscoverJavaPackagesViaGradle(const std::string &project_path) {
  // Try to list dependencies from a Gradle project
  std::string cmd;
#ifdef _WIN32
  cmd = "cd /d \"" + project_path +
        "\" && gradle dependencies --configuration compileClasspath -q 2>nul";
#else
  cmd = "cd \"" + project_path +
        "\" && gradle dependencies --configuration compileClasspath -q 2>/dev/null";
#endif

  std::string output = command_runner_->Run(cmd);
  if (output.empty())
    return;

  // Parse Gradle dependency tree output
  // Lines like: "+--- com.google.guava:guava:31.1-jre"
  std::istringstream stream(output);
  std::string line;
  while (std::getline(stream, line)) {
    // Look for lines with dependency notation
    size_t dash_pos = line.find("--- ");
    if (dash_pos == std::string::npos)
      continue;

    std::string dep = line.substr(dash_pos + 4);
    // Remove trailing scope markers like " (*)" or " -> x.y.z"
    size_t arrow_pos = dep.find(" ->");
    if (arrow_pos != std::string::npos)
      dep = dep.substr(0, arrow_pos);
    size_t paren_pos = dep.find(" (");
    if (paren_pos != std::string::npos)
      dep = dep.substr(0, paren_pos);

    // Split by ':'
    std::vector<std::string> parts;
    std::istringstream part_stream(dep);
    std::string part;
    while (std::getline(part_stream, part, ':')) {
      parts.push_back(part);
    }

    if (parts.size() >= 3) {
      PackageInfo info;
      info.name = parts[0] + "." + parts[1];
      info.version = parts[2];
      info.language = "java";
      std::string key = "java::" + info.name;
      discovered_packages_[key] = info;
    }
  }
}

// ============================================================================
// .NET Package Discovery
// ============================================================================

void PloySema::DiscoverDotnetPackages() {
  // Discover .NET SDKs and NuGet packages

  // First, detect installed .NET SDKs
#ifdef _WIN32
  std::string output = command_runner_->Run("dotnet --list-sdks 2>nul");
#else
  std::string output = command_runner_->Run("dotnet --list-sdks 2>/dev/null");
#endif
  if (output.empty()) {
    // dotnet CLI not available; skip but still discover NuGet
    DiscoverDotnetNugetPackages();
    return;
  }

  // Parse SDK version output (e.g., "8.0.100 [/usr/share/dotnet/sdk]")
  std::istringstream sdk_stream(output);
  std::string line;
  std::string latest_version;
  while (std::getline(sdk_stream, line)) {
    if (line.empty())
      continue;
    size_t space_pos = line.find(' ');
    if (space_pos != std::string::npos) {
      latest_version = line.substr(0, space_pos);
    }
  }

  // Register the .NET runtime as a discovered package
  if (!latest_version.empty()) {
    // Register common .NET framework assemblies
    static const char *dotnet_std_packages[] = {"System",
                                                "System.Collections",
                                                "System.Collections.Generic",
                                                "System.IO",
                                                "System.Linq",
                                                "System.Net",
                                                "System.Net.Http",
                                                "System.Text",
                                                "System.Text.Json",
                                                "System.Threading",
                                                "System.Threading.Tasks",
                                                "System.Runtime",
                                                "System.Console",
                                                "System.Math",
                                                "System.Numerics",
                                                "Microsoft.Extensions.DependencyInjection",
                                                "Microsoft.Extensions.Logging",
                                                "Microsoft.Extensions.Configuration"};
    for (const char *pkg : dotnet_std_packages) {
      PackageInfo info;
      info.name = pkg;
      info.version = latest_version;
      info.language = "dotnet";
      discovered_packages_["dotnet::" + std::string(pkg)] = info;
    }
  }

  // Discover NuGet packages from global cache
  DiscoverDotnetNugetPackages();
}

void PloySema::DiscoverDotnetNugetPackages() {
  // List globally installed NuGet packages
#ifdef _WIN32
  std::string output = command_runner_->Run("dotnet nuget locals global-packages --list 2>nul");
#else
  std::string output =
      command_runner_->Run("dotnet nuget locals global-packages --list 2>/dev/null");
#endif
  // Output is like: "global-packages: C:\Users\...\.nuget\packages\"
  // Actual package resolution happens at link time via the .NET toolchain

  // Try listing packages from the current project (if any)
#ifdef _WIN32
  std::string proj_output = command_runner_->Run("dotnet list package 2>nul");
#else
  std::string proj_output = command_runner_->Run("dotnet list package 2>/dev/null");
#endif
  if (proj_output.empty())
    return;

  // Parse 'dotnet list package' output
  // Lines like: "   > Newtonsoft.Json           13.0.3      13.0.3"
  std::istringstream stream(proj_output);
  std::string line;
  while (std::getline(stream, line)) {
    if (line.find('>') == std::string::npos)
      continue;

    size_t gt_pos = line.find('>');
    std::string rest = line.substr(gt_pos + 1);

    // Trim and split by whitespace
    std::istringstream word_stream(rest);
    std::string pkg_name, requested_ver, resolved_ver;
    word_stream >> pkg_name >> requested_ver >> resolved_ver;

    if (!pkg_name.empty()) {
      PackageInfo info;
      info.name = pkg_name;
      info.version = resolved_ver.empty() ? requested_ver : resolved_ver;
      info.language = "dotnet";
      std::string key = "dotnet::" + pkg_name;
      discovered_packages_[key] = info;
    }
  }
}

// ============================================================================
// Language-version pinning
// ============================================================================
//
// Three surface forms feed a single scope stack of `language -> version`
// maps:
//
//   * `LANG <lang> = <version>;`            �?module-wide pin (stack[0]).
//   * `WITH LANG (lang=ver, ...) { body }`  �?scoped pin (push on entry,
//                                              analyze body, pop on exit).
//   * `@LANG(lang=ver, ...) <stmt>`         �?single-statement pin (push,
//                                              analyze the wrapped stmt, pop).
//
// `ResolveLangVersion(lang)` walks the stack from innermost to outermost
// and returns the first match, or "" when no pin applies (downstream
// stages then fall back to the language default).

namespace {

// Map a user-written language identifier (case-insensitive, allowing common
// aliases) to the canonical short name accepted by `IsValidLanguage`.
// Returns empty string when the alias is unknown.
std::string CanonicalizeLangName(const std::string &raw) {
  std::string s;
  s.reserve(raw.size());
  for (char c : raw) {
    s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  if (s == "cpp" || s == "c++") return "cpp";
  if (s == "python" || s == "py") return "python";
  if (s == "java") return "java";
  if (s == "dotnet" || s == "cs" || s == "csharp" || s == "c#") return "dotnet";
  if (s == "rust") return "rust";
  if (s == "go" || s == "golang") return "go";
  if (s == "javascript" || s == "js" || s == "ecma" || s == "ecmascript" ||
      s == "typescript" || s == "ts") {
    return "javascript";
  }
  if (s == "ruby" || s == "rb") return "ruby";
  if (s == "c") return "c";
  if (s == "ploy") return "ploy";
  return {};
}

} // namespace

void PloySema::AnalyzeLangPragma(const std::shared_ptr<LangPragma> &pragma) {
  if (!pragma) return;
  const std::string canon = CanonicalizeLangName(pragma->language);
  if (canon.empty()) {
    ReportError(pragma->loc, frontends::ErrorCode::kInvalidLanguage,
                "unknown language '" + pragma->language + "' in LANG pragma",
                "expected one of: cpp, python, java, dotnet, rust, go, javascript, ruby");
    return;
  }
  if (pragma->version.empty()) {
    ReportError(pragma->loc, frontends::ErrorCode::kLangVersionMismatch,
                "LANG pragma for '" + pragma->language + "' has empty version");
    return;
  }
  // Module-wide pins live in stack[0]; later pragmas overwrite earlier ones.
  if (lang_pin_stack_.empty()) {
    lang_pin_stack_.emplace_back();
  }
  lang_pin_stack_.front()[canon] = pragma->version;
}

void PloySema::AnalyzeWithLangBlock(const std::shared_ptr<WithLangBlock> &block) {
  if (!block) return;
  std::unordered_map<std::string, std::string> frame;
  for (const auto &pin : block->pins) {
    const std::string canon = CanonicalizeLangName(pin.language);
    if (canon.empty()) {
      ReportError(block->loc, frontends::ErrorCode::kInvalidLanguage,
                  "unknown language '" + pin.language + "' in WITH LANG",
                  "expected one of: cpp, python, java, dotnet, rust, go, javascript, ruby");
      continue;
    }
    if (pin.version.empty()) {
      ReportError(block->loc, frontends::ErrorCode::kLangVersionMismatch,
                  "WITH LANG pin for '" + pin.language + "' has empty version");
      continue;
    }
    frame[canon] = pin.version;
  }
  lang_pin_stack_.push_back(std::move(frame));
  AnalyzeBlockStatements(block->body);
  lang_pin_stack_.pop_back();
}

void PloySema::AnalyzeLangAnnotation(const std::shared_ptr<LangAnnotation> &anno) {
  if (!anno) return;
  std::unordered_map<std::string, std::string> frame;
  for (const auto &pin : anno->pins) {
    const std::string canon = CanonicalizeLangName(pin.language);
    if (canon.empty()) {
      ReportError(anno->loc, frontends::ErrorCode::kInvalidLanguage,
                  "unknown language '" + pin.language + "' in @LANG annotation",
                  "expected one of: cpp, python, java, dotnet, rust, go, javascript, ruby");
      continue;
    }
    if (pin.version.empty()) {
      ReportError(anno->loc, frontends::ErrorCode::kLangVersionMismatch,
                  "@LANG pin for '" + pin.language + "' has empty version");
      continue;
    }
    frame[canon] = pin.version;
  }
  lang_pin_stack_.push_back(std::move(frame));
  if (anno->target) {
    AnalyzeStatement(anno->target);
  } else {
    ReportError(anno->loc, frontends::ErrorCode::kMissingExpression,
                "@LANG annotation has no target statement");
  }
  lang_pin_stack_.pop_back();
}

std::string PloySema::ResolveLangVersion(const std::string &language) const {
  const std::string canon = CanonicalizeLangName(language);
  if (canon.empty()) {
    return {};
  }
  // Walk innermost-first.
  for (auto it = lang_pin_stack_.rbegin(); it != lang_pin_stack_.rend(); ++it) {
    auto found = it->find(canon);
    if (found != it->end()) {
      return found->second;
    }
  }
  return {};
}

} // namespace polyglot::ploy
