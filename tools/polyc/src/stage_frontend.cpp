/**
 * @file     stage_frontend.cpp
 * @brief    Compiler driver implementation
 *
 * @ingroup  Tool / polyc
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
// ============================================================================
// stage_frontend.cpp 鈥?Stage 1 implementation
// ============================================================================

#include <chrono>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <unordered_set>

#include "middle/include/ir/ir_context.h"

#include "frontends/common/include/diagnostics.h"
#include "frontends/common/include/frontend_registry.h"
#include "frontends/common/include/language_frontend.h"
#include "frontends/common/include/preprocessor.h"
#include "frontends/common/include/token_pool.h"
#include "frontends/ploy/include/command_runner.h"
#include "frontends/ploy/include/package_discovery_cache.h"
#include "frontends/ploy/include/package_indexer.h"
#include "frontends/ploy/include/ploy_lexer.h"
#include "frontends/ploy/include/ploy_parser.h"
#include "tools/polyc/src/stage_frontend.h"

namespace polyglot::tools {

namespace {

namespace fs = std::filesystem;
using namespace std::chrono;

/// Apply include paths to a Preprocessor instance.
void ApplyIncludePaths(frontends::Preprocessor &pp, const std::vector<std::string> &paths) {
  for (const auto &p : paths) {
    pp.AddIncludePath(p);
  }
}

/// Query an environment variable: return true if it exists and is non-empty.
bool HasEnvVar(const char *name) {
#if defined(_WIN32)
  char *value = nullptr;
  std::size_t len = 0;
  errno_t rc = _dupenv_s(&value, &len, name);
  if (rc != 0 || value == nullptr) {
    if (value)
      std::free(value);
    return false;
  }
  const bool present = (len > 1 && value[0] != '\0');
  std::free(value);
  return present;
#else
  return std::getenv(name) != nullptr;
#endif
}

} // namespace

// ============================================================================
// RunFrontendStage
// ============================================================================

FrontendResult RunFrontendStage(const DriverSettings &settings) {
  FrontendResult result;
  result.language = settings.language;
  result.source_label = settings.source_path.empty() ? "<cli>" : settings.source_path;
  result.processed_source = settings.source;
  result.pkg_cache = std::make_shared<ploy::PackageDiscoveryCache>();

  const bool V = settings.verbose;

  // Per-session shared token pool.  Always created so downstream stages may
  // record stats even when --dump-token-pool is off.
  const std::size_t arena_chunk = settings.token_pool_arena_chunk_bytes != 0
                                      ? settings.token_pool_arena_chunk_bytes
                                      : frontends::StringArena::kDefaultChunkBytes;
  auto session_pool = std::make_unique<frontends::SharedTokenPool>(arena_chunk);

  // 鈹€鈹€ Stage 1a: Preprocessing 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
  if (settings.language != "ploy") {
    auto *fe = frontends::FrontendRegistry::Instance().GetFrontend(settings.language);
    if (fe && fe->NeedsPreprocessing()) {
      // Built-in per-language preprocessing defaults: C/C++ goes through the
      // preprocessor by default; Rust and Python skip it because their own
      // toolchains own that step.  The Rust default can be force-enabled via
      // the POLYC_PREPROCESS_RUST environment variable for diagnostics work.
      bool pp_enabled = !(settings.language == "rust" || settings.language == "python");
      if (settings.language == "rust" && HasEnvVar("POLYC_PREPROCESS_RUST"))
        pp_enabled = true;

      if (pp_enabled) {
        frontends::Preprocessor pp(result.diagnostics);
        pp.SetTokenPool(session_pool.get());
        ApplyIncludePaths(pp, settings.include_paths);
        ApplyIncludePaths(pp, settings.system_include_paths);
        // Apply -D / -U flags
        for (const auto &d : settings.defines) {
          auto eq = d.find('=');
          if (eq == std::string::npos) {
            pp.Define(d, "1");
          } else {
            pp.Define(d.substr(0, eq), d.substr(eq + 1));
          }
        }
        for (const auto &u : settings.undefines)
          pp.Undefine(u);
        result.processed_source = pp.Process(settings.source, result.source_label);
        if (V)
          std::cerr << "[stage/frontend] preprocessed\n";
      }
    }
  }
  // .ploy never uses the preprocessor

  // 鈹€鈹€ Stage 1b: Non-.ploy 鈫?FrontendRegistry 鈫?IR directly 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
  if (settings.language != "ploy") {
    auto *fe = frontends::FrontendRegistry::Instance().GetFrontend(settings.language);
    if (!fe) {
      result.diagnostics.Report(core::SourceLoc{result.source_label, 1, 1},
                                "Unknown language: " + settings.language);
      result.success = false;
      return result;
    }

    result.ir_ctx = std::make_shared<ir::IRContext>();
    frontends::FrontendOptions fe_opts;
    fe_opts.verbose = V;
    fe_opts.strict = settings.strict;
    fe_opts.force = settings.force;
    fe_opts.include_paths = settings.include_paths;
    fe_opts.system_include_paths = settings.system_include_paths;
    fe_opts.defines = settings.defines;
    fe_opts.undefines = settings.undefines;
    fe_opts.python_stub_paths = settings.python_stub_paths;
    fe_opts.classpath = settings.classpath;
    fe_opts.dotnet_references = settings.dotnet_references;
    fe_opts.rust_crate_dir = settings.rust_crate_dir;
    fe_opts.rust_externs = settings.rust_externs;
    fe_opts.go_project_dir = settings.go_project_dir;
    fe_opts.go_module_paths = settings.go_module_paths;
    fe_opts.js_project_dir = settings.js_project_dir;
    fe_opts.node_modules_paths = settings.node_modules_paths;
    fe_opts.ruby_project_dir = settings.ruby_project_dir;
    fe_opts.gem_paths = settings.gem_paths;
    // Per-language version selection.
    fe_opts.cpp_dialect             = settings.cpp_dialect;
    fe_opts.python_version          = settings.python_version;
    fe_opts.java_release            = settings.java_release;
    fe_opts.dotnet_lang_version     = settings.dotnet_lang_version;
    fe_opts.dotnet_target_framework = settings.dotnet_target_framework;
    fe_opts.rust_edition            = settings.rust_edition;
    fe_opts.go_version              = settings.go_version;
    fe_opts.ecma_version            = settings.ecma_version;
    fe_opts.ruby_version            = settings.ruby_version;
    fe_opts.token_pool              = session_pool.get();
    fe_opts.dump_token_pool_stats   = settings.dump_token_pool;

    auto fe_result = fe->Lower(result.processed_source, result.source_label, *result.ir_ctx,
                               result.diagnostics, fe_opts);
    result.success = fe_result.lowered;
    if (settings.dump_token_pool) {
      const auto stats = session_pool->Stats();
      std::ostringstream js;
      js << "{\n"
         << "  \"language\": \"" << settings.language << "\",\n"
         << "  \"tokens\": " << stats.tokens << ",\n"
         << "  \"arena_bytes\": " << stats.arena_bytes << ",\n"
         << "  \"arena_capacity\": " << stats.arena_capacity << ",\n"
         << "  \"unique_identifiers\": " << stats.unique_identifiers << ",\n"
         << "  \"intern_hits\": " << stats.intern_hits << ",\n"
         << "  \"intern_misses\": " << stats.intern_misses << "\n"
         << "}\n";
      result.token_pool_stats_json = js.str();
    }
    return result;
  }

  // 鈹€鈹€ Stage 1c: .ploy 鈥?Lexing 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
  {
    ploy::PloyLexer token_lexer(result.processed_source, result.source_label);
    token_lexer.SetTokenPool(session_pool.get());
    std::ostringstream toss;
    while (true) {
      auto tok = token_lexer.NextToken();
      if (tok.kind == frontends::TokenKind::kEndOfFile)
        break;
      // Mirror to pool (PloyLexer doesn't yet call EmitToken internally).
      session_pool->Add(tok);
      toss << "L" << tok.loc.line << ":" << tok.loc.column << "  [" << static_cast<int>(tok.kind)
           << "] " << tok.lexeme << "\n";
      result.tokens.push_back(std::move(tok));
    }
    result.token_dump = toss.str();
    if (V)
      std::cerr << "[stage/frontend] lexing done (" << result.tokens.size() << " tokens)\n";
  }

  // 鈹€鈹€ Stage 1d: .ploy 鈥?Parsing 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
  {
    ploy::PloyLexer parse_lexer(result.processed_source, result.source_label);
    ploy::PloyParser parser(parse_lexer, result.diagnostics);
    parser.ParseModule();
    result.ast = parser.TakeModule();

    if (!result.ast || result.diagnostics.HasErrors()) {
      result.success = false;
      if (V)
        std::cerr << "[stage/frontend] parse FAILED\n";
      return result;
    }
    if (V)
      std::cerr << "[stage/frontend] parsed (" << result.ast->declarations.size() << " decls)\n";

    // AST summary for aux
    std::ostringstream ast_oss;
    ast_oss << "Module: " << result.ast->declarations.size() << " declarations\n";
    for (std::size_t i = 0; i < result.ast->declarations.size(); ++i) {
      auto &decl = result.ast->declarations[i];
      ast_oss << "  [" << i << "] loc=" << decl->loc.line << ":" << decl->loc.column << "\n";
    }
    result.ast_dump = ast_oss.str();
  }

  // 鈹€鈹€ Stage 1e: .ploy 鈥?Package index 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
  if (settings.package_index && result.ast) {
    std::unordered_set<std::string> seen;
    std::vector<std::string> languages;
    for (const auto &decl : result.ast->declarations) {
      auto imp = std::dynamic_pointer_cast<ploy::ImportDecl>(decl);
      if (imp && !imp->language.empty() && !imp->package_name.empty()) {
        if (seen.insert(imp->language).second)
          languages.push_back(imp->language);
      }
    }
    if (!languages.empty()) {
      ploy::PackageIndexerOptions idx_opts;
      idx_opts.command_timeout = milliseconds{settings.package_index_timeout_ms};
      idx_opts.verbose = V;
      auto runner = std::make_shared<ploy::DefaultCommandRunner>(idx_opts.command_timeout);
      ploy::PackageIndexer indexer(result.pkg_cache, runner, idx_opts);
      if (V) {
        indexer.SetProgressCallback([](const std::string &lang, const std::string &msg) {
          std::cerr << "  [pkg-index:" << lang << "] " << msg << "\n";
        });
      }
      // Wire CLI-supplied per-language project roots into the indexer
      // through the existing VenvConfig channel (manager=kVenv just
      // means "default plumbing"; the runner only inspects venv_path).
      std::vector<ploy::VenvConfig> venv_configs;
      if (!settings.rust_crate_dir.empty()) {
        ploy::VenvConfig vc;
        vc.language = "rust";
        vc.venv_path = settings.rust_crate_dir;
        vc.manager = ploy::VenvConfigDecl::ManagerKind::kVenv;
        venv_configs.push_back(std::move(vc));
      }
      indexer.BuildIndex(languages, venv_configs);
      if (V) {
        auto stats = indexer.LastStats();
        std::cerr << "  [pkg-index] " << stats.packages_found << " package(s) indexed\n";
      }
    }
  }

  result.success = true;
  if (settings.dump_token_pool) {
    const auto stats = session_pool->Stats();
    std::ostringstream js;
    js << "{\n"
       << "  \"language\": \"" << settings.language << "\",\n"
       << "  \"tokens\": " << stats.tokens << ",\n"
       << "  \"arena_bytes\": " << stats.arena_bytes << ",\n"
       << "  \"arena_capacity\": " << stats.arena_capacity << ",\n"
       << "  \"unique_identifiers\": " << stats.unique_identifiers << ",\n"
       << "  \"intern_hits\": " << stats.intern_hits << ",\n"
       << "  \"intern_misses\": " << stats.intern_misses << "\n"
       << "}\n";
    result.token_pool_stats_json = js.str();
  }
  return result;
}

} // namespace polyglot::tools
