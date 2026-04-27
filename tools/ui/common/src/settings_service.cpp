/**
 * @file     settings_service.cpp
 * @brief    SettingsService implementation
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-04-27
 */
#include "tools/ui/common/include/settings_service.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTextStream>
#include <QVariant>

#include <fstream>
#include <map>

namespace polyglot::tools::ui {

using polyglot::tools::common::EffectiveSettings;
using polyglot::tools::common::SettingsDiagnostic;
using polyglot::tools::common::DeepMerge;
using polyglot::tools::common::GetByDottedKey;
using polyglot::tools::common::SetByDottedKey;
using polyglot::tools::common::PrettyPrint;
namespace pcommon = polyglot::tools::common;
using nlohmann::json;

// ---------------------------------------------------------------------------
// Helpers: nlohmann::json <-> QJsonValue
// ---------------------------------------------------------------------------

static QJsonValue ToQ(const json &j) {
  if (j.is_null())      return QJsonValue(QJsonValue::Null);
  if (j.is_boolean())   return QJsonValue(j.get<bool>());
  if (j.is_number_integer()) return QJsonValue(static_cast<qint64>(j.get<long long>()));
  if (j.is_number())    return QJsonValue(j.get<double>());
  if (j.is_string())    return QJsonValue(QString::fromStdString(j.get<std::string>()));
  if (j.is_array()) {
    QJsonArray arr;
    for (const auto &item : j) arr.append(ToQ(item));
    return arr;
  }
  if (j.is_object()) {
    QJsonObject obj;
    for (auto it = j.begin(); it != j.end(); ++it) {
      obj.insert(QString::fromStdString(it.key()), ToQ(*it));
    }
    return obj;
  }
  return QJsonValue();
}

static json FromQ(const QJsonValue &v) {
  switch (v.type()) {
    case QJsonValue::Null: return nullptr;
    case QJsonValue::Bool: return v.toBool();
    case QJsonValue::Double: {
      double d = v.toDouble();
      if (d == static_cast<long long>(d)) return static_cast<long long>(d);
      return d;
    }
    case QJsonValue::String: return v.toString().toStdString();
    case QJsonValue::Array: {
      json a = json::array();
      for (const auto &item : v.toArray()) a.push_back(FromQ(item));
      return a;
    }
    case QJsonValue::Object: {
      json o = json::object();
      const auto obj = v.toObject();
      for (auto it = obj.begin(); it != obj.end(); ++it) {
        o[it.key().toStdString()] = FromQ(it.value());
      }
      return o;
    }
    default: return nullptr;
  }
}

// ---------------------------------------------------------------------------
// Resource readers
// ---------------------------------------------------------------------------

QString SettingsService::DefaultsResourceText() {
  QFile f(":/polyglot/settings/default_settings.json");
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return "{}";
  return QString::fromUtf8(f.readAll());
}
QString SettingsService::SchemaResourceText() {
  QFile f(":/polyglot/settings/settings_schema.json");
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return "{}";
  return QString::fromUtf8(f.readAll());
}

// ---------------------------------------------------------------------------
// Singleton + lifecycle
// ---------------------------------------------------------------------------

SettingsService &SettingsService::Instance() {
  static SettingsService instance;
  return instance;
}

SettingsService::SettingsService() : QObject(nullptr) {
  debounce_.setSingleShot(true);
  debounce_.setInterval(200);
  connect(&debounce_, &QTimer::timeout, this, &SettingsService::OnDebouncedReload);
  connect(&watcher_, &QFileSystemWatcher::fileChanged, this, &SettingsService::OnFileChanged);

  Load();
  MigrateLegacyQSettings();
}

SettingsService::~SettingsService() = default;

// ---------------------------------------------------------------------------
// Load / reload
// ---------------------------------------------------------------------------

void SettingsService::Load() {
  previous_ = last_;
  last_ = pcommon::LoadEffectiveSettings(
      DefaultsResourceText().toStdString(), SchemaResourceText().toStdString(),
      workspace_root_.isEmpty() ? std::filesystem::path{}
                                : std::filesystem::path(workspace_root_.toStdString()));
  RewatchFiles();
  emit settingsReloaded();
}

void SettingsService::Reload() { Load(); }

void SettingsService::RewatchFiles() {
  if (!watcher_.files().isEmpty()) watcher_.removePaths(watcher_.files());
  const QString user = UserSettingsPath();
  const QString ws = WorkspaceSettingsPath();
  if (!user.isEmpty() && QFileInfo::exists(user)) watcher_.addPath(user);
  if (!ws.isEmpty()   && QFileInfo::exists(ws))   watcher_.addPath(ws);
}

void SettingsService::OnFileChanged(const QString & /*path*/) {
  debounce_.start();  // 200ms debounce
}

void SettingsService::OnDebouncedReload() {
  Load();
  // Diff old vs new effective tree; emit settingsChanged for each delta.
  const auto &before = previous_.effective;
  const auto &after = last_.effective;
  if (!before.is_object() || !after.is_object()) return;
  std::map<std::string, json> beforeMap, afterMap;
  if (before.is_object()) for (auto it = before.begin(); it != before.end(); ++it) beforeMap[it.key()] = *it;
  if (after.is_object())  for (auto it = after.begin();  it != after.end();  ++it) afterMap[it.key()]  = *it;
  for (const auto &[k, v] : afterMap) {
    auto bit = beforeMap.find(k);
    if (bit == beforeMap.end() || bit->second != v) {
      emit settingsChanged(QString::fromStdString(k),
                           bit == beforeMap.end() ? QJsonValue() : ToQ(bit->second),
                           ToQ(v));
    }
  }
  for (const auto &[k, v] : beforeMap) {
    if (!afterMap.count(k)) {
      emit settingsChanged(QString::fromStdString(k), ToQ(v), QJsonValue());
    }
  }
}

// ---------------------------------------------------------------------------
// Workspace
// ---------------------------------------------------------------------------

void SettingsService::SetWorkspaceRoot(const QString &root) {
  if (root == workspace_root_) return;
  workspace_root_ = root;
  Load();
}

// ---------------------------------------------------------------------------
// Path accessors
// ---------------------------------------------------------------------------

QString SettingsService::UserSettingsPath() const {
  return QString::fromStdString(pcommon::UserSettingsPath().string());
}
QString SettingsService::UserKeybindingsPath() const {
  return QString::fromStdString(pcommon::UserKeybindingsPath().string());
}
QString SettingsService::WorkspaceSettingsPath() const {
  if (workspace_root_.isEmpty()) return {};
  return QString::fromStdString(
      pcommon::WorkspaceSettingsPath(workspace_root_.toStdString()).string());
}

// ---------------------------------------------------------------------------
// Getters
// ---------------------------------------------------------------------------

QJsonValue SettingsService::GetJson(const QString &key) const {
  const json v = GetByDottedKey(last_.effective, key.toStdString());
  return v.is_null() ? QJsonValue() : ToQ(v);
}
QString SettingsService::GetString(const QString &key, const QString &fallback) const {
  const auto v = GetJson(key);
  return v.isString() ? v.toString() : fallback;
}
int SettingsService::GetInt(const QString &key, int fallback) const {
  const auto v = GetJson(key);
  return v.isDouble() ? static_cast<int>(v.toDouble()) : fallback;
}
double SettingsService::GetDouble(const QString &key, double fallback) const {
  const auto v = GetJson(key);
  return v.isDouble() ? v.toDouble() : fallback;
}
bool SettingsService::GetBool(const QString &key, bool fallback) const {
  const auto v = GetJson(key);
  return v.isBool() ? v.toBool() : fallback;
}
QStringList SettingsService::GetStringList(const QString &key) const {
  const auto v = GetJson(key);
  if (!v.isArray()) return {};
  QStringList out;
  for (const auto &item : v.toArray()) {
    if (item.isString()) out << item.toString();
  }
  return out;
}

// ---------------------------------------------------------------------------
// Setters
// ---------------------------------------------------------------------------

static void EnsureParentDir(const QString &path) {
  QFileInfo fi(path);
  QDir().mkpath(fi.absolutePath());
}

void SettingsService::WriteBack(Scope scope, const json &tree) {
  const QString path = (scope == Scope::User) ? UserSettingsPath() : WorkspaceSettingsPath();
  if (path.isEmpty()) return;
  EnsureParentDir(path);
  std::ofstream out(path.toStdString(), std::ios::binary | std::ios::trunc);
  if (!out) return;
  out << tree.dump(2);
}

void SettingsService::Set(const QString &key, const QJsonValue &value, Scope scope) {
  json &target = (scope == Scope::User) ? last_.user : last_.workspace;
  if (!target.is_object()) target = json::object();

  const QJsonValue oldVal = GetJson(key);
  SetByDottedKey(target, key.toStdString(), FromQ(value));
  WriteBack(scope, target);

  // Re-merge effective.
  last_.effective = last_.defaults;
  DeepMerge(last_.effective, last_.user);
  DeepMerge(last_.effective, last_.workspace);

  emit settingsChanged(key, oldVal, value);
}

void SettingsService::Reset(const QString &key, Scope scope) {
  json &target = (scope == Scope::User) ? last_.user : last_.workspace;
  if (!target.is_object()) return;
  if (!target.contains(key.toStdString())) return;
  const QJsonValue oldVal = GetJson(key);
  target.erase(key.toStdString());
  WriteBack(scope, target);
  last_.effective = last_.defaults;
  DeepMerge(last_.effective, last_.user);
  DeepMerge(last_.effective, last_.workspace);
  emit settingsChanged(key, oldVal, GetJson(key));
}

// ---------------------------------------------------------------------------
// Pretty-print
// ---------------------------------------------------------------------------

QString SettingsService::EffectivePrettyPrint() const {
  return QString::fromStdString(PrettyPrint(last_.effective));
}
QString SettingsService::DefaultsPrettyPrint() const {
  return QString::fromStdString(PrettyPrint(last_.defaults));
}

// ---------------------------------------------------------------------------
// Legacy QSettings migration
// ---------------------------------------------------------------------------

void SettingsService::MigrateLegacyQSettings() {
  if (migrated_) return;
  migrated_ = true;

  // Run only once: if a marker file exists, we already migrated.
  const QString user = UserSettingsPath();
  const QString marker = user + ".qsettings.bak";
  if (QFileInfo::exists(marker)) return;

  QSettings s("PolyglotCompiler", "IDE");
  if (s.allKeys().isEmpty()) return;  // nothing to migrate

  // Map legacy QSettings keys → new dotted keys.
  static const std::map<QString, QString> kMap = {
      {"appearance/font_family",      "editor.fontFamily"},
      {"appearance/font_size",        "editor.fontSize"},
      {"appearance/show_toolbar",     "workbench.showToolbar"},
      {"appearance/show_statusbar",   "workbench.showStatusBar"},
      {"editor/tab_width",            "editor.tabSize"},
      {"editor/insert_spaces",        "editor.insertSpaces"},
      {"editor/word_wrap",            "editor.wordWrap"},
      {"editor/show_line_numbers",    "editor.lineNumbers"},
      {"editor/auto_indent",          "editor.autoIndent"},
      {"editor/highlight_current_line", "editor.highlightCurrentLine"},
      {"editor/bracket_matching",     "editor.bracketMatching"},
      {"topology/layout_mode",        "topology.layoutAlgorithm"},
  };

  if (!last_.user.is_object()) last_.user = json::object();
  for (const auto &[old_key, new_key] : kMap) {
    if (!s.contains(old_key)) continue;
    const QVariant v = s.value(old_key);
    QJsonValue jv;
    switch (v.typeId()) {
      case QMetaType::Bool:   jv = v.toBool(); break;
      case QMetaType::Int:
      case QMetaType::LongLong:
      case QMetaType::UInt:
      case QMetaType::ULongLong: jv = QJsonValue(static_cast<qint64>(v.toLongLong())); break;
      case QMetaType::Double: jv = v.toDouble(); break;
      default:                jv = v.toString(); break;
    }
    SetByDottedKey(last_.user, new_key.toStdString(), FromQ(jv));
  }
  EnsureParentDir(user);
  WriteBack(Scope::User, last_.user);
  // Backup: write a snapshot of QSettings keys → marker file (JSON).
  json bak = json::object();
  for (const auto &k : s.allKeys()) {
    bak[k.toStdString()] = s.value(k).toString().toStdString();
  }
  std::ofstream bakOut(marker.toStdString());
  if (bakOut) bakOut << bak.dump(2);

  // Re-merge.
  last_.effective = last_.defaults;
  DeepMerge(last_.effective, last_.user);
  DeepMerge(last_.effective, last_.workspace);
  emit settingsReloaded();
}

}  // namespace polyglot::tools::ui
