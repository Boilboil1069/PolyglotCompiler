/**
 * @file     code_assist.cpp
 * @brief    Implementation of SnippetExpander / HoverTooltip /
 *           SignatureHelpWidget.
 *
 * @ingroup  Tool / polyui / Completion
 * @author   Manning Cyrus
 * @date     2026-05-04
 */
#include "tools/ui/common/include/code_assist.h"

#include <QApplication>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QScreen>
#include <QTextCursor>
#include <QTextDocument>
#include <QVBoxLayout>

#include <algorithm>

namespace polyglot::tools::ui {

// ---------------------------------------------------------------------------
// SnippetExpander
// ---------------------------------------------------------------------------

SnippetExpander::SnippetExpander(QPlainTextEdit *editor, QObject *parent)
    : QObject(parent), editor_(editor) {}

QString SnippetExpander::Parse(const QString &snippet,
                               QVector<SnippetStop> &stops) {
  QString out;
  out.reserve(snippet.size());
  stops.clear();

  for (int i = 0; i < snippet.size();) {
    const QChar c = snippet[i];
    if (c == QLatin1Char('\\') && i + 1 < snippet.size() &&
        snippet[i + 1] == QLatin1Char('$')) {
      out.append(QLatin1Char('$'));
      i += 2;
      continue;
    }
    if (c != QLatin1Char('$')) {
      out.append(c);
      ++i;
      continue;
    }
    // Either `$N` or `${N}` / `${N:default}`.
    if (i + 1 < snippet.size() && snippet[i + 1] == QLatin1Char('{')) {
      // Find matching `}`.
      int j = i + 2;
      int depth = 1;
      while (j < snippet.size() && depth > 0) {
        if (snippet[j] == QLatin1Char('{')) ++depth;
        else if (snippet[j] == QLatin1Char('}')) --depth;
        if (depth > 0) ++j;
      }
      if (j >= snippet.size()) {
        // Malformed — emit verbatim.
        out.append(c);
        ++i;
        continue;
      }
      const QString inner = snippet.mid(i + 2, j - (i + 2));
      const int colon = inner.indexOf(QLatin1Char(':'));
      const QString idx_str = colon >= 0 ? inner.left(colon) : inner;
      const QString def_text = colon >= 0 ? inner.mid(colon + 1) : QString();
      bool ok = false;
      const int idx = idx_str.toInt(&ok);
      if (!ok) {
        out.append(c);
        ++i;
        continue;
      }
      SnippetStop stop;
      stop.tab_index = idx;
      stop.absolute_start = out.size();
      out.append(def_text);
      stop.absolute_end = out.size();
      stops.push_back(stop);
      i = j + 1;
      continue;
    }
    if (i + 1 < snippet.size() && snippet[i + 1].isDigit()) {
      int j = i + 1;
      while (j < snippet.size() && snippet[j].isDigit()) ++j;
      const int idx = snippet.mid(i + 1, j - i - 1).toInt();
      SnippetStop stop;
      stop.tab_index = idx;
      stop.absolute_start = out.size();
      stop.absolute_end = out.size();
      stops.push_back(stop);
      i = j;
      continue;
    }
    out.append(c);
    ++i;
  }

  // Sort stops by tab_index, dropping the synthetic 0 (final cursor)
  // if any: we want it to come last.
  std::stable_sort(stops.begin(), stops.end(),
                   [](const SnippetStop &a, const SnippetStop &b) {
                     auto rank = [](int idx) {
                       return idx == 0 ? std::numeric_limits<int>::max() : idx;
                     };
                     return rank(a.tab_index) < rank(b.tab_index);
                   });
  return out;
}

bool SnippetExpander::ExpandAtCursor(const QString &snippet, bool replace_word) {
  if (!editor_) return false;
  QTextCursor cursor = editor_->textCursor();
  if (replace_word) {
    cursor.movePosition(QTextCursor::StartOfWord, QTextCursor::KeepAnchor);
  }

  QVector<SnippetStop> rel_stops;
  const QString plain = Parse(snippet, rel_stops);
  const int insertion_pos = cursor.selectionStart();
  cursor.insertText(plain);

  if (rel_stops.isEmpty()) {
    Cancel();
    return false;
  }

  // Promote relative offsets to absolute document positions.
  stops_.clear();
  stops_.reserve(rel_stops.size());
  for (const auto &s : rel_stops) {
    SnippetStop abs;
    abs.tab_index = s.tab_index;
    abs.absolute_start = insertion_pos + s.absolute_start;
    abs.absolute_end = insertion_pos + s.absolute_end;
    stops_.push_back(abs);
  }
  active_index_ = 0;

  // Move to the first stop and select its text.
  QTextCursor c = editor_->textCursor();
  c.setPosition(stops_.first().absolute_start);
  c.setPosition(stops_.first().absolute_end, QTextCursor::KeepAnchor);
  editor_->setTextCursor(c);
  return true;
}

bool SnippetExpander::AdvanceToNextStop() {
  if (active_index_ + 1 >= stops_.size()) {
    Cancel();
    return false;
  }
  ++active_index_;
  const SnippetStop &s = stops_[active_index_];
  QTextCursor c = editor_->textCursor();
  c.setPosition(s.absolute_start);
  c.setPosition(s.absolute_end, QTextCursor::KeepAnchor);
  editor_->setTextCursor(c);
  return true;
}

void SnippetExpander::Cancel() {
  stops_.clear();
  active_index_ = 0;
}

// ---------------------------------------------------------------------------
// HoverTooltip
// ---------------------------------------------------------------------------

HoverTooltip::HoverTooltip(QWidget *parent) : QLabel(parent) {
  setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint);
  setTextFormat(Qt::MarkdownText);
  setWordWrap(true);
  setMargin(8);
  setMaximumWidth(640);
  setStyleSheet(QStringLiteral(
      "QLabel { background: #2d2d30; color: #cccccc; "
      "border: 1px solid #454545; font-family: 'Segoe UI', sans-serif; "
      "font-size: 10pt; }"));
  hide();
}

void HoverTooltip::ShowMarkdown(const QString &markdown,
                                const QPoint &global_pos) {
  if (markdown.isEmpty()) {
    HideTooltip();
    return;
  }
  setText(markdown);
  adjustSize();
  // Clamp inside the available screen geometry.
  QPoint pos = global_pos;
  if (QScreen *screen = QGuiApplication::screenAt(global_pos)) {
    const QRect avail = screen->availableGeometry();
    if (pos.x() + width() > avail.right()) pos.setX(avail.right() - width());
    if (pos.y() + height() > avail.bottom())
      pos.setY(global_pos.y() - height() - 16);
  }
  move(pos);
  show();
  raise();
}

void HoverTooltip::HideTooltip() { hide(); }

// ---------------------------------------------------------------------------
// SignatureHelpWidget
// ---------------------------------------------------------------------------

SignatureHelpWidget::SignatureHelpWidget(QWidget *parent) : QWidget(parent) {
  setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint);
  setAttribute(Qt::WA_ShowWithoutActivating);
  body_ = new QLabel(this);
  body_->setTextFormat(Qt::RichText);
  body_->setMargin(6);
  body_->setStyleSheet(QStringLiteral(
      "QLabel { background: #252526; color: #d4d4d4; "
      "border: 1px solid #454545; font-family: 'Consolas', monospace; "
      "font-size: 10pt; }"));
  auto *layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(body_);
  hide();
}

void SignatureHelpWidget::ShowSignatures(
    const QVector<SignatureRecord> &signatures, int active_signature,
    int active_parameter, const QPoint &global_pos) {
  if (signatures.isEmpty()) {
    HideHelp();
    return;
  }
  signatures_ = signatures;
  active_signature_ =
      std::clamp(active_signature, 0, signatures_.size() - 1);
  const int param_count =
      signatures_[active_signature_].parameter_labels.size();
  active_parameter_ =
      param_count == 0 ? 0 : std::clamp(active_parameter, 0, param_count - 1);
  Render();
  adjustSize();
  QPoint pos = global_pos;
  pos.setY(pos.y() - height() - 4);
  move(pos);
  show();
  raise();
}

void SignatureHelpWidget::HideHelp() {
  signatures_.clear();
  hide();
}

void SignatureHelpWidget::NextOverload() {
  if (signatures_.isEmpty()) return;
  active_signature_ = (active_signature_ + 1) % signatures_.size();
  Render();
}

void SignatureHelpWidget::PrevOverload() {
  if (signatures_.isEmpty()) return;
  active_signature_ =
      (active_signature_ - 1 + signatures_.size()) % signatures_.size();
  Render();
}

void SignatureHelpWidget::Render() {
  QString html;
  for (int i = 0; i < signatures_.size(); ++i) {
    const SignatureRecord &sig = signatures_[i];
    const bool is_active = (i == active_signature_);
    QString row = sig.label.toHtmlEscaped();
    // Underline the active parameter substring (best effort).
    if (is_active && active_parameter_ >= 0 &&
        active_parameter_ < sig.parameter_labels.size()) {
      const QString p = sig.parameter_labels[active_parameter_].toHtmlEscaped();
      const int idx = row.indexOf(p);
      if (idx >= 0) {
        row.insert(idx + p.size(), QStringLiteral("</u></b>"));
        row.insert(idx, QStringLiteral("<b><u>"));
      }
    }
    html += QStringLiteral("<div style='%1'>%2</div>")
                .arg(is_active ? QStringLiteral("color:#ffffff;")
                               : QStringLiteral("color:#888888;"),
                     row);
  }
  if (signatures_.size() > 1) {
    html += QStringLiteral(
                "<div style='color:#888888;font-size:8pt;'>"
                "&#8593;/&#8595; switch overload (%1/%2)</div>")
                .arg(active_signature_ + 1)
                .arg(signatures_.size());
  }
  body_->setText(html);
}

}  // namespace polyglot::tools::ui
