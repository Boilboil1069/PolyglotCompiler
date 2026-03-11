// main_window.cpp — Main window implementation for the PolyglotCompiler IDE.
//
// Assembles the menu bar, tool bar, file browser, tabbed code editor,
// output panel, and status bar.  Integrates with CompilerService for
// real-time analysis, compilation, and auto-completion.

#include "tools/ui/include/main_window.h"
#include "tools/ui/include/code_editor.h"
#include "tools/ui/include/compiler_service.h"
#include "tools/ui/include/file_browser.h"
#include "tools/ui/include/output_panel.h"
#include "tools/ui/include/syntax_highlighter.h"

#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QSettings>
#include <QShortcut>
#include <QStyle>
#include <QTextStream>

namespace polyglot::tools::ui {

// ============================================================================
// Construction / Destruction
// ============================================================================

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("PolyglotCompiler IDE");
    resize(1400, 900);

    compiler_service_ = std::make_unique<CompilerService>();

    SetupCentralWidget();
    SetupDockWidgets();
    SetupMenuBar();
    SetupToolBar();
    SetupStatusBar();
    SetupConnections();
    SetupShortcuts();
    SetupAnalysisTimer();

    RestoreState();

    // Create an initial empty tab
    CreateNewTab("Untitled-1.ploy", "ploy");
}

MainWindow::~MainWindow() {
    SaveState();
}

// ============================================================================
// Central Widget — tabbed editor
// ============================================================================

void MainWindow::SetupCentralWidget() {
    // Main layout: file browser | (editor / output)
    main_splitter_ = new QSplitter(Qt::Horizontal, this);

    // Right side: editor on top, output on bottom
    vertical_splitter_ = new QSplitter(Qt::Vertical);

    editor_tabs_ = new QTabWidget();
    editor_tabs_->setTabsClosable(true);
    editor_tabs_->setMovable(true);
    editor_tabs_->setDocumentMode(true);
    editor_tabs_->setStyleSheet(
        "QTabWidget::pane { border: none; background: #1e1e1e; }"
        "QTabBar::tab { background: #2d2d2d; color: #969696; padding: 6px 12px; "
        "border: none; min-width: 100px; font-size: 12px; }"
        "QTabBar::tab:selected { background: #1e1e1e; color: #ffffff; "
        "border-top: 2px solid #007acc; }"
        "QTabBar::tab:hover { background: #383838; }");

    vertical_splitter_->addWidget(editor_tabs_);

    // Output panel will be added in SetupDockWidgets
    main_splitter_->addWidget(vertical_splitter_);

    // Set initial sizes (file browser : editor area)
    main_splitter_->setSizes({250, 1150});

    setCentralWidget(main_splitter_);
}

// ============================================================================
// Dock Widgets — file browser + output panel
// ============================================================================

void MainWindow::SetupDockWidgets() {
    // File browser on the left
    file_browser_ = new FileBrowser();
    main_splitter_->insertWidget(0, file_browser_);

    // Output panel at the bottom
    output_panel_ = new OutputPanel();
    vertical_splitter_->addWidget(output_panel_);

    // Set initial vertical sizes (editor : output)
    vertical_splitter_->setSizes({650, 250});
}

// ============================================================================
// Menu Bar
// ============================================================================

void MainWindow::SetupMenuBar() {
    QMenuBar *mb = menuBar();
    mb->setStyleSheet(
        "QMenuBar { background: #333333; color: #cccccc; padding: 2px; }"
        "QMenuBar::item { padding: 4px 10px; }"
        "QMenuBar::item:selected { background: #505050; }"
        "QMenu { background: #252526; color: #cccccc; border: 1px solid #454545; }"
        "QMenu::item { padding: 5px 30px 5px 20px; }"
        "QMenu::item:selected { background: #094771; }"
        "QMenu::separator { height: 1px; background: #454545; margin: 2px 10px; }");

    // ── File Menu ─────────────────────────────────────────────────────
    file_menu_ = mb->addMenu("&File");

    action_new_ = file_menu_->addAction("&New File");
    action_new_->setShortcut(QKeySequence::New);

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

    // ── Help Menu ─────────────────────────────────────────────────────
    help_menu_ = mb->addMenu("&Help");
    help_menu_->addAction("&About PolyglotCompiler IDE", this, &MainWindow::ShowAbout);
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
    main_toolbar_->setStyleSheet(
        "QToolBar { background: #333333; border: none; spacing: 4px; padding: 2px; }"
        "QToolButton { background: transparent; color: #cccccc; padding: 4px 8px; "
        "border-radius: 3px; font-size: 12px; }"
        "QToolButton:hover { background: #505050; }"
        "QToolButton:pressed { background: #094771; }");

    main_toolbar_->addAction(action_new_);
    main_toolbar_->addAction(action_open_);
    main_toolbar_->addAction(action_save_);
    main_toolbar_->addSeparator();

    // Language selector
    auto *lang_label = new QLabel("  Language: ", main_toolbar_);
    lang_label->setStyleSheet("QLabel { color: #cccccc; font-size: 12px; }");
    main_toolbar_->addWidget(lang_label);

    language_combo_ = new QComboBox(main_toolbar_);
    language_combo_->addItems({"Ploy", "C++", "Python", "Rust", "Java", "C#"});
    language_combo_->setCurrentIndex(0);
    language_combo_->setStyleSheet(
        "QComboBox { background: #3c3c3c; color: #cccccc; border: 1px solid #555; "
        "border-radius: 3px; padding: 2px 8px; min-width: 80px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background: #252526; color: #cccccc; "
        "selection-background-color: #094771; }");
    main_toolbar_->addWidget(language_combo_);

    main_toolbar_->addSeparator();

    // Target architecture
    auto *target_label = new QLabel("  Target: ", main_toolbar_);
    target_label->setStyleSheet("QLabel { color: #cccccc; font-size: 12px; }");
    main_toolbar_->addWidget(target_label);

    target_combo_ = new QComboBox(main_toolbar_);
    target_combo_->addItems({"x86_64", "arm64", "wasm"});
    target_combo_->setStyleSheet(language_combo_->styleSheet());
    main_toolbar_->addWidget(target_combo_);

    // Optimization level
    auto *opt_label = new QLabel("  Opt: ", main_toolbar_);
    opt_label->setStyleSheet("QLabel { color: #cccccc; font-size: 12px; }");
    main_toolbar_->addWidget(opt_label);

    opt_level_combo_ = new QComboBox(main_toolbar_);
    opt_level_combo_->addItems({"O0", "O1", "O2", "O3"});
    opt_level_combo_->setStyleSheet(language_combo_->styleSheet());
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
    sb->setStyleSheet(
        "QStatusBar { background: #007acc; color: #ffffff; font-size: 12px; }"
        "QStatusBar::item { border: none; }"
        "QLabel { color: #ffffff; padding: 0px 8px; }");

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

    // Tab management
    connect(editor_tabs_, &QTabWidget::currentChanged, this, &MainWindow::OnTabChanged);
    connect(editor_tabs_, &QTabWidget::tabCloseRequested, this, &MainWindow::CloseTab);

    // File browser
    connect(file_browser_, &FileBrowser::FileActivated, this, &MainWindow::OnFileActivated);

    // Output panel error click
    connect(output_panel_, &OutputPanel::ErrorClicked,
            this, [this](int line, int /*col*/, const QString &/*file*/) {
        CodeEditor *editor = CurrentEditor();
        if (!editor) return;
        QTextCursor cursor(editor->document()->findBlockByLineNumber(line - 1));
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
}

// ============================================================================
// File Actions
// ============================================================================

void MainWindow::NewFile() {
    QString title = QString("Untitled-%1.ploy").arg(next_untitled_id_++);
    CreateNewTab(title, "ploy");
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

    return index;
}

int MainWindow::CreateNewTab(const QString &title, const QString &language) {
    auto *editor = new CodeEditor();
    editor->setStyleSheet(
        "QPlainTextEdit { background: #1e1e1e; color: #d4d4d4; border: none; "
        "selection-background-color: #264f78; }");

    int index = editor_tabs_->addTab(editor, title);
    editor_tabs_->setCurrentIndex(index);

    TabInfo info;
    info.language = language;
    info.highlighter = nullptr;
    tab_info_[index] = info;

    AttachHighlighter(editor, language);
    UpdateLanguageCombo(language);

    // Connect modification signal
    connect(editor->document(), &QTextDocument::modificationChanged,
            this, [this, index](bool modified) {
        UpdateTabTitle(index, modified);
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
    std::string filename = editor_tabs_->tabText(index).toStdString();
    std::string target = target_combo_->currentText().toStdString();
    int opt = opt_level_combo_->currentIndex();

    output_panel_->AppendOutput(
        QString("Compiling %1 (%2, %3, O%4)...")
            .arg(editor_tabs_->tabText(index))
            .arg(it->second.language)
            .arg(target_combo_->currentText())
            .arg(opt));

    auto result = compiler_service_->Compile(source, language, filename, target, opt);

    output_panel_->AppendOutput(QString::fromStdString(result.output));
    output_panel_->AppendOutput(
        QString("Elapsed: %1 ms").arg(result.elapsed_ms, 0, 'f', 2));

    output_panel_->ShowDiagnostics(result.diagnostics);

    if (result.success) {
        status_message_->setText("Compilation successful");
    } else {
        status_message_->setText("Compilation failed");
    }
}

void MainWindow::CompileAndRun() {
    Compile();
    // Run step will be extended when the runtime integration is complete
    output_panel_->AppendOutput("[Note] Run step requires compiled binary output.");
}

void MainWindow::AnalyzeCode() {
    CodeEditor *editor = CurrentEditor();
    if (!editor) return;

    int index = editor_tabs_->currentIndex();
    auto it = tab_info_.find(index);
    if (it == tab_info_.end()) return;

    std::string source = editor->toPlainText().toStdString();
    std::string language = it->second.language.toStdString();
    std::string filename = editor_tabs_->tabText(index).toStdString();

    auto diagnostics = compiler_service_->Analyze(source, language, filename);

    output_panel_->ShowDiagnostics(diagnostics);

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
    // Placeholder for stopping background compilation
    status_message_->setText("Compilation stopped");
    action_stop_->setEnabled(false);
}

// ============================================================================
// View Actions
// ============================================================================

void MainWindow::ToggleFileBrowser() {
    file_browser_->setVisible(!file_browser_->isVisible());
    action_toggle_browser_->setChecked(file_browser_->isVisible());
}

void MainWindow::ToggleOutputPanel() {
    output_panel_->setVisible(!output_panel_->isVisible());
    action_toggle_output_->setChecked(output_panel_->isVisible());
}

void MainWindow::ToggleToolBar() {
    main_toolbar_->setVisible(!main_toolbar_->isVisible());
    action_toggle_toolbar_->setChecked(main_toolbar_->isVisible());
}

void MainWindow::ToggleStatusBar() {
    statusBar()->setVisible(!statusBar()->isVisible());
    action_toggle_statusbar_->setChecked(statusBar()->isVisible());
}

void MainWindow::ToggleLineNumbers() {
    CodeEditor *editor = CurrentEditor();
    if (editor) {
        editor->SetLineNumbersVisible(!editor->LineNumbersVisible());
        action_toggle_linenumbers_->setChecked(editor->LineNumbersVisible());
    }
}

void MainWindow::ToggleWordWrap() {
    CodeEditor *editor = CurrentEditor();
    if (editor) {
        bool wrap = editor->lineWrapMode() == QPlainTextEdit::NoWrap;
        editor->setLineWrapMode(wrap ? QPlainTextEdit::WidgetWidth
                                     : QPlainTextEdit::NoWrap);
        action_toggle_wordwrap_->setChecked(wrap);
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
// Help Actions
// ============================================================================

void MainWindow::ShowAbout() {
    QMessageBox::about(this, "About PolyglotCompiler IDE",
        "<h2>PolyglotCompiler IDE</h2>"
        "<p>Version 1.0.0</p>"
        "<p>A cross-language compiler IDE supporting Ploy, C++, Python, "
        "Rust, Java, and C#.</p>"
        "<p>Built with Qt " QT_VERSION_STR " and the PolyglotCompiler toolchain.</p>"
        "<p>&copy; 2026 PolyglotCompiler Project</p>");
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
        "<tr><td><b>Ctrl++</b></td><td>Zoom In</td></tr>"
        "<tr><td><b>Ctrl+-</b></td><td>Zoom Out</td></tr>"
        "<tr><td><b>Ctrl+0</b></td><td>Zoom Reset</td></tr>"
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

    OnCursorPositionChanged();
}

// ============================================================================
// Editor Modified — trigger debounced analysis
// ============================================================================

void MainWindow::OnEditorModified() {
    analysis_timer_->start(); // restarts the timer
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
    CodeEditor *editor = CurrentEditor();
    if (!editor) return;

    int index = editor_tabs_->currentIndex();
    auto it = tab_info_.find(index);
    if (it == tab_info_.end()) return;

    std::string source = editor->toPlainText().toStdString();
    std::string language = it->second.language.toStdString();
    std::string filename = editor_tabs_->tabText(index).toStdString();

    auto diagnostics = compiler_service_->Analyze(source, language, filename);
    output_panel_->ShowDiagnostics(diagnostics);
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
        }
        status_language_->setText(language);
    }
}

// ============================================================================
// Language Detection
// ============================================================================

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

void MainWindow::ShowDiagnostics(const std::vector<DiagnosticInfo> &diagnostics) {
    output_panel_->ShowDiagnostics(diagnostics);
}

// ============================================================================
// Window State Persistence
// ============================================================================

void MainWindow::RestoreState() {
    QSettings settings("PolyglotCompiler", "IDE");
    restoreGeometry(settings.value("geometry").toByteArray());
}

void MainWindow::SaveState() {
    QSettings settings("PolyglotCompiler", "IDE");
    settings.setValue("geometry", saveGeometry());
}

// ============================================================================
// Close Event
// ============================================================================

void MainWindow::closeEvent(QCloseEvent *event) {
    if (MaybeSaveAll()) {
        SaveState();
        event->accept();
    } else {
        event->ignore();
    }
}

} // namespace polyglot::tools::ui
