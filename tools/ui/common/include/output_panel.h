// output_panel.h — Tabbed output panel for compiler messages.
//
// Contains tabs for general output, error list, and compiler log.

#pragma once

#include <QHeaderView>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QTableWidget>
#include <QWidget>
#include <QVBoxLayout>

#include <vector>

namespace polyglot::tools::ui {

struct DiagnosticInfo;

// ============================================================================
// OutputPanel — tabbed bottom panel
// ============================================================================

class OutputPanel : public QWidget {
    Q_OBJECT

  public:
    explicit OutputPanel(QWidget *parent = nullptr);
    ~OutputPanel() override;

    // Append text to the output console
    void AppendOutput(const QString &text);

    // Clear the output console
    void ClearOutput();

    // Display diagnostics in the error table
    void ShowDiagnostics(const std::vector<DiagnosticInfo> &diagnostics,
                         const QString &file = QString());

    // Clear the error table
    void ClearDiagnostics();

    // Append text to the compiler log
    void AppendLog(const QString &text);

    // Clear the compiler log
    void ClearLog();

    // Clear everything
    void ClearAll();

    // Switch to a specific tab
    void ShowOutputTab();
    void ShowErrorsTab();
    void ShowLogTab();

  signals:
    // Emitted when user double-clicks an error row
    void ErrorClicked(int line, int column, const QString &file);

  private slots:
    void OnErrorRowDoubleClicked(int row, int column);

  private:
    void SetupUi();

    QVBoxLayout *layout_{nullptr};
    QTabWidget *tab_widget_{nullptr};

    QPlainTextEdit *output_console_{nullptr};
    QTableWidget *error_table_{nullptr};
    QPlainTextEdit *compiler_log_{nullptr};
};

} // namespace polyglot::tools::ui
