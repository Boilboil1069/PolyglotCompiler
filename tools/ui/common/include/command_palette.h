/**
 * @file     command_palette.h
 * @brief    VS Code-style "Quick Open" command palette (Ctrl+Shift+P)
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-04-27
 */
#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>

class QLineEdit;
class QListWidget;

namespace polyglot::tools::ui {

class CommandPalette : public QDialog {
  Q_OBJECT
 public:
  explicit CommandPalette(QWidget *parent = nullptr);
  ~CommandPalette() override;

  /// Reload the candidate list from KeybindingService::AllCommands().
  void Refresh();

 protected:
  void showEvent(QShowEvent *e) override;
  bool eventFilter(QObject *obj, QEvent *e) override;

 private slots:
  void OnFilterChanged(const QString &text);
  void OnAccept();

 private:
  QLineEdit *filter_{nullptr};
  QListWidget *list_{nullptr};
  QStringList commands_;  // all known command ids
};

}  // namespace polyglot::tools::ui
