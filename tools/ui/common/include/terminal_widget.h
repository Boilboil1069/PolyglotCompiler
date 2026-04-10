/**
 * @file     terminal_widget.h
 * @brief    Embedded terminal emulator for the PolyglotCompiler IDE
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <QFont>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QProcess>
#include <QWidget>
#include <QVBoxLayout>

#include <deque>
#include <string>

namespace polyglot::tools::ui {

// ============================================================================
// TerminalWidget — single embedded terminal session
// ============================================================================

/** @brief TerminalWidget class. */
class TerminalWidget : public QWidget {
    Q_OBJECT

  public:
    explicit TerminalWidget(QWidget *parent = nullptr);
    ~TerminalWidget() override;

    // Set the working directory for the shell process.
    void SetWorkingDirectory(const QString &path);

    // Return the current working directory of the shell.
    QString WorkingDirectory() const;

    // Return true if the shell process is running.
    bool IsRunning() const;

    // Send a command string to the running shell.
    void SendCommand(const QString &command);

    // Clear the terminal output display.
    void ClearOutput();

    // Restart the shell process (kill + re-launch).
    void RestartShell();

    // Stop / kill the shell process.
    void StopShell();

  signals:
    // Emitted when the shell process finishes.
    void ShellFinished(int exit_code);

    // Emitted when the terminal title should be updated (e.g. new cwd).
    void TitleChanged(const QString &title);

  private slots:
    void OnReadyReadStdout();
    void OnReadyReadStderr();
    void OnProcessFinished(int exit_code, QProcess::ExitStatus status);
    void OnProcessError(QProcess::ProcessError error);

  private:
    void SetupUi();
    void StartShell();
    void AppendAnsiText(const QString &text);
    void HandleInputKey(QKeyEvent *event);
    QString DetectShellProgram() const;
    QStringList DetectShellArguments() const;

    // Parse basic ANSI SGR escape codes and return HTML-styled text.
    QString ParseAnsiToHtml(const QString &raw) const;

    // Get the text the user has typed after the current prompt.
    QString GetCurrentInput() const;

    // Replace the current input line with new text (used for history nav).
    void ReplaceCurrentInput(const QString &text);

    // ── UI components ────────────────────────────────────────────────────
    QVBoxLayout *layout_{nullptr};
    QPlainTextEdit *output_area_{nullptr};

    // ── Process ──────────────────────────────────────────────────────────
    QProcess *process_{nullptr};
    QString working_directory_;

    // ── Input handling ───────────────────────────────────────────────────
    // Position in the output_area_ where the current input prompt starts.
    int prompt_position_{0};

    // Command history for Up/Down arrow navigation.
    std::deque<QString> history_;
    int history_index_{-1};
    static constexpr int kMaxHistorySize = 500;

    // ── Colours for ANSI codes ───────────────────────────────────────────
    QString current_fg_colour_{"#cccccc"};
    QString current_bg_colour_{"#1e1e1e"};
    bool bold_active_{false};
};

// ============================================================================
// TerminalInputFilter — event filter for capturing key presses in the text area
// ============================================================================

/** @brief TerminalInputFilter class. */
class TerminalInputFilter : public QObject {
    Q_OBJECT

  public:
    explicit TerminalInputFilter(TerminalWidget *terminal, QPlainTextEdit *output,
                                 QObject *parent = nullptr);

  signals:
    void KeyPressed(QKeyEvent *event);

  protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

  private:
    TerminalWidget *terminal_;
    QPlainTextEdit *output_;
};

} // namespace polyglot::tools::ui
