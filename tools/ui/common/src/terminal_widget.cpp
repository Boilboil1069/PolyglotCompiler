/**
 * @file     terminal_widget.cpp
 * @brief    Embedded terminal emulator for the PolyglotCompiler IDE
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include "tools/ui/common/include/terminal_widget.h"

#include <QApplication>
#include <QDir>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>

#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <signal.h>
#endif

namespace polyglot::tools::ui {

// ============================================================================
// TerminalInputFilter
// ============================================================================

TerminalInputFilter::TerminalInputFilter(TerminalWidget *terminal,
                                         QPlainTextEdit *output,
                                         QObject *parent)
    : QObject(parent), terminal_(terminal), output_(output) {}

bool TerminalInputFilter::eventFilter(QObject *obj, QEvent *event) {
    if (event->type() == QEvent::KeyPress) {
        auto *key_event = static_cast<QKeyEvent *>(event);
        emit KeyPressed(key_event);
        return true; // consume all key presses — terminal handles them
    }
    return QObject::eventFilter(obj, event);
}

// ============================================================================
// Construction / Destruction
// ============================================================================

TerminalWidget::TerminalWidget(QWidget *parent) : QWidget(parent) {
    SetupUi();
    StartShell();
}

TerminalWidget::~TerminalWidget() {
    // Disconnect all signals before stopping the shell.  StopShell() calls
    // waitForFinished() which pumps the event loop.  Without disconnecting,
    // the QProcess::finished signal fires OnProcessFinished → ShellFinished,
    // whose connected slots in MainWindow may access already-destroyed parent
    // widgets (e.g. QTabWidget::indexOf on a deleted QStackedWidget),
    // causing a use-after-free crash.
    if (process_) {
        process_->disconnect(this);
    }
    StopShell();
}

// ============================================================================
// Public API
// ============================================================================

void TerminalWidget::SetWorkingDirectory(const QString &path) {
    working_directory_ = path;
    if (process_ && process_->state() == QProcess::Running) {
#ifdef Q_OS_WIN
        SendCommand("cd /d " + path);
#else
        SendCommand("cd " + path);
#endif
    }
}

QString TerminalWidget::WorkingDirectory() const {
    return working_directory_;
}

bool TerminalWidget::IsRunning() const {
    return process_ && process_->state() == QProcess::Running;
}

void TerminalWidget::SendCommand(const QString &command) {
    if (!IsRunning()) return;

    // Record in history (avoid consecutive duplicates).
    if (history_.empty() || history_.back() != command) {
        history_.push_back(command);
        if (static_cast<int>(history_.size()) > kMaxHistorySize) {
            history_.pop_front();
        }
    }
    history_index_ = -1;

    // Write to the process stdin.
    QByteArray data = command.toUtf8() + "\n";
    process_->write(data);
}

void TerminalWidget::ClearOutput() {
    output_area_->clear();
    prompt_position_ = 0;
}

void TerminalWidget::RestartShell() {
    StopShell();
    ClearOutput();
    StartShell();
}

void TerminalWidget::StopShell() {
    if (process_) {
        if (process_->state() == QProcess::Running) {
            process_->terminate();
            if (!process_->waitForFinished(2000)) {
                process_->kill();
                process_->waitForFinished(1000);
            }
        }
    }
}

// ============================================================================
// UI Setup
// ============================================================================

void TerminalWidget::SetupUi() {
    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(0);

    output_area_ = new QPlainTextEdit(this);
    output_area_->setReadOnly(false);
    output_area_->setUndoRedoEnabled(false);
    output_area_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    output_area_->setMaximumBlockCount(10000); // scrollback limit

    // Monospace font
    QFont font("Consolas", 11);
    font.setStyleHint(QFont::Monospace);
    output_area_->setFont(font);

    // Dark terminal theme
    output_area_->setStyleSheet(
        "QPlainTextEdit {"
        "  background-color: #1e1e1e;"
        "  color: #cccccc;"
        "  border: none;"
        "  selection-background-color: #264f78;"
        "  selection-color: #ffffff;"
        "}"
        "QScrollBar:vertical {"
        "  background: #1e1e1e; width: 10px;"
        "}"
        "QScrollBar::handle:vertical {"
        "  background: #424242; min-height: 20px; border-radius: 5px;"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
        "  height: 0px;"
        "}");

    layout_->addWidget(output_area_);

    // Install our key-press interceptor so we can handle terminal input
    auto *filter = new TerminalInputFilter(this, output_area_, this);
    output_area_->installEventFilter(filter);
    connect(filter, &TerminalInputFilter::KeyPressed,
            this, &TerminalWidget::HandleInputKey);
}

// ============================================================================
// Shell Process Management
// ============================================================================

void TerminalWidget::StartShell() {
    if (!process_) {
        process_ = new QProcess(this);
        connect(process_, &QProcess::readyReadStandardOutput,
                this, &TerminalWidget::OnReadyReadStdout);
        connect(process_, &QProcess::readyReadStandardError,
                this, &TerminalWidget::OnReadyReadStderr);
        connect(process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &TerminalWidget::OnProcessFinished);
        connect(process_, &QProcess::errorOccurred,
                this, &TerminalWidget::OnProcessError);
    }

    // Merge stderr into stdout for simpler handling.
    process_->setProcessChannelMode(QProcess::MergedChannels);

    // Set working directory.
    if (!working_directory_.isEmpty() && QDir(working_directory_).exists()) {
        process_->setWorkingDirectory(working_directory_);
    } else {
        process_->setWorkingDirectory(QDir::homePath());
    }

    QString shell = DetectShellProgram();
    QStringList args = DetectShellArguments();

    process_->start(shell, args);

    if (!process_->waitForStarted(3000)) {
        AppendAnsiText("\r\n[Terminal] Failed to start shell: " + shell + "\r\n");
    } else {
        emit TitleChanged("Terminal — " + shell);
    }
}

QString TerminalWidget::DetectShellProgram() const {
#ifdef Q_OS_WIN
    // Prefer PowerShell if available, fall back to cmd.exe
    QString ps = qEnvironmentVariable("SystemRoot") + "\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
    if (QFile::exists(ps)) {
        return ps;
    }
    return "cmd.exe";
#elif defined(Q_OS_MACOS)
    // macOS default is zsh since Catalina
    QString zsh = "/bin/zsh";
    if (QFile::exists(zsh)) return zsh;
    return "/bin/bash";
#else
    // Linux: respect $SHELL, fall back to bash
    QString shell = qEnvironmentVariable("SHELL");
    if (!shell.isEmpty() && QFile::exists(shell)) return shell;
    return "/bin/bash";
#endif
}

QStringList TerminalWidget::DetectShellArguments() const {
#ifdef Q_OS_WIN
    // For powershell: -NoLogo to suppress banner, -NoExit to keep alive
    QString shell = DetectShellProgram();
    if (shell.contains("powershell", Qt::CaseInsensitive)) {
        return {"-NoLogo", "-NoExit", "-Command", "-"};
    }
    // cmd.exe: /Q to disable echo for cleaner output
    return {"/Q"};
#else
    // Interactive login shell
    return {"-i"};
#endif
}

// ============================================================================
// Process I/O Slots
// ============================================================================

void TerminalWidget::OnReadyReadStdout() {
    QByteArray data = process_->readAllStandardOutput();
    QString text = QString::fromUtf8(data);
    AppendAnsiText(text);
}

void TerminalWidget::OnReadyReadStderr() {
    QByteArray data = process_->readAllStandardError();
    QString text = QString::fromUtf8(data);
    AppendAnsiText(text);
}

void TerminalWidget::OnProcessFinished(int exit_code, QProcess::ExitStatus /*status*/) {
    AppendAnsiText(QString("\r\n[Terminal] Process exited with code %1\r\n").arg(exit_code));
    emit ShellFinished(exit_code);
}

void TerminalWidget::OnProcessError(QProcess::ProcessError error) {
    static const char *kErrorNames[] = {
        "FailedToStart", "Crashed", "Timedout", "ReadError", "WriteError", "UnknownError"};
    int idx = static_cast<int>(error);
    if (idx < 0 || idx > 5) idx = 5;
    AppendAnsiText(QString("\r\n[Terminal] Process error: %1\r\n").arg(kErrorNames[idx]));
}

// ============================================================================
// ANSI Text Handling
// ============================================================================

void TerminalWidget::AppendAnsiText(const QString &text) {
    // Move cursor to end.
    QTextCursor cursor = output_area_->textCursor();
    cursor.movePosition(QTextCursor::End);
    output_area_->setTextCursor(cursor);

    // Parse ANSI escape codes and insert styled text.
    QString html = ParseAnsiToHtml(text);

    // Insert as HTML to preserve colours.
    cursor.insertHtml(html);

    // Update prompt position to current end of text.
    cursor.movePosition(QTextCursor::End);
    output_area_->setTextCursor(cursor);
    prompt_position_ = cursor.position();

    // Auto-scroll to bottom.
    QScrollBar *sb = output_area_->verticalScrollBar();
    sb->setValue(sb->maximum());
}

QString TerminalWidget::ParseAnsiToHtml(const QString &raw) const {
    // Basic ANSI SGR code parser.
    // Handles: ESC[0m (reset), ESC[1m (bold), ESC[30-37m (fg colours),
    //          ESC[90-97m (bright fg), ESC[40-47m (bg), ESC[38;5;Nm,
    //          ESC[39m (default fg), ESC[49m (default bg).
    //
    // For a full VT100 emulator we would need something like libvterm,
    // but this covers the most common cases in compiler output.

    static const QRegularExpression kAnsiEscape(R"(\x1B\[[0-9;]*m)");
    static const QRegularExpression kAnsiAny(R"(\x1B\[[^a-zA-Z]*[a-zA-Z])");

    // Standard 8-colour palette (normal + bright).
    static const char *kFgColours[] = {
        "#000000", "#cd3131", "#0dbc79", "#e5e510",
        "#2472c8", "#bc3fbc", "#11a8cd", "#e5e5e5"};
    static const char *kBrightFgColours[] = {
        "#666666", "#f14c4c", "#23d18b", "#f5f543",
        "#3b8eea", "#d670d6", "#29b8db", "#ffffff"};

    QString result;
    result.reserve(raw.size() * 2);

    QString fg = current_fg_colour_;
    QString bg = current_bg_colour_;
    bool bold = bold_active_;

    int pos = 0;
    QRegularExpressionMatchIterator it = kAnsiAny.globalMatch(raw);

    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        int match_start = match.capturedStart();

        // Append text before this escape sequence.
        if (match_start > pos) {
            QString segment = raw.mid(pos, match_start - pos)
                                  .toHtmlEscaped()
                                  .replace("\n", "<br>")
                                  .replace("\r", "")
                                  .replace(" ", "&nbsp;");
            result += QString("<span style=\"color:%1;%2\">%3</span>")
                          .arg(fg,
                               bold ? "font-weight:bold;" : "",
                               segment);
        }
        pos = match.capturedEnd();

        // Only process SGR sequences (ending with 'm').
        QString seq = match.captured();
        if (!seq.endsWith('m')) continue;

        // Extract numeric parameters.
        QString params_str = seq.mid(2, seq.size() - 3); // strip ESC[ and m
        QStringList params = params_str.split(';', Qt::SkipEmptyParts);

        for (int i = 0; i < params.size(); ++i) {
            int code = params[i].toInt();
            if (code == 0) {
                // Reset
                fg = "#cccccc";
                bg = "#1e1e1e";
                bold = false;
            } else if (code == 1) {
                bold = true;
            } else if (code == 22) {
                bold = false;
            } else if (code >= 30 && code <= 37) {
                fg = kFgColours[code - 30];
            } else if (code == 39) {
                fg = "#cccccc";
            } else if (code >= 40 && code <= 47) {
                bg = kFgColours[code - 40]; // reuse palette
            } else if (code == 49) {
                bg = "#1e1e1e";
            } else if (code >= 90 && code <= 97) {
                fg = kBrightFgColours[code - 90];
            } else if (code == 38 && i + 2 < params.size() && params[i + 1].toInt() == 5) {
                // ESC[38;5;Nm — 256-colour mode (approximate with the first 16)
                int n = params[i + 2].toInt();
                if (n < 8)
                    fg = kFgColours[n];
                else if (n < 16)
                    fg = kBrightFgColours[n - 8];
                // Extended 256-colour palette not fully mapped; use default.
                i += 2;
            }
        }
    }

    // Append any remaining text after the last escape.
    if (pos < raw.size()) {
        QString segment = raw.mid(pos)
                              .toHtmlEscaped()
                              .replace("\n", "<br>")
                              .replace("\r", "")
                              .replace(" ", "&nbsp;");
        result += QString("<span style=\"color:%1;%2\">%3</span>")
                      .arg(fg,
                           bold ? "font-weight:bold;" : "",
                           segment);
    }

    return result;
}

// ============================================================================
// Keyboard Input Handling
// ============================================================================

void TerminalWidget::HandleInputKey(QKeyEvent *event) {
    if (!IsRunning()) {
        // If shell is dead, offer restart on Enter.
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            RestartShell();
        }
        return;
    }

    int key = event->key();
    Qt::KeyboardModifiers mods = event->modifiers();

    // Ctrl+C — send interrupt signal.
    if (key == Qt::Key_C && (mods & Qt::ControlModifier)) {
#ifdef Q_OS_WIN
        // On Windows, write Ctrl+C character.
        process_->write("\x03");
#else
        // On Unix, send SIGINT.
        ::kill(process_->processId(), SIGINT);
#endif
        return;
    }

    // Ctrl+L — clear screen.
    if (key == Qt::Key_L && (mods & Qt::ControlModifier)) {
        ClearOutput();
        return;
    }

    // Up arrow — previous history entry.
    if (key == Qt::Key_Up) {
        if (!history_.empty()) {
            if (history_index_ == -1) {
                history_index_ = static_cast<int>(history_.size()) - 1;
            } else if (history_index_ > 0) {
                --history_index_;
            }
            ReplaceCurrentInput(history_[static_cast<size_t>(history_index_)]);
        }
        return;
    }

    // Down arrow — next history entry.
    if (key == Qt::Key_Down) {
        if (!history_.empty() && history_index_ >= 0) {
            if (history_index_ < static_cast<int>(history_.size()) - 1) {
                ++history_index_;
                ReplaceCurrentInput(history_[static_cast<size_t>(history_index_)]);
            } else {
                history_index_ = -1;
                ReplaceCurrentInput("");
            }
        }
        return;
    }

    // Enter — send the current input line to the shell.
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        QString input = GetCurrentInput();
        // Append a newline visually.
        QTextCursor cursor = output_area_->textCursor();
        cursor.movePosition(QTextCursor::End);
        output_area_->setTextCursor(cursor);
        output_area_->insertPlainText("\n");
        prompt_position_ = cursor.position() + 1;

        // Send to process.
        SendCommand(input);
        return;
    }

    // Backspace — only allow deleting characters after the prompt.
    if (key == Qt::Key_Backspace) {
        QTextCursor cursor = output_area_->textCursor();
        if (cursor.position() > prompt_position_) {
            cursor.deletePreviousChar();
            output_area_->setTextCursor(cursor);
        }
        return;
    }

    // Delete key.
    if (key == Qt::Key_Delete) {
        QTextCursor cursor = output_area_->textCursor();
        if (cursor.position() >= prompt_position_) {
            cursor.deleteChar();
            output_area_->setTextCursor(cursor);
        }
        return;
    }

    // Home — move to prompt start.
    if (key == Qt::Key_Home) {
        QTextCursor cursor = output_area_->textCursor();
        cursor.setPosition(prompt_position_);
        output_area_->setTextCursor(cursor);
        return;
    }

    // End — move to end of input.
    if (key == Qt::Key_End) {
        QTextCursor cursor = output_area_->textCursor();
        cursor.movePosition(QTextCursor::End);
        output_area_->setTextCursor(cursor);
        return;
    }

    // Left / Right — navigate within the input area only.
    if (key == Qt::Key_Left) {
        QTextCursor cursor = output_area_->textCursor();
        if (cursor.position() > prompt_position_) {
            cursor.movePosition(QTextCursor::Left);
            output_area_->setTextCursor(cursor);
        }
        return;
    }
    if (key == Qt::Key_Right) {
        QTextCursor cursor = output_area_->textCursor();
        cursor.movePosition(QTextCursor::Right);
        output_area_->setTextCursor(cursor);
        return;
    }

    // Tab — send tab character (for shell completion).
    if (key == Qt::Key_Tab) {
        process_->write("\t");
        return;
    }

    // Regular text input — insert at cursor position (must be after prompt).
    QString text = event->text();
    if (!text.isEmpty()) {
        QTextCursor cursor = output_area_->textCursor();
        if (cursor.position() < prompt_position_) {
            cursor.movePosition(QTextCursor::End);
        }
        cursor.insertText(text);
        output_area_->setTextCursor(cursor);
    }
}

// ============================================================================
// Input Helpers
// ============================================================================

QString TerminalWidget::GetCurrentInput() const {
    QString all_text = output_area_->toPlainText();
    if (prompt_position_ >= all_text.size()) return {};
    return all_text.mid(prompt_position_);
}

void TerminalWidget::ReplaceCurrentInput(const QString &text) {
    QTextCursor cursor = output_area_->textCursor();
    cursor.movePosition(QTextCursor::End);
    int end_pos = cursor.position();

    if (end_pos > prompt_position_) {
        cursor.setPosition(prompt_position_);
        cursor.setPosition(end_pos, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
    }

    cursor.insertText(text);
    output_area_->setTextCursor(cursor);
}

} // namespace polyglot::tools::ui
