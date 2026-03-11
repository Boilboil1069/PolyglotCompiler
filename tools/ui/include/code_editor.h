// code_editor.h — Code editor widget with line numbers and bracket matching.
//
// Extends QPlainTextEdit with line-number gutter, current-line highlighting,
// bracket matching, and zoom support.

#pragma once

#include <QPlainTextEdit>
#include <QWidget>

namespace polyglot::tools::ui {

// Forward declaration of the line-number area helper
class LineNumberArea;

// ============================================================================
// CodeEditor — syntax-aware plain text editor
// ============================================================================

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
    QString file_path_;
};

// ============================================================================
// LineNumberArea — helper widget drawn in the gutter
// ============================================================================

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
