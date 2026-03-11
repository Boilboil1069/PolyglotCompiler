// main.cpp — PolyglotCompiler IDE entry point.
//
// Initializes the Qt application, applies the dark theme, and shows the
// main window.  Usage:
//
//   polyui [--folder PATH]
//

#include <QApplication>
#include <QDir>
#include <QPalette>
#include <QStyleFactory>

#include "tools/ui/include/main_window.h"
#include "tools/ui/include/file_browser.h"

namespace {

// Apply a VS-Code-inspired dark theme to the entire application
void ApplyDarkTheme(QApplication &app) {
    app.setStyle(QStyleFactory::create("Fusion"));

    QPalette dark_palette;
    dark_palette.setColor(QPalette::Window,          QColor(30, 30, 30));
    dark_palette.setColor(QPalette::WindowText,      QColor(204, 204, 204));
    dark_palette.setColor(QPalette::Base,             QColor(30, 30, 30));
    dark_palette.setColor(QPalette::AlternateBase,    QColor(45, 45, 45));
    dark_palette.setColor(QPalette::ToolTipBase,      QColor(45, 45, 45));
    dark_palette.setColor(QPalette::ToolTipText,      QColor(204, 204, 204));
    dark_palette.setColor(QPalette::Text,             QColor(212, 212, 212));
    dark_palette.setColor(QPalette::Button,           QColor(51, 51, 51));
    dark_palette.setColor(QPalette::ButtonText,       QColor(204, 204, 204));
    dark_palette.setColor(QPalette::BrightText,       QColor(255, 255, 255));
    dark_palette.setColor(QPalette::Link,             QColor(0, 122, 204));
    dark_palette.setColor(QPalette::Highlight,        QColor(38, 79, 120));
    dark_palette.setColor(QPalette::HighlightedText,  QColor(255, 255, 255));
    dark_palette.setColor(QPalette::PlaceholderText,  QColor(128, 128, 128));

    // Disabled palette
    dark_palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(100, 100, 100));
    dark_palette.setColor(QPalette::Disabled, QPalette::Text,       QColor(100, 100, 100));
    dark_palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(100, 100, 100));

    app.setPalette(dark_palette);

    // Global stylesheet for scrollbars and tooltips
    app.setStyleSheet(
        "QToolTip { color: #cccccc; background: #2d2d2d; border: 1px solid #555; "
        "padding: 4px; font-size: 12px; }"
        "QScrollBar:vertical { background: #1e1e1e; width: 12px; margin: 0; }"
        "QScrollBar::handle:vertical { background: #424242; min-height: 20px; "
        "border-radius: 6px; }"
        "QScrollBar::handle:vertical:hover { background: #555555; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar:horizontal { background: #1e1e1e; height: 12px; margin: 0; }"
        "QScrollBar::handle:horizontal { background: #424242; min-width: 20px; "
        "border-radius: 6px; }"
        "QScrollBar::handle:horizontal:hover { background: #555555; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }");
}

} // anonymous namespace

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("PolyglotCompiler IDE");
    app.setOrganizationName("PolyglotCompiler");
    app.setApplicationVersion("1.0.0");

    ApplyDarkTheme(app);

    polyglot::tools::ui::MainWindow window;

    // Handle --folder argument to open a project directory on startup
    for (int i = 1; i < argc; ++i) {
        if ((QString(argv[i]) == "--folder" || QString(argv[i]) == "-f") &&
            i + 1 < argc) {
            QString folder = argv[++i];
            if (QDir(folder).exists()) {
                window.findChild<polyglot::tools::ui::FileBrowser *>()
                    ? window.findChild<polyglot::tools::ui::FileBrowser *>()
                          ->SetRootPath(folder)
                    : void();
            }
        }
    }

    window.show();
    return app.exec();
}
