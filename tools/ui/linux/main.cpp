// main.cpp - Entry point for the PolyglotCompiler desktop IDE (polyui).
//
// Linux-specific entry point.  Sets up a dark theme, parses command-line
// arguments, and launches the main window.

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>

#include <cstdlib>
#include <iostream>
#include <string>

#include "tools/ui/common/include/mainwindow.h"
#include "tools/ui/common/include/file_browser.h"

namespace {

// Apply a dark colour palette suitable for an IDE.
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
    std::cout << "polyui (PolyglotCompiler IDE) v1.0.0\n"
              << "Built with Qt " << QT_VERSION_STR << " (Linux)\n";
}

}  // anonymous namespace

int main(int argc, char *argv[]) {
    // On Linux/Wayland, ensure the xcb platform is preferred for stability.
    // Users can override by setting QT_QPA_PLATFORM themselves.
    if (!std::getenv("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "xcb");
    }

    QApplication app(argc, argv);
    app.setApplicationName("PolyglotCompiler IDE");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("PolyglotCompiler");
    app.setDesktopFileName("polyui");

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

    polyglot::tools::ui::MainWindow window;
    window.setWindowTitle("PolyglotCompiler IDE");
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
