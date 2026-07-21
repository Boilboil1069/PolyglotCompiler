/**
 * @file     theme_service.h
 * @brief    JSON-driven theme loader, watcher and activator for polyui
 *
 * Discovers `.polytheme.json` files across three layers (built-in resources,
 * user config, workspace `.polyglot/themes/`), validates them against
 * @c theme_schema.json, resolves the @c extends chain, maps the dotted color
 * keys onto the legacy @ref ThemeColors struct, and pushes the result into
 * the existing @ref ThemeManager.  Hot-reloads via QFileSystemWatcher.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-04-27
 */
#pragma once

#include <QFileSystemWatcher>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <memory>
#include <vector>

#include "tools/ui/common/include/theme_manager.h"

namespace polyglot::tools::ui {

/** @brief Compact metadata for a discovered theme. */
struct ThemeMeta {
  QString id;            ///< Reverse-DNS id from the JSON @c id field.
  QString name;          ///< Human-readable display name.
  QString type;          ///< "dark" | "light" | "high-contrast".
  QString version;       ///< Semver string.
  QString author;        ///< Author / publisher.
  QString description;   ///< One-line description.
  QString source_path;   ///< Absolute file path or qrc URI.
  QString layer;         ///< "builtin" | "user" | "workspace".
  QString extends;       ///< Parent theme id, may be empty.
  QString qss;           ///< Optional QSS appended on top.
};

/** @brief A schema/parse diagnostic produced while loading a theme file. */
struct ThemeDiagnostic {
  QString file;     ///< Source file (or qrc URI).
  QString message;  ///< Human-readable description.
  bool    is_error{true};
};

/**
 * @brief Singleton service that owns all loaded themes.
 *
 * The service does not replace @ref ThemeManager — it composes on top of it,
 * registering each loaded theme into the manager's map under its display name.
 * Existing call sites that consume @c ThemeManager::Instance().XStylesheet()
 * keep working unchanged.
 */
class ThemeService : public QObject {
  Q_OBJECT

 public:
  static ThemeService &Instance();

  /// Scan all three layers and register every valid theme.  Idempotent.
  void Scan();

  /// Set the workspace root used to discover @c <workspace>/.polyglot/themes/.
  void SetWorkspaceRoot(const QString &root);

  /// All currently registered themes (sorted by display name).
  std::vector<ThemeMeta> Themes() const { return themes_; }

  /// Look up a theme by id.  Returns nullptr if not registered.
  const ThemeMeta *FindById(const QString &id) const;

  /// The currently active theme metadata, or nullptr if none.
  const ThemeMeta *CurrentTheme() const;

  /// Activate a theme by id.  Updates @c workbench.colorTheme and
  /// @ref ThemeManager and emits @ref themeChanged.  Returns false if the id
  /// is unknown.
  bool Activate(const QString &theme_id);

  /// Validate a single theme file (path on disk or qrc URI) against the
  /// embedded JSON schema.  Returns true if no errors.
  bool ValidateFile(const QString &path, QStringList *errors = nullptr) const;

  /// Validate a raw JSON string against the embedded schema.
  bool ValidateString(const QString &json_text, QStringList *errors = nullptr) const;

  /// Write a theme to a file in canonical .polytheme.json form.
  bool ExportToFile(const QString &theme_id, const QString &out_path,
                    QString *error = nullptr) const;

  /// Copy a .polytheme.json file into the user-themes directory.
  /// Returns the new on-disk path or an empty string on failure.
  QString InstallFromFile(const QString &source_path, QString *error = nullptr);

  /// Remove a user-installed theme by id.  Built-in themes cannot be removed.
  bool Uninstall(const QString &theme_id, QString *error = nullptr);

  /// Last batch of diagnostics produced by Scan().
  std::vector<ThemeDiagnostic> LastDiagnostics() const { return diagnostics_; }

  /// Absolute path of the user-level themes directory (created on demand).
  QString UserThemesDir() const;

  /// Absolute path of the workspace themes directory (may be empty).
  QString WorkspaceThemesDir() const;

  /// Embedded JSON schema text (loaded once from qrc).
  QString SchemaJson() const;

  /// Resolve a flattened UI color key (e.g. "editor.background") against the
  /// currently active theme.  Returns "#rrggbb[aa]" or empty if unset.
  QString ResolveColor(const QString &key) const;

  /// Resolve a tokenColors scope (e.g. "comment", "keyword.control") against
  /// the active theme.  Returns the foreground color, or empty if unset.
  QString ResolveTokenColor(const QString &scope) const;

 signals:
  /// Emitted after Activate() succeeds; carries the new theme id.
  void themeChanged(const QString &theme_id);

  /// Emitted after Scan() finishes.
  void themesScanned();

  /// Emitted when a theme failed to load or validate.
  void themeError(const QString &message);

 private:
  ThemeService();
  ~ThemeService() override = default;
  ThemeService(const ThemeService &)            = delete;
  ThemeService &operator=(const ThemeService &) = delete;

  // Internal helpers
  void   ConnectSettings();
  void   ScheduleRescan();
  void   RescanNow();
  void   ScanDirectory(const QString &dir, const QString &layer,
                       std::vector<ThemeMeta> *out);
  void   ScanBuiltins(std::vector<ThemeMeta> *out);
  bool   LoadAndRegister(const QString &path, const QString &layer,
                         ThemeMeta *out_meta, QString *error);
  void   RebuildWatchList();

  std::vector<ThemeMeta>        themes_;
  std::vector<ThemeDiagnostic>  diagnostics_;
  QString                       workspace_root_;
  QString                       active_id_;
  QMap<QString, QString>        active_colors_;        ///< flat key → hex
  QMap<QString, QString>        active_token_colors_;  ///< scope → hex
  std::unique_ptr<QFileSystemWatcher> watcher_;
  std::unique_ptr<QTimer>            debounce_;
  QString                            schema_text_;
};

}  // namespace polyglot::tools::ui
