/**
 * @file     cpp_frontend.cpp
 * @brief    C++ language frontend adapter implementation
 *
 * @ingroup  Frontend / C++
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include <optional>

#include "frontends/common/include/frontend_registry.h"
#include "frontends/common/include/preprocessor.h"
#include "frontends/common/include/sema_context.h"
#include "frontends/cpp/include/cpp_frontend.h"
#include "frontends/cpp/include/cpp_lexer.h"
#include "frontends/cpp/include/cpp_lowering.h"
#include "frontends/cpp/include/cpp_parser.h"
#include "frontends/cpp/include/cpp_sema.h"

namespace polyglot::cpp {

namespace {

bool HasPreprocessorDirective(const std::string &source) {
  size_t line_start = 0;
  while (line_start < source.size()) {
    size_t cursor = line_start;
    while (cursor < source.size() && (source[cursor] == ' ' || source[cursor] == '\t'))
      ++cursor;
    if (cursor < source.size() && source[cursor] == '#')
      return true;
    const auto newline = source.find('\n', line_start);
    if (newline == std::string::npos)
      break;
    line_start = newline + 1;
  }
  return false;
}

std::optional<std::string> PrepareCppSource(const std::string &source, const std::string &filename,
                                            frontends::Diagnostics &diagnostics,
                                            const frontends::FrontendOptions &options,
                                            bool signature_scan) {
  if (options.source_is_preprocessed)
    return source;

  if (!options.enable_preprocessing) {
    if (HasPreprocessorDirective(source)) {
      diagnostics.ReportError(
          core::SourceLoc{filename, 1, 1}, frontends::ErrorCode::kUnsupportedSyntax,
          "C++ source contains preprocessor directives, but preprocessing is disabled and the "
          "input was not marked as already preprocessed");
      return std::nullopt;
    }
    return source;
  }

  frontends::Preprocessor preprocessor(diagnostics);
  // In signature-only mode, <...> headers supply external declarations and
  // must not be re-exported as if they belonged to this module.  Quoted
  // project headers still resolve and preprocess normally.
  preprocessor.SetAngleIncludesExternal(signature_scan);
  std::vector<std::string> include_paths = options.include_paths;
  include_paths.insert(include_paths.end(), options.system_include_paths.begin(),
                       options.system_include_paths.end());
  preprocessor.SetIncludePaths(std::move(include_paths));
  preprocessor.Define("__cplusplus",
                      frontends::CppDialectCplusplusValue(options.cpp_dialect));
  for (const auto &definition : options.defines) {
    const auto equals = definition.find('=');
    if (equals == std::string::npos)
      preprocessor.Define(definition, "1");
    else
      preprocessor.Define(definition.substr(0, equals), definition.substr(equals + 1));
  }
  for (const auto &name : options.undefines)
    preprocessor.Undefine(name);
  auto processed = preprocessor.Process(source, filename);
  if (diagnostics.HasErrors())
    return std::nullopt;
  return processed;
}

} // namespace

// ============================================================================
// Auto-registration
// ============================================================================

REGISTER_FRONTEND(std::make_shared<CppLanguageFrontend>());

// ============================================================================
// Tokenize
// ============================================================================

std::vector<frontends::Token> CppLanguageFrontend::Tokenize(const std::string &source,
                                                            const std::string &filename) const {
  CppLexer lexer(source, filename);
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

bool CppLanguageFrontend::Analyze(const std::string &source, const std::string &filename,
                                  frontends::Diagnostics &diagnostics,
                                  const frontends::FrontendOptions &options) const {
  auto prepared = PrepareCppSource(source, filename, diagnostics, options, false);
  if (!prepared)
    return false;
  CppLexer lexer(*prepared, filename);
  CppParser parser(lexer, diagnostics);
  parser.SetCppDialect(options.cpp_dialect);
  parser.ParseModule();
  if (diagnostics.HasErrors())
    return false;

  auto module = parser.TakeModule();
  if (!module)
    return false;

  frontends::SemaContext ctx(diagnostics);
  AnalyzeModule(*module, ctx);
  return !diagnostics.HasErrors();
}

// ============================================================================
// Lower
// ============================================================================

frontends::FrontendResult CppLanguageFrontend::Lower(
    const std::string &source, const std::string &filename, ir::IRContext &ir_ctx,
    frontends::Diagnostics &diagnostics, const frontends::FrontendOptions &options) const {
  frontends::FrontendResult result;

  auto prepared = PrepareCppSource(source, filename, diagnostics, options, false);
  if (!prepared)
    return result;
  CppLexer lexer(*prepared, filename);
  CppParser parser(lexer, diagnostics);
  parser.SetCppDialect(options.cpp_dialect);
  parser.ParseModule();
  auto module = parser.TakeModule();

  if (!module || diagnostics.HasErrors())
    return result;

  frontends::SemaContext ctx(diagnostics);
  AnalyzeModule(*module, ctx);
  if (diagnostics.HasErrors())
    return result;

  LowerToIR(*module, ir_ctx, diagnostics);
  result.lowered = true;
  result.success = !diagnostics.HasErrors();
  return result;
}

// ============================================================================
// ExtractSignatures — parse C++ source and extract function signatures
// ============================================================================

namespace {

/// Map a C++ TypeNode to a core::Type.
core::Type CppTypeToCore(const std::shared_ptr<TypeNode> &tn) {
  if (!tn)
    return core::Type::Void();

  if (auto st = std::dynamic_pointer_cast<SimpleType>(tn)) {
    // Use the exact primitive map consumed by C++ lowering. A separate
    // signature table previously described LP64 long as i32 and size_t as
    // signed, causing cross-language ABI casts to truncate valid values.
    const auto mapped = core::TypeSystem().MapFromLanguage("cpp", st->name);
    if (mapped.kind != core::TypeKind::kStruct)
      return mapped;
    return core::Type{core::TypeKind::kClass, st->name, "cpp"};
  }
  if (auto pt = std::dynamic_pointer_cast<PointerType>(tn)) {
    return core::Type{core::TypeKind::kPointer, "ptr", "cpp"};
  }
  if (auto rt = std::dynamic_pointer_cast<ReferenceType>(tn)) {
    return core::Type{core::TypeKind::kReference, "ref", "cpp"};
  }
  if (auto qt = std::dynamic_pointer_cast<QualifiedType>(tn)) {
    return CppTypeToCore(qt->inner);
  }
  return core::Type::Any();
}

} // namespace

std::vector<frontends::ForeignFunctionSignature> CppLanguageFrontend::ExtractSignatures(
    const std::string &source, const std::string &filename, const std::string &module_name) const {
  frontends::Diagnostics diagnostics;
  frontends::FrontendOptions options;
  return ExtractSignatures(source, filename, module_name, diagnostics, options);
}

std::vector<frontends::ForeignFunctionSignature> CppLanguageFrontend::ExtractSignatures(
    const std::string &source, const std::string &filename, const std::string &module_name,
    frontends::Diagnostics &diagnostics, const frontends::FrontendOptions &options) const {
  std::vector<frontends::ForeignFunctionSignature> result;

  auto prepared = PrepareCppSource(source, filename, diagnostics, options, true);
  if (!prepared)
    return result;
  CppLexer lexer(*prepared, filename);
  CppParser parser(lexer, diagnostics);
  parser.SetCppDialect(options.cpp_dialect);
  parser.ParseModule();
  auto module = parser.TakeModule();
  if (!module || diagnostics.HasErrors())
    return result;

  frontends::SemaContext ctx(diagnostics);
  AnalyzeModuleSignatures(*module, ctx);
  if (diagnostics.HasErrors())
    return result;

  // Walk all top-level declarations looking for functions
  for (const auto &decl : module->declarations) {
    if (auto fn = std::dynamic_pointer_cast<FunctionDecl>(decl)) {
      frontends::ForeignFunctionSignature sig;
      sig.name = fn->name;
      sig.qualified_name = module_name.empty() ? fn->name : module_name + "::" + fn->name;
      sig.return_type = CppTypeToCore(fn->return_type);
      sig.has_type_annotations = true; // C++ always has explicit types

      for (const auto &p : fn->params) {
        sig.param_types.push_back(CppTypeToCore(p.type));
        sig.param_names.push_back(p.name);
      }

      result.push_back(std::move(sig));
    }
    // Also extract methods from class/struct declarations
    if (auto cls = std::dynamic_pointer_cast<RecordDecl>(decl)) {
      for (const auto &member : cls->methods) {
        if (auto method = std::dynamic_pointer_cast<FunctionDecl>(member)) {
          frontends::ForeignFunctionSignature sig;
          sig.name = method->name;
          sig.qualified_name = module_name.empty()
                                   ? cls->name + "::" + method->name
                                   : module_name + "::" + cls->name + "::" + method->name;
          sig.return_type = CppTypeToCore(method->return_type);
          sig.is_method = true;
          sig.class_name = cls->name;
          sig.has_type_annotations = true;

          for (const auto &p : method->params) {
            sig.param_types.push_back(CppTypeToCore(p.type));
            sig.param_names.push_back(p.name);
          }

          result.push_back(std::move(sig));
        }
      }
    }
  }

  return result;
}

} // namespace polyglot::cpp
