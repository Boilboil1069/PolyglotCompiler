/**
 * @file     polyui_cli.cpp
 * @brief    Shared CLI / theme bootstrap implementation for the three
 *           platform polyui main executables.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 */
#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QContextMenuEvent>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QGraphicsTextItem>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QPalette>
#include <QPixmap>
#include <QSettings>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTimer>
#include <QTreeView>
#include <QWidget>
#include <iostream>
#include <memory>

#include "common/include/version.h"
#include "tools/ui/common/include/code_editor.h"
#include "tools/ui/common/include/file_browser.h"
#include "tools/ui/common/include/mainwindow.h"
#include "tools/ui/common/include/native_trace_panel.h"
#include "tools/ui/common/include/polyui_cli.h"
#include "tools/ui/common/include/settings_service.h"
#include "tools/ui/common/include/theme_service.h"
#include "tools/ui/common/include/topology_panel.h"

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
    } else if (a == "--file" && i + 1 < argc) {
      o.initial_file = QString::fromLocal8Bit(argv[++i]);
    } else if (a == "--view" && i + 1 < argc) {
      o.workspace_view = QString::fromLocal8Bit(argv[++i]);
    } else if (a == "--peek" && i + 1 < argc) {
      o.peek_symbol = QString::fromLocal8Bit(argv[++i]);
    } else if (a == "--ui-trace-smoke") {
      o.ui_trace_smoke = true;
    } else if (a == "--run-args" && i + 1 < argc) {
      o.run_arguments = QString::fromLocal8Bit(argv[++i]);
    } else if (a == "--trace-file" && i + 1 < argc) {
      o.trace_file = QString::fromLocal8Bit(argv[++i]);
    } else if (a == "--ui-smoke") {
      o.ui_smoke = true;
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
            << "  --file <path>              Open a source document on startup\n"
            << "  --view <code|split|flow>    Select the workspace layout\n"
            << "  --peek <symbol>            Preview a function in the source editor\n"
            << "  --ui-smoke                 Verify inline docs and right-click navigation\n"
            << "                             (requires --headless --file --peek)\n"
            << "  --ui-trace-smoke           Verify real Compile & Run trace, source and ports\n"
            << "                             (requires --headless --file; program must exit 0)\n"
            << "  --run-args <arguments>     Program arguments for --ui-trace-smoke\n"
            << "  --trace-file <path>        Inspect an existing JSONL session instead of running\n"
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
#ifdef Q_OS_MAC
  // Offscreen Qt has no native menu font; use a real installed UI family.
  app.setFont(QFont(QStringLiteral("Helvetica Neue"), 11));
#endif

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
#ifdef Q_OS_WIN
  // Fresh Windows build directories do not have ui-validation/ yet.
  if (!QDir().mkpath(QFileInfo(out_path).absolutePath())) return 4;
#endif
  // File-system models settle asynchronously and external volumes can take
  // seconds to enumerate. Wait for actual rows, rather than recording an empty
  // explorer merely because a fixed startup delay elapsed.
  QList<QFileSystemModel *> pending_models;
  for (auto *tree : root_widget->findChildren<QTreeView *>()) {
    auto *model = qobject_cast<QFileSystemModel *>(tree->model());
    // An unopened explorer has an invalid view root but its model reports ".".
    // There is no visible directory to await in --file-only workspaces.
    if (model && tree->isVisible() && tree->rootIndex().isValid() && !model->rootPath().isEmpty() &&
        !QDir(model->rootPath()).entryList(model->nameFilters(), model->filter()).isEmpty() &&
        model->rowCount(model->index(model->rootPath())) == 0)
      pending_models.push_back(model);
  }
  QEventLoop settle;
  QTimer poll;
  QObject::connect(&poll, &QTimer::timeout, &settle, [&]() {
    for (auto *model : pending_models)
      if (model->rowCount(model->index(model->rootPath())) == 0) return;
    settle.quit();
  });
  poll.start(25);
  QTimer::singleShot(5000, &settle, &QEventLoop::quit);
  settle.exec();
  for (auto *model : pending_models) {
    if (model->rowCount(model->index(model->rootPath())) == 0) {
      std::cerr << "polyui: file explorer did not load before screenshot timeout\n";
      return 5;
    }
  }
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


void PrepareWorkspaceCli(QApplication &app, const PolyUiCliOptions &options) {
  app.setProperty("polyui.headless", options.headless);
  if (!options.headless) return;
  // GUI tests must neither depend on nor overwrite the user's saved layout.
  static auto isolated_settings = std::make_unique<QTemporaryDir>();
  app.setProperty("polyui.settings_file", isolated_settings->filePath(QStringLiteral("ide.ini")));
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, isolated_settings->path());
  QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, isolated_settings->path());
}

void ApplyWorkspaceCli(MainWindow *window, const PolyUiCliOptions &options) {
  if (!window) return;
  if (!options.initial_file.isEmpty()) window->OpenWorkspaceFile(options.initial_file);
  if (!options.workspace_view.isEmpty()) window->SetWorkspaceView(options.workspace_view);
  if (!options.peek_symbol.isEmpty()) window->PreviewSymbol(options.peek_symbol);
}

int RunWorkspaceSmoke(MainWindow *window, const PolyUiCliOptions &options) {
  if (!window || !options.headless || options.initial_file.isEmpty() || options.peek_symbol.isEmpty()) {
    std::cerr << "polyui: --ui-smoke requires --headless --file and --peek\n";
    return 2;
  }
  QApplication::processEvents();
  CodeEditor *editor = nullptr;
  for (auto *candidate : window->findChildren<CodeEditor *>()) {
    if (QFileInfo(candidate->FilePath()).absoluteFilePath() == QFileInfo(options.initial_file).absoluteFilePath())
      editor = candidate;
  }
  if (!editor || !editor->InlineDefinitionVisible()) {
    std::cerr << "polyui smoke: inline definition is not visible\n"; return 3;
  }
  const auto definitions = editor->InspectAtCursor(editor->textCursor());
  if (definitions.size() != 1 || definitions.front().documentation.empty()) {
    std::cerr << "polyui smoke: expected one documented source definition\n"; return 4;
  }
  auto *documentation = editor->findChild<QTextBrowser *>("foreignDocumentation");
  auto *preview = editor->findChild<QPlainTextEdit *>("foreignSourcePreview");
  if (!documentation || !preview || !preview->isReadOnly() ||
      !documentation->toPlainText().contains(QString::fromStdString(definitions.front().documentation)) ||
      !preview->toPlainText().contains(QString::fromStdString(definitions.front().name))) {
    std::cerr << "polyui smoke: source documentation / preview mismatch\n"; return 5;
  }
  if (!options.screenshot.isEmpty() && HandleScreenshotCli(window, options.screenshot) != 0) return 6;
#ifdef Q_OS_WIN
  // Opening a Poly file must also leave it visible in the Windows explorer.
  auto *browser = window->findChild<FileBrowser *>();
  auto *tree = browser ? browser->findChild<QTreeView *>() : nullptr;
  auto *model = tree ? qobject_cast<QFileSystemModel *>(tree->model()) : nullptr;
  if (!model) {
    std::cerr << "polyui smoke: Windows explorer is unavailable\n";
    return 7;
  }
  if (QFileInfo(options.initial_file).absolutePath() == model->rootPath()) {
    const QModelIndex root = tree->rootIndex();
    bool found = false;
    for (int row = 0; row < model->rowCount(root); ++row) {
      if (QFileInfo(model->filePath(model->index(row, 0, root))).absoluteFilePath() ==
          QFileInfo(options.initial_file).absoluteFilePath()) {
        found = true;
        break;
      }
    }
    if (!found) {
      std::cerr << "polyui smoke: Poly file is missing from Windows explorer\n";
      return 7;
    }
  }
#endif
  bool action_triggered = false;
  QTimer::singleShot(0, window, [&]() {
    auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
    if (!menu) return;
    if (!options.screenshot.isEmpty()) menu->grab().save(options.screenshot + ".context.png");
    for (auto *action : menu->actions()) {
      if (action->objectName() == "goToDefinitionAction" && action->isEnabled()) {
        action->trigger(); action_triggered = true; break;
      }
    }
    menu->close();
  });
  // A bounded fallback prevents a platform popup failure from hanging CI.
  QTimer::singleShot(2000, window, []() {
    if (auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget())) menu->close();
  });
  const auto point = editor->cursorRect().center();
  QContextMenuEvent context(QContextMenuEvent::Mouse, point, editor->viewport()->mapToGlobal(point));
  QApplication::sendEvent(editor->viewport(), &context);
  QApplication::processEvents();
  bool navigated = false;
  const auto &expected = definitions.front().location;
  for (auto *candidate : window->findChildren<CodeEditor *>()) {
    if (candidate->FilePath() == QString::fromStdString(expected.file) && candidate->isVisible() &&
        candidate->textCursor().blockNumber() == expected.line - 1 &&
        candidate->textCursor().positionInBlock() == expected.column - 1) navigated = true;
  }
  if (!action_triggered || !navigated) {
    std::cerr << "polyui smoke: context-menu definition navigation failed\n"; return 7;
  }
  QJsonObject report{{"inlineDocumentation", true}, {"sourcePreview", true},
                     {"contextMenuDefinition", true}, {"target", QString::fromStdString(expected.file)},
                     {"line", expected.line}, {"column", expected.column}};
  std::cout << QJsonDocument(report).toJson(QJsonDocument::Compact).toStdString() << '\n';
  return 0;
}

int RunRuntimeTraceSmoke(MainWindow *window, const PolyUiCliOptions &options) {
  if (!window || !options.headless || options.initial_file.isEmpty()) {
    std::cerr << "polyui: --ui-trace-smoke requires --headless --file\n";
    return 2;
  }
  auto *topology = window->findChild<TopologyPanel *>();
  if (!topology)
    return 3;
  QApplication::processEvents();
  if (options.trace_file.isEmpty()) {
    window->RunWorkspaceDocument(true, options.run_arguments);
    QElapsedTimer timeout;
    timeout.start();
    while (window->IsProgramRunning() && timeout.elapsed() < 60000)
      QApplication::processEvents(QEventLoop::AllEvents, 20);
    if (window->IsProgramRunning() || window->LastRunExitCode() != 0) {
      std::cerr << "polyui trace smoke: native program failed or timed out; exit="
                << window->LastRunExitCode() << '\n';
      return 4;
    }
  } else {
    window->SetWorkspaceView("split");
    topology->LoadFromFile(options.initial_file);
    if (!topology->BeginTraceSession(options.trace_file, false))
      return 5;
  }
  auto *panel = topology->TracePanel();
  if (!panel || !panel->Error().isEmpty() || panel->Trace().Samples().empty()) {
    std::cerr << "polyui trace smoke: missing or invalid actual samples: "
              << (panel ? panel->Error().toStdString() : "panel missing") << '\n';
    return 6;
  }
  std::size_t mapped = 0;
  for (const auto &[id, site] : panel->Trace().Sites()) {
    Q_UNUSED(id);
    QString qualified = QString::fromStdString(site.callee);
    const auto prefix = QString::fromStdString(site.language) + "::";
    if (!qualified.startsWith(prefix))
      qualified.prepend(prefix);
    int matches = 0;
    for (const auto &[node_id, node] : topology->NodeItems()) {
      Q_UNUSED(node_id);
      if (QFileInfo(node->CallFile()).canonicalFilePath() ==
              QFileInfo(QString::fromStdString(site.file)).canonicalFilePath() &&
          node->CallLine() == site.line && node->CallColumn() == site.column &&
          node->NodeName() == qualified)
        ++matches;
    }
    if (matches == 1)
      ++mapped;
  }
  if (mapped != panel->Trace().Sites().size()) {
    std::cerr << "polyui trace smoke: only " << mapped << '/' << panel->Trace().Sites().size()
              << " compiler call sites map to exactly one static card\n";
    return 7;
  }
  auto *toggle = panel->findChild<QCheckBox *>("runtimeOverlayToggle");
  if (!toggle)
    return 8;
  toggle->setChecked(false);
  bool static_restored = true;
  for (const auto &[id, node] : topology->NodeItems()) {
    Q_UNUSED(id);
    for (auto *port : node->InputPorts())
      static_restored &= port->RuntimeValue().isEmpty();
    for (auto *port : node->OutputPorts())
      static_restored &= port->RuntimeValue().isEmpty();
  }
  toggle->setChecked(true);
  panel->SelectSample(0);
  QApplication::processEvents();
  const auto *sample = panel->SelectedSample();
  const auto *site = sample ? panel->Trace().Site(sample->site_id) : nullptr;
  if (!sample || !site)
    return 9;
  bool source_highlighted = false, ports_have_values = false;
  for (auto *editor : window->findChildren<CodeEditor *>()) {
    if (editor->isVisible() && editor->RuntimeSampleLine() == site->line &&
        QFileInfo(editor->FilePath()).canonicalFilePath() ==
            QFileInfo(QString::fromStdString(site->file)).canonicalFilePath())
      source_highlighted = true;
  }
  for (const auto &[id, node] : topology->NodeItems()) {
    Q_UNUSED(id);
    if (node->CallLine() != site->line || node->CallColumn() != site->column)
      continue;
    for (auto *port : node->OutputPorts())
      ports_have_values |= !port->RuntimeValue().isEmpty();
  }
  if (!static_restored || !source_highlighted || !ports_have_values) {
    std::cerr << "polyui trace smoke: source/port association or overlay toggle failed\n";
    return 10;
  }
  if (!options.screenshot.isEmpty() && HandleScreenshotCli(window, options.screenshot) != 0)
    return 11;
  QJsonObject report{{"realRuntimeSamples", true},
                     {"compiledAndRan", options.trace_file.isEmpty()},
                     {"exitCode", options.trace_file.isEmpty()
                                      ? QJsonValue(window->LastRunExitCode())
                                      : QJsonValue(QJsonValue::Null)},
                     {"sampleCount", static_cast<qint64>(panel->Trace().Samples().size())},
                     {"mappedCallSites", static_cast<qint64>(mapped)},
                     {"sourceHighlight", source_highlighted},
                     {"portValues", ports_have_values},
                     {"overlayOffRestoresStatic", static_restored},
                     {"instanceId", QString::fromStdString(sample->instance_id)},
                     {"target", QString::fromStdString(site->file)},
                     {"line", site->line},
                     {"column", site->column}};
  std::cout << QJsonDocument(report).toJson(QJsonDocument::Compact).toStdString() << '\n';
  return 0;
}

}  // namespace polyglot::tools::ui
