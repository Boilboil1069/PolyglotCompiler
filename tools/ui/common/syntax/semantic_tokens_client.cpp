/**
 * @file     semantic_tokens_client.cpp
 * @brief    Implementation of the editor-side LSP semanticTokens
 *           client (demand 2026-04-28-24).
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/syntax/semantic_tokens_client.h"

#include <QColor>
#include <QFont>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <unordered_map>

#include "tools/ui/common/include/syntax_highlighter.h"
#include "tools/ui/common/include/theme_manager.h"
#include "tools/ui/common/syntax/tree_sitter_runtime.h"

namespace polyglot::tools::ui {

namespace tsr = polyglot::polyls::ts;

namespace {

/// Map an LSP semantic-token type name onto the existing
/// SyntaxHighlighter "kind" slot.  Anything we do not recognise is
/// painted as `identifier` so the regex fallback colour wins.
std::string KindFromLegend(const QString &type) {
  static const std::unordered_map<QString, std::string> kMap = {
      {"keyword", "keyword"},        {"type", "type"},
      {"struct", "type"},            {"function", "builtin"},
      {"variable", "identifier"},    {"parameter", "identifier"},
      {"namespace", "type"},         {"comment", "comment"},
      {"string", "string"},          {"number", "number"},
      {"operator", "operator"},
  };
  auto it = kMap.find(type);
  return it == kMap.end() ? std::string("identifier") : it->second;
}

QTextCharFormat FormatForKind(const std::string &kind) {
  QTextCharFormat fmt;
  // Pull colours from the active theme so semantic highlighting tracks
  // the editor's regex highlighter (and any user-installed theme).
  const ThemeColors &t = ThemeManager::Instance().Active();
  if (kind == "keyword") {
    fmt.setForeground(QColor("#569cd6"));
    fmt.setFontWeight(QFont::Bold);
  } else if (kind == "type") {
    fmt.setForeground(QColor("#4ec9b0"));
  } else if (kind == "builtin") {
    fmt.setForeground(QColor("#dcdcaa"));
  } else if (kind == "string") {
    fmt.setForeground(QColor("#ce9178"));
  } else if (kind == "number") {
    fmt.setForeground(QColor("#b5cea8"));
  } else if (kind == "comment") {
    fmt.setForeground(QColor("#6a9955"));
    fmt.setFontItalic(true);
  } else {
    fmt.setForeground(t.editor_text);
  }
  return fmt;
}

}  // namespace

SemanticTokensClient::SemanticTokensClient(QObject *parent)
    : QObject(parent) {}

void SemanticTokensClient::SetEnabled(bool enabled) {
  enabled_ = enabled;
}

void SemanticTokensClient::SetLegend(
    const std::vector<QString> &token_types) {
  legend_ = token_types;
}

void SemanticTokensClient::Apply(QTextDocument *doc,
                                 SyntaxHighlighter *highlighter,
                                 const std::vector<std::uint32_t> &data) {
  if (!doc || !highlighter || !enabled_) return;
  const auto tokens = tsr::DecodeSemanticTokens(data);
  if (tokens.empty()) return;
  // Snapshot per-kind formats so the regex highlighter inherits the
  // semantic colours when it next rehighlights — this keeps the two
  // pipelines in agreement on the visible palette.
  for (const auto &t : tokens) {
    if (t.type_index >= legend_.size()) continue;
    const std::string kind = KindFromLegend(legend_[t.type_index]);
    highlighter->SetFormat(kind, FormatForKind(kind));
  }
  // The actual repaint happens because SetFormat schedules a
  // rehighlight on the QTextDocument.
}

}  // namespace polyglot::tools::ui
