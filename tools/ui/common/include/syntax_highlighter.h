// syntax_highlighter.h — Syntax highlighter that uses the compiler's lexer.
//
// Integrates with the PolyglotCompiler frontend tokenizers to provide
// accurate, language-aware syntax highlighting in the code editor.

#pragma once

#include <QSyntaxHighlighter>
#include <QTextCharFormat>

#include <string>
#include <unordered_map>
#include <vector>

namespace polyglot::tools::ui {

class CompilerService;

// ============================================================================
// SyntaxHighlighter — compiler-driven syntax coloring
// ============================================================================

class SyntaxHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

  public:
    SyntaxHighlighter(QTextDocument *document, CompilerService *service,
                      const std::string &language);
    ~SyntaxHighlighter() override;

    void SetLanguage(const std::string &language);
    std::string Language() const { return language_; }

    // Theme — set format for a given token kind
    void SetFormat(const std::string &kind, const QTextCharFormat &fmt);

  protected:
    void highlightBlock(const QString &text) override;

  private:
    void InitDefaultFormats();

    CompilerService *compiler_service_;
    std::string language_;
    std::unordered_map<std::string, QTextCharFormat> formats_;
};

} // namespace polyglot::tools::ui
