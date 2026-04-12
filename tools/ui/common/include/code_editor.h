/**
 * @file     code_editor.h
 * @brief    Code editor widget with line numbers, bracket matching,
 *           auto-completion, diagnostics overlay, and minimap
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <QAbstractListModel>
#include <QFont>
#include <QListView>
#include <QPlainTextEdit>
#include <QTextBlock>
#include <QWidget>

#include <string>
#include <vector>

#include "tools/ui/common/include/compiler_service.h"

namespace polyglot::tools::ui {

class CompilerService;

// Forward declaration of helper widgets
class LineNumberArea;
class MinimapWidget;
class CompletionPopup;

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

    // ── Auto-completion ──────────────────────────────────────────────────
    void SetCompilerService(CompilerService *service) { compiler_service_ = service; }
    void SetLanguage(const std::string &lang) { language_ = lang; }
    void TriggerCompletion();

    // ── Diagnostics overlay (squiggly lines + inline hints) ──────────────
    void SetDiagnostics(const std::vector<DiagnosticInfo> &diagnostics);
    void ClearDiagnostics();

    // ── Minimap ──────────────────────────────────────────────────────────
    void SetMinimapVisible(bool visible);
    bool MinimapVisible() const { return minimap_visible_; }

    // ── Go-to-definition ─────────────────────────────────────────────────
    // Emitted when user Ctrl+Clicks a symbol — the MainWindow handles the
    // actual navigation logic.
    void SetGoToDefinitionEnabled(bool enabled) { goto_def_enabled_ = enabled; }

    // ── Accessors for child widgets (e.g. MinimapWidget) ─────────────────
    QTextBlock FirstVisibleBlock() const { return firstVisibleBlock(); }
    QRectF BlockBoundingGeometry(const QTextBlock &block) const {
        return blockBoundingGeometry(block);
    }
    QPointF ContentOffset() const { return contentOffset(); }

  signals:
    void GoToDefinitionRequested(const QString &symbol, int line, int column);

  protected:
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

  private slots:
    void UpdateLineNumberAreaWidth(int new_block_count);
    void HighlightCurrentLine();
    void UpdateLineNumberArea(const QRect &rect, int dy);
    void OnCompletionAccepted(const QModelIndex &index);

  private:
    // Find the matching bracket for the given character and position
    int FindMatchingBracket(int pos) const;

    // Collect identifier-based completions from the current document text
    std::vector<CompletionItem> CollectIdentifierCompletions(
        const std::string &prefix) const;

    // Compute the word (prefix) at the current cursor position
    QString WordUnderCursor() const;

    // Draw wavy underline for a text range (used by diagnostics overlay)
    void DrawWavyUnderline(QPainter &painter, const QRectF &rect,
                           const QColor &color) const;

    LineNumberArea *line_number_area_{nullptr};
    MinimapWidget *minimap_{nullptr};
    CompletionPopup *completion_popup_{nullptr};
    CompilerService *compiler_service_{nullptr};
    std::string language_;

    bool line_numbers_visible_{true};
    int base_font_size_{11};
    int current_zoom_{0};
    int tab_width_spaces_{4};
    bool insert_spaces_{true};
    bool auto_indent_enabled_{true};
    bool bracket_matching_enabled_{true};
    bool highlight_current_line_enabled_{true};
    bool minimap_visible_{true};
    bool goto_def_enabled_{true};
    QString file_path_;

    // Diagnostics data — set by SetDiagnostics()
    std::vector<DiagnosticInfo> diagnostics_;
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

// ============================================================================
// CompletionPopup — lightweight auto-completion list
// ============================================================================

/** @brief CompletionModel — data model for the completion list. */
class CompletionModel : public QAbstractListModel {
    Q_OBJECT
  public:
    explicit CompletionModel(QObject *parent = nullptr)
        : QAbstractListModel(parent) {}

    void SetItems(const std::vector<CompletionItem> &items);
    const CompletionItem &ItemAt(int row) const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index,
                  int role = Qt::DisplayRole) const override;

  private:
    std::vector<CompletionItem> items_;
};

/** @brief CompletionPopup — drop-down list for auto-completion. */
class CompletionPopup : public QListView {
    Q_OBJECT
  public:
    explicit CompletionPopup(QWidget *parent = nullptr);

    void ShowCompletions(const std::vector<CompletionItem> &items,
                         const QPoint &pos);
    void Hide();

    CompletionModel *Model() { return &model_; }

  private:
    CompletionModel model_;
};

// ============================================================================
// MinimapWidget — condensed file overview drawn on the right edge
// ============================================================================

/** @brief MinimapWidget class. */
class MinimapWidget : public QWidget {
    Q_OBJECT
  public:
    explicit MinimapWidget(CodeEditor *editor);

    void UpdateContent();
    QSize sizeHint() const override { return QSize(kWidth, 0); }

    static constexpr int kWidth = 80;

  signals:
    void ScrollRequested(int line);

  protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

  private:
    CodeEditor *editor_;
    QPixmap cache_;
    bool cache_dirty_{true};
};

} // namespace polyglot::tools::ui
