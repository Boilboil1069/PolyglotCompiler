// ============================================================================
// foreign_signature_extractor.cpp — Implementation
// ============================================================================

#include "tools/polyc/src/foreign_signature_extractor.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "frontends/common/include/frontend_registry.h"

namespace polyglot::tools {

namespace fs = std::filesystem;

// ============================================================================
// Construction
// ============================================================================

ForeignSignatureExtractor::ForeignSignatureExtractor(
    const ForeignExtractionOptions &opts)
    : opts_(opts) {}

// ============================================================================
// File resolution
// ============================================================================

std::string ForeignSignatureExtractor::ResolveSourceFile(
    const std::string &language,
    const std::string &module_name) const {

    // Get the frontend to retrieve possible file extensions.
    const auto *fe =
        frontends::FrontendRegistry::Instance().GetFrontend(language);
    if (!fe) return {};

    const auto extensions = fe->Extensions();

    // Search order:
    //   1. base_directory / <module_name>.<ext>
    //   2. Each include_path  / <module_name>.<ext>
    // For case sensitivity, try the module name as-is (common on POSIX).

    auto try_dirs = [&](const std::string &dir) -> std::string {
        for (const auto &ext : extensions) {
            fs::path candidate = fs::path(dir) / (module_name + ext);
            if (fs::exists(candidate)) return candidate.string();
        }
        return {};
    };

    // Primary: base directory
    if (!opts_.base_directory.empty()) {
        auto found = try_dirs(opts_.base_directory);
        if (!found.empty()) return found;
    }

    // Secondary: include paths
    for (const auto &inc : opts_.include_paths) {
        auto found = try_dirs(inc);
        if (!found.empty()) return found;
    }

    return {};
}

// ============================================================================
// ReadFile helper
// ============================================================================

std::string ForeignSignatureExtractor::ReadFile(const std::string &path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) return {};
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

// ============================================================================
// ExtractAll — main entry point
// ============================================================================

std::unordered_map<std::string, ploy::FunctionSignature>
ForeignSignatureExtractor::ExtractAll(const ploy::Module &module) const {

    std::unordered_map<std::string, ploy::FunctionSignature> result;

    for (const auto &decl : module.declarations) {
        auto import = std::dynamic_pointer_cast<ploy::ImportDecl>(decl);
        if (!import) continue;

        // Only process local file imports (not PACKAGE imports).
        // Pattern: IMPORT <language>::<module_name>;
        // e.g., IMPORT cpp::math_ops;
        if (import->language.empty()) continue;
        if (!import->package_name.empty()) continue;  // skip PACKAGE imports
        if (import->module_path.empty()) continue;

        const std::string &language = import->language;
        const std::string &module_name = import->module_path;

        // Don't process ploy-to-ploy imports
        if (language == "ploy") continue;

        // Resolve the source file on disk.
        std::string file_path = ResolveSourceFile(language, module_name);
        if (file_path.empty()) {
            if (opts_.verbose) {
                std::cerr << "[foreign-sig] Could not locate source file for "
                          << language << "::" << module_name << "\n";
            }
            continue;
        }

        // Read and parse the file.
        std::string source = ReadFile(file_path);
        if (source.empty()) {
            if (opts_.verbose) {
                std::cerr << "[foreign-sig] Could not read " << file_path << "\n";
            }
            continue;
        }

        // Get the frontend and extract signatures.
        const auto *fe =
            frontends::FrontendRegistry::Instance().GetFrontend(language);
        if (!fe) continue;

        auto foreign_sigs =
            fe->ExtractSignatures(source, file_path, module_name);

        if (opts_.verbose) {
            std::cerr << "[foreign-sig] Extracted " << foreign_sigs.size()
                      << " signature(s) from " << file_path << "\n";
        }

        // Convert ForeignFunctionSignature → ploy::FunctionSignature and
        // insert into the result map.
        for (const auto &fsig : foreign_sigs) {
            ploy::FunctionSignature sig;
            sig.name = fsig.qualified_name;
            sig.language = language;
            sig.param_types = fsig.param_types;
            sig.param_names = fsig.param_names;
            sig.return_type = fsig.return_type;
            sig.param_count = fsig.param_types.size();
            sig.param_count_known = true;
            sig.validated = false;

            // Register under both the qualified name and the short name.
            // e.g., "math_ops::add" and also "add" (if not already present).
            result[fsig.qualified_name] = sig;

            // Also register the short name for convenience —
            // LINK declarations may use short names.
            if (!fsig.name.empty() && fsig.name != fsig.qualified_name) {
                if (result.find(fsig.name) == result.end()) {
                    result[fsig.name] = sig;
                }
            }
        }
    }

    return result;
}

}  // namespace polyglot::tools
