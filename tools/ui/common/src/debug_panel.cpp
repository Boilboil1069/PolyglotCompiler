// debug_panel.cpp — Debugger integration panel implementation.
//
// Implements breakpoint management, call stack display, variable inspection,
// watch expressions, and debug console. The debugger (lldb on macOS, gdb on
// Linux) is driven via a QProcess pipe using MI-style or CLI commands.

#include "tools/ui/common/include/debug_panel.h"
#include "tools/ui/common/include/theme_manager.h"

#include <QCheckBox>
#include <QFileInfo>
#include <QHeaderView>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStandardPaths>

namespace polyglot::tools::ui {

// ============================================================================
// Construction / Destruction
// ============================================================================

DebugPanel::DebugPanel(QWidget *parent) : QWidget(parent) {
    SetupUi();
    debugger_path_ = FindDebugger();
}

DebugPanel::~DebugPanel() {
    if (debugger_process_ && debugger_process_->state() != QProcess::NotRunning) {
        debugger_process_->kill();
        debugger_process_->waitForFinished(2000);
    }
}

// ============================================================================
// UI Setup
// ============================================================================

void DebugPanel::SetupUi() {
    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(0);

    SetupToolBar();

    tabs_ = new QTabWidget();
    tabs_->setTabPosition(QTabWidget::North);
    tabs_->setStyleSheet(ThemeManager::Instance().TabWidgetStylesheet(false));

    SetupBreakpointsView();
    SetupCallStackView();
    SetupVariablesView();
    SetupWatchView();
    SetupConsoleView();

    layout_->addWidget(tabs_, 1);
}

void DebugPanel::SetupToolBar() {
    toolbar_ = new QToolBar();
    toolbar_->setMovable(false);
    toolbar_->setIconSize(QSize(16, 16));
    toolbar_->setStyleSheet(ThemeManager::Instance().ToolBarStylesheet());

    action_start_ = toolbar_->addAction("Start");
    action_start_->setToolTip("Start debugging (F5)");
    connect(action_start_, &QAction::triggered, this, &DebugPanel::StartDebug);

    action_stop_ = toolbar_->addAction("Stop");
    action_stop_->setToolTip("Stop debugging (Shift+F5)");
    connect(action_stop_, &QAction::triggered, this, &DebugPanel::StopDebug);

    toolbar_->addSeparator();

    action_pause_ = toolbar_->addAction("Pause");
    action_pause_->setToolTip("Pause (F6)");
    connect(action_pause_, &QAction::triggered, this, &DebugPanel::PauseDebug);

    action_continue_ = toolbar_->addAction("Continue");
    action_continue_->setToolTip("Continue (F5)");
    connect(action_continue_, &QAction::triggered, this, &DebugPanel::ContinueDebug);

    toolbar_->addSeparator();

    action_step_over_ = toolbar_->addAction("Step Over");
    action_step_over_->setToolTip("Step Over (F10)");
    connect(action_step_over_, &QAction::triggered, this, &DebugPanel::StepOver);

    action_step_into_ = toolbar_->addAction("Step Into");
    action_step_into_->setToolTip("Step Into (F11)");
    connect(action_step_into_, &QAction::triggered, this, &DebugPanel::StepInto);

    action_step_out_ = toolbar_->addAction("Step Out");
    action_step_out_->setToolTip("Step Out (Shift+F11)");
    connect(action_step_out_, &QAction::triggered, this, &DebugPanel::StepOut);

    layout_->addWidget(toolbar_);
    UpdateToolBarState();
}

void DebugPanel::SetupBreakpointsView() {
    auto *bp_widget = new QWidget();
    auto *bp_layout = new QVBoxLayout(bp_widget);
    bp_layout->setContentsMargins(0, 0, 0, 0);
    bp_layout->setSpacing(0);

    breakpoint_tree_ = new QTreeWidget();
    breakpoint_tree_->setHeaderLabels({"", "File", "Line", "Condition", "Hits"});
    breakpoint_tree_->setRootIsDecorated(false);
    breakpoint_tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    breakpoint_tree_->setStyleSheet(ThemeManager::Instance().TreeWidgetStylesheet());

    // Checkbox column (enabled state)
    breakpoint_tree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    breakpoint_tree_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    breakpoint_tree_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    breakpoint_tree_->header()->setSectionResizeMode(3, QHeaderView::Stretch);
    breakpoint_tree_->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);

    connect(breakpoint_tree_, &QTreeWidget::itemChanged,
            this, &DebugPanel::OnBreakpointItemChanged);
    connect(breakpoint_tree_, &QTreeWidget::itemDoubleClicked,
            this, &DebugPanel::OnBreakpointDoubleClicked);
    connect(breakpoint_tree_, &QTreeWidget::customContextMenuRequested,
            this, &DebugPanel::OnBreakpointContextMenu);

    bp_layout->addWidget(breakpoint_tree_, 1);

    // Bottom bar
    auto *bp_btn_row = new QHBoxLayout();
    bp_btn_row->setContentsMargins(4, 4, 4, 4);
    remove_all_bp_button_ = new QPushButton("Remove All");
    remove_all_bp_button_->setStyleSheet(ThemeManager::Instance().PushButtonStylesheet());
    connect(remove_all_bp_button_, &QPushButton::clicked, this, &DebugPanel::RemoveAllBreakpoints);
    bp_btn_row->addStretch();
    bp_btn_row->addWidget(remove_all_bp_button_);
    bp_layout->addLayout(bp_btn_row);

    tabs_->addTab(bp_widget, "Breakpoints");
}

void DebugPanel::SetupCallStackView() {
    callstack_tree_ = new QTreeWidget();
    callstack_tree_->setHeaderLabels({"#", "Function", "File", "Line"});
    callstack_tree_->setRootIsDecorated(false);
    callstack_tree_->setStyleSheet(
        ThemeManager::Instance().TreeWidgetStylesheet() +
        " QTreeWidget { font-family: Menlo, Consolas, monospace; font-size: 11px; }");
    callstack_tree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    callstack_tree_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    callstack_tree_->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    callstack_tree_->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);

    connect(callstack_tree_, &QTreeWidget::itemClicked,
            this, &DebugPanel::OnStackFrameClicked);

    tabs_->addTab(callstack_tree_, "Call Stack");
}

void DebugPanel::SetupVariablesView() {
    auto *var_widget = new QWidget();
    auto *var_layout = new QVBoxLayout(var_widget);
    var_layout->setContentsMargins(0, 0, 0, 0);
    var_layout->setSpacing(0);

    // Scope selector
    auto *scope_row = new QHBoxLayout();
    scope_row->setContentsMargins(4, 4, 4, 0);
    auto *scope_label = new QLabel("Scope:");
    scope_label->setStyleSheet(ThemeManager::Instance().LabelStylesheet() +
        " QLabel { font-size: 11px; }");
    scope_row->addWidget(scope_label);
    scope_combo_ = new QComboBox();
    scope_combo_->addItems({"Locals", "Arguments", "Globals"});
    scope_combo_->setStyleSheet(ThemeManager::Instance().ComboBoxStylesheet());
    connect(scope_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { UpdateVariables(); });
    scope_row->addWidget(scope_combo_, 1);
    var_layout->addLayout(scope_row);

    variables_tree_ = new QTreeWidget();
    variables_tree_->setHeaderLabels({"Name", "Value", "Type"});
    variables_tree_->setRootIsDecorated(true);
    variables_tree_->setStyleSheet(
        ThemeManager::Instance().TreeWidgetStylesheet() +
        " QTreeWidget { font-family: Menlo, Consolas, monospace; font-size: 11px; }");
    variables_tree_->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    variables_tree_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    variables_tree_->header()->setSectionResizeMode(2, QHeaderView::Interactive);

    connect(variables_tree_, &QTreeWidget::itemExpanded,
            this, &DebugPanel::OnVariableExpanded);

    var_layout->addWidget(variables_tree_, 1);

    tabs_->addTab(var_widget, "Variables");
}

void DebugPanel::SetupWatchView() {
    auto *watch_widget = new QWidget();
    auto *watch_layout = new QVBoxLayout(watch_widget);
    watch_layout->setContentsMargins(0, 0, 0, 0);
    watch_layout->setSpacing(0);

    watch_tree_ = new QTreeWidget();
    watch_tree_->setHeaderLabels({"Expression", "Value", "Type"});
    watch_tree_->setRootIsDecorated(false);
    watch_tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    watch_tree_->setStyleSheet(
        ThemeManager::Instance().TreeWidgetStylesheet() +
        " QTreeWidget { font-family: Menlo, Consolas, monospace; font-size: 11px; }");
    watch_tree_->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    watch_tree_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    watch_tree_->header()->setSectionResizeMode(2, QHeaderView::Interactive);

    // Context menu for watch expressions (remove, edit, evaluate)
    connect(watch_tree_, &QTreeWidget::customContextMenuRequested,
            this, [this](const QPoint &pos) {
        auto *item = watch_tree_->itemAt(pos);
        if (!item) return;

        QMenu menu(this);
        menu.setStyleSheet(
            "QMenu { background: #252526; color: #cccccc; border: 1px solid #454545; }"
            "QMenu::item { padding: 5px 30px 5px 20px; }"
            "QMenu::item:selected { background: #094771; }");

        menu.addAction("Evaluate", this, &DebugPanel::OnEvaluateWatch);
        menu.addSeparator();
        menu.addAction("Remove", this, &DebugPanel::OnRemoveWatch);
        menu.addAction("Remove All", [this]() {
            watch_expressions_.clear();
            watch_tree_->clear();
        });

        menu.exec(watch_tree_->viewport()->mapToGlobal(pos));
    });

    watch_layout->addWidget(watch_tree_, 1);

    // Input row for adding watch expressions
    auto *input_row = new QHBoxLayout();
    input_row->setContentsMargins(4, 4, 4, 4);
    watch_input_ = new QLineEdit();
    watch_input_->setPlaceholderText("Add watch expression...");
    watch_input_->setStyleSheet(
        ThemeManager::Instance().LineEditStylesheet() +
        " QLineEdit { font-family: Menlo, Consolas, monospace; font-size: 11px; }");
    connect(watch_input_, &QLineEdit::returnPressed, this, &DebugPanel::OnAddWatch);
    input_row->addWidget(watch_input_, 1);

    add_watch_button_ = new QPushButton("+");
    add_watch_button_->setFixedWidth(28);
    {
        const auto &tc = ThemeManager::Instance().Active();
        add_watch_button_->setStyleSheet(
            QString("QPushButton { background: %1; color: %2; border: none; "
                    "border-radius: 3px; padding: 4px; font-weight: bold; }"
                    "QPushButton:hover { background: %3; }")
                .arg(tc.accent.name(), tc.button_primary_text.name(),
                     tc.accent_hover.name()));
    }
    connect(add_watch_button_, &QPushButton::clicked, this, &DebugPanel::OnAddWatch);
    input_row->addWidget(add_watch_button_);

    watch_layout->addLayout(input_row);

    tabs_->addTab(watch_widget, "Watch");
}

void DebugPanel::SetupConsoleView() {
    auto *console_widget = new QWidget();
    auto *console_layout = new QVBoxLayout(console_widget);
    console_layout->setContentsMargins(0, 0, 0, 0);
    console_layout->setSpacing(0);

    console_output_ = new QPlainTextEdit();
    console_output_->setReadOnly(true);
    console_output_->setMaximumBlockCount(5000);
    console_output_->setStyleSheet(ThemeManager::Instance().PlainTextEditStylesheet());
    console_layout->addWidget(console_output_, 1);

    console_input_ = new QLineEdit();
    console_input_->setPlaceholderText("Debug command...");
    {
        const auto &tc = ThemeManager::Instance().Active();
        console_input_->setStyleSheet(
            QString("QLineEdit { background: %1; color: %2; border: none; "
                    "border-top: 1px solid %3; padding: 6px; "
                    "font-family: Menlo, Consolas, monospace; font-size: 11px; }")
                .arg(tc.input_background.name(), tc.input_text.name(),
                     tc.input_border.name()));
    }
    connect(console_input_, &QLineEdit::returnPressed, this, &DebugPanel::OnConsoleInput);
    console_layout->addWidget(console_input_);

    tabs_->addTab(console_widget, "Console");
}

// ============================================================================
// Public Interface
// ============================================================================

void DebugPanel::SetExecutable(const QString &path) {
    executable_path_ = path;
}

void DebugPanel::SetArguments(const QStringList &args) {
    program_arguments_ = args;
}

void DebugPanel::SetWorkingDirectory(const QString &path) {
    working_directory_ = path;
}

void DebugPanel::SetDebuggerPath(const QString &path) {
    if (!path.isEmpty()) {
        debugger_path_ = path;
    }
}

void DebugPanel::SetBreakOnEntry(bool enabled) {
    break_on_entry_ = enabled;
}

// ============================================================================
// Breakpoint Management
// ============================================================================

void DebugPanel::ToggleBreakpoint(const QString &file, int line) {
    // Check if breakpoint already exists at this location
    for (auto it = breakpoints_.begin(); it != breakpoints_.end(); ++it) {
        if (it->file == file && it->line == line) {
            // Remove existing breakpoint
            if (IsDebugging()) {
                SendDebuggerCommand(QString("breakpoint delete %1").arg(it->id));
            }
            breakpoints_.erase(it);
            RefreshBreakpointTree();
            emit BreakpointsChanged();
            return;
        }
    }

    // Add new breakpoint
    Breakpoint bp;
    bp.id = next_breakpoint_id_++;
    bp.file = file;
    bp.line = line;
    bp.enabled = true;
    breakpoints_.push_back(bp);

    if (IsDebugging() && IsPaused()) {
        SendDebuggerCommand(
            QString("breakpoint set --file %1 --line %2").arg(file).arg(line));
    }

    RefreshBreakpointTree();
    emit BreakpointsChanged();
}

void DebugPanel::SetBreakpointCondition(const QString &file, int line,
                                         const QString &condition) {
    for (auto &bp : breakpoints_) {
        if (bp.file == file && bp.line == line) {
            bp.condition = condition;
            if (IsDebugging()) {
                SendDebuggerCommand(
                    QString("breakpoint modify -c '%1' %2").arg(condition).arg(bp.id));
            }
            RefreshBreakpointTree();
            emit BreakpointsChanged();
            return;
        }
    }
}

void DebugPanel::RemoveAllBreakpoints() {
    if (IsDebugging()) {
        SendDebuggerCommand("breakpoint delete");
    }
    breakpoints_.clear();
    next_breakpoint_id_ = 1;
    RefreshBreakpointTree();
    emit BreakpointsChanged();
}

std::vector<Breakpoint> DebugPanel::GetBreakpointsForFile(const QString &file) const {
    std::vector<Breakpoint> result;
    for (const auto &bp : breakpoints_) {
        if (bp.file == file) {
            result.push_back(bp);
        }
    }
    return result;
}

void DebugPanel::RefreshBreakpointTree() {
    breakpoint_tree_->blockSignals(true);
    breakpoint_tree_->clear();

    for (const auto &bp : breakpoints_) {
        auto *item = new QTreeWidgetItem();
        item->setCheckState(0, bp.enabled ? Qt::Checked : Qt::Unchecked);
        // Show only filename, not full path
        QString filename = bp.file.mid(bp.file.lastIndexOf('/') + 1);
        item->setText(1, filename);
        item->setToolTip(1, bp.file);
        item->setText(2, QString::number(bp.line));
        item->setText(3, bp.condition);
        item->setText(4, QString::number(bp.hit_count));
        item->setData(0, Qt::UserRole, bp.id);

        // Color: red dot simulation via text color
        QColor color = bp.enabled ? QColor("#f44747") : QColor("#666666");
        item->setForeground(1, color);
        item->setForeground(2, color);

        breakpoint_tree_->addTopLevelItem(item);
    }

    breakpoint_tree_->blockSignals(false);
}

// ============================================================================
// Debug Control
// ============================================================================

void DebugPanel::StartDebug() {
    if (executable_path_.isEmpty()) {
        emit StatusMessage("No executable set. Build the project first.");
        return;
    }

    if (debugger_path_.isEmpty()) {
        emit StatusMessage("No debugger found (lldb/gdb). Install one to debug.");
        return;
    }

    SetDebugState(DebugState::Starting);

    // Clear previous output
    console_output_->clear();
    callstack_tree_->clear();
    variables_tree_->clear();

    // Launch debugger
    if (debugger_process_) {
        debugger_process_->kill();
        debugger_process_->waitForFinished(2000);
        debugger_process_->deleteLater();
    }

    debugger_process_ = new QProcess(this);
    if (!working_directory_.isEmpty()) {
        debugger_process_->setWorkingDirectory(working_directory_);
    }
    debugger_process_->setProcessChannelMode(QProcess::MergedChannels);

    connect(debugger_process_, &QProcess::readyReadStandardOutput,
            this, &DebugPanel::OnDebuggerReadyRead);
    connect(debugger_process_, &QProcess::errorOccurred,
            this, &DebugPanel::OnDebuggerError);
    connect(debugger_process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &DebugPanel::OnDebuggerFinished);

    QStringList args;
    bool is_lldb = debugger_path_.contains("lldb");

    if (is_lldb) {
        args << "--no-use-colors" << "--" << executable_path_;
        args << program_arguments_;
    } else {
        // gdb
        args << "--quiet" << "--interpreter=mi" << executable_path_;
    }

    console_output_->appendPlainText(
        ">>> " + debugger_path_ + " " + args.join(" ") + "\n");

    debugger_process_->start(debugger_path_, args);

    if (!debugger_process_->waitForStarted(5000)) {
        emit StatusMessage("Failed to start debugger");
        SetDebugState(DebugState::Idle);
        return;
    }

    // Set breakpoints
    SetBreakpointsInDebugger();

    // If break-on-entry is enabled, set a temporary breakpoint at main
    if (break_on_entry_) {
        bool is_lldb_dbg = debugger_path_.contains("lldb");
        if (is_lldb_dbg) {
            SendDebuggerCommand("breakpoint set --one-shot true --name main");
        } else {
            SendDebuggerCommand("-break-insert -t main");
        }
    }

    // Run the program
    if (is_lldb) {
        SendDebuggerCommand("run");
    } else {
        SendDebuggerCommand("-exec-run");
    }

    SetDebugState(DebugState::Running);
    emit DebugStarted();
    emit StatusMessage("Debug session started");
}

void DebugPanel::StopDebug() {
    if (!IsDebugging()) return;

    SetDebugState(DebugState::Stopping);

    bool is_lldb = debugger_path_.contains("lldb");
    if (is_lldb) {
        SendDebuggerCommand("kill");
        SendDebuggerCommand("quit");
    } else {
        SendDebuggerCommand("-exec-abort");
        SendDebuggerCommand("-gdb-exit");
    }

    if (debugger_process_) {
        if (!debugger_process_->waitForFinished(3000)) {
            debugger_process_->kill();
            debugger_process_->waitForFinished(2000);
        }
    }

    SetDebugState(DebugState::Idle);
    emit DebugStopped();
    emit StatusMessage("Debug session ended");
}

void DebugPanel::PauseDebug() {
    if (debug_state_ != DebugState::Running) return;

    bool is_lldb = debugger_path_.contains("lldb");
    if (is_lldb) {
        SendDebuggerCommand("process interrupt");
    } else {
        SendDebuggerCommand("-exec-interrupt");
    }
}

void DebugPanel::ContinueDebug() {
    if (!IsPaused()) return;

    bool is_lldb = debugger_path_.contains("lldb");
    SendDebuggerCommand(is_lldb ? "continue" : "-exec-continue");
    SetDebugState(DebugState::Running);
}

void DebugPanel::StepOver() {
    if (!IsPaused()) return;

    bool is_lldb = debugger_path_.contains("lldb");
    SendDebuggerCommand(is_lldb ? "next" : "-exec-next");
    SetDebugState(DebugState::Running);
}

void DebugPanel::StepInto() {
    if (!IsPaused()) return;

    bool is_lldb = debugger_path_.contains("lldb");
    SendDebuggerCommand(is_lldb ? "step" : "-exec-step");
    SetDebugState(DebugState::Running);
}

void DebugPanel::StepOut() {
    if (!IsPaused()) return;

    bool is_lldb = debugger_path_.contains("lldb");
    SendDebuggerCommand(is_lldb ? "finish" : "-exec-finish");
    SetDebugState(DebugState::Running);
}

void DebugPanel::RunToCursor(const QString &file, int line) {
    if (!IsPaused()) return;

    bool is_lldb = debugger_path_.contains("lldb");
    if (is_lldb) {
        SendDebuggerCommand(
            QString("breakpoint set --one-shot true --file %1 --line %2")
                .arg(file).arg(line));
        SendDebuggerCommand("continue");
    } else {
        SendDebuggerCommand(QString("-exec-until %1:%2").arg(file).arg(line));
    }
    SetDebugState(DebugState::Running);
}

// ============================================================================
// Debugger Process I/O
// ============================================================================

void DebugPanel::OnDebuggerReadyRead() {
    if (!debugger_process_) return;

    QString output = QString::fromUtf8(debugger_process_->readAllStandardOutput());
    console_output_->appendPlainText(output);

    // Auto-scroll
    auto *scrollbar = console_output_->verticalScrollBar();
    if (scrollbar) scrollbar->setValue(scrollbar->maximum());

    ParseDebuggerOutput(output);
    emit DebugOutput(output);
}

void DebugPanel::OnDebuggerError() {
    if (!debugger_process_) return;
    console_output_->appendPlainText(
        "[Error] " + debugger_process_->errorString() + "\n");
}

void DebugPanel::OnDebuggerFinished(int exit_code, QProcess::ExitStatus /*status*/) {
    console_output_->appendPlainText(
        QString("\n--- Debugger exited (code %1) ---\n").arg(exit_code));

    SetDebugState(DebugState::Idle);
    emit DebugStopped();
    emit StatusMessage("Debug session ended");

    debugger_process_->deleteLater();
    debugger_process_ = nullptr;
}

void DebugPanel::SendDebuggerCommand(const QString &command) {
    if (!debugger_process_ || debugger_process_->state() == QProcess::NotRunning)
        return;

    QString cmd = command + "\n";
    debugger_process_->write(cmd.toUtf8());
    console_output_->appendPlainText("(dbg) " + command);
}

void DebugPanel::ParseDebuggerOutput(const QString &output) {
    // Accumulate output lines for multi-line parsing
    pending_output_ += output;

    bool is_lldb = debugger_path_.contains("lldb");

    if (is_lldb) {
        // ── Detect stopped state ─────────────────────────────────────────
        static const QRegularExpression stop_re(
            R"(stop reason = (?:breakpoint|step|signal))");
        static const QRegularExpression frame_re(
            R"(at\s+(.+?):(\d+))");

        if (stop_re.match(output).hasMatch()) {
            SetDebugState(DebugState::Paused);

            auto frame_match = frame_re.match(output);
            if (frame_match.hasMatch()) {
                QString file = frame_match.captured(1);
                int line = frame_match.captured(2).toInt();
                emit DebugLocationChanged(file, line);
            }

            UpdateCallStack();
            UpdateVariables();
            if (!watch_expressions_.empty()) {
                OnEvaluateWatch();
            }
        }

        // ── Parse backtrace output ──────────────────────────────────────
        // lldb bt format: "  * frame #0: 0x... module`func at file.cpp:42"
        //                 "    frame #1: 0x... module`func + 18 at file.cpp:10"
        static const QRegularExpression bt_frame_re(
            R"(frame #(\d+):\s+0x([0-9a-fA-F]+)\s+(\S+)`(.+?)(?:\s+\+\s+\d+)?\s+at\s+(.+?):(\d+))");
        auto bt_iter = bt_frame_re.globalMatch(output);
        bool found_frames = false;
        while (bt_iter.hasNext()) {
            auto m = bt_iter.next();
            if (!found_frames) {
                callstack_tree_->clear();
                stack_frames_.clear();
                found_frames = true;
            }
            StackFrame sf;
            sf.index = m.captured(1).toInt();
            sf.address = "0x" + m.captured(2);
            sf.module = m.captured(3);
            sf.function_name = m.captured(4).trimmed();
            sf.file = m.captured(5);
            sf.line = m.captured(6).toInt();
            stack_frames_.push_back(sf);

            auto *item = new QTreeWidgetItem({
                QString::number(sf.index),
                sf.function_name,
                sf.file,
                QString::number(sf.line)
            });
            // Highlight the current frame
            if (sf.index == 0) {
                item->setForeground(1, QColor("#dcdcaa"));
            }
            callstack_tree_->addTopLevelItem(item);
        }

        // ── Parse "frame variable" output for variables ─────────────────
        // Format: "(type) name = value"
        static const QRegularExpression var_re(
            R"(\(([^)]+)\)\s+(\w+)\s+=\s+(.+))");
        auto var_iter = var_re.globalMatch(output);
        bool found_vars = false;
        while (var_iter.hasNext()) {
            auto m = var_iter.next();
            if (!found_vars) {
                variables_tree_->clear();
                local_variables_.clear();
                found_vars = true;
            }
            DebugVariable dv;
            dv.type = m.captured(1).trimmed();
            dv.name = m.captured(2).trimmed();
            dv.value = m.captured(3).trimmed();
            // Detect composite types that may have children
            dv.has_children = dv.value.startsWith("{") || dv.value.startsWith("(");
            local_variables_.push_back(dv);

            auto *item = new QTreeWidgetItem({dv.name, dv.value, dv.type});
            if (dv.has_children) {
                // Add a placeholder child so the expand arrow appears
                item->addChild(new QTreeWidgetItem({"...", "", ""}));
            }
            // Color type column
            item->setForeground(2, QColor("#4ec9b0"));
            variables_tree_->addTopLevelItem(item);
        }

        // ── Parse expression evaluation result for watch ────────────────
        // Format: "(type) $N = value" or "= value"
        static const QRegularExpression expr_result_re(
            R"(\(([^)]+)\)\s+\$\d+\s+=\s+(.+))");
        auto expr_match = expr_result_re.match(output);
        if (expr_match.hasMatch()) {
            // Update the most recently evaluated watch expression
            QString type = expr_match.captured(1).trimmed();
            QString value = expr_match.captured(2).trimmed();
            UpdateWatchResult(type, value);
        }

    } else {
        // ── GDB/MI output parsing ────────────────────────────────────────

        // Stopped event
        static const QRegularExpression gdb_stop_re(
            R"RE(\*stopped,reason="(breakpoint-hit|end-stepping-range|signal-received)")RE");
        static const QRegularExpression gdb_file_re(
            R"RE(fullname="(.+?)",line="(\d+)")RE");

        if (gdb_stop_re.match(output).hasMatch()) {
            SetDebugState(DebugState::Paused);

            auto file_match = gdb_file_re.match(output);
            if (file_match.hasMatch()) {
                emit DebugLocationChanged(file_match.captured(1),
                                          file_match.captured(2).toInt());
            }

            UpdateCallStack();
            UpdateVariables();
            if (!watch_expressions_.empty()) {
                OnEvaluateWatch();
            }
        }

        // ── Parse GDB/MI stack list frames response ─────────────────────
        // Format: ^done,stack=[frame={level="0",addr="0x...",func="main",
        //         file="test.cpp",fullname="/path/test.cpp",line="42"}, ...]
        static const QRegularExpression gdb_frame_re(
            R"RE(level="(\d+)",addr="(0x[0-9a-fA-F]+)",func="(.+?)".*?fullname="(.+?)",line="(\d+)")RE");
        auto gdb_frame_iter = gdb_frame_re.globalMatch(output);
        bool found_gdb_frames = false;
        while (gdb_frame_iter.hasNext()) {
            auto m = gdb_frame_iter.next();
            if (!found_gdb_frames) {
                callstack_tree_->clear();
                stack_frames_.clear();
                found_gdb_frames = true;
            }
            StackFrame sf;
            sf.index = m.captured(1).toInt();
            sf.address = m.captured(2);
            sf.function_name = m.captured(3);
            sf.file = m.captured(4);
            sf.line = m.captured(5).toInt();
            stack_frames_.push_back(sf);

            auto *item = new QTreeWidgetItem({
                QString::number(sf.index),
                sf.function_name,
                sf.file,
                QString::number(sf.line)
            });
            if (sf.index == 0) {
                item->setForeground(1, QColor("#dcdcaa"));
            }
            callstack_tree_->addTopLevelItem(item);
        }

        // ── Parse GDB/MI variable list response ─────────────────────────
        // Format: ^done,variables=[{name="x",value="42",type="int"}, ...]
        static const QRegularExpression gdb_var_re(
            R"RE(name="(.+?)".*?value="(.+?)"(?:.*?type="(.+?)")?)RE");
        auto gdb_var_iter = gdb_var_re.globalMatch(output);
        bool found_gdb_vars = false;
        while (gdb_var_iter.hasNext()) {
            auto m = gdb_var_iter.next();
            if (!found_gdb_vars) {
                variables_tree_->clear();
                local_variables_.clear();
                found_gdb_vars = true;
            }
            DebugVariable dv;
            dv.name = m.captured(1);
            dv.value = m.captured(2);
            dv.type = m.capturedLength(3) > 0 ? m.captured(3) : "auto";
            dv.has_children = dv.value.startsWith("{");
            local_variables_.push_back(dv);

            auto *item = new QTreeWidgetItem({dv.name, dv.value, dv.type});
            if (dv.has_children) {
                item->addChild(new QTreeWidgetItem({"...", "", ""}));
            }
            item->setForeground(2, QColor("#4ec9b0"));
            variables_tree_->addTopLevelItem(item);
        }

        // ── Parse GDB/MI expression evaluation response ─────────────────
        // Format: ^done,value="42"
        static const QRegularExpression gdb_eval_re(
            R"RE(\^done,value="(.+?)")RE");
        auto gdb_eval_match = gdb_eval_re.match(output);
        if (gdb_eval_match.hasMatch()) {
            QString value = gdb_eval_match.captured(1);
            UpdateWatchResult("", value);
        }
    }

    // Clear accumulated output after processing
    pending_output_.clear();
}

void DebugPanel::SetBreakpointsInDebugger() {
    bool is_lldb = debugger_path_.contains("lldb");

    for (auto &bp : breakpoints_) {
        if (!bp.enabled) continue;

        if (is_lldb) {
            QString cmd = QString("breakpoint set --file %1 --line %2")
                              .arg(bp.file).arg(bp.line);
            if (!bp.condition.isEmpty()) {
                cmd += " --condition '" + bp.condition + "'";
            }
            SendDebuggerCommand(cmd);
        } else {
            // GDB MI
            QString cmd = QString("-break-insert %1:%2").arg(bp.file).arg(bp.line);
            SendDebuggerCommand(cmd);
            if (!bp.condition.isEmpty()) {
                SendDebuggerCommand(
                    QString("-break-condition %1 %2").arg(bp.id).arg(bp.condition));
            }
        }
    }
}

// ============================================================================
// State Management
// ============================================================================

void DebugPanel::SetDebugState(DebugState state) {
    debug_state_ = state;
    UpdateToolBarState();
}

void DebugPanel::UpdateToolBarState() {
    bool idle = (debug_state_ == DebugState::Idle);
    bool running = (debug_state_ == DebugState::Running);
    bool paused = (debug_state_ == DebugState::Paused);

    action_start_->setEnabled(idle);
    action_stop_->setEnabled(!idle);
    action_pause_->setEnabled(running);
    action_continue_->setEnabled(paused);
    action_step_over_->setEnabled(paused);
    action_step_into_->setEnabled(paused);
    action_step_out_->setEnabled(paused);
}

void DebugPanel::UpdateCallStack() {
    callstack_tree_->clear();
    stack_frames_.clear();

    bool is_lldb = debugger_path_.contains("lldb");
    if (is_lldb) {
        SendDebuggerCommand("bt");
    } else {
        SendDebuggerCommand("-stack-list-frames");
    }

    // Output will be parsed asynchronously in ParseDebuggerOutput.
    // For now, we parse the backtrace format here as a secondary pass.
    // The frames will appear in the console output and be caught in ReadyRead.
}

void DebugPanel::UpdateVariables() {
    variables_tree_->clear();
    local_variables_.clear();

    bool is_lldb = debugger_path_.contains("lldb");
    if (is_lldb) {
        SendDebuggerCommand("frame variable");
    } else {
        SendDebuggerCommand("-stack-list-variables --all-values");
    }
}

QString DebugPanel::FindDebugger() const {
    // On macOS, prefer lldb (ships with Xcode CLT)
    QStringList lldb_paths = {
        "/usr/bin/lldb",
        "/Applications/Xcode.app/Contents/Developer/usr/bin/lldb",
    };
    for (const auto &p : lldb_paths) {
        if (QFileInfo::exists(p)) return p;
    }

    // Try gdb as fallback
    QStringList gdb_paths = {
        "/usr/local/bin/gdb",
        "/usr/bin/gdb",
        "/opt/homebrew/bin/gdb",
    };
    for (const auto &p : gdb_paths) {
        if (QFileInfo::exists(p)) return p;
    }

    // PATH lookup
    QString found = QStandardPaths::findExecutable("lldb");
    if (!found.isEmpty()) return found;
    found = QStandardPaths::findExecutable("gdb");
    if (!found.isEmpty()) return found;

    return {};
}

// ============================================================================
// UI Interaction Slots
// ============================================================================

void DebugPanel::OnBreakpointItemChanged(QTreeWidgetItem *item, int column) {
    if (column != 0) return; // only care about checkbox

    int bp_id = item->data(0, Qt::UserRole).toInt();
    bool enabled = (item->checkState(0) == Qt::Checked);

    for (auto &bp : breakpoints_) {
        if (bp.id == bp_id) {
            bp.enabled = enabled;
            if (IsDebugging()) {
                bool is_lldb = debugger_path_.contains("lldb");
                if (is_lldb) {
                    SendDebuggerCommand(
                        QString("breakpoint %1 %2")
                            .arg(enabled ? "enable" : "disable")
                            .arg(bp.id));
                } else {
                    SendDebuggerCommand(
                        QString(enabled ? "-break-enable %1" : "-break-disable %1")
                            .arg(bp.id));
                }
            }
            break;
        }
    }
    emit BreakpointsChanged();
}

void DebugPanel::OnBreakpointDoubleClicked(QTreeWidgetItem *item, int /*column*/) {
    int bp_id = item->data(0, Qt::UserRole).toInt();
    for (const auto &bp : breakpoints_) {
        if (bp.id == bp_id) {
            emit DebugLocationChanged(bp.file, bp.line);
            break;
        }
    }
}

void DebugPanel::OnBreakpointContextMenu(const QPoint &pos) {
    auto *item = breakpoint_tree_->itemAt(pos);
    if (!item) return;

    int bp_id = item->data(0, Qt::UserRole).toInt();

    QMenu menu(this);
    {
        const auto &tc = ThemeManager::Instance().Active();
        menu.setStyleSheet(
            QString("QMenu { background: %1; color: %2; border: 1px solid %3; }"
                    "QMenu::item { padding: 5px 30px 5px 20px; }"
                    "QMenu::item:selected { background: %4; }")
                .arg(tc.menu_background.name(), tc.menu_text.name(),
                     tc.border.name(), tc.menu_hover.name()));
    }

    menu.addAction("Go to Location", [this, bp_id]() {
        for (const auto &bp : breakpoints_) {
            if (bp.id == bp_id) {
                emit DebugLocationChanged(bp.file, bp.line);
                break;
            }
        }
    });

    menu.addAction("Edit Condition...", [this, bp_id]() {
        for (auto &bp : breakpoints_) {
            if (bp.id == bp_id) {
                bool ok = false;
                QString cond = QInputDialog::getText(
                    this, "Breakpoint Condition",
                    "Stop when condition is true:",
                    QLineEdit::Normal, bp.condition, &ok);
                if (ok) {
                    SetBreakpointCondition(bp.file, bp.line, cond);
                }
                break;
            }
        }
    });

    menu.addSeparator();

    menu.addAction("Remove", [this, bp_id]() {
        for (auto it = breakpoints_.begin(); it != breakpoints_.end(); ++it) {
            if (it->id == bp_id) {
                if (IsDebugging()) {
                    SendDebuggerCommand(QString("breakpoint delete %1").arg(bp_id));
                }
                breakpoints_.erase(it);
                RefreshBreakpointTree();
                emit BreakpointsChanged();
                break;
            }
        }
    });

    menu.addAction("Remove All", this, &DebugPanel::RemoveAllBreakpoints);

    menu.exec(breakpoint_tree_->viewport()->mapToGlobal(pos));
}

void DebugPanel::OnStackFrameClicked(QTreeWidgetItem *item, int /*column*/) {
    QString file = item->text(2);
    int line = item->text(3).toInt();
    if (!file.isEmpty() && line > 0) {
        emit DebugLocationChanged(file, line);

        // Also switch debugger to this frame for variable inspection
        int frame_idx = item->text(0).toInt();
        bool is_lldb = debugger_path_.contains("lldb");
        if (is_lldb) {
            SendDebuggerCommand(QString("frame select %1").arg(frame_idx));
        } else {
            SendDebuggerCommand(QString("-stack-select-frame %1").arg(frame_idx));
        }
        UpdateVariables();
    }
}

void DebugPanel::OnVariableExpanded(QTreeWidgetItem *item) {
    // If the first child is the placeholder "...", replace it with real data
    if (item->childCount() == 1 && item->child(0)->text(0) == "...") {
        // Remove placeholder
        delete item->takeChild(0);

        // Query the debugger for children of this variable
        QString var_name = item->text(0);
        // Walk up the tree to build the full qualified name
        QTreeWidgetItem *parent = item->parent();
        while (parent) {
            var_name = parent->text(0) + "." + var_name;
            parent = parent->parent();
        }

        bool is_lldb = debugger_path_.contains("lldb");
        if (is_lldb) {
            SendDebuggerCommand("frame variable " + var_name);
        } else {
            // GDB MI: create a variable object and list children
            SendDebuggerCommand("-var-create - * \"" + var_name + "\"");
            SendDebuggerCommand("-var-list-children --all-values -");
        }
    }
}

void DebugPanel::OnAddWatch() {
    QString expr = watch_input_->text().trimmed();
    if (expr.isEmpty()) return;

    WatchExpression we;
    we.expression = expr;
    we.value = "<not evaluated>";
    we.type = "";

    watch_expressions_.push_back(we);
    watch_input_->clear();

    auto *item = new QTreeWidgetItem({we.expression, we.value, we.type});
    item->setData(0, Qt::UserRole, static_cast<int>(watch_expressions_.size() - 1));
    watch_tree_->addTopLevelItem(item);

    // If paused, evaluate immediately
    if (IsPaused()) {
        OnEvaluateWatch();
    }
}

void DebugPanel::OnRemoveWatch() {
    auto *item = watch_tree_->currentItem();
    if (!item) return;

    int idx = item->data(0, Qt::UserRole).toInt();
    if (idx >= 0 && idx < static_cast<int>(watch_expressions_.size())) {
        watch_expressions_.erase(watch_expressions_.begin() + idx);
    }

    delete item;

    // Re-index remaining items
    for (int i = 0; i < watch_tree_->topLevelItemCount(); ++i) {
        watch_tree_->topLevelItem(i)->setData(0, Qt::UserRole, i);
    }
}

void DebugPanel::OnEvaluateWatch() {
    if (!IsPaused()) return;

    bool is_lldb = debugger_path_.contains("lldb");

    for (auto &we : watch_expressions_) {
        // Mark as evaluating so UpdateWatchResult can match results
        we.value = "<evaluating...>";
        we.type = "";

        if (is_lldb) {
            SendDebuggerCommand("expression -- " + we.expression);
        } else {
            SendDebuggerCommand("-data-evaluate-expression \"" + we.expression + "\"");
        }
    }

    // Update tree to show evaluating state
    for (int i = 0; i < watch_tree_->topLevelItemCount(); ++i) {
        auto *item = watch_tree_->topLevelItem(i);
        item->setText(1, "<evaluating...>");
        item->setText(2, "");
        item->setForeground(1, QColor("#969696"));
    }
}

void DebugPanel::OnConsoleInput() {
    QString cmd = console_input_->text().trimmed();
    if (cmd.isEmpty()) return;

    console_input_->clear();
    SendDebuggerCommand(cmd);
}

// ============================================================================
// Watch Result Update
// ============================================================================

void DebugPanel::UpdateWatchResult(const QString &type, const QString &value) {
    // Find the next unevaluated watch expression and update it.
    // We track evaluation order by marking expressions that have been updated
    // in the current evaluation cycle.
    for (int i = 0; i < static_cast<int>(watch_expressions_.size()); ++i) {
        auto &we = watch_expressions_[i];
        if (we.value == "<evaluating...>" || we.value == "<not evaluated>") {
            we.value = value;
            we.type = type;

            // Update the tree widget item
            if (i < watch_tree_->topLevelItemCount()) {
                auto *item = watch_tree_->topLevelItem(i);
                item->setText(1, value);
                item->setText(2, type);
                // Color the value based on content
                if (value.startsWith("0x") || value.contains("nil") ||
                    value.contains("null") || value.contains("NULL")) {
                    item->setForeground(1, QColor("#569cd6"));
                } else if (value.startsWith("\"")) {
                    item->setForeground(1, QColor("#ce9178"));
                } else {
                    item->setForeground(1, QColor("#b5cea8"));
                }
                item->setForeground(2, QColor("#4ec9b0"));
            }
            return;
        }
    }
}

} // namespace polyglot::tools::ui
