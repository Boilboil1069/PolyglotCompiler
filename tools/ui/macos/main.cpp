/**
 * @file     main.cpp
 * @brief    Entry point for the PolyglotCompiler desktop IDE (polyui)
 *
 * @ingroup  Unknown
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include <QApplication>
#include <QIcon>
#include <QPalette>
#include <QStyleFactory>

#include <iostream>
#include <string>

#include "common/include/version.h"
#include "tools/ui/common/include/mainwindow.h"
#include "tools/ui/common/include/file_browser.h"

namespace {

// Apply a dark colour palette suitable for an IDE.
// On macOS the native style respects the system dark mode, but we force
// the Fusion style for a consistent look across all platforms.
void ApplyDarkTheme(QApplication &app) {
    app.setStyle(QStyleFactory::create("Fusion"));

    QPalette p;
    p.setColor(QPalette::Window,          QColor(45, 45, 48));
    p.setColor(QPalette::WindowText,      QColor(212, 212, 212));
    p.setColor(QPalette::Base,            QColor(30, 30, 30));
    p.setColor(QPalette::AlternateBase,   QColor(45, 45, 48));
    p.setColor(QPalette::ToolTipBase,     QColor(50, 50, 52));
    p.setColor(QPalette::ToolTipText,     QColor(212, 212, 212));
    p.setColor(QPalette::Text,            QColor(212, 212, 212));
    p.setColor(QPalette::Button,          QColor(55, 55, 58));
    p.setColor(QPalette::ButtonText,      QColor(212, 212, 212));
    p.setColor(QPalette::BrightText,      QColor(255, 51, 51));
    p.setColor(QPalette::Link,            QColor(86, 156, 214));
    p.setColor(QPalette::Highlight,       QColor(38, 79, 120));
    p.setColor(QPalette::HighlightedText, QColor(255, 255, 255));

    p.setColor(QPalette::Disabled, QPalette::Text,       QColor(128, 128, 128));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(128, 128, 128));

    app.setPalette(p);

    app.setStyleSheet(
        "QToolTip { color: #d4d4d4; background-color: #2d2d30; "
        "border: 1px solid #3f3f46; }"
        "QMenu { background-color: #2d2d30; color: #d4d4d4; "
        "border: 1px solid #3f3f46; }"
        "QMenu::item:selected { background-color: #094771; }"
        "QMenuBar { background-color: #2d2d30; color: #d4d4d4; }"
        "QMenuBar::item:selected { background-color: #094771; }"
        "QTabBar::tab { background: #2d2d30; color: #d4d4d4; "
        "padding: 6px 12px; }"
        "QTabBar::tab:selected { background: #1e1e1e; "
        "border-bottom: 2px solid #569cd6; }"
        "QStatusBar { background-color: #007acc; color: white; }");
}

void PrintUsage() {
    std::cout << "Usage: polyui [options]\n"
              << "\nOptions:\n"
              << "  --folder <path>    Open a project folder on startup\n"
              << "  --version          Print version information and exit\n"
              << "  --help             Show this help message and exit\n";
}

void PrintVersion() {
    std::cout << POLYGLOT_IDE_BANNER << "\n"
              << "Built with Qt " << QT_VERSION_STR << " (macOS)\n";
}

}  // anonymous namespace

int main(int argc, char *argv[]) {
    // AA_UseHighDpiPixmaps is always enabled in Qt6 — no need to set it.

    QApplication app(argc, argv);
    app.setApplicationName(POLYGLOT_IDE_NAME);
    app.setApplicationVersion(POLYGLOT_VERSION_STRING);
    app.setOrganizationName(POLYGLOT_ORGANIZATION_NAME);
    app.setOrganizationDomain(POLYGLOT_ORGANIZATION_DOMAIN);

    // Parse command-line arguments.
    std::string initial_folder;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            PrintUsage();
            return 0;
        }
        if (arg == "--version" || arg == "-v") {
            PrintVersion();
            return 0;
        }
        if ((arg == "--folder" || arg == "-d") && i + 1 < argc) {
            initial_folder = argv[++i];
        }
    }

    ApplyDarkTheme(app);

    const QIcon app_icon(":/icons/icon.png");
    if (!app_icon.isNull()) {
        app.setWindowIcon(app_icon);
    }

    polyglot::tools::ui::MainWindow window;
    if (!app_icon.isNull()) {
        window.setWindowIcon(app_icon);
    }
    window.setWindowTitle(POLYGLOT_IDE_NAME);
    window.resize(1400, 900);

    // If a folder was given on the command line, open it in the file browser.
    if (!initial_folder.empty()) {
        auto *browser =
            window.findChild<polyglot::tools::ui::FileBrowser *>();
        if (browser) {
            browser->SetRootPath(QString::fromStdString(initial_folder));
        }
    }

    window.show();
    return app.exec();
}
