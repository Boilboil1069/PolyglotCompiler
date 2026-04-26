/**
 * @file     go_frontend.cpp
 * @brief    Go language frontend adapter
 *
 * @ingroup  Frontend / Go
 * @author   Manning Cyrus
 * @date     2026-04-26
 */
#include "frontends/go/include/go_frontend.h"

#include "frontends/common/include/frontend_registry.h"
#include "frontends/common/include/sema_context.h"
#include "frontends/go/include/go_ast.h"
#include "frontends/go/include/go_lexer.h"
#include "frontends/go/include/go_lowering.h"
#include "frontends/go/include/go_parser.h"
#include "frontends/go/include/go_sema.h"

namespace polyglot::go {

REGISTER_FRONTEND(std::make_shared<GoLanguageFrontend>());

std::vector<frontends::Token> GoLanguageFrontend::Tokenize(
    const std::string &source, const std::string &filename) const {
    GoLexer lex(source, filename);
    std::vector<frontends::Token> out;
    while (true) {
        auto t = lex.NextToken();
        if (t.kind == frontends::TokenKind::kEndOfFile) break;
        out.push_back(t);
    }
    return out;
}

bool GoLanguageFrontend::Analyze(
    const std::string &source, const std::string &filename,
    frontends::Diagnostics &d, const frontends::FrontendOptions &) const {
    GoLexer lex(source, filename);
    GoParser p(lex, d);
    p.ParseFile();
    if (d.HasErrors()) return false;
    auto f = p.TakeFile();
    if (!f) return false;
    frontends::SemaContext ctx(d);
    AnalyzeFile(*f, ctx);
    return !d.HasErrors();
}

frontends::FrontendResult GoLanguageFrontend::Lower(
    const std::string &source, const std::string &filename,
    ir::IRContext &ir_ctx, frontends::Diagnostics &d,
    const frontends::FrontendOptions &) const {
    frontends::FrontendResult r;
    GoLexer lex(source, filename);
    GoParser p(lex, d);
    p.ParseFile();
    auto f = p.TakeFile();
    if (!f || d.HasErrors()) return r;
    frontends::SemaContext ctx(d);
    AnalyzeFile(*f, ctx);
    if (d.HasErrors()) return r;
    LowerToIR(*f, ir_ctx, d);
    r.lowered = true;
    r.success = !d.HasErrors();
    return r;
}

namespace {

core::Type GoTypeToCore(const std::shared_ptr<TypeNode> &t) {
    if (!t) return core::Type::Any();
    if (t->kind == TypeKind::kPointer) {
        core::Type p{core::TypeKind::kPointer, "ptr"};
        p.type_args.push_back(GoTypeToCore(t->elem));
        return p;
    }
    if (t->kind == TypeKind::kSlice) return core::Type::Slice(GoTypeToCore(t->elem));
    if (t->kind == TypeKind::kArray) return core::Type::Array(GoTypeToCore(t->elem));
    if (t->kind == TypeKind::kMap) {
        return core::Type{core::TypeKind::kStruct, "map", "go"};
    }
    if (t->kind == TypeKind::kChan)      return core::Type{core::TypeKind::kClass, "chan", "go"};
    if (t->kind == TypeKind::kFunc)      return core::Type{core::TypeKind::kFunction, "func", "go"};
    if (t->kind == TypeKind::kInterface) return core::Type::Any();
    if (t->kind == TypeKind::kStruct)    return core::Type{core::TypeKind::kStruct, t->name, "go"};
    if (t->kind != TypeKind::kNamed)     return core::Type::Any();
    const std::string &n = t->name;
    if (n == "bool")    return core::Type::Bool();
    if (n == "int8")    return core::Type::Int(8, true);
    if (n == "int16")   return core::Type::Int(16, true);
    if (n == "int32" || n == "rune") return core::Type::Int(32, true);
    if (n == "int" || n == "int64")  return core::Type::Int(64, true);
    if (n == "uint8" || n == "byte") return core::Type::Int(8, false);
    if (n == "uint16")  return core::Type::Int(16, false);
    if (n == "uint32")  return core::Type::Int(32, false);
    if (n == "uint" || n == "uint64" || n == "uintptr") return core::Type::Int(64, false);
    if (n == "float32") return core::Type::Float(32);
    if (n == "float64") return core::Type::Float(64);
    if (n == "string")  return core::Type::String();
    if (n == "error")   return core::Type{core::TypeKind::kClass, "error", "go"};
    return core::Type{core::TypeKind::kClass, n, "go"};
}

}  // namespace

std::vector<frontends::ForeignFunctionSignature>
GoLanguageFrontend::ExtractSignatures(const std::string &source,
                                      const std::string &filename,
                                      const std::string &module_name) const {
    std::vector<frontends::ForeignFunctionSignature> out;
    frontends::Diagnostics d;
    GoLexer lex(source, filename);
    GoParser p(lex, d);
    p.ParseFile();
    auto f = p.TakeFile();
    if (!f) return out;
    std::string mod = module_name.empty() ? f->package_name : module_name;
    for (auto &fn : f->funcs) {
        if (!fn) continue;
        // Only export capitalised names (Go's convention) — but include all
        // for cross-language access.
        frontends::ForeignFunctionSignature sig;
        sig.name = fn->name;
        for (auto &p : fn->params) {
            sig.param_types.push_back(GoTypeToCore(p.second));
            sig.param_names.push_back(p.first);
        }
        if (fn->results.size() == 1)
            sig.return_type = GoTypeToCore(fn->results.front().second);
        else if (fn->results.empty())
            sig.return_type = core::Type::Void();
        else
            sig.return_type = GoTypeToCore(fn->results.front().second);
        sig.has_type_annotations = true;
        sig.is_method = fn->receiver.has_value();
        if (sig.is_method && fn->receiver->type) {
            std::string recv = fn->receiver->type->name;
            if (fn->receiver->type->kind == TypeKind::kPointer && fn->receiver->type->elem)
                recv = fn->receiver->type->elem->name;
            sig.class_name = recv;
            sig.qualified_name = (mod.empty() ? "" : mod + ".") + recv + "." + fn->name;
        } else {
            sig.qualified_name = (mod.empty() ? "" : mod + ".") + fn->name;
        }
        out.push_back(std::move(sig));
    }
    return out;
}

}  // namespace polyglot::go
