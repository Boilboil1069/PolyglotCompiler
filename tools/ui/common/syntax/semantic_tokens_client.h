/**
 * @file     semantic_tokens_client.h
 * @brief    Qt-side consumer for LSP `textDocument/semanticTokens`
 *           (demand 2026-04-28-24).
 *
 * The client receives the delta-encoded uint32 stream sent by polyls,
 * decodes it through @ref polyglot::polyls::ts::DecodeSemanticTokens
 * and overlays the resulting spans onto a @ref QSyntaxHighlighter so
 * the editor renders them on top of (or in lieu of) the existing
 * regex-driven `SyntaxHighlighter`.
 *
 * Activation is controlled by the `editor/useLspSemanticTokens`
 * setting (default `true`).  When disabled, or when the server has
 * no semanticTokens capability, the editor keeps its regex highlighter
 * as a fallback.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <QObject>
#include <QString>
#include <QTextCharFormat>
#include <cstdint>
#include <vector>

class QTextDocument;

namespace polyglot::tools::ui {

class SyntaxHighlighter;

/// Encapsulates the per-document state needed to apply LSP semantic
/// tokens to a Qt editor buffer.  One instance per open editor.
class SemanticTokensClient : public QObject {
  Q_OBJECT
 public:
  explicit SemanticTokensClient(QObject *parent = nullptr);

  /// Toggle the feature globally.  When disabled, @ref Apply becomes
  /// a no-op and any previously applied formats are cleared.
  void SetEnabled(bool enabled);
  bool IsEnabled() const { return enabled_; }

  /// Server-advertised legend (token type names).  Used to translate
  /// the numeric `type_index` produced by the wire decoder into the
  /// theme color slot we should paint with.
  void SetLegend(const std::vector<QString> &token_types);

  /// Decode @p data and apply the resulting spans to @p doc by handing
  /// them to @p highlighter via per-kind `QTextCharFormat`s.  Replaces
  /// any previously applied semantic-token formats on the document.
  void Apply(QTextDocument *doc, SyntaxHighlighter *highlighter,
             const std::vector<std::uint32_t> &data);

 private:
  bool enabled_{true};
  std::vector<QString> legend_;
};

}  // namespace polyglot::tools::ui
