/**
 * @file     mainwindow.cpp
 * @brief    Main window implementation for the PolyglotCompiler IDE
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include "tools/ui/common/include/mainwindow.h"
#include "tools/ui/common/include/action_manager.h"
#include "tools/ui/common/include/build_panel.h"
#include "tools/ui/common/include/code_editor.h"
#include "tools/ui/common/include/compiler_service.h"
#include "tools/ui/common/include/debug_panel.h"
#include "tools/ui/common/include/file_browser.h"
#include "tools/ui/common/include/git_panel.h"
#include "tools/ui/common/include/output_panel.h"
#include "tools/ui/common/include/panel_manager.h"
#include "tools/ui/common/include/settings_dialog.h"
#include "tools/ui/common/include/syntax_highlighter.h"
#include "tools/ui/common/include/terminal_widget.h"
#include "tools/ui/common/include/theme_manager.h"
#include "tools/ui/common/include/topology_panel.h"

#include "common/include/plugins/plugin_manager.h"
#include "common/include/version.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QInputDialog>
#include <QMessageBox>
#include <QPalette>
#include <QSettings>
#include <QShortcut>
#include <QStyle>
#include <QTabBar>
#include <QTextBlock>
#include <QTextStream>
#include <QTimer>
#include <QToolTip>

namespace polyglot::tools::ui {

// ============================================================================
// Construction / Destruction
// ============================================================================

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle(POLYGLOT_IDE_NAME);
    resize(1400, 900);

    compiler_service_ = std::make_unique<CompilerService>();
    action_manager_ = new ActionManager(this);

    SetupCentralWidget();
    SetupDockWidgets();
    SetupMenuBar();
    SetupToolBar();
    SetupStatusBar();
    SetupConnections();
    SetupShortcuts();
    SetupAnalysisTimer();

    RestoreState();
    ApplySettings();

    // Initialize the plugin system: set default search paths and discover
    // plugins from the application's "plugins" directory.
    InitializePlugins();

    // Create an initial empty tab using the configured default language.
    static const QStringList lang_ids = {"ploy", "cpp", "python", "rust", "java", "csharp"};
    int lang_index = language_combo_ ? language_combo_->currentIndex() : 0;
    if (lang_index < 0 || lang_index >= lang_ids.size()) {
        lang_index = 0;
    }
    const QString initial_language = lang_ids[lang_index];
    CreateNewTab(
        QString("Untitled-%1.%2")
            .arg(next_untitled_id_++)
            .arg(LanguageToExtension(initial_language)),
        initial_language);
}

MainWindow::~MainWindow() {
    SaveState();
}

// ============================================================================
// Central Widget — tabbed editor
// ============================================================================

void MainWindow::SetupCentralWidget() {
    // Main layout: file browser | (editor / bottom panels)
    main_splitter_ = new QSplitter(Qt::Horizontal, this);

    // Right side: editor on top, bottom panels (output + terminal) on bottom
    vertical_splitter_ = new QSplitter(Qt::Vertical);

    editor_tabs_ = new QTabWidget();
    editor_tabs_->setTabsClosable(true);
    editor_tabs_->setMovable(true);
    editor_tabs_->setDocumentMode(true);
    editor_tabs_->setStyleSheet(
        ThemeManager::Instance().TabWidgetStylesheet(false));

    vertical_splitter_->addWidget(editor_tabs_);

    // Bottom tab container: holds Output panel and Terminal tabs
    bottom_tabs_ = new QTabWidget();
    bottom_tabs_->setTabPosition(QTabWidget::South);
    bottom_tabs_->setStyleSheet(
        ThemeManager::Instance().TabWidgetStylesheet(true));

    // Terminal tabs widget (supports multiple terminal instances)
    terminal_tabs_ = new QTabWidget();
    terminal_tabs_->setTabsClosable(true);
    terminal_tabs_->setMovable(true);
    terminal_tabs_->setStyleSheet(
        ThemeManager::Instance().TabWidgetStylesheet(false));
    connect(terminal_tabs_, &QTabWidget::tabCloseRequested,
            this, &MainWindow::CloseTerminalTab);

    vertical_splitter_->addWidget(bottom_tabs_);

    // Set initial sizes — will be adjusted in SetupDockWidgets
    main_splitter_->addWidget(vertical_splitter_);

    // NOTE: initial sizes are set in SetupDockWidgets() after file_browser_
    // is inserted, so that both children exist when setSizes() is called.

    setCentralWidget(main_splitter_);
}

// ============================================================================
// Dock Widgets — file browser + output panel
// ============================================================================

void MainWindow::SetupDockWidgets() {
    // File browser on the left
    file_browser_ = new FileBrowser();
    main_splitter_->insertWidget(0, file_browser_);

    // Now that both children exist (file_browser_ at 0, vertical_splitter_ at 1),
    // set the initial sizes so the explorer panel is always visible on first launch.
    main_splitter_->setSizes({250, 1150});
    file_browser_->setMinimumWidth(160);

    // Output panel as a tab in the bottom panel
    output_panel_ = new OutputPanel();

    // Terminal tabs as a tab in the bottom panel
    // (terminal_tabs_ is already created in SetupCentralWidget)

    // Git panel
    git_panel_ = new GitPanel();

    // Build panel
    build_panel_ = new BuildPanel();

    // Debug panel
    debug_panel_ = new DebugPanel();

    // Topology panel
    topology_panel_ = new TopologyPanel();

    // Create PanelManager to manage bottom panel tabs
    panel_manager_ = new PanelManager(bottom_tabs_, this);
    panel_manager_->RegisterPanel("output",   output_panel_,  "Output");
    panel_manager_->RegisterPanel("terminal", terminal_tabs_,  "Terminal");
    panel_manager_->RegisterPanel("git",      git_panel_,      "Git");
    panel_manager_->RegisterPanel("build",    build_panel_,    "Build");
    panel_manager_->RegisterPanel("debug",    debug_panel_,    "Debug");
    panel_manager_->RegisterPanel("topology", topology_panel_, "Topology");

    // Settings dialog (lazily shown, but create now for signal wiring)
    settings_dialog_ = new SettingsDialog(this);

    // Create the first terminal instance
    NewTerminal();

    // Set initial vertical sizes (editor : bottom panels)
    vertical_splitter_->setSizes({650, 250});
}

// ============================================================================
// Menu Bar
// ============================================================================

void MainWindow::SetupMenuBar() {
    QMenuBar *mb = menuBar();
    mb->setStyleSheet(ThemeManager::Instance().MenuBarStylesheet());

    // ── File Menu ─────────────────────────────────────────────────────
    file_menu_ = mb->addMenu("&File");

    action_new_ = file_menu_->addAction("&New File");
    action_new_->setShortcut(QKeySequence::New);

    action_new_from_template_ = file_menu_->addAction("New From &Template...");
    action_new_from_template_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N));

    action_open_ = file_menu_->addAction("&Open File...");
    action_open_->setShortcut(QKeySequence::Open);

    action_open_folder_ = file_menu_->addAction("Open &Folder...");
    action_open_folder_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_K));

    file_menu_->addSeparator();

    action_save_ = file_menu_->addAction("&Save");
    action_save_->setShortcut(QKeySequence::Save);

    action_save_as_ = file_menu_->addAction("Save &As...");
    action_save_as_->setShortcut(QKeySequence::SaveAs);

    action_save_all_ = file_menu_->addAction("Save A&ll");
    action_save_all_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));

    file_menu_->addSeparator();

    action_close_tab_ = file_menu_->addAction("&Close Tab");
    action_close_tab_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_W));

    file_menu_->addSeparator();

    action_settings_ = file_menu_->addAction("&Settings...");
    action_settings_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma));

    file_menu_->addSeparator();

    action_quit_ = file_menu_->addAction("&Quit");
    action_quit_->setShortcut(QKeySequence::Quit);

    // ── Edit Menu ─────────────────────────────────────────────────────
    edit_menu_ = mb->addMenu("&Edit");

    action_undo_ = edit_menu_->addAction("&Undo");
    action_undo_->setShortcut(QKeySequence::Undo);

    action_redo_ = edit_menu_->addAction("&Redo");
    action_redo_->setShortcut(QKeySequence::Redo);

    edit_menu_->addSeparator();

    action_cut_ = edit_menu_->addAction("Cu&t");
    action_cut_->setShortcut(QKeySequence::Cut);

    action_copy_ = edit_menu_->addAction("&Copy");
    action_copy_->setShortcut(QKeySequence::Copy);

    action_paste_ = edit_menu_->addAction("&Paste");
    action_paste_->setShortcut(QKeySequence::Paste);

    action_select_all_ = edit_menu_->addAction("Select &All");
    action_select_all_->setShortcut(QKeySequence::SelectAll);

    edit_menu_->addSeparator();

    action_find_ = edit_menu_->addAction("&Find...");
    action_find_->setShortcut(QKeySequence::Find);

    action_replace_ = edit_menu_->addAction("&Replace...");
    action_replace_->setShortcut(QKeySequence::Replace);

    action_goto_line_ = edit_menu_->addAction("&Go to Line...");
    action_goto_line_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_G));

    // ── View Menu ─────────────────────────────────────────────────────
    view_menu_ = mb->addMenu("&View");

    action_toggle_browser_ = view_menu_->addAction("Toggle &Explorer");
    action_toggle_browser_->setCheckable(true);
    action_toggle_browser_->setChecked(true);
    action_toggle_browser_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_B));

    action_toggle_output_ = view_menu_->addAction("Toggle &Output Panel");
    action_toggle_output_->setCheckable(true);
    action_toggle_output_->setChecked(true);
    action_toggle_output_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_J));

    action_toggle_toolbar_ = view_menu_->addAction("Toggle &Toolbar");
    action_toggle_toolbar_->setCheckable(true);
    action_toggle_toolbar_->setChecked(true);

    action_toggle_statusbar_ = view_menu_->addAction("Toggle &Status Bar");
    action_toggle_statusbar_->setCheckable(true);
    action_toggle_statusbar_->setChecked(true);

    view_menu_->addSeparator();

    action_toggle_linenumbers_ = view_menu_->addAction("Toggle &Line Numbers");
    action_toggle_linenumbers_->setCheckable(true);
    action_toggle_linenumbers_->setChecked(true);

    action_toggle_wordwrap_ = view_menu_->addAction("Toggle &Word Wrap");
    action_toggle_wordwrap_->setCheckable(true);
    action_toggle_wordwrap_->setChecked(false);

    view_menu_->addSeparator();

    action_zoom_in_ = view_menu_->addAction("Zoom &In");
    action_zoom_in_->setShortcut(QKeySequence::ZoomIn);

    action_zoom_out_ = view_menu_->addAction("Zoom &Out");
    action_zoom_out_->setShortcut(QKeySequence::ZoomOut);

    action_zoom_reset_ = view_menu_->addAction("Zoom &Reset");
    action_zoom_reset_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));

    view_menu_->addSeparator();

    action_toggle_git_ = view_menu_->addAction("Toggle &Git Panel");
    action_toggle_git_->setCheckable(true);
    action_toggle_git_->setChecked(false);
    action_toggle_git_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G));

    action_toggle_build_ = view_menu_->addAction("Toggle B&uild Panel");
    action_toggle_build_->setCheckable(true);
    action_toggle_build_->setChecked(false);

    action_toggle_debug_ = view_menu_->addAction("Toggle &Debug Panel");
    action_toggle_debug_->setCheckable(true);
    action_toggle_debug_->setChecked(false);

    action_toggle_topology_ = view_menu_->addAction("Toggle &Topology Panel");
    action_toggle_topology_->setCheckable(true);
    action_toggle_topology_->setChecked(false);
    action_toggle_topology_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T));

    // ── Build Menu ────────────────────────────────────────────────────
    build_menu_ = mb->addMenu("&Build");

    action_compile_ = build_menu_->addAction("&Compile");
    action_compile_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B));

    action_compile_run_ = build_menu_->addAction("Compile && &Run");
    action_compile_run_->setShortcut(QKeySequence(Qt::Key_F5));

    action_analyze_ = build_menu_->addAction("&Analyze");
    action_analyze_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A));

    build_menu_->addSeparator();

    action_stop_ = build_menu_->addAction("&Stop");
    action_stop_->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F5));
    action_stop_->setEnabled(false);

    build_menu_->addSeparator();

    action_cmake_configure_ = build_menu_->addAction("CMake: Co&nfigure");
    action_cmake_build_ = build_menu_->addAction("CMake: &Build");

    // ── Debug Menu ────────────────────────────────────────────────────
    debug_menu_ = mb->addMenu("&Debug");

    action_debug_start_ = debug_menu_->addAction("Start &Debugging");
    action_debug_start_->setShortcut(QKeySequence(Qt::Key_F9));

    action_debug_stop_ = debug_menu_->addAction("S&top Debugging");

    debug_menu_->addSeparator();

    action_debug_step_over_ = debug_menu_->addAction("Step &Over");
    action_debug_step_over_->setShortcut(QKeySequence(Qt::Key_F10));

    action_debug_step_into_ = debug_menu_->addAction("Step &Into");
    action_debug_step_into_->setShortcut(QKeySequence(Qt::Key_F11));

    action_debug_step_out_ = debug_menu_->addAction("Step Ou&t");
    action_debug_step_out_->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F11));

    // ── Terminal Menu ─────────────────────────────────────────────────
    terminal_menu_ = mb->addMenu("&Terminal");

    action_new_terminal_ = terminal_menu_->addAction("&New Terminal");
    action_new_terminal_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_QuoteLeft));

    action_toggle_terminal_ = terminal_menu_->addAction("&Toggle Terminal");
    action_toggle_terminal_->setCheckable(true);
    action_toggle_terminal_->setChecked(true);
    action_toggle_terminal_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_QuoteLeft));

    terminal_menu_->addSeparator();

    action_clear_terminal_ = terminal_menu_->addAction("&Clear Terminal");

    action_restart_terminal_ = terminal_menu_->addAction("&Restart Terminal");

    // ── Help Menu ─────────────────────────────────────────────────────
    help_menu_ = mb->addMenu("&Help");
    help_menu_->addAction("&About " POLYGLOT_IDE_NAME, this, &MainWindow::ShowAbout);
    help_menu_->addAction("About &Qt", this, &MainWindow::ShowAboutQt);
    help_menu_->addSeparator();
    help_menu_->addAction("Keyboard &Shortcuts", this, &MainWindow::ShowShortcuts);
}

// ============================================================================
// Tool Bar
// ============================================================================

void MainWindow::SetupToolBar() {
    main_toolbar_ = addToolBar("Main");
    main_toolbar_->setMovable(false);
    main_toolbar_->setIconSize(QSize(20, 20));
    main_toolbar_->setStyleSheet(ThemeManager::Instance().ToolBarStylesheet());

    main_toolbar_->addAction(action_new_);
    main_toolbar_->addAction(action_open_);
    main_toolbar_->addAction(action_save_);
    main_toolbar_->addSeparator();

    const auto &tm = ThemeManager::Instance();
    QString label_init_ss = QString("QLabel { color: %1; font-size: 12px; }")
                                .arg(tm.Active().text.name());
    QString combo_init_ss = tm.ComboBoxStylesheet();

    // Language selector
    auto *lang_label = new QLabel("  Language: ", main_toolbar_);
    lang_label->setStyleSheet(label_init_ss);
    main_toolbar_->addWidget(lang_label);

    language_combo_ = new QComboBox(main_toolbar_);
    language_combo_->addItems({"Ploy", "C++", "Python", "Rust", "Java", "C#"});
    language_combo_->setCurrentIndex(0);
    language_combo_->setStyleSheet(combo_init_ss);
    main_toolbar_->addWidget(language_combo_);

    main_toolbar_->addSeparator();

    // Target architecture
    auto *target_label = new QLabel("  Target: ", main_toolbar_);
    target_label->setStyleSheet(label_init_ss);
    main_toolbar_->addWidget(target_label);

    target_combo_ = new QComboBox(main_toolbar_);
    target_combo_->addItems({"x86_64", "arm64", "wasm"});
    target_combo_->setStyleSheet(combo_init_ss);
    main_toolbar_->addWidget(target_combo_);

    // Optimization level
    auto *opt_label = new QLabel("  Opt: ", main_toolbar_);
    opt_label->setStyleSheet(label_init_ss);
    main_toolbar_->addWidget(opt_label);

    opt_level_combo_ = new QComboBox(main_toolbar_);
    opt_level_combo_->addItems({"O0", "O1", "O2", "O3"});
    opt_level_combo_->setStyleSheet(combo_init_ss);
    main_toolbar_->addWidget(opt_level_combo_);

    main_toolbar_->addSeparator();

    main_toolbar_->addAction(action_compile_);
    main_toolbar_->addAction(action_compile_run_);
    main_toolbar_->addAction(action_analyze_);
    main_toolbar_->addAction(action_stop_);
}

// ============================================================================
// Status Bar
// ============================================================================

void MainWindow::SetupStatusBar() {
    QStatusBar *sb = statusBar();
    sb->setStyleSheet(ThemeManager::Instance().StatusBarStylesheet());

    status_message_ = new QLabel("Ready");
    sb->addWidget(status_message_, 1);

    status_language_ = new QLabel("Ploy");
    sb->addPermanentWidget(status_language_);

    status_encoding_ = new QLabel("UTF-8");
    sb->addPermanentWidget(status_encoding_);

    status_position_ = new QLabel("Ln 1, Col 1");
    sb->addPermanentWidget(status_position_);
}

// ============================================================================
// Connections
// ============================================================================

void MainWindow::SetupConnections() {
    // File actions
    connect(action_new_, &QAction::triggered, this, &MainWindow::NewFile);
    connect(action_new_from_template_, &QAction::triggered, this, &MainWindow::NewFromTemplate);
    connect(action_open_, &QAction::triggered, this, &MainWindow::OpenFile);
    connect(action_open_folder_, &QAction::triggered, this, &MainWindow::OpenFolder);
    connect(action_save_, &QAction::triggered, this, &MainWindow::Save);
    connect(action_save_as_, &QAction::triggered, this, &MainWindow::SaveAs);
    connect(action_save_all_, &QAction::triggered, this, &MainWindow::SaveAll);
    connect(action_close_tab_, &QAction::triggered, this, [this] {
        CloseTab(editor_tabs_->currentIndex());
    });
    connect(action_quit_, &QAction::triggered, this, &QMainWindow::close);

    // Edit actions
    connect(action_undo_, &QAction::triggered, this, &MainWindow::Undo);
    connect(action_redo_, &QAction::triggered, this, &MainWindow::Redo);
    connect(action_cut_, &QAction::triggered, this, &MainWindow::Cut);
    connect(action_copy_, &QAction::triggered, this, &MainWindow::Copy);
    connect(action_paste_, &QAction::triggered, this, &MainWindow::Paste);
    connect(action_select_all_, &QAction::triggered, this, &MainWindow::SelectAll);
    connect(action_find_, &QAction::triggered, this, &MainWindow::Find);
    connect(action_replace_, &QAction::triggered, this, &MainWindow::Replace);
    connect(action_goto_line_, &QAction::triggered, this, &MainWindow::GoToLine);

    // Build actions
    connect(action_compile_, &QAction::triggered, this, &MainWindow::Compile);
    connect(action_compile_run_, &QAction::triggered, this, &MainWindow::CompileAndRun);
    connect(action_analyze_, &QAction::triggered, this, &MainWindow::AnalyzeCode);
    connect(action_stop_, &QAction::triggered, this, &MainWindow::StopCompilation);

    // View actions
    connect(action_toggle_browser_, &QAction::triggered, this, &MainWindow::ToggleFileBrowser);
    connect(action_toggle_output_, &QAction::triggered, this, &MainWindow::ToggleOutputPanel);
    connect(action_toggle_toolbar_, &QAction::triggered, this, &MainWindow::ToggleToolBar);
    connect(action_toggle_statusbar_, &QAction::triggered, this, &MainWindow::ToggleStatusBar);
    connect(action_toggle_linenumbers_, &QAction::triggered, this, &MainWindow::ToggleLineNumbers);
    connect(action_toggle_wordwrap_, &QAction::triggered, this, &MainWindow::ToggleWordWrap);
    connect(action_zoom_in_, &QAction::triggered, this, &MainWindow::ZoomIn);
    connect(action_zoom_out_, &QAction::triggered, this, &MainWindow::ZoomOut);
    connect(action_zoom_reset_, &QAction::triggered, this, &MainWindow::ZoomReset);

    // Terminal actions
    connect(action_new_terminal_, &QAction::triggered, this, &MainWindow::NewTerminal);
    connect(action_toggle_terminal_, &QAction::triggered, this, &MainWindow::ToggleTerminal);
    connect(action_clear_terminal_, &QAction::triggered, this, &MainWindow::ClearTerminal);
    connect(action_restart_terminal_, &QAction::triggered, this, &MainWindow::RestartTerminal);

    // Settings
    connect(action_settings_, &QAction::triggered, this, &MainWindow::OpenSettings);
    if (settings_dialog_) {
        connect(settings_dialog_, &SettingsDialog::SettingsChanged,
                this, &MainWindow::ApplySettings);
    }

    // Panel toggles
    connect(action_toggle_git_, &QAction::triggered, this, &MainWindow::ToggleGitPanel);
    connect(action_toggle_build_, &QAction::triggered, this, &MainWindow::ToggleBuildPanel);
    connect(action_toggle_debug_, &QAction::triggered, this, &MainWindow::ToggleDebugPanel);
    connect(action_toggle_topology_, &QAction::triggered, this, &MainWindow::ToggleTopologyPanel);

    // CMake build actions
    connect(action_cmake_configure_, &QAction::triggered, this, &MainWindow::CmakeConfigure);
    connect(action_cmake_build_, &QAction::triggered, this, &MainWindow::CmakeBuild);

    // Debug actions
    connect(action_debug_start_, &QAction::triggered, this, &MainWindow::DebugStart);
    connect(action_debug_stop_, &QAction::triggered, this, &MainWindow::DebugStop);
    connect(action_debug_step_over_, &QAction::triggered, this, &MainWindow::DebugStepOver);
    connect(action_debug_step_into_, &QAction::triggered, this, &MainWindow::DebugStepInto);
    connect(action_debug_step_out_, &QAction::triggered, this, &MainWindow::DebugStepOut);

    // Git panel signals
    connect(git_panel_, &GitPanel::OperationCompleted,
            this, [this](const QString &msg) { status_message_->setText(msg); });
    connect(git_panel_, &GitPanel::OperationFailed,
            this, [this](const QString &msg) { status_message_->setText(msg); });
    connect(git_panel_, &GitPanel::FileOpenRequested,
            this, &MainWindow::OnFileActivated);

    // Build panel signals
    connect(build_panel_, &BuildPanel::StatusMessage,
            this, [this](const QString &msg) { status_message_->setText(msg); });
    connect(build_panel_, &BuildPanel::BuildErrorFound,
            this, [this](const QString &file, int line, const QString &msg) {
        output_panel_->AppendOutput(
            QString("%1:%2: error: %3").arg(file).arg(line).arg(msg));
    });

    // Debug panel signals
    connect(debug_panel_, &DebugPanel::StatusMessage,
            this, [this](const QString &msg) { status_message_->setText(msg); });
    connect(debug_panel_, &DebugPanel::DebugLocationChanged,
            this, [this](const QString &file, int line) {
        int idx = OpenFileInTab(file);
        if (idx >= 0) {
            CodeEditor *editor = EditorAt(idx);
            if (editor) {
                QTextCursor cursor(editor->document()->findBlockByLineNumber(line - 1));
                editor->setTextCursor(cursor);
                editor->centerCursor();
                editor->setFocus();
            }
        }
        // Highlight the corresponding topology node during debugging
        if (topology_panel_) {
            topology_panel_->HighlightDebugNode(file, line);
        }
    });
    connect(debug_panel_, &DebugPanel::DebugStopped,
            this, [this]() {
        // Clear topology debug highlights when the debug session ends
        if (topology_panel_) {
            topology_panel_->ClearDebugHighlights();
            topology_panel_->ClearExecutionHighlight();
        }
    });
    connect(debug_panel_, &DebugPanel::DebugStarted,
            this, [this]() {
        // Clear previous execution highlights when a new debug session starts
        if (topology_panel_) {
            topology_panel_->ClearDebugHighlights();
            topology_panel_->ClearExecutionHighlight();
        }
    });

    // Topology panel signals
    connect(topology_panel_, &TopologyPanel::ValidationComplete,
            this, [this](int errors, int warnings) {
        status_message_->setText(
            QString("Topology validation: %1 error(s), %2 warning(s)")
                .arg(errors).arg(warnings));
    });
    connect(topology_panel_, &TopologyPanel::NodeDoubleClicked,
            this, [this](const QString &file, int line) {
        if (!file.isEmpty() && line > 0) {
            int idx = OpenFileInTab(file);
            if (idx >= 0) {
                CodeEditor *editor = EditorAt(idx);
                if (editor) {
                    QTextCursor cursor(editor->document()->findBlockByLineNumber(line - 1));
                    editor->setTextCursor(cursor);
                    editor->centerCursor();
                    editor->setFocus();
                }
            }
        }
    });

    // Topology panel: open generated .ploy file in editor
    connect(topology_panel_, &TopologyPanel::OpenFileRequested,
            this, [this](const QString &file_path) {
        if (!file_path.isEmpty()) {
            OpenFileInTab(file_path);
        }
    });

    // Topology panel: bidirectional sync — when edge sync modifies the .ploy
    // file, reload and highlight the affected line in the editor.
    connect(topology_panel_, &TopologyPanel::FileContentChanged,
            this, [this](const QString &file_path, int line) {
        if (file_path.isEmpty() || line <= 0) return;
        int idx = OpenFileInTab(file_path);
        if (idx >= 0) {
            CodeEditor *editor = EditorAt(idx);
            if (editor) {
                // Reload the file content from disk to reflect the change
                QFile file(file_path);
                if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                    QTextStream in(&file);
                    QString content = in.readAll();
                    file.close();
                    // Block signals to avoid triggering modification tracking
                    editor->blockSignals(true);
                    editor->setPlainText(content);
                    editor->blockSignals(false);
                }
                // Move cursor to the changed line and briefly highlight it
                QTextBlock block = editor->document()->findBlockByLineNumber(line - 1);
                if (block.isValid()) {
                    QTextCursor cursor(block);
                    editor->setTextCursor(cursor);
                    editor->centerCursor();
                    // Apply a temporary highlight via extra selections
                    QTextEdit::ExtraSelection sel;
                    sel.format.setBackground(QColor(255, 255, 0, 60));
                    sel.format.setProperty(QTextFormat::FullWidthSelection, true);
                    sel.cursor = cursor;
                    QList<QTextEdit::ExtraSelection> extras = editor->extraSelections();
                    extras.append(sel);
                    editor->setExtraSelections(extras);
                    // Clear the highlight after 2 seconds
                    QTimer::singleShot(2000, editor, [editor]() {
                        editor->setExtraSelections({});
                    });
                }
            }
        }
    });

    // Tab management
    connect(editor_tabs_, &QTabWidget::currentChanged, this, &MainWindow::OnTabChanged);
    connect(editor_tabs_, &QTabWidget::tabCloseRequested, this, &MainWindow::CloseTab);
    connect(editor_tabs_->tabBar(), &QTabBar::tabMoved,
            this, &MainWindow::OnEditorTabMoved);
    connect(bottom_tabs_, &QTabWidget::currentChanged, this,
            [this](int) { UpdateViewActionChecks(); });

    // File browser
    connect(file_browser_, &FileBrowser::FileActivated, this, &MainWindow::OnFileActivated);

    // File browser — open file from context menu
    connect(file_browser_, &FileBrowser::OpenFileRequested,
            this, &MainWindow::OnFileActivated);

    // File browser — open terminal at a directory
    connect(file_browser_, &FileBrowser::OpenTerminalRequested,
            this, [this](const QString &dir) {
        // Create a new terminal tab rooted at the requested directory.
        auto *terminal = new TerminalWidget(terminal_tabs_);
        terminal->SetWorkingDirectory(dir);
        QString title = QString("Terminal %1").arg(next_terminal_id_++);
        int idx = terminal_tabs_->addTab(terminal, title);
        terminal_tabs_->setCurrentIndex(idx);

        connect(terminal, &TerminalWidget::TitleChanged,
                this, [this, terminal](const QString &new_title) {
            int i = terminal_tabs_->indexOf(terminal);
            if (i >= 0) terminal_tabs_->setTabText(i, new_title);
        });
        connect(terminal, &TerminalWidget::ShellFinished,
                this, [this, terminal](int exit_code) {
            int i = terminal_tabs_->indexOf(terminal);
            if (i >= 0) {
                terminal_tabs_->setTabText(
                    i, terminal_tabs_->tabText(i) +
                           QString(" [exited %1]").arg(exit_code));
            }
        });

        // Ensure bottom panel is visible and showing the terminal.
        bottom_tabs_->setCurrentWidget(terminal_tabs_);
        vertical_splitter_->setSizes({vertical_splitter_->height() * 2 / 3,
                                      vertical_splitter_->height() / 3});
    });

    // File browser — generate topology for a .ploy file
    connect(file_browser_, &FileBrowser::GenerateTopologyRequested,
            this, [this](const QString &ploy_path) {
        panel_manager_->ShowPanel("topology");
        UpdateViewActionChecks();
        topology_panel_->LoadFromFile(ploy_path);
    });

    // File browser — create file from template in the selected directory
    connect(file_browser_, &FileBrowser::NewFromTemplateRequested,
            this, [this](const QString &parent_dir) {
        NewFromTemplateInDir(parent_dir);
    });

    // Output panel error click
    connect(output_panel_, &OutputPanel::ErrorClicked,
            this, [this](int line, int col, const QString &file) {
        int target_index = editor_tabs_->currentIndex();
        if (!file.isEmpty()) {
            target_index = OpenFileInTab(file);
        }
        CodeEditor *editor = EditorAt(target_index);
        if (!editor) return;

        QTextBlock block = editor->document()->findBlockByLineNumber(qMax(0, line - 1));
        if (!block.isValid()) return;

        QTextCursor cursor(editor->document());
        int column_offset = qMax(0, col - 1);
        int position = qMin(block.position() + column_offset,
                            block.position() + qMax(0, block.length() - 1));
        cursor.setPosition(position);
        editor->setTextCursor(cursor);
        editor->centerCursor();
        editor->setFocus();
    });

    // Language combo
    connect(language_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::OnLanguageChanged);
}

// ============================================================================
// Shortcuts
// ============================================================================

void MainWindow::SetupShortcuts() {
    // Extra shortcuts not covered by menu actions can be added here
}

// ============================================================================
// Analysis Timer (real-time error checking as the user types)
// ============================================================================

void MainWindow::SetupAnalysisTimer() {
    analysis_timer_ = new QTimer(this);
    analysis_timer_->setSingleShot(true);
    analysis_timer_->setInterval(500); // 500 ms debounce
    connect(analysis_timer_, &QTimer::timeout,
            this, &MainWindow::OnAnalysisTimerTimeout);

    auto_save_timer_ = new QTimer(this);
    auto_save_timer_->setSingleShot(false);
    connect(auto_save_timer_, &QTimer::timeout,
            this, &MainWindow::AutoSaveModifiedFiles);
}

// ============================================================================
// File Actions
// ============================================================================

void MainWindow::NewFile() {
    static const QStringList lang_ids = {"ploy", "cpp", "python", "rust", "java", "csharp"};
    int lang_index = language_combo_ ? language_combo_->currentIndex() : 0;
    if (lang_index < 0 || lang_index >= lang_ids.size()) {
        lang_index = 0;
    }
    QString language = lang_ids[lang_index];
    QString title = QString("Untitled-%1.%2")
                        .arg(next_untitled_id_++)
                        .arg(LanguageToExtension(language));
    CreateNewTab(title, language);
}

// ============================================================================
// NewFromTemplate — create a new file pre-filled with a language template
// ============================================================================

void MainWindow::NewFromTemplate() {
    // Templates for each supported language.  Each entry contains a display
    // name, language id, file extension, and multi-line boilerplate source.
    struct Template {
        QString display_name;
        QString language;
        QString extension;
        QString content;
    };

    static const std::vector<Template> templates = {
        {"Ploy — Cross-language linker script", "ploy", "ploy",
         "// Cross-language linker script\n"
         "// Link functions from different languages\n"
         "\n"
         "// Import packages\n"
         "IMPORT python PACKAGE numpy;\n"
         "\n"
         "// Link external functions\n"
         "LINK cpp::math::add AS FUNC(INT, INT) -> INT;\n"
         "LINK python::utils::process AS FUNC(STRING) -> STRING;\n"
         "\n"
         "// Define a pipeline\n"
         "FUNC main() -> INT {\n"
         "    LET result = CALL(cpp, math::add, 1, 2);\n"
         "    RETURN result;\n"
         "}\n"},

        {"C++ — Hello World", "cpp", "cpp",
         "#include <iostream>\n"
         "\n"
         "int main() {\n"
         "    std::cout << \"Hello, World!\" << std::endl;\n"
         "    return 0;\n"
         "}\n"},

        {"C++ — Class template", "cpp", "hpp",
         "#pragma once\n"
         "\n"
         "#include <string>\n"
         "\n"
         "class MyClass {\n"
         "  public:\n"
         "    MyClass() = default;\n"
         "    ~MyClass() = default;\n"
         "\n"
         "    void DoSomething();\n"
         "\n"
         "  private:\n"
         "    std::string name_;\n"
         "    int value_{0};\n"
         "};\n"},

        {"Python — Script", "python", "py",
         "#!/usr/bin/env python3\n"
         "\"\"\"Module docstring.\"\"\"\n"
         "\n"
         "\n"
         "def main() -> None:\n"
         "    \"\"\"Entry point.\"\"\"\n"
         "    print(\"Hello, World!\")\n"
         "\n"
         "\n"
         "if __name__ == \"__main__\":\n"
         "    main()\n"},

        {"Python — Class", "python", "py",
         "#!/usr/bin/env python3\n"
         "\"\"\"Module with a sample class.\"\"\"\n"
         "\n"
         "from dataclasses import dataclass\n"
         "\n"
         "\n"
         "@dataclass\n"
         "class MyClass:\n"
         "    \"\"\"A sample data class.\"\"\"\n"
         "\n"
         "    name: str\n"
         "    value: int = 0\n"
         "\n"
         "    def process(self) -> str:\n"
         "        \"\"\"Process the data.\"\"\"\n"
         "        return f\"{self.name}: {self.value}\"\n"},

        {"Rust — Hello World", "rust", "rs",
         "fn main() {\n"
         "    println!(\"Hello, World!\");\n"
         "}\n"},

        {"Rust — Struct with impl", "rust", "rs",
         "/// A sample struct.\n"
         "pub struct MyStruct {\n"
         "    name: String,\n"
         "    value: i32,\n"
         "}\n"
         "\n"
         "impl MyStruct {\n"
         "    pub fn new(name: &str, value: i32) -> Self {\n"
         "        Self {\n"
         "            name: name.to_string(),\n"
         "            value,\n"
         "        }\n"
         "    }\n"
         "\n"
         "    pub fn display(&self) {\n"
         "        println!(\"{}: {}\", self.name, self.value);\n"
         "    }\n"
         "}\n"
         "\n"
         "fn main() {\n"
         "    let s = MyStruct::new(\"example\", 42);\n"
         "    s.display();\n"
         "}\n"},

        {"Java — Hello World", "java", "java",
         "public class Main {\n"
         "    public static void main(String[] args) {\n"
         "        System.out.println(\"Hello, World!\");\n"
         "    }\n"
         "}\n"},

        {"Java — Class template", "java", "java",
         "/**\n"
         " * A sample Java class.\n"
         " */\n"
         "public class MyClass {\n"
         "    private String name;\n"
         "    private int value;\n"
         "\n"
         "    public MyClass(String name, int value) {\n"
         "        this.name = name;\n"
         "        this.value = value;\n"
         "    }\n"
         "\n"
         "    public String getName() {\n"
         "        return name;\n"
         "    }\n"
         "\n"
         "    public int getValue() {\n"
         "        return value;\n"
         "    }\n"
         "\n"
         "    @Override\n"
         "    public String toString() {\n"
         "        return name + \": \" + value;\n"
         "    }\n"
         "}\n"},

        {"C# — Hello World", "csharp", "cs",
         "using System;\n"
         "\n"
         "namespace MyApp\n"
         "{\n"
         "    class Program\n"
         "    {\n"
         "        static void Main(string[] args)\n"
         "        {\n"
         "            Console.WriteLine(\"Hello, World!\");\n"
         "        }\n"
         "    }\n"
         "}\n"},

        {"C# — Class template", "csharp", "cs",
         "using System;\n"
         "\n"
         "namespace MyApp\n"
         "{\n"
         "    /// <summary>\n"
         "    /// A sample C# class.\n"
         "    /// </summary>\n"
         "    public class MyClass\n"
         "    {\n"
         "        public string Name { get; set; }\n"
         "        public int Value { get; set; }\n"
         "\n"
         "        public MyClass(string name, int value)\n"
         "        {\n"
         "            Name = name;\n"
         "            Value = value;\n"
         "        }\n"
         "\n"
         "        public override string ToString()\n"
         "        {\n"
         "            return $\"{Name}: {Value}\";\n"
         "        }\n"
         "    }\n"
         "}\n"},
    };

    // Build the list of display names
    QStringList names;
    names.reserve(static_cast<int>(templates.size()));
    for (const auto &t : templates) {
        names << t.display_name;
    }

    bool ok = false;
    QString chosen = QInputDialog::getItem(
        this, "New From Template", "Select a template:", names, 0, false, &ok);
    if (!ok || chosen.isEmpty()) return;

    // Find the chosen template
    int idx = names.indexOf(chosen);
    if (idx < 0 || idx >= static_cast<int>(templates.size())) return;

    const Template &tmpl = templates[static_cast<size_t>(idx)];
    QString title = QString("Untitled-%1.%2")
                        .arg(next_untitled_id_++)
                        .arg(tmpl.extension);
    int tab_index = CreateNewTab(title, tmpl.language);

    CodeEditor *editor = EditorAt(tab_index);
    if (editor) {
        editor->setPlainText(tmpl.content);
        editor->document()->setModified(false);
    }

    status_message_->setText(QString("Created from template: %1").arg(tmpl.display_name));
}

// ============================================================================
// NewFromTemplateInDir — create a template file inside a specific directory
// ============================================================================

void MainWindow::NewFromTemplateInDir(const QString &parent_dir) {
    // Reuse the same template catalogue as NewFromTemplate().
    struct Template {
        QString display_name;
        QString language;
        QString extension;
        QString content;
    };

    static const std::vector<Template> templates = {
        {"Ploy — Cross-language linker script", "ploy", "ploy",
         "// Cross-language linker script\n"
         "IMPORT python PACKAGE numpy;\n"
         "\n"
         "LINK cpp::math::add AS FUNC(INT, INT) -> INT;\n"
         "\n"
         "FUNC main() -> INT {\n"
         "    LET result = CALL(cpp, math::add, 1, 2);\n"
         "    RETURN result;\n"
         "}\n"},
        {"C++ — Hello World", "cpp", "cpp",
         "#include <iostream>\n\nint main() {\n"
         "    std::cout << \"Hello, World!\" << std::endl;\n"
         "    return 0;\n}\n"},
        {"Python — Script", "python", "py",
         "#!/usr/bin/env python3\n\"\"\"Module docstring.\"\"\"\n\n\n"
         "def main() -> None:\n    print(\"Hello, World!\")\n\n\n"
         "if __name__ == \"__main__\":\n    main()\n"},
        {"Rust — Hello World", "rust", "rs",
         "fn main() {\n    println!(\"Hello, World!\");\n}\n"},
        {"Java — Hello World", "java", "java",
         "public class Main {\n"
         "    public static void main(String[] args) {\n"
         "        System.out.println(\"Hello, World!\");\n"
         "    }\n}\n"},
        {"C# — Hello World", "csharp", "cs",
         "using System;\n\nclass Program {\n"
         "    static void Main(string[] args) {\n"
         "        Console.WriteLine(\"Hello, World!\");\n"
         "    }\n}\n"},
    };

    QStringList names;
    names.reserve(static_cast<int>(templates.size()));
    for (const auto &t : templates) {
        names << t.display_name;
    }

    bool ok = false;
    QString chosen = QInputDialog::getItem(
        this, "New From Template", "Select a template:", names, 0, false, &ok);
    if (!ok || chosen.isEmpty()) return;

    int idx = names.indexOf(chosen);
    if (idx < 0 || idx >= static_cast<int>(templates.size())) return;

    const Template &tmpl = templates[static_cast<size_t>(idx)];

    // Ask for the file name
    QString default_name = QString("untitled.%1").arg(tmpl.extension);
    QString name = QInputDialog::getText(
        this, "New From Template", "File name:", QLineEdit::Normal,
        default_name, &ok);
    if (!ok || name.isEmpty()) return;

    QString full_path = parent_dir + "/" + name;
    QFile file(full_path);
    if (file.exists()) {
        QMessageBox::warning(this, "New From Template",
                             "A file with that name already exists.");
        return;
    }
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "New From Template",
                              "Failed to create file: " + file.errorString());
        return;
    }
    file.write(tmpl.content.toUtf8());
    file.close();

    OpenFileInTab(full_path);
    status_message_->setText(
        QString("Created from template: %1").arg(tmpl.display_name));
}

void MainWindow::OpenFile() {
    QStringList paths = QFileDialog::getOpenFileNames(
        this, "Open File", QString(),
        "All Supported Files (*.ploy *.cpp *.h *.hpp *.c *.py *.rs *.java *.cs);;"
        "Ploy Files (*.ploy);;"
        "C++ Files (*.cpp *.h *.hpp *.c);;"
        "Python Files (*.py);;"
        "Rust Files (*.rs);;"
        "Java Files (*.java);;"
        "C# Files (*.cs);;"
        "All Files (*)");

    for (const QString &path : paths) {
        OpenFileInTab(path);
    }
}

void MainWindow::OpenFolder() {
    QString dir = QFileDialog::getExistingDirectory(
        this, "Open Folder", QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (!dir.isEmpty()) {
        file_browser_->SetRootPath(dir);

        // Notify plugins about workspace change
        auto &pm = polyglot::plugins::PluginManager::Instance();
        pm.SetWorkspaceRoot(dir.toStdString());
        pm.FireWorkspaceChanged(dir.toStdString());
    }
}

void MainWindow::Save() {
    int index = editor_tabs_->currentIndex();
    if (index < 0) return;

    auto it = tab_info_.find(index);
    if (it == tab_info_.end()) return;

    if (it->second.file_path.isEmpty()) {
        SaveAs();
        return;
    }

    CodeEditor *editor = EditorAt(index);
    if (!editor) return;

    QFile file(it->second.file_path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(&file);
        stream << editor->toPlainText();
        file.close();
        editor->document()->setModified(false);
        UpdateTabTitle(index, false);
        status_message_->setText("Saved: " + it->second.file_path);

        // Notify plugins about the file save
        polyglot::plugins::PluginManager::Instance().FireFileSaved(
            it->second.file_path.toStdString());

        // Re-index file symbols after save
        if (compiler_service_) {
            compiler_service_->IndexWorkspaceFile(
                it->second.file_path.toStdString(),
                editor->toPlainText().toStdString(),
                it->second.language.toStdString());
        }
    } else {
        QMessageBox::warning(this, "Save Error",
                             "Could not save file: " + it->second.file_path);
    }
}

void MainWindow::SaveAs() {
    int index = editor_tabs_->currentIndex();
    if (index < 0) return;

    CodeEditor *editor = EditorAt(index);
    if (!editor) return;

    QString path = QFileDialog::getSaveFileName(
        this, "Save As", QString(),
        "Ploy Files (*.ploy);;"
        "C++ Files (*.cpp *.h *.hpp);;"
        "Python Files (*.py);;"
        "Rust Files (*.rs);;"
        "Java Files (*.java);;"
        "C# Files (*.cs);;"
        "All Files (*)");

    if (path.isEmpty()) return;

    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(&file);
        stream << editor->toPlainText();
        file.close();

        auto it = tab_info_.find(index);
        if (it != tab_info_.end()) {
            it->second.file_path = path;
            QString lang = DetectLanguage(path);
            it->second.language = lang;
            AttachHighlighter(editor, lang);
            UpdateLanguageCombo(lang);
            status_language_->setText(lang);
        }

        editor->SetFilePath(path);
        editor->document()->setModified(false);
        editor_tabs_->setTabText(index, QFileInfo(path).fileName());
        status_message_->setText("Saved: " + path);
    }
}

void MainWindow::SaveAll() {
    int current = editor_tabs_->currentIndex();
    for (int i = 0; i < editor_tabs_->count(); ++i) {
        editor_tabs_->setCurrentIndex(i);
        CodeEditor *editor = EditorAt(i);
        if (editor && editor->document()->isModified()) {
            Save();
        }
    }
    editor_tabs_->setCurrentIndex(current);
}

// ============================================================================
// Tab Management
// ============================================================================

void MainWindow::CloseTab(int index) {
    if (index < 0 || index >= editor_tabs_->count()) return;

    if (!MaybeSave(index)) return;

    tab_info_.erase(index);
    editor_tabs_->removeTab(index);

    // Re-index remaining tabs
    std::unordered_map<int, TabInfo> new_map;
    for (auto &[key, val] : tab_info_) {
        int new_key = (key > index) ? key - 1 : key;
        new_map[new_key] = std::move(val);
    }
    tab_info_ = std::move(new_map);

    if (editor_tabs_->count() == 0) {
        NewFile();
    }
}

int MainWindow::OpenFileInTab(const QString &path) {
    // Check if already open
    for (auto &[index, info] : tab_info_) {
        if (info.file_path == path) {
            editor_tabs_->setCurrentIndex(index);
            return index;
        }
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Open Error", "Could not open: " + path);
        return -1;
    }

    QTextStream stream(&file);
    QString content = stream.readAll();
    file.close();

    QString language = DetectLanguage(path);
    int index = CreateNewTab(QFileInfo(path).fileName(), language);

    CodeEditor *editor = EditorAt(index);
    if (editor) {
        editor->setPlainText(content);
        editor->SetFilePath(path);
        editor->document()->setModified(false);
    }

    auto it = tab_info_.find(index);
    if (it != tab_info_.end()) {
        it->second.file_path = path;
    }

    // Notify plugins about the file open
    polyglot::plugins::PluginManager::Instance().FireFileOpened(
        path.toStdString());

    // Index file symbols for cross-file go-to-definition and completions
    if (editor && compiler_service_) {
        compiler_service_->IndexWorkspaceFile(
            path.toStdString(),
            content.toStdString(),
            language.toStdString());
    }

    return index;
}

int MainWindow::CreateNewTab(const QString &title, const QString &language) {
    auto *editor = new CodeEditor();

    // Apply editor theme.  Use the current theme's editor colors.
    // NOTE: Do not set the `color` property via stylesheet — that overrides
    // QSyntaxHighlighter character formats on many Qt builds.  Instead, set
    // the text foreground via QPalette so that the highlighter's per-token
    // setFormat() calls take precedence.
    const auto &tc = ThemeManager::Instance().Active();
    editor->setStyleSheet(
        QString("QPlainTextEdit { background: %1; border: none; "
                "selection-background-color: %2; }")
            .arg(tc.editor_background.name(), tc.editor_selection.name()));
    {
        QPalette pal = editor->palette();
        pal.setColor(QPalette::Text, tc.editor_text);
        pal.setColor(QPalette::Base, tc.editor_background);
        editor->setPalette(pal);
    }
    ApplyEditorSettings(editor);

    int index = editor_tabs_->addTab(editor, title);
    editor_tabs_->setCurrentIndex(index);

    TabInfo info;
    info.language = language;
    info.highlighter = nullptr;
    tab_info_[index] = info;

    AttachHighlighter(editor, language);
    UpdateLanguageCombo(language);

    // Wire the compiler service and language for auto-completion
    editor->SetCompilerService(compiler_service_.get());
    editor->SetLanguage(language.toStdString());

    // Handle go-to-definition requests
    connect(editor, &CodeEditor::GoToDefinitionRequested,
            this, [this](const QString &symbol, int line, int col) {
        Q_UNUSED(line)
        Q_UNUSED(col)

        CodeEditor *ed = CurrentEditor();
        if (!ed) return;

        int idx = editor_tabs_->currentIndex();
        auto info_it = tab_info_.find(idx);
        std::string current_file = info_it != tab_info_.end()
            ? info_it->second.file_path.toStdString() : std::string();
        std::string current_lang = info_it != tab_info_.end()
            ? info_it->second.language.toStdString() : std::string();
        std::string source = ed->toPlainText().toStdString();

        // Use CompilerService::FindDefinition for cross-file resolution
        auto def = compiler_service_->FindDefinition(
            symbol.toStdString(), current_file, source, current_lang);

        if (def.found) {
            if (def.file == current_file || def.file.empty()) {
                // Navigate within current file
                QTextBlock block = ed->document()->findBlockByNumber(
                    static_cast<int>(def.line) - 1);
                if (block.isValid()) {
                    QTextCursor cursor(block);
                    ed->setTextCursor(cursor);
                    ed->centerCursor();
                    ed->setFocus();
                }
            } else {
                // Open the target file and navigate to definition line
                int tab_idx = OpenFileInTab(QString::fromStdString(def.file));
                if (tab_idx >= 0) {
                    editor_tabs_->setCurrentIndex(tab_idx);
                    CodeEditor *target_ed = EditorAt(tab_idx);
                    if (target_ed) {
                        QTextBlock block = target_ed->document()->findBlockByNumber(
                            static_cast<int>(def.line) - 1);
                        if (block.isValid()) {
                            QTextCursor cursor(block);
                            target_ed->setTextCursor(cursor);
                            target_ed->centerCursor();
                            target_ed->setFocus();
                        }
                    }
                }
            }
            return;
        }

        // Definition not found — show tooltip
        QToolTip::showText(
            ed->mapToGlobal(ed->cursorRect().topLeft()),
            QString("Definition of '%1' not found").arg(symbol),
            ed, QRect(), 2000);
    });

    // Connect modification signal
    connect(editor->document(), &QTextDocument::modificationChanged,
            this, [this, editor](bool modified) {
        int current_index = editor_tabs_->indexOf(editor);
        if (current_index >= 0) {
            UpdateTabTitle(current_index, modified);
        }
    });

    // Connect cursor position
    connect(editor, &QPlainTextEdit::cursorPositionChanged,
            this, &MainWindow::OnCursorPositionChanged);

    // Connect text change to analysis timer
    connect(editor, &QPlainTextEdit::textChanged,
            this, &MainWindow::OnEditorModified);

    return index;
}

CodeEditor *MainWindow::CurrentEditor() const {
    return qobject_cast<CodeEditor *>(editor_tabs_->currentWidget());
}

CodeEditor *MainWindow::EditorAt(int index) const {
    return qobject_cast<CodeEditor *>(editor_tabs_->widget(index));
}

void MainWindow::UpdateTabTitle(int index, bool modified) {
    if (index < 0 || index >= editor_tabs_->count()) return;

    QString title = editor_tabs_->tabText(index);
    if (modified && !title.startsWith("* ")) {
        editor_tabs_->setTabText(index, "* " + title);
    } else if (!modified && title.startsWith("* ")) {
        editor_tabs_->setTabText(index, title.mid(2));
    }
}

bool MainWindow::MaybeSave(int index) {
    CodeEditor *editor = EditorAt(index);
    if (!editor || !editor->document()->isModified()) return true;

    auto result = QMessageBox::question(
        this, "Unsaved Changes",
        "The file has unsaved changes. Save before closing?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

    if (result == QMessageBox::Save) {
        int prev = editor_tabs_->currentIndex();
        editor_tabs_->setCurrentIndex(index);
        Save();
        editor_tabs_->setCurrentIndex(prev);
        return true;
    }
    return result == QMessageBox::Discard;
}

bool MainWindow::MaybeSaveAll() {
    for (int i = 0; i < editor_tabs_->count(); ++i) {
        if (!MaybeSave(i)) return false;
    }
    return true;
}

// ============================================================================
// Edit Actions
// ============================================================================

void MainWindow::Undo() { if (auto *e = CurrentEditor()) e->undo(); }
void MainWindow::Redo() { if (auto *e = CurrentEditor()) e->redo(); }
void MainWindow::Cut() { if (auto *e = CurrentEditor()) e->cut(); }
void MainWindow::Copy() { if (auto *e = CurrentEditor()) e->copy(); }
void MainWindow::Paste() { if (auto *e = CurrentEditor()) e->paste(); }
void MainWindow::SelectAll() { if (auto *e = CurrentEditor()) e->selectAll(); }

void MainWindow::Find() {
    // Simple find dialog using QInputDialog
    bool ok = false;
    QString text = QInputDialog::getText(this, "Find", "Search for:",
                                         QLineEdit::Normal, QString(), &ok);
    if (ok && !text.isEmpty()) {
        CodeEditor *editor = CurrentEditor();
        if (editor) {
            editor->find(text);
        }
    }
}

void MainWindow::Replace() {
    // Simple replace: find first, then prompt for replacement
    bool ok = false;
    QString find_text = QInputDialog::getText(this, "Replace", "Find:",
                                              QLineEdit::Normal, QString(), &ok);
    if (!ok || find_text.isEmpty()) return;

    QString replace_text = QInputDialog::getText(this, "Replace", "Replace with:",
                                                 QLineEdit::Normal, QString(), &ok);
    if (!ok) return;

    CodeEditor *editor = CurrentEditor();
    if (!editor) return;

    QString content = editor->toPlainText();
    content.replace(find_text, replace_text);
    editor->setPlainText(content);
}

void MainWindow::GoToLine() {
    CodeEditor *editor = CurrentEditor();
    if (!editor) return;

    bool ok = false;
    int line = QInputDialog::getInt(this, "Go to Line", "Line number:",
                                    1, 1, editor->blockCount(), 1, &ok);
    if (ok) {
        QTextCursor cursor(editor->document()->findBlockByLineNumber(line - 1));
        editor->setTextCursor(cursor);
        editor->centerCursor();
    }
}

// ============================================================================
// Build Actions
// ============================================================================

void MainWindow::Compile() {
    CodeEditor *editor = CurrentEditor();
    if (!editor) return;

    int index = editor_tabs_->currentIndex();
    auto it = tab_info_.find(index);
    if (it == tab_info_.end()) return;

    output_panel_->ClearAll();
    output_panel_->ShowOutputTab();

    std::string source = editor->toPlainText().toStdString();
    std::string language = it->second.language.toStdString();
    QString filename_qt = it->second.file_path.isEmpty()
                              ? editor_tabs_->tabText(index)
                              : QFileInfo(it->second.file_path).fileName();
    if (filename_qt.startsWith("* ")) {
        filename_qt = filename_qt.mid(2);
    }
    std::string filename = filename_qt.toStdString();
    std::string target = target_combo_->currentText().toStdString();
    int opt = opt_level_combo_->currentIndex();

    output_panel_->AppendOutput(
        QString("Compiling %1 (%2, %3, O%4)...")
            .arg(editor_tabs_->tabText(index))
            .arg(it->second.language)
            .arg(target_combo_->currentText())
            .arg(opt));

    // Notify plugins that a build is starting
    polyglot::plugins::PluginManager::Instance().FireBuildStarted();

    auto result = compiler_service_->Compile(source, language, filename, target, opt);

    output_panel_->AppendOutput(QString::fromStdString(result.output));
    output_panel_->AppendOutput(
        QString("Elapsed: %1 ms").arg(result.elapsed_ms, 0, 'f', 2));

    output_panel_->ShowDiagnostics(
        result.diagnostics,
        it->second.file_path);

    // Push diagnostics to the editor for squiggly underlines and inline hints
    editor->SetDiagnostics(result.diagnostics);

    if (result.success) {
        status_message_->setText("Compilation successful");
    } else {
        status_message_->setText("Compilation failed");
    }

    // Notify plugins that the build finished
    polyglot::plugins::PluginManager::Instance().FireBuildFinished(
        result.success ? 0 : 1);
}

void MainWindow::CompileAndRun() {
    CodeEditor *editor = CurrentEditor();
    if (!editor) return;

    int index = editor_tabs_->currentIndex();
    auto it = tab_info_.find(index);
    if (it == tab_info_.end()) return;

    // First, compile
    output_panel_->ClearAll();
    output_panel_->ShowOutputTab();

    std::string source = editor->toPlainText().toStdString();
    std::string language = it->second.language.toStdString();
    QString filename_qt = it->second.file_path.isEmpty()
                              ? editor_tabs_->tabText(index)
                              : QFileInfo(it->second.file_path).fileName();
    if (filename_qt.startsWith("* ")) {
        filename_qt = filename_qt.mid(2);
    }
    std::string filename = filename_qt.toStdString();
    std::string target = target_combo_->currentText().toStdString();
    int opt = opt_level_combo_->currentIndex();

    output_panel_->AppendOutput(
        QString("Compiling %1 (%2, %3, O%4)...")
            .arg(filename_qt)
            .arg(it->second.language)
            .arg(target_combo_->currentText())
            .arg(opt));

    auto result = compiler_service_->Compile(source, language, filename, target, opt);

    output_panel_->AppendOutput(QString::fromStdString(result.output));
    output_panel_->AppendOutput(
        QString("Compilation elapsed: %1 ms").arg(result.elapsed_ms, 0, 'f', 2));

    output_panel_->ShowDiagnostics(result.diagnostics, it->second.file_path);

    // Push diagnostics to the editor for squiggly underlines and inline hints
    editor->SetDiagnostics(result.diagnostics);

    if (!result.success) {
        status_message_->setText("Compilation failed — cannot run");
        return;
    }

    status_message_->setText("Compilation successful — running...");
    action_stop_->setEnabled(true);
    action_compile_run_->setEnabled(false);

    // Determine binary path
    // Prefer file next to source with no extension (or .exe on Windows)
    QString source_dir;
    if (!it->second.file_path.isEmpty()) {
        source_dir = QFileInfo(it->second.file_path).absolutePath();
    } else {
        source_dir = QDir::currentPath();
    }

    QString base_name = QFileInfo(filename_qt).completeBaseName();
#ifdef Q_OS_WIN
    QString binary_path = source_dir + "/aux/" + base_name + ".exe";
    // Fallback: check same directory
    if (!QFileInfo::exists(binary_path)) {
        binary_path = source_dir + "/" + base_name + ".exe";
    }
#else
    QString binary_path = source_dir + "/aux/" + base_name;
    if (!QFileInfo::exists(binary_path)) {
        binary_path = source_dir + "/" + base_name;
    }
#endif

    if (!QFileInfo::exists(binary_path)) {
        output_panel_->AppendOutput(
            "[Warning] Compiled binary not found at: " + binary_path);
        output_panel_->AppendOutput(
            "[Note] For interpreted languages, runtime execution is delegated "
            "to the language runtime.");
        status_message_->setText("Binary not found — check output");
        action_stop_->setEnabled(false);
        action_compile_run_->setEnabled(true);
        return;
    }

    last_compiled_binary_ = binary_path;

    // Clean up any previous run process
    if (run_process_) {
        run_process_->kill();
        run_process_->waitForFinished(2000);
        run_process_->deleteLater();
        run_process_ = nullptr;
    }

    run_process_ = new QProcess(this);
    run_process_->setWorkingDirectory(source_dir);
    run_process_->setProcessChannelMode(QProcess::MergedChannels);

    connect(run_process_, &QProcess::readyReadStandardOutput,
            this, [this]() {
        if (!run_process_) return;
        QString out = QString::fromUtf8(run_process_->readAllStandardOutput());
        output_panel_->AppendOutput(out);
    });

    connect(run_process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exit_code, QProcess::ExitStatus exit_status) {
        QString status_str = (exit_status == QProcess::CrashExit)
                                 ? "crashed"
                                 : QString("exited with code %1").arg(exit_code);
        output_panel_->AppendOutput(
            QString("\n--- Program %1 ---").arg(status_str));

        if (exit_code == 0 && exit_status == QProcess::NormalExit) {
            status_message_->setText("Program finished successfully");
        } else {
            status_message_->setText("Program " + status_str);
        }

        action_stop_->setEnabled(false);
        action_compile_run_->setEnabled(true);
        run_process_->deleteLater();
        run_process_ = nullptr;
    });

    connect(run_process_, &QProcess::errorOccurred,
            this, [this](QProcess::ProcessError error) {
        QString msg;
        switch (error) {
        case QProcess::FailedToStart: msg = "Failed to start"; break;
        case QProcess::Crashed:       msg = "Process crashed"; break;
        case QProcess::Timedout:      msg = "Timed out"; break;
        default:                      msg = "Unknown error"; break;
        }
        output_panel_->AppendOutput("[Error] " + msg);
        status_message_->setText("Run error: " + msg);
        action_stop_->setEnabled(false);
        action_compile_run_->setEnabled(true);
    });

    // Also set executable and working dir for the debug panel
    debug_panel_->SetExecutable(binary_path);
    debug_panel_->SetWorkingDirectory(source_dir);

    output_panel_->AppendOutput("\n>>> Running: " + binary_path + "\n");
    run_process_->start(binary_path, QStringList());
}

void MainWindow::AnalyzeCode() {
    CodeEditor *editor = CurrentEditor();
    if (!editor) return;

    int index = editor_tabs_->currentIndex();
    auto it = tab_info_.find(index);
    if (it == tab_info_.end()) return;

    std::string source = editor->toPlainText().toStdString();
    std::string language = it->second.language.toStdString();
    QString filename_qt = it->second.file_path.isEmpty()
                              ? editor_tabs_->tabText(index)
                              : QFileInfo(it->second.file_path).fileName();
    if (filename_qt.startsWith("* ")) {
        filename_qt = filename_qt.mid(2);
    }
    std::string filename = filename_qt.toStdString();

    auto diagnostics = compiler_service_->Analyze(source, language, filename);

    output_panel_->ShowDiagnostics(
        diagnostics,
        it->second.file_path);

    // Push diagnostics to the editor for squiggly underlines and inline hints
    editor->SetDiagnostics(diagnostics);

    int errors = 0;
    int warnings = 0;
    for (const auto &d : diagnostics) {
        if (d.severity == "error") ++errors;
        else if (d.severity == "warning") ++warnings;
    }

    status_message_->setText(
        QString("Analysis: %1 error(s), %2 warning(s)")
            .arg(errors).arg(warnings));
}

void MainWindow::StopCompilation() {
    bool stopped_something = false;

    // Stop running program if any
    if (run_process_ && run_process_->state() != QProcess::NotRunning) {
        output_panel_->AppendOutput("\n--- Stopping program ---");
        run_process_->kill();
        run_process_->waitForFinished(3000);
        stopped_something = true;
    }

    // Also stop any active build
    if (build_panel_ && build_panel_->IsBuilding()) {
        build_panel_->OnStopBuild();
        stopped_something = true;
    }

    if (stopped_something) {
        status_message_->setText("Stopped");
    } else {
        status_message_->setText("Nothing to stop");
    }

    action_stop_->setEnabled(false);
    action_compile_run_->setEnabled(true);
}

// ============================================================================
// View Actions
// ============================================================================

void MainWindow::ToggleFileBrowser() {
    file_browser_->setVisible(!file_browser_->isVisible());
    UpdateViewActionChecks();
}

void MainWindow::ToggleOutputPanel() {
    panel_manager_->TogglePanel("output");
    UpdateViewActionChecks();
}

void MainWindow::ToggleTerminal() {
    panel_manager_->TogglePanel("terminal");
    UpdateViewActionChecks();

    // Focus the active terminal.
    if (panel_manager_->IsPanelActive("terminal") && terminal_tabs_->count() > 0) {
        auto *terminal = qobject_cast<TerminalWidget *>(terminal_tabs_->currentWidget());
        if (terminal) {
            terminal->setFocus();
        }
    }
}

void MainWindow::ToggleToolBar() {
    main_toolbar_->setVisible(!main_toolbar_->isVisible());
    QSettings settings("PolyglotCompiler", "IDE");
    settings.setValue("appearance/show_toolbar", main_toolbar_->isVisible());
    UpdateViewActionChecks();
}

void MainWindow::ToggleStatusBar() {
    statusBar()->setVisible(!statusBar()->isVisible());
    QSettings settings("PolyglotCompiler", "IDE");
    settings.setValue("appearance/show_statusbar", statusBar()->isVisible());
    UpdateViewActionChecks();
}

void MainWindow::ToggleLineNumbers() {
    CodeEditor *editor = CurrentEditor();
    if (editor) {
        editor->SetLineNumbersVisible(!editor->LineNumbersVisible());
        action_toggle_linenumbers_->setChecked(editor->LineNumbersVisible());
        QSettings settings("PolyglotCompiler", "IDE");
        settings.setValue("editor/show_line_numbers", editor->LineNumbersVisible());
    }
}

void MainWindow::ToggleWordWrap() {
    CodeEditor *editor = CurrentEditor();
    if (editor) {
        bool wrap = editor->lineWrapMode() == QPlainTextEdit::NoWrap;
        editor->setLineWrapMode(wrap ? QPlainTextEdit::WidgetWidth
                                     : QPlainTextEdit::NoWrap);
        action_toggle_wordwrap_->setChecked(wrap);
        QSettings settings("PolyglotCompiler", "IDE");
        settings.setValue("editor/word_wrap", wrap);
    }
}

void MainWindow::ZoomIn() {
    if (auto *e = CurrentEditor()) e->ZoomIn();
}

void MainWindow::ZoomOut() {
    if (auto *e = CurrentEditor()) e->ZoomOut();
}

void MainWindow::ZoomReset() {
    if (auto *e = CurrentEditor()) e->ZoomReset();
}

// ============================================================================
// Terminal Actions
// ============================================================================

void MainWindow::NewTerminal() {
    auto *terminal = new TerminalWidget(terminal_tabs_);
    QString title = QString("Terminal %1").arg(next_terminal_id_++);

    // Set working directory to the file browser root if available.
    if (file_browser_) {
        QString root = file_browser_->RootPath();
        if (!root.isEmpty()) {
            terminal->SetWorkingDirectory(root);
        }
    }

    int idx = terminal_tabs_->addTab(terminal, title);
    terminal_tabs_->setCurrentIndex(idx);

    // Update tab title when the terminal reports a title change.
    connect(terminal, &TerminalWidget::TitleChanged,
            this, [this, terminal](const QString &new_title) {
        int i = terminal_tabs_->indexOf(terminal);
        if (i >= 0) {
            terminal_tabs_->setTabText(i, new_title);
        }
    });

    // When the shell finishes, mark the tab.
    connect(terminal, &TerminalWidget::ShellFinished,
            this, [this, terminal](int exit_code) {
        int i = terminal_tabs_->indexOf(terminal);
        if (i >= 0) {
            terminal_tabs_->setTabText(
                i, terminal_tabs_->tabText(i) + QString(" [exited %1]").arg(exit_code));
        }
    });

    // Ensure bottom panel is visible and showing the terminal.
    panel_manager_->ShowPanel("terminal");
    UpdateViewActionChecks();
    terminal->setFocus();
}

void MainWindow::ClearTerminal() {
    auto *terminal = qobject_cast<TerminalWidget *>(terminal_tabs_->currentWidget());
    if (terminal) {
        terminal->ClearOutput();
    }
}

void MainWindow::RestartTerminal() {
    auto *terminal = qobject_cast<TerminalWidget *>(terminal_tabs_->currentWidget());
    if (terminal) {
        terminal->RestartShell();
    }
}

void MainWindow::CloseTerminalTab(int index) {
    auto *terminal = qobject_cast<TerminalWidget *>(terminal_tabs_->widget(index));
    if (terminal) {
        terminal->StopShell();
    }
    terminal_tabs_->removeTab(index);
    if (terminal) {
        terminal->deleteLater();
    }
    // Closing the last terminal tab must not hide the entire bottom panel,
    // because the bottom panel also hosts Output/Build/Git/Debug/Topology.
    // Hiding it here used to persist `view/show_bottom_panel = false`,
    // making the next launch start with no bottom tabs visible at all.
    if (terminal_tabs_->count() == 0
        && bottom_tabs_->currentWidget() == terminal_tabs_
        && bottom_tabs_->count() > 1) {
        // Switch to the first non-terminal panel so the bottom area stays
        // useful instead of showing an empty Terminal tab.
        for (int i = 0; i < bottom_tabs_->count(); ++i) {
            if (bottom_tabs_->widget(i) != terminal_tabs_) {
                bottom_tabs_->setCurrentIndex(i);
                break;
            }
        }
    }
    UpdateViewActionChecks();
}

// ============================================================================
// Settings
// ============================================================================

void MainWindow::OpenSettings() {
    if (!settings_dialog_) {
        settings_dialog_ = new SettingsDialog(this);
        connect(settings_dialog_, &SettingsDialog::SettingsChanged,
                this, &MainWindow::ApplySettings);
    }
    settings_dialog_->exec();
}

// ============================================================================
// Git Panel
// ============================================================================

void MainWindow::ToggleGitPanel() {
    panel_manager_->TogglePanel("git");
    UpdateViewActionChecks();

    // Set repo path from file browser if available
    if (panel_manager_->IsPanelActive("git") &&
        file_browser_ && !file_browser_->RootPath().isEmpty()) {
        git_panel_->SetRepoPath(file_browser_->RootPath());
    }
}

// ============================================================================
// Build Panel (CMake)
// ============================================================================

void MainWindow::ToggleBuildPanel() {
    panel_manager_->TogglePanel("build");
    UpdateViewActionChecks();
}

void MainWindow::CmakeConfigure() {
    // Set project path from file browser
    if (file_browser_ && !file_browser_->RootPath().isEmpty()) {
        build_panel_->SetProjectPath(file_browser_->RootPath());
    }
    panel_manager_->ShowPanel("build");
    UpdateViewActionChecks();
    build_panel_->OnConfigure();
}

void MainWindow::CmakeBuild() {
    if (file_browser_ && !file_browser_->RootPath().isEmpty()) {
        build_panel_->SetProjectPath(file_browser_->RootPath());
    }
    panel_manager_->ShowPanel("build");
    UpdateViewActionChecks();
    build_panel_->OnBuild();
}

// ============================================================================
// Debug Panel
// ============================================================================

void MainWindow::ToggleDebugPanel() {
    panel_manager_->TogglePanel("debug");
    UpdateViewActionChecks();
}

void MainWindow::DebugStart() {
    panel_manager_->ShowPanel("debug");
    UpdateViewActionChecks();
    debug_panel_->StartDebug();
}

void MainWindow::DebugStop() {
    debug_panel_->StopDebug();
}

void MainWindow::DebugStepOver() {
    debug_panel_->StepOver();
}

void MainWindow::DebugStepInto() {
    debug_panel_->StepInto();
}

void MainWindow::DebugStepOut() {
    debug_panel_->StepOut();
}

// ============================================================================
// Topology Panel
// ============================================================================

void MainWindow::ToggleTopologyPanel() {
    panel_manager_->TogglePanel("topology");
    UpdateViewActionChecks();
}

void MainWindow::OpenTopologyForCurrentFile() {
    CodeEditor *editor = CurrentEditor();
    if (!editor) return;

    int idx = editor_tabs_->currentIndex();
    auto it = tab_info_.find(idx);
    if (it == tab_info_.end() || it->second.file_path.isEmpty()) return;

    // Only .ploy files are supported
    if (!it->second.file_path.endsWith(".ploy", Qt::CaseInsensitive)) {
        status_message_->setText("Topology view only supports .ploy files");
        return;
    }

    panel_manager_->ShowPanel("topology");
    UpdateViewActionChecks();
    topology_panel_->LoadFromFile(it->second.file_path);
}

// ============================================================================
// Help Actions
// ============================================================================

void MainWindow::ShowAbout() {
    QMessageBox::about(this, "About " POLYGLOT_IDE_NAME,
        "<h2>" POLYGLOT_IDE_NAME "</h2>"
        "<p>Version " POLYGLOT_VERSION_STRING "</p>"
        "<p>A cross-language compiler IDE supporting Ploy, C++, Python, "
        "Rust, Java, and C#.</p>"
        "<p>Built with Qt " QT_VERSION_STR " and the " POLYGLOT_PROJECT_NAME " toolchain.</p>"
        "<p>&copy; 2026 " POLYGLOT_PROJECT_NAME " Project</p>");
}

void MainWindow::ShowAboutQt() {
    QMessageBox::aboutQt(this, "About Qt");
}

void MainWindow::ShowShortcuts() {
    QMessageBox::information(this, "Keyboard Shortcuts",
        "<table cellpadding='4'>"
        "<tr><td><b>Ctrl+N</b></td><td>New File</td></tr>"
        "<tr><td><b>Ctrl+O</b></td><td>Open File</td></tr>"
        "<tr><td><b>Ctrl+S</b></td><td>Save</td></tr>"
        "<tr><td><b>Ctrl+Shift+S</b></td><td>Save All</td></tr>"
        "<tr><td><b>Ctrl+W</b></td><td>Close Tab</td></tr>"
        "<tr><td><b>Ctrl+Z</b></td><td>Undo</td></tr>"
        "<tr><td><b>Ctrl+Y</b></td><td>Redo</td></tr>"
        "<tr><td><b>Ctrl+F</b></td><td>Find</td></tr>"
        "<tr><td><b>Ctrl+H</b></td><td>Replace</td></tr>"
        "<tr><td><b>Ctrl+G</b></td><td>Go to Line</td></tr>"
        "<tr><td><b>Ctrl+Shift+B</b></td><td>Compile</td></tr>"
        "<tr><td><b>F5</b></td><td>Compile &amp; Run</td></tr>"
        "<tr><td><b>Ctrl+Shift+A</b></td><td>Analyze</td></tr>"
        "<tr><td><b>Ctrl+B</b></td><td>Toggle Explorer</td></tr>"
        "<tr><td><b>Ctrl+J</b></td><td>Toggle Output</td></tr>"
        "<tr><td><b>Ctrl+`</b></td><td>Toggle Terminal</td></tr>"
        "<tr><td><b>Ctrl+Shift+`</b></td><td>New Terminal</td></tr>"
        "<tr><td><b>Ctrl++</b></td><td>Zoom In</td></tr>"
        "<tr><td><b>Ctrl+-</b></td><td>Zoom Out</td></tr>"
        "<tr><td><b>Ctrl+0</b></td><td>Zoom Reset</td></tr>"
        "<tr><td><b>Ctrl+,</b></td><td>Settings</td></tr>"
        "<tr><td><b>Ctrl+Shift+G</b></td><td>Toggle Git Panel</td></tr>"
        "<tr><td><b>F9</b></td><td>Start Debugging</td></tr>"
        "<tr><td><b>F10</b></td><td>Step Over</td></tr>"
        "<tr><td><b>F11</b></td><td>Step Into</td></tr>"
        "<tr><td><b>Shift+F11</b></td><td>Step Out</td></tr>"
        "</table>");
}

// ============================================================================
// Tab Changed
// ============================================================================

void MainWindow::OnTabChanged(int index) {
    if (index < 0) return;

    auto it = tab_info_.find(index);
    if (it != tab_info_.end()) {
        UpdateLanguageCombo(it->second.language);
        status_language_->setText(it->second.language);
    }

    if (auto *editor = EditorAt(index)) {
        action_toggle_linenumbers_->setChecked(editor->LineNumbersVisible());
        action_toggle_wordwrap_->setChecked(
            editor->lineWrapMode() != QPlainTextEdit::NoWrap);
    }

    UpdateViewActionChecks();
    OnCursorPositionChanged();
}

// ============================================================================
// Editor Modified — trigger debounced analysis
// ============================================================================

void MainWindow::OnEditorModified() {
    if (realtime_analysis_enabled_ && analysis_timer_) {
        analysis_timer_->start(); // restarts the timer
    }
}

// ============================================================================
// Cursor Position Changed
// ============================================================================

void MainWindow::OnCursorPositionChanged() {
    CodeEditor *editor = CurrentEditor();
    if (!editor) return;

    QTextCursor cursor = editor->textCursor();
    int line = cursor.blockNumber() + 1;
    int col = cursor.columnNumber() + 1;
    status_position_->setText(QString("Ln %1, Col %2").arg(line).arg(col));
}

// ============================================================================
// File Browser — file activated
// ============================================================================

void MainWindow::OnFileActivated(const QString &path) {
    OpenFileInTab(path);
}

// ============================================================================
// Analysis Timer Timeout — run analysis on current editor
// ============================================================================

void MainWindow::OnAnalysisTimerTimeout() {
    if (!realtime_analysis_enabled_) return;

    CodeEditor *editor = CurrentEditor();
    if (!editor) return;

    int index = editor_tabs_->currentIndex();
    auto it = tab_info_.find(index);
    if (it == tab_info_.end()) return;

    std::string source = editor->toPlainText().toStdString();
    std::string language = it->second.language.toStdString();
    QString filename_qt = it->second.file_path.isEmpty()
                              ? editor_tabs_->tabText(index)
                              : QFileInfo(it->second.file_path).fileName();
    if (filename_qt.startsWith("* ")) {
        filename_qt = filename_qt.mid(2);
    }
    std::string filename = filename_qt.toStdString();

    auto diagnostics = compiler_service_->Analyze(source, language, filename);
    output_panel_->ShowDiagnostics(
        diagnostics,
        it->second.file_path);

    // Push diagnostics to the editor for squiggly underlines and inline hints
    editor->SetDiagnostics(diagnostics);
}

// ============================================================================
// Language Combo Changed
// ============================================================================

void MainWindow::OnLanguageChanged(int index) {
    static const QStringList lang_ids = {"ploy", "cpp", "python", "rust", "java", "csharp"};
    if (index < 0 || index >= lang_ids.size()) return;

    QString language = lang_ids[index];
    int tab_index = editor_tabs_->currentIndex();
    auto it = tab_info_.find(tab_index);
    if (it != tab_info_.end()) {
        it->second.language = language;
        CodeEditor *editor = EditorAt(tab_index);
        if (editor) {
            AttachHighlighter(editor, language);
            editor->SetLanguage(language.toStdString());
        }
        status_language_->setText(language);
    }
}

void MainWindow::OnEditorTabMoved(int from, int to) {
    if (from == to) return;

    std::unordered_map<int, TabInfo> remapped;
    remapped.reserve(tab_info_.size());

    for (auto &[old_index, info] : tab_info_) {
        int new_index = old_index;

        if (old_index == from) {
            new_index = to;
        } else if (from < to && old_index > from && old_index <= to) {
            new_index = old_index - 1;
        } else if (from > to && old_index >= to && old_index < from) {
            new_index = old_index + 1;
        }

        remapped.emplace(new_index, std::move(info));
    }

    tab_info_ = std::move(remapped);
}

QString MainWindow::LanguageToExtension(const QString &language) const {
    if (language == "cpp") return "cpp";
    if (language == "python") return "py";
    if (language == "rust") return "rs";
    if (language == "java") return "java";
    if (language == "csharp") return "cs";
    return "ploy";
}

void MainWindow::ApplyEditorSettings(CodeEditor *editor) {
    if (!editor) return;

    QSettings settings("PolyglotCompiler", "IDE");

    const int tab_width = qBound(1, settings.value("editor/tab_width", 4).toInt(), 16);
    const bool insert_spaces = settings.value("editor/insert_spaces", true).toBool();
    const bool auto_indent = settings.value("editor/auto_indent", true).toBool();
    const bool show_line_numbers = settings.value("editor/show_line_numbers", true).toBool();
    const bool highlight_current_line =
        settings.value("editor/highlight_current_line", true).toBool();
    const bool bracket_matching = settings.value("editor/bracket_matching", true).toBool();
    const bool word_wrap = settings.value("editor/word_wrap", false).toBool();

    QFont editor_font = editor->font();
    editor_font.setFamily(
        settings.value("appearance/font_family", editor_font.family()).toString());
    editor_font.setPointSize(
        qBound(8, settings.value("appearance/font_size", 11).toInt(), 32));

    editor->SetEditorFont(editor_font);
    editor->SetTabWidth(tab_width);
    editor->SetInsertSpaces(insert_spaces);
    editor->SetAutoIndentEnabled(auto_indent);
    editor->SetLineNumbersVisible(show_line_numbers);
    editor->SetHighlightCurrentLineEnabled(highlight_current_line);
    editor->SetBracketMatchingEnabled(bracket_matching);
    editor->setLineWrapMode(word_wrap ? QPlainTextEdit::WidgetWidth
                                      : QPlainTextEdit::NoWrap);
}

void MainWindow::UpdateViewActionChecks() {
    if (action_toggle_browser_) {
        action_toggle_browser_->setChecked(file_browser_ && file_browser_->isVisible());
    }
    if (action_toggle_toolbar_) {
        action_toggle_toolbar_->setChecked(main_toolbar_ && main_toolbar_->isVisible());
    }
    if (action_toggle_statusbar_) {
        action_toggle_statusbar_->setChecked(statusBar() && statusBar()->isVisible());
    }

    // Use PanelManager for bottom panel state queries
    if (action_toggle_output_) {
        action_toggle_output_->setChecked(panel_manager_->IsPanelActive("output"));
    }
    if (action_toggle_terminal_) {
        action_toggle_terminal_->setChecked(panel_manager_->IsPanelActive("terminal"));
    }
    if (action_toggle_git_) {
        action_toggle_git_->setChecked(panel_manager_->IsPanelActive("git"));
    }
    if (action_toggle_build_) {
        action_toggle_build_->setChecked(panel_manager_->IsPanelActive("build"));
    }
    if (action_toggle_debug_) {
        action_toggle_debug_->setChecked(panel_manager_->IsPanelActive("debug"));
    }
    if (action_toggle_topology_) {
        action_toggle_topology_->setChecked(panel_manager_->IsPanelActive("topology"));
    }
}

void MainWindow::AutoSaveModifiedFiles() {
    int saved_count = 0;

    for (int i = 0; i < editor_tabs_->count(); ++i) {
        auto info_it = tab_info_.find(i);
        if (info_it == tab_info_.end() || info_it->second.file_path.isEmpty()) {
            continue;
        }

        CodeEditor *editor = EditorAt(i);
        if (!editor || !editor->document()->isModified()) {
            continue;
        }

        QFile file(info_it->second.file_path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            continue;
        }

        QTextStream stream(&file);
        stream << editor->toPlainText();
        file.close();

        editor->document()->setModified(false);
        UpdateTabTitle(i, false);
        ++saved_count;
    }

    if (saved_count > 0) {
        status_message_->setText(
            QString("Auto-saved %1 file%2")
                .arg(saved_count)
                .arg(saved_count == 1 ? "" : "s"));
    }
}

void MainWindow::ApplySettings() {
    QSettings settings("PolyglotCompiler", "IDE");

    // Global font preferences
    QFont app_font = QApplication::font();
    app_font.setFamily(settings.value("appearance/font_family", app_font.family()).toString());
    app_font.setPointSize(qBound(8, settings.value("appearance/font_size", 11).toInt(), 32));
    QApplication::setFont(app_font);

    const bool show_toolbar = settings.value("appearance/show_toolbar", true).toBool();
    const bool show_statusbar = settings.value("appearance/show_statusbar", true).toBool();
    const bool show_explorer = settings.value("view/show_file_browser", true).toBool();
    if (main_toolbar_) {
        main_toolbar_->setVisible(show_toolbar);
    }
    if (statusBar()) {
        statusBar()->setVisible(show_statusbar);
    }
    if (file_browser_) {
        file_browser_->setVisible(show_explorer);
    }

    // Theme
    static const QStringList theme_names = {"Dark", "Light", "Monokai", "Solarized Dark"};
    int theme_idx = qBound(0, settings.value("appearance/theme", 0).toInt(),
                           theme_names.size() - 1);
    ThemeManager::Instance().SetActiveTheme(theme_names[theme_idx]);
    ApplyTheme();

    // Notify plugins about the theme change
    polyglot::plugins::PluginManager::Instance().FireThemeChanged(
        theme_names[theme_idx].toStdString());

    // Compiler defaults
    const int default_target = qBound(
        0, settings.value("compiler/default_target", 0).toInt(),
        qMax(0, target_combo_->count() - 1));
    const int default_opt = qBound(
        0, settings.value("compiler/default_opt_level", 0).toInt(),
        qMax(0, opt_level_combo_->count() - 1));
    target_combo_->setCurrentIndex(default_target);
    opt_level_combo_->setCurrentIndex(default_opt);

    // Only apply default language directly when there is no open editor yet.
    if (editor_tabs_->count() == 0) {
        const int default_lang = qBound(
            0, settings.value("compiler/default_language", 0).toInt(),
            qMax(0, language_combo_->count() - 1));
        language_combo_->setCurrentIndex(default_lang);
    }

    // Real-time analysis
    realtime_analysis_enabled_ = settings.value("compiler/realtime_analysis", true).toBool();
    const int analysis_delay_ms = qBound(
        100, settings.value("compiler/analysis_delay", 500).toInt(), 5000);
    if (analysis_timer_) {
        analysis_timer_->setInterval(analysis_delay_ms);
        if (!realtime_analysis_enabled_) {
            analysis_timer_->stop();
        }
    }

    // Auto-save
    const bool auto_save = settings.value("editor/auto_save", false).toBool();
    const int auto_save_interval_s = qBound(
        5, settings.value("editor/auto_save_interval", 30).toInt(), 300);
    if (auto_save_timer_) {
        if (auto_save) {
            auto_save_timer_->start(auto_save_interval_s * 1000);
        } else {
            auto_save_timer_->stop();
        }
    }

    // Encoding indicator
    static const QStringList encodings = {
        "UTF-8", "UTF-16", "ASCII", "Latin-1", "Shift-JIS", "GB2312"};
    const int encoding_idx = qBound(
        0, settings.value("editor/encoding", 0).toInt(), encodings.size() - 1);
    status_encoding_->setText(encodings[encoding_idx]);

    for (int i = 0; i < editor_tabs_->count(); ++i) {
        ApplyEditorSettings(EditorAt(i));
    }

    if (CodeEditor *editor = CurrentEditor()) {
        action_toggle_linenumbers_->setChecked(editor->LineNumbersVisible());
        action_toggle_wordwrap_->setChecked(
            editor->lineWrapMode() != QPlainTextEdit::NoWrap);
    }

    // ── Propagate environment/build/debug paths to panels ────────────────
    // Build panel: set cmake path, build directory, generator, and jobs
    if (build_panel_) {
        QString cmake_path = settings.value("environment/cmake_path").toString();
        if (!cmake_path.isEmpty()) {
            build_panel_->SetCmakePath(cmake_path);
        }

        QString build_dir = settings.value("build/build_dir", "build").toString();
        if (!build_dir.isEmpty() && file_browser_ && !file_browser_->RootPath().isEmpty()) {
            QString full_build_dir = build_dir;
            if (QDir::isRelativePath(build_dir)) {
                full_build_dir = file_browser_->RootPath() + "/" + build_dir;
            }
            build_panel_->SetBuildDir(full_build_dir);
            build_panel_->SetProjectPath(file_browser_->RootPath());
        }
    }

    // Debug panel: set debugger path, working directory, break-on-entry
    if (debug_panel_) {
        QString debugger_path = settings.value("debug/debugger_path").toString();
        if (!debugger_path.isEmpty()) {
            debug_panel_->SetDebuggerPath(debugger_path);
        }

        if (file_browser_ && !file_browser_->RootPath().isEmpty()) {
            debug_panel_->SetWorkingDirectory(file_browser_->RootPath());
        }

        bool break_on_entry = settings.value("debug/break_on_entry", false).toBool();
        debug_panel_->SetBreakOnEntry(break_on_entry);
    }

    // Apply custom keybindings
    ApplyCustomKeybindings();

    UpdateViewActionChecks();
}

// ============================================================================
// Language Detection
// ============================================================================

void MainWindow::ApplyTheme() {
    const auto &tm = ThemeManager::Instance();
    const auto &tc = tm.Active();

    // Main window background
    setStyleSheet(QString("QMainWindow { background: %1; }").arg(tc.background.name()));

    // Menu bar
    menuBar()->setStyleSheet(tm.MenuBarStylesheet());

    // Toolbar
    if (main_toolbar_) {
        main_toolbar_->setStyleSheet(tm.ToolBarStylesheet());
    }

    // Status bar
    statusBar()->setStyleSheet(tm.StatusBarStylesheet());

    // Editor tabs
    if (editor_tabs_) {
        editor_tabs_->setStyleSheet(tm.TabWidgetStylesheet(false));
    }

    // Bottom tabs
    if (bottom_tabs_) {
        bottom_tabs_->setStyleSheet(tm.TabWidgetStylesheet(true));
    }

    // Terminal tabs
    if (terminal_tabs_) {
        terminal_tabs_->setStyleSheet(tm.TabWidgetStylesheet(false));
    }

    // Combo boxes in toolbar
    QString combo_ss = tm.ComboBoxStylesheet();
    if (language_combo_) language_combo_->setStyleSheet(combo_ss);
    if (target_combo_)   target_combo_->setStyleSheet(combo_ss);
    if (opt_level_combo_) opt_level_combo_->setStyleSheet(combo_ss);

    // Toolbar labels
    QString label_ss = QString("QLabel { color: %1; font-size: 12px; }").arg(tc.text.name());
    for (auto *w : main_toolbar_->findChildren<QLabel *>()) {
        w->setStyleSheet(label_ss);
    }

    // File browser
    if (file_browser_) {
        file_browser_->ApplyTheme();
    }

    // Output panel
    if (output_panel_) {
        output_panel_->ApplyTheme();
    }

    // Splitters
    QString splitter_ss = tm.SplitterStylesheet();
    if (main_splitter_) main_splitter_->setStyleSheet(splitter_ss);
    if (vertical_splitter_) vertical_splitter_->setStyleSheet(splitter_ss);

    // Editors — apply background and selection via stylesheet, text color
    // via QPalette so that QSyntaxHighlighter formats take precedence.
    for (int i = 0; i < editor_tabs_->count(); ++i) {
        if (auto *ed = EditorAt(i)) {
            ed->ApplyTheme();

            // Re-trigger syntax highlighting so colors update immediately
            auto it = tab_info_.find(i);
            if (it != tab_info_.end() && it->second.highlighter) {
                it->second.highlighter->rehighlight();
            }
        }
    }
}

void MainWindow::ApplyCustomKeybindings() {
    // Delegate keybinding management to ActionManager
    if (action_manager_) {
        action_manager_->LoadKeybindings();
    }
}

QString MainWindow::DetectLanguage(const QString &filename) const {
    QString ext = QFileInfo(filename).suffix().toLower();
    if (ext == "ploy") return "ploy";
    if (ext == "cpp" || ext == "c" || ext == "h" || ext == "hpp" ||
        ext == "cc" || ext == "cxx" || ext == "hh") return "cpp";
    if (ext == "py") return "python";
    if (ext == "rs") return "rust";
    if (ext == "java") return "java";
    if (ext == "cs") return "csharp";
    return "ploy"; // default
}

void MainWindow::UpdateLanguageCombo(const QString &language) {
    static const QStringList lang_ids = {"ploy", "cpp", "python", "rust", "java", "csharp"};
    int idx = lang_ids.indexOf(language);
    if (idx >= 0) {
        // Block signals to prevent recursive OnLanguageChanged
        language_combo_->blockSignals(true);
        language_combo_->setCurrentIndex(idx);
        language_combo_->blockSignals(false);
    }
}

// ============================================================================
// Syntax Highlighting
// ============================================================================

void MainWindow::AttachHighlighter(CodeEditor *editor, const QString &language) {
    int index = editor_tabs_->indexOf(editor);
    auto it = tab_info_.find(index);
    if (it == tab_info_.end()) return;

    // Delete old highlighter
    delete it->second.highlighter;

    auto *highlighter = new SyntaxHighlighter(
        editor->document(),
        compiler_service_.get(),
        language.toStdString());

    it->second.highlighter = highlighter;
}

// ============================================================================
// Output Helpers
// ============================================================================

void MainWindow::AppendOutput(const QString &text) {
    output_panel_->AppendOutput(text);
}

void MainWindow::ShowDiagnostics(const std::vector<DiagnosticInfo> &diagnostics,
                                 const QString &file) {
    output_panel_->ShowDiagnostics(diagnostics, file);
}

// ============================================================================
// Window State Persistence
// ============================================================================

void MainWindow::RestoreState() {
    QSettings settings("PolyglotCompiler", "IDE");
    if (settings.contains("geometry")) {
        restoreGeometry(settings.value("geometry").toByteArray());
    }
    if (settings.contains("window_state")) {
        QMainWindow::restoreState(settings.value("window_state").toByteArray());
    }
    if (settings.contains("layout/main_splitter")) {
        main_splitter_->restoreState(settings.value("layout/main_splitter").toByteArray());
    }
    if (settings.contains("layout/vertical_splitter")) {
        vertical_splitter_->restoreState(
            settings.value("layout/vertical_splitter").toByteArray());
    }

    // Safeguard: if restored splitter state collapsed the file browser to zero
    // width, reset to a sensible default so the explorer is always visible.
    {
        QList<int> sizes = main_splitter_->sizes();
        if (sizes.size() >= 2 && sizes[0] < 80) {
            main_splitter_->setSizes({250, qMax(sizes[1], 800)});
        }
    }
    // Same safeguard for the vertical splitter: keep the bottom panel area
    // (Output / Terminal / Build / Git / Debug / Topology) at a usable height
    // even if a previous session collapsed it.
    {
        QList<int> vsizes = vertical_splitter_->sizes();
        if (vsizes.size() >= 2 && vsizes[1] < 80) {
            vertical_splitter_->setSizes({qMax(vsizes[0], 500), 250});
        }
    }

    const QString root_path = settings.value("workspace/root_path").toString();
    if (!root_path.isEmpty() && QDir(root_path).exists()) {
        file_browser_->SetRootPath(root_path);
    }

    // Restore visibility flags for all major UI panels.
    // QMainWindow::restoreState() may override toolbar visibility, so we
    // must re-apply the persisted flags explicitly *after* restoreState().
    bool show_browser = settings.value("view/show_file_browser", true).toBool();
    bool show_bottom = settings.value("view/show_bottom_panel", true).toBool();
    const bool show_toolbar = settings.value("appearance/show_toolbar", true).toBool();
    const bool show_statusbar = settings.value("appearance/show_statusbar", true).toBool();

    // Recovery: previous versions had a bug where closing the last terminal
    // tab would persist `view/show_bottom_panel=false`, leaving subsequent
    // launches with no bottom panel.  If both the explorer and the bottom
    // panel ended up hidden, the default workspace is unusable, so restore
    // both to their default-visible state.  Users can still toggle either
    // off explicitly within a session.
    if (!show_browser && !show_bottom) {
        show_browser = true;
        show_bottom = true;
        settings.setValue("view/show_file_browser", true);
        settings.setValue("view/show_bottom_panel", true);
    }

    file_browser_->setVisible(show_browser);
    bottom_tabs_->setVisible(show_bottom);
    if (main_toolbar_)  main_toolbar_->setVisible(show_toolbar);
    if (statusBar())    statusBar()->setVisible(show_statusbar);

    const int bottom_index = settings.value("view/bottom_panel_index", 0).toInt();
    if (bottom_index >= 0 && bottom_index < bottom_tabs_->count()) {
        bottom_tabs_->setCurrentIndex(bottom_index);
    }

    UpdateViewActionChecks();
}

void MainWindow::SaveState() {
    QSettings settings("PolyglotCompiler", "IDE");
    settings.setValue("geometry", saveGeometry());
    settings.setValue("window_state", QMainWindow::saveState());
    settings.setValue("layout/main_splitter", main_splitter_->saveState());
    settings.setValue("layout/vertical_splitter", vertical_splitter_->saveState());
    settings.setValue("workspace/root_path", file_browser_->RootPath());
    settings.setValue("view/show_file_browser", file_browser_->isVisible());
    settings.setValue("view/show_bottom_panel", bottom_tabs_->isVisible());
    settings.setValue("view/bottom_panel_index", bottom_tabs_->currentIndex());
    settings.setValue("appearance/show_toolbar", main_toolbar_->isVisible());
    settings.setValue("appearance/show_statusbar", statusBar()->isVisible());
}

// ============================================================================
// Close Event
// ============================================================================

void MainWindow::closeEvent(QCloseEvent *event) {
    if (MaybeSaveAll()) {
        SaveState();
        // Shut down plugins before closing
        polyglot::plugins::PluginManager::Instance().UnloadAll();
        event->accept();
    } else {
        event->ignore();
    }
}

// ============================================================================
// Plugin System
// ============================================================================

void MainWindow::InitializePlugins() {
    auto &pm = polyglot::plugins::PluginManager::Instance();

    // Default plugin search paths:
    //   1. <app_dir>/plugins/
    //   2. <workspace>/plugins/  (if a folder is open)
    QString app_dir = QCoreApplication::applicationDirPath();
    pm.AddSearchPath((app_dir + "/plugins").toStdString());

#ifdef Q_OS_LINUX
    // XDG-compliant user plugin directory
    QString xdg_data = qEnvironmentVariable("XDG_DATA_HOME",
                           QDir::homePath() + "/.local/share");
    pm.AddSearchPath((xdg_data + "/polyglot/plugins").toStdString());
#elif defined(Q_OS_MACOS)
    pm.AddSearchPath(
        (QDir::homePath() + "/Library/Application Support/PolyglotCompiler/plugins")
            .toStdString());
#elif defined(Q_OS_WIN)
    QString appdata = qEnvironmentVariable("APPDATA");
    if (!appdata.isEmpty())
        pm.AddSearchPath((appdata + "/PolyglotCompiler/plugins").toStdString());
#endif

    // Set up log forwarding to the output panel
    pm.SetLogCallback([this](const std::string &plugin_id,
                             PolyglotLogLevel level,
                             const std::string &msg) {
        const char *prefix = "[INFO]";
        switch (level) {
            case POLYGLOT_LOG_DEBUG:   prefix = "[DEBUG]"; break;
            case POLYGLOT_LOG_INFO:    prefix = "[INFO]";  break;
            case POLYGLOT_LOG_WARNING: prefix = "[WARN]";  break;
            case POLYGLOT_LOG_ERROR:   prefix = "[ERROR]"; break;
        }
        QString text = QString("[plugin:%1] %2 %3")
                           .arg(QString::fromStdString(plugin_id))
                           .arg(prefix)
                           .arg(QString::fromStdString(msg));
        AppendOutput(text);
    });

    // Set up diagnostic forwarding to the output panel
    pm.SetDiagnosticCallback([this](const std::string &plugin_id,
                                    const PolyglotDiagnostic &diag) {
        const char *sev = "note";
        switch (diag.severity) {
            case POLYGLOT_DIAG_WARNING: sev = "warning"; break;
            case POLYGLOT_DIAG_ERROR:   sev = "error";   break;
            default: break;
        }
        QString text = QString("[plugin:%1] %2:%3:%4: %5: %6")
                           .arg(QString::fromStdString(plugin_id))
                           .arg(diag.file ? diag.file : "<unknown>")
                           .arg(diag.line)
                           .arg(diag.column)
                           .arg(sev)
                           .arg(diag.message ? diag.message : "");
        AppendOutput(text);
    });

    // Set up file-open forwarding
    pm.SetOpenFileCallback([this](const std::string &path, uint32_t line) {
        int tab = OpenFileInTab(QString::fromStdString(path));
        if (tab >= 0 && line > 0) {
            auto *editor = EditorAt(tab);
            if (editor) {
                QTextCursor cursor = editor->textCursor();
                cursor.movePosition(QTextCursor::Start);
                cursor.movePosition(QTextCursor::Down,
                                    QTextCursor::MoveAnchor,
                                    static_cast<int>(line) - 1);
                editor->setTextCursor(cursor);
            }
        }
    });

    // Set up file-type registration forwarding to update language detection
    pm.SetFileTypeRegisteredCallback([this](const std::string &extension,
                                            const std::string &language) {
        AppendOutput(QString("[plugin] Registered file type: .%1 -> %2")
                         .arg(QString::fromStdString(extension))
                         .arg(QString::fromStdString(language)));
    });

    // Set up menu-item registration forwarding
    pm.SetMenuItemRegisteredCallback(
        [this](const std::string &plugin_id,
               const PolyglotMenuContribution &item) {
        if (!item.action_id || !item.label) return;

        // Register the plugin action through ActionManager
        QString action_id = QString::fromStdString(
            std::string(item.action_id));
        QString label = QString::fromStdString(std::string(item.label));
        QKeySequence shortcut;
        if (item.shortcut) {
            shortcut = QKeySequence(QString::fromStdString(
                std::string(item.shortcut)));
        }

        auto callback_fn = item.callback;
        auto &pm_ref = polyglot::plugins::PluginManager::Instance();
        action_manager_->RegisterPluginAction(
            QString::fromStdString(plugin_id),
            action_id, label, shortcut,
            [action_id, &pm_ref]() {
                pm_ref.ExecuteMenuAction(action_id.toStdString());
            });
    });

    // Discover and activate all plugins
    pm.DiscoverPlugins();

    for (const auto *info : pm.ListPlugins()) {
        if (info && info->id) {
            pm.ActivatePlugin(info->id);
        }
    }
}

} // namespace polyglot::tools::ui
