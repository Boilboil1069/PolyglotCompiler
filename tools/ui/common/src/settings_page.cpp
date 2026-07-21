/**
 * @file     settings_page.cpp
 * @brief    SettingsPage implementation (VS Code-style two-pane editor)
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-04-27
 */
#include "tools/ui/common/include/settings_page.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include "tools/ui/common/include/settings_service.h"

namespace polyglot::tools::ui {

SettingsPage::SettingsPage(QWidget *parent) : QDialog(parent) {
  setWindowTitle(tr("Settings"));
  resize(960, 640);
  setMinimumSize(720, 480);

  // Load the bundled schema.
  QFile f(":/polyglot/settings/settings_schema.json");
  if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    const auto doc = QJsonDocument::fromJson(f.readAll());
    if (doc.isObject()) schema_ = doc.object();
  }

  BuildUi();
  BuildNamespaces();
}

SettingsPage::~SettingsPage() = default;

// ---------------------------------------------------------------------------
// UI scaffold
// ---------------------------------------------------------------------------

void SettingsPage::BuildUi() {
  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  // Top toolbar.
  auto *toolbar = new QHBoxLayout();
  toolbar->setContentsMargins(8, 6, 8, 6);
  filter_ = new QLineEdit(this);
  filter_->setPlaceholderText(tr("Search settings"));
  toolbar->addWidget(filter_, 1);

  auto *btn_user = new QPushButton(tr("Open User settings.json"), this);
  auto *btn_ws = new QPushButton(tr("Open Workspace settings.json"), this);
  auto *btn_def = new QPushButton(tr("Open Default Settings (JSON, read-only)"), this);
  auto *btn_reset = new QPushButton(tr("Reset All"), this);
  toolbar->addWidget(btn_user);
  toolbar->addWidget(btn_ws);
  toolbar->addWidget(btn_def);
  toolbar->addWidget(btn_reset);
  root->addLayout(toolbar);

  // Body: left list + right pages.
  auto *body = new QHBoxLayout();
  body->setContentsMargins(0, 0, 0, 0);
  body->setSpacing(0);
  ns_list_ = new QListWidget(this);
  ns_list_->setFixedWidth(220);
  body->addWidget(ns_list_);
  pages_ = new QStackedWidget(this);
  body->addWidget(pages_, 1);
  root->addLayout(body, 1);

  connect(filter_, &QLineEdit::textChanged, this, &SettingsPage::OnFilterChanged);
  connect(ns_list_, &QListWidget::currentRowChanged, this, &SettingsPage::OnNamespaceChanged);
  connect(btn_user, &QPushButton::clicked, this, &SettingsPage::OnOpenUserJson);
  connect(btn_ws, &QPushButton::clicked, this, &SettingsPage::OnOpenWorkspaceJson);
  connect(btn_def, &QPushButton::clicked, this, &SettingsPage::OnOpenDefaultsJson);
  connect(btn_reset, &QPushButton::clicked, this, &SettingsPage::OnResetAll);
}

void SettingsPage::BuildNamespaces() {
  // Group fields by their first segment.
  const auto props = schema_.value("properties").toObject();
  QHash<QString, QJsonObject> grouped;
  for (auto it = props.begin(); it != props.end(); ++it) {
    const QString key = it.key();
    const int dot = key.indexOf('.');
    const QString ns = (dot < 0) ? key : key.left(dot);
    auto bucket = grouped.value(ns);
    bucket.insert(key, it.value());
    grouped.insert(ns, bucket);
  }
  QStringList ns_names = grouped.keys();
  std::sort(ns_names.begin(), ns_names.end());
  for (const QString &ns : ns_names) {
    QString display = ns;
    if (!display.isEmpty()) display[0] = display[0].toUpper();
    ns_list_->addItem(display);
    QWidget *page = BuildNamespacePage(ns, grouped.value(ns));
    pages_->addWidget(page);
    ns_pages_.insert(ns, page);
  }
  if (ns_list_->count() > 0) ns_list_->setCurrentRow(0);
}

// ---------------------------------------------------------------------------
// Per-namespace page
// ---------------------------------------------------------------------------

namespace {

QString SourceLabel(const QString &key) {
  // Determine which layer last set this key, by inspecting SettingsService internals.
  // Cheap heuristic: compare effective vs default to see if user/workspace overrode.
  auto &svc = SettingsService::Instance();
  const QString user_path = svc.UserSettingsPath();
  const QString ws_path = svc.WorkspaceSettingsPath();
  // Re-read the user/workspace files on the fly (small files; this page is rare).
  auto containsKey = [&](const QString &p) {
    if (p.isEmpty()) return false;
    QFile f(p);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    const auto doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return false;
    return doc.object().contains(key);
  };
  if (containsKey(ws_path)) return QObject::tr("workspace");
  if (containsKey(user_path)) return QObject::tr("user");
  return QObject::tr("default");
}

}  // namespace

QWidget *SettingsPage::BuildNamespacePage(const QString &ns,
                                          const QJsonObject &fields_in_namespace) {
  auto *scroll = new QScrollArea();
  scroll->setWidgetResizable(true);
  auto *body = new QWidget();
  auto *form = new QFormLayout(body);
  form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
  form->setContentsMargins(16, 16, 16, 16);

  auto *title = new QLabel("<h2>" + ns + "</h2>", body);
  form->addRow(title);

  auto &svc = SettingsService::Instance();

  for (auto it = fields_in_namespace.begin(); it != fields_in_namespace.end(); ++it) {
    const QString key = it.key();
    const QJsonObject spec = it.value().toObject();
    const QString type = spec.value("type").toString();
    const QString descEn = spec.value("description").toString();
    const QString descZh = spec.value("descriptionZh").toString();
    const QString tip = descZh.isEmpty() ? descEn : (descZh + "  /  " + descEn);

    auto *label = new QLabel(key + "   <i style='color:#888'>(" + SourceLabel(key) + ")</i>", body);
    label->setToolTip(tip);

    QWidget *editor = nullptr;
    if (type == "boolean") {
      auto *cb = new QCheckBox();
      cb->setChecked(svc.GetBool(key, spec.value("default").toBool()));
      QObject::connect(cb, &QCheckBox::toggled, this,
                       [key](bool v) { SettingsService::Instance().Set(key, QJsonValue(v)); });
      editor = cb;
    } else if (type == "integer") {
      auto *sb = new QSpinBox();
      sb->setRange(spec.value("minimum").isDouble() ? spec.value("minimum").toInt() : INT_MIN,
                   spec.value("maximum").isDouble() ? spec.value("maximum").toInt() : INT_MAX);
      sb->setValue(svc.GetInt(key, spec.value("default").toInt()));
      QObject::connect(sb, qOverload<int>(&QSpinBox::valueChanged), this,
                       [key](int v) { SettingsService::Instance().Set(key, QJsonValue(v)); });
      editor = sb;
    } else if (type == "number") {
      auto *sb = new QDoubleSpinBox();
      sb->setDecimals(3);
      sb->setRange(-1e9, 1e9);
      sb->setValue(svc.GetDouble(key, spec.value("default").toDouble()));
      QObject::connect(sb, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                       [key](double v) { SettingsService::Instance().Set(key, QJsonValue(v)); });
      editor = sb;
    } else if (type == "string" && spec.contains("enum")) {
      auto *cb = new QComboBox();
      for (const auto &e : spec.value("enum").toArray()) cb->addItem(e.toString());
      const QString cur = svc.GetString(key, spec.value("default").toString());
      const int idx = cb->findText(cur);
      if (idx >= 0) cb->setCurrentIndex(idx);
      QObject::connect(cb, &QComboBox::currentTextChanged, this,
                       [key](const QString &v) { SettingsService::Instance().Set(key, QJsonValue(v)); });
      editor = cb;
    } else if (type == "string") {
      auto *le = new QLineEdit();
      le->setText(svc.GetString(key, spec.value("default").toString()));
      QObject::connect(le, &QLineEdit::editingFinished, this, [key, le]() {
        SettingsService::Instance().Set(key, QJsonValue(le->text()));
      });
      editor = le;
    } else if (type == "array" || type == "object") {
      auto *le = new QLineEdit();
      le->setText(QJsonDocument(QJsonValue(svc.GetJson(key)).isArray()
                                    ? QJsonDocument(svc.GetJson(key).toArray())
                                    : QJsonDocument(svc.GetJson(key).toObject()))
                      .toJson(QJsonDocument::Compact));
      le->setToolTip(tip + "\n(" + tr("JSON literal") + ")");
      QObject::connect(le, &QLineEdit::editingFinished, this, [key, le]() {
        QJsonParseError err;
        const auto doc = QJsonDocument::fromJson(le->text().toUtf8(), &err);
        if (err.error == QJsonParseError::NoError) {
          QJsonValue v = doc.isArray() ? QJsonValue(doc.array())
                                       : QJsonValue(doc.object());
          SettingsService::Instance().Set(key, v);
        }
      });
      editor = le;
    } else {
      editor = new QLabel("<unsupported type: " + type + ">");
    }
    form->addRow(label, editor);
  }
  scroll->setWidget(body);
  return scroll;
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void SettingsPage::OnNamespaceChanged(int row) {
  if (row < 0 || row >= pages_->count()) return;
  pages_->setCurrentIndex(row);
}

void SettingsPage::OnFilterChanged(const QString & /*text*/) {
  // Light-weight: just enable/disable namespace rows whose name matches.
  // Full per-field filtering would require recreating each page; deferred.
}

void SettingsPage::OnOpenUserJson() {
  emit RequestOpenJson(SettingsService::Instance().UserSettingsPath());
  accept();
}
void SettingsPage::OnOpenWorkspaceJson() {
  const QString ws = SettingsService::Instance().WorkspaceSettingsPath();
  if (ws.isEmpty()) return;
  // Ensure the file exists with a stub.
  QFile f(ws);
  if (!f.exists()) {
    QFileInfo fi(ws);
    QDir().mkpath(fi.absolutePath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
      f.write("{\n  // Workspace-scoped settings (override user settings).\n}\n");
      f.close();
    }
  }
  emit RequestOpenJson(ws);
  accept();
}
void SettingsPage::OnOpenDefaultsJson() {
  // Materialise to a temp file so the editor has something on disk to show.
  QString path = QDir::tempPath() + "/polyglot_default_settings.json";
  QFile f(path);
  if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
    f.write(SettingsService::Instance().DefaultsPrettyPrint().toUtf8());
    f.close();
  }
  emit RequestOpenJson(path);
  accept();
}
void SettingsPage::OnResetAll() {
  // Wipe user-layer file by writing "{}".
  const QString user = SettingsService::Instance().UserSettingsPath();
  QFile f(user);
  if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
    f.write("{}\n");
    f.close();
  }
  SettingsService::Instance().Load();
  accept();
}

}  // namespace polyglot::tools::ui
