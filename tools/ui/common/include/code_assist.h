/**
 * @file     code_assist.h
 * @brief    Snippet expander, hover tooltip, and signature-help widget
 *
 * The three small Qt widgets and the snippet helper declared here are
 * the editor-side counterparts of the polyls language features added by
 * demand 2026-04-28-21:
 *
 *   • SnippetExpander   — parses LSP-style snippet templates
 *                         (`${1:placeholder}`, `$1`, escaped `\$`),
 *                         expands them into the editor and tracks the
 *                         remaining placeholders so Tab can jump to the
 *                         next stop.
 *   • HoverTooltip      — frameless QLabel that renders Markdown content
 *                         received from `textDocument/hover`.  Auto-
 *                         hides on Esc / focus loss / scroll.
 *   • SignatureHelpWidget — floating tooltip listing one or more
 *                         signatures with the active parameter
 *                         underlined and overload navigation via the
 *                         arrow keys.
 *
 * @ingroup  Tool / polyui / Completion
 * @author   Manning Cyrus
 * @date     2026-05-04
 */
#pragma once

#include <QLabel>
#include <QObject>
#include <QPlainTextEdit>
#include <QPoint>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <utility>

namespace polyglot::tools::ui {

// ---------------------------------------------------------------------------
// SnippetExpander
// ---------------------------------------------------------------------------

/// One placeholder in an expanded snippet, stored as an absolute
/// document range that is updated by Qt's QTextDocument as the user
/// types.
struct SnippetStop {
  int tab_index{0};
  int absolute_start{0};
  int absolute_end{0};
};

class SnippetExpander : public QObject {
  Q_OBJECT
 public:
  explicit SnippetExpander(QPlainTextEdit *editor, QObject *parent = nullptr);

  /// Insert @p snippet at the editor's current cursor.  Any text
  /// previously selected by `replace_word` is overwritten.  Returns
  /// true when the snippet contained at least one tab stop and the
  /// editor is now in "snippet mode" (subsequent Tab presses are
  /// intercepted by @ref AdvanceToNextStop).
  bool ExpandAtCursor(const QString &snippet, bool replace_word);

  /// True when there are unvisited tab stops.
  bool Active() const { return active_index_ < stops_.size(); }

  /// Move the cursor to the next placeholder; selects its current text
  /// so the user can overwrite it by typing.  Returns false when no
  /// further stops remain (the caller should then forward the Tab key
  /// to the editor as a literal indent).
  bool AdvanceToNextStop();

  /// Cancel snippet mode (called on Esc or when the cursor leaves the
  /// snippet region).
  void Cancel();

 private:
  /// Parse @p snippet into plain text and a sorted vector of stops
  /// (relative to the produced text).  Handles `\$`, `$1`, `${1}`,
  /// `${1:default}`.
  static QString Parse(const QString &snippet, QVector<SnippetStop> &stops);

  QPlainTextEdit *editor_{nullptr};
  QVector<SnippetStop> stops_;
  int active_index_{0};
};

// ---------------------------------------------------------------------------
// HoverTooltip
// ---------------------------------------------------------------------------

class HoverTooltip : public QLabel {
  Q_OBJECT
 public:
  explicit HoverTooltip(QWidget *parent = nullptr);

  /// Display @p markdown anchored near @p global_pos.  Empty markdown
  /// hides the tooltip.
  void ShowMarkdown(const QString &markdown, const QPoint &global_pos);
  void HideTooltip();
};

// ---------------------------------------------------------------------------
// SignatureHelpWidget
// ---------------------------------------------------------------------------

struct SignatureRecord {
  QString label;
  QStringList parameter_labels;
  QString documentation;
};

class SignatureHelpWidget : public QWidget {
  Q_OBJECT
 public:
  explicit SignatureHelpWidget(QWidget *parent = nullptr);

  /// Show the @p signatures list.  @p active_signature and
  /// @p active_parameter are clamped to the available range.
  void ShowSignatures(const QVector<SignatureRecord> &signatures,
                      int active_signature, int active_parameter,
                      const QPoint &global_pos);
  void HideHelp();

  /// Switch to the previous / next overload, if any.
  void NextOverload();
  void PrevOverload();
  bool IsActive() const { return isVisible() && !signatures_.isEmpty(); }

 private:
  void Render();

  QLabel *body_{nullptr};
  QVector<SignatureRecord> signatures_;
  int active_signature_{0};
  int active_parameter_{0};
};

}  // namespace polyglot::tools::ui
