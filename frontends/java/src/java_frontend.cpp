// java_frontend.cpp — Java language frontend adapter implementation.

#include "frontends/java/include/java_frontend.h"

#include "frontends/common/include/frontend_registry.h"
#include "frontends/common/include/sema_context.h"
#include "frontends/java/include/java_lexer.h"
#include "frontends/java/include/java_parser.h"
#include "frontends/java/include/java_sema.h"
#include "frontends/java/include/java_lowering.h"

namespace polyglot::java {

// ============================================================================
// Auto-registration
// ============================================================================

REGISTER_FRONTEND(std::make_shared<JavaLanguageFrontend>());

// ============================================================================
// Tokenize
// ============================================================================

std::vector<frontends::Token> JavaLanguageFrontend::Tokenize(
    const std::string &source, const std::string &filename) const {
    JavaLexer lexer(source, filename);
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

bool JavaLanguageFrontend::Analyze(
    const std::string &source,
    const std::string &filename,
    frontends::Diagnostics &diagnostics,
    const frontends::FrontendOptions & /*options*/) const {

    JavaLexer lexer(source, filename);
    JavaParser parser(lexer, diagnostics);
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

frontends::FrontendResult JavaLanguageFrontend::Lower(
    const std::string &source,
    const std::string &filename,
    ir::IRContext &ir_ctx,
    frontends::Diagnostics &diagnostics,
    const frontends::FrontendOptions & /*options*/) const {

    frontends::FrontendResult result;

    JavaLexer lexer(source, filename);
    JavaParser parser(lexer, diagnostics);
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
// ExtractSignatures — parse Java source and extract method signatures
// ============================================================================

namespace {

/// Map a Java TypeNode to a core::Type.
core::Type JavaTypeToCore(const std::shared_ptr<TypeNode> &tn) {
    if (!tn) return core::Type::Void();

    if (auto st = std::dynamic_pointer_cast<SimpleType>(tn)) {
        const std::string &n = st->name;
        if (n == "void")    return core::Type::Void();
        if (n == "boolean") return core::Type::Bool();
        if (n == "int")     return core::Type::Int(32, true);
        if (n == "long")    return core::Type::Int(64, true);
        if (n == "short")   return core::Type::Int(16, true);
        if (n == "byte")    return core::Type::Int(8, true);
        if (n == "char")    return core::Type::Int(16, false);
        if (n == "float")   return core::Type::Float(32);
        if (n == "double")  return core::Type::Float(64);
        if (n == "String")  return core::Type::String();
        if (n == "Integer" || n == "Long" || n == "Short" || n == "Byte")
            return core::Type::Int();
        if (n == "Double" || n == "Float")
            return core::Type::Float();
        if (n == "Boolean") return core::Type::Bool();
        if (n == "Object")  return core::Type::Any();
        return core::Type{core::TypeKind::kClass, n, "java"};
    }
    if (auto at = std::dynamic_pointer_cast<ArrayType>(tn)) {
        return core::Type{core::TypeKind::kArray, "array", "java"};
    }
    if (auto gt = std::dynamic_pointer_cast<GenericType>(tn)) {
        const std::string &n = gt->name;
        if (n == "List" || n == "ArrayList" || n == "LinkedList")
            return core::Type{core::TypeKind::kArray, n, "java"};
        if (n == "Map" || n == "HashMap" || n == "TreeMap")
            return core::Type{core::TypeKind::kStruct, n, "java"};
        if (n == "Set" || n == "HashSet")
            return core::Type{core::TypeKind::kArray, n, "java"};
        if (n == "Optional") {
            if (!gt->type_args.empty()) return JavaTypeToCore(gt->type_args[0]);
            return core::Type::Any();
        }
        return core::Type{core::TypeKind::kClass, n, "java"};
    }
    return core::Type::Any();
}

}  // namespace

std::vector<frontends::ForeignFunctionSignature> JavaLanguageFrontend::ExtractSignatures(
    const std::string &source,
    const std::string &filename,
    const std::string &module_name) const {

    std::vector<frontends::ForeignFunctionSignature> result;

    frontends::Diagnostics diags;
    JavaLexer lexer(source, filename);
    JavaParser parser(lexer, diags);
    parser.ParseModule();
    auto module = parser.TakeModule();
    if (!module) return result;

    // Walk declarations — Java top-level is usually a class
    for (const auto &decl : module->declarations) {
        if (auto cls = std::dynamic_pointer_cast<ClassDecl>(decl)) {
            for (const auto &member : cls->members) {
                if (auto method = std::dynamic_pointer_cast<MethodDecl>(member)) {
                    frontends::ForeignFunctionSignature sig;
                    sig.name = method->name;
                    sig.qualified_name = module_name.empty()
                        ? cls->name + "::" + method->name
                        : module_name + "::" + cls->name + "::" + method->name;
                    sig.return_type = JavaTypeToCore(method->return_type);
                    sig.is_method = !method->is_static;
                    sig.class_name = cls->name;
                    sig.has_type_annotations = true;  // Java always has types

                    for (const auto &p : method->params) {
                        sig.param_types.push_back(JavaTypeToCore(p.type));
                        sig.param_names.push_back(p.name);
                    }

                    result.push_back(std::move(sig));
                }
            }
        }
        // Top-level methods (shouldn't happen in valid Java but handle anyway)
        if (auto method = std::dynamic_pointer_cast<MethodDecl>(decl)) {
            frontends::ForeignFunctionSignature sig;
            sig.name = method->name;
            sig.qualified_name = module_name.empty() ? method->name
                                                     : module_name + "::" + method->name;
            sig.return_type = JavaTypeToCore(method->return_type);
            sig.has_type_annotations = true;
            for (const auto &p : method->params) {
                sig.param_types.push_back(JavaTypeToCore(p.type));
                sig.param_names.push_back(p.name);
            }
            result.push_back(std::move(sig));
        }
    }

    return result;
}

}  // namespace polyglot::java
