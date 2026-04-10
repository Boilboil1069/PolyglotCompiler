/**
 * @file     python_frontend.cpp
 * @brief    Python language frontend adapter implementation
 *
 * @ingroup  Frontend / Python
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include "frontends/python/include/python_frontend.h"

#include "frontends/common/include/frontend_registry.h"
#include "frontends/common/include/sema_context.h"
#include "frontends/python/include/python_lexer.h"
#include "frontends/python/include/python_parser.h"
#include "frontends/python/include/python_sema.h"
#include "frontends/python/include/python_lowering.h"

namespace polyglot::python {

// ============================================================================
// Auto-registration
// ============================================================================

REGISTER_FRONTEND(std::make_shared<PythonLanguageFrontend>());

// ============================================================================
// Tokenize
// ============================================================================

std::vector<frontends::Token> PythonLanguageFrontend::Tokenize(
    const std::string &source, const std::string &filename) const {
    PythonLexer lexer(source, filename);
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

bool PythonLanguageFrontend::Analyze(
    const std::string &source,
    const std::string &filename,
    frontends::Diagnostics &diagnostics,
    const frontends::FrontendOptions & /*options*/) const {

    PythonLexer lexer(source, filename);
    PythonParser parser(lexer, diagnostics);
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

frontends::FrontendResult PythonLanguageFrontend::Lower(
    const std::string &source,
    const std::string &filename,
    ir::IRContext &ir_ctx,
    frontends::Diagnostics &diagnostics,
    const frontends::FrontendOptions & /*options*/) const {

    frontends::FrontendResult result;

    PythonLexer lexer(source, filename);
    PythonParser parser(lexer, diagnostics);
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
// ExtractSignatures — parse Python source and extract function signatures
// ============================================================================

namespace {

/// Map a Python type annotation (Expression AST node) to a core::Type.
core::Type PyAnnotationToCore(const std::shared_ptr<Expression> &expr) {
    if (!expr) return core::Type::Any();

    if (auto id = std::dynamic_pointer_cast<Identifier>(expr)) {
        const std::string &n = id->name;
        if (n == "int")          return core::Type::Int();
        if (n == "float")        return core::Type::Float();
        if (n == "str")          return core::Type::String();
        if (n == "bool")         return core::Type::Bool();
        if (n == "None")         return core::Type::Void();
        if (n == "bytes")        return core::Type{core::TypeKind::kSlice, "bytes", "python"};
        if (n == "object")       return core::Type::Any();
        // Class or other named type
        return core::Type{core::TypeKind::kClass, n, "python"};
    }
    if (auto attr = std::dynamic_pointer_cast<AttributeExpression>(expr)) {
        // e.g. np.ndarray → treat as class
        std::string name;
        if (auto base = std::dynamic_pointer_cast<Identifier>(attr->object))
            name = base->name + ".";
        name += attr->attribute;
        return core::Type{core::TypeKind::kClass, name, "python"};
    }
    if (auto idx = std::dynamic_pointer_cast<IndexExpression>(expr)) {
        // e.g. List[int], Optional[str], Dict[str, int]
        if (auto base = std::dynamic_pointer_cast<Identifier>(idx->object)) {
            if (base->name == "Optional") return PyAnnotationToCore(idx->index);
            if (base->name == "List" || base->name == "list")
                return core::Type{core::TypeKind::kArray, "list", "python"};
            if (base->name == "Dict" || base->name == "dict")
                return core::Type{core::TypeKind::kStruct, "dict", "python"};
            if (base->name == "Tuple" || base->name == "tuple")
                return core::Type{core::TypeKind::kStruct, "tuple", "python"};
            if (base->name == "Set" || base->name == "set")
                return core::Type{core::TypeKind::kArray, "set", "python"};
        }
    }
    return core::Type::Any();
}

/// Infer the return type of a Python function from its body by scanning for
/// return statements.  Returns Any if nothing can be inferred.
core::Type InferReturnType(const std::vector<std::shared_ptr<Statement>> &body) {
    for (const auto &stmt : body) {
        if (auto ret = std::dynamic_pointer_cast<ReturnStatement>(stmt)) {
            if (!ret->value) return core::Type::Void();
            // Try to infer from the value expression
            if (auto lit = std::dynamic_pointer_cast<Literal>(ret->value)) {
                if (lit->is_string) return core::Type::String();
                const std::string &v = lit->value;
                if (v == "True" || v == "False") return core::Type::Bool();
                if (v == "None") return core::Type::Void();
                if (v.find('.') != std::string::npos) return core::Type::Float();
                // Assume integer for numeric literals
                if (!v.empty() && (std::isdigit(v[0]) || v[0] == '-'))
                    return core::Type::Int();
            }
            // f-string or string operations → string
            // For now, return Any for complex expressions
            return core::Type::Any();
        }
    }
    return core::Type::Void();  // No return statement → implicitly None
}

/// Infer a parameter's type from its default value.
core::Type InferFromDefault(const std::shared_ptr<Expression> &default_val) {
    if (!default_val) return core::Type::Any();
    if (auto lit = std::dynamic_pointer_cast<Literal>(default_val)) {
        if (lit->is_string) return core::Type::String();
        const std::string &v = lit->value;
        if (v == "True" || v == "False") return core::Type::Bool();
        if (v == "None") return core::Type::Any();
        if (v.find('.') != std::string::npos) return core::Type::Float();
        if (!v.empty() && (std::isdigit(v[0]) || v[0] == '-'))
            return core::Type::Int();
    }
    if (auto list_expr = std::dynamic_pointer_cast<ListExpression>(default_val)) {
        return core::Type{core::TypeKind::kArray, "list", "python"};
    }
    if (auto dict_expr = std::dynamic_pointer_cast<DictExpression>(default_val)) {
        return core::Type{core::TypeKind::kStruct, "dict", "python"};
    }
    return core::Type::Any();
}

void ExtractFromFunctionDef(const FunctionDef &fn,
                            const std::string &module_name,
                            const std::string &class_name,
                            std::vector<frontends::ForeignFunctionSignature> &out) {
    frontends::ForeignFunctionSignature sig;
    sig.name = fn.name;
    if (!class_name.empty()) {
        sig.qualified_name = module_name.empty()
            ? class_name + "::" + fn.name
            : module_name + "::" + class_name + "::" + fn.name;
        sig.is_method = true;
        sig.class_name = class_name;
    } else {
        sig.qualified_name = module_name.empty() ? fn.name
                                                 : module_name + "::" + fn.name;
    }

    // Return type: annotation first, then infer from body
    bool has_annotations = false;
    if (fn.return_annotation) {
        sig.return_type = PyAnnotationToCore(fn.return_annotation);
        has_annotations = true;
    } else {
        sig.return_type = InferReturnType(fn.body);
    }

    // Parameters
    for (const auto &p : fn.params) {
        // Skip 'self' for methods
        if (sig.is_method && p.name == "self") continue;
        if (p.annotation) {
            sig.param_types.push_back(PyAnnotationToCore(p.annotation));
            has_annotations = true;
        } else if (p.default_value) {
            sig.param_types.push_back(InferFromDefault(p.default_value));
        } else {
            sig.param_types.push_back(core::Type::Any());
        }
        sig.param_names.push_back(p.name);
    }

    sig.has_type_annotations = has_annotations;
    out.push_back(std::move(sig));
}

}  // namespace

std::vector<frontends::ForeignFunctionSignature> PythonLanguageFrontend::ExtractSignatures(
    const std::string &source,
    const std::string &filename,
    const std::string &module_name) const {

    std::vector<frontends::ForeignFunctionSignature> result;

    frontends::Diagnostics diags;
    PythonLexer lexer(source, filename);
    PythonParser parser(lexer, diags);
    parser.ParseModule();
    auto module = parser.TakeModule();
    if (!module) return result;

    for (const auto &stmt : module->body) {
        // Top-level functions
        if (auto fn = std::dynamic_pointer_cast<FunctionDef>(stmt)) {
            ExtractFromFunctionDef(*fn, module_name, "", result);
        }
        // Class methods
        if (auto cls = std::dynamic_pointer_cast<ClassDef>(stmt)) {
            for (const auto &member : cls->body) {
                if (auto method = std::dynamic_pointer_cast<FunctionDef>(member)) {
                    ExtractFromFunctionDef(*method, module_name, cls->name, result);
                }
            }
        }
    }

    return result;
}

}  // namespace polyglot::python
