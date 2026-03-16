// code_editor.cpp — Code editor widget implementation.
//
// Provides line numbering, current-line highlighting, bracket matching,
// auto-indent, and zoom support.

#include "tools/ui/common/include/code_editor.h"

#include <QFontDatabase>
#include <QKeyEvent>
#include <QPainter>
#include <QTextBlock>

namespace polyglot::tools::ui {

// ============================================================================
// Construction
// ============================================================================

CodeEditor::CodeEditor(QWidget *parent) : QPlainTextEdit(parent) {
    // Use a monospace font
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(base_font_size_);
    setFont(font);

    // Tab width: 4 spaces
    SetTabWidth(4);

    // Line number area
    line_number_area_ = new LineNumberArea(this);

    connect(this, &CodeEditor::blockCountChanged,
            this, &CodeEditor::UpdateLineNumberAreaWidth);
    connect(this, &CodeEditor::updateRequest,
            this, &CodeEditor::UpdateLineNumberArea);
    connect(this, &CodeEditor::cursorPositionChanged,
            this, &CodeEditor::HighlightCurrentLine);

    UpdateLineNumberAreaWidth(0);
    HighlightCurrentLine();
}

CodeEditor::~CodeEditor() = default;

// ============================================================================
// Line Numbers
// ============================================================================

void CodeEditor::SetLineNumbersVisible(bool visible) {
    line_numbers_visible_ = visible;
    UpdateLineNumberAreaWidth(0);
    line_number_area_->setVisible(visible);
}

int CodeEditor::LineNumberAreaWidth() const {
    if (!line_numbers_visible_) return 0;

    int digits = 1;
    int max_lines = qMax(1, blockCount());
    while (max_lines >= 10) {
        max_lines /= 10;
        ++digits;
    }
    digits = qMax(digits, 3); // Minimum 3 digits wide

    int space = 10 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
    return space;
}

void CodeEditor::UpdateLineNumberAreaWidth(int /*new_block_count*/) {
    setViewportMargins(LineNumberAreaWidth(), 0, 0, 0);
}

void CodeEditor::UpdateLineNumberArea(const QRect &rect, int dy) {
    if (dy) {
        line_number_area_->scroll(0, dy);
    } else {
        line_number_area_->update(0, rect.y(), line_number_area_->width(), rect.height());
    }
    if (rect.contains(viewport()->rect())) {
        UpdateLineNumberAreaWidth(0);
    }
}

void CodeEditor::resizeEvent(QResizeEvent *event) {
    QPlainTextEdit::resizeEvent(event);

    QRect cr = contentsRect();
    line_number_area_->setGeometry(
        QRect(cr.left(), cr.top(), LineNumberAreaWidth(), cr.height()));
}

void CodeEditor::LineNumberAreaPaintEvent(QPaintEvent *event) {
    if (!line_numbers_visible_) return;

    QPainter painter(line_number_area_);
    painter.fillRect(event->rect(), QColor(45, 45, 45));

    QTextBlock block = firstVisibleBlock();
    int block_number = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());

    QFont num_font = font();
    painter.setFont(num_font);

    int current_line = textCursor().blockNumber();

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            QString number = QString::number(block_number + 1);

            if (block_number == current_line) {
                painter.setPen(QColor(255, 255, 255));
            } else {
                painter.setPen(QColor(130, 130, 130));
            }
            painter.drawText(0, top, line_number_area_->width() - 5,
                             fontMetrics().height(),
                             Qt::AlignRight, number);
        }

        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++block_number;
    }
}

// ============================================================================
// Current Line Highlighting
// ============================================================================

void CodeEditor::HighlightCurrentLine() {
    QList<QTextEdit::ExtraSelection> extra_selections;

    if (highlight_current_line_enabled_ && !isReadOnly()) {
        QTextEdit::ExtraSelection selection;
        QColor line_color = QColor(40, 40, 60);
        selection.format.setBackground(line_color);
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = textCursor();
        selection.cursor.clearSelection();
        extra_selections.append(selection);
    }

    setExtraSelections(extra_selections);

    // Preserve current-line highlight and add bracket pairs on top.
    if (bracket_matching_enabled_) {
        HighlightMatchingBrackets();
    }
}

// ============================================================================
// Bracket Matching
// ============================================================================

void CodeEditor::HighlightMatchingBrackets() {
    if (!bracket_matching_enabled_) return;

    QTextCursor cursor = textCursor();
    int pos = cursor.position();
    QTextDocument *doc = document();

    if (pos < 0 || pos >= doc->characterCount()) return;

    QChar ch = doc->characterAt(pos);
    QChar prev_ch = (pos > 0) ? doc->characterAt(pos - 1) : QChar();

    int match_pos = -1;
    int bracket_pos = -1;

    static const QString open_brackets = "({[";
    static const QString close_brackets = ")}]";

    if (open_brackets.contains(ch) || close_brackets.contains(ch)) {
        bracket_pos = pos;
        match_pos = FindMatchingBracket(pos);
    } else if (open_brackets.contains(prev_ch) || close_brackets.contains(prev_ch)) {
        bracket_pos = pos - 1;
        match_pos = FindMatchingBracket(pos - 1);
    }

    if (bracket_pos >= 0 && match_pos >= 0) {
        QList<QTextEdit::ExtraSelection> selections = extraSelections();

        QTextEdit::ExtraSelection sel1;
        sel1.format.setBackground(QColor(80, 80, 120));
        sel1.format.setForeground(QColor(255, 215, 0));
        sel1.cursor = QTextCursor(doc);
        sel1.cursor.setPosition(bracket_pos);
        sel1.cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor);
        selections.append(sel1);

        QTextEdit::ExtraSelection sel2;
        sel2.format.setBackground(QColor(80, 80, 120));
        sel2.format.setForeground(QColor(255, 215, 0));
        sel2.cursor = QTextCursor(doc);
        sel2.cursor.setPosition(match_pos);
        sel2.cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor);
        selections.append(sel2);

        setExtraSelections(selections);
    }
}

int CodeEditor::FindMatchingBracket(int pos) const {
    QTextDocument *doc = document();
    QChar ch = doc->characterAt(pos);

    static const QString open_brackets = "({[";
    static const QString close_brackets = ")}]";

    int dir = 0;
    QChar match_ch;

    int idx = open_brackets.indexOf(ch);
    if (idx >= 0) {
        dir = 1;
        match_ch = close_brackets[idx];
    } else {
        idx = close_brackets.indexOf(ch);
        if (idx >= 0) {
            dir = -1;
            match_ch = open_brackets[idx];
        }
    }

    if (dir == 0) return -1;

    int depth = 1;
    int i = pos + dir;
    int count = doc->characterCount();

    while (i >= 0 && i < count && depth > 0) {
        QChar c = doc->characterAt(i);
        if (c == ch) ++depth;
        else if (c == match_ch) --depth;
        if (depth == 0) return i;
        i += dir;
    }

    return -1;
}

// ============================================================================
// Zoom
// ============================================================================

void CodeEditor::ZoomIn(int range) {
    current_zoom_ += range;
    QFont f = font();
    f.setPointSize(base_font_size_ + current_zoom_);
    setFont(f);
    SetTabWidth(tab_width_spaces_);
}

void CodeEditor::ZoomOut(int range) {
    current_zoom_ -= range;
    if (base_font_size_ + current_zoom_ < 6) {
        current_zoom_ = 6 - base_font_size_;
    }
    QFont f = font();
    f.setPointSize(base_font_size_ + current_zoom_);
    setFont(f);
    SetTabWidth(tab_width_spaces_);
}

void CodeEditor::ZoomReset() {
    current_zoom_ = 0;
    QFont f = font();
    f.setPointSize(base_font_size_);
    setFont(f);
    SetTabWidth(tab_width_spaces_);
}

// ============================================================================
// Tab Width
// ============================================================================

void CodeEditor::SetTabWidth(int spaces) {
    tab_width_spaces_ = qMax(1, spaces);
    QFontMetricsF fm(font());
    qreal tab_width = fm.horizontalAdvance(' ') * tab_width_spaces_;
    setTabStopDistance(tab_width);
}

void CodeEditor::SetEditorFont(const QFont &font) {
    QFont f = font;
    setFont(f);
    base_font_size_ = f.pointSize() > 0 ? f.pointSize() : base_font_size_;
    current_zoom_ = 0;
    SetTabWidth(tab_width_spaces_);
}

// ============================================================================
// Key Press — auto-indent and bracket auto-close
// ============================================================================

void CodeEditor::keyPressEvent(QKeyEvent *event) {
    // Auto-indent on Enter
    if (auto_indent_enabled_ &&
        (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)) {
        QTextCursor cursor = textCursor();
        QString current_line = cursor.block().text();

        // Calculate leading whitespace
        int indent = 0;
        while (indent < current_line.size() && current_line[indent].isSpace()) {
            ++indent;
        }
        QString whitespace = current_line.left(indent);

        // Increase indent if the line ends with { or :
        QString trimmed = current_line.trimmed();
        if (trimmed.endsWith('{') || trimmed.endsWith(':')) {
            whitespace += insert_spaces_
                              ? QString(tab_width_spaces_, QLatin1Char(' '))
                              : QStringLiteral("\t");
        }

        QPlainTextEdit::keyPressEvent(event);
        insertPlainText(whitespace);
        return;
    }

    // Auto-close brackets
    if (event->text() == "{") {
        QPlainTextEdit::keyPressEvent(event);
        insertPlainText("}");
        moveCursor(QTextCursor::Left);
        return;
    }
    if (event->text() == "(") {
        QPlainTextEdit::keyPressEvent(event);
        insertPlainText(")");
        moveCursor(QTextCursor::Left);
        return;
    }
    if (event->text() == "[") {
        QPlainTextEdit::keyPressEvent(event);
        insertPlainText("]");
        moveCursor(QTextCursor::Left);
        return;
    }
    if (event->text() == "\"") {
        QPlainTextEdit::keyPressEvent(event);
        insertPlainText("\"");
        moveCursor(QTextCursor::Left);
        return;
    }

    // Tab inserts spaces
    if (event->key() == Qt::Key_Tab) {
        if (insert_spaces_) {
            insertPlainText(QString(tab_width_spaces_, QLatin1Char(' ')));
        } else {
            QPlainTextEdit::keyPressEvent(event);
        }
        return;
    }

    QPlainTextEdit::keyPressEvent(event);
}

} // namespace polyglot::tools::ui
