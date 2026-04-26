/**
 * @file     dotnet_frontend.cpp
 * @brief    .NET/C# language frontend adapter implementation
 *
 * @ingroup  Frontend / .NET
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include "frontends/dotnet/include/dotnet_frontend.h"

#include "frontends/common/include/frontend_registry.h"
#include "frontends/common/include/sema_context.h"
#include "frontends/dotnet/include/dotnet_lexer.h"
#include "frontends/dotnet/include/dotnet_parser.h"
#include "frontends/dotnet/include/dotnet_sema.h"
#include "frontends/dotnet/include/dotnet_lowering.h"
#include "frontends/dotnet/include/metadata_reader.h"

namespace polyglot::dotnet {

// ============================================================================
// Auto-registration
// ============================================================================

REGISTER_FRONTEND(std::make_shared<DotnetLanguageFrontend>());

// ============================================================================
// Tokenize
// ============================================================================

std::vector<frontends::Token> DotnetLanguageFrontend::Tokenize(
    const std::string &source, const std::string &filename) const {
    DotnetLexer lexer(source, filename);
    std::vector<frontends::Token> tokens;
    while (true) {
        auto tok = lexer.NextToken();
        if (tok.kind == frontends::TokenKind::kEndOfFile) break;
        tokens.push_back(tok);
    }
    return tokens;
}

// ============================================================================
// Analyze
// ============================================================================

bool DotnetLanguageFrontend::Analyze(
    const std::string &source,
    const std::string &filename,
    frontends::Diagnostics &diagnostics,
    const frontends::FrontendOptions &options) const {

    DotnetLexer lexer(source, filename);
    DotnetParser parser(lexer, diagnostics);
    parser.ParseModule();
    if (diagnostics.HasErrors()) return false;

    auto module = parser.TakeModule();
    if (!module) return false;

    frontends::SemaContext ctx(diagnostics);
    DotNetSemaOptions sema_opts;
    AssemblyLoader loader(options.dotnet_references, diagnostics);
    if (!loader.empty()) sema_opts.loader = &loader;
    AnalyzeModule(*module, ctx, sema_opts);
    return !diagnostics.HasErrors();
}

// ============================================================================
// Lower
// ============================================================================

frontends::FrontendResult DotnetLanguageFrontend::Lower(
    const std::string &source,
    const std::string &filename,
    ir::IRContext &ir_ctx,
    frontends::Diagnostics &diagnostics,
    const frontends::FrontendOptions &options) const {

    frontends::FrontendResult result;

    DotnetLexer lexer(source, filename);
    DotnetParser parser(lexer, diagnostics);
    parser.ParseModule();
    auto module = parser.TakeModule();

    if (!module || diagnostics.HasErrors()) return result;

    frontends::SemaContext ctx(diagnostics);
    DotNetSemaOptions sema_opts;
    AssemblyLoader loader(options.dotnet_references, diagnostics);
    if (!loader.empty()) sema_opts.loader = &loader;
    AnalyzeModule(*module, ctx, sema_opts);
    if (diagnostics.HasErrors()) return result;

    LowerToIR(*module, ir_ctx, diagnostics);
    result.lowered = true;
    result.success = !diagnostics.HasErrors();
    return result;
}

// ============================================================================
// ExtractSignatures — parse C# source and extract method signatures
// ============================================================================

namespace {

/// Map a C# TypeNode to a core::Type.
core::Type DotnetTypeToCore(const std::shared_ptr<TypeNode> &tn) {
    if (!tn) return core::Type::Void();

    if (auto st = std::dynamic_pointer_cast<SimpleType>(tn)) {
        const std::string &n = st->name;
        if (n == "void")   return core::Type::Void();
        if (n == "bool")   return core::Type::Bool();
        if (n == "int" || n == "Int32")
            return core::Type::Int(32, true);
        if (n == "long" || n == "Int64")
            return core::Type::Int(64, true);
        if (n == "short" || n == "Int16")
            return core::Type::Int(16, true);
        if (n == "byte" || n == "Byte")
            return core::Type::Int(8, false);
        if (n == "sbyte" || n == "SByte")
            return core::Type::Int(8, true);
        if (n == "uint" || n == "UInt32")
            return core::Type::Int(32, false);
        if (n == "ulong" || n == "UInt64")
            return core::Type::Int(64, false);
        if (n == "ushort" || n == "UInt16")
            return core::Type::Int(16, false);
        if (n == "float" || n == "Single")
            return core::Type::Float(32);
        if (n == "double" || n == "Double")
            return core::Type::Float(64);
        if (n == "decimal" || n == "Decimal")
            return core::Type::Float(128);
        if (n == "string" || n == "String")
            return core::Type::String();
        if (n == "char" || n == "Char")
            return core::Type::Int(16, false);
        if (n == "object" || n == "Object")
            return core::Type::Any();
        if (n == "dynamic")
            return core::Type::Any();
        return core::Type{core::TypeKind::kClass, n, "dotnet"};
    }
    if (auto at = std::dynamic_pointer_cast<ArrayType>(tn)) {
        return core::Type{core::TypeKind::kArray, "array", "dotnet"};
    }
    if (auto nt = std::dynamic_pointer_cast<NullableType>(tn)) {
        return DotnetTypeToCore(nt->inner);
    }
    if (auto gt = std::dynamic_pointer_cast<GenericType>(tn)) {
        const std::string &n = gt->name;
        if (n == "List" || n == "IList" || n == "IEnumerable" || n == "ICollection")
            return core::Type{core::TypeKind::kArray, n, "dotnet"};
        if (n == "Dictionary" || n == "IDictionary")
            return core::Type{core::TypeKind::kStruct, n, "dotnet"};
        if (n == "Task") {
            if (!gt->type_args.empty()) return DotnetTypeToCore(gt->type_args[0]);
            return core::Type::Void();
        }
        if (n == "Nullable") {
            if (!gt->type_args.empty()) return DotnetTypeToCore(gt->type_args[0]);
            return core::Type::Any();
        }
        return core::Type{core::TypeKind::kClass, n, "dotnet"};
    }
    return core::Type::Any();
}

void ExtractFromClassMembers(const std::string &class_name,
                             const std::string &module_name,
                             const std::vector<std::shared_ptr<Statement>> &members,
                             std::vector<frontends::ForeignFunctionSignature> &out) {
    for (const auto &member : members) {
        if (auto method = std::dynamic_pointer_cast<MethodDecl>(member)) {
            frontends::ForeignFunctionSignature sig;
            sig.name = method->name;
            sig.qualified_name = module_name.empty()
                ? class_name + "::" + method->name
                : module_name + "::" + class_name + "::" + method->name;
            sig.return_type = DotnetTypeToCore(method->return_type);
            sig.is_method = !method->is_static;
            sig.class_name = class_name;
            sig.has_type_annotations = true;

            for (const auto &p : method->params) {
                sig.param_types.push_back(DotnetTypeToCore(p.type));
                sig.param_names.push_back(p.name);
            }

            out.push_back(std::move(sig));
        }
    }
}

}  // namespace

std::vector<frontends::ForeignFunctionSignature> DotnetLanguageFrontend::ExtractSignatures(
    const std::string &source,
    const std::string &filename,
    const std::string &module_name) const {

    std::vector<frontends::ForeignFunctionSignature> result;

    frontends::Diagnostics diags;
    DotnetLexer lexer(source, filename);
    DotnetParser parser(lexer, diags);
    parser.ParseModule();
    auto module = parser.TakeModule();
    if (!module) return result;

    for (const auto &decl : module->declarations) {
        if (auto cls = std::dynamic_pointer_cast<ClassDecl>(decl)) {
            ExtractFromClassMembers(cls->name, module_name, cls->members, result);
        }
        if (auto ns = std::dynamic_pointer_cast<NamespaceDecl>(decl)) {
            for (const auto &ns_member : ns->members) {
                if (auto cls = std::dynamic_pointer_cast<ClassDecl>(ns_member)) {
                    ExtractFromClassMembers(cls->name, module_name, cls->members, result);
                }
            }
        }
    }

    return result;
}

}  // namespace polyglot::dotnet
