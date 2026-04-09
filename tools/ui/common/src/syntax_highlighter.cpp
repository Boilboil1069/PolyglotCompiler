// syntax_highlighter.cpp — Compiler-driven syntax highlighting implementation.
//
// Uses the PolyglotCompiler frontend tokenizers (via CompilerService) to
// tokenize each block of text and apply the appropriate formatting.
//
// Ploy-specific notes:
//   • Cross-language directive keywords (LINK, IMPORT, EXPORT, MAP_TYPE,
//     PIPELINE, EXTEND, CALL, NEW, METHOD, GET, SET, WITH, DELETE, CONVERT,
//     MAP_FUNC, CONFIG) are displayed in a distinct "link" purple/magenta
//     color so they visually stand out from control-flow keywords.
//   • Primitive type keywords (INT, FLOAT, STRING, …) are shown in teal
//     via the "type" format, matching the convention used for type names in
//     other supported languages.
//   • Language qualifier identifiers (cpp, python, rust, …) are shown in
//     yellow via the "builtin" format because they name external runtimes.

#include "tools/ui/common/include/syntax_highlighter.h"
#include "tools/ui/common/include/compiler_service.h"

#include <QColor>
#include <QFont>

#include <string>
#include <unordered_set>

namespace polyglot::tools::ui {

// ============================================================================
// Construction
// ============================================================================

SyntaxHighlighter::SyntaxHighlighter(QTextDocument *document,
                                     CompilerService *service,
                                     const std::string &language)
    : QSyntaxHighlighter(document),
      compiler_service_(service),
      language_(language) {
    InitDefaultFormats();
}

SyntaxHighlighter::~SyntaxHighlighter() = default;

// ============================================================================
// Language
// ============================================================================

void SyntaxHighlighter::SetLanguage(const std::string &language) {
    language_ = language;
    rehighlight();
}

// ============================================================================
// Custom format
// ============================================================================

void SyntaxHighlighter::SetFormat(const std::string &kind,
                                  const QTextCharFormat &fmt) {
    formats_[kind] = fmt;
    rehighlight();
}

// ============================================================================
// Default color scheme (dark theme)
// ============================================================================

void SyntaxHighlighter::InitDefaultFormats() {
    QTextCharFormat kw_fmt;
    kw_fmt.setForeground(QColor(86, 156, 214));   // Blue
    kw_fmt.setFontWeight(QFont::Bold);
    formats_["keyword"] = kw_fmt;

    QTextCharFormat type_fmt;
    type_fmt.setForeground(QColor(78, 201, 176));  // Teal
    formats_["type"] = type_fmt;

    QTextCharFormat builtin_fmt;
    builtin_fmt.setForeground(QColor(220, 220, 170)); // Yellow-ish
    formats_["builtin"] = builtin_fmt;

    QTextCharFormat num_fmt;
    num_fmt.setForeground(QColor(181, 206, 168));  // Green
    formats_["number"] = num_fmt;

    QTextCharFormat str_fmt;
    str_fmt.setForeground(QColor(206, 145, 120));  // Orange
    formats_["string"] = str_fmt;

    QTextCharFormat comment_fmt;
    comment_fmt.setForeground(QColor(106, 153, 85)); // Dark green
    comment_fmt.setFontItalic(true);
    formats_["comment"] = comment_fmt;

    QTextCharFormat op_fmt;
    op_fmt.setForeground(QColor(212, 212, 212));   // Light grey
    formats_["operator"] = op_fmt;

    QTextCharFormat id_fmt;
    id_fmt.setForeground(QColor(156, 220, 254));   // Light blue
    formats_["identifier"] = id_fmt;

    QTextCharFormat pp_fmt;
    pp_fmt.setForeground(QColor(155, 155, 155));   // Grey
    formats_["preprocessor"] = pp_fmt;

    QTextCharFormat plain_fmt;
    plain_fmt.setForeground(QColor(212, 212, 212));
    formats_["plain"] = plain_fmt;

    // Ploy cross-language directive keywords (LINK, IMPORT, EXPORT, …)
    // Displayed in a vivid magenta/purple to distinguish them from ordinary
    // control-flow keywords.
    QTextCharFormat link_fmt;
    link_fmt.setForeground(QColor(197, 134, 192)); // Purple-magenta
    link_fmt.setFontWeight(QFont::Bold);
    formats_["link"] = link_fmt;
}

// ============================================================================
// Highlight Block
// ============================================================================

void SyntaxHighlighter::highlightBlock(const QString &text) {
    if (!compiler_service_ || text.isEmpty()) return;

    // Tokenize the current line
    std::string source = text.toStdString();
    auto tokens = compiler_service_->Tokenize(source, language_);

    // For Ploy files, cross-language directive keywords get their own "link"
    // color format so they stand out from control-flow keywords (IF/WHILE/…).
    // This set mirrors the directive keywords defined in the Ploy lexer.
    static const std::unordered_set<std::string> ploy_link_keywords = {
        "LINK", "IMPORT", "EXPORT", "MAP_TYPE", "PIPELINE",
        "CALL", "NEW",    "METHOD", "GET",       "SET",
        "WITH", "DELETE", "EXTEND", "CONVERT",   "MAP_FUNC",
        "CONFIG"
    };

    const bool is_ploy = (language_ == "ploy");

    for (const auto &tok : tokens) {
        int start = static_cast<int>(tok.column) - 1; // 1-based to 0-based
        if (start < 0) start = 0;
        int length = static_cast<int>(tok.length);

        if (start + length > text.length()) {
            length = text.length() - start;
        }
        if (length <= 0) continue;

        // For Ploy: override keyword format for cross-language directive keywords
        std::string kind = tok.kind;
        if (is_ploy && kind == "keyword" && ploy_link_keywords.count(tok.lexeme)) {
            kind = "link";
        }

        auto it = formats_.find(kind);
        if (it != formats_.end()) {
            setFormat(start, length, it->second);
        }
    }
}

} // namespace polyglot::tools::ui
