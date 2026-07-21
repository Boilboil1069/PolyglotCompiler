/**
 * @file     markdown_viewer.h
 * @brief    Markdown rendering widget for the PolyglotCompiler IDE
 *
 * Renders Markdown documents (CommonMark + a useful subset of GFM as supported
 * by Qt's QTextDocument::setMarkdown()) directly inside an editor tab, so that
 * the IDE can present `.md` / `.markdown` files (project README, USER_GUIDE,
 * spec docs, ...) as formatted documents instead of raw source.  Native Qt
 * rendering is used — no third-party Markdown library is required.  Supports
 * preview/source toggle, theme-aware styling, relative resource resolution,
 * and external-link routing.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-04-27
 */
#pragma once

#include <QString>
#include <QWidget>

class QPlainTextEdit;
class QStackedWidget;
class QTextBrowser;
class QToolBar;
class QToolButton;
class QUrl;

namespace polyglot::tools::ui {

/**
 * @brief Two-mode viewer: rendered Markdown preview <-> raw source view.
 *
 * The widget owns a top tool-bar (with "Preview / Source / Reload" buttons)
 * and a stacked widget that swaps between a QTextBrowser (rendered) and a
 * read-only QPlainTextEdit (raw source).  Files are loaded via LoadFile();
 * subsequent on-disk changes can be picked up by Reload().
 */
class MarkdownViewer : public QWidget {
  Q_OBJECT

 public:
  explicit MarkdownViewer(QWidget *parent = nullptr);
  ~MarkdownViewer() override;

  /// Load the given Markdown file from disk.  Returns true on success.
  bool LoadFile(const QString &path);

  /// Re-read the underlying file from disk and refresh both views.
  void Reload();

  /// Replace the document with in-memory Markdown text (no file backing).
  void SetMarkdown(const QString &markdown, const QString &base_dir = QString());

  /// Absolute path of the file currently displayed (empty for in-memory).
  QString FilePath() const { return file_path_; }

  /// Whether the viewer is currently showing the rendered preview.
  bool IsPreviewMode() const;

  /// Re-apply ThemeManager colours.  Safe to call on theme change.
  void ApplyTheme();

 public slots:
  /// Switch to rendered Markdown preview.
  void ShowPreview();
  /// Switch to raw source (read-only) view.
  void ShowSource();
  /// Toggle between the two modes.
  void TogglePreview();

 signals:
  /// Emitted when the user clicks an external (http/https/mailto) link.
  void ExternalLinkClicked(const QUrl &url);

 private slots:
  void OnAnchorClicked(const QUrl &url);

 private:
  void RenderInto(const QString &markdown, const QString &base_dir);

  QToolBar *toolbar_{nullptr};
  QToolButton *btn_preview_{nullptr};
  QToolButton *btn_source_{nullptr};
  QToolButton *btn_reload_{nullptr};
  QStackedWidget *stack_{nullptr};
  QTextBrowser *preview_{nullptr};
  QPlainTextEdit *source_view_{nullptr};

  QString file_path_;
};

}  // namespace polyglot::tools::ui
