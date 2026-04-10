// ============================================================================
// foreign_signature_extractor.h — Extract function signatures from foreign
// source files referenced by IMPORT declarations in .ploy modules.
// ============================================================================

#pragma once

#include <memory>
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
// Given a parsed .ploy Module, walks all IMPORT declarations that reference
// foreign-language source files (e.g., IMPORT cpp::math_ops;).
// For each import, it:
//   1. Locates the corresponding source file on disk.
//   2. Reads the source, invokes the language frontend's ExtractSignatures().
//   3. Converts ForeignFunctionSignature → ploy::FunctionSignature.
//   4. Returns a map suitable for injection into PloySema::KnownSignatures().
// ============================================================================

struct ForeignExtractionOptions {
    /// Directory containing the .ploy file (used as base for relative paths).
    std::string base_directory;

    /// Additional search directories for foreign source files.
    std::vector<std::string> include_paths;

    /// Whether to emit verbose logging to stderr.
    bool verbose{false};
};

class ForeignSignatureExtractor {
  public:
    explicit ForeignSignatureExtractor(const ForeignExtractionOptions &opts);

    /// Walk the ploy Module's IMPORT declarations and extract signatures from
    /// all referenced foreign-language source files.
    /// Returns a map of qualified_name → FunctionSignature.
    std::unordered_map<std::string, ploy::FunctionSignature>
    ExtractAll(const ploy::Module &module) const;

  private:
    /// Attempt to locate a source file for the given language + module name.
    /// Returns the full path if found, or empty string if not found.
    std::string ResolveSourceFile(const std::string &language,
                                  const std::string &module_name) const;

    /// Read a file into a string.  Returns empty string on failure.
    static std::string ReadFile(const std::string &path);

    ForeignExtractionOptions opts_;
};

}  // namespace polyglot::tools
