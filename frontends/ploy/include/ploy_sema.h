/**
 * @file     ploy_sema.h
 * @brief    Ploy language frontend
 *
 * @ingroup  Frontend / Ploy
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common/include/core/types.h"
#include "frontends/common/include/diagnostics.h"
#include "frontends/ploy/include/ploy_ast.h"

namespace polyglot::ploy {

// Forward declarations for decoupled subsystems
class PackageDiscoveryCache;
class ICommandRunner;

// ============================================================================
// PloySemaOptions — configuration structure for PloySema
//
// All fields have sensible defaults so that existing call-sites that construct
// PloySema(diagnostics) continue to work unchanged.
// ============================================================================

/** @brief PloySemaOptions data structure. */
struct PloySemaOptions {
    // When true, PloySema runs external package-manager commands on the first
    // IMPORT PACKAGE for each language to discover installed packages and their
    // versions.  Defaults to false — callers should use PackageIndexer to run
    // discovery as an explicit pre-compilation phase and pass the populated
    // cache via discovery_cache.  Set to true only for backward-compatible
    // scenarios where inline discovery is acceptable.
    bool enable_package_discovery{false};

    // Enable strict type-checking mode.  When enabled, the sema emits warnings
    // for every site where core::Type::Any() is used as a fallback that the
    // programmer could have resolved by adding explicit type annotations.
    bool strict_mode{false};

    // Optional shared cache for package discovery results.  When nullptr a
    // fresh per-instance cache is used (backward-compatible default).  Callers
    // that run multiple compilations in the same session can provide a shared
    // cache to avoid redundant external-command invocations.
    std::shared_ptr<PackageDiscoveryCache> discovery_cache{nullptr};

    // Optional command-runner abstraction.  When nullptr the default
    // implementation that calls _popen/popen is used.  Tests can inject a mock
    // to avoid touching the real filesystem or spawning processes.
    std::shared_ptr<ICommandRunner> command_runner{nullptr};
};

// ============================================================================
// Symbol Table Entry
// ============================================================================

/** @brief PloySymbol data structure. */
struct PloySymbol {
    /** @brief Kind enumeration. */
    enum class Kind { kVariable, kFunction, kImport, kLinkTarget, kPipeline };
    Kind kind{Kind::kVariable};
    std::string name;
    core::Type type{core::Type::Invalid()};
    bool is_mutable{false};
    std::string language;           // For imported symbols
    std::string external_name;     // For exported symbols
    core::SourceLoc defined_at{};
};

// ============================================================================
// Type Mapping Entry
// ============================================================================

/** @brief TypeMappingEntry data structure. */
struct TypeMappingEntry {
    std::string source_language;
    std::string source_type;
    std::string target_language;
    std::string target_type;
    core::SourceLoc defined_at{};
};

// ============================================================================
// Link Entry (validated cross-language link)
// ============================================================================

/** @brief LinkEntry data structure. */
struct LinkEntry {
    LinkDecl::LinkKind kind{LinkDecl::LinkKind::kFunction};
    std::string target_language;
    std::string source_language;
    std::string target_symbol;
    std::string source_symbol;
    // Resolved types for source and target function signatures
    core::Type target_type{core::Type::Invalid()};
    core::Type source_type{core::Type::Invalid()};
    // Per-parameter type mappings from the LINK body
    std::vector<TypeMappingEntry> param_mappings;
    core::SourceLoc defined_at{};
};

// ============================================================================
// Package Registry Entry (for auto-discovery)
// ============================================================================

/** @brief PackageInfo data structure. */
struct PackageInfo {
    std::string name;                   // Package name (e.g. "numpy")
    std::string version;                // Installed version (e.g. "1.24.3")
    std::string language;               // Language ecosystem (e.g. "python")
    std::string install_path;           // Filesystem path to the package
    std::vector<std::string> symbols;   // Exported symbols (functions, classes)
};

// ============================================================================
// Virtual Environment Configuration
// ============================================================================

/** @brief VenvConfig data structure. */
struct VenvConfig {
    VenvConfigDecl::ManagerKind manager{VenvConfigDecl::ManagerKind::kVenv};
    std::string language;       // e.g. "python"
    std::string venv_path;      // Path to the virtual environment / env name
    core::SourceLoc defined_at{};
};

// ============================================================================
// ABI Signature — unified calling convention and type layout descriptor
// ============================================================================

// Describes the ABI-level contract for a cross-language function call.
// Used by sema for pre-lowering validation and by the linker for stub
// generation correctness checks.
/** @brief ABIParamDesc data structure. */
struct ABIParamDesc {
    core::Type semantic_type{core::Type::Invalid()};  // High-level type
    std::string abi_type_name;                        // ABI-level type name (e.g. "i64", "f64", "ptr")
    size_t size_bytes{0};                             // Size in bytes (0 = unknown)
    size_t alignment{0};                              // Alignment in bytes (0 = natural)
    bool is_pointer{false};                           // Whether passed as pointer
    bool is_by_value{false};                          // Whether passed by value (struct copy)
};

/** @brief ABISignature data structure. */
struct ABISignature {
    std::string function_name;                        // Qualified function name
    std::string language;                             // Source language
    std::string calling_convention;                   // "sysv", "win64", "cdecl", "python_c", etc.
    std::vector<ABIParamDesc> params;                 // Parameter ABI descriptors
    ABIParamDesc return_desc;                         // Return value ABI descriptor
    bool is_variadic{false};                          // Whether the function is variadic
    bool is_complete{false};                          // Whether all param/return types are resolved
    core::SourceLoc defined_at{};                     // Where the signature was declared

    // Validate compatibility with another ABI signature (e.g. source vs target)
    // Returns empty string on success, or a diagnostic message on failure.
    std::string ValidateCompatibility(const ABISignature &other) const;
};

// ============================================================================
// Function Signature Registry (for parameter count/type validation)
// ============================================================================

/** @brief FunctionSignature data structure. */
struct FunctionSignature {
    std::string name;                         // Qualified function name
    std::string language;                     // Source language
    std::vector<core::Type> param_types;      // Parameter types (empty if unknown)
    std::vector<std::string> param_names;     // Parameter names (for named-arg matching)
    core::Type return_type{core::Type::Unknown()};// Return type (Unknown until resolved)
    size_t param_count{0};                    // Number of parameters
    bool param_count_known{false};            // Whether param count is statically known
    bool validated{false};                    // Whether the signature has been ABI-validated
    core::SourceLoc defined_at{};             // Where the function was declared

    // Optional ABI-level descriptor for cross-language validation.
    // Populated when the signature comes from a LINK declaration with MAP_TYPE.
    std::shared_ptr<ABISignature> abi{nullptr};
};

// ============================================================================
// Foreign Class Schema (for cross-language NEW/METHOD/GET/SET validation)
// ============================================================================

/** @brief ForeignClassSchema data structure. */
struct ForeignClassSchema {
    std::string class_name;                   // Fully qualified class name
    std::string language;                     // Source language (e.g. "python", "cpp")
    std::unordered_map<std::string, core::Type> attributes;    // Attribute types
    std::unordered_map<std::string, FunctionSignature> methods; // Method signatures
    bool has_constructor{false};              // Whether a constructor is registered
    FunctionSignature constructor_sig;        // Constructor signature
    core::SourceLoc defined_at{};
};

// ============================================================================
// Semantic Analyzer
// ============================================================================

/** @brief PloySema class. */
class PloySema {
  public:
    // Backward-compatible constructor — uses default options.
    // DEPRECATED: All new call-sites should explicitly pass PloySemaOptions{}
    // so that the intent (strict or permissive) is visible at the call-site.
    [[deprecated("Pass PloySemaOptions{} explicitly to declare strict-mode intent")]]
    explicit PloySema(frontends::Diagnostics &diagnostics)
        : PloySema(diagnostics, PloySemaOptions{}) {}

    // Full constructor — accepts options for fine-grained control.
    PloySema(frontends::Diagnostics &diagnostics, const PloySemaOptions &options);

    // Enable strict type-checking mode.  When enabled, the sema emits warnings
    // for every site where core::Type::Any() is used as a fallback that the
    // programmer could have resolved by adding explicit type annotations.
    void SetStrictMode(bool enable) { strict_mode_ = enable; }
    bool IsStrictMode() const { return strict_mode_; }

    // Whether package discovery is enabled for this instance.
    bool IsDiscoveryEnabled() const { return discovery_enabled_; }

    // Run semantic analysis on the parsed module
    bool Analyze(const std::shared_ptr<Module> &module);

    // Access results
    const std::vector<LinkEntry> &Links() const { return links_; }
    const std::vector<TypeMappingEntry> &TypeMappings() const { return type_mappings_; }
    const std::unordered_map<std::string, PloySymbol> &Symbols() const { return symbols_; }
    const std::vector<VenvConfig> &VenvConfigs() const { return venv_configs_; }
    const std::unordered_map<std::string, PackageInfo> &DiscoveredPackages() const {
        return discovered_packages_;
    }
    const std::unordered_map<std::string, FunctionSignature> &KnownSignatures() const {
        return known_signatures_;
    }
    const std::unordered_map<std::string, ForeignClassSchema> &ClassSchemas() const {
        return class_schemas_;
    }

    // Register a class schema for foreign class validation.
    void RegisterClassSchema(const std::string &qualified_name,
                             ForeignClassSchema schema);

    // Look up a class schema by qualified name (e.g. "python::torch.nn.Linear").
    const ForeignClassSchema *LookupClassSchema(const std::string &qualified_name) const;
    const std::unordered_map<std::string, std::shared_ptr<ABISignature>> &ABISignatures() const {
        return abi_signatures_;
    }
    const std::unordered_map<std::string, std::vector<std::pair<std::string, core::Type>>> &StructDefs() const {
        return struct_defs_;
    }

    /// Inject foreign function signatures extracted from external source files.
    /// Signatures already present (e.g. from LINK declarations) are NOT overwritten.
    void InjectForeignSignatures(
        const std::unordered_map<std::string, FunctionSignature> &foreign_sigs);

  private:
    // Declaration analysis
    void AnalyzeStatement(const std::shared_ptr<Statement> &stmt);
    void AnalyzeLinkDecl(const std::shared_ptr<LinkDecl> &link);
    void AnalyzeImportDecl(const std::shared_ptr<ImportDecl> &import);
    void AnalyzeExportDecl(const std::shared_ptr<ExportDecl> &export_decl);
    void AnalyzeMapTypeDecl(const std::shared_ptr<MapTypeDecl> &map_type);
    void AnalyzePipelineDecl(const std::shared_ptr<PipelineDecl> &pipeline);
    void AnalyzeFuncDecl(const std::shared_ptr<FuncDecl> &func);
    void AnalyzeVarDecl(const std::shared_ptr<VarDecl> &var);
    void AnalyzeStructDecl(const std::shared_ptr<StructDecl> &struct_decl);
    void AnalyzeMapFuncDecl(const std::shared_ptr<MapFuncDecl> &map_func);
    void AnalyzeVenvConfigDecl(const std::shared_ptr<VenvConfigDecl> &venv_config);
    void AnalyzeExtendDecl(const std::shared_ptr<ExtendDecl> &extend);

    // Statement analysis
    void AnalyzeIfStatement(const std::shared_ptr<IfStatement> &if_stmt);
    void AnalyzeWhileStatement(const std::shared_ptr<WhileStatement> &while_stmt);
    void AnalyzeForStatement(const std::shared_ptr<ForStatement> &for_stmt);
    void AnalyzeMatchStatement(const std::shared_ptr<MatchStatement> &match_stmt);
    void AnalyzeReturnStatement(const std::shared_ptr<ReturnStatement> &ret);
    void AnalyzeWithStatement(const std::shared_ptr<WithStatement> &with_stmt);
    void AnalyzeBlockStatements(const std::vector<std::shared_ptr<Statement>> &stmts);

    // Expression analysis
    core::Type AnalyzeExpression(const std::shared_ptr<Expression> &expr);
    core::Type AnalyzeCallExpression(const std::shared_ptr<CallExpression> &call);
    core::Type AnalyzeCrossLangCall(const std::shared_ptr<CrossLangCallExpression> &call);
    core::Type AnalyzeNewExpression(const std::shared_ptr<NewExpression> &new_expr);
    core::Type AnalyzeMethodCallExpression(const std::shared_ptr<MethodCallExpression> &method_call);
    core::Type AnalyzeGetAttrExpression(const std::shared_ptr<GetAttrExpression> &get_attr);
    core::Type AnalyzeSetAttrExpression(const std::shared_ptr<SetAttrExpression> &set_attr);
    core::Type AnalyzeBinaryExpression(const std::shared_ptr<BinaryExpression> &bin);
    core::Type AnalyzeUnaryExpression(const std::shared_ptr<UnaryExpression> &unary);
    core::Type AnalyzeConvertExpression(const std::shared_ptr<ConvertExpression> &conv);
    core::Type AnalyzeListLiteral(const std::shared_ptr<ListLiteral> &list);
    core::Type AnalyzeTupleLiteral(const std::shared_ptr<TupleLiteral> &tuple);
    core::Type AnalyzeDictLiteral(const std::shared_ptr<DictLiteral> &dict);
    core::Type AnalyzeStructLiteral(const std::shared_ptr<StructLiteral> &struct_lit);
    core::Type AnalyzeDeleteExpression(const std::shared_ptr<DeleteExpression> &del_expr);

    // Type resolution
    core::Type ResolveType(const std::shared_ptr<TypeNode> &type_node);
    bool IsValidLanguage(const std::string &lang) const;
    bool AreTypesCompatible(const core::Type &from, const core::Type &to) const;

    // Version constraint validation
    bool IsValidVersionString(const std::string &version) const;
    bool IsValidVersionOp(const std::string &op) const;
    bool CompareVersions(const std::string &installed, const std::string &required,
                         const std::string &op) const;
    std::vector<int> ParseVersionParts(const std::string &version) const;

    // Package auto-discovery
    void DiscoverPackages(const std::string &language, const std::string &venv_path = "",
                          VenvConfigDecl::ManagerKind manager = VenvConfigDecl::ManagerKind::kVenv);
    void DiscoverPythonPackages(const std::string &venv_path = "",
                                VenvConfigDecl::ManagerKind manager = VenvConfigDecl::ManagerKind::kVenv);
    void DiscoverPythonPackagesViaPip(const std::string &python_cmd);
    void DiscoverPythonPackagesViaConda(const std::string &env_name);
    void DiscoverPythonPackagesViaUv(const std::string &venv_path);
    void DiscoverPythonPackagesViaPipenv(const std::string &project_path);
    void DiscoverPythonPackagesViaPoetry(const std::string &project_path);
    void DiscoverRustCrates();
    void DiscoverCppPackages();
    void DiscoverJavaPackages(const std::string &classpath = "");
    void DiscoverJavaPackagesViaMaven(const std::string &project_path);
    void DiscoverJavaPackagesViaGradle(const std::string &project_path);
    void DiscoverDotnetPackages();
    void DiscoverDotnetNugetPackages();

    // Helper
    void Report(const core::SourceLoc &loc, const std::string &message);
    void ReportError(const core::SourceLoc &loc, frontends::ErrorCode code,
                     const std::string &message);
    void ReportError(const core::SourceLoc &loc, frontends::ErrorCode code,
                     const std::string &message, const std::string &suggestion);
    void ReportErrorWithTraceback(const core::SourceLoc &loc, frontends::ErrorCode code,
                                  const std::string &message,
                                  const core::SourceLoc &related_loc,
                                  const std::string &related_msg);
    void ReportWarning(const core::SourceLoc &loc, frontends::ErrorCode code,
                       const std::string &message);
    void ReportWarning(const core::SourceLoc &loc, frontends::ErrorCode code,
                       const std::string &message, const std::string &suggestion);

    // Report a diagnostic whose severity depends on strict mode:
    // strict → error (fails compilation), permissive → warning.
    void ReportStrictDiag(const core::SourceLoc &loc, frontends::ErrorCode code,
                          const std::string &message);

    bool DeclareSymbol(const PloySymbol &symbol);

    // Function signature validation helpers
    void RegisterFunctionSignature(const std::string &qualified_name,
                                   const FunctionSignature &sig);
    const FunctionSignature *LookupSignature(const std::string &qualified_name) const;
    void ValidateCallArgCount(const core::SourceLoc &call_loc,
                              const std::string &func_name,
                              size_t actual_args,
                              const FunctionSignature *sig);
    void ValidateCallArgTypes(const core::SourceLoc &call_loc,
                              const std::string &func_name,
                              const std::vector<core::Type> &actual_types,
                              const FunctionSignature *sig);
    // Validate that the return value of a call is used in a type-compatible context.
    void ValidateReturnType(const core::SourceLoc &call_loc,
                            const std::string &func_name,
                            const core::Type &expected_type,
                            const FunctionSignature *sig);

    // Build an ABISignature from a FunctionSignature and language info.
    std::shared_ptr<ABISignature> BuildABISignature(const FunctionSignature &sig,
                                                     const std::string &language) const;

    // Determine the calling convention string for a given language.
    static std::string CallingConventionForLanguage(const std::string &language);

    // Build an ABIParamDesc from a core::Type.
    ABIParamDesc TypeToABIParam(const core::Type &type) const;

    // Resolve the concrete type for an object expression used in
    // NEW/METHOD/GET/SET, replacing opaque Any() with the class type
    // registered via LINK/EXTEND when available.
    core::Type ResolveObjectType(const std::string &class_name,
                                 const std::string &language,
                                 const core::SourceLoc &loc);

    // Build and validate an ABI signature for a cross-language method/ctor
    // call.  Reports diagnostics when the resolved types are incompatible.
    void ValidateObjCallABI(const core::SourceLoc &loc,
                            const std::string &callee_name,
                            const std::string &language,
                            const std::vector<core::Type> &arg_types,
                            const core::Type &return_type);

    // Validate that a WITH resource type supports __enter__/__exit__ protocol.
    void ValidateContextManagerProtocol(const core::SourceLoc &loc,
                                        const std::string &language,
                                        const core::Type &resource_type);

    frontends::Diagnostics &diagnostics_;
    bool strict_mode_{false};
    bool discovery_enabled_{true};
    core::TypeSystem type_system_{};
    std::unordered_map<std::string, PloySymbol> symbols_{};
    std::vector<LinkEntry> links_{};
    std::vector<TypeMappingEntry> type_mappings_{};
    // Struct definitions: struct name -> list of (field_name, field_type)
    std::unordered_map<std::string, std::vector<std::pair<std::string, core::Type>>> struct_defs_{};
    // MAP_FUNC registry: function name -> return type
    std::unordered_map<std::string, core::Type> map_funcs_{};
    // Known function signatures for parameter validation
    std::unordered_map<std::string, FunctionSignature> known_signatures_{};
    // ABI signatures for cross-language link validation
    std::unordered_map<std::string, std::shared_ptr<ABISignature>> abi_signatures_{};
    // Foreign class schemas for NEW/METHOD/GET/SET validation
    std::unordered_map<std::string, ForeignClassSchema> class_schemas_{};
    int loop_depth_{0};
    core::Type current_return_type_{core::Type::Invalid()};
    // Track whether code is unreachable (after RETURN, BREAK, CONTINUE)
    bool unreachable_{false};

    // Virtual environment configurations
    std::vector<VenvConfig> venv_configs_{};

    // Session-level discovery cache (shared across compilations or per-instance)
    std::shared_ptr<PackageDiscoveryCache> discovery_cache_;

    // Command runner abstraction (default: real popen, can be mocked in tests)
    std::shared_ptr<ICommandRunner> command_runner_;

    // Instance-local copy of discovered packages (populated from cache)
    std::unordered_map<std::string, PackageInfo> discovered_packages_{};
};

} // namespace polyglot::ploy
