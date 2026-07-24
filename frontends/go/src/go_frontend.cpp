/**
 * @file     go_frontend.cpp
 * @brief    Go language frontend adapter
 *
 * @ingroup  Frontend / Go
 * @author   Manning Cyrus
 * @date     2026-04-26
 */
#include "frontends/common/include/frontend_registry.h"
#include "frontends/common/include/sema_context.h"
#include "frontends/go/include/go_ast.h"
#include "frontends/go/include/go_frontend.h"
#include "frontends/go/include/go_import_resolver.h"
#include "frontends/go/include/go_lexer.h"
#include "frontends/go/include/go_lowering.h"
#include "frontends/go/include/go_parser.h"
#include "frontends/go/include/go_sema.h"

namespace polyglot::go {

REGISTER_FRONTEND(std::make_shared<GoLanguageFrontend>());

std::vector<frontends::Token> GoLanguageFrontend::Tokenize(const std::string &source,
                                                           const std::string &filename) const {
  GoLexer lex(source, filename);
  std::vector<frontends::Token> out;
  while (true) {
    auto t = lex.NextToken();
    if (t.kind == frontends::TokenKind::kEndOfFile)
      break;
    out.push_back(t);
  }
  return out;
}

bool GoLanguageFrontend::Analyze(const std::string &source, const std::string &filename,
                                 frontends::Diagnostics &d,
                                 const frontends::FrontendOptions &opts) const {
  GoLexer lex(source, filename);
  GoParser p(lex, d);
  p.SetGoVersion(opts.go_version);
  p.ParseFile();
  if (d.HasErrors())
    return false;
  auto f = p.TakeFile();
  if (!f)
    return false;
  frontends::SemaContext ctx(d);
  AnalyzeFile(*f, ctx);
  if (d.HasErrors())
    return false;
  // Resolve external package imports so that downstream consumers (e.g.
  // topology extraction, IR lowering) can verify cross-package symbols.
  GoImportResolver resolver(opts.go_project_dir, opts.go_module_paths, d);
  for (const auto &gd : f->decls) {
    if (gd.keyword != "import")
      continue;
    for (const auto &spec : gd.imports) {
      const GoPackage *pkg = resolver.Resolve(spec.path);
      if (!pkg) {
        if (opts.strict)
          return false;
        continue;
      }
      for (const auto &kv : pkg->exports) {
        core::Symbol s;
        s.name = (spec.alias.empty() ? pkg->package_name : spec.alias) + "." + kv.second.name;
        s.kind = (kv.second.kind == GoSymbolKind::kFunction) ? core::SymbolKind::kFunction
                 : (kv.second.kind == GoSymbolKind::kStruct ||
                    kv.second.kind == GoSymbolKind::kInterface ||
                    kv.second.kind == GoSymbolKind::kTypeAlias)
                     ? core::SymbolKind::kTypeName
                     : core::SymbolKind::kVariable;
        s.type = kv.second.return_type;
        s.language = "go";
        s.access = "external";
        ctx.Symbols().Declare(s);
      }
    }
  }
  return !d.HasErrors();
}

frontends::FrontendResult GoLanguageFrontend::Lower(const std::string &source,
                                                    const std::string &filename,
                                                    ir::IRContext &ir_ctx,
                                                    frontends::Diagnostics &d,
                                                    const frontends::FrontendOptions &opts) const {
  frontends::FrontendResult r;
  GoLexer lex(source, filename);
  GoParser p(lex, d);
  p.SetGoVersion(opts.go_version);
  p.ParseFile();
  auto f = p.TakeFile();
  if (!f || d.HasErrors())
    return r;
  frontends::SemaContext ctx(d);
  AnalyzeFile(*f, ctx);
  if (d.HasErrors())
    return r;
  // Run the same import resolution as Analyze() so the lowered IR can
  // reference external symbols by their qualified Go name.
  GoImportResolver resolver(opts.go_project_dir, opts.go_module_paths, d);
  for (const auto &gd : f->decls) {
    if (gd.keyword != "import")
      continue;
    for (const auto &spec : gd.imports) {
      const GoPackage *pkg = resolver.Resolve(spec.path);
      if (!pkg && opts.strict) {
        r.lowered = false;
        r.success = false;
        return r;
      }
    }
  }
  LowerToIR(*f, ir_ctx, d);
  r.lowered = true;
  r.success = !d.HasErrors();
  return r;
}

namespace {

core::Type GoTypeToCore(const std::shared_ptr<TypeNode> &t) {
  if (!t)
    return core::Type::Any();
  if (t->kind == TypeKind::kPointer) {
    core::Type p{core::TypeKind::kPointer, "ptr"};
    p.type_args.push_back(GoTypeToCore(t->elem));
    return p;
  }
  if (t->kind == TypeKind::kSlice)
    return core::Type::Slice(GoTypeToCore(t->elem));
  if (t->kind == TypeKind::kArray)
    return core::Type::Array(GoTypeToCore(t->elem),
                             t->array_len > 0 ? static_cast<size_t>(t->array_len) : 0);
  if (t->kind == TypeKind::kMap) {
    return core::Type{core::TypeKind::kStruct, "map", "go"};
  }
  if (t->kind == TypeKind::kChan)
    return core::Type{core::TypeKind::kClass, "chan", "go"};
  if (t->kind == TypeKind::kFunc)
    return core::Type{core::TypeKind::kFunction, "func", "go"};
  if (t->kind == TypeKind::kInterface)
    return core::Type::Any();
  if (t->kind == TypeKind::kStruct)
    return core::Type{core::TypeKind::kStruct, t->name, "go"};
  if (t->kind != TypeKind::kNamed)
    return core::Type::Any();
  const std::string &n = t->name;
  if (n == "bool")
    return core::Type::Bool();
  if (n == "int8")
    return core::Type::Int(8, true);
  if (n == "int16")
    return core::Type::Int(16, true);
  if (n == "int32" || n == "rune")
    return core::Type::Int(32, true);
  if (n == "int" || n == "int64")
    return core::Type::Int(64, true);
  if (n == "uint8" || n == "byte")
    return core::Type::Int(8, false);
  if (n == "uint16")
    return core::Type::Int(16, false);
  if (n == "uint32")
    return core::Type::Int(32, false);
  if (n == "uint" || n == "uint64" || n == "uintptr")
    return core::Type::Int(64, false);
  if (n == "float32")
    return core::Type::Float(32);
  if (n == "float64")
    return core::Type::Float(64);
  if (n == "string")
    return core::Type::String();
  if (n == "error")
    return core::Type{core::TypeKind::kClass, "error", "go"};
  return core::Type{core::TypeKind::kClass, n, "go"};
}

bool HasUnprojectableArrayExtent(const std::shared_ptr<TypeNode> &type) {
  if (!type)
    return false;
  if (type->kind == TypeKind::kArray &&
      (type->array_len_expr || type->array_len <= 0))
    return true;
  if (HasUnprojectableArrayExtent(type->elem) || HasUnprojectableArrayExtent(type->key))
    return true;
  for (const auto &nested : type->params)
    if (HasUnprojectableArrayExtent(nested))
      return true;
  for (const auto &nested : type->results)
    if (HasUnprojectableArrayExtent(nested))
      return true;
  for (const auto &nested : type->type_args)
    if (HasUnprojectableArrayExtent(nested))
      return true;
  return false;
}

} // namespace

std::vector<frontends::ForeignFunctionSignature> GoLanguageFrontend::ExtractSignatures(
    const std::string &source, const std::string &filename, const std::string &module_name) const {
  frontends::Diagnostics diagnostics;
  frontends::FrontendOptions options;
  return ExtractSignatures(source, filename, module_name, diagnostics, options);
}

std::vector<frontends::ForeignFunctionSignature> GoLanguageFrontend::ExtractSignatures(
    const std::string &source, const std::string &filename, const std::string &module_name,
    frontends::Diagnostics &diagnostics, const frontends::FrontendOptions &options) const {
  std::vector<frontends::ForeignFunctionSignature> out;
  if (!Analyze(source, filename, diagnostics, options) || diagnostics.HasErrors())
    return out;
  GoLexer lex(source, filename);
  GoParser p(lex, diagnostics);
  p.SetGoVersion(options.go_version);
  p.ParseFile();
  auto f = p.TakeFile();
  if (!f || diagnostics.HasErrors())
    return out;
  std::string mod = module_name.empty() ? f->package_name : module_name;
  for (auto &fn : f->funcs) {
    if (!fn)
      continue;
    bool unprojectable_extent = false;
    for (const auto &parameter : fn->params)
      unprojectable_extent |= HasUnprojectableArrayExtent(parameter.second);
    for (const auto &result : fn->results)
      unprojectable_extent |= HasUnprojectableArrayExtent(result.second);
    if (fn->receiver)
      unprojectable_extent |= HasUnprojectableArrayExtent(fn->receiver->type);
    if (unprojectable_extent) {
      diagnostics.ReportError(
          fn->loc, frontends::ErrorCode::kSignatureMissing,
          "Go foreign signatures require a positive literal array extent; constant-expression "
          "evaluation is not available in the fixed-arity signature projection");
      return {};
    }
    // Only export capitalised names (Go's convention) - but include all
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
    else {
      std::vector<core::Type> result_types;
      result_types.reserve(fn->results.size());
      for (const auto &result : fn->results)
        result_types.push_back(GoTypeToCore(result.second));
      sig.return_type = core::Type::Tuple(std::move(result_types));
    }
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

} // namespace polyglot::go
