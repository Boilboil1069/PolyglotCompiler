/**
 * @file     rust_frontend.cpp
 * @brief    Rust language frontend adapter implementation
 *
 * @ingroup  Frontend / Rust
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include "frontends/rust/include/rust_frontend.h"

#include "frontends/common/include/frontend_registry.h"
#include "frontends/common/include/sema_context.h"
#include "frontends/rust/include/rust_ast.h"
#include "frontends/rust/include/rust_lexer.h"
#include "frontends/rust/include/rust_parser.h"
#include "frontends/rust/include/rust_sema.h"
#include "frontends/rust/include/rust_lowering.h"

namespace polyglot::rust {

// ============================================================================
// Auto-registration
// ============================================================================

REGISTER_FRONTEND(std::make_shared<RustLanguageFrontend>());

// ============================================================================
// Tokenize
// ============================================================================

std::vector<frontends::Token> RustLanguageFrontend::Tokenize(
    const std::string &source, const std::string &filename) const {
    RustLexer lexer(source, filename);
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

bool RustLanguageFrontend::Analyze(
    const std::string &source,
    const std::string &filename,
    frontends::Diagnostics &diagnostics,
    const frontends::FrontendOptions & /*options*/) const {

    RustLexer lexer(source, filename);
    RustParser parser(lexer, diagnostics);
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

frontends::FrontendResult RustLanguageFrontend::Lower(
    const std::string &source,
    const std::string &filename,
    ir::IRContext &ir_ctx,
    frontends::Diagnostics &diagnostics,
    const frontends::FrontendOptions & /*options*/) const {

    frontends::FrontendResult result;

    RustLexer lexer(source, filename);
    RustParser parser(lexer, diagnostics);
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
// ExtractSignatures — parse Rust source and extract function signatures
// ============================================================================

namespace {

/// Map a Rust TypeNode to a core::Type.
core::Type RustTypeToCore(const std::shared_ptr<TypeNode> &tn) {
    if (!tn) return core::Type::Void();  // no return type → ()

    if (auto tp = std::dynamic_pointer_cast<TypePath>(tn)) {
        // Flatten path segments to a single name
        std::string name;
        for (size_t i = 0; i < tp->segments.size(); ++i) {
            if (i > 0) name += "::";
            name += tp->segments[i];
        }
        // Common primitive type names
        if (name == "i8")    return core::Type::Int(8, true);
        if (name == "i16")   return core::Type::Int(16, true);
        if (name == "i32")   return core::Type::Int(32, true);
        if (name == "i64")   return core::Type::Int(64, true);
        if (name == "i128")  return core::Type::Int(64, true);
        if (name == "isize") return core::Type::Int(64, true);
        if (name == "u8")    return core::Type::Int(8, false);
        if (name == "u16")   return core::Type::Int(16, false);
        if (name == "u32")   return core::Type::Int(32, false);
        if (name == "u64")   return core::Type::Int(64, false);
        if (name == "u128")  return core::Type::Int(64, false);
        if (name == "usize") return core::Type::Int(64, false);
        if (name == "f32")   return core::Type::Float(32);
        if (name == "f64")   return core::Type::Float(64);
        if (name == "bool")  return core::Type::Bool();
        if (name == "char")  return core::Type::Int(32, false);
        if (name == "String" || name == "str")
            return core::Type::String();
        if (name == "Vec")
            return core::Type{core::TypeKind::kArray, "Vec", "rust"};
        if (name == "HashMap" || name == "std::collections::HashMap")
            return core::Type{core::TypeKind::kStruct, "HashMap", "rust"};
        if (name == "Option") {
            // Try to unwrap the generic arg
            if (!tp->generic_args.empty() && !tp->generic_args[0].empty())
                return RustTypeToCore(tp->generic_args[0][0]);
            return core::Type::Any();
        }
        if (name == "Result") {
            if (!tp->generic_args.empty() && !tp->generic_args[0].empty())
                return RustTypeToCore(tp->generic_args[0][0]);
            return core::Type::Any();
        }
        if (name == "Box") {
            if (!tp->generic_args.empty() && !tp->generic_args[0].empty())
                return RustTypeToCore(tp->generic_args[0][0]);
            return core::Type::Any();
        }
        return core::Type{core::TypeKind::kClass, name, "rust"};
    }
    if (auto rt = std::dynamic_pointer_cast<ReferenceType>(tn)) {
        return RustTypeToCore(rt->inner);  // &T → T for cross-lang purposes
    }
    if (auto st = std::dynamic_pointer_cast<SliceType>(tn)) {
        return core::Type{core::TypeKind::kSlice, "slice", "rust"};
    }
    if (auto at = std::dynamic_pointer_cast<ArrayType>(tn)) {
        return core::Type{core::TypeKind::kArray, "array", "rust"};
    }
    if (auto tt = std::dynamic_pointer_cast<TupleType>(tn)) {
        if (tt->elements.empty()) return core::Type::Void();  // () → void
        return core::Type{core::TypeKind::kStruct, "tuple", "rust"};
    }
    return core::Type::Any();
}

void ExtractFromFunction(const FunctionItem &fn,
                         const std::string &module_name,
                         const std::string &impl_type,
                         std::vector<frontends::ForeignFunctionSignature> &out) {
    frontends::ForeignFunctionSignature sig;
    sig.name = fn.name;
    sig.has_type_annotations = true;

    if (!impl_type.empty()) {
        sig.qualified_name = module_name.empty()
            ? impl_type + "::" + fn.name
            : module_name + "::" + impl_type + "::" + fn.name;
        sig.is_method = true;
        sig.class_name = impl_type;
    } else {
        sig.qualified_name = module_name.empty() ? fn.name
                                                 : module_name + "::" + fn.name;
    }

    sig.return_type = fn.return_type ? RustTypeToCore(fn.return_type)
                                     : core::Type::Void();

    for (const auto &p : fn.params) {
        // Skip self parameter
        if (p.name == "self" || p.name == "&self" || p.name == "&mut self")
            continue;
        sig.param_types.push_back(RustTypeToCore(p.type));
        sig.param_names.push_back(p.name);
    }

    out.push_back(std::move(sig));
}

}  // namespace

std::vector<frontends::ForeignFunctionSignature> RustLanguageFrontend::ExtractSignatures(
    const std::string &source,
    const std::string &filename,
    const std::string &module_name) const {

    std::vector<frontends::ForeignFunctionSignature> result;

    frontends::Diagnostics diags;
    RustLexer lexer(source, filename);
    RustParser parser(lexer, diags);
    parser.ParseModule();
    auto module = parser.TakeModule();
    if (!module) return result;

    for (const auto &item : module->items) {
        // Top-level functions
        if (auto fn = std::dynamic_pointer_cast<FunctionItem>(item)) {
            ExtractFromFunction(*fn, module_name, "", result);
        }
        // impl blocks
        if (auto impl = std::dynamic_pointer_cast<ImplItem>(item)) {
            std::string impl_type;
            if (auto tp = std::dynamic_pointer_cast<TypePath>(impl->target_type)) {
                for (size_t i = 0; i < tp->segments.size(); ++i) {
                    if (i > 0) impl_type += "::";
                    impl_type += tp->segments[i];
                }
            }
            for (const auto &member : impl->items) {
                if (auto fn = std::dynamic_pointer_cast<FunctionItem>(member)) {
                    ExtractFromFunction(*fn, module_name, impl_type, result);
                }
            }
        }
    }

    return result;
}

}  // namespace polyglot::rust
