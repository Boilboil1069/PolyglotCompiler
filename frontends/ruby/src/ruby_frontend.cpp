/**
 * @file     ruby_frontend.cpp
 * @brief    Ruby language frontend adapter
 *
 * @ingroup  Frontend / Ruby
 * @author   Manning Cyrus
 * @date     2026-04-26
 */
#include <filesystem>

#include "frontends/common/include/frontend_registry.h"
#include "frontends/common/include/sema_context.h"
#include "frontends/ruby/include/ruby_ast.h"
#include "frontends/ruby/include/ruby_frontend.h"
#include "frontends/ruby/include/ruby_import_resolver.h"
#include "frontends/ruby/include/ruby_lexer.h"
#include "frontends/ruby/include/ruby_lowering.h"
#include "frontends/ruby/include/ruby_parser.h"
#include "frontends/ruby/include/ruby_sema.h"

namespace polyglot::ruby {

REGISTER_FRONTEND(std::make_shared<RubyLanguageFrontend>());

std::vector<frontends::Token> RubyLanguageFrontend::Tokenize(const std::string &source,
                                                             const std::string &filename) const {
  RbLexer lex(source, filename);
  std::vector<frontends::Token> out;
  while (true) {
    auto t = lex.NextToken();
    if (t.kind == frontends::TokenKind::kEndOfFile)
      break;
    out.push_back(t);
  }
  return out;
}

namespace {

// Walk the module body and pull out every top-level require/require_relative/
// load/autoload call so the resolver can be driven uniformly.
struct RbRequire {
  std::string specifier;
  bool relative{false};
};
void CollectRequires(const std::vector<std::shared_ptr<Statement>> &body,
                     std::vector<RbRequire> &out) {
  for (const auto &stmt : body) {
    auto es = std::dynamic_pointer_cast<ExprStmt>(stmt);
    if (!es)
      continue;
    auto call = std::dynamic_pointer_cast<CallExpr>(es->expr);
    if (!call)
      continue;
    if (call->receiver)
      continue; // only bare-method form
    const std::string &m = call->method;
    bool is_require = (m == "require" || m == "load");
    bool is_relative = (m == "require_relative");
    bool is_autoload = (m == "autoload");
    if (!is_require && !is_relative && !is_autoload)
      continue;
    // First string arg is the path; for autoload it is the second arg.
    std::size_t idx = is_autoload ? 1 : 0;
    if (call->args.size() <= idx)
      continue;
    auto lit = std::dynamic_pointer_cast<Literal>(call->args[idx]);
    if (!lit)
      continue;
    if (lit->kind != Literal::Kind::kString && lit->kind != Literal::Kind::kSymbol)
      continue;
    out.push_back({lit->value, is_relative});
  }
}

} // namespace

bool RubyLanguageFrontend::Analyze(const std::string &source, const std::string &filename,
                                   frontends::Diagnostics &d,
                                   const frontends::FrontendOptions &opts) const {
  RbLexer lex(source, filename);
  RbParser p(lex, d);
  p.ParseModule();
  if (d.HasErrors())
    return false;
  auto m = p.TakeModule();
  if (!m)
    return false;
  frontends::SemaContext ctx(d);
  AnalyzeModule(*m, ctx);
  if (d.HasErrors())
    return false;
  // Resolve `require` / `require_relative` / `load` / `autoload` to real
  // .rb files and inject their top-level definitions as external symbols.
  std::string importer_dir = std::filesystem::path(filename).parent_path().string();
  RbImportResolver resolver(opts.ruby_project_dir, opts.gem_paths, d);
  std::vector<RbRequire> requires_;
  CollectRequires(m->body, requires_);
  for (const auto &r : requires_) {
    const RbFile *rf = resolver.Resolve(r.specifier, importer_dir, r.relative);
    if (!rf) {
      if (opts.strict)
        return false;
      continue;
    }
    for (const auto &kv : rf->exports) {
      core::Symbol s;
      s.name = kv.second.qualified_name.empty() ? kv.second.name : kv.second.qualified_name;
      s.kind = (kv.second.kind == RbSymbolKind::kMethod) ? core::SymbolKind::kFunction
               : (kv.second.kind == RbSymbolKind::kClass || kv.second.kind == RbSymbolKind::kModule)
                   ? core::SymbolKind::kTypeName
                   : core::SymbolKind::kVariable;
      s.type = kv.second.return_type;
      s.language = "ruby";
      s.access = "external";
      ctx.Symbols().Declare(s);
    }
  }
  return !d.HasErrors();
}

frontends::FrontendResult RubyLanguageFrontend::Lower(
    const std::string &source, const std::string &filename, ir::IRContext &ir_ctx,
    frontends::Diagnostics &d, const frontends::FrontendOptions &opts) const {
  frontends::FrontendResult r;
  RbLexer lex(source, filename);
  RbParser p(lex, d);
  p.ParseModule();
  auto m = p.TakeModule();
  if (!m || d.HasErrors())
    return r;
  frontends::SemaContext ctx(d);
  AnalyzeModule(*m, ctx);
  if (d.HasErrors())
    return r;
  // Run the same require resolution pass so the lowered IR can reference
  // external Ruby symbols by their fully qualified name.
  std::string importer_dir = std::filesystem::path(filename).parent_path().string();
  RbImportResolver resolver(opts.ruby_project_dir, opts.gem_paths, d);
  std::vector<RbRequire> requires_;
  CollectRequires(m->body, requires_);
  for (const auto &rq : requires_) {
    const RbFile *rf = resolver.Resolve(rq.specifier, importer_dir, rq.relative);
    if (!rf && opts.strict) {
      r.lowered = false;
      r.success = false;
      return r;
    }
  }
  LowerToIR(*m, ir_ctx, d);
  r.lowered = true;
  r.success = !d.HasErrors();
  return r;
}

namespace {

core::Type RbTypeToCore(const std::shared_ptr<TypeNode> &t) {
  if (!t)
    return core::Type::Any();
  const std::string &n = t->name;
  if (n == "Integer" || n == "Fixnum" || n == "Bignum")
    return core::Type::Int(64, true);
  if (n == "Float")
    return core::Type::Float(64);
  if (n == "Numeric")
    return core::Type::Float(64);
  if (n == "String")
    return core::Type::String();
  if (n == "Symbol")
    return core::Type::String();
  if (n == "TrueClass" || n == "FalseClass" || n == "Boolean")
    return core::Type::Bool();
  if (n == "NilClass" || n == "Nil")
    return core::Type::Void();
  if (n == "Array")
    return core::Type{core::TypeKind::kArray, "Array", "ruby"};
  if (n == "Hash")
    return core::Type{core::TypeKind::kStruct, "Hash", "ruby"};
  if (n == "Object")
    return core::Type::Any();
  return core::Type{core::TypeKind::kClass, n, "ruby"};
}

void Visit(const std::shared_ptr<Statement> &s, const std::string &module_name,
           const std::string &prefix, std::vector<frontends::ForeignFunctionSignature> &out) {
  if (auto m = std::dynamic_pointer_cast<MethodDecl>(s)) {
    frontends::ForeignFunctionSignature sig;
    sig.name = m->name;
    sig.has_type_annotations = (m->return_type != nullptr);
    for (auto &p : m->params) {
      if (p.splat || p.double_splat || p.block)
        continue;
      sig.param_types.push_back(RbTypeToCore(p.type));
      sig.param_names.push_back(p.name);
      if (p.type)
        sig.has_type_annotations = true;
    }
    sig.return_type = m->return_type ? RbTypeToCore(m->return_type) : core::Type::Any();
    if (!prefix.empty()) {
      sig.is_method = true;
      sig.class_name = prefix;
      sig.qualified_name =
          (module_name.empty() ? "" : module_name + "::") + prefix + "::" + m->name;
    } else {
      sig.qualified_name = (module_name.empty() ? "" : module_name + "::") + m->name;
    }
    out.push_back(std::move(sig));
    return;
  }
  if (auto c = std::dynamic_pointer_cast<ClassDecl>(s)) {
    std::string p = prefix.empty() ? c->name : prefix + "::" + c->name;
    for (auto &b : c->body)
      Visit(b, module_name, p, out);
    return;
  }
  if (auto md = std::dynamic_pointer_cast<ModuleDecl>(s)) {
    std::string p = prefix.empty() ? md->name : prefix + "::" + md->name;
    for (auto &b : md->body)
      Visit(b, module_name, p, out);
    return;
  }
}

} // namespace

std::vector<frontends::ForeignFunctionSignature> RubyLanguageFrontend::ExtractSignatures(
    const std::string &source, const std::string &filename, const std::string &module_name) const {
  std::vector<frontends::ForeignFunctionSignature> out;
  frontends::Diagnostics d;
  RbLexer lex(source, filename);
  RbParser p(lex, d);
  p.ParseModule();
  auto m = p.TakeModule();
  if (!m)
    return out;
  for (auto &s : m->body)
    Visit(s, module_name, "", out);
  return out;
}

} // namespace polyglot::ruby
