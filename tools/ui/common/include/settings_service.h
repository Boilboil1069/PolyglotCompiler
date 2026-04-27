/**
 * @file     settings_service.h
 * @brief    Qt-side settings service backed by JSON files
 *
 * Wraps EffectiveSettingsLoader with a Qt singleton, file watcher, change
 * signals and one-shot QSettings migration so every UI panel can subscribe
 * to settings changes and react in real time.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-04-27
 */
#pragma once

#include <QFileSystemWatcher>
#include <QJsonValue>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <filesystem>
#include <memory>

#include "tools/common/include/effective_settings_loader.h"

namespace polyglot::tools::ui {

/**
 * @brief Centralised settings store for the polyui IDE.
 *
 * Three-layer JSON merge (default < user < workspace).  Writes always go to
 * either the user or workspace layer; the merged "effective" view is rebuilt
 * after every write and broadcast via @ref settingsChanged.
 */
class SettingsService : public QObject {
  Q_OBJECT

 public:
  enum class Scope { User, Workspace };

  static SettingsService &Instance();

  /// Reload all layers from disk.
  void Load();

  /// Set the active workspace root (e.g. when the user opens a folder).
  void SetWorkspaceRoot(const QString &root);
  QString WorkspaceRoot() const { return workspace_root_; }

  /// Generic getter.  @p key is dotted (e.g. "editor.tabSize").
  QJsonValue GetJson(const QString &key) const;
  QString    GetString(const QString &key, const QString &fallback = {}) const;
  int        GetInt(const QString &key, int fallback = 0) const;
  double     GetDouble(const QString &key, double fallback = 0.0) const;
  bool       GetBool(const QString &key, bool fallback = false) const;
  QStringList GetStringList(const QString &key) const;

  /// Generic setter.  Writes to the chosen scope's JSON file and re-merges.
  void Set(const QString &key, const QJsonValue &value, Scope scope = Scope::User);

  /// Reset a single key in the chosen scope (removes the field).
  void Reset(const QString &key, Scope scope);

  /// File paths for the three layers.
  QString UserSettingsPath() const;
  QString WorkspaceSettingsPath() const;
  QString UserKeybindingsPath() const;

  /// Pretty-printed effective tree (for `--print-effective-settings`).
  QString EffectivePrettyPrint() const;
  QString DefaultsPrettyPrint() const;

  /// Diagnostics from the most recent load (validation errors, parse errors, …).
  const std::vector<polyglot::tools::common::SettingsDiagnostic> &Diagnostics() const {
    return last_.diagnostics;
  }

  /// One-shot migration: read legacy QSettings("PolyglotCompiler", "IDE")
  /// keys and translate them to the new dotted JSON namespace, writing the
  /// result into the user settings file.  Backs up the QSettings tree to
  /// `<settings>.qsettings.bak`.  Idempotent (no-op after first success).
  void MigrateLegacyQSettings();

 signals:
  /// Emitted when any settings field changes (including externally).
  /// Subscribers can either filter by @p key or unconditionally re-read.
  void settingsChanged(const QString &key, const QJsonValue &oldValue,
                       const QJsonValue &newValue);
  /// Emitted once after a full reload (e.g. workspace switch / file watcher).
  void settingsReloaded();

 private slots:
  void OnFileChanged(const QString &path);
  void OnDebouncedReload();

 private:
  SettingsService();
  ~SettingsService() override;

  void Reload();
  void RewatchFiles();
  void WriteBack(Scope scope, const nlohmann::json &tree);

  static QString DefaultsResourceText();
  static QString SchemaResourceText();

  QString workspace_root_;
  polyglot::tools::common::EffectiveSettings last_;
  polyglot::tools::common::EffectiveSettings previous_;  // for diffing on reload

  QFileSystemWatcher watcher_;
  QTimer debounce_;
  bool migrated_{false};
};

}  // namespace polyglot::tools::ui
