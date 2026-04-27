/**
 * @file     command_palette.cpp
 * @brief    Command palette implementation
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-04-27
 */
#include "tools/ui/common/include/command_palette.h"

#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QShowEvent>
#include <QVBoxLayout>

#include "tools/ui/common/include/keybinding_service.h"

namespace polyglot::tools::ui {

CommandPalette::CommandPalette(QWidget *parent) : QDialog(parent) {
  setWindowFlag(Qt::FramelessWindowHint, false);
  setWindowTitle(tr("Command Palette"));
  resize(640, 400);

  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(8, 8, 8, 8);
  root->setSpacing(6);

  filter_ = new QLineEdit(this);
  filter_->setPlaceholderText(tr("Type a command (e.g. \"settings\", \"open\", \"theme\")"));
  filter_->installEventFilter(this);

  list_ = new QListWidget(this);
  list_->setUniformItemSizes(true);

  root->addWidget(filter_);
  root->addWidget(list_, 1);

  connect(filter_, &QLineEdit::textChanged, this, &CommandPalette::OnFilterChanged);
  connect(filter_, &QLineEdit::returnPressed, this, &CommandPalette::OnAccept);
  connect(list_, &QListWidget::itemActivated, this, [this](QListWidgetItem *) { OnAccept(); });
}

CommandPalette::~CommandPalette() = default;

void CommandPalette::Refresh() {
  commands_ = KeybindingService::Instance().AllCommands();
  std::sort(commands_.begin(), commands_.end());
  OnFilterChanged(filter_->text());
}

void CommandPalette::showEvent(QShowEvent *e) {
  Refresh();
  filter_->clear();
  filter_->setFocus();
  QDialog::showEvent(e);
}

bool CommandPalette::eventFilter(QObject *obj, QEvent *e) {
  if (obj == filter_ && e->type() == QEvent::KeyPress) {
    auto *ke = static_cast<QKeyEvent *>(e);
    if (ke->key() == Qt::Key_Down) {
      const int n = list_->count();
      if (n > 0) {
        const int row = std::min(n - 1, list_->currentRow() + 1);
        list_->setCurrentRow(row);
      }
      return true;
    }
    if (ke->key() == Qt::Key_Up) {
      const int row = std::max(0, list_->currentRow() - 1);
      list_->setCurrentRow(row);
      return true;
    }
    if (ke->key() == Qt::Key_Escape) {
      reject();
      return true;
    }
  }
  return QDialog::eventFilter(obj, e);
}

static bool FuzzyMatch(const QString &needle, const QString &haystack) {
  if (needle.isEmpty()) return true;
  int i = 0;
  for (QChar c : haystack) {
    if (c.toLower() == needle[i].toLower()) {
      if (++i == needle.size()) return true;
    }
  }
  return false;
}

void CommandPalette::OnFilterChanged(const QString &text) {
  list_->clear();
  auto &kb = KeybindingService::Instance();
  for (const QString &cmd : commands_) {
    const QString title = kb.CommandTitle(cmd);
    if (FuzzyMatch(text, title) || FuzzyMatch(text, cmd)) {
      const QString key = kb.KeyForCommand(cmd);
      const QString label = key.isEmpty() ? title : (title + "    [" + key + "]");
      auto *item = new QListWidgetItem(label, list_);
      item->setData(Qt::UserRole, cmd);
    }
  }
  if (list_->count() > 0) list_->setCurrentRow(0);
}

void CommandPalette::OnAccept() {
  auto *item = list_->currentItem();
  if (!item) return;
  const QString cmd = item->data(Qt::UserRole).toString();
  accept();
  KeybindingService::Instance().Run(cmd);
}

}  // namespace polyglot::tools::ui
