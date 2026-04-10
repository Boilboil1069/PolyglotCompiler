/**
 * @file     compiler_service.h
 * @brief    Compiler integration service for the UI layer
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "frontends/common/include/diagnostics.h"
#include "frontends/common/include/frontend_registry.h"
#include "frontends/common/include/lexer_base.h"

namespace polyglot::tools::ui {

// ============================================================================
// Token Info — lightweight token data transferred to the UI
// ============================================================================

/** @brief TokenInfo data structure. */
struct TokenInfo {
    size_t line{0};
    size_t column{0};
    size_t length{0};
    std::string kind;   // "keyword", "identifier", "number", "string", "comment", "operator", "type", "builtin"
    std::string lexeme;
};

// ============================================================================
// Diagnostic Info — UI-friendly diagnostic representation
// ============================================================================

/** @brief DiagnosticInfo data structure. */
struct DiagnosticInfo {
    size_t line{0};
    size_t column{0};
    size_t end_line{0};
    size_t end_column{0};
    std::string severity;   // "error", "warning", "note"
    std::string message;
    std::string code;       // "E1001", "E3004", etc.
    std::string suggestion;

    /** @brief Related data structure. */
    struct Related {
        size_t line{0};
        size_t column{0};
        std::string message;
    };
    std::vector<Related> related;
};

// ============================================================================
// Completion Item — auto-completion suggestions
// ============================================================================

/** @brief CompletionItem data structure. */
struct CompletionItem {
    std::string label;
    std::string kind;       // "keyword", "function", "variable", "type", "snippet"
    std::string detail;
    std::string insert_text;
};

// ============================================================================
// Compile Result
// ============================================================================

/** @brief CompileResult data structure. */
struct CompileResult {
    bool success{false};
    std::string output;
    std::string assembly;
    std::vector<DiagnosticInfo> diagnostics;
    double elapsed_ms{0.0};
};

// ============================================================================
// CompilerService — main interface consumed by the HTTP server
// ============================================================================

/** @brief CompilerService class. */
class CompilerService {
  public:
    CompilerService();
    ~CompilerService();

    // Tokenize source code for syntax highlighting
    std::vector<TokenInfo> Tokenize(const std::string &source,
                                    const std::string &language) const;

    // Analyze source code and return diagnostics (errors + warnings)
    std::vector<DiagnosticInfo> Analyze(const std::string &source,
                                        const std::string &language,
                                        const std::string &filename) const;

    // Full compilation pipeline
    CompileResult Compile(const std::string &source,
                          const std::string &language,
                          const std::string &filename,
                          const std::string &target_arch,
                          int opt_level) const;

    // Auto-completion at a given cursor position
    std::vector<CompletionItem> Complete(const std::string &source,
                                         const std::string &language,
                                         size_t line,
                                         size_t column) const;

    // Return supported languages list
    std::vector<std::string> SupportedLanguages() const;

  private:
    // Internal helpers
    std::vector<DiagnosticInfo> ConvertDiagnostics(
        const frontends::Diagnostics &diags) const;

    std::vector<CompletionItem> GetPloyCompletions(
        const std::string &source, size_t line, size_t column) const;
};

} // namespace polyglot::tools::ui
