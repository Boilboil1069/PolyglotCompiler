/**
 * @file     main.cpp
 * @brief    Entry point for the PolyglotCompiler desktop IDE (polyui),
 *           Linux build.  See tools/ui/windows/main.cpp for shared
 *           bootstrap explanation.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include <QApplication>
#include <QIcon>
#include <cstdlib>
#include <cstring>
#include <string>

#include "common/include/version.h"
#include "tools/ui/common/include/file_browser.h"
#include "tools/ui/common/include/mainwindow.h"
#include "tools/ui/common/include/polyui_cli.h"

int main(int argc, char *argv[]) {
  using namespace polyglot::tools::ui;

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--headless") == 0) {
      qputenv("QT_QPA_PLATFORM", "offscreen");
      break;
    }
  }
  if (!std::getenv("QT_QPA_PLATFORM")) {
    qputenv("QT_QPA_PLATFORM", "xcb");
  }

  QApplication app(argc, argv);
  app.setApplicationName(POLYGLOT_IDE_NAME);
  app.setApplicationVersion(POLYGLOT_VERSION_STRING);
  app.setOrganizationName(POLYGLOT_ORGANIZATION_NAME);
  app.setDesktopFileName(POLYGLOT_POLYUI_NAME);

  const PolyUiCliOptions opts = ParsePolyUiArgs(argc, argv);
  if (opts.show_help)    { PrintPolyUiUsage(); return 0; }
  if (opts.show_version) { PrintPolyUiVersion("Linux"); return 0; }
  if (opts.list_themes)  { return HandleListThemesCli(); }
  if (!opts.validate_theme.isEmpty()) {
    return HandleValidateThemeCli(opts.validate_theme);
  }

  ApplyFallbackDarkPalette(app);
  BootstrapThemeService(app, opts.theme);

  const QIcon app_icon(":/icons/icon.png");
  if (!app_icon.isNull()) app.setWindowIcon(app_icon);

  MainWindow window;
  if (!app_icon.isNull()) window.setWindowIcon(app_icon);
  window.setWindowTitle(POLYGLOT_IDE_NAME);
  window.resize(1400, 900);

  if (!opts.initial_folder.empty()) {
    if (auto *browser = window.findChild<FileBrowser *>()) {
      browser->SetRootPath(QString::fromStdString(opts.initial_folder));
    }
  }

  window.show();
  if (opts.headless && !opts.screenshot.isEmpty()) {
    return HandleScreenshotCli(&window, opts.screenshot);
  }
  return app.exec();
}