/**
 * @file     polyui_cli.h
 * @brief    Shared command-line and theme bootstrap helpers used by the
 *           three platform-specific @c polyui main executables.
 *
 * The helpers centralize:
 *   1. CLI parsing for theme-system flags
 *      (@c --theme, @c --list-themes, @c --validate-theme,
 *      @c --headless, @c --screenshot).
 *   2. Bootstrap of the @ref ThemeService — scans built-in/user/workspace
 *      theme layers and activates either the requested id or the
 *      persisted @c workbench.colorTheme value (default @c polyglot.dark).
 *   3. A neutral fallback @c QPalette so the IDE still renders something
 *      sensible if no theme can be loaded.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 */
#pragma once

#include <QString>
#include <QStringList>
#include <string>

class QApplication;
class QWidget;

namespace polyglot::tools::ui {

/** @brief Parsed @c polyui command-line options (theme system + classic). */
struct PolyUiCliOptions {
  // --- classic flags (preserved from the previous main.cpp) ---
  bool        show_help{false};
  bool        show_version{false};
  std::string initial_folder;

  // --- theme-system flags ---
  QString     theme;             ///< --theme <id|path>
  bool        list_themes{false};///< --list-themes
  QString     validate_theme;    ///< --validate-theme <path>
  bool        headless{false};   ///< --headless (offscreen platform)
  QString     screenshot;        ///< --screenshot <out.png>

  /// True when the parser recognized at least one CLI flag that should
  /// terminate the program early without showing the main window.
  bool ShouldExitEarly() const {
    return show_help || show_version || list_themes || !validate_theme.isEmpty();
  }
};

/// Parse the standard @c (argc,argv) into @ref PolyUiCliOptions.  Unknown
/// flags are silently ignored so that platform-specific @c main files can
/// add their own switches around this call.
PolyUiCliOptions ParsePolyUiArgs(int argc, char *argv[]);

/// Print the canonical @c --help banner shared across all platforms.
void PrintPolyUiUsage();

/// Print version + Qt build info.  @p platform_suffix is appended to the
/// "Built with Qt …" line so the three mains can identify themselves.
void PrintPolyUiVersion(const char *platform_suffix);

/// Apply a neutral dark @c QPalette so the IDE remains readable before any
/// theme has been applied.  Does **not** call @c setStyleSheet — the actual
/// QSS comes from the active @c .polytheme.json (or its sibling @c .qss).
void ApplyFallbackDarkPalette(QApplication &app);

/// Initialize @ref ThemeService and activate the requested theme.  When
/// @p requested_theme is empty, the persisted @c workbench.colorTheme value
/// (or @c polyglot.dark by default) is used.  Safe to call multiple times.
void BootstrapThemeService(QApplication &app, const QString &requested_theme,
                           const QString &workspace_root = {});

/// Print all discovered themes (id, name, type, layer) to stdout.  Returns
/// the recommended process exit code.
int HandleListThemesCli();

/// Validate a single @c .polytheme.json file at @p path and emit a JSON
/// report on stdout.  Returns 0 on success, 1 on validation failure.
int HandleValidateThemeCli(const QString &path);

/// Render @p root_widget into @p out_path as a PNG.  Used by the
/// @c --headless @c --screenshot pipeline for visual smoke tests.
/// Returns the recommended process exit code.
int HandleScreenshotCli(QWidget *root_widget, const QString &out_path);

}  // namespace polyglot::tools::ui
