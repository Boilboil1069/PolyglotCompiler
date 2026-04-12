/**
 * @file     code_editor.cpp
 * @brief    Code editor widget implementation — line numbers, bracket matching,
 *           auto-completion, diagnostics overlay (squiggly lines + inline hints),
 *           go-to-definition, and minimap
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include "tools/ui/common/include/code_editor.h"
#include "tools/ui/common/include/compiler_service.h"
#include "tools/ui/common/include/theme_manager.h"

#include <QApplication>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QTextBlock>
#include <QToolTip>

#include <algorithm>
#include <cmath>
#include <unordered_set>

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

    // Minimap
    minimap_ = new MinimapWidget(this);
    connect(minimap_, &MinimapWidget::ScrollRequested,
            this, [this](int line) {
        QTextBlock block = document()->findBlockByNumber(qMax(0, line));
        if (block.isValid()) {
            QTextCursor cursor(block);
            setTextCursor(cursor);
            centerCursor();
        }
    });

    // Completion popup
    completion_popup_ = new CompletionPopup(this);
    connect(completion_popup_, &QListView::clicked,
            this, &CodeEditor::OnCompletionAccepted);

    connect(this, &CodeEditor::blockCountChanged,
            this, &CodeEditor::UpdateLineNumberAreaWidth);
    connect(this, &CodeEditor::updateRequest,
            this, &CodeEditor::UpdateLineNumberArea);
    connect(this, &CodeEditor::cursorPositionChanged,
            this, &CodeEditor::HighlightCurrentLine);

    // Update minimap on text changes
    connect(this, &CodeEditor::textChanged,
            minimap_, &MinimapWidget::UpdateContent);
    connect(verticalScrollBar(), &QScrollBar::valueChanged,
            minimap_, QOverload<>::of(&MinimapWidget::update));

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
    int right_margin = minimap_visible_ ? MinimapWidget::kWidth : 0;
    setViewportMargins(LineNumberAreaWidth(), 0, right_margin, 0);
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

    // Position the minimap on the far right
    if (minimap_visible_) {
        minimap_->setGeometry(
            QRect(cr.right() - MinimapWidget::kWidth, cr.top(),
                  MinimapWidget::kWidth, cr.height()));
        minimap_->show();
    } else {
        minimap_->hide();
    }
}

void CodeEditor::LineNumberAreaPaintEvent(QPaintEvent *event) {
    if (!line_numbers_visible_) return;

    QPainter painter(line_number_area_);
    const auto &tc = ThemeManager::Instance().Active();
    painter.fillRect(event->rect(), tc.editor_line_number_bg);

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
                painter.setPen(tc.text);
            } else {
                painter.setPen(tc.editor_line_number);
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
        QColor line_color = ThemeManager::Instance().Active().editor_current_line;
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
        const auto &tc = ThemeManager::Instance().Active();
        // Use selection color for bracket background, accent for foreground
        QColor bracket_bg = tc.selection;
        QColor bracket_fg = tc.accent;

        QTextEdit::ExtraSelection sel1;
        sel1.format.setBackground(bracket_bg);
        sel1.format.setForeground(bracket_fg);
        sel1.cursor = QTextCursor(doc);
        sel1.cursor.setPosition(bracket_pos);
        sel1.cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor);
        selections.append(sel1);

        QTextEdit::ExtraSelection sel2;
        sel2.format.setBackground(bracket_bg);
        sel2.format.setForeground(bracket_fg);
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

void CodeEditor::ApplyTheme() {
    const auto &tc = ThemeManager::Instance().Active();
    setStyleSheet(QString(
        "QPlainTextEdit { background: %1; border: none; "
        "selection-background-color: %2; }")
        .arg(tc.editor_background.name(), tc.editor_selection.name()));
    QPalette pal = palette();
    pal.setColor(QPalette::Text, tc.editor_text);
    pal.setColor(QPalette::Base, tc.editor_background);
    setPalette(pal);
    // Repaint the gutter, current line and minimap
    HighlightCurrentLine();
    line_number_area_->update();
    minimap_->UpdateContent();
}

// ============================================================================
// Key Press — auto-indent, bracket auto-close, and completion trigger
// ============================================================================

void CodeEditor::keyPressEvent(QKeyEvent *event) {
    // If the completion popup is visible, handle navigation keys
    if (completion_popup_->isVisible()) {
        if (event->key() == Qt::Key_Escape) {
            completion_popup_->Hide();
            return;
        }
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            QModelIndex idx = completion_popup_->currentIndex();
            if (idx.isValid()) {
                OnCompletionAccepted(idx);
            }
            completion_popup_->Hide();
            return;
        }
        if (event->key() == Qt::Key_Down) {
            int row = completion_popup_->currentIndex().row();
            int count = completion_popup_->Model()->rowCount();
            if (row + 1 < count) {
                completion_popup_->setCurrentIndex(
                    completion_popup_->Model()->index(row + 1, 0));
            }
            return;
        }
        if (event->key() == Qt::Key_Up) {
            int row = completion_popup_->currentIndex().row();
            if (row > 0) {
                completion_popup_->setCurrentIndex(
                    completion_popup_->Model()->index(row - 1, 0));
            }
            return;
        }
    }

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
        completion_popup_->Hide();
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

    // Trigger auto-completion after typing identifier characters
    if (!event->text().isEmpty()) {
        QChar ch = event->text().at(0);
        if (ch.isLetterOrNumber() || ch == '_') {
            TriggerCompletion();
        } else {
            completion_popup_->Hide();
        }
    }
}

// ============================================================================
// Auto-Completion
// ============================================================================

QString CodeEditor::WordUnderCursor() const {
    QTextCursor tc = textCursor();
    // Move backward to the start of the word
    while (tc.position() > 0) {
        tc.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, 1);
        QChar ch = tc.selectedText().at(0);
        if (!ch.isLetterOrNumber() && ch != '_') {
            tc.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 1);
            break;
        }
        tc.clearSelection();
        tc.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, 1);
    }
    tc.movePosition(QTextCursor::EndOfWord, QTextCursor::KeepAnchor);
    // We actually want just the prefix up to the cursor, not the full word
    QTextCursor prefix_cursor = textCursor();
    prefix_cursor.movePosition(QTextCursor::StartOfWord, QTextCursor::KeepAnchor);
    return prefix_cursor.selectedText();
}

std::vector<CompletionItem> CodeEditor::CollectIdentifierCompletions(
    const std::string &prefix) const {
    // Scan the entire document for identifiers (lexical completion)
    std::unordered_set<std::string> seen;
    std::vector<CompletionItem> result;

    QString doc_text = toPlainText();
    // Simple identifier extraction: sequences of [A-Za-z_][A-Za-z0-9_]*
    int i = 0;
    int len = doc_text.length();
    while (i < len) {
        QChar ch = doc_text[i];
        if (ch.isLetter() || ch == '_') {
            int start = i;
            while (i < len && (doc_text[i].isLetterOrNumber() || doc_text[i] == '_')) {
                ++i;
            }
            std::string word = doc_text.mid(start, i - start).toStdString();
            if (word.size() >= 2 && word != prefix && seen.insert(word).second) {
                // Check prefix match (case-insensitive for Ploy uppercase keywords)
                bool match = false;
                if (prefix.empty()) {
                    match = false; // Don't suggest everything when no prefix
                } else {
                    std::string lower_word = word;
                    std::string lower_prefix = prefix;
                    std::transform(lower_word.begin(), lower_word.end(),
                                   lower_word.begin(), ::tolower);
                    std::transform(lower_prefix.begin(), lower_prefix.end(),
                                   lower_prefix.begin(), ::tolower);
                    match = lower_word.substr(0, lower_prefix.size()) == lower_prefix;
                }
                if (match) {
                    CompletionItem item;
                    item.label = word;
                    item.kind = "identifier";
                    item.detail = "identifier";
                    item.insert_text = word;
                    result.push_back(std::move(item));
                }
            }
        } else {
            ++i;
        }
    }
    return result;
}

void CodeEditor::TriggerCompletion() {
    QString prefix = WordUnderCursor();
    if (prefix.length() < 1) {
        completion_popup_->Hide();
        return;
    }

    std::vector<CompletionItem> items;

    // Get compiler-provided completions (keywords + snippets)
    if (compiler_service_) {
        QTextCursor tc = textCursor();
        size_t line = static_cast<size_t>(tc.blockNumber() + 1);
        size_t col = static_cast<size_t>(tc.columnNumber() + 1);
        auto compiler_items = compiler_service_->Complete(
            toPlainText().toStdString(), language_, line, col);

        // Filter by prefix (case-insensitive)
        std::string pfx = prefix.toStdString();
        std::string lower_pfx = pfx;
        std::transform(lower_pfx.begin(), lower_pfx.end(),
                       lower_pfx.begin(), ::tolower);
        for (auto &ci : compiler_items) {
            std::string lower_label = ci.label;
            std::transform(lower_label.begin(), lower_label.end(),
                           lower_label.begin(), ::tolower);
            if (lower_label.substr(0, lower_pfx.size()) == lower_pfx) {
                items.push_back(std::move(ci));
            }
        }
    }

    // Add identifier completions from the document
    auto id_items = CollectIdentifierCompletions(prefix.toStdString());
    // Deduplicate against compiler-provided labels
    std::unordered_set<std::string> existing;
    for (const auto &ci : items) existing.insert(ci.label);
    for (auto &ci : id_items) {
        if (existing.insert(ci.label).second) {
            items.push_back(std::move(ci));
        }
    }

    if (items.empty()) {
        completion_popup_->Hide();
        return;
    }

    // Position the popup below the cursor
    QTextCursor tc = textCursor();
    QRect cursor_rect = cursorRect(tc);
    QPoint pos = mapToGlobal(cursor_rect.bottomLeft());
    pos.setY(pos.y() + 2);

    completion_popup_->ShowCompletions(items, pos);
}

void CodeEditor::OnCompletionAccepted(const QModelIndex &index) {
    if (!index.isValid()) return;

    const CompletionItem &item = completion_popup_->Model()->ItemAt(index.row());

    // Replace the current prefix with the completion text
    QTextCursor tc = textCursor();
    tc.movePosition(QTextCursor::StartOfWord, QTextCursor::KeepAnchor);
    tc.insertText(QString::fromStdString(item.label));
    setTextCursor(tc);

    completion_popup_->Hide();
}

// ============================================================================
// Diagnostics Overlay — squiggly underlines + inline hints
// ============================================================================

void CodeEditor::SetDiagnostics(const std::vector<DiagnosticInfo> &diagnostics) {
    diagnostics_ = diagnostics;
    viewport()->update(); // trigger repaint
}

void CodeEditor::ClearDiagnostics() {
    diagnostics_.clear();
    viewport()->update();
}

void CodeEditor::DrawWavyUnderline(QPainter &painter, const QRectF &rect,
                                    const QColor &color) const {
    painter.save();
    QPen pen(color, 1.2);
    painter.setPen(pen);
    painter.setRenderHint(QPainter::Antialiasing, true);

    constexpr double kWaveLength = 4.0;
    constexpr double kAmplitude = 2.0;

    double y_base = rect.bottom();
    double x_start = rect.left();
    double x_end = rect.right();

    QPainterPath path;
    path.moveTo(x_start, y_base);

    bool up = true;
    for (double x = x_start; x < x_end; x += kWaveLength) {
        double x_next = qMin(x + kWaveLength, x_end);
        double y_ctrl = up ? (y_base - kAmplitude) : (y_base + kAmplitude);
        path.quadTo((x + x_next) / 2.0, y_ctrl, x_next, y_base);
        up = !up;
    }

    painter.drawPath(path);
    painter.restore();
}

void CodeEditor::paintEvent(QPaintEvent *event) {
    // Let the base class draw the text first
    QPlainTextEdit::paintEvent(event);

    if (diagnostics_.empty()) return;

    QPainter painter(viewport());
    const auto &tc = ThemeManager::Instance().Active();

    QTextBlock block = firstVisibleBlock();
    int block_number = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());

    QFontMetricsF fm(font());

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            int line = block_number + 1; // 1-based
            QString block_text = block.text();

            for (const auto &diag : diagnostics_) {
                if (static_cast<int>(diag.line) != line) continue;

                // Determine color based on severity
                QColor underline_color = tc.error;
                if (diag.severity == "warning") {
                    underline_color = tc.warning;
                } else if (diag.severity == "note") {
                    underline_color = tc.info;
                }

                // Calculate the underline region
                int col_start = qMax(0, static_cast<int>(diag.column) - 1);
                int col_end = col_start + 1;
                if (diag.end_column > diag.column) {
                    col_end = static_cast<int>(diag.end_column) - 1;
                }
                // Extend to at least the rest of the token / identifier
                if (col_end <= col_start + 1 && col_start < block_text.length()) {
                    int e = col_start;
                    while (e < block_text.length() &&
                           (block_text[e].isLetterOrNumber() || block_text[e] == '_')) {
                        ++e;
                    }
                    if (e > col_start) col_end = e;
                }
                col_end = qMin(col_end, block_text.length());

                // Compute pixel coordinates
                double x_start = fm.horizontalAdvance(block_text.left(col_start));
                double x_end = fm.horizontalAdvance(block_text.left(col_end));

                QRectF wave_rect(x_start + contentOffset().x(),
                                 static_cast<double>(top),
                                 x_end - x_start,
                                 fm.height());

                DrawWavyUnderline(painter, wave_rect, underline_color);

                // ── Inline diagnostic hint ───────────────────────────
                // Draw a short message at the end of the line in grey
                QString hint = QString::fromStdString(
                    " \u2190 " + diag.message);
                if (hint.length() > 60) {
                    hint = hint.left(57) + "...";
                }
                double text_end_x = fm.horizontalAdvance(block_text) + 12.0;
                QColor hint_color = tc.text_secondary;
                hint_color.setAlpha(160);
                painter.setPen(hint_color);
                QFont hint_font = font();
                hint_font.setItalic(true);
                hint_font.setPointSizeF(hint_font.pointSizeF() * 0.9);
                painter.setFont(hint_font);
                painter.drawText(
                    QPointF(text_end_x + contentOffset().x(),
                            static_cast<double>(top) + fm.ascent()),
                    hint);
                painter.setFont(font()); // restore
            }
        }

        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++block_number;
    }
}

// ============================================================================
// Go-To-Definition — Ctrl+Click
// ============================================================================

void CodeEditor::mousePressEvent(QMouseEvent *event) {
    if (goto_def_enabled_ &&
        event->button() == Qt::LeftButton &&
        event->modifiers() & Qt::ControlModifier) {

        // Find the word under the click position
        QTextCursor tc = cursorForPosition(event->pos());
        tc.select(QTextCursor::WordUnderCursor);
        QString word = tc.selectedText().trimmed();
        if (!word.isEmpty()) {
            int line = tc.blockNumber() + 1;
            int col = tc.columnNumber() + 1;
            emit GoToDefinitionRequested(word, line, col);
            event->accept();
            return;
        }
    }
    QPlainTextEdit::mousePressEvent(event);
}

void CodeEditor::mouseMoveEvent(QMouseEvent *event) {
    // Show hand cursor when Ctrl is held and hovering over a word
    if (goto_def_enabled_ && event->modifiers() & Qt::ControlModifier) {
        QTextCursor tc = cursorForPosition(event->pos());
        tc.select(QTextCursor::WordUnderCursor);
        if (!tc.selectedText().trimmed().isEmpty()) {
            viewport()->setCursor(Qt::PointingHandCursor);
        } else {
            viewport()->setCursor(Qt::IBeamCursor);
        }
    } else {
        viewport()->setCursor(Qt::IBeamCursor);
    }
    QPlainTextEdit::mouseMoveEvent(event);
}

// ============================================================================
// Minimap
// ============================================================================

void CodeEditor::SetMinimapVisible(bool visible) {
    minimap_visible_ = visible;
    UpdateLineNumberAreaWidth(0);
    // Force re-layout
    QResizeEvent e(size(), size());
    resizeEvent(&e);
}

// ============================================================================
// CompletionModel
// ============================================================================

void CompletionModel::SetItems(const std::vector<CompletionItem> &items) {
    beginResetModel();
    items_ = items;
    endResetModel();
}

const CompletionItem &CompletionModel::ItemAt(int row) const {
    return items_.at(static_cast<size_t>(row));
}

int CompletionModel::rowCount(const QModelIndex & /*parent*/) const {
    return static_cast<int>(items_.size());
}

QVariant CompletionModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= static_cast<int>(items_.size()))
        return {};

    const auto &item = items_[static_cast<size_t>(index.row())];
    switch (role) {
        case Qt::DisplayRole:
            return QString::fromStdString(item.label);
        case Qt::ToolTipRole:
            return QString::fromStdString(item.detail);
        default:
            return {};
    }
}

// ============================================================================
// CompletionPopup
// ============================================================================

CompletionPopup::CompletionPopup(QWidget *parent)
    : QListView(parent) {
    setModel(&model_);
    setWindowFlags(Qt::ToolTip);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setMaximumHeight(200);
    setMinimumWidth(200);

    // Styling
    setStyleSheet(
        "QListView { background: #252526; color: #cccccc; border: 1px solid #454545; "
        "font-family: monospace; font-size: 10pt; }"
        "QListView::item:selected { background: #094771; }");
}

void CompletionPopup::ShowCompletions(const std::vector<CompletionItem> &items,
                                       const QPoint &pos) {
    model_.SetItems(items);
    int visible_rows = qMin(static_cast<int>(items.size()), 10);
    int row_height = sizeHintForRow(0);
    if (row_height <= 0) row_height = 20;
    setFixedHeight(visible_rows * row_height + 4);
    setFixedWidth(qMax(250, sizeHintForColumn(0) + 30));
    move(pos);
    show();
    if (model_.rowCount() > 0) {
        setCurrentIndex(model_.index(0, 0));
    }
}

void CompletionPopup::Hide() {
    hide();
}

// ============================================================================
// MinimapWidget
// ============================================================================

MinimapWidget::MinimapWidget(CodeEditor *editor)
    : QWidget(editor), editor_(editor) {
    setFixedWidth(kWidth);
    setMouseTracking(true);
}

void MinimapWidget::UpdateContent() {
    cache_dirty_ = true;
    update();
}

void MinimapWidget::paintEvent(QPaintEvent * /*event*/) {
    QPainter painter(this);
    const auto &tc = ThemeManager::Instance().Active();

    // Background — slightly lighter/darker than editor background
    QColor bg = tc.editor_background;
    bg = bg.lighter(110);
    painter.fillRect(rect(), bg);

    QTextDocument *doc = editor_->document();
    int total_lines = doc->blockCount();
    if (total_lines <= 0) return;

    // Determine visible range for the viewport indicator
    int first_visible = editor_->FirstVisibleBlock().blockNumber();
    QTextBlock last_block = editor_->FirstVisibleBlock();
    int visible_count = 0;
    while (last_block.isValid() && last_block.isVisible()) {
        QRectF geom = editor_->BlockBoundingGeometry(last_block)
                          .translated(editor_->ContentOffset());
        if (geom.top() > editor_->viewport()->height()) break;
        last_block = last_block.next();
        ++visible_count;
    }

    // Each line maps to a vertical pixel range
    double line_height = qMax(1.0, static_cast<double>(height()) / total_lines);
    // Cap to avoid very tall lines when the file is small
    if (line_height > 3.0) line_height = 3.0;

    // Draw each line as a thin horizontal segment colored by text density
    if (cache_dirty_) {
        cache_ = QPixmap(size());
        cache_.fill(Qt::transparent);
        QPainter cp(&cache_);

        QColor text_color = tc.editor_text;
        text_color.setAlpha(80);
        QColor kw_color = QColor(86, 156, 214);
        kw_color.setAlpha(100);

        QTextBlock block = doc->begin();
        int line_idx = 0;
        while (block.isValid()) {
            QString text = block.text();
            double y = line_idx * line_height;
            if (y > height()) break;

            // Draw a thin representation of the line
            int visible_chars = qMin(text.length(), kWidth);
            if (!text.trimmed().isEmpty()) {
                // Draw a simple bar whose length reflects the line length
                int bar_len = qMin(static_cast<int>(
                    static_cast<double>(visible_chars) / 120.0 * kWidth), kWidth);
                // Indent-aware: skip leading whitespace in minimap
                int leading = 0;
                while (leading < text.length() && text[leading].isSpace()) ++leading;
                int x_start = static_cast<int>(
                    static_cast<double>(leading) / 120.0 * kWidth);
                x_start = qMin(x_start, kWidth - 2);

                cp.setPen(Qt::NoPen);
                cp.setBrush(text_color);
                cp.drawRect(QRectF(x_start, y, bar_len - x_start, qMax(line_height, 1.0)));
            }

            block = block.next();
            ++line_idx;
        }
        cache_dirty_ = false;
    }

    painter.drawPixmap(0, 0, cache_);

    // Draw viewport indicator (semi-transparent rectangle)
    double y_start = first_visible * line_height;
    double y_end = (first_visible + visible_count) * line_height;
    QColor vp_color = tc.accent;
    vp_color.setAlpha(40);
    painter.fillRect(QRectF(0, y_start, width(), y_end - y_start), vp_color);

    // Draw border on the left edge
    painter.setPen(QPen(tc.border, 1));
    painter.drawLine(0, 0, 0, height());
}

void MinimapWidget::mousePressEvent(QMouseEvent *event) {
    int total_lines = editor_->document()->blockCount();
    if (total_lines <= 0) return;

    double line_height = qMax(1.0, static_cast<double>(height()) / total_lines);
    if (line_height > 3.0) line_height = 3.0;

    int target_line = static_cast<int>(event->pos().y() / line_height);
    target_line = qBound(0, target_line, total_lines - 1);
    emit ScrollRequested(target_line);
}

void MinimapWidget::mouseMoveEvent(QMouseEvent *event) {
    if (event->buttons() & Qt::LeftButton) {
        // Drag to scroll
        int total_lines = editor_->document()->blockCount();
        if (total_lines <= 0) return;

        double line_height = qMax(1.0, static_cast<double>(height()) / total_lines);
        if (line_height > 3.0) line_height = 3.0;

        int target_line = static_cast<int>(event->pos().y() / line_height);
        target_line = qBound(0, target_line, total_lines - 1);
        emit ScrollRequested(target_line);
    }
}

} // namespace polyglot::tools::ui
