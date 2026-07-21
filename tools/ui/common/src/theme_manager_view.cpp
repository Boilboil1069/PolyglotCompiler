/**
 * @file     theme_manager_view.cpp
 * @brief    Implementation of the Theme Manager dialog.
 */

#include "tools/ui/common/include/theme_manager_view.h"

#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTextEdit>
#include <QTextOption>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "tools/ui/common/include/theme_manager.h"
#include "tools/ui/common/include/theme_service.h"

namespace polyglot::tools::ui {

namespace {

QString LayerHumanLabel(const QString &layer) {
  if (layer == "builtin")   return QObject::tr("Built-in");
  if (layer == "user")      return QObject::tr("User");
  if (layer == "workspace") return QObject::tr("Workspace");
  return layer;
}

}  // namespace

ThemeManagerView::ThemeManagerView(QWidget *parent) : QDialog(parent) {
  setWindowTitle(tr("Theme Manager"));
  resize(1100, 720);
  BuildUi();

  connect(&ThemeService::Instance(), &ThemeService::themesScanned,
          this, &ThemeManagerView::OnThemesScanned);
  connect(&ThemeService::Instance(), &ThemeService::themeChanged,
          this, [this](const QString &) { RefreshPreview(); });

  ThemeService::Instance().Scan();
  ReloadList();
  RefreshPreview();
}

void ThemeManagerView::BuildUi() {
  auto *root = new QVBoxLayout(this);

  // Top toolbar -------------------------------------------------------------
  auto *toolbar = new QToolBar(this);
  btn_activate_  = new QPushButton(tr("Activate"), toolbar);
  btn_install_   = new QPushButton(tr("Install From File…"), toolbar);
  btn_uninstall_ = new QPushButton(tr("Uninstall"), toolbar);
  btn_export_    = new QPushButton(tr("Export…"), toolbar);
  btn_duplicate_ = new QPushButton(tr("Duplicate…"), toolbar);
  toolbar->addWidget(btn_activate_);
  toolbar->addSeparator();
  toolbar->addWidget(btn_duplicate_);
  toolbar->addWidget(btn_export_);
  toolbar->addSeparator();
  toolbar->addWidget(btn_install_);
  toolbar->addWidget(btn_uninstall_);
  root->addWidget(toolbar);

  connect(btn_activate_,  &QPushButton::clicked, this, &ThemeManagerView::OnActivate);
  connect(btn_install_,   &QPushButton::clicked, this, &ThemeManagerView::OnInstallFromFile);
  connect(btn_uninstall_, &QPushButton::clicked, this, &ThemeManagerView::OnUninstall);
  connect(btn_export_,    &QPushButton::clicked, this, &ThemeManagerView::OnExport);
  connect(btn_duplicate_, &QPushButton::clicked, this, &ThemeManagerView::OnDuplicate);

  // 3-pane splitter ---------------------------------------------------------
  auto *splitter = new QSplitter(Qt::Horizontal, this);
  root->addWidget(splitter, 1);

  // Left pane: search + filter + list
  auto *left = new QWidget(splitter);
  auto *left_layout = new QVBoxLayout(left);
  left_layout->setContentsMargins(4, 4, 4, 4);

  auto *filters = new QHBoxLayout();
  search_ = new QLineEdit(left);
  search_->setPlaceholderText(tr("Search themes…"));
  type_filter_ = new QComboBox(left);
  type_filter_->addItems({tr("All"), tr("Dark"), tr("Light"), tr("High Contrast")});
  filters->addWidget(search_, 1);
  filters->addWidget(type_filter_);
  left_layout->addLayout(filters);

  theme_list_ = new QListWidget(left);
  left_layout->addWidget(theme_list_, 1);

  connect(search_,      &QLineEdit::textChanged,           this, &ThemeManagerView::OnFilterChanged);
  connect(type_filter_, qOverload<int>(&QComboBox::currentIndexChanged),
                                                            this, &ThemeManagerView::OnFilterChanged);
  connect(theme_list_,  &QListWidget::itemSelectionChanged, this, &ThemeManagerView::OnSelectionChanged);

  splitter->addWidget(left);

  // Centre pane: live preview
  preview_ = new QTextEdit(splitter);
  preview_->setReadOnly(true);
  preview_->setLineWrapMode(QTextEdit::NoWrap);
  splitter->addWidget(preview_);

  // Right pane: metadata + token tree
  auto *right = new QWidget(splitter);
  auto *right_layout = new QVBoxLayout(right);
  right_layout->setContentsMargins(4, 4, 4, 4);

  meta_name_    = new QLabel(right);   meta_name_->setStyleSheet("font-weight:600;font-size:14px;");
  meta_id_      = new QLabel(right);   meta_id_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  meta_type_    = new QLabel(right);
  meta_version_ = new QLabel(right);
  meta_author_  = new QLabel(right);
  meta_extends_ = new QLabel(right);
  meta_layer_   = new QLabel(right);
  for (auto *l : {meta_name_, meta_id_, meta_type_, meta_version_,
                  meta_author_, meta_extends_, meta_layer_}) {
    right_layout->addWidget(l);
  }

  tokens_ = new QTreeWidget(right);
  tokens_->setHeaderLabels({tr("Color Key"), tr("Value")});
  tokens_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  right_layout->addWidget(tokens_, 1);

  splitter->addWidget(right);
  splitter->setStretchFactor(0, 2);
  splitter->setStretchFactor(1, 4);
  splitter->setStretchFactor(2, 3);
}

void ThemeManagerView::OnThemesScanned() { ReloadList(); }

void ThemeManagerView::OnFilterChanged() { ReloadList(); }

void ThemeManagerView::ReloadList() {
  theme_list_->clear();
  const QString needle = search_->text().trimmed().toLower();
  const QString type_choice = type_filter_->currentText();
  for (const auto &m : ThemeService::Instance().Themes()) {
    if (!needle.isEmpty() && !m.name.toLower().contains(needle) &&
        !m.id.toLower().contains(needle)) {
      continue;
    }
    if (type_choice == tr("Dark")          && m.type != "dark")          continue;
    if (type_choice == tr("Light")         && m.type != "light")         continue;
    if (type_choice == tr("High Contrast") && m.type != "high-contrast") continue;
    auto *item = new QListWidgetItem(
        QString("[%1] %2  —  %3").arg(LayerHumanLabel(m.layer), m.name, m.id),
        theme_list_);
    item->setData(Qt::UserRole, m.id);
    if (m.id == ThemeManager::Instance().ActiveName() ||
        (ThemeService::Instance().CurrentTheme() &&
         ThemeService::Instance().CurrentTheme()->id == m.id)) {
      QFont f = item->font();
      f.setBold(true);
      item->setFont(f);
    }
  }
  if (theme_list_->count() > 0 && !theme_list_->currentItem()) {
    theme_list_->setCurrentRow(0);
  }
}

void ThemeManagerView::OnSelectionChanged() {
  RefreshPreview();
  auto *item = theme_list_->currentItem();
  if (!item) return;
  const QString id = item->data(Qt::UserRole).toString();
  const ThemeMeta *m = ThemeService::Instance().FindById(id);
  if (!m) return;
  meta_name_->setText(m->name);
  meta_id_->setText(tr("ID: %1").arg(m->id));
  meta_type_->setText(tr("Type: %1").arg(m->type));
  meta_version_->setText(tr("Version: %1").arg(m->version));
  meta_author_->setText(tr("Author: %1").arg(m->author));
  meta_extends_->setText(tr("Extends: %1").arg(m->extends.isEmpty() ? "—" : m->extends));
  meta_layer_->setText(tr("Source: %1 (%2)").arg(LayerHumanLabel(m->layer), m->source_path));
  btn_uninstall_->setEnabled(m->layer != "builtin");

  // Populate the tokens tree from the active ThemeManager state — we
  // re-register the theme on every selection to expose the merged colors.
  tokens_->clear();
  const ThemeColors &c = ThemeManager::Instance().GetTheme(m->name);
  auto add = [&](const QString &group, const QString &key, const QColor &v) {
    auto *parent = tokens_->topLevelItemCount() == 0 ? nullptr : tokens_->topLevelItem(0);
    auto *grp = nullptr ? parent : nullptr;
    Q_UNUSED(grp);
    auto *root = new QTreeWidgetItem(tokens_);
    root->setText(0, group + "." + key);
    root->setText(1, v.name(QColor::HexRgb));
    root->setBackground(1, v);
  };
  add("workbench", "background",   c.background);
  add("workbench", "surface",      c.surface);
  add("workbench", "border",       c.border);
  add("editor",    "background",   c.editor_background);
  add("editor",    "foreground",   c.editor_text);
  add("editor",    "selection",    c.editor_selection);
  add("editor",    "lineNumber",   c.editor_line_number);
  add("statusBar", "background",   c.statusbar_background);
  add("statusBar", "foreground",   c.statusbar_text);
  add("button",    "background",   c.button_primary);
  add("button",    "foreground",   c.button_primary_text);
  add("input",     "background",   c.input_background);
  add("scrollbar", "thumb",        c.scrollbar_thumb);
}

void ThemeManagerView::RefreshPreview() {
  // Tiny mini-IDE preview rendered as HTML so we don't need to spin up real
  // editors / panels (they are live-themed by ApplyPalette / setStyleSheet
  // already, so this preview only shows the *selected* theme regardless of
  // what's active globally).
  auto *item = theme_list_->currentItem();
  if (!item) { preview_->clear(); return; }
  const QString id = item->data(Qt::UserRole).toString();
  const ThemeMeta *m = ThemeService::Instance().FindById(id);
  if (!m) { preview_->clear(); return; }
  const ThemeColors &c = ThemeManager::Instance().GetTheme(m->name);

  const QString html = QStringLiteral(R"(
    <div style="background:%1;color:%2;font-family:Consolas,'Courier New',monospace;font-size:13px;">
      <div style="background:%3;color:%4;padding:4px 8px;font-weight:600;">menu | %5</div>
      <div style="background:%6;padding:8px;">
        <pre style="margin:0;color:%7;background:%8;padding:6px;border-left:3px solid %9;">
<span style="color:%10;">// preview snippet for %11</span>
<span style="color:%12;">FUNC</span> sum(a: <span style="color:%13;">INT</span>, b: <span style="color:%13;">INT</span>) -&gt; <span style="color:%13;">INT</span> {
    <span style="color:%12;">RETURN</span> a + b;
}
        </pre>
      </div>
      <div style="background:%14;color:%15;padding:4px 8px;font-size:11px;">
        status: ready | theme: %5 | tokens: %16
      </div>
    </div>
  )")
    .arg(c.background.name(),                         // 1
         c.text.name(),                               // 2
         c.menu_background.name(),                    // 3
         c.menu_text.name(),                          // 4
         m->name,                                     // 5
         c.surface.name(),                            // 6
         c.editor_text.name(),                        // 7
         c.editor_background.name(),                  // 8
         c.tab_active_indicator.name(),               // 9
         c.text_secondary.name(),                     // 10
         m->id,                                       // 11
         c.accent.name(),                             // 12
         c.success.name(),                            // 13
         c.statusbar_background.name(),               // 14
         c.statusbar_text.name(),                     // 15
         m->type);                                    // 16
  preview_->setHtml(html);
}

void ThemeManagerView::OnActivate() {
  auto *item = theme_list_->currentItem();
  if (!item) return;
  const QString id = item->data(Qt::UserRole).toString();
  if (!ThemeService::Instance().Activate(id)) {
    QMessageBox::warning(this, tr("Theme Manager"),
                         tr("Failed to activate theme '%1'.").arg(id));
  }
  ReloadList();
}

void ThemeManagerView::OnInstallFromFile() {
  const QString path = QFileDialog::getOpenFileName(
      this, tr("Install Theme"), {}, tr("Polytheme files (*.polytheme.json)"));
  if (path.isEmpty()) return;
  QString err;
  const QString dest = ThemeService::Instance().InstallFromFile(path, &err);
  if (dest.isEmpty()) {
    QMessageBox::warning(this, tr("Theme Manager"),
                         tr("Install failed: %1").arg(err));
  } else {
    QMessageBox::information(this, tr("Theme Manager"),
                             tr("Theme installed to:\n%1").arg(dest));
  }
}

void ThemeManagerView::OnUninstall() {
  auto *item = theme_list_->currentItem();
  if (!item) return;
  const QString id = item->data(Qt::UserRole).toString();
  if (QMessageBox::question(this, tr("Uninstall Theme"),
                            tr("Remove theme '%1' from disk?").arg(id))
      != QMessageBox::Yes) {
    return;
  }
  QString err;
  if (!ThemeService::Instance().Uninstall(id, &err)) {
    QMessageBox::warning(this, tr("Theme Manager"),
                         tr("Uninstall failed: %1").arg(err));
  }
}

void ThemeManagerView::OnExport() {
  auto *item = theme_list_->currentItem();
  if (!item) return;
  const QString id = item->data(Qt::UserRole).toString();
  const QString path = QFileDialog::getSaveFileName(
      this, tr("Export Theme"),
      QString("%1.polytheme.json").arg(id),
      tr("Polytheme files (*.polytheme.json)"));
  if (path.isEmpty()) return;
  QString err;
  if (!ThemeService::Instance().ExportToFile(id, path, &err)) {
    QMessageBox::warning(this, tr("Theme Manager"),
                         tr("Export failed: %1").arg(err));
  } else {
    QMessageBox::information(this, tr("Theme Manager"),
                             tr("Exported to:\n%1").arg(path));
  }
}

void ThemeManagerView::OnDuplicate() {
  auto *item = theme_list_->currentItem();
  if (!item) return;
  const QString id = item->data(Qt::UserRole).toString();
  bool ok = false;
  const QString new_id = QInputDialog::getText(
      this, tr("Duplicate Theme"), tr("New theme id:"), QLineEdit::Normal,
      id + ".copy", &ok);
  if (!ok || new_id.trimmed().isEmpty()) return;

  // Materialise the parent into a stand-alone file under user themes, then
  // patch the id and add an `extends` link so user edits stay minimal.
  const QString user_dir = ThemeService::Instance().UserThemesDir();
  const QString out = user_dir + "/" + new_id + ".polytheme.json";
  QString err;
  if (!ThemeService::Instance().ExportToFile(id, out, &err)) {
    QMessageBox::warning(this, tr("Theme Manager"),
                         tr("Duplicate failed: %1").arg(err));
    return;
  }
  // Rewrite id and inject extends.
  QFile f(out);
  if (f.open(QIODevice::ReadOnly)) {
    auto doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (doc.isObject()) {
      QJsonObject root = doc.object();
      root["id"]      = new_id;
      root["name"]    = root.value("name").toString() + " (Copy)";
      root["extends"] = id;
      QFile w(out);
      if (w.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        w.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
      }
    }
  }
  ThemeService::Instance().Scan();
  QMessageBox::information(this, tr("Theme Manager"),
                           tr("Duplicated to:\n%1").arg(out));
}

}  // namespace polyglot::tools::ui
