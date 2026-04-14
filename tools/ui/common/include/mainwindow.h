/**
 * @file     mainwindow.h
 * @brief    Main application window for the PolyglotCompiler IDE
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <QAction>
#include <QComboBox>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QProcess>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

#include <memory>
#include <string>
#include <unordered_map>

namespace polyglot::tools::ui {

class ActionManager;
class BuildPanel;
class CodeEditor;
class CompilerService;
class DebugPanel;
class FileBrowser;
class GitPanel;
class OutputPanel;
class PanelManager;
class SettingsDialog;
class SyntaxHighlighter;
class TerminalWidget;
class TopologyPanel;

// ============================================================================
// MainWindow — top-level IDE window
// ============================================================================

/** @brief MainWindow class. */
class MainWindow : public QMainWindow {
    Q_OBJECT

  public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

  protected:
    void closeEvent(QCloseEvent *event) override;

  private slots:
    // File menu actions
    void NewFile();
    void NewFromTemplate();
    void NewFromTemplateInDir(const QString &parent_dir);
    void OpenFile();
    void OpenFolder();
    void Save();
    void SaveAs();
    void SaveAll();
    void CloseTab(int index);

    // Edit menu actions
    void Undo();
    void Redo();
    void Cut();
    void Copy();
    void Paste();
    void SelectAll();
    void Find();
    void Replace();
    void GoToLine();

    // Build menu actions
    void Compile();
    void CompileAndRun();
    void AnalyzeCode();
    void StopCompilation();

    // View menu actions
    void ToggleFileBrowser();
    void ToggleOutputPanel();
    void ToggleTerminal();
    void ToggleToolBar();
    void ToggleStatusBar();
    void ToggleLineNumbers();
    void ToggleWordWrap();
    void ZoomIn();
    void ZoomOut();
    void ZoomReset();

    // Settings
    void OpenSettings();

    // Git panel
    void ToggleGitPanel();

    // Build panel (CMake)
    void ToggleBuildPanel();
    void CmakeConfigure();
    void CmakeBuild();

    // Debug panel
    void ToggleDebugPanel();
    void DebugStart();
    void DebugStop();
    void DebugStepOver();
    void DebugStepInto();
    void DebugStepOut();

    // Topology panel
    void ToggleTopologyPanel();
    void OpenTopologyForCurrentFile();

    // Help menu actions
    void ShowAbout();
    void ShowAboutQt();
    void ShowShortcuts();

    // Terminal actions
    void NewTerminal();
    void ClearTerminal();
    void RestartTerminal();
    void CloseTerminalTab(int index);

    // Editor tab management
    void OnTabChanged(int index);
    void OnEditorModified();
    void OnCursorPositionChanged();

    // File browser interaction
    void OnFileActivated(const QString &path);

    // Real-time analysis
    void OnAnalysisTimerTimeout();

    // Language combo changed
    void OnLanguageChanged(int index);
    void OnEditorTabMoved(int from, int to);

  private:
    // UI construction helpers
    void SetupMenuBar();
    void SetupToolBar();
    void SetupStatusBar();
    void SetupCentralWidget();
    void SetupDockWidgets();
    void SetupConnections();
    void SetupShortcuts();
    void SetupAnalysisTimer();

    // Tab helpers
    int OpenFileInTab(const QString &path);
    int CreateNewTab(const QString &title, const QString &language);
    CodeEditor *CurrentEditor() const;
    CodeEditor *EditorAt(int index) const;
    void UpdateTabTitle(int index, bool modified);
    bool MaybeSave(int index);
    bool MaybeSaveAll();

    // Language detection
    QString DetectLanguage(const QString &filename) const;
    void UpdateLanguageCombo(const QString &language);

    // Apply syntax highlighter to an editor
    void AttachHighlighter(CodeEditor *editor, const QString &language);

    // Output helpers
    void AppendOutput(const QString &text);
    void ShowDiagnostics(const std::vector<struct DiagnosticInfo> &diagnostics,
                         const QString &file = QString());

    // Settings helpers
    void ApplySettings();
    void ApplyEditorSettings(CodeEditor *editor);
    void ApplyTheme();
    void ApplyCustomKeybindings();
    void UpdateViewActionChecks();
    void AutoSaveModifiedFiles();
    QString LanguageToExtension(const QString &language) const;

    // Restore/save window state
    void RestoreState();
    void SaveState();

    // Plugin system initialization and cleanup
    void InitializePlugins();

    // ── UI Components ────────────────────────────────────────────────────
    QTabWidget *editor_tabs_{nullptr};
    FileBrowser *file_browser_{nullptr};
    OutputPanel *output_panel_{nullptr};
    QTabWidget *bottom_tabs_{nullptr};       // container for output + terminal tabs
    QTabWidget *terminal_tabs_{nullptr};     // multiple terminal instances

    QSplitter *main_splitter_{nullptr};
    QSplitter *vertical_splitter_{nullptr};

    // ── Menu Bar ─────────────────────────────────────────────────────────
    QMenu *file_menu_{nullptr};
    QMenu *edit_menu_{nullptr};
    QMenu *view_menu_{nullptr};
    QMenu *build_menu_{nullptr};
    QMenu *debug_menu_{nullptr};
    QMenu *help_menu_{nullptr};

    // ── Tool Bar ─────────────────────────────────────────────────────────
    QToolBar *main_toolbar_{nullptr};
    QComboBox *language_combo_{nullptr};
    QComboBox *target_combo_{nullptr};
    QComboBox *opt_level_combo_{nullptr};

    // ── Status Bar ───────────────────────────────────────────────────────
    QLabel *status_position_{nullptr};
    QLabel *status_language_{nullptr};
    QLabel *status_encoding_{nullptr};
    QLabel *status_message_{nullptr};

    // ── Actions ──────────────────────────────────────────────────────────
    QAction *action_new_{nullptr};
    QAction *action_new_from_template_{nullptr};
    QAction *action_open_{nullptr};
    QAction *action_open_folder_{nullptr};
    QAction *action_save_{nullptr};
    QAction *action_save_as_{nullptr};
    QAction *action_save_all_{nullptr};
    QAction *action_close_tab_{nullptr};
    QAction *action_quit_{nullptr};

    QAction *action_undo_{nullptr};
    QAction *action_redo_{nullptr};
    QAction *action_cut_{nullptr};
    QAction *action_copy_{nullptr};
    QAction *action_paste_{nullptr};
    QAction *action_select_all_{nullptr};
    QAction *action_find_{nullptr};
    QAction *action_replace_{nullptr};
    QAction *action_goto_line_{nullptr};

    QAction *action_compile_{nullptr};
    QAction *action_compile_run_{nullptr};
    QAction *action_analyze_{nullptr};
    QAction *action_stop_{nullptr};

    QAction *action_toggle_browser_{nullptr};
    QAction *action_toggle_output_{nullptr};
    QAction *action_toggle_toolbar_{nullptr};
    QAction *action_toggle_statusbar_{nullptr};
    QAction *action_toggle_linenumbers_{nullptr};
    QAction *action_toggle_wordwrap_{nullptr};
    QAction *action_zoom_in_{nullptr};
    QAction *action_zoom_out_{nullptr};
    QAction *action_zoom_reset_{nullptr};

    QAction *action_toggle_terminal_{nullptr};
    QAction *action_new_terminal_{nullptr};
    QAction *action_clear_terminal_{nullptr};
    QAction *action_restart_terminal_{nullptr};

    // Settings / Git / Build / Debug actions
    QAction *action_settings_{nullptr};
    QAction *action_toggle_git_{nullptr};
    QAction *action_toggle_build_{nullptr};
    QAction *action_toggle_debug_{nullptr};
    QAction *action_cmake_configure_{nullptr};
    QAction *action_cmake_build_{nullptr};
    QAction *action_debug_start_{nullptr};
    QAction *action_debug_stop_{nullptr};
    QAction *action_debug_step_over_{nullptr};
    QAction *action_debug_step_into_{nullptr};
    QAction *action_debug_step_out_{nullptr};

    QAction *action_toggle_topology_{nullptr};
    QAction *action_open_topology_{nullptr};

    QMenu *terminal_menu_{nullptr};

    // ── Compiler Service ─────────────────────────────────────────────────
    std::unique_ptr<CompilerService> compiler_service_;
    // ── Run Process (for Compile & Run) ──────────────────────────────────
    QProcess *run_process_{nullptr};
    QString last_compiled_binary_;
    // ── Panels ──────────────────────────────────────────────────────
    SettingsDialog *settings_dialog_{nullptr};
    GitPanel *git_panel_{nullptr};
    BuildPanel *build_panel_{nullptr};
    DebugPanel *debug_panel_{nullptr};
    TopologyPanel *topology_panel_{nullptr};
    // ── Panel Manager ────────────────────────────────────────────────────
    PanelManager *panel_manager_{nullptr};
    // ── Action Manager ───────────────────────────────────────────────────
    ActionManager *action_manager_{nullptr};
    // ── Analysis timer (real-time error checking) ────────────────────────
    QTimer *analysis_timer_{nullptr};
    QTimer *auto_save_timer_{nullptr};
    bool realtime_analysis_enabled_{true};

    // ── Per-tab state ────────────────────────────────────────────────────
    /** @brief TabInfo data structure. */
    struct TabInfo {
        QString file_path;
        QString language;
        SyntaxHighlighter *highlighter{nullptr};
    };
    std::unordered_map<int, TabInfo> tab_info_;
    int next_untitled_id_{1};
    int next_terminal_id_{1};
};

} // namespace polyglot::tools::ui
