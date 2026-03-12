// debug_panel.h — Debugger integration panel for the PolyglotCompiler IDE.
//
// Provides breakpoint management, variable inspection, call stack display,
// debug control (step/continue/pause), and watch expressions.
// Interfaces with lldb/gdb via command-line pipe for native debugging.

#pragma once

#include <QAction>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <string>
#include <unordered_map>
#include <vector>

namespace polyglot::tools::ui {

class CodeEditor;

// ============================================================================
// Breakpoint — a single breakpoint definition
// ============================================================================

struct Breakpoint {
    int id{-1};
    QString file;
    int line{0};
    bool enabled{true};
    QString condition;    // conditional breakpoint expression
    int hit_count{0};
    bool is_set{false};   // true once confirmed by debugger
};

// ============================================================================
// StackFrame — a single call stack frame
// ============================================================================

struct StackFrame {
    int index{0};
    QString function_name;
    QString file;
    int line{0};
    QString address;
    QString module;
};

// ============================================================================
// DebugVariable — variable in scope during debug session
// ============================================================================

struct DebugVariable {
    QString name;
    QString value;
    QString type;
    bool has_children{false};
    std::vector<DebugVariable> children;
};

// ============================================================================
// WatchExpression — user-defined watch entry
// ============================================================================

struct WatchExpression {
    QString expression;
    QString value;
    QString type;
};

// ============================================================================
// DebugPanel — dock-able debug control and inspection panel
// ============================================================================

class DebugPanel : public QWidget {
    Q_OBJECT

  public:
    explicit DebugPanel(QWidget *parent = nullptr);
    ~DebugPanel() override;

    // Breakpoint management (called from the editor gutter clicks).
    void ToggleBreakpoint(const QString &file, int line);
    void SetBreakpointCondition(const QString &file, int line, const QString &condition);
    void RemoveAllBreakpoints();
    std::vector<Breakpoint> GetBreakpoints() const { return breakpoints_; }
    std::vector<Breakpoint> GetBreakpointsForFile(const QString &file) const;

    // Debug session state.
    bool IsDebugging() const { return debug_state_ != DebugState::Idle; }
    bool IsPaused() const { return debug_state_ == DebugState::Paused; }

    // Set the executable path to debug.
    void SetExecutable(const QString &path);
    void SetArguments(const QStringList &args);
    void SetWorkingDirectory(const QString &path);

  signals:
    // Emitted when a breakpoint is added/removed/changed.
    void BreakpointsChanged();

    // Emitted when the debugger stops at a location (breakpoint hit, step, etc.).
    void DebugLocationChanged(const QString &file, int line);

    // Emitted when the debug session starts/stops.
    void DebugStarted();
    void DebugStopped();

    // Emitted when a debug output line is available.
    void DebugOutput(const QString &text);

    // Status messages for the main status bar.
    void StatusMessage(const QString &message);

  public slots:
    // Debug control
    void StartDebug();
    void StopDebug();
    void PauseDebug();
    void ContinueDebug();
    void StepOver();
    void StepInto();
    void StepOut();
    void RunToCursor(const QString &file, int line);

  private slots:
    // Debugger process I/O
    void OnDebuggerReadyRead();
    void OnDebuggerError();
    void OnDebuggerFinished(int exit_code, QProcess::ExitStatus status);

    // UI interactions
    void OnBreakpointItemChanged(QTreeWidgetItem *item, int column);
    void OnBreakpointDoubleClicked(QTreeWidgetItem *item, int column);
    void OnBreakpointContextMenu(const QPoint &pos);
    void OnStackFrameClicked(QTreeWidgetItem *item, int column);
    void OnVariableExpanded(QTreeWidgetItem *item);
    void OnAddWatch();
    void OnRemoveWatch();
    void OnEvaluateWatch();

    // Console
    void OnConsoleInput();

  private:
    void SetupUi();
    void SetupToolBar();
    void SetupBreakpointsView();
    void SetupCallStackView();
    void SetupVariablesView();
    void SetupWatchView();
    void SetupConsoleView();

    // Debugger command interface
    void SendDebuggerCommand(const QString &command);
    void ParseDebuggerOutput(const QString &output);
    void SetBreakpointsInDebugger();
    void RefreshBreakpointTree();

    // State management
    enum class DebugState {
        Idle,       // no debug session
        Starting,   // launching debugger
        Running,    // program running
        Paused,     // stopped at breakpoint / step
        Stopping    // shutting down
    };
    void SetDebugState(DebugState state);
    void UpdateToolBarState();
    void UpdateVariables();
    void UpdateCallStack();

    // Debugger detection
    QString FindDebugger() const;

    // ── UI Components ────────────────────────────────────────────────────
    QVBoxLayout *layout_{nullptr};
    QToolBar *toolbar_{nullptr};
    QTabWidget *tabs_{nullptr};

    // Breakpoints tab
    QTreeWidget *breakpoint_tree_{nullptr};
    QPushButton *remove_all_bp_button_{nullptr};

    // Call Stack tab
    QTreeWidget *callstack_tree_{nullptr};

    // Variables tab
    QTreeWidget *variables_tree_{nullptr};
    QComboBox *scope_combo_{nullptr};

    // Watch tab
    QTreeWidget *watch_tree_{nullptr};
    QLineEdit *watch_input_{nullptr};
    QPushButton *add_watch_button_{nullptr};

    // Debug Console tab
    QPlainTextEdit *console_output_{nullptr};
    QLineEdit *console_input_{nullptr};

    // Toolbar actions
    QAction *action_start_{nullptr};
    QAction *action_stop_{nullptr};
    QAction *action_pause_{nullptr};
    QAction *action_continue_{nullptr};
    QAction *action_step_over_{nullptr};
    QAction *action_step_into_{nullptr};
    QAction *action_step_out_{nullptr};

    // ── State ────────────────────────────────────────────────────────────
    DebugState debug_state_{DebugState::Idle};
    QProcess *debugger_process_{nullptr};
    QString executable_path_;
    QStringList program_arguments_;
    QString working_directory_;
    QString debugger_path_;
    std::vector<Breakpoint> breakpoints_;
    int next_breakpoint_id_{1};
    std::vector<StackFrame> stack_frames_;
    std::vector<DebugVariable> local_variables_;
    std::vector<WatchExpression> watch_expressions_;
    QString pending_output_;
};

} // namespace polyglot::tools::ui
