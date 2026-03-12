// settings_dialog.h — Settings dialog for the PolyglotCompiler IDE.
//
// Provides a full-featured settings page with categories for appearance,
// editor behaviour, compiler defaults, environment paths, and key bindings.
// Settings are persisted via QSettings.

#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFontComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>

namespace polyglot::tools::ui {

// ============================================================================
// SettingsDialog — modal preferences window
// ============================================================================

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
};

} // namespace polyglot::tools::ui
