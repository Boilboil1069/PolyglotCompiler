/**
 * @file     foreign_signature_extractor.h
 * @brief    Compiler driver implementation
 *
 * @ingroup  Tool / polyc
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
// ============================================================================
// foreign_signature_extractor.h — Extract function signatures from foreign
// source files referenced by IMPORT declarations in .poly modules.
// ============================================================================

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "frontends/common/include/language_frontend.h"
#include "frontends/ploy/include/ploy_ast.h"
#include "frontends/ploy/include/ploy_sema.h"

namespace polyglot::tools {

// ============================================================================
// ForeignSignatureExtractor
//
// Given a parsed .poly Module, walks all IMPORT declarations that reference
// foreign-language source files (e.g., IMPORT cpp::math_ops;).
// For each import, it:
//   1. Locates the corresponding source file on disk.
//   2. Reads the source, invokes the language frontend's ExtractSignatures().
//   3. Converts ForeignFunctionSignature → ploy::FunctionSignature.
//   4. Returns a map suitable for injection into PloySema::KnownSignatures().
// ============================================================================

/** @brief ForeignExtractionOptions data structure. */
struct ForeignExtractionOptions {
  /// Directory containing the .poly file (used as base for relative paths).
  std::string base_directory;

  /// Original .poly entry and writable auxiliary directory.  When both are
  /// set, project-local PACKAGE manifests may contribute source to the same
  /// bundled unit used later by the packaging stage.
  std::string poly_source_file;
  std::string bundle_directory;
  bool require_local_source_packages{false};

  /// Additional search directories for foreign source files.
  std::vector<std::string> include_paths;

  /// Whether to emit verbose logging to stderr.
  bool verbose{false};

  /// Language versions and project/toolchain inputs used while analysing the
  /// imported source.  Signature extraction must use the same dialect as the
  /// main frontend pipeline.
  frontends::FrontendOptions frontend_options{};

  /// Optional sink for resolution/read/lexer/parser/sema/signature-collision
  /// failures from imported sources.  Compiler callers should always provide
  /// it; when null, extraction still fails closed but diagnostics are only
  /// printed in verbose mode.
  frontends::Diagnostics *diagnostics{nullptr};
};

/** @brief ForeignSignatureExtractor class. */
class ForeignSignatureExtractor {
public:
  explicit ForeignSignatureExtractor(const ForeignExtractionOptions &opts);

  /// Walk the poly Module's IMPORT declarations and extract signatures from
  /// all referenced foreign-language source files.
  /// Returns a map of qualified_name → FunctionSignature.  Each imported
  /// module is committed atomically: an unrepresentable overload or alias
  /// collision rejects every signature from that module.
  std::unordered_map<std::string, ploy::FunctionSignature> ExtractAll(
      const ploy::Module &module) const;

private:
  /// Attempt to locate a source file for the given language + module name.
  /// Returns the full path if found, or empty string if not found.
  std::string ResolveSourceFile(const std::string &language, const std::string &module_name) const;

  /// Read a file into a string.  An empty string is a valid empty file;
  /// std::nullopt denotes an open/read failure.
  static std::optional<std::string> ReadFile(const std::string &path);

  ForeignExtractionOptions opts_;
};

} // namespace polyglot::tools
