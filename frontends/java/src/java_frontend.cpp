/**
 * @file     java_frontend.cpp
 * @brief    Java language frontend adapter implementation
 *
 * @ingroup  Frontend / Java
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include "frontends/common/include/frontend_registry.h"
#include "frontends/common/include/sema_context.h"
#include "frontends/java/include/class_file_reader.h"
#include "frontends/java/include/java_frontend.h"
#include "frontends/java/include/java_lexer.h"
#include "frontends/java/include/java_lowering.h"
#include "frontends/java/include/java_parser.h"
#include "frontends/java/include/java_sema.h"

namespace polyglot::java {

// ============================================================================
// Auto-registration
// ============================================================================

REGISTER_FRONTEND(std::make_shared<JavaLanguageFrontend>());

// ============================================================================
// Tokenize
// ============================================================================

std::vector<frontends::Token> JavaLanguageFrontend::Tokenize(const std::string &source,
                                                             const std::string &filename) const {
  JavaLexer lexer(source, filename);
  std::vector<frontends::Token> tokens;
  while (true) {
    auto tok = lexer.NextToken();
    if (tok.kind == frontends::TokenKind::kEndOfFile)
      break;
    tokens.push_back(tok);
  }
  return tokens;
}

// ============================================================================
// Analyze
// ============================================================================

bool JavaLanguageFrontend::Analyze(const std::string &source, const std::string &filename,
                                   frontends::Diagnostics &diagnostics,
                                   const frontends::FrontendOptions &options) const {
  JavaLexer lexer(source, filename);
  JavaParser parser(lexer, diagnostics);
  parser.SetJavaRelease(options.java_release);
  parser.ParseModule();
  if (diagnostics.HasErrors())
    return false;

  auto module = parser.TakeModule();
  if (!module)
    return false;

  frontends::SemaContext ctx(diagnostics);
  JavaSemaOptions sema_opts;
  ClasspathLoader loader(options.classpath, diagnostics);
  if (!options.classpath.empty())
    sema_opts.classpath_loader = &loader;
  AnalyzeModule(*module, ctx, sema_opts);
  return !diagnostics.HasErrors();
}

// ============================================================================
// Lower
// ============================================================================

frontends::FrontendResult JavaLanguageFrontend::Lower(
    const std::string &source, const std::string &filename, ir::IRContext &ir_ctx,
    frontends::Diagnostics &diagnostics, const frontends::FrontendOptions &options) const {
  frontends::FrontendResult result;

  JavaLexer lexer(source, filename);
  JavaParser parser(lexer, diagnostics);
  parser.SetJavaRelease(options.java_release);
  parser.ParseModule();
  auto module = parser.TakeModule();

  if (!module || diagnostics.HasErrors())
    return result;

  frontends::SemaContext ctx(diagnostics);
  JavaSemaOptions sema_opts;
  ClasspathLoader loader(options.classpath, diagnostics);
  if (!options.classpath.empty())
    sema_opts.classpath_loader = &loader;
  AnalyzeModule(*module, ctx, sema_opts);
  if (diagnostics.HasErrors())
    return result;

  LowerToIR(*module, ir_ctx, diagnostics);
  result.lowered = true;
  result.success = !diagnostics.HasErrors();
  return result;
}

// ============================================================================
// ExtractSignatures —parse Java source and extract method signatures
// ============================================================================

namespace {

/// Map a Java TypeNode to a core::Type.
core::Type JavaTypeToCore(const std::shared_ptr<TypeNode> &tn) {
  if (!tn)
    return core::Type::Void();

  if (auto st = std::dynamic_pointer_cast<SimpleType>(tn)) {
    const std::string &n = st->name;
    if (n == "void")
      return core::Type::Void();
    if (n == "boolean")
      return core::Type::Bool();
    if (n == "int")
      return core::Type::Int(32, true);
    if (n == "long")
      return core::Type::Int(64, true);
    if (n == "short")
      return core::Type::Int(16, true);
    if (n == "byte")
      return core::Type::Int(8, true);
    if (n == "char")
      return core::Type::Int(16, false);
    if (n == "float")
      return core::Type::Float(32);
    if (n == "double")
      return core::Type::Float(64);
    if (n == "String")
      return core::Type::String();
    if (n == "Integer" || n == "Long" || n == "Short" || n == "Byte")
      return core::Type::Int();
    if (n == "Double" || n == "Float")
      return core::Type::Float();
    if (n == "Boolean")
      return core::Type::Bool();
    if (n == "Object")
      return core::Type::Any();
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
      if (!gt->type_args.empty())
        return JavaTypeToCore(gt->type_args[0]);
      return core::Type::Any();
    }
    return core::Type{core::TypeKind::kClass, n, "java"};
  }
  return core::Type::Any();
}

void AddJavaMethodSignature(const MethodDecl &method, const std::string &type_name,
                            const std::string &module_name,
                            std::vector<frontends::ForeignFunctionSignature> &out) {
  frontends::ForeignFunctionSignature sig;
  sig.name = method.name;
  sig.class_name = type_name;
  const std::string owner = module_name.empty() ? type_name : module_name + "::" + type_name;
  sig.qualified_name = owner.empty() ? method.name : owner + "::" + method.name;
  sig.return_type = JavaTypeToCore(method.return_type);
  sig.is_method = !method.is_static;
  sig.has_type_annotations = true;
  for (const auto &param : method.params) {
    sig.param_types.push_back(JavaTypeToCore(param.type));
    sig.param_names.push_back(param.name);
  }
  out.push_back(std::move(sig));
}

void ExtractJavaDeclaration(
    const std::shared_ptr<Statement> &decl, const std::string &parent_type,
    const std::string &module_name,
    std::vector<frontends::ForeignFunctionSignature> &out) {
  if (!decl)
    return;
  if (auto method = std::dynamic_pointer_cast<MethodDecl>(decl)) {
    AddJavaMethodSignature(*method, parent_type, module_name, out);
    return;
  }

  std::string own_name;
  const std::vector<std::shared_ptr<Statement>> *members = nullptr;
  if (auto cls = std::dynamic_pointer_cast<ClassDecl>(decl)) {
    own_name = cls->name;
    members = &cls->members;
  } else if (auto iface = std::dynamic_pointer_cast<InterfaceDecl>(decl)) {
    own_name = iface->name;
    members = &iface->members;
  } else if (auto record = std::dynamic_pointer_cast<RecordDecl>(decl)) {
    own_name = record->name;
    members = &record->members;
  } else if (auto en = std::dynamic_pointer_cast<EnumDecl>(decl)) {
    own_name = en->name;
    members = &en->members;
  }
  if (!members)
    return;
  const std::string qualified_type =
      parent_type.empty() ? own_name : parent_type + "::" + own_name;
  for (const auto &member : *members)
    ExtractJavaDeclaration(member, qualified_type, module_name, out);
}

} // namespace

std::vector<frontends::ForeignFunctionSignature> JavaLanguageFrontend::ExtractSignatures(
    const std::string &source, const std::string &filename, const std::string &module_name) const {
  frontends::Diagnostics diagnostics;
  frontends::FrontendOptions options;
  return ExtractSignatures(source, filename, module_name, diagnostics, options);
}

std::vector<frontends::ForeignFunctionSignature> JavaLanguageFrontend::ExtractSignatures(
    const std::string &source, const std::string &filename, const std::string &module_name,
    frontends::Diagnostics &diagnostics, const frontends::FrontendOptions &options) const {
  std::vector<frontends::ForeignFunctionSignature> result;

  if (!Analyze(source, filename, diagnostics, options) || diagnostics.HasErrors())
    return result;
  JavaLexer lexer(source, filename);
  JavaParser parser(lexer, diagnostics);
  parser.SetJavaRelease(options.java_release);
  parser.ParseModule();
  auto module = parser.TakeModule();
  if (!module || diagnostics.HasErrors())
    return result;

  for (const auto &decl : module->declarations)
    ExtractJavaDeclaration(decl, "", module_name, result);

  return result;
}

} // namespace polyglot::java
