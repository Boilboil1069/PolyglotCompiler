#pragma once

#include <QTemporaryDir>
#include <filesystem>
#include <memory>
#include <string>

#include "frontends/ploy/include/ploy_lexer.h"
#include "frontends/ploy/include/ploy_parser.h"
#include "frontends/ploy/include/ploy_sema.h"
#include "tools/polyc/src/foreign_signature_extractor.h"

namespace polyglot::tools::ui::cross_language {

inline std::string CompilerTypeName(const core::Type &type) {
  if (type.kind == core::TypeKind::kInt && type.bit_width > 0)
    return std::string(type.is_signed ? "i" : "u") + std::to_string(type.bit_width);
  if (type.kind == core::TypeKind::kFloat && type.bit_width > 0)
    return "f" + std::to_string(type.bit_width);
  return type.ToString();
}

// All UI consumers use the compiler's frontend registry, vendored-package
// bundler and foreign signature transaction. Diagnostics are never filtered.
struct PolyWorkspaceAnalysis {
  frontends::Diagnostics diagnostics;
  std::shared_ptr<ploy::Module> module;
  std::unique_ptr<ploy::PloySema> sema;
  std::unordered_map<std::string, ploy::FunctionSignature> foreign_signatures;
  QTemporaryDir bundle_directory;
};

inline std::unique_ptr<PolyWorkspaceAnalysis> AnalyzePolyWorkspace(
    const std::string &source, const std::string &filename, bool strict = true,
    frontends::FrontendOptions frontend_options = {}) {
  auto result = std::make_unique<PolyWorkspaceAnalysis>();
  ploy::PloyLexer lexer(source, filename);
  lexer.SetTokenPool(frontend_options.token_pool);
  ploy::PloyParser parser(lexer, result->diagnostics);
  parser.ParseModule();
  result->module = parser.TakeModule();
  if (!result->module || result->diagnostics.HasErrors())
    return result;

  tools::ForeignExtractionOptions extraction;
  extraction.base_directory = std::filesystem::path(filename).parent_path().string();
  extraction.poly_source_file = filename;
  extraction.bundle_directory = result->bundle_directory.path().toStdString();
  extraction.require_local_source_packages = true;
  extraction.frontend_options = frontend_options;
  extraction.frontend_options.strict = strict;
  extraction.diagnostics = &result->diagnostics;
  tools::ForeignSignatureExtractor extractor(extraction);
  result->foreign_signatures = extractor.ExtractAll(*result->module);

  ploy::PloySemaOptions options;
  options.enable_package_discovery = false;
  options.strict_mode = strict;
  result->sema = std::make_unique<ploy::PloySema>(result->diagnostics, options);
  // CALL return inference must see the real signatures during analysis.
  result->sema->InjectForeignSignatures(result->foreign_signatures);
  result->sema->Analyze(result->module);
  return result;
}

} // namespace polyglot::tools::ui::cross_language
