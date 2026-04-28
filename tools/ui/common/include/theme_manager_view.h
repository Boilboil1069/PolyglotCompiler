/**
 * @file     theme_manager_view.h
 * @brief    Three-pane Theme Manager dialog (list / preview / metadata).
 *
 * Mirrors the layout of VS Code's "Color Theme" picker but expanded into a
 * full dialog so users can browse, activate, install, export and uninstall
 * themes without leaving polyui.  All actual theme operations are delegated
 * to ThemeService — this file is purely UI glue.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-04-27
 */
#pragma once

#include <QDialog>
#include <QString>

class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSplitter;
class QTextEdit;
class QTreeWidget;
class QLabel;
class QLineEdit;
class QComboBox;

namespace polyglot::tools::ui {

class ThemeManagerView : public QDialog {
  Q_OBJECT

 public:
  explicit ThemeManagerView(QWidget *parent = nullptr);
  ~ThemeManagerView() override = default;

 private slots:
  void OnThemesScanned();
  void OnSelectionChanged();
  void OnActivate();
  void OnInstallFromFile();
  void OnUninstall();
  void OnExport();
  void OnDuplicate();
  void OnFilterChanged();

 private:
  void BuildUi();
  void ReloadList();
  void RefreshPreview();

  // Left
  QLineEdit  *search_{nullptr};
  QComboBox  *type_filter_{nullptr};
  QListWidget *theme_list_{nullptr};

  // Centre
  QTextEdit  *preview_{nullptr};

  // Right
  QLabel     *meta_name_{nullptr};
  QLabel     *meta_id_{nullptr};
  QLabel     *meta_type_{nullptr};
  QLabel     *meta_version_{nullptr};
  QLabel     *meta_author_{nullptr};
  QLabel     *meta_extends_{nullptr};
  QLabel     *meta_layer_{nullptr};
  QTreeWidget *tokens_{nullptr};

  // Toolbar
  QPushButton *btn_activate_{nullptr};
  QPushButton *btn_install_{nullptr};
  QPushButton *btn_uninstall_{nullptr};
  QPushButton *btn_export_{nullptr};
  QPushButton *btn_duplicate_{nullptr};
};

}  // namespace polyglot::tools::ui
