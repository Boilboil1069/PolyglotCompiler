/**
 * @file     output_panel.cpp
 * @brief    Tabbed output panel implementation
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include "tools/ui/common/include/output_panel.h"
#include "tools/ui/common/include/compiler_service.h"
#include "tools/ui/common/include/theme_manager.h"

#include <QFont>
#include <QFontDatabase>

namespace polyglot::tools::ui {

// ============================================================================
// Construction
// ============================================================================

OutputPanel::OutputPanel(QWidget *parent) : QWidget(parent) {
    SetupUi();
}

OutputPanel::~OutputPanel() = default;

// ============================================================================
// UI Setup
// ============================================================================

void OutputPanel::SetupUi() {
    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);

    tab_widget_ = new QTabWidget(this);
    tab_widget_->setTabPosition(QTabWidget::South);
    tab_widget_->setStyleSheet(ThemeManager::Instance().TabWidgetStylesheet(true));

    // Output console
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(10);

    output_console_ = new QPlainTextEdit(this);
    output_console_->setReadOnly(true);
    output_console_->setFont(mono);
    output_console_->setStyleSheet(ThemeManager::Instance().PlainTextEditStylesheet());
    output_console_->setLineWrapMode(QPlainTextEdit::NoWrap);
    tab_widget_->addTab(output_console_, "Output");

    // Error table
    error_table_ = new QTableWidget(this);
    error_table_->setColumnCount(5);
    error_table_->setHorizontalHeaderLabels(
        {"Severity", "Code", "Line", "Column", "Message"});
    error_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    error_table_->setSelectionMode(QAbstractItemView::SingleSelection);
    error_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    error_table_->horizontalHeader()->setStretchLastSection(true);
    error_table_->horizontalHeader()->setDefaultSectionSize(80);
    error_table_->setColumnWidth(0, 70);
    error_table_->setColumnWidth(1, 60);
    error_table_->setColumnWidth(2, 50);
    error_table_->setColumnWidth(3, 60);
    error_table_->verticalHeader()->setVisible(false);
    {
        const auto &tc = ThemeManager::Instance().Active();
        error_table_->setStyleSheet(
            QString("QTableWidget { background: %1; color: %2; border: none; "
                    "gridline-color: %3; font-size: 12px; }"
                    "QHeaderView::section { background: %4; color: %5; "
                    "border: none; padding: 4px; font-weight: bold; }"
                    "QTableWidget::item:selected { background: %6; }")
                .arg(tc.background.name(), tc.text.name(), tc.border.name(),
                     tc.surface.name(), tc.text.name(), tc.selection.name()));
    }
    tab_widget_->addTab(error_table_, "Errors (0)");

    connect(error_table_, &QTableWidget::cellDoubleClicked,
            this, &OutputPanel::OnErrorRowDoubleClicked);

    // Compiler log
    compiler_log_ = new QPlainTextEdit(this);
    compiler_log_->setReadOnly(true);
    compiler_log_->setFont(mono);
    compiler_log_->setStyleSheet(ThemeManager::Instance().PlainTextEditStylesheet());
    compiler_log_->setLineWrapMode(QPlainTextEdit::NoWrap);
    tab_widget_->addTab(compiler_log_, "Compiler Log");

    layout_->addWidget(tab_widget_);
}

// ============================================================================
// Output Console
// ============================================================================

void OutputPanel::AppendOutput(const QString &text) {
    output_console_->appendPlainText(text);
}

void OutputPanel::ClearOutput() {
    output_console_->clear();
}

// ============================================================================
// Diagnostics Table
// ============================================================================

void OutputPanel::ShowDiagnostics(const std::vector<DiagnosticInfo> &diagnostics,
                                  const QString &file) {
    error_table_->setRowCount(0);

    int error_count = 0;
    int warning_count = 0;

    for (const auto &d : diagnostics) {
        int row = error_table_->rowCount();
        error_table_->insertRow(row);

        // Severity with color-coded icon
        auto *severity_item = new QTableWidgetItem(QString::fromStdString(d.severity));
        severity_item->setData(Qt::UserRole, file);
        if (d.severity == "error") {
            severity_item->setForeground(QColor(244, 71, 71));
            ++error_count;
        } else if (d.severity == "warning") {
            severity_item->setForeground(QColor(205, 155, 50));
            ++warning_count;
        } else {
            severity_item->setForeground(QColor(100, 150, 255));
        }
        error_table_->setItem(row, 0, severity_item);

        error_table_->setItem(row, 1, new QTableWidgetItem(
            QString::fromStdString(d.code)));
        error_table_->setItem(row, 2, new QTableWidgetItem(
            QString::number(d.line)));
        error_table_->setItem(row, 3, new QTableWidgetItem(
            QString::number(d.column)));
        error_table_->setItem(row, 4, new QTableWidgetItem(
            QString::fromStdString(d.message)));
    }

    // Update tab title with counts
    QString title = QString("Errors (%1 error%2, %3 warning%4)")
        .arg(error_count).arg(error_count == 1 ? "" : "s")
        .arg(warning_count).arg(warning_count == 1 ? "" : "s");
    tab_widget_->setTabText(1, title);

    // Auto-switch to errors tab when there are errors
    if (error_count > 0) {
        tab_widget_->setCurrentIndex(1);
    }
}

void OutputPanel::ClearDiagnostics() {
    error_table_->setRowCount(0);
    tab_widget_->setTabText(1, "Errors (0)");
}

// ============================================================================
// Compiler Log
// ============================================================================

void OutputPanel::AppendLog(const QString &text) {
    compiler_log_->appendPlainText(text);
}

void OutputPanel::ClearLog() {
    compiler_log_->clear();
}

// ============================================================================
// Clear All
// ============================================================================

void OutputPanel::ClearAll() {
    ClearOutput();
    ClearDiagnostics();
    ClearLog();
}

// ============================================================================
// Theme
// ============================================================================

void OutputPanel::ApplyTheme() {
    const auto &tm = ThemeManager::Instance();
    const auto &tc = tm.Active();

    tab_widget_->setStyleSheet(tm.TabWidgetStylesheet(true));
    output_console_->setStyleSheet(tm.PlainTextEditStylesheet());
    compiler_log_->setStyleSheet(tm.PlainTextEditStylesheet());

    error_table_->setStyleSheet(
        QString("QTableWidget { background: %1; color: %2; border: none; "
                "gridline-color: %3; font-size: 12px; }"
                "QHeaderView::section { background: %4; color: %5; "
                "border: none; padding: 4px; font-weight: bold; }"
                "QTableWidget::item:selected { background: %6; }")
            .arg(tc.background.name(), tc.text.name(), tc.border.name(),
                 tc.surface.name(), tc.text.name(), tc.selection.name()));
}

// ============================================================================
// Tab Switching
// ============================================================================

void OutputPanel::ShowOutputTab() {
    tab_widget_->setCurrentIndex(0);
}

void OutputPanel::ShowErrorsTab() {
    tab_widget_->setCurrentIndex(1);
}

void OutputPanel::ShowLogTab() {
    tab_widget_->setCurrentIndex(2);
}

// ============================================================================
// Error Row Double-Click
// ============================================================================

void OutputPanel::OnErrorRowDoubleClicked(int row, int /*column*/) {
    auto *severity_item = error_table_->item(row, 0);
    auto *line_item = error_table_->item(row, 2);
    auto *col_item = error_table_->item(row, 3);
    QString file = severity_item ? severity_item->data(Qt::UserRole).toString()
                                 : QString();

    if (line_item && col_item) {
        int line = line_item->text().toInt();
        int col = col_item->text().toInt();
        emit ErrorClicked(line, col, file);
    }
}

} // namespace polyglot::tools::ui
