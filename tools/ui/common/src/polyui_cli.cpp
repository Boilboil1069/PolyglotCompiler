/**
 * @file     polyui_cli.cpp
 * @brief    Shared CLI / theme bootstrap implementation for the three
 *           platform polyui main executables.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 */
#include "tools/ui/common/include/polyui_cli.h"

#include <QApplication>
#include <QColor>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QPalette>
#include <QPixmap>
#include <QStyleFactory>
#include <QWidget>
#include <iostream>

#include "common/include/version.h"
#include "tools/ui/common/include/settings_service.h"
#include "tools/ui/common/include/theme_service.h"

namespace polyglot::tools::ui {

PolyUiCliOptions ParsePolyUiArgs(int argc, char *argv[]) {
  PolyUiCliOptions o;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--help" || a == "-h") {
      o.show_help = true;
    } else if (a == "--version" || a == "-v") {
      o.show_version = true;
    } else if ((a == "--folder" || a == "-d") && i + 1 < argc) {
      o.initial_folder = argv[++i];
    } else if (a == "--theme" && i + 1 < argc) {
      o.theme = QString::fromLocal8Bit(argv[++i]);
    } else if (a == "--list-themes") {
      o.list_themes = true;
    } else if (a == "--validate-theme" && i + 1 < argc) {
      o.validate_theme = QString::fromLocal8Bit(argv[++i]);
    } else if (a == "--headless") {
      o.headless = true;
    } else if (a == "--screenshot" && i + 1 < argc) {
      o.screenshot = QString::fromLocal8Bit(argv[++i]);
    }
  }
  return o;
}

void PrintPolyUiUsage() {
  std::cout << "Usage: polyui [options]\n"
            << "\n"
            << "General options:\n"
            << "  --folder <path>            Open a project folder on startup\n"
            << "  --version, -v              Print version information and exit\n"
            << "  --help, -h                 Show this help message and exit\n"
            << "\n"
            << "Theme system:\n"
            << "  --theme <id|path>          Activate the given theme by id\n"
            << "                             or by .polytheme.json file path\n"
            << "  --list-themes              Print every discovered theme to stdout\n"
            << "                             (id, name, type, layer) and exit\n"
            << "  --validate-theme <path>    Validate a .polytheme.json file and\n"
            << "                             print a JSON diagnostic report; exit\n"
            << "                             code 0 if valid, 1 otherwise\n"
            << "  --headless                 Use the offscreen QPA platform; useful\n"
            << "                             together with --screenshot in CI\n"
            << "  --screenshot <out.png>     Render the main window once and write\n"
            << "                             a PNG of its current state to <out>\n";
}

void PrintPolyUiVersion(const char *platform_suffix) {
  std::cout << POLYGLOT_IDE_BANNER << "\n"
            << "Built with Qt " << QT_VERSION_STR;
  if (platform_suffix && *platform_suffix) std::cout << " (" << platform_suffix << ")";
  std::cout << "\n";
}

void ApplyFallbackDarkPalette(QApplication &app) {
  // Style is set unconditionally so all platforms render with the same
  // baseline before any QSS arrives from the theme files.
  app.setStyle(QStyleFactory::create("Fusion"));

  QPalette p;
  p.setColor(QPalette::Window,           QColor(45, 45, 48));
  p.setColor(QPalette::WindowText,       QColor(212, 212, 212));
  p.setColor(QPalette::Base,             QColor(30, 30, 30));
  p.setColor(QPalette::AlternateBase,    QColor(45, 45, 48));
  p.setColor(QPalette::ToolTipBase,      QColor(50, 50, 52));
  p.setColor(QPalette::ToolTipText,      QColor(212, 212, 212));
  p.setColor(QPalette::Text,             QColor(212, 212, 212));
  p.setColor(QPalette::Button,           QColor(55, 55, 58));
  p.setColor(QPalette::ButtonText,       QColor(212, 212, 212));
  p.setColor(QPalette::BrightText,       QColor(255, 51, 51));
  p.setColor(QPalette::Link,             QColor(86, 156, 214));
  p.setColor(QPalette::Highlight,        QColor(38, 79, 120));
  p.setColor(QPalette::HighlightedText,  QColor(255, 255, 255));
  p.setColor(QPalette::Disabled, QPalette::Text,       QColor(128, 128, 128));
  p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(128, 128, 128));
  app.setPalette(p);
}

void BootstrapThemeService(QApplication & /*app*/, const QString &requested_theme,
                           const QString &workspace_root) {
  ThemeService &svc = ThemeService::Instance();
  if (!workspace_root.isEmpty()) svc.SetWorkspaceRoot(workspace_root);
  svc.Scan();

  // Resolution order:
  //   1. --theme on the command line (id or absolute file path)
  //   2. workbench.colorTheme persisted in user settings
  //   3. polyglot.dark default built-in
  QString id = requested_theme;
  if (!id.isEmpty() && QFileInfo::exists(id)) {
    // A path was passed: install (copy into user dir) so it has a stable id.
    QString err;
    const QString installed = svc.InstallFromFile(id, &err);
    if (!installed.isEmpty()) {
      svc.Scan();
      // The freshly installed file's id is parsed during Scan; locate it by
      // matching source_path.
      for (const auto &m : svc.Themes()) {
        if (QFileInfo(m.source_path) == QFileInfo(installed)) {
          id = m.id;
          break;
        }
      }
    } else {
      std::cerr << "polyui: failed to install --theme file: "
                << err.toStdString() << "\n";
      id.clear();
    }
  }
  if (id.isEmpty()) {
    id = SettingsService::Instance().GetString("workbench.colorTheme");
  }
  if (id.isEmpty()) {
    id = QStringLiteral("polyglot.dark");
  }
  if (!svc.Activate(id)) {
    // Last-ditch fallback so the user always lands on something.
    svc.Activate(QStringLiteral("polyglot.dark"));
  }
}

int HandleListThemesCli() {
  ThemeService &svc = ThemeService::Instance();
  svc.Scan();
  std::cout << "id\tname\ttype\tlayer\tsource\n";
  for (const auto &m : svc.Themes()) {
    std::cout << m.id.toStdString()      << "\t"
              << m.name.toStdString()    << "\t"
              << m.type.toStdString()    << "\t"
              << m.layer.toStdString()   << "\t"
              << m.source_path.toStdString() << "\n";
  }
  return 0;
}

int HandleValidateThemeCli(const QString &path) {
  ThemeService &svc = ThemeService::Instance();
  QStringList errs;
  const bool ok = svc.ValidateFile(path, &errs);

  // Emit a structured JSON report so CI / IDE wrappers can parse it.
  QJsonObject report;
  report.insert("file", path);
  report.insert("valid", ok);
  QJsonArray earr;
  for (const QString &e : errs) earr.append(e);
  report.insert("errors", earr);
  std::cout << QJsonDocument(report).toJson(QJsonDocument::Indented).toStdString();
  return ok ? 0 : 1;
}

int HandleScreenshotCli(QWidget *root_widget, const QString &out_path) {
  if (!root_widget || out_path.isEmpty()) return 2;
  // Flush any pending events so the widget reflects its final state.
  QApplication::processEvents();
  const QPixmap pix = root_widget->grab();
  if (pix.isNull()) {
    std::cerr << "polyui: --screenshot grab returned a null pixmap\n";
    return 3;
  }
  if (!pix.save(out_path)) {
    std::cerr << "polyui: --screenshot failed to write " << out_path.toStdString() << "\n";
    return 4;
  }
  return 0;
}

}  // namespace polyglot::tools::ui
