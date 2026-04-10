/**
 * @file     code_editor.h
 * @brief    Code editor widget with line numbers and bracket matching
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <QFont>
#include <QPlainTextEdit>
#include <QWidget>

namespace polyglot::tools::ui {

// Forward declaration of the line-number area helper
class LineNumberArea;

// ============================================================================
// CodeEditor — syntax-aware plain text editor
// ============================================================================

/** @brief CodeEditor class. */
class CodeEditor : public QPlainTextEdit {
    Q_OBJECT

  public:
    explicit CodeEditor(QWidget *parent = nullptr);
    ~CodeEditor() override;

    // Line number gutter
    void SetLineNumbersVisible(bool visible);
    bool LineNumbersVisible() const { return line_numbers_visible_; }
    int LineNumberAreaWidth() const;
    void LineNumberAreaPaintEvent(QPaintEvent *event);

    // Zoom
    void ZoomIn(int range = 1);
    void ZoomOut(int range = 1);
    void ZoomReset();

    // Bracket matching
    void HighlightMatchingBrackets();

    // Current file tracking
    void SetFilePath(const QString &path) { file_path_ = path; }
    QString FilePath() const { return file_path_; }

    // Tab width
    void SetTabWidth(int spaces);
    int TabWidth() const { return tab_width_spaces_; }

    // Behaviour and style
    void SetInsertSpaces(bool enabled) { insert_spaces_ = enabled; }
    void SetAutoIndentEnabled(bool enabled) { auto_indent_enabled_ = enabled; }
    void SetBracketMatchingEnabled(bool enabled) { bracket_matching_enabled_ = enabled; }
    void SetHighlightCurrentLineEnabled(bool enabled) {
        highlight_current_line_enabled_ = enabled;
        HighlightCurrentLine();
    }
    void SetEditorFont(const QFont &font);

    // Re-apply theme colors
    void ApplyTheme();

  protected:
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

  private slots:
    void UpdateLineNumberAreaWidth(int new_block_count);
    void HighlightCurrentLine();
    void UpdateLineNumberArea(const QRect &rect, int dy);

  private:
    // Find the matching bracket for the given character and position
    int FindMatchingBracket(int pos) const;

    LineNumberArea *line_number_area_{nullptr};
    bool line_numbers_visible_{true};
    int base_font_size_{11};
    int current_zoom_{0};
    int tab_width_spaces_{4};
    bool insert_spaces_{true};
    bool auto_indent_enabled_{true};
    bool bracket_matching_enabled_{true};
    bool highlight_current_line_enabled_{true};
    QString file_path_;
};

// ============================================================================
// LineNumberArea — helper widget drawn in the gutter
// ============================================================================

/** @brief LineNumberArea class. */
class LineNumberArea : public QWidget {
  public:
    explicit LineNumberArea(CodeEditor *editor)
        : QWidget(editor), editor_(editor) {}

    QSize sizeHint() const override {
        return QSize(editor_->LineNumberAreaWidth(), 0);
    }

  protected:
    void paintEvent(QPaintEvent *event) override {
        editor_->LineNumberAreaPaintEvent(event);
    }

  private:
    CodeEditor *editor_;
};

} // namespace polyglot::tools::ui
