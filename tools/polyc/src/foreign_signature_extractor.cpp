/**
 * @file     foreign_signature_extractor.cpp
 * @brief    Compiler driver implementation
 *
 * @ingroup  Tool / polyc
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
// ============================================================================
// foreign_signature_extractor.cpp — Implementation
// ============================================================================

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>

#include "frontends/common/include/frontend_registry.h"
#include "tools/polyc/src/foreign_signature_extractor.h"
#include "tools/polyc/src/local_source_packages.h"

namespace polyglot::tools {

namespace fs = std::filesystem;

namespace {

bool ApplyPinnedVersion(const std::string &language, const std::string &version,
                        frontends::FrontendOptions &options,
                        frontends::Diagnostics &diagnostics,
                        const core::SourceLoc &loc) {
  auto reconcile = [&](auto parsed, auto selected, auto auto_value,
                       auto default_value, auto to_string, auto &target) {
    if (!parsed)
      return false;

    const auto pinned = *parsed;
    const auto effective_selected =
        selected == auto_value ? default_value : selected;
    const auto effective_pinned =
        pinned == auto_value ? default_value : pinned;

    // A non-auto option is an explicit caller selection (CLI, UI or API).
    // Module-level LANG pins may fill an auto selection, but must never
    // silently replace an explicit dialect.  Equal effective values are fine
    // (including an "auto" pin whose stable default equals the selection).
    if (selected != auto_value && effective_selected != effective_pinned) {
      diagnostics.ReportError(
          loc, frontends::ErrorCode::kLangVersionMismatch,
          "explicit " + language + " selector '" + to_string(selected) +
              "' conflicts with module LANG pin '" + to_string(pinned) + "'");
      return true;
    }

    if (selected == auto_value)
      target = pinned;
    return true;
  };

  bool recognized = false;
  if (language == "cpp") {
    recognized = reconcile(frontends::ParseCppDialect(version), options.cpp_dialect,
                           frontends::CppDialect::kAuto,
                           frontends::kCppDialectDefault,
                           [](auto value) {
                             return std::string(frontends::CppDialectToString(value));
                           },
                           options.cpp_dialect);
  } else if (language == "python") {
    recognized = reconcile(frontends::ParsePythonVersion(version), options.python_version,
                           frontends::PythonVersion::kAuto,
                           frontends::kPythonVersionDefault,
                           [](auto value) {
                             return std::string(frontends::PythonVersionToString(value));
                           },
                           options.python_version);
  } else if (language == "java") {
    recognized = reconcile(frontends::ParseJavaRelease(version), options.java_release,
                           frontends::JavaRelease::kAuto,
                           frontends::kJavaReleaseDefault,
                           [](auto value) {
                             return std::string(frontends::JavaReleaseToString(value));
                           },
                           options.java_release);
  } else if (language == "dotnet") {
    recognized = reconcile(frontends::ParseDotnetLangVersion(version),
                           options.dotnet_lang_version,
                           frontends::DotnetLangVersion::kAuto,
                           frontends::kDotnetLangVersionDefault,
                           [](auto value) {
                             return std::string(frontends::DotnetLangVersionToString(value));
                           },
                           options.dotnet_lang_version);
  } else if (language == "rust") {
    recognized = reconcile(frontends::ParseRustEdition(version), options.rust_edition,
                           frontends::RustEdition::kAuto,
                           frontends::kRustEditionDefault,
                           [](auto value) {
                             return std::string(frontends::RustEditionToString(value));
                           },
                           options.rust_edition);
  } else if (language == "go") {
    recognized = reconcile(frontends::ParseGoVersion(version), options.go_version,
                           frontends::GoVersion::kAuto,
                           frontends::kGoVersionDefault,
                           [](auto value) {
                             return std::string(frontends::GoVersionToString(value));
                           },
                           options.go_version);
  } else if (language == "javascript") {
    recognized = reconcile(frontends::ParseEcmaVersion(version), options.ecma_version,
                           frontends::EcmaVersion::kAuto,
                           frontends::kEcmaVersionDefault,
                           [](auto value) {
                             return std::string(frontends::EcmaVersionToString(value));
                           },
                           options.ecma_version);
  } else if (language == "ruby") {
    recognized = reconcile(frontends::ParseRubyVersion(version), options.ruby_version,
                           frontends::RubyVersion::kAuto,
                           frontends::kRubyVersionDefault,
                           [](auto value) {
                             return std::string(frontends::RubyVersionToString(value));
                           },
                           options.ruby_version);
  }

  if (!recognized) {
    diagnostics.ReportError(
        loc, frontends::ErrorCode::kLangVersionMismatch,
        "unsupported version '" + version + "' for imported language '" + language + "'");
  }
  return recognized && !diagnostics.HasErrors();
}

bool EquivalentSignature(const ploy::FunctionSignature &lhs,
                         const ploy::FunctionSignature &rhs) {
  // `name` is intentionally excluded: the same callable is published under
  // qualified and convenience aliases.  Language, parameter names/types and
  // return type are observable at cross-language call sites and therefore
  // must all agree before a duplicate key can be safely deduplicated.
  return lhs.language == rhs.language && lhs.param_types == rhs.param_types &&
         lhs.param_names == rhs.param_names && lhs.return_type == rhs.return_type &&
         lhs.param_count == rhs.param_count &&
         lhs.param_count_known == rhs.param_count_known;
}

} // namespace

// ============================================================================
// Construction
// ============================================================================

ForeignSignatureExtractor::ForeignSignatureExtractor(const ForeignExtractionOptions &opts) :
    opts_(opts) {}

// ============================================================================
// File resolution
// ============================================================================

std::string ForeignSignatureExtractor::ResolveSourceFile(const std::string &language,
                                                         const std::string &module_name) const {
  // Get the frontend to retrieve possible file extensions.
  const auto *fe = frontends::FrontendRegistry::Instance().GetFrontend(language);
  if (!fe)
    return {};

  const auto extensions = fe->Extensions();

  // Search order:
  //   1. base_directory / <module_name>.<ext>
  //   2. Each include_path  / <module_name>.<ext>
  // For case sensitivity, try the module name as-is (common on POSIX).

  auto try_dirs = [&](const std::string &dir) -> std::string {
    std::vector<fs::path> module_paths{fs::path(module_name)};
    std::string nested = module_name;
    std::string::size_type pos = 0;
    while ((pos = nested.find("::", pos)) != std::string::npos) {
      nested.replace(pos, 2, 1, fs::path::preferred_separator);
      ++pos;
    }
    if (nested != module_name)
      module_paths.emplace_back(nested);

    for (const auto &module_path : module_paths) {
      for (const auto &prefix : {fs::path{}, fs::path(language)}) {
        for (const auto &ext : extensions) {
          fs::path candidate = fs::path(dir) / prefix / module_path;
          candidate += ext;
          if (fs::exists(candidate))
            return candidate.string();
        }
      }
    }
    return {};
  };

  // Primary: base directory
  if (!opts_.base_directory.empty()) {
    auto found = try_dirs(opts_.base_directory);
    if (!found.empty())
      return found;
  }

  // Secondary: include paths
  for (const auto &inc : opts_.include_paths) {
    auto found = try_dirs(inc);
    if (!found.empty())
      return found;
  }

  return {};
}

// ============================================================================
// ReadFile helper
// ============================================================================

std::optional<std::string> ForeignSignatureExtractor::ReadFile(const std::string &path) {
  std::error_code file_error;
  if (!fs::is_regular_file(path, file_error) || file_error)
    return std::nullopt;
  std::ifstream ifs(path, std::ios::binary);
  if (!ifs.is_open())
    return std::nullopt;
  std::ostringstream oss;
  oss << ifs.rdbuf();
  if (ifs.bad())
    return std::nullopt;
  return oss.str();
}

// ============================================================================
// ExtractAll — main entry point
// ============================================================================

std::unordered_map<std::string, ploy::FunctionSignature> ForeignSignatureExtractor::ExtractAll(
    const ploy::Module &module) const {
  std::unordered_map<std::string, ploy::FunctionSignature> result;

  const auto report_failure = [&](const core::SourceLoc &loc,
                                  frontends::ErrorCode code,
                                  const std::string &message) {
    if (opts_.diagnostics)
      opts_.diagnostics->ReportError(loc, code, message);
    if (opts_.verbose)
      std::cerr << "[foreign-sig] " << message << "\n";
  };

  std::unordered_map<std::string, std::string> module_version_pins;
  for (const auto &decl : module.declarations) {
    auto pragma = std::dynamic_pointer_cast<ploy::LangPragma>(decl);
    if (!pragma)
      continue;
    const auto *pinned_frontend =
        frontends::FrontendRegistry::Instance().GetFrontend(pragma->language);
    if (pinned_frontend)
      module_version_pins[pinned_frontend->Name()] = pragma->version;
  }

  for (const auto &decl : module.declarations) {
    auto import = std::dynamic_pointer_cast<ploy::ImportDecl>(decl);
    if (!import)
      continue;

    // Only process local file imports (not PACKAGE imports).
    // Pattern: IMPORT <language>::<module_name>;
    // e.g., IMPORT cpp::math_ops;
    if (import->language.empty())
      continue;
    if (!import->package_name.empty())
      continue; // skip PACKAGE imports
    if (import->module_path.empty())
      continue;

    const std::string &language = import->language;
    const std::string &module_name = import->module_path;

    // Don't process poly-to-poly imports
    if (language == "poly")
      continue;

    // Resolve the source file on disk.
    std::string file_path = ResolveSourceFile(language, module_name);
    if (file_path.empty()) {
      report_failure(import->loc, frontends::ErrorCode::kSignatureMissing,
                     "could not locate local foreign source for import '" + language +
                         "::" + module_name + "'");
      continue;
    }

    // Signature analysis must see the exact same vendored source unit that
    // packaging later sends through the built-in frontend.  Otherwise a
    // consumer wrapper referencing a class/struct supplied by a PACKAGE would
    // fail sema before the package bundler ever ran.
    std::string analysis_path = file_path;
    if (!opts_.poly_source_file.empty() && !opts_.bundle_directory.empty()) {
      std::string bundle_error;
      analysis_path = BuildVendoredSourceBundle(opts_.poly_source_file, language, file_path,
                                                opts_.bundle_directory, &bundle_error,
                                                opts_.require_local_source_packages);
      if (analysis_path.empty()) {
        report_failure(import->loc, frontends::ErrorCode::kSignatureMissing,
                       bundle_error.empty() ? "failed to build vendored source bundle for '" +
                                                  language + "::" + module_name + "'"
                                            : bundle_error);
        continue;
      }
    }

    // Read and parse the (possibly bundled) file.
    auto source = ReadFile(analysis_path);
    if (!source) {
      report_failure(import->loc, frontends::ErrorCode::kSignatureMissing,
                     "could not read local foreign source '" + analysis_path +
                         "' for import '" + language + "::" + module_name + "'");
      continue;
    }

    // Get the frontend and extract signatures.
    const auto *fe = frontends::FrontendRegistry::Instance().GetFrontend(language);
    if (!fe)
      continue;
    const std::string canonical_language = fe->Name();

    frontends::Diagnostics extraction_diags;
    auto frontend_options = opts_.frontend_options;
    if (auto pin = module_version_pins.find(canonical_language);
        pin != module_version_pins.end()) {
      ApplyPinnedVersion(canonical_language, pin->second, frontend_options,
                         extraction_diags, import->loc);
    }
    auto foreign_sigs = fe->ExtractSignatures(*source, analysis_path, module_name, extraction_diags,
                                              frontend_options);
    if (opts_.diagnostics)
      opts_.diagnostics->Append(extraction_diags);

    if (extraction_diags.HasErrors()) {
      if (opts_.verbose) {
        std::cerr << "[foreign-sig] Refusing partial signatures from " << analysis_path << "\n"
                  << extraction_diags.FormatAll() << "\n";
      }
      continue;
    }

    if (opts_.verbose) {
      std::cerr << "[foreign-sig] Extracted " << foreign_sigs.size() << " signature(s) from "
                << analysis_path << "\n";
    }

    // Convert ForeignFunctionSignature → ploy::FunctionSignature into a
    // module-local transaction.  The Ploy registry is currently keyed by one
    // string and cannot represent overload sets, default/rest/variadic call
    // shapes, or two distinct functions sharing a convenience alias.  Any
    // distinct collision therefore rejects the entire imported module; an
    // identical duplicate declaration is safe to deduplicate.
    std::unordered_map<std::string, ploy::FunctionSignature> module_signatures;
    bool module_conflict = false;
    const auto add_signature_key = [&](const std::string &key,
                                       const ploy::FunctionSignature &signature) {
      if (key.empty()) {
        report_failure(import->loc, frontends::ErrorCode::kUnsupportedSyntax,
                       "foreign frontend produced an empty signature key for import '" +
                           language + "::" + module_name + "'");
        return false;
      }

      if (auto local = module_signatures.find(key); local != module_signatures.end()) {
        if (EquivalentSignature(local->second, signature))
          return true;
        report_failure(
            import->loc, frontends::ErrorCode::kSignatureMismatch,
            "distinct foreign declarations collapse onto signature key '" + key +
                "' in import '" + language + "::" + module_name +
                "'; overload and alias collisions are not representable");
        return false;
      }

      if (auto existing = result.find(key); existing != result.end()) {
        if (EquivalentSignature(existing->second, signature))
          return true;
        report_failure(
            import->loc, frontends::ErrorCode::kSignatureMismatch,
            "foreign signature alias '" + key +
                "' conflicts with a different imported signature; refusing module '" +
                language + "::" + module_name + "'");
        return false;
      }

      module_signatures.emplace(key, signature);
      return true;
    };

    for (const auto &fsig : foreign_sigs) {
      ploy::FunctionSignature sig;
      sig.name = fsig.qualified_name;
      sig.language = canonical_language;
      sig.param_types = fsig.param_types;
      sig.param_names = fsig.param_names;
      sig.return_type = fsig.return_type;
      // ForeignFunctionSignature currently models only fixed arity.  Do not
      // infer variadic/default/rest semantics that the model cannot carry.
      sig.param_count = fsig.param_types.size();
      sig.param_count_known = true;
      sig.validated = false;

      std::vector<std::string> keys;
      const auto add_unique_key = [&](std::string key) {
        if (!key.empty() && std::find(keys.begin(), keys.end(), key) == keys.end())
          keys.push_back(std::move(key));
      };
      add_unique_key(fsig.qualified_name);
      // A method is callable through its owning type, not as a module-level
      // or process-global short function. Publishing `close`, `gate`, etc. as
      // convenience aliases made unrelated package classes collide and could
      // roll back an otherwise valid module transaction. Free functions keep
      // the historical qualified and short aliases used by Poly CALL sites.
      if (!fsig.is_method) {
        if (!module_name.empty() && !fsig.name.empty())
          add_unique_key(module_name + "::" + fsig.name);
        if (!fsig.name.empty() && fsig.name != fsig.qualified_name)
          add_unique_key(fsig.name);
      }

      if (keys.empty()) {
        add_signature_key({}, sig);
        module_conflict = true;
        break;
      }
      for (const auto &key : keys) {
        if (!add_signature_key(key, sig)) {
          module_conflict = true;
          break;
        }
      }
      if (module_conflict)
        break;
    }

    if (module_conflict)
      continue;
    result.insert(module_signatures.begin(), module_signatures.end());
  }

  return result;
}

} // namespace polyglot::tools
