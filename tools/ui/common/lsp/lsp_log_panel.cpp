/**
 * @file     lsp_log_panel.cpp
 *
 * @ingroup  Tool / polyui / LSP
 * @author   Manning Cyrus
 * @date     2026-05-04
 */
#include "tools/ui/common/lsp/lsp_log_panel.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMetaObject>
#include <QSplitter>
#include <QString>
#include <QVBoxLayout>

namespace polyglot::tools::ui::lsp {

LspLogPanel::LspLogPanel(QWidget *parent) : QWidget(parent) {
  auto *root = new QVBoxLayout(this);

  auto *filter_row = new QHBoxLayout();
  filter_row->addWidget(new QLabel(tr("Direction:")));
  direction_filter_ = new QComboBox(this);
  direction_filter_->addItems({tr("All"), tr("Outbound (tx)"), tr("Inbound (rx)")});
  filter_row->addWidget(direction_filter_);

  filter_row->addWidget(new QLabel(tr("Kind:")));
  kind_filter_ = new QComboBox(this);
  kind_filter_->addItems({tr("All"), tr("Request"), tr("Response"),
                          tr("Notification")});
  filter_row->addWidget(kind_filter_);

  filter_row->addWidget(new QLabel(tr("Method:")));
  method_filter_ = new QLineEdit(this);
  method_filter_->setPlaceholderText(tr("substring (case-insensitive)"));
  filter_row->addWidget(method_filter_, 1);

  clear_button_ = new QPushButton(tr("Clear"), this);
  filter_row->addWidget(clear_button_);

  root->addLayout(filter_row);

  auto *splitter = new QSplitter(Qt::Vertical, this);
  list_ = new QListWidget(splitter);
  detail_ = new QPlainTextEdit(splitter);
  detail_->setReadOnly(true);
  detail_->setFont(QFont("Consolas, Menlo, monospace"));
  splitter->addWidget(list_);
  splitter->addWidget(detail_);
  splitter->setStretchFactor(0, 3);
  splitter->setStretchFactor(1, 2);
  root->addWidget(splitter, 1);

  connect(direction_filter_, &QComboBox::currentIndexChanged, this,
          &LspLogPanel::RebuildList);
  connect(kind_filter_, &QComboBox::currentIndexChanged, this,
          &LspLogPanel::RebuildList);
  connect(method_filter_, &QLineEdit::textChanged, this,
          &LspLogPanel::RebuildList);
  connect(clear_button_, &QPushButton::clicked, this, &LspLogPanel::Clear);
  connect(list_, &QListWidget::itemSelectionChanged, this,
          &LspLogPanel::OnSelectionChanged);
}

void LspLogPanel::Append(const LogEntry &entry) {
  // Marshal onto the GUI thread when invoked from the LSP read worker.
  QMetaObject::invokeMethod(
      this,
      [this, entry]() {
        entries_.push_back(entry);
        while (entries_.size() > kMaxEntries) entries_.pop_front();
        if (MatchesFilter(entry)) AddEntryRow(entry);
      },
      Qt::QueuedConnection);
}

void LspLogPanel::Clear() {
  entries_.clear();
  list_->clear();
  detail_->clear();
}

void LspLogPanel::OnSelectionChanged() {
  const int row = list_->currentRow();
  if (row < 0 || row >= static_cast<int>(list_->count())) {
    detail_->clear();
    return;
  }
  // The list and entries_ may be out of sync after filtering; iterate
  // entries_ and pick the n-th one that currently matches.
  int matched = 0;
  for (const auto &e : entries_) {
    if (!MatchesFilter(e)) continue;
    if (matched == row) {
      detail_->setPlainText(QString::fromStdString(e.payload.dump(2)));
      return;
    }
    ++matched;
  }
}

void LspLogPanel::RebuildList() {
  list_->clear();
  for (const auto &e : entries_) {
    if (MatchesFilter(e)) AddEntryRow(e);
  }
}

void LspLogPanel::AddEntryRow(const LogEntry &entry) {
  QString label = QString("[%1] %2 %3")
                      .arg(QString::fromStdString(entry.direction))
                      .arg(QString::fromStdString(entry.kind))
                      .arg(QString::fromStdString(
                          entry.method.empty() ? std::string("<response>") : entry.method));
  list_->addItem(label);
}

bool LspLogPanel::MatchesFilter(const LogEntry &entry) const {
  const int dir = direction_filter_->currentIndex();
  if (dir == 1 && entry.direction != "tx") return false;
  if (dir == 2 && entry.direction != "rx") return false;

  const int kind = kind_filter_->currentIndex();
  if (kind == 1 && entry.kind != "request") return false;
  if (kind == 2 && entry.kind != "response") return false;
  if (kind == 3 && entry.kind != "notification") return false;

  const QString needle = method_filter_->text().trimmed().toLower();
  if (!needle.isEmpty()) {
    if (!QString::fromStdString(entry.method).toLower().contains(needle)) return false;
  }
  return true;
}

std::string LspLogPanel::ClassifyEntry(const Json &payload) {
  if (payload.contains("method") && payload.contains("id")) return "request";
  if (payload.contains("method")) return "notification";
  return "response";
}

}  // namespace polyglot::tools::ui::lsp
