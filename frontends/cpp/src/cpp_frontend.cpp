// cpp_frontend.cpp — C++ language frontend adapter implementation.

#include "frontends/cpp/include/cpp_frontend.h"

#include "frontends/common/include/frontend_registry.h"
#include "frontends/common/include/sema_context.h"
#include "frontends/cpp/include/cpp_lexer.h"
#include "frontends/cpp/include/cpp_parser.h"
#include "frontends/cpp/include/cpp_sema.h"
#include "frontends/cpp/include/cpp_lowering.h"

namespace polyglot::cpp {

// ============================================================================
// Auto-registration
// ============================================================================

REGISTER_FRONTEND(std::make_shared<CppLanguageFrontend>());

// ============================================================================
// Tokenize
// ============================================================================

std::vector<frontends::Token> CppLanguageFrontend::Tokenize(
    const std::string &source, const std::string &filename) const {
    CppLexer lexer(source, filename);
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

bool CppLanguageFrontend::Analyze(
    const std::string &source,
    const std::string &filename,
    frontends::Diagnostics &diagnostics,
    const frontends::FrontendOptions & /*options*/) const {

    CppLexer lexer(source, filename);
    CppParser parser(lexer, diagnostics);
    parser.ParseModule();
    if (diagnostics.HasErrors()) return false;

    auto module = parser.TakeModule();
    if (!module) return false;

    frontends::SemaContext ctx(diagnostics);
    AnalyzeModule(*module, ctx);
    return !diagnostics.HasErrors();
}

// ============================================================================
// Lower
// ============================================================================

frontends::FrontendResult CppLanguageFrontend::Lower(
    const std::string &source,
    const std::string &filename,
    ir::IRContext &ir_ctx,
    frontends::Diagnostics &diagnostics,
    const frontends::FrontendOptions & /*options*/) const {

    frontends::FrontendResult result;

    CppLexer lexer(source, filename);
    CppParser parser(lexer, diagnostics);
    parser.ParseModule();
    auto module = parser.TakeModule();

    if (!module || diagnostics.HasErrors()) return result;

    frontends::SemaContext ctx(diagnostics);
    AnalyzeModule(*module, ctx);
    if (diagnostics.HasErrors()) return result;

    LowerToIR(*module, ir_ctx, diagnostics);
    result.lowered = true;
    result.success = !diagnostics.HasErrors();
    return result;
}

// ============================================================================
// ExtractSignatures — parse C++ source and extract function signatures
// ============================================================================

namespace {

/// Map a C++ TypeNode to a core::Type.
core::Type CppTypeToCore(const std::shared_ptr<TypeNode> &tn) {
    if (!tn) return core::Type::Void();

    if (auto st = std::dynamic_pointer_cast<SimpleType>(tn)) {
        const std::string &n = st->name;
        if (n == "void")   return core::Type::Void();
        if (n == "bool")   return core::Type::Bool();
        if (n == "int" || n == "int32_t" || n == "long")
            return core::Type::Int(32, true);
        if (n == "unsigned" || n == "uint32_t" || n == "unsigned int")
            return core::Type::Int(32, false);
        if (n == "int64_t" || n == "long long" || n == "size_t")
            return core::Type::Int(64, true);
        if (n == "uint64_t" || n == "unsigned long long")
            return core::Type::Int(64, false);
        if (n == "int8_t" || n == "char" || n == "signed char")
            return core::Type::Int(8, true);
        if (n == "uint8_t" || n == "unsigned char")
            return core::Type::Int(8, false);
        if (n == "int16_t" || n == "short")
            return core::Type::Int(16, true);
        if (n == "uint16_t" || n == "unsigned short")
            return core::Type::Int(16, false);
        if (n == "float")  return core::Type::Float(32);
        if (n == "double") return core::Type::Float(64);
        if (n == "string" || n == "std::string")
            return core::Type::String();
        // Class / struct / unknown type
        return core::Type{core::TypeKind::kClass, n, "cpp"};
    }
    if (auto pt = std::dynamic_pointer_cast<PointerType>(tn)) {
        return core::Type{core::TypeKind::kPointer, "ptr", "cpp"};
    }
    if (auto rt = std::dynamic_pointer_cast<ReferenceType>(tn)) {
        return core::Type{core::TypeKind::kReference, "ref", "cpp"};
    }
    if (auto qt = std::dynamic_pointer_cast<QualifiedType>(tn)) {
        return CppTypeToCore(qt->inner);
    }
    return core::Type::Any();
}

}  // namespace

std::vector<frontends::ForeignFunctionSignature> CppLanguageFrontend::ExtractSignatures(
    const std::string &source,
    const std::string &filename,
    const std::string &module_name) const {

    std::vector<frontends::ForeignFunctionSignature> result;

    frontends::Diagnostics diags;
    CppLexer lexer(source, filename);
    CppParser parser(lexer, diags);
    parser.ParseModule();
    auto module = parser.TakeModule();
    if (!module) return result;

    // Walk all top-level declarations looking for functions
    for (const auto &decl : module->declarations) {
        if (auto fn = std::dynamic_pointer_cast<FunctionDecl>(decl)) {
            frontends::ForeignFunctionSignature sig;
            sig.name = fn->name;
            sig.qualified_name = module_name.empty() ? fn->name
                                                     : module_name + "::" + fn->name;
            sig.return_type = CppTypeToCore(fn->return_type);
            sig.has_type_annotations = true;  // C++ always has explicit types

            for (const auto &p : fn->params) {
                sig.param_types.push_back(CppTypeToCore(p.type));
                sig.param_names.push_back(p.name);
            }

            result.push_back(std::move(sig));
        }
        // Also extract methods from class/struct declarations
        if (auto cls = std::dynamic_pointer_cast<RecordDecl>(decl)) {
            for (const auto &member : cls->methods) {
                if (auto method = std::dynamic_pointer_cast<FunctionDecl>(member)) {
                    frontends::ForeignFunctionSignature sig;
                    sig.name = method->name;
                    sig.qualified_name = module_name.empty()
                        ? cls->name + "::" + method->name
                        : module_name + "::" + cls->name + "::" + method->name;
                    sig.return_type = CppTypeToCore(method->return_type);
                    sig.is_method = true;
                    sig.class_name = cls->name;
                    sig.has_type_annotations = true;

                    for (const auto &p : method->params) {
                        sig.param_types.push_back(CppTypeToCore(p.type));
                        sig.param_names.push_back(p.name);
                    }

                    result.push_back(std::move(sig));
                }
            }
        }
    }

    return result;
}

}  // namespace polyglot::cpp
