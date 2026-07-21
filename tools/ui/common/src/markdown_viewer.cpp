/**
 * @file     markdown_viewer.cpp
 * @brief    Markdown viewer implementation for the PolyglotCompiler IDE
 *
 * See markdown_viewer.h for design notes.  The rendering itself is delegated
 * to Qt's built-in QTextDocument::setMarkdown(), which supports CommonMark
 * plus a useful subset of GitHub-Flavoured-Markdown (tables, strike-through,
 * task lists, fenced code blocks).  No third-party library is required.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-04-27
 */
#include "tools/ui/common/include/markdown_viewer.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTextStream>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include "tools/ui/common/include/theme_manager.h"

namespace polyglot::tools::ui {

namespace {

// Stylesheet applied to the rendered QTextBrowser to give code blocks /
// blockquotes / tables a distinctive look that adapts to the active theme.
QString BuildPreviewStylesheet(const ThemeColors &tc) {
  return QString(
             "QTextBrowser {"
             "  background: %1;"
             "  color: %2;"
             "  border: none;"
             "  selection-background-color: %3;"
             "  padding: 12px 16px;"
             "}")
      .arg(tc.editor_background.name(), tc.editor_text.name(),
           tc.editor_selection.name());
}

QString BuildSourceStylesheet(const ThemeColors &tc) {
  return QString(
             "QPlainTextEdit {"
             "  background: %1;"
             "  color: %2;"
             "  border: none;"
             "  selection-background-color: %3;"
             "}")
      .arg(tc.editor_background.name(), tc.editor_text.name(),
           tc.editor_selection.name());
}

}  // namespace

MarkdownViewer::MarkdownViewer(QWidget *parent) : QWidget(parent) {
  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  // -- Toolbar ---------------------------------------------------------------
  toolbar_ = new QToolBar(this);
  toolbar_->setMovable(false);
  toolbar_->setIconSize(QSize(16, 16));

  btn_preview_ = new QToolButton(this);
  btn_preview_->setText(tr("Preview"));
  btn_preview_->setCheckable(true);
  btn_preview_->setChecked(true);
  btn_preview_->setToolTip(tr("Show rendered Markdown (Ctrl+Shift+M)"));

  btn_source_ = new QToolButton(this);
  btn_source_->setText(tr("Source"));
  btn_source_->setCheckable(true);
  btn_source_->setToolTip(tr("Show raw Markdown source"));

  btn_reload_ = new QToolButton(this);
  btn_reload_->setText(tr("Reload"));
  btn_reload_->setToolTip(tr("Reload file from disk"));

  toolbar_->addWidget(btn_preview_);
  toolbar_->addWidget(btn_source_);
  toolbar_->addSeparator();
  toolbar_->addWidget(btn_reload_);
  root->addWidget(toolbar_);

  connect(btn_preview_, &QToolButton::clicked, this, &MarkdownViewer::ShowPreview);
  connect(btn_source_, &QToolButton::clicked, this, &MarkdownViewer::ShowSource);
  connect(btn_reload_, &QToolButton::clicked, this, &MarkdownViewer::Reload);

  // -- Stacked content -------------------------------------------------------
  stack_ = new QStackedWidget(this);

  preview_ = new QTextBrowser(this);
  preview_->setOpenLinks(false);              // we route via signal
  preview_->setOpenExternalLinks(false);      // ditto
  preview_->setReadOnly(true);
  preview_->setUndoRedoEnabled(false);
  connect(preview_, &QTextBrowser::anchorClicked, this, &MarkdownViewer::OnAnchorClicked);

  source_view_ = new QPlainTextEdit(this);
  source_view_->setReadOnly(true);
  source_view_->setLineWrapMode(QPlainTextEdit::NoWrap);

  stack_->addWidget(preview_);      // index 0
  stack_->addWidget(source_view_);  // index 1
  stack_->setCurrentIndex(0);
  root->addWidget(stack_, /*stretch=*/1);

  ApplyTheme();
}

MarkdownViewer::~MarkdownViewer() = default;

bool MarkdownViewer::LoadFile(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return false;
  }
  QTextStream in(&f);
  // Force UTF-8 so non-ASCII docs (e.g. USER_GUIDE_zh.md) render correctly
  // regardless of the system locale.
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  in.setEncoding(QStringConverter::Utf8);
#else
  in.setCodec("UTF-8");
#endif
  const QString text = in.readAll();
  f.close();

  file_path_ = path;
  RenderInto(text, QFileInfo(path).absolutePath());
  return true;
}

void MarkdownViewer::Reload() {
  if (file_path_.isEmpty()) return;
  // Preserve scroll positions across reload.
  const int prev_preview = preview_->verticalScrollBar() ? preview_->verticalScrollBar()->value() : 0;
  const int prev_source = source_view_->verticalScrollBar() ? source_view_->verticalScrollBar()->value() : 0;
  if (LoadFile(file_path_)) {
    if (preview_->verticalScrollBar()) preview_->verticalScrollBar()->setValue(prev_preview);
    if (source_view_->verticalScrollBar()) source_view_->verticalScrollBar()->setValue(prev_source);
  }
}

void MarkdownViewer::SetMarkdown(const QString &markdown, const QString &base_dir) {
  file_path_.clear();
  RenderInto(markdown, base_dir);
}

void MarkdownViewer::RenderInto(const QString &markdown, const QString &base_dir) {
  // Make relative <img src="..."> resolve against the source file's directory.
  if (!base_dir.isEmpty()) {
    preview_->setSearchPaths({base_dir});
    preview_->document()->setBaseUrl(QUrl::fromLocalFile(QDir(base_dir).absolutePath() + "/"));
  } else {
    preview_->setSearchPaths({});
    preview_->document()->setBaseUrl(QUrl());
  }

  // Qt's setMarkdown() handles CommonMark + a subset of GFM (tables,
  // task lists, strike-through, fenced code).
  preview_->document()->setMarkdown(markdown, QTextDocument::MarkdownDialectGitHub);
  source_view_->setPlainText(markdown);
}

bool MarkdownViewer::IsPreviewMode() const { return stack_->currentIndex() == 0; }

void MarkdownViewer::ShowPreview() {
  stack_->setCurrentIndex(0);
  btn_preview_->setChecked(true);
  btn_source_->setChecked(false);
}

void MarkdownViewer::ShowSource() {
  stack_->setCurrentIndex(1);
  btn_preview_->setChecked(false);
  btn_source_->setChecked(true);
}

void MarkdownViewer::TogglePreview() {
  if (IsPreviewMode()) {
    ShowSource();
  } else {
    ShowPreview();
  }
}

void MarkdownViewer::ApplyTheme() {
  const auto &tc = ThemeManager::Instance().Active();
  preview_->setStyleSheet(BuildPreviewStylesheet(tc));
  source_view_->setStyleSheet(BuildSourceStylesheet(tc));

  QPalette pal = preview_->palette();
  pal.setColor(QPalette::Base, tc.editor_background);
  pal.setColor(QPalette::Text, tc.editor_text);
  preview_->setPalette(pal);

  QPalette spal = source_view_->palette();
  spal.setColor(QPalette::Base, tc.editor_background);
  spal.setColor(QPalette::Text, tc.editor_text);
  source_view_->setPalette(spal);
}

void MarkdownViewer::OnAnchorClicked(const QUrl &url) {
  // External links → user's default browser; intra-doc anchors → scroll.
  if (url.isRelative() || url.scheme() == "file") {
    // Local file or fragment.  If it points to another markdown file we
    // emit ExternalLinkClicked so MainWindow can decide whether to open it
    // in a new tab; otherwise fall back to internal navigation for #anchors.
    if (url.hasFragment() && url.path().isEmpty()) {
      preview_->scrollToAnchor(url.fragment());
      return;
    }
    emit ExternalLinkClicked(url);
    return;
  }
  // http(s), mailto, etc.
  if (url.scheme() == "http" || url.scheme() == "https" || url.scheme() == "mailto") {
    QDesktopServices::openUrl(url);
    emit ExternalLinkClicked(url);
    return;
  }
  // Unknown scheme — best effort.
  QDesktopServices::openUrl(url);
}

}  // namespace polyglot::tools::ui
