// settings_dialog.cpp — Settings dialog implementation.
//
// Full-featured preferences dialog with category navigation, persistent
// settings via QSettings, and Apply/OK/Cancel workflow.

#include "tools/ui/common/include/settings_dialog.h"
#include "tools/ui/common/include/theme_manager.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace polyglot::tools::ui {

// ============================================================================
// Construction / Destruction
// ============================================================================

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent) {
    setWindowTitle("Settings");
    resize(800, 600);
    setMinimumSize(640, 480);
    SetupUi();
    LoadSettings();
}

SettingsDialog::~SettingsDialog() = default;

// ============================================================================
// UI Setup
// ============================================================================

void SettingsDialog::SetupUi() {
    auto *main_layout = new QHBoxLayout();

    // Left: category list
    category_list_ = new QListWidget();
    category_list_->setFixedWidth(180);
    category_list_->setStyleSheet(ThemeManager::Instance().ListWidgetStylesheet());

    category_list_->addItem("Appearance");
    category_list_->addItem("Editor");
    category_list_->addItem("Compiler");
    category_list_->addItem("Environment");
    category_list_->addItem("Build");
    category_list_->addItem("Debug");
    category_list_->addItem("Key Bindings");

    connect(category_list_, &QListWidget::currentRowChanged,
            this, &SettingsDialog::OnCategoryChanged);

    // Right: stacked pages
    pages_ = new QStackedWidget();
    {
        const auto &tm = ThemeManager::Instance();
        pages_->setStyleSheet(
            tm.GroupBoxStylesheet() +
            tm.LabelStylesheet() +
            tm.LineEditStylesheet() +
            tm.SpinBoxStylesheet() +
            tm.ComboBoxStylesheet() +
            tm.CheckBoxStylesheet() +
            tm.PushButtonPrimaryStylesheet());
    }

    pages_->addWidget(CreateAppearancePage());
    pages_->addWidget(CreateEditorPage());
    pages_->addWidget(CreateCompilerPage());
    pages_->addWidget(CreateEnvironmentPage());
    pages_->addWidget(CreateBuildPage());
    pages_->addWidget(CreateDebugPage());
    pages_->addWidget(CreateKeybindingsPage());

    main_layout->addWidget(category_list_);
    main_layout->addWidget(pages_, 1);

    // Bottom button bar
    auto *outer_layout = new QVBoxLayout(this);
    outer_layout->addLayout(main_layout, 1);

    auto *button_box = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply |
        QDialogButtonBox::RestoreDefaults);
    button_box->setStyleSheet(ThemeManager::Instance().PushButtonStylesheet());

    connect(button_box->button(QDialogButtonBox::Ok), &QPushButton::clicked,
            this, &SettingsDialog::OnOk);
    connect(button_box->button(QDialogButtonBox::Cancel), &QPushButton::clicked,
            this, &QDialog::reject);
    connect(button_box->button(QDialogButtonBox::Apply), &QPushButton::clicked,
            this, &SettingsDialog::OnApply);
    connect(button_box->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked,
            this, &SettingsDialog::OnResetDefaults);

    outer_layout->addWidget(button_box);

    setStyleSheet(ThemeManager::Instance().DialogStylesheet());
    category_list_->setCurrentRow(0);
}

// ============================================================================
// Page Factories
// ============================================================================

static QWidget *WrapInScrollArea(QWidget *content) {
    auto *scroll = new QScrollArea();
    scroll->setWidget(content);
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet("QScrollArea { border: none; background: transparent; }");
    return scroll;
}

QWidget *SettingsDialog::CreateAppearancePage() {
    auto *page = new QWidget();
    auto *layout = new QVBoxLayout(page);

    auto *theme_group = new QGroupBox("Theme");
    auto *theme_layout = new QFormLayout(theme_group);
    theme_combo_ = new QComboBox();
    theme_combo_->addItems({"Dark (default)", "Light", "Monokai", "Solarized Dark"});
    theme_layout->addRow("Color Theme:", theme_combo_);
    layout->addWidget(theme_group);

    auto *font_group = new QGroupBox("Font");
    auto *font_layout = new QFormLayout(font_group);
    font_combo_ = new QFontComboBox();
    font_combo_->setCurrentFont(QFont("Menlo"));
    font_layout->addRow("Font Family:", font_combo_);
    font_size_spin_ = new QSpinBox();
    font_size_spin_->setRange(8, 32);
    font_size_spin_->setValue(13);
    font_layout->addRow("Font Size:", font_size_spin_);
    layout->addWidget(font_group);

    auto *ui_group = new QGroupBox("Window");
    auto *ui_layout = new QVBoxLayout(ui_group);
    show_toolbar_check_ = new QCheckBox("Show Toolbar");
    show_toolbar_check_->setChecked(true);
    ui_layout->addWidget(show_toolbar_check_);
    show_statusbar_check_ = new QCheckBox("Show Status Bar");
    show_statusbar_check_->setChecked(true);
    ui_layout->addWidget(show_statusbar_check_);
    layout->addWidget(ui_group);

    layout->addStretch();
    return WrapInScrollArea(page);
}

QWidget *SettingsDialog::CreateEditorPage() {
    auto *page = new QWidget();
    auto *layout = new QVBoxLayout(page);

    auto *indent_group = new QGroupBox("Indentation");
    auto *indent_layout = new QFormLayout(indent_group);
    tab_width_spin_ = new QSpinBox();
    tab_width_spin_->setRange(1, 16);
    tab_width_spin_->setValue(4);
    indent_layout->addRow("Tab Width:", tab_width_spin_);
    insert_spaces_check_ = new QCheckBox("Insert Spaces Instead of Tabs");
    insert_spaces_check_->setChecked(true);
    indent_layout->addRow(insert_spaces_check_);
    auto_indent_check_ = new QCheckBox("Auto Indent");
    auto_indent_check_->setChecked(true);
    indent_layout->addRow(auto_indent_check_);
    layout->addWidget(indent_group);

    auto *display_group = new QGroupBox("Display");
    auto *display_layout = new QVBoxLayout(display_group);
    show_line_numbers_check_ = new QCheckBox("Show Line Numbers");
    show_line_numbers_check_->setChecked(true);
    display_layout->addWidget(show_line_numbers_check_);
    highlight_current_line_check_ = new QCheckBox("Highlight Current Line");
    highlight_current_line_check_->setChecked(true);
    display_layout->addWidget(highlight_current_line_check_);
    bracket_matching_check_ = new QCheckBox("Bracket Matching");
    bracket_matching_check_->setChecked(true);
    display_layout->addWidget(bracket_matching_check_);
    word_wrap_check_ = new QCheckBox("Word Wrap");
    word_wrap_check_->setChecked(false);
    display_layout->addWidget(word_wrap_check_);
    layout->addWidget(display_group);

    auto *save_group = new QGroupBox("Auto Save");
    auto *save_layout = new QFormLayout(save_group);
    auto_save_check_ = new QCheckBox("Enable Auto Save");
    auto_save_check_->setChecked(false);
    save_layout->addRow(auto_save_check_);
    auto_save_interval_spin_ = new QSpinBox();
    auto_save_interval_spin_->setRange(5, 300);
    auto_save_interval_spin_->setValue(30);
    auto_save_interval_spin_->setSuffix(" seconds");
    save_layout->addRow("Interval:", auto_save_interval_spin_);
    layout->addWidget(save_group);

    auto *encoding_group = new QGroupBox("File");
    auto *encoding_layout = new QFormLayout(encoding_group);
    encoding_combo_ = new QComboBox();
    encoding_combo_->addItems({"UTF-8", "UTF-16", "ASCII", "Latin-1", "Shift-JIS", "GB2312"});
    encoding_layout->addRow("Default Encoding:", encoding_combo_);
    eol_combo_ = new QComboBox();
    eol_combo_->addItems({"LF (Unix/macOS)", "CRLF (Windows)", "CR (Classic macOS)"});
    encoding_layout->addRow("Line Ending:", eol_combo_);
    layout->addWidget(encoding_group);

    layout->addStretch();
    return WrapInScrollArea(page);
}

QWidget *SettingsDialog::CreateCompilerPage() {
    auto *page = new QWidget();
    auto *layout = new QVBoxLayout(page);

    auto *defaults_group = new QGroupBox("Default Compiler Settings");
    auto *defaults_layout = new QFormLayout(defaults_group);
    default_language_combo_ = new QComboBox();
    default_language_combo_->addItems({"Ploy", "C++", "Python", "Rust", "Java", "C#"});
    defaults_layout->addRow("Default Language:", default_language_combo_);
    default_target_combo_ = new QComboBox();
    default_target_combo_->addItems({"x86_64", "arm64", "wasm"});
    defaults_layout->addRow("Default Target:", default_target_combo_);
    default_opt_level_combo_ = new QComboBox();
    default_opt_level_combo_->addItems({"O0 (No Optimization)", "O1", "O2", "O3 (Maximum)"});
    defaults_layout->addRow("Optimization Level:", default_opt_level_combo_);
    layout->addWidget(defaults_group);

    auto *analysis_group = new QGroupBox("Real-time Analysis");
    auto *analysis_layout = new QFormLayout(analysis_group);
    realtime_analysis_check_ = new QCheckBox("Enable Real-time Analysis");
    realtime_analysis_check_->setChecked(true);
    analysis_layout->addRow(realtime_analysis_check_);
    analysis_delay_spin_ = new QSpinBox();
    analysis_delay_spin_->setRange(100, 5000);
    analysis_delay_spin_->setValue(500);
    analysis_delay_spin_->setSuffix(" ms");
    analysis_layout->addRow("Analysis Delay:", analysis_delay_spin_);
    layout->addWidget(analysis_group);

    auto *output_group = new QGroupBox("Output");
    auto *output_layout = new QVBoxLayout(output_group);
    show_warnings_check_ = new QCheckBox("Show Warnings");
    show_warnings_check_->setChecked(true);
    output_layout->addWidget(show_warnings_check_);
    verbose_output_check_ = new QCheckBox("Verbose Compiler Output");
    verbose_output_check_->setChecked(false);
    output_layout->addWidget(verbose_output_check_);
    layout->addWidget(output_group);

    layout->addStretch();
    return WrapInScrollArea(page);
}

QWidget *SettingsDialog::CreateEnvironmentPage() {
    auto *page = new QWidget();
    auto *layout = new QVBoxLayout(page);

    auto *paths_group = new QGroupBox("Tool Paths");
    auto *paths_layout = new QFormLayout(paths_group);

    auto add_path_row = [&](const QString &label, QLineEdit *&edit,
                            void (SettingsDialog::*browse_slot)()) {
        auto *row = new QHBoxLayout();
        edit = new QLineEdit();
        edit->setPlaceholderText("Auto-detect");
        row->addWidget(edit, 1);
        auto *btn = new QPushButton("Browse...");
        btn->setFixedWidth(80);
        connect(btn, &QPushButton::clicked, this, browse_slot);
        row->addWidget(btn);
        paths_layout->addRow(label, row);
    };

    add_path_row("CMake:", cmake_path_edit_, &SettingsDialog::OnBrowseCmakePath);
    add_path_row("Qt:", qt_path_edit_, &SettingsDialog::OnBrowseQtPath);
    add_path_row("Python:", python_path_edit_, &SettingsDialog::OnBrowsePythonPath);
    add_path_row("Rust (cargo):", rust_path_edit_, &SettingsDialog::OnBrowseRustPath);
    add_path_row("Java (JDK):", java_path_edit_, &SettingsDialog::OnBrowseJavaPath);
    add_path_row(".NET SDK:", dotnet_path_edit_, &SettingsDialog::OnBrowseDotnetPath);

    layout->addWidget(paths_group);
    layout->addStretch();
    return WrapInScrollArea(page);
}

QWidget *SettingsDialog::CreateBuildPage() {
    auto *page = new QWidget();
    auto *layout = new QVBoxLayout(page);

    auto *build_group = new QGroupBox("Build Configuration");
    auto *build_layout = new QFormLayout(build_group);

    auto *build_dir_row = new QHBoxLayout();
    build_dir_edit_ = new QLineEdit();
    build_dir_edit_->setPlaceholderText("build");
    build_dir_row->addWidget(build_dir_edit_, 1);
    auto *browse_btn = new QPushButton("Browse...");
    browse_btn->setFixedWidth(80);
    connect(browse_btn, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, "Select Build Directory");
        if (!dir.isEmpty()) build_dir_edit_->setText(dir);
    });
    build_dir_row->addWidget(browse_btn);
    build_layout->addRow("Build Directory:", build_dir_row);

    build_generator_combo_ = new QComboBox();
    build_generator_combo_->addItems({"Ninja", "Unix Makefiles", "Visual Studio 17 2022",
                                       "Xcode", "MinGW Makefiles"});
    build_layout->addRow("CMake Generator:", build_generator_combo_);

    layout->addWidget(build_group);

    auto *jobs_group = new QGroupBox("Parallelism");
    auto *jobs_layout = new QFormLayout(jobs_group);
    parallel_build_check_ = new QCheckBox("Enable Parallel Build");
    parallel_build_check_->setChecked(true);
    jobs_layout->addRow(parallel_build_check_);
    build_jobs_spin_ = new QSpinBox();
    build_jobs_spin_->setRange(1, 64);
    build_jobs_spin_->setValue(4);
    jobs_layout->addRow("Build Jobs:", build_jobs_spin_);
    layout->addWidget(jobs_group);

    auto *auto_group = new QGroupBox("Automation");
    auto *auto_layout = new QVBoxLayout(auto_group);
    build_on_save_check_ = new QCheckBox("Auto Build on Save");
    build_on_save_check_->setChecked(false);
    auto_layout->addWidget(build_on_save_check_);
    layout->addWidget(auto_group);

    layout->addStretch();
    return WrapInScrollArea(page);
}

QWidget *SettingsDialog::CreateDebugPage() {
    auto *page = new QWidget();
    auto *layout = new QVBoxLayout(page);

    auto *general_group = new QGroupBox("General");
    auto *general_layout = new QFormLayout(general_group);
    debugger_combo_ = new QComboBox();
    debugger_combo_->addItems({"Auto-detect", "lldb", "gdb"});
    general_layout->addRow("Debugger:", debugger_combo_);

    auto *dbg_path_row = new QHBoxLayout();
    debugger_path_edit_ = new QLineEdit();
    debugger_path_edit_->setPlaceholderText("Auto-detect");
    dbg_path_row->addWidget(debugger_path_edit_, 1);
    auto *dbg_browse = new QPushButton("Browse...");
    dbg_browse->setFixedWidth(80);
    connect(dbg_browse, &QPushButton::clicked, this, [this]() {
        QString path = QFileDialog::getOpenFileName(this, "Select Debugger Executable");
        if (!path.isEmpty()) debugger_path_edit_->setText(path);
    });
    dbg_path_row->addWidget(dbg_browse);
    general_layout->addRow("Debugger Path:", dbg_path_row);
    layout->addWidget(general_group);

    auto *behaviour_group = new QGroupBox("Behaviour");
    auto *behaviour_layout = new QVBoxLayout(behaviour_group);
    break_on_entry_check_ = new QCheckBox("Break on Program Entry");
    break_on_entry_check_->setChecked(false);
    behaviour_layout->addWidget(break_on_entry_check_);
    break_on_exception_check_ = new QCheckBox("Break on Unhandled Exceptions");
    break_on_exception_check_->setChecked(true);
    behaviour_layout->addWidget(break_on_exception_check_);
    show_disassembly_check_ = new QCheckBox("Show Disassembly View");
    show_disassembly_check_->setChecked(false);
    behaviour_layout->addWidget(show_disassembly_check_);
    layout->addWidget(behaviour_group);

    layout->addStretch();
    return WrapInScrollArea(page);
}

QWidget *SettingsDialog::CreateKeybindingsPage() {
    auto *page = new QWidget();
    auto *layout = new QVBoxLayout(page);

    PopulateDefaultKeybindings();
    LoadKeybindings();

    keybinding_tree_ = new QTreeWidget();
    keybinding_tree_->setHeaderLabels({"Action", "Shortcut", "Default"});
    keybinding_tree_->setRootIsDecorated(false);
    keybinding_tree_->setAlternatingRowColors(true);
    keybinding_tree_->setStyleSheet(
        ThemeManager::Instance().TreeWidgetStylesheet() +
        " QTreeWidget { alternate-background-color: " +
        ThemeManager::Instance().Active().surface_alt.name() + "; }");
    keybinding_tree_->setColumnWidth(0, 250);
    keybinding_tree_->setColumnWidth(1, 180);
    keybinding_tree_->header()->setStretchLastSection(true);

    // Populate tree from entries
    for (const auto &entry : keybinding_entries_) {
        auto *item = new QTreeWidgetItem({
            entry.display_name,
            entry.custom_shortcut.isEmpty()
                ? entry.default_shortcut.toString(QKeySequence::NativeText)
                : entry.custom_shortcut.toString(QKeySequence::NativeText),
            entry.default_shortcut.toString(QKeySequence::NativeText)
        });
        item->setData(0, Qt::UserRole, entry.action_id);
        // Mark customized entries
        if (!entry.custom_shortcut.isEmpty() &&
            entry.custom_shortcut != entry.default_shortcut) {
            item->setForeground(1, ThemeManager::Instance().Active().accent);
        }
        keybinding_tree_->addTopLevelItem(item);
    }
    layout->addWidget(keybinding_tree_, 1);

    // Editing row
    auto *edit_row = new QHBoxLayout();
    edit_row->setContentsMargins(4, 8, 4, 4);

    auto *edit_label = new QLabel("New shortcut:");
    edit_label->setStyleSheet(ThemeManager::Instance().LabelStylesheet());
    edit_row->addWidget(edit_label);

    shortcut_edit_ = new QKeySequenceEdit();
    shortcut_edit_->setStyleSheet(ThemeManager::Instance().LineEditStylesheet());
    edit_row->addWidget(shortcut_edit_, 1);

    apply_shortcut_button_ = new QPushButton("Apply");
    apply_shortcut_button_->setStyleSheet(ThemeManager::Instance().PushButtonPrimaryStylesheet());
    connect(apply_shortcut_button_, &QPushButton::clicked, this, [this]() {
        auto *item = keybinding_tree_->currentItem();
        if (!item) return;

        QKeySequence seq = shortcut_edit_->keySequence();
        QString action_id = item->data(0, Qt::UserRole).toString();

        for (auto &entry : keybinding_entries_) {
            if (entry.action_id == action_id) {
                entry.custom_shortcut = seq;
                item->setText(1, seq.toString(QKeySequence::NativeText));
                if (seq != entry.default_shortcut) {
                    item->setForeground(1, ThemeManager::Instance().Active().accent);
                } else {
                    item->setForeground(1, ThemeManager::Instance().Active().text);
                }
                break;
            }
        }
    });
    edit_row->addWidget(apply_shortcut_button_);

    reset_shortcut_button_ = new QPushButton("Reset");
    reset_shortcut_button_->setStyleSheet(ThemeManager::Instance().PushButtonStylesheet());
    connect(reset_shortcut_button_, &QPushButton::clicked, this, [this]() {
        auto *item = keybinding_tree_->currentItem();
        if (!item) return;

        QString action_id = item->data(0, Qt::UserRole).toString();
        for (auto &entry : keybinding_entries_) {
            if (entry.action_id == action_id) {
                entry.custom_shortcut = QKeySequence();
                item->setText(1, entry.default_shortcut.toString(QKeySequence::NativeText));
                item->setForeground(1, ThemeManager::Instance().Active().text);
                shortcut_edit_->setKeySequence(entry.default_shortcut);
                break;
            }
        }
    });
    edit_row->addWidget(reset_shortcut_button_);

    layout->addLayout(edit_row);

    // When selecting an item, load its current shortcut into the editor
    connect(keybinding_tree_, &QTreeWidget::currentItemChanged,
            this, [this](QTreeWidgetItem *current, QTreeWidgetItem *) {
        if (!current) return;
        QString action_id = current->data(0, Qt::UserRole).toString();
        for (const auto &entry : keybinding_entries_) {
            if (entry.action_id == action_id) {
                QKeySequence seq = entry.custom_shortcut.isEmpty()
                                       ? entry.default_shortcut
                                       : entry.custom_shortcut;
                shortcut_edit_->setKeySequence(seq);
                break;
            }
        }
    });

    return page;
}

void SettingsDialog::PopulateDefaultKeybindings() {
    keybinding_entries_.clear();

    struct Def { const char *id; const char *name; QKeySequence shortcut; };
    static const Def defs[] = {
        {"new_file",        "New File",          QKeySequence::New},
        {"open_file",       "Open File",         QKeySequence::Open},
        {"save",            "Save",              QKeySequence::Save},
        {"save_all",        "Save All",          QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S)},
        {"close_tab",       "Close Tab",         QKeySequence(Qt::CTRL | Qt::Key_W)},
        {"undo",            "Undo",              QKeySequence::Undo},
        {"redo",            "Redo",              QKeySequence::Redo},
        {"find",            "Find",              QKeySequence::Find},
        {"replace",         "Replace",           QKeySequence::Replace},
        {"goto_line",       "Go to Line",        QKeySequence(Qt::CTRL | Qt::Key_G)},
        {"compile",         "Compile",           QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B)},
        {"compile_run",     "Compile & Run",     QKeySequence(Qt::Key_F5)},
        {"analyze",         "Analyze",           QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A)},
        {"stop",            "Stop",              QKeySequence(Qt::SHIFT | Qt::Key_F5)},
        {"toggle_explorer", "Toggle Explorer",   QKeySequence(Qt::CTRL | Qt::Key_B)},
        {"toggle_output",   "Toggle Output",     QKeySequence(Qt::CTRL | Qt::Key_J)},
        {"toggle_terminal", "Toggle Terminal",   QKeySequence(Qt::CTRL | Qt::Key_QuoteLeft)},
        {"new_terminal",    "New Terminal",      QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_QuoteLeft)},
        {"zoom_in",         "Zoom In",           QKeySequence::ZoomIn},
        {"zoom_out",        "Zoom Out",          QKeySequence::ZoomOut},
        {"zoom_reset",      "Zoom Reset",        QKeySequence(Qt::CTRL | Qt::Key_0)},
        {"debug_start",     "Start Debugging",   QKeySequence(Qt::Key_F9)},
        {"debug_stop",      "Stop Debugging",    QKeySequence(Qt::SHIFT | Qt::Key_F5)},
        {"step_over",       "Step Over",         QKeySequence(Qt::Key_F10)},
        {"step_into",       "Step Into",         QKeySequence(Qt::Key_F11)},
        {"step_out",        "Step Out",          QKeySequence(Qt::SHIFT | Qt::Key_F11)},
        {"settings",        "Settings",          QKeySequence(Qt::CTRL | Qt::Key_Comma)},
        {"toggle_git",      "Toggle Git Panel",  QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G)},
    };

    for (const auto &d : defs) {
        KeyBindingEntry entry;
        entry.action_id = d.id;
        entry.display_name = d.name;
        entry.default_shortcut = d.shortcut;
        keybinding_entries_.push_back(entry);
    }
}

void SettingsDialog::LoadKeybindings() {
    QSettings s("PolyglotCompiler", "IDE");
    int count = s.beginReadArray("keybindings");
    for (int i = 0; i < count; ++i) {
        s.setArrayIndex(i);
        QString action_id = s.value("action").toString();
        QString seq_str = s.value("shortcut").toString();
        for (auto &entry : keybinding_entries_) {
            if (entry.action_id == action_id) {
                entry.custom_shortcut = QKeySequence(seq_str);
                break;
            }
        }
    }
    s.endArray();
}

void SettingsDialog::SaveKeybindings() {
    QSettings s("PolyglotCompiler", "IDE");
    s.beginWriteArray("keybindings");
    int idx = 0;
    for (const auto &entry : keybinding_entries_) {
        if (!entry.custom_shortcut.isEmpty() &&
            entry.custom_shortcut != entry.default_shortcut) {
            s.setArrayIndex(idx++);
            s.setValue("action", entry.action_id);
            s.setValue("shortcut", entry.custom_shortcut.toString());
        }
    }
    s.endArray();
}

// ============================================================================
// Load / Save Settings
// ============================================================================

void SettingsDialog::LoadSettings() {
    QSettings s("PolyglotCompiler", "IDE");

    // Appearance
    theme_combo_->setCurrentIndex(s.value("appearance/theme", 0).toInt());
    font_combo_->setCurrentFont(QFont(s.value("appearance/font_family", "Menlo").toString()));
    font_size_spin_->setValue(s.value("appearance/font_size", 13).toInt());
    show_toolbar_check_->setChecked(s.value("appearance/show_toolbar", true).toBool());
    show_statusbar_check_->setChecked(s.value("appearance/show_statusbar", true).toBool());

    // Editor
    tab_width_spin_->setValue(s.value("editor/tab_width", 4).toInt());
    insert_spaces_check_->setChecked(s.value("editor/insert_spaces", true).toBool());
    auto_indent_check_->setChecked(s.value("editor/auto_indent", true).toBool());
    show_line_numbers_check_->setChecked(s.value("editor/show_line_numbers", true).toBool());
    highlight_current_line_check_->setChecked(s.value("editor/highlight_current_line", true).toBool());
    bracket_matching_check_->setChecked(s.value("editor/bracket_matching", true).toBool());
    word_wrap_check_->setChecked(s.value("editor/word_wrap", false).toBool());
    auto_save_check_->setChecked(s.value("editor/auto_save", false).toBool());
    auto_save_interval_spin_->setValue(s.value("editor/auto_save_interval", 30).toInt());
    encoding_combo_->setCurrentIndex(s.value("editor/encoding", 0).toInt());
    eol_combo_->setCurrentIndex(s.value("editor/eol", 0).toInt());

    // Compiler
    default_language_combo_->setCurrentIndex(s.value("compiler/default_language", 0).toInt());
    default_target_combo_->setCurrentIndex(s.value("compiler/default_target", 0).toInt());
    default_opt_level_combo_->setCurrentIndex(s.value("compiler/default_opt_level", 0).toInt());
    realtime_analysis_check_->setChecked(s.value("compiler/realtime_analysis", true).toBool());
    analysis_delay_spin_->setValue(s.value("compiler/analysis_delay", 500).toInt());
    show_warnings_check_->setChecked(s.value("compiler/show_warnings", true).toBool());
    verbose_output_check_->setChecked(s.value("compiler/verbose_output", false).toBool());

    // Environment
    cmake_path_edit_->setText(s.value("environment/cmake_path").toString());
    qt_path_edit_->setText(s.value("environment/qt_path").toString());
    python_path_edit_->setText(s.value("environment/python_path").toString());
    rust_path_edit_->setText(s.value("environment/rust_path").toString());
    java_path_edit_->setText(s.value("environment/java_path").toString());
    dotnet_path_edit_->setText(s.value("environment/dotnet_path").toString());

    // Build
    build_dir_edit_->setText(s.value("build/build_dir", "build").toString());
    build_generator_combo_->setCurrentIndex(s.value("build/generator", 0).toInt());
    parallel_build_check_->setChecked(s.value("build/parallel_build", true).toBool());
    build_jobs_spin_->setValue(s.value("build/build_jobs", 4).toInt());
    build_on_save_check_->setChecked(s.value("build/build_on_save", false).toBool());

    // Debug
    debugger_combo_->setCurrentIndex(s.value("debug/debugger", 0).toInt());
    debugger_path_edit_->setText(s.value("debug/debugger_path").toString());
    break_on_entry_check_->setChecked(s.value("debug/break_on_entry", false).toBool());
    break_on_exception_check_->setChecked(s.value("debug/break_on_exception", true).toBool());
    show_disassembly_check_->setChecked(s.value("debug/show_disassembly", false).toBool());
}

void SettingsDialog::SaveSettings() {
    QSettings s("PolyglotCompiler", "IDE");

    // Appearance
    s.setValue("appearance/theme", theme_combo_->currentIndex());
    s.setValue("appearance/font_family", font_combo_->currentFont().family());
    s.setValue("appearance/font_size", font_size_spin_->value());
    s.setValue("appearance/show_toolbar", show_toolbar_check_->isChecked());
    s.setValue("appearance/show_statusbar", show_statusbar_check_->isChecked());

    // Editor
    s.setValue("editor/tab_width", tab_width_spin_->value());
    s.setValue("editor/insert_spaces", insert_spaces_check_->isChecked());
    s.setValue("editor/auto_indent", auto_indent_check_->isChecked());
    s.setValue("editor/show_line_numbers", show_line_numbers_check_->isChecked());
    s.setValue("editor/highlight_current_line", highlight_current_line_check_->isChecked());
    s.setValue("editor/bracket_matching", bracket_matching_check_->isChecked());
    s.setValue("editor/word_wrap", word_wrap_check_->isChecked());
    s.setValue("editor/auto_save", auto_save_check_->isChecked());
    s.setValue("editor/auto_save_interval", auto_save_interval_spin_->value());
    s.setValue("editor/encoding", encoding_combo_->currentIndex());
    s.setValue("editor/eol", eol_combo_->currentIndex());

    // Compiler
    s.setValue("compiler/default_language", default_language_combo_->currentIndex());
    s.setValue("compiler/default_target", default_target_combo_->currentIndex());
    s.setValue("compiler/default_opt_level", default_opt_level_combo_->currentIndex());
    s.setValue("compiler/realtime_analysis", realtime_analysis_check_->isChecked());
    s.setValue("compiler/analysis_delay", analysis_delay_spin_->value());
    s.setValue("compiler/show_warnings", show_warnings_check_->isChecked());
    s.setValue("compiler/verbose_output", verbose_output_check_->isChecked());

    // Environment
    s.setValue("environment/cmake_path", cmake_path_edit_->text());
    s.setValue("environment/qt_path", qt_path_edit_->text());
    s.setValue("environment/python_path", python_path_edit_->text());
    s.setValue("environment/rust_path", rust_path_edit_->text());
    s.setValue("environment/java_path", java_path_edit_->text());
    s.setValue("environment/dotnet_path", dotnet_path_edit_->text());

    // Build
    s.setValue("build/build_dir", build_dir_edit_->text());
    s.setValue("build/generator", build_generator_combo_->currentIndex());
    s.setValue("build/parallel_build", parallel_build_check_->isChecked());
    s.setValue("build/build_jobs", build_jobs_spin_->value());
    s.setValue("build/build_on_save", build_on_save_check_->isChecked());

    // Debug
    s.setValue("debug/debugger", debugger_combo_->currentIndex());
    s.setValue("debug/debugger_path", debugger_path_edit_->text());
    s.setValue("debug/break_on_entry", break_on_entry_check_->isChecked());
    s.setValue("debug/break_on_exception", break_on_exception_check_->isChecked());
    s.setValue("debug/show_disassembly", show_disassembly_check_->isChecked());
}

// ============================================================================
// Slots
// ============================================================================

void SettingsDialog::OnCategoryChanged(int row) {
    pages_->setCurrentIndex(row);
}

void SettingsDialog::OnApply() {
    SaveSettings();
    SaveKeybindings();
    emit SettingsChanged();
}

void SettingsDialog::OnOk() {
    SaveSettings();
    SaveKeybindings();
    emit SettingsChanged();
    accept();
}

void SettingsDialog::OnResetDefaults() {
    auto result = QMessageBox::question(
        this, "Reset to Defaults",
        "This will reset all settings to their default values. Continue?",
        QMessageBox::Yes | QMessageBox::No);
    if (result != QMessageBox::Yes) return;

    QSettings s("PolyglotCompiler", "IDE");
    s.clear();
    LoadSettings();
}

void SettingsDialog::OnBrowseCmakePath() {
    QString path = QFileDialog::getOpenFileName(this, "Select CMake Executable");
    if (!path.isEmpty()) cmake_path_edit_->setText(path);
}

void SettingsDialog::OnBrowseQtPath() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Qt Installation");
    if (!dir.isEmpty()) qt_path_edit_->setText(dir);
}

void SettingsDialog::OnBrowsePythonPath() {
    QString path = QFileDialog::getOpenFileName(this, "Select Python Executable");
    if (!path.isEmpty()) python_path_edit_->setText(path);
}

void SettingsDialog::OnBrowseRustPath() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Rust/Cargo Directory");
    if (!dir.isEmpty()) rust_path_edit_->setText(dir);
}

void SettingsDialog::OnBrowseJavaPath() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select JDK Home");
    if (!dir.isEmpty()) java_path_edit_->setText(dir);
}

void SettingsDialog::OnBrowseDotnetPath() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select .NET SDK Directory");
    if (!dir.isEmpty()) dotnet_path_edit_->setText(dir);
}

} // namespace polyglot::tools::ui
