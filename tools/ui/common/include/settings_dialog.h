/**
 * @file     settings_dialog.h
 * @brief    Settings dialog for the PolyglotCompiler IDE
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFontComboBox>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTreeWidget>

namespace polyglot::tools::ui {

// ============================================================================
// SettingsDialog — modal preferences window
// ============================================================================

/** @brief SettingsDialog class. */
class SettingsDialog : public QDialog {
    Q_OBJECT

  public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    ~SettingsDialog() override;

  signals:
    // Emitted when the user clicks Apply or OK so the main window can refresh.
    void SettingsChanged();

  private slots:
    void OnCategoryChanged(int row);
    void OnApply();
    void OnOk();
    void OnResetDefaults();
    void OnBrowseCmakePath();
    void OnBrowseQtPath();
    void OnBrowsePythonPath();
    void OnBrowseRustPath();
    void OnBrowseJavaPath();
    void OnBrowseDotnetPath();

  private:
    void SetupUi();
    QWidget *CreateAppearancePage();
    QWidget *CreateEditorPage();
    QWidget *CreateCompilerPage();
    QWidget *CreateEnvironmentPage();
    QWidget *CreateBuildPage();
    QWidget *CreateDebugPage();
    QWidget *CreateKeybindingsPage();
    QWidget *CreatePluginsPage();

    void LoadSettings();
    void SaveSettings();

    // Navigation
    QListWidget *category_list_{nullptr};
    QStackedWidget *pages_{nullptr};

    // ── Appearance settings ──────────────────────────────────────────────
    QComboBox *theme_combo_{nullptr};
    QFontComboBox *font_combo_{nullptr};
    QSpinBox *font_size_spin_{nullptr};
    QCheckBox *show_toolbar_check_{nullptr};
    QCheckBox *show_statusbar_check_{nullptr};
    QCheckBox *show_explorer_check_{nullptr};

    // ── Editor settings ──────────────────────────────────────────────────
    QSpinBox *tab_width_spin_{nullptr};
    QCheckBox *insert_spaces_check_{nullptr};
    QCheckBox *auto_indent_check_{nullptr};
    QCheckBox *show_line_numbers_check_{nullptr};
    QCheckBox *highlight_current_line_check_{nullptr};
    QCheckBox *bracket_matching_check_{nullptr};
    QCheckBox *word_wrap_check_{nullptr};
    QCheckBox *auto_save_check_{nullptr};
    QSpinBox *auto_save_interval_spin_{nullptr};
    QComboBox *encoding_combo_{nullptr};
    QComboBox *eol_combo_{nullptr};

    // ── Compiler settings ────────────────────────────────────────────────
    QComboBox *default_language_combo_{nullptr};
    QComboBox *default_target_combo_{nullptr};
    QComboBox *default_opt_level_combo_{nullptr};
    QCheckBox *realtime_analysis_check_{nullptr};
    QSpinBox *analysis_delay_spin_{nullptr};
    QCheckBox *show_warnings_check_{nullptr};
    QCheckBox *verbose_output_check_{nullptr};

    // ── Environment paths ────────────────────────────────────────────────
    QLineEdit *cmake_path_edit_{nullptr};
    QLineEdit *qt_path_edit_{nullptr};
    QLineEdit *python_path_edit_{nullptr};
    QLineEdit *rust_path_edit_{nullptr};
    QLineEdit *java_path_edit_{nullptr};
    QLineEdit *dotnet_path_edit_{nullptr};

    // ── Build settings ───────────────────────────────────────────────────
    QLineEdit *build_dir_edit_{nullptr};
    QComboBox *build_generator_combo_{nullptr};
    QCheckBox *parallel_build_check_{nullptr};
    QSpinBox *build_jobs_spin_{nullptr};
    QCheckBox *build_on_save_check_{nullptr};

    // ── Debug settings ───────────────────────────────────────────────────
    QCheckBox *break_on_entry_check_{nullptr};
    QCheckBox *break_on_exception_check_{nullptr};
    QCheckBox *show_disassembly_check_{nullptr};
    QComboBox *debugger_combo_{nullptr};
    QLineEdit *debugger_path_edit_{nullptr};

    // ── Key bindings ─────────────────────────────────────────────────────
    QTreeWidget *keybinding_tree_{nullptr};
    QKeySequenceEdit *shortcut_edit_{nullptr};
    QPushButton *apply_shortcut_button_{nullptr};
    QPushButton *reset_shortcut_button_{nullptr};

    // Stores user-customized shortcuts: action_id -> QKeySequence string
    /** @brief KeyBindingEntry data structure. */
    struct KeyBindingEntry {
        QString action_id;    // internal identifier
        QString display_name; // user-visible label
        QKeySequence default_shortcut;
        QKeySequence custom_shortcut;
    };
    std::vector<KeyBindingEntry> keybinding_entries_;

    // ── Plugin settings ──────────────────────────────────────────────────
    QTreeWidget *plugin_tree_{nullptr};
    QPushButton *plugin_enable_button_{nullptr};
    QPushButton *plugin_disable_button_{nullptr};
    QPushButton *plugin_browse_button_{nullptr};
    QLineEdit   *plugin_dir_edit_{nullptr};

    void PopulateDefaultKeybindings();
    void LoadKeybindings();
    void SaveKeybindings();
    void RefreshPluginList();
};

} // namespace polyglot::tools::ui
