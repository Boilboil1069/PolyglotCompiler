/**
 * @file     diagnostics.h
 * @brief    Shared frontend infrastructure
 *
 * @ingroup  Frontend / Common
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <string>
#include <vector>

#include "common/include/core/source_loc.h"

namespace polyglot::frontends {

// ============================================================================
// Diagnostic Severity
// ============================================================================

/** @brief DiagnosticSeverity enumeration. */
enum class DiagnosticSeverity {
    kError,     // Fatal error — compilation fails
    kWarning,   // Non-fatal issue — compilation continues
    kNote       // Informational — attached to a previous diagnostic for context
};

// ============================================================================
// Error Code Categories for UI Integration
// ============================================================================

/** @brief ErrorCode enumeration. */
enum class ErrorCode {
    kUnknown = 0,

    // Lexer errors (1xxx)
    kUnexpectedCharacter = 1001,
    kUnterminatedString  = 1002,
    kUnterminatedComment = 1003,

    // Parser errors (2xxx)
    kUnexpectedToken      = 2001,
    kMissingSemicolon      = 2002,
    kMissingClosingBrace   = 2003,
    kMissingClosingParen   = 2004,
    kInvalidExpression     = 2005,

    // Semantic errors (3xxx)
    kUndefinedSymbol       = 3001,
    kRedefinedSymbol       = 3002,
    kTypeMismatch          = 3003,
    kParamCountMismatch    = 3004,
    kImmutableAssignment   = 3005,
    kBreakOutsideLoop      = 3006,
    kContinueOutsideLoop   = 3007,
    kInvalidLanguage       = 3008,
    kEmptySymbolName       = 3009,
    kReturnTypeMismatch    = 3010,
    kMissingExpression     = 3011,
    kVersionConstraintFail = 3012,
    kDuplicateField        = 3013,
    kUnknownField          = 3014,
    kSelectiveAliasConflict = 3015,
    kDuplicateConfig       = 3016,
    kMissingTypeAnnotation = 3017,
    kUnusedVariable        = 3018,
    kUnusedCallResult      = 3019,
    kUnreachableCode       = 3020,
    kABIIncompatible       = 3021,
    kOpaqueTypeFallback    = 3022,
    kSignatureMissing      = 3023,
    kGenericWarning        = 3099,

    // Lowering errors (4xxx)
    kLoweringUndefined     = 4001,
    kUnsupportedOperator   = 4002,

    // Linker errors (5xxx)
    kUnresolvedSymbol      = 5001,
    kDuplicateExport       = 5002,
    kSignatureMismatch     = 5003,
    kABICrossModuleMismatch = 5004
};

// ============================================================================
// Diagnostic Structure — UI-Ready
// ============================================================================

/** @brief Diagnostic data structure. */
struct Diagnostic {
    core::SourceLoc loc{};
    std::string message;
    DiagnosticSeverity severity{DiagnosticSeverity::kError};
    ErrorCode code{ErrorCode::kUnknown};

    // Related diagnostics forming a traceback chain (e.g., "defined here", "called from")
    std::vector<Diagnostic> related;

    // Suggestion for fixing the error (displayed in UI as a quick-fix hint)
    std::string suggestion;
};

// ============================================================================
// Diagnostics Container — Collects all diagnostics during compilation
// ============================================================================

/** @brief Diagnostics class. */
class Diagnostics {
  public:
    // Simple error report (backward-compatible)
    void Report(const core::SourceLoc &loc, const std::string &message) {
        Diagnostic d;
        d.loc = loc;
        d.message = message;
        d.severity = DiagnosticSeverity::kError;
        d.code = ErrorCode::kUnknown;
        diagnostics_.push_back(d);
    }

    // Error with error code
    void ReportError(const core::SourceLoc &loc, ErrorCode code, const std::string &message) {
        Diagnostic d;
        d.loc = loc;
        d.message = message;
        d.severity = DiagnosticSeverity::kError;
        d.code = code;
        diagnostics_.push_back(d);
    }

    // Error with error code and suggestion
    void ReportError(const core::SourceLoc &loc, ErrorCode code, const std::string &message,
                     const std::string &suggestion) {
        Diagnostic d;
        d.loc = loc;
        d.message = message;
        d.severity = DiagnosticSeverity::kError;
        d.code = code;
        d.suggestion = suggestion;
        diagnostics_.push_back(d);
    }

    // Error with traceback (related locations)
    void ReportErrorWithTraceback(const core::SourceLoc &loc, ErrorCode code,
                                  const std::string &message,
                                  const std::vector<Diagnostic> &related) {
        Diagnostic d;
        d.loc = loc;
        d.message = message;
        d.severity = DiagnosticSeverity::kError;
        d.code = code;
        d.related = related;
        diagnostics_.push_back(d);
    }

    // Warning report
    void ReportWarning(const core::SourceLoc &loc, ErrorCode code, const std::string &message) {
        Diagnostic d;
        d.loc = loc;
        d.message = message;
        d.severity = DiagnosticSeverity::kWarning;
        d.code = code;
        diagnostics_.push_back(d);
    }

    // Warning with suggestion
    void ReportWarning(const core::SourceLoc &loc, ErrorCode code, const std::string &message,
                       const std::string &suggestion) {
        Diagnostic d;
        d.loc = loc;
        d.message = message;
        d.severity = DiagnosticSeverity::kWarning;
        d.code = code;
        d.suggestion = suggestion;
        diagnostics_.push_back(d);
    }

    // Note (attached to a previous diagnostic for additional context)
    void ReportNote(const core::SourceLoc &loc, const std::string &message) {
        Diagnostic d;
        d.loc = loc;
        d.message = message;
        d.severity = DiagnosticSeverity::kNote;
        d.code = ErrorCode::kUnknown;
        // If there is a previous diagnostic, attach as related
        if (!diagnostics_.empty()) {
            diagnostics_.back().related.push_back(d);
        } else {
            diagnostics_.push_back(d);
        }
    }

    const std::vector<Diagnostic> &All() const { return diagnostics_; }

    bool HasErrors() const {
        for (const auto &d : diagnostics_) {
            if (d.severity == DiagnosticSeverity::kError) return true;
        }
        return false;
    }

    bool HasWarnings() const {
        for (const auto &d : diagnostics_) {
            if (d.severity == DiagnosticSeverity::kWarning) return true;
        }
        return false;
    }

    size_t ErrorCount() const {
        size_t count = 0;
        for (const auto &d : diagnostics_) {
            if (d.severity == DiagnosticSeverity::kError) ++count;
        }
        return count;
    }

    size_t WarningCount() const {
        size_t count = 0;
        for (const auto &d : diagnostics_) {
            if (d.severity == DiagnosticSeverity::kWarning) ++count;
        }
        return count;
    }

    // Format a single diagnostic for console output
    static std::string Format(const Diagnostic &d) {
        std::string severity_str;
        switch (d.severity) {
            case DiagnosticSeverity::kError:   severity_str = "error"; break;
            case DiagnosticSeverity::kWarning: severity_str = "warning"; break;
            case DiagnosticSeverity::kNote:    severity_str = "note"; break;
        }

        std::string result = d.loc.file + ":" + std::to_string(d.loc.line) + ":" +
                             std::to_string(d.loc.column) + ": " + severity_str;
        if (d.code != ErrorCode::kUnknown) {
            result += " [E" + std::to_string(static_cast<int>(d.code)) + "]";
        }
        result += ": " + d.message;

        if (!d.suggestion.empty()) {
            result += "\n  suggestion: " + d.suggestion;
        }

        for (const auto &rel : d.related) {
            result += "\n  " + rel.loc.file + ":" + std::to_string(rel.loc.line) + ":" +
                      std::to_string(rel.loc.column) + ": note: " + rel.message;
        }

        return result;
    }

    // Format all diagnostics for console output
    std::string FormatAll() const {
        std::string result;
        for (const auto &d : diagnostics_) {
            if (!result.empty()) result += "\n";
            result += Format(d);
        }
        return result;
    }

  private:
    std::vector<Diagnostic> diagnostics_{};
};

} // namespace polyglot::frontends
