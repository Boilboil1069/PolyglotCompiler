/**
 * @file     settings_page.h
 * @brief    VS Code-style two-pane settings editor
 *
 * Replaces the legacy SettingsDialog as the primary preferences UI.  Reads
 * the bundled JSON Schema and renders one section per namespace, with
 * per-field "Source" badges (default / user / workspace) and an "Open JSON"
 * button that pops the underlying settings.json in a CodeEditor tab.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-04-27
 */
#pragma once

#include <QDialog>
#include <QHash>
#include <QJsonObject>
#include <QString>

class QLineEdit;
class QListWidget;
class QStackedWidget;
class QWidget;

namespace polyglot::tools::ui {

class SettingsPage : public QDialog {
  Q_OBJECT

 public:
  explicit SettingsPage(QWidget *parent = nullptr);
  ~SettingsPage() override;

 signals:
  /// Asks MainWindow to open a JSON file in a code editor tab.
  void RequestOpenJson(const QString &path);

 private slots:
  void OnNamespaceChanged(int row);
  void OnFilterChanged(const QString &text);
  void OnOpenUserJson();
  void OnOpenWorkspaceJson();
  void OnOpenDefaultsJson();
  void OnResetAll();

 private:
  void BuildUi();
  void BuildNamespaces();
  QWidget *BuildNamespacePage(const QString &ns,
                              const QJsonObject &fields_in_namespace);

  QLineEdit *filter_{nullptr};
  QListWidget *ns_list_{nullptr};
  QStackedWidget *pages_{nullptr};

  QJsonObject schema_;        // full settings_schema.json
  QHash<QString, QWidget *> ns_pages_;
};

}  // namespace polyglot::tools::ui
