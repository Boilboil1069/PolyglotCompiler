// compiler_service.h — Compiler integration service for the UI layer.
//
// Provides APIs for syntax analysis, diagnostics, compilation, and
// auto-completion that the HTTP server exposes to the web front-end.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "frontends/common/include/diagnostics.h"
#include "frontends/common/include/lexer_base.h"

namespace polyglot::tools::ui {

// ============================================================================
// Token Info — lightweight token data transferred to the UI
// ============================================================================

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

struct DiagnosticInfo {
    size_t line{0};
    size_t column{0};
    size_t end_line{0};
    size_t end_column{0};
    std::string severity;   // "error", "warning", "note"
    std::string message;
    std::string code;       // "E1001", "E3004", etc.
    std::string suggestion;

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

struct CompletionItem {
    std::string label;
    std::string kind;       // "keyword", "function", "variable", "type", "snippet"
    std::string detail;
    std::string insert_text;
};

// ============================================================================
// Compile Result
// ============================================================================

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
    std::vector<TokenInfo> TokenizePloy(const std::string &source) const;
    std::vector<TokenInfo> TokenizeCpp(const std::string &source) const;
    std::vector<TokenInfo> TokenizePython(const std::string &source) const;
    std::vector<TokenInfo> TokenizeRust(const std::string &source) const;
    std::vector<TokenInfo> TokenizeJava(const std::string &source) const;
    std::vector<TokenInfo> TokenizeCSharp(const std::string &source) const;

    std::vector<DiagnosticInfo> ConvertDiagnostics(
        const frontends::Diagnostics &diags) const;

    std::vector<CompletionItem> GetPloyCompletions(
        const std::string &source, size_t line, size_t column) const;
};

} // namespace polyglot::tools::ui
