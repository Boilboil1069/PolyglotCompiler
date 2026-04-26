/**
 * @file     ruby_frontend.cpp
 * @brief    Ruby language frontend adapter
 *
 * @ingroup  Frontend / Ruby
 * @author   Manning Cyrus
 * @date     2026-04-26
 */
#include "frontends/ruby/include/ruby_frontend.h"

#include "frontends/common/include/frontend_registry.h"
#include "frontends/common/include/sema_context.h"
#include "frontends/ruby/include/ruby_ast.h"
#include "frontends/ruby/include/ruby_lexer.h"
#include "frontends/ruby/include/ruby_lowering.h"
#include "frontends/ruby/include/ruby_parser.h"
#include "frontends/ruby/include/ruby_sema.h"

namespace polyglot::ruby {

REGISTER_FRONTEND(std::make_shared<RubyLanguageFrontend>());

std::vector<frontends::Token> RubyLanguageFrontend::Tokenize(
    const std::string &source, const std::string &filename) const {
    RbLexer lex(source, filename);
    std::vector<frontends::Token> out;
    while (true) {
        auto t = lex.NextToken();
        if (t.kind == frontends::TokenKind::kEndOfFile) break;
        out.push_back(t);
    }
    return out;
}

bool RubyLanguageFrontend::Analyze(
    const std::string &source, const std::string &filename,
    frontends::Diagnostics &d, const frontends::FrontendOptions &) const {
    RbLexer lex(source, filename);
    RbParser p(lex, d);
    p.ParseModule();
    if (d.HasErrors()) return false;
    auto m = p.TakeModule();
    if (!m) return false;
    frontends::SemaContext ctx(d);
    AnalyzeModule(*m, ctx);
    return !d.HasErrors();
}

frontends::FrontendResult RubyLanguageFrontend::Lower(
    const std::string &source, const std::string &filename,
    ir::IRContext &ir_ctx, frontends::Diagnostics &d,
    const frontends::FrontendOptions &) const {
    frontends::FrontendResult r;
    RbLexer lex(source, filename);
    RbParser p(lex, d);
    p.ParseModule();
    auto m = p.TakeModule();
    if (!m || d.HasErrors()) return r;
    frontends::SemaContext ctx(d);
    AnalyzeModule(*m, ctx);
    if (d.HasErrors()) return r;
    LowerToIR(*m, ir_ctx, d);
    r.lowered = true;
    r.success = !d.HasErrors();
    return r;
}

namespace {

core::Type RbTypeToCore(const std::shared_ptr<TypeNode> &t) {
    if (!t) return core::Type::Any();
    const std::string &n = t->name;
    if (n == "Integer" || n == "Fixnum" || n == "Bignum") return core::Type::Int(64, true);
    if (n == "Float") return core::Type::Float(64);
    if (n == "Numeric") return core::Type::Float(64);
    if (n == "String") return core::Type::String();
    if (n == "Symbol") return core::Type::String();
    if (n == "TrueClass" || n == "FalseClass" || n == "Boolean") return core::Type::Bool();
    if (n == "NilClass" || n == "Nil") return core::Type::Void();
    if (n == "Array") return core::Type{core::TypeKind::kArray, "Array", "ruby"};
    if (n == "Hash")  return core::Type{core::TypeKind::kStruct, "Hash", "ruby"};
    if (n == "Object") return core::Type::Any();
    return core::Type{core::TypeKind::kClass, n, "ruby"};
}

void Visit(const std::shared_ptr<Statement> &s, const std::string &module_name,
           const std::string &prefix,
           std::vector<frontends::ForeignFunctionSignature> &out) {
    if (auto m = std::dynamic_pointer_cast<MethodDecl>(s)) {
        frontends::ForeignFunctionSignature sig;
        sig.name = m->name;
        sig.has_type_annotations = (m->return_type != nullptr);
        for (auto &p : m->params) {
            if (p.splat || p.double_splat || p.block) continue;
            sig.param_types.push_back(RbTypeToCore(p.type));
            sig.param_names.push_back(p.name);
            if (p.type) sig.has_type_annotations = true;
        }
        sig.return_type = m->return_type ? RbTypeToCore(m->return_type) : core::Type::Any();
        if (!prefix.empty()) {
            sig.is_method = true;
            sig.class_name = prefix;
            sig.qualified_name = (module_name.empty() ? "" : module_name + "::") +
                                 prefix + "::" + m->name;
        } else {
            sig.qualified_name = (module_name.empty() ? "" : module_name + "::") + m->name;
        }
        out.push_back(std::move(sig));
        return;
    }
    if (auto c = std::dynamic_pointer_cast<ClassDecl>(s)) {
        std::string p = prefix.empty() ? c->name : prefix + "::" + c->name;
        for (auto &b : c->body) Visit(b, module_name, p, out);
        return;
    }
    if (auto md = std::dynamic_pointer_cast<ModuleDecl>(s)) {
        std::string p = prefix.empty() ? md->name : prefix + "::" + md->name;
        for (auto &b : md->body) Visit(b, module_name, p, out);
        return;
    }
}

}  // namespace

std::vector<frontends::ForeignFunctionSignature>
RubyLanguageFrontend::ExtractSignatures(const std::string &source,
                                         const std::string &filename,
                                         const std::string &module_name) const {
    std::vector<frontends::ForeignFunctionSignature> out;
    frontends::Diagnostics d;
    RbLexer lex(source, filename);
    RbParser p(lex, d);
    p.ParseModule();
    auto m = p.TakeModule();
    if (!m) return out;
    for (auto &s : m->body) Visit(s, module_name, "", out);
    return out;
}

}  // namespace polyglot::ruby
