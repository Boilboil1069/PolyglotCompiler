/**
 * @file     stage_semantic.cpp
 * @brief    Compiler driver implementation
 *
 * @ingroup  Tool / polyc
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
// ============================================================================
// stage_semantic.cpp — Stage 2 implementation
// ============================================================================

#include <filesystem>
#include <iostream>
#include <sstream>

#include "frontends/ploy/include/ploy_sema.h"
#include "tools/polyc/src/foreign_signature_extractor.h"
#include "tools/polyc/src/stage_semantic.h"

namespace polyglot::tools {

namespace fs = std::filesystem;

SemanticResult RunSemanticStage(const DriverSettings &settings, const FrontendResult &frontend) {
  SemanticResult result;
  const bool V = settings.verbose;

  // Non-.poly: the frontend already produced IR — nothing to do here.
  if (settings.language != "poly" || !frontend.ast) {
    result.success = frontend.success;
    return result;
  }

  // ── Sema ─────────────────────────────────────────────────────────────────
  ploy::PloySemaOptions opts;
  opts.strict_mode = settings.strict;
  opts.enable_package_discovery = false; // sema never shells out; indexer ran in stage 1
  opts.discovery_cache = frontend.pkg_cache;

  result.sema = std::make_shared<ploy::PloySema>(result.diagnostics, opts);

  // ── Foreign Signature Extraction ─────────────────────────────────────────
  // Extract and inject before semantic analysis so CALL expressions are
  // checked against the real foreign parameter and return types on their
  // first (and only) analysis pass.  Injecting after Analyze() leaves AST type
  // annotations as Unknown and cannot retract diagnostics already emitted for
  // missing or mismatched signatures.
  {
    ForeignExtractionOptions feopts;
    // Base directory = directory containing the .poly source file.
    if (!settings.source_path.empty()) {
      feopts.base_directory = fs::path(settings.source_path).parent_path().string();
    }
    feopts.poly_source_file = settings.source_path;
    feopts.require_local_source_packages = !settings.package_index;
    feopts.include_paths = settings.include_paths;
    feopts.verbose = V;
    feopts.diagnostics = &result.diagnostics;
    feopts.frontend_options.strict = settings.strict;
    feopts.frontend_options.force = settings.force;
    feopts.frontend_options.include_paths = settings.include_paths;
    feopts.frontend_options.system_include_paths = settings.system_include_paths;
    feopts.frontend_options.defines = settings.defines;
    feopts.frontend_options.undefines = settings.undefines;
    feopts.frontend_options.python_stub_paths = settings.python_stub_paths;
    feopts.frontend_options.classpath = settings.classpath;
    feopts.frontend_options.dotnet_references = settings.dotnet_references;
    feopts.frontend_options.rust_crate_dir = settings.rust_crate_dir;
    feopts.frontend_options.rust_externs = settings.rust_externs;
    feopts.frontend_options.go_project_dir = settings.go_project_dir;
    feopts.frontend_options.go_module_paths = settings.go_module_paths;
    feopts.frontend_options.js_project_dir = settings.js_project_dir;
    feopts.frontend_options.node_modules_paths = settings.node_modules_paths;
    feopts.frontend_options.ruby_project_dir = settings.ruby_project_dir;
    feopts.frontend_options.gem_paths = settings.gem_paths;
    feopts.frontend_options.cpp_dialect = settings.cpp_dialect;
    feopts.frontend_options.python_version = settings.python_version;
    feopts.frontend_options.java_release = settings.java_release;
    feopts.frontend_options.dotnet_lang_version = settings.dotnet_lang_version;
    feopts.frontend_options.dotnet_target_framework = settings.dotnet_target_framework;
    feopts.frontend_options.rust_edition = settings.rust_edition;
    feopts.frontend_options.go_version = settings.go_version;
    feopts.frontend_options.ecma_version = settings.ecma_version;
    feopts.frontend_options.ruby_version = settings.ruby_version;

    ForeignSignatureExtractor extractor(feopts);
    auto foreign_sigs = extractor.ExtractAll(*frontend.ast);

    if (!foreign_sigs.empty()) {
      result.sema->InjectForeignSignatures(foreign_sigs);
      if (V) {
        std::cerr << "[stage/semantic] Injected " << foreign_sigs.size()
                  << " foreign signature(s)\n";
      }
    }
  }

  const bool ok = result.sema->Analyze(frontend.ast);

  const bool semantic_valid = ok && !result.diagnostics.HasErrors();
  if (!semantic_valid && !settings.force) {
    result.success = false;
    if (V)
      std::cerr << "[stage/semantic] FAILED\n";
    return result;
  }

  result.symbols = result.sema->Symbols();
  result.signatures = result.sema->KnownSignatures();
  result.link_entries = result.sema->Links();

  if (V) {
    std::cerr << "[stage/semantic] " << result.symbols.size() << " symbols, "
              << result.link_entries.size() << " link entries\n";
  }

  // Build aux symbol dump
  std::ostringstream oss;
  oss << "Symbol Table (" << result.symbols.size() << " entries)\n";
  for (const auto &[name, sym] : result.symbols) {
    oss << "  " << name << " : kind=" << static_cast<int>(sym.kind) << "\n";
  }
  result.symbols_dump = oss.str();

  // --force may allow later diagnostic stages to inspect the partial
  // database, but it must not relabel a semantically invalid result as valid.
  result.success = semantic_valid;
  return result;
}

} // namespace polyglot::tools
