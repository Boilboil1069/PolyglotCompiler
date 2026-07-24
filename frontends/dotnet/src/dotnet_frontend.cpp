/**
 * @file     dotnet_frontend.cpp
 * @brief    .NET/C# language frontend adapter implementation
 *
 * @ingroup  Frontend / .NET
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include "frontends/common/include/frontend_registry.h"
#include "frontends/common/include/sema_context.h"
#include "frontends/dotnet/include/dotnet_frontend.h"
#include "frontends/dotnet/include/dotnet_lexer.h"
#include "frontends/dotnet/include/dotnet_lowering.h"
#include "frontends/dotnet/include/dotnet_parser.h"
#include "frontends/dotnet/include/dotnet_sema.h"
#include "frontends/dotnet/include/metadata_reader.h"

namespace polyglot::dotnet {

namespace {

frontends::DotnetLangVersion LanguageDefaultForTargetFramework(
    frontends::DotnetTargetFramework framework) {
  switch (framework) {
  case frontends::DotnetTargetFramework::kNet6:
    return frontends::DotnetLangVersion::kCs10;
  case frontends::DotnetTargetFramework::kNet7:
    return frontends::DotnetLangVersion::kCs11;
  case frontends::DotnetTargetFramework::kNet8:
    return frontends::DotnetLangVersion::kCs12;
  case frontends::DotnetTargetFramework::kNet9:
    return frontends::DotnetLangVersion::kCs13;
  case frontends::DotnetTargetFramework::kNet10:
    return frontends::DotnetLangVersion::kCs14;
  case frontends::DotnetTargetFramework::kAuto:
    return frontends::kDotnetLangVersionDefault;
  }
  return frontends::kDotnetLangVersionDefault;
}

bool ResolveTargetFrameworkLanguageVersion(frontends::FrontendOptions &options,
                                           const std::string &filename,
                                           frontends::Diagnostics &diagnostics) {
  if (options.dotnet_target_framework ==
      frontends::DotnetTargetFramework::kAuto) {
    return true;
  }

  const auto framework_default =
      LanguageDefaultForTargetFramework(options.dotnet_target_framework);
  if (options.dotnet_lang_version == frontends::DotnetLangVersion::kAuto) {
    // Match the SDK's target-framework-derived default instead of silently
    // analysing every TFM as the repository-wide C# 14 default.
    options.dotnet_lang_version = framework_default;
    return true;
  }

  if (options.dotnet_lang_version == frontends::DotnetLangVersion::kPreview ||
      !frontends::DotnetLangVersionAtLeast(framework_default,
                                           options.dotnet_lang_version)) {
    diagnostics.ReportError(
        core::SourceLoc{filename, 1, 1},
        frontends::ErrorCode::kLangVersionMismatch,
        std::string(frontends::DotnetTargetFrameworkToString(
                        options.dotnet_target_framework)) +
            " supports C# " +
            frontends::DotnetLangVersionToString(framework_default) +
            " by default, but C# " +
            frontends::DotnetLangVersionToString(options.dotnet_lang_version) +
            " was selected");
    return false;
  }
  return true;
}

} // namespace

// ============================================================================
// Auto-registration
// ============================================================================

REGISTER_FRONTEND(std::make_shared<DotnetLanguageFrontend>());

// ============================================================================
// Tokenize
// ============================================================================

std::vector<frontends::Token> DotnetLanguageFrontend::Tokenize(const std::string &source,
                                                               const std::string &filename) const {
  DotnetLexer lexer(source, filename);
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

bool DotnetLanguageFrontend::Analyze(const std::string &source, const std::string &filename,
                                     frontends::Diagnostics &diagnostics,
                                     const frontends::FrontendOptions &options) const {
  auto effective_options = options;
  if (!ResolveTargetFrameworkLanguageVersion(effective_options, filename,
                                             diagnostics))
    return false;
  DotnetLexer lexer(source, filename);
  DotnetParser parser(lexer, diagnostics);
  parser.SetDotnetLangVersion(effective_options.dotnet_lang_version);
  parser.ParseModule();
  if (diagnostics.HasErrors())
    return false;

  auto module = parser.TakeModule();
  if (!module)
    return false;

  frontends::SemaContext ctx(diagnostics);
  DotNetSemaOptions sema_opts;
  AssemblyLoader loader(effective_options.dotnet_references, diagnostics);
  if (!loader.empty())
    sema_opts.loader = &loader;
  AnalyzeModule(*module, ctx, sema_opts);
  return !diagnostics.HasErrors();
}

// ============================================================================
// Lower
// ============================================================================

frontends::FrontendResult DotnetLanguageFrontend::Lower(
    const std::string &source, const std::string &filename, ir::IRContext &ir_ctx,
    frontends::Diagnostics &diagnostics, const frontends::FrontendOptions &options) const {
  frontends::FrontendResult result;

  auto effective_options = options;
  if (!ResolveTargetFrameworkLanguageVersion(effective_options, filename,
                                             diagnostics))
    return result;

  DotnetLexer lexer(source, filename);
  DotnetParser parser(lexer, diagnostics);
  parser.SetDotnetLangVersion(effective_options.dotnet_lang_version);
  parser.ParseModule();
  auto module = parser.TakeModule();

  if (!module || diagnostics.HasErrors())
    return result;

  frontends::SemaContext ctx(diagnostics);
  DotNetSemaOptions sema_opts;
  AssemblyLoader loader(effective_options.dotnet_references, diagnostics);
  if (!loader.empty())
    sema_opts.loader = &loader;
  AnalyzeModule(*module, ctx, sema_opts);
  if (diagnostics.HasErrors())
    return result;

  LowerToIR(*module, ir_ctx, diagnostics);
  result.lowered = true;
  result.success = !diagnostics.HasErrors();
  return result;
}

// ============================================================================
// ExtractSignatures —parse C# source and extract method signatures
// ============================================================================

namespace {

/// Map a C# TypeNode to a core::Type.
core::Type DotnetTypeToCore(const std::shared_ptr<TypeNode> &tn) {
  if (!tn)
    return core::Type::Void();

  if (auto st = std::dynamic_pointer_cast<SimpleType>(tn)) {
    const std::string &n = st->name;
    if (n == "void")
      return core::Type::Void();
    if (n == "bool")
      return core::Type::Bool();
    if (n == "int" || n == "Int32")
      return core::Type::Int(32, true);
    if (n == "long" || n == "Int64")
      return core::Type::Int(64, true);
    if (n == "short" || n == "Int16")
      return core::Type::Int(16, true);
    if (n == "byte" || n == "Byte")
      return core::Type::Int(8, false);
    if (n == "sbyte" || n == "SByte")
      return core::Type::Int(8, true);
    if (n == "uint" || n == "UInt32")
      return core::Type::Int(32, false);
    if (n == "ulong" || n == "UInt64")
      return core::Type::Int(64, false);
    if (n == "ushort" || n == "UInt16")
      return core::Type::Int(16, false);
    if (n == "float" || n == "Single")
      return core::Type::Float(32);
    if (n == "double" || n == "Double")
      return core::Type::Float(64);
    if (n == "decimal" || n == "Decimal")
      return core::Type::Float(128);
    if (n == "string" || n == "String")
      return core::Type::String();
    if (n == "char" || n == "Char")
      return core::Type::Int(16, false);
    if (n == "object" || n == "Object")
      return core::Type::Any();
    if (n == "dynamic")
      return core::Type::Any();
    return core::Type{core::TypeKind::kClass, n, "dotnet"};
  }
  if (auto at = std::dynamic_pointer_cast<ArrayType>(tn)) {
    return core::Type{core::TypeKind::kArray, "array", "dotnet"};
  }
  if (auto nt = std::dynamic_pointer_cast<NullableType>(tn)) {
    return DotnetTypeToCore(nt->inner);
  }
  if (auto gt = std::dynamic_pointer_cast<GenericType>(tn)) {
    const std::string &n = gt->name;
    if (n == "List" || n == "IList" || n == "IEnumerable" || n == "ICollection")
      return core::Type{core::TypeKind::kArray, n, "dotnet"};
    if (n == "Dictionary" || n == "IDictionary")
      return core::Type{core::TypeKind::kStruct, n, "dotnet"};
    if (n == "Task") {
      if (!gt->type_args.empty())
        return DotnetTypeToCore(gt->type_args[0]);
      return core::Type::Void();
    }
    if (n == "Nullable") {
      if (!gt->type_args.empty())
        return DotnetTypeToCore(gt->type_args[0]);
      return core::Type::Any();
    }
    return core::Type{core::TypeKind::kClass, n, "dotnet"};
  }
  return core::Type::Any();
}

void ExtractDotnetDeclaration(
    const std::shared_ptr<Statement> &decl, const std::string &scope,
    const std::string &module_name,
    std::vector<frontends::ForeignFunctionSignature> &out) {
  if (!decl)
    return;
  if (auto method = std::dynamic_pointer_cast<MethodDecl>(decl)) {
    frontends::ForeignFunctionSignature sig;
    sig.name = method->name;
    sig.class_name = scope;
    const std::string owner = module_name.empty() ? scope : module_name + "::" + scope;
    sig.qualified_name = owner.empty() ? method->name : owner + "::" + method->name;
    sig.return_type = DotnetTypeToCore(method->return_type);
    sig.is_method = !method->is_static;
    sig.has_type_annotations = true;
    for (const auto &param : method->params) {
      sig.param_types.push_back(DotnetTypeToCore(param.type));
      sig.param_names.push_back(param.name);
    }
    out.push_back(std::move(sig));
    return;
  }
  if (auto extension = std::dynamic_pointer_cast<ExtensionDecl>(decl)) {
    for (const auto &member : extension->members)
      ExtractDotnetDeclaration(member, scope, module_name, out);
    return;
  }

  std::string own_name;
  const std::vector<std::shared_ptr<Statement>> *members = nullptr;
  if (auto ns = std::dynamic_pointer_cast<NamespaceDecl>(decl)) {
    own_name = ns->name;
    members = &ns->members;
  } else if (auto cls = std::dynamic_pointer_cast<ClassDecl>(decl)) {
    own_name = cls->name;
    members = &cls->members;
  } else if (auto st = std::dynamic_pointer_cast<StructDecl>(decl)) {
    own_name = st->name;
    members = &st->members;
  } else if (auto iface = std::dynamic_pointer_cast<InterfaceDecl>(decl)) {
    own_name = iface->name;
    members = &iface->members;
  }
  if (!members)
    return;
  const std::string child_scope = scope.empty() ? own_name : scope + "." + own_name;
  for (const auto &member : *members)
    ExtractDotnetDeclaration(member, child_scope, module_name, out);
}

} // namespace

std::vector<frontends::ForeignFunctionSignature> DotnetLanguageFrontend::ExtractSignatures(
    const std::string &source, const std::string &filename, const std::string &module_name) const {
  frontends::Diagnostics diagnostics;
  frontends::FrontendOptions options;
  return ExtractSignatures(source, filename, module_name, diagnostics, options);
}

std::vector<frontends::ForeignFunctionSignature> DotnetLanguageFrontend::ExtractSignatures(
    const std::string &source, const std::string &filename, const std::string &module_name,
    frontends::Diagnostics &diagnostics, const frontends::FrontendOptions &options) const {
  std::vector<frontends::ForeignFunctionSignature> result;

  auto effective_options = options;
  if (!ResolveTargetFrameworkLanguageVersion(effective_options, filename,
                                             diagnostics))
    return result;
  if (!Analyze(source, filename, diagnostics, effective_options) ||
      diagnostics.HasErrors())
    return result;
  DotnetLexer lexer(source, filename);
  DotnetParser parser(lexer, diagnostics);
  parser.SetDotnetLangVersion(effective_options.dotnet_lang_version);
  parser.ParseModule();
  auto module = parser.TakeModule();
  if (!module || diagnostics.HasErrors())
    return result;

  for (const auto &decl : module->declarations)
    ExtractDotnetDeclaration(decl, "", module_name, result);

  return result;
}

} // namespace polyglot::dotnet
