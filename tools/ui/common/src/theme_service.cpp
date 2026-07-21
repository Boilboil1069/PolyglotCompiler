/**
 * @file     theme_service.cpp
 * @brief    Implementation of the JSON-driven theme loader for polyui.
 *
 * Pipeline per Activate():
 *   1. Look up ThemeMeta by id.
 *   2. Resolve the @c extends chain (depth-first, cycle-safe) into a single
 *      flat color map and tokenColors list.
 *   3. Convert the dotted color keys onto the legacy ThemeColors struct so
 *      every existing widget that reads ThemeManager::Active() keeps
 *      working with no code changes.
 *   4. Register/replace the theme in ThemeManager and call SetActiveTheme.
 *   5. Append the optional sibling QSS file to qApp->styleSheet().
 *   6. Persist the new id into SettingsService("workbench.colorTheme").
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-04-27
 */

#include "tools/ui/common/include/theme_service.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QPalette>
#include <QStandardPaths>
#include <QStringView>
#include <QTextStream>

#include <algorithm>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "tools/common/include/effective_settings_loader.h"
#include "tools/ui/common/include/settings_service.h"

namespace polyglot::tools::ui {

namespace {

// All built-in themes are shipped in the Qt resource system under this prefix.
constexpr const char *kBuiltinPrefix = ":/polyglot/themes";

// Built-in theme manifest list (alias -> on-disk file name).  The aliases are
// the qrc names declared by settings_resources.qrc.
constexpr const char *kBuiltinAliases[] = {
    ":/polyglot/themes/polyglot.light.polytheme.json",
    ":/polyglot/themes/polyglot.dark.polytheme.json",
    ":/polyglot/themes/polyglot.high-contrast.polytheme.json",
    ":/polyglot/themes/polyglot.solarized-dark.polytheme.json",
    ":/polyglot/themes/polyglot.solarized-light.polytheme.json",
};

QString ReadAllText(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
  return QString::fromUtf8(f.readAll());
}

QColor ParseColor(const QString &s) {
  if (s.isEmpty()) return {};
  // QColor handles "#rgb", "#rrggbb", "#aarrggbb" and "#rrggbbaa" (latter via
  // explicit setNamedColor which accepts both forms on Qt6).  Note that the
  // VS Code convention "#rrggbbaa" needs alpha re-mapped.
  if (s.startsWith('#') && s.length() == 9) {
    bool ok1 = false, ok2 = false, ok3 = false, ok4 = false;
    const int r = QStringView{s}.mid(1, 2).toInt(&ok1, 16);
    const int g = QStringView{s}.mid(3, 2).toInt(&ok2, 16);
    const int b = QStringView{s}.mid(5, 2).toInt(&ok3, 16);
    const int a = QStringView{s}.mid(7, 2).toInt(&ok4, 16);
    if (ok1 && ok2 && ok3 && ok4) return QColor(r, g, b, a);
  }
  return QColor::fromString(s);
}

QString ColorOr(const std::unordered_map<std::string, std::string> &m,
                const char *key, const char *fallback) {
  auto it = m.find(key);
  if (it != m.end()) return QString::fromStdString(it->second);
  return QString::fromUtf8(fallback);
}

// Convert a dotted-key color map into the legacy ThemeColors struct so we can
// keep using ThemeManager::Active() everywhere.  Unknown keys are ignored.
ThemeColors MapToThemeColors(const std::unordered_map<std::string, std::string> &m,
                             bool prefer_dark) {
  ThemeColors t{};
  // Workbench
  t.background    = ParseColor(ColorOr(m, "workbench.background", prefer_dark ? "#1e1e1e" : "#ffffff"));
  t.surface       = ParseColor(ColorOr(m, "workbench.surface",    prefer_dark ? "#2d2d2d" : "#f3f3f3"));
  t.surface_alt   = ParseColor(ColorOr(m, "workbench.surfaceAlt", prefer_dark ? "#252526" : "#ececec"));
  t.border        = ParseColor(ColorOr(m, "workbench.border",     prefer_dark ? "#454545" : "#cccccc"));
  // Text
  t.text          = ParseColor(ColorOr(m, "foreground",            prefer_dark ? "#cccccc" : "#333333"));
  t.text_secondary= ParseColor(ColorOr(m, "descriptionForeground", prefer_dark ? "#969696" : "#616161"));
  t.text_disabled = ParseColor(ColorOr(m, "disabledForeground",    prefer_dark ? "#666666" : "#a0a0a0"));
  // Accent
  t.accent        = ParseColor(ColorOr(m, "focusBorder",           "#0e639c"));
  t.accent_hover  = ParseColor(ColorOr(m, "button.hoverBackground","#1177bb"));
  t.accent_pressed= ParseColor(ColorOr(m, "button.background",     "#0e639c"));
  // Selection
  t.selection     = ParseColor(ColorOr(m, "selectionBackground",   prefer_dark ? "#094771" : "#add6ff"));
  t.selection_text= ParseColor(ColorOr(m, "selectionForeground",   prefer_dark ? "#ffffff" : "#000000"));
  // Status
  t.error         = ParseColor(ColorOr(m, "errorForeground",                "#f44747"));
  t.warning       = ParseColor(ColorOr(m, "editorWarning.foreground",       "#cca700"));
  t.success       = ParseColor(ColorOr(m, "testing.iconPassed",             "#89d185"));
  t.info          = ParseColor(ColorOr(m, "editorInfo.foreground",          "#3794ff"));
  // Editor
  t.editor_background      = ParseColor(ColorOr(m, "editor.background",            prefer_dark ? "#1e1e1e" : "#ffffff"));
  t.editor_text            = ParseColor(ColorOr(m, "editor.foreground",            prefer_dark ? "#d4d4d4" : "#000000"));
  t.editor_selection       = ParseColor(ColorOr(m, "editor.selectionBackground",   prefer_dark ? "#264f78" : "#add6ff"));
  t.editor_current_line    = ParseColor(ColorOr(m, "editor.lineHighlightBackground", prefer_dark ? "#2a2d2e" : "#f0f0f0"));
  t.editor_line_number     = ParseColor(ColorOr(m, "editorLineNumber.foreground",  prefer_dark ? "#858585" : "#999999"));
  t.editor_line_number_bg  = ParseColor(ColorOr(m, "editorGutter.background",      prefer_dark ? "#1e1e1e" : "#f7f7f7"));
  // Tabs
  t.tab_background         = ParseColor(ColorOr(m, "tab.inactiveBackground",       prefer_dark ? "#2d2d2d" : "#ececec"));
  t.tab_active             = ParseColor(ColorOr(m, "tab.activeBackground",         prefer_dark ? "#1e1e1e" : "#ffffff"));
  t.tab_active_indicator   = ParseColor(ColorOr(m, "tab.activeBorder",             "#007acc"));
  t.tab_hover              = ParseColor(ColorOr(m, "tab.hoverBackground",          prefer_dark ? "#383838" : "#dcdcdc"));
  t.tab_text               = ParseColor(ColorOr(m, "tab.inactiveForeground",       prefer_dark ? "#969696" : "#666666"));
  t.tab_active_text        = ParseColor(ColorOr(m, "tab.activeForeground",         prefer_dark ? "#ffffff" : "#000000"));
  // Status bar
  t.statusbar_background   = ParseColor(ColorOr(m, "statusBar.background",         "#007acc"));
  t.statusbar_text         = ParseColor(ColorOr(m, "statusBar.foreground",         "#ffffff"));
  // Menu
  t.menu_background        = ParseColor(ColorOr(m, "menu.background",              prefer_dark ? "#252526" : "#f3f3f3"));
  t.menu_text              = ParseColor(ColorOr(m, "menu.foreground",              prefer_dark ? "#cccccc" : "#333333"));
  t.menu_hover             = ParseColor(ColorOr(m, "menu.selectionBackground",     prefer_dark ? "#094771" : "#add6ff"));
  t.menu_separator         = ParseColor(ColorOr(m, "menu.separatorBackground",     prefer_dark ? "#454545" : "#cccccc"));
  // Buttons
  t.button_background      = ParseColor(ColorOr(m, "button.secondaryBackground",   prefer_dark ? "#3c3c3c" : "#e7e7e7"));
  t.button_text            = ParseColor(ColorOr(m, "button.secondaryForeground",   prefer_dark ? "#cccccc" : "#333333"));
  t.button_hover           = ParseColor(ColorOr(m, "button.secondaryHoverBackground", prefer_dark ? "#505050" : "#d0d0d0"));
  t.button_primary         = ParseColor(ColorOr(m, "button.background",            "#0e639c"));
  t.button_primary_text    = ParseColor(ColorOr(m, "button.foreground",            "#ffffff"));
  t.button_primary_hover   = ParseColor(ColorOr(m, "button.hoverBackground",       "#1177bb"));
  // Inputs
  t.input_background       = ParseColor(ColorOr(m, "input.background",             prefer_dark ? "#3c3c3c" : "#ffffff"));
  t.input_text             = ParseColor(ColorOr(m, "input.foreground",             prefer_dark ? "#cccccc" : "#333333"));
  t.input_border           = ParseColor(ColorOr(m, "input.border",                 prefer_dark ? "#555555" : "#cecece"));
  t.input_placeholder      = ParseColor(ColorOr(m, "input.placeholderForeground",  prefer_dark ? "#888888" : "#a0a0a0"));
  // Scrollbars
  t.scrollbar_bg           = ParseColor(ColorOr(m, "scrollbar.shadow",             prefer_dark ? "#1e1e1e" : "#f0f0f0"));
  t.scrollbar_thumb        = ParseColor(ColorOr(m, "scrollbarSlider.background",   prefer_dark ? "#424242" : "#c1c1c1"));
  t.scrollbar_thumb_hover  = ParseColor(ColorOr(m, "scrollbarSlider.hoverBackground", prefer_dark ? "#4f4f4f" : "#a8a8a8"));
  // Progress
  t.progress_background    = ParseColor(ColorOr(m, "progressBar.background",       prefer_dark ? "#1e1e1e" : "#f0f0f0"));
  t.progress_chunk         = ParseColor(ColorOr(m, "progressBar.foreground",       "#0e639c"));
  return t;
}

// Apply a global QPalette mirroring the new theme so non-stylesheet widgets
// also reflect the change.
void ApplyPalette(const ThemeColors &t) {
  if (auto *app = qobject_cast<QApplication *>(qApp); app) {
    QPalette p = app->palette();
    p.setColor(QPalette::Window,          t.background);
    p.setColor(QPalette::Base,            t.input_background);
    p.setColor(QPalette::AlternateBase,   t.surface_alt);
    p.setColor(QPalette::Text,            t.text);
    p.setColor(QPalette::WindowText,      t.text);
    p.setColor(QPalette::ButtonText,      t.button_text);
    p.setColor(QPalette::Button,          t.button_background);
    p.setColor(QPalette::Highlight,       t.selection);
    p.setColor(QPalette::HighlightedText, t.selection_text);
    p.setColor(QPalette::ToolTipBase,     t.surface);
    p.setColor(QPalette::ToolTipText,     t.text);
    p.setColor(QPalette::PlaceholderText, t.input_placeholder);
    p.setColor(QPalette::Disabled, QPalette::Text,       t.text_disabled);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, t.text_disabled);
    p.setColor(QPalette::Disabled, QPalette::WindowText, t.text_disabled);
    app->setPalette(p);
  }
}

// Walk the JSON tree and copy every leaf string under @c colors{} into the
// flat dotted map.  We accept either a flat structure (VS Code convention
// where keys are already dotted) or a nested structure.
void FlattenColors(const QJsonObject &obj, const QString &prefix,
                   std::unordered_map<std::string, std::string> *out) {
  for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
    const QString full = prefix.isEmpty() ? it.key() : (prefix + "." + it.key());
    if (it->isString()) {
      (*out)[full.toStdString()] = it->toString().toStdString();
    } else if (it->isObject()) {
      FlattenColors(it->toObject(), full, out);
    }
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// ThemeService — singleton lifecycle
// ---------------------------------------------------------------------------

ThemeService &ThemeService::Instance() {
  static ThemeService s;
  return s;
}

ThemeService::ThemeService()
    : QObject(nullptr),
      watcher_(std::make_unique<QFileSystemWatcher>(this)),
      debounce_(std::make_unique<QTimer>(this)) {
  debounce_->setSingleShot(true);
  debounce_->setInterval(500);
  connect(debounce_.get(), &QTimer::timeout, this, &ThemeService::RescanNow);
  connect(watcher_.get(), &QFileSystemWatcher::fileChanged,
          this, [this](const QString &) { ScheduleRescan(); });
  connect(watcher_.get(), &QFileSystemWatcher::directoryChanged,
          this, [this](const QString &) { ScheduleRescan(); });

  // Cache the JSON schema on first use so ValidateString() is allocation-free.
  schema_text_ = ReadAllText(":/polyglot/themes/theme_schema.json");

  ConnectSettings();
}

void ThemeService::ConnectSettings() {
  // React when the user flips workbench.colorTheme via SettingsPage or CLI.
  connect(&SettingsService::Instance(), &SettingsService::settingsChanged,
          this, [this](const QString &key, const QJsonValue &, const QJsonValue &nv) {
            if (key == "workbench.colorTheme" && nv.isString()) {
              const QString id = nv.toString();
              if (id != active_id_) Activate(id);
            }
          });
}

// ---------------------------------------------------------------------------
// Discovery
// ---------------------------------------------------------------------------

void ThemeService::Scan() { RescanNow(); }

void ThemeService::SetWorkspaceRoot(const QString &root) {
  if (root == workspace_root_) return;
  workspace_root_ = root;
  RescanNow();
}

void ThemeService::ScheduleRescan() { debounce_->start(); }

void ThemeService::RescanNow() {
  std::vector<ThemeMeta> next;
  diagnostics_.clear();

  ScanBuiltins(&next);
  ScanDirectory(UserThemesDir(),      "user",      &next);
  ScanDirectory(WorkspaceThemesDir(), "workspace", &next);

  std::sort(next.begin(), next.end(), [](const ThemeMeta &a, const ThemeMeta &b) {
    if (a.layer != b.layer) {
      // Render order: builtin -> user -> workspace, then by name.
      auto rank = [](const QString &l) {
        if (l == "builtin") return 0;
        if (l == "user")    return 1;
        return 2;
      };
      return rank(a.layer) < rank(b.layer);
    }
    return a.name.localeAwareCompare(b.name) < 0;
  });

  themes_ = std::move(next);
  RebuildWatchList();
  emit themesScanned();

  // If the active id disappeared (file deleted), fall back to default.
  if (!active_id_.isEmpty() && FindById(active_id_) == nullptr) {
    Activate("polyglot.dark");
  } else if (!active_id_.isEmpty()) {
    // Re-apply so editors and panels refresh after a hot reload.
    Activate(active_id_);
  }
}

void ThemeService::ScanBuiltins(std::vector<ThemeMeta> *out) {
  for (const char *alias : kBuiltinAliases) {
    ThemeMeta meta;
    QString err;
    if (LoadAndRegister(QString::fromUtf8(alias), "builtin", &meta, &err)) {
      out->push_back(meta);
    } else {
      diagnostics_.push_back({QString::fromUtf8(alias), err, true});
      emit themeError(err);
    }
  }
}

void ThemeService::ScanDirectory(const QString &dir, const QString &layer,
                                 std::vector<ThemeMeta> *out) {
  if (dir.isEmpty()) return;
  QDir d(dir);
  if (!d.exists()) return;
  const QStringList entries = d.entryList(
      {"*.polytheme.json"}, QDir::Files | QDir::Readable, QDir::Name);
  for (const QString &name : entries) {
    const QString abs = d.absoluteFilePath(name);
    ThemeMeta meta;
    QString err;
    if (LoadAndRegister(abs, layer, &meta, &err)) {
      out->push_back(meta);
    } else {
      diagnostics_.push_back({abs, err, true});
      emit themeError(err);
    }
  }
}

bool ThemeService::LoadAndRegister(const QString &path, const QString &layer,
                                   ThemeMeta *out_meta, QString *error) {
  const QString text = ReadAllText(path);
  if (text.isEmpty()) {
    if (error) *error = "theme file is empty or unreadable: " + path;
    return false;
  }
  QStringList errs;
  if (!ValidateString(text, &errs)) {
    if (error) *error = "schema validation failed: " + errs.join("; ");
    return false;
  }
  QJsonParseError pe{};
  const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &pe);
  if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
    if (error) *error = "JSON parse error: " + pe.errorString();
    return false;
  }
  const QJsonObject root = doc.object();
  ThemeMeta meta;
  meta.id          = root.value("id").toString();
  meta.name        = root.value("name").toString();
  meta.type        = root.value("type").toString("dark");
  meta.version     = root.value("version").toString("0.0.0");
  meta.author      = root.value("author").toString();
  meta.description = root.value("description").toString();
  meta.extends     = root.value("extends").toString();
  meta.qss         = root.value("qss").toString();
  meta.source_path = path;
  meta.layer       = layer;
  if (meta.id.isEmpty() || meta.name.isEmpty()) {
    if (error) *error = "theme is missing id or name: " + path;
    return false;
  }

  // Register colors immediately so Activate() can resolve extends chains
  // without re-loading from disk.
  std::unordered_map<std::string, std::string> flat;
  if (root.contains("colors") && root.value("colors").isObject()) {
    FlattenColors(root.value("colors").toObject(), {}, &flat);
  }
  const bool is_dark = meta.type == "dark" || meta.type == "high-contrast";
  ThemeColors tc = MapToThemeColors(flat, is_dark);
  ThemeManager::Instance().RegisterTheme(meta.name, tc);

  if (out_meta) *out_meta = meta;
  return true;
}

void ThemeService::RebuildWatchList() {
  if (!watcher_->files().isEmpty())       watcher_->removePaths(watcher_->files());
  if (!watcher_->directories().isEmpty()) watcher_->removePaths(watcher_->directories());
  QStringList files, dirs;
  for (const auto &m : themes_) {
    if (m.layer != "builtin" && QFileInfo::exists(m.source_path)) {
      files.append(m.source_path);
    }
  }
  if (QDir(UserThemesDir()).exists())      dirs.append(UserThemesDir());
  if (QDir(WorkspaceThemesDir()).exists()) dirs.append(WorkspaceThemesDir());
  if (!files.isEmpty()) watcher_->addPaths(files);
  if (!dirs.isEmpty())  watcher_->addPaths(dirs);
}

// ---------------------------------------------------------------------------
// Lookup helpers
// ---------------------------------------------------------------------------

const ThemeMeta *ThemeService::FindById(const QString &id) const {
  // Workspace overrides user overrides built-in: walk in *reverse* layer order.
  const ThemeMeta *best = nullptr;
  int best_rank = -1;
  for (const auto &m : themes_) {
    if (m.id != id) continue;
    const int rank = (m.layer == "workspace") ? 2 : (m.layer == "user" ? 1 : 0);
    if (rank > best_rank) { best = &m; best_rank = rank; }
  }
  return best;
}

const ThemeMeta *ThemeService::CurrentTheme() const {
  if (active_id_.isEmpty()) return nullptr;
  return FindById(active_id_);
}

QString ThemeService::ResolveColor(const QString &key) const {
  return active_colors_.value(key, QString{});
}

QString ThemeService::ResolveTokenColor(const QString &scope) const {
  // Exact match first; otherwise walk up the dotted scope (e.g.
  // "keyword.control.if" → "keyword.control" → "keyword").
  if (active_token_colors_.contains(scope)) return active_token_colors_.value(scope);
  QString s = scope;
  while (true) {
    const int dot = s.lastIndexOf('.');
    if (dot < 0) break;
    s.truncate(dot);
    if (active_token_colors_.contains(s)) return active_token_colors_.value(s);
  }
  return {};
}

// ---------------------------------------------------------------------------
// Activation
// ---------------------------------------------------------------------------

bool ThemeService::Activate(const QString &theme_id) {
  const ThemeMeta *meta = FindById(theme_id);
  if (!meta) {
    emit themeError("unknown theme id: " + theme_id);
    return false;
  }

  // Resolve extends chain (deepest ancestor first) into a single flat map.
  std::unordered_map<std::string, std::string> flat;
  std::vector<QJsonArray> token_layers;  // child overrides parent
  std::unordered_set<std::string> visited;
  std::vector<const ThemeMeta *> chain;
  const ThemeMeta *cursor = meta;
  while (cursor) {
    if (visited.count(cursor->id.toStdString())) break;  // cycle guard
    visited.insert(cursor->id.toStdString());
    chain.push_back(cursor);
    if (cursor->extends.isEmpty()) break;
    cursor = FindById(cursor->extends);
  }
  // Walk parent -> child so child wins.
  for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
    const QString text = ReadAllText((*it)->source_path);
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) continue;
    const QJsonObject root = doc.object();
    if (root.contains("colors") && root.value("colors").isObject()) {
      FlattenColors(root.value("colors").toObject(), {}, &flat);
    }
    if (root.contains("tokenColors") && root.value("tokenColors").isArray()) {
      token_layers.push_back(root.value("tokenColors").toArray());
    }
  }

  const bool is_dark = meta->type == "dark" || meta->type == "high-contrast";
  ThemeColors composed = MapToThemeColors(flat, is_dark);

  // Push into the legacy ThemeManager under the display name and activate.
  ThemeManager::Instance().RegisterTheme(meta->name, composed);
  ThemeManager::Instance().SetActiveTheme(meta->name);

  ApplyPalette(composed);

  // Apply optional sibling QSS file.
  if (!meta->qss.isEmpty()) {
    const QString qss_path = QFileInfo(meta->source_path).absoluteDir().filePath(meta->qss);
    const QString qss_text = ReadAllText(qss_path);
    if (auto *app = qobject_cast<QApplication *>(qApp); app && !qss_text.isEmpty()) {
      app->setStyleSheet(qss_text);
    }
  } else {
    if (auto *app = qobject_cast<QApplication *>(qApp); app) {
      app->setStyleSheet(QString());  // clear stale QSS from previous theme
    }
  }

  active_id_ = meta->id;

  // Cache the resolved color map and tokenColors for the Inspect commands.
  active_colors_.clear();
  for (const auto &kv : flat) {
    active_colors_.insert(QString::fromStdString(kv.first),
                          QString::fromStdString(kv.second));
  }
  active_token_colors_.clear();
  // tokenColors layers are pushed parent-first; iterate in same order so child
  // entries override parents.  Each entry is { scope: <str|array>, settings:
  // { foreground: "#rrggbb", ... } } in TextMate style.
  for (const QJsonArray &layer : token_layers) {
    for (const QJsonValue &v : layer) {
      if (!v.isObject()) continue;
      const QJsonObject e = v.toObject();
      const QJsonValue scope_v = e.value("scope");
      const QString fg = e.value("settings").toObject().value("foreground").toString();
      if (fg.isEmpty()) continue;
      QStringList scopes;
      if (scope_v.isString()) {
        scopes = scope_v.toString().split(',', Qt::SkipEmptyParts);
      } else if (scope_v.isArray()) {
        for (const QJsonValue &s : scope_v.toArray()) scopes.append(s.toString());
      }
      for (QString s : scopes) {
        s = s.trimmed();
        if (!s.isEmpty()) active_token_colors_.insert(s, fg);
      }
    }
  }

  // Persist the choice (silently — guard recursion).
  const QString current = SettingsService::Instance().GetString("workbench.colorTheme");
  if (current != meta->id) {
    SettingsService::Instance().Set("workbench.colorTheme", QJsonValue(meta->id),
                                    SettingsService::Scope::User);
  }

  emit themeChanged(meta->id);
  return true;
}

// ---------------------------------------------------------------------------
// Validation (lightweight: structural checks; full Draft-2020-12 checking is
// performed by tools/common json_schema_validator if linked in.)
// ---------------------------------------------------------------------------

bool ThemeService::ValidateFile(const QString &path, QStringList *errors) const {
  return ValidateString(ReadAllText(path), errors);
}

bool ThemeService::ValidateString(const QString &json_text, QStringList *errors) const {
  QStringList errs;
  if (json_text.isEmpty()) {
    errs << "input is empty";
    if (errors) *errors = errs;
    return false;
  }
  QJsonParseError pe{};
  const QJsonDocument doc = QJsonDocument::fromJson(json_text.toUtf8(), &pe);
  if (pe.error != QJsonParseError::NoError) {
    errs << ("JSON parse error: " + pe.errorString());
    if (errors) *errors = errs;
    return false;
  }
  if (!doc.isObject()) {
    errs << "top-level value must be an object";
    if (errors) *errors = errs;
    return false;
  }
  const QJsonObject root = doc.object();
  for (const char *required : {"id", "name", "type"}) {
    if (!root.contains(QString::fromUtf8(required))) {
      errs << ("missing required field: " + QString::fromUtf8(required));
    }
  }
  if (root.contains("type")) {
    const QString t = root.value("type").toString();
    if (t != "dark" && t != "light" && t != "high-contrast") {
      errs << ("type must be one of dark|light|high-contrast (got: " + t + ")");
    }
  }
  if (root.contains("colors") && !root.value("colors").isObject()) {
    errs << "colors must be an object";
  }
  if (root.contains("tokenColors") && !root.value("tokenColors").isArray()) {
    errs << "tokenColors must be an array";
  }
  if (errors) *errors = errs;
  return errs.isEmpty();
}

// ---------------------------------------------------------------------------
// Export / install / uninstall
// ---------------------------------------------------------------------------

bool ThemeService::ExportToFile(const QString &theme_id, const QString &out_path,
                                QString *error) const {
  const ThemeMeta *meta = FindById(theme_id);
  if (!meta) {
    if (error) *error = "unknown theme id: " + theme_id;
    return false;
  }
  // Walk extends chain and merge colors / tokenColors so the exported file is
  // self-contained.
  std::unordered_map<std::string, std::string> flat;
  QJsonArray tokens;
  std::vector<const ThemeMeta *> chain;
  std::unordered_set<std::string> visited;
  const ThemeMeta *cursor = meta;
  while (cursor) {
    if (visited.count(cursor->id.toStdString())) break;
    visited.insert(cursor->id.toStdString());
    chain.push_back(cursor);
    if (cursor->extends.isEmpty()) break;
    cursor = FindById(cursor->extends);
  }
  for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
    const QString text = ReadAllText((*it)->source_path);
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) continue;
    const QJsonObject root = doc.object();
    if (root.contains("colors") && root.value("colors").isObject()) {
      FlattenColors(root.value("colors").toObject(), {}, &flat);
    }
    if (root.contains("tokenColors") && root.value("tokenColors").isArray()) {
      const QJsonArray arr = root.value("tokenColors").toArray();
      for (const auto &v : arr) tokens.append(v);
    }
  }

  QJsonObject root;
  root["name"]    = meta->name;
  root["id"]      = meta->id;
  root["type"]    = meta->type;
  root["version"] = meta->version;
  root["author"]  = meta->author;
  if (!meta->description.isEmpty()) root["description"] = meta->description;
  QJsonObject colors;
  for (const auto &kv : flat) colors[QString::fromStdString(kv.first)] = QString::fromStdString(kv.second);
  root["colors"]      = colors;
  root["tokenColors"] = tokens;

  QFile f(out_path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    if (error) *error = "cannot open output file for writing: " + out_path;
    return false;
  }
  f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
  return true;
}

QString ThemeService::InstallFromFile(const QString &source_path, QString *error) {
  QStringList errs;
  if (!ValidateFile(source_path, &errs)) {
    if (error) *error = "invalid theme file: " + errs.join("; ");
    return {};
  }
  const QString user_dir = UserThemesDir();
  QDir().mkpath(user_dir);
  const QString dest = QDir(user_dir).absoluteFilePath(QFileInfo(source_path).fileName());
  if (QFile::exists(dest)) QFile::remove(dest);
  if (!QFile::copy(source_path, dest)) {
    if (error) *error = "copy failed (target may already exist): " + dest;
    return {};
  }
  RescanNow();
  return dest;
}

bool ThemeService::Uninstall(const QString &theme_id, QString *error) {
  const ThemeMeta *meta = FindById(theme_id);
  if (!meta) {
    if (error) *error = "unknown theme id: " + theme_id;
    return false;
  }
  if (meta->layer == "builtin") {
    if (error) *error = "built-in themes cannot be uninstalled";
    return false;
  }
  if (!QFile::remove(meta->source_path)) {
    if (error) *error = "failed to remove file: " + meta->source_path;
    return false;
  }
  RescanNow();
  return true;
}

// ---------------------------------------------------------------------------
// Path resolution
// ---------------------------------------------------------------------------

QString ThemeService::UserThemesDir() const {
  const auto p = polyglot::tools::common::UserSettingsPath().parent_path();
  return QString::fromStdString(p.string()) + "/themes";
}

QString ThemeService::WorkspaceThemesDir() const {
  if (workspace_root_.isEmpty()) return {};
  return workspace_root_ + "/.polyglot/themes";
}

QString ThemeService::SchemaJson() const { return schema_text_; }

}  // namespace polyglot::tools::ui
