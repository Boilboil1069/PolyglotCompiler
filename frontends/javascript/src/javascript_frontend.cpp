/**
 * @file     javascript_frontend.cpp
 * @brief    JavaScript language frontend adapter implementation
 *
 * @ingroup  Frontend / JavaScript
 * @author   Manning Cyrus
 * @date     2026-04-26
 */
#include "frontends/javascript/include/javascript_frontend.h"

#include "frontends/common/include/frontend_registry.h"
#include "frontends/common/include/sema_context.h"
#include "frontends/javascript/include/javascript_ast.h"
#include "frontends/javascript/include/javascript_lexer.h"
#include "frontends/javascript/include/javascript_lowering.h"
#include "frontends/javascript/include/javascript_parser.h"
#include "frontends/javascript/include/javascript_sema.h"

namespace polyglot::javascript {

// ============================================================================
// Auto-registration
// ============================================================================

REGISTER_FRONTEND(std::make_shared<JsLanguageFrontend>());

// ============================================================================
// Tokenize
// ============================================================================

std::vector<frontends::Token> JsLanguageFrontend::Tokenize(
    const std::string &source, const std::string &filename) const {
    JsLexer lexer(source, filename);
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

bool JsLanguageFrontend::Analyze(
    const std::string &source, const std::string &filename,
    frontends::Diagnostics &diagnostics,
    const frontends::FrontendOptions & /*options*/) const {
    JsLexer lexer(source, filename);
    JsParser parser(lexer, diagnostics);
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

frontends::FrontendResult JsLanguageFrontend::Lower(
    const std::string &source, const std::string &filename,
    ir::IRContext &ir_ctx, frontends::Diagnostics &diagnostics,
    const frontends::FrontendOptions & /*options*/) const {
    frontends::FrontendResult result;
    JsLexer lexer(source, filename);
    JsParser parser(lexer, diagnostics);
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
// ExtractSignatures — JSDoc-driven, with conservative inference.
// ============================================================================

namespace {

// Map a JavaScript JSDoc TypeNode to a core::Type for cross-language interop.
core::Type JsTypeToCore(const std::shared_ptr<TypeNode> &t) {
    if (!t) return core::Type::Any();
    if (auto nt = std::dynamic_pointer_cast<NamedType>(t)) {
        const std::string &n = nt->name;
        if (n == "number" || n == "Number")           return core::Type::Float(64);
        if (n == "i8")     return core::Type::Int(8, true);
        if (n == "i16")    return core::Type::Int(16, true);
        if (n == "i32" || n == "int" || n == "integer") return core::Type::Int(32, true);
        if (n == "i64" || n == "long")                return core::Type::Int(64, true);
        if (n == "u8")     return core::Type::Int(8, false);
        if (n == "u16")    return core::Type::Int(16, false);
        if (n == "u32")    return core::Type::Int(32, false);
        if (n == "u64")    return core::Type::Int(64, false);
        if (n == "f32" || n == "float")               return core::Type::Float(32);
        if (n == "f64" || n == "double")              return core::Type::Float(64);
        if (n == "boolean" || n == "Boolean" || n == "bool") return core::Type::Bool();
        if (n == "string" || n == "String")           return core::Type::String();
        if (n == "void" || n == "undefined" || n == "null") return core::Type::Void();
        if (n == "bigint" || n == "BigInt")           return core::Type::Int(64, true);
        if (n == "any" || n == "unknown" || n == "object" || n == "Object" || n == "*")
            return core::Type::Any();
        return core::Type{core::TypeKind::kClass, n, "javascript"};
    }
    if (auto gt = std::dynamic_pointer_cast<GenericType>(t)) {
        if (gt->name == "Array" || gt->name == "ReadonlyArray") {
            if (!gt->args.empty()) {
                return core::Type::Array(JsTypeToCore(gt->args[0]));
            }
            return core::Type{core::TypeKind::kArray, "Array", "javascript"};
        }
        if (gt->name == "Promise" && !gt->args.empty()) {
            return JsTypeToCore(gt->args[0]);
        }
        if (gt->name == "Map" || gt->name == "Set") {
            return core::Type{core::TypeKind::kClass, gt->name, "javascript"};
        }
        return core::Type{core::TypeKind::kClass, gt->name, "javascript"};
    }
    if (auto un = std::dynamic_pointer_cast<UnionType>(t)) {
        // Reduce union to the first non-null/undefined option.
        for (auto &opt : un->options) {
            if (auto nt = std::dynamic_pointer_cast<NamedType>(opt)) {
                if (nt->name == "null" || nt->name == "undefined") continue;
            }
            return JsTypeToCore(opt);
        }
        return core::Type::Any();
    }
    return core::Type::Any();
}

void ExtractFromFunction(
    const std::string &fn_name,
    const std::vector<ArrowFunction::Param> &params,
    const std::shared_ptr<TypeNode> &return_type,
    const std::string &module_name,
    const std::string &class_name,
    std::vector<frontends::ForeignFunctionSignature> &out) {
    frontends::ForeignFunctionSignature sig;
    sig.name = fn_name;
    if (!class_name.empty()) {
        sig.qualified_name = module_name.empty()
            ? class_name + "." + fn_name
            : module_name + "." + class_name + "." + fn_name;
        sig.is_method = true;
        sig.class_name = class_name;
    } else {
        sig.qualified_name = module_name.empty()
            ? fn_name
            : module_name + "." + fn_name;
    }
    sig.return_type = return_type ? JsTypeToCore(return_type) : core::Type::Any();
    sig.has_type_annotations = false;
    for (const auto &p : params) {
        if (p.rest) continue;
        sig.param_types.push_back(JsTypeToCore(p.type));
        sig.param_names.push_back(p.name);
        if (p.type) sig.has_type_annotations = true;
    }
    out.push_back(std::move(sig));
}

}  // namespace

std::vector<frontends::ForeignFunctionSignature>
JsLanguageFrontend::ExtractSignatures(
    const std::string &source, const std::string &filename,
    const std::string &module_name) const {
    std::vector<frontends::ForeignFunctionSignature> result;

    frontends::Diagnostics diags;
    JsLexer lexer(source, filename);
    JsParser parser(lexer, diags);
    parser.ParseModule();
    auto module = parser.TakeModule();
    if (!module) return result;

    auto walk = [&](auto &&self, const std::shared_ptr<Statement> &s) -> void {
        if (auto fd = std::dynamic_pointer_cast<FunctionDecl>(s)) {
            if (!fd->name.empty()) {
                ExtractFromFunction(fd->name, fd->params, fd->return_type,
                                    module_name, "", result);
            }
        } else if (auto cd = std::dynamic_pointer_cast<ClassDecl>(s)) {
            if (cd->name.empty()) return;
            for (auto &m : cd->members) {
                if (auto md = std::dynamic_pointer_cast<MethodDecl>(m)) {
                    if (md->kind == MethodDecl::Kind::kConstructor) {
                        ExtractFromFunction("constructor", md->params,
                                            md->return_type, module_name,
                                            cd->name, result);
                    } else if (!md->name.empty() && !md->is_private) {
                        ExtractFromFunction(md->name, md->params,
                                            md->return_type, module_name,
                                            cd->name, result);
                    }
                }
            }
        } else if (auto exp = std::dynamic_pointer_cast<ExportDecl>(s)) {
            if (exp->declaration) self(self, exp->declaration);
        }
    };

    for (auto &s : module->body) walk(walk, s);
    return result;
}

}  // namespace polyglot::javascript
