/**
 * @file     theme_manager.cpp
 * @brief    Centralized theme management implementation
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include "tools/ui/common/include/theme_manager.h"

namespace polyglot::tools::ui {

// ============================================================================
// Singleton
// ============================================================================

ThemeManager &ThemeManager::Instance() {
    static ThemeManager instance;
    return instance;
}

ThemeManager::ThemeManager() {
    RegisterBuiltinThemes();
}

// ============================================================================
// Theme Access
// ============================================================================

const ThemeColors &ThemeManager::GetTheme(const QString &name) const {
    auto it = themes_.find(name);
    if (it != themes_.end()) return it.value();
    // Fallback when the requested theme is not found
    it = themes_.find(QStringLiteral("Dark"));
    if (it != themes_.end()) return it.value();
    // Final safety net — return a static default
    static const ThemeColors kDefault{};
    return kDefault;
}

void ThemeManager::SetActiveTheme(const QString &name) {
    if (themes_.contains(name)) {
        active_name_ = name;
    }
}

const ThemeColors &ThemeManager::Active() const {
    return GetTheme(active_name_);
}

QStringList ThemeManager::AvailableThemes() const {
    return themes_.keys();
}

// ============================================================================
// Built-in Themes
// ============================================================================

void ThemeManager::RegisterBuiltinThemes() {
    // ── Dark (default, VS Code-like) ─────────────────────────────────────
    {
        ThemeColors t;
        t.background          = QColor("#1e1e1e");
        t.surface             = QColor("#2d2d2d");
        t.surface_alt         = QColor("#252526");
        t.border              = QColor("#454545");

        t.text                = QColor("#cccccc");
        t.text_secondary      = QColor("#969696");
        t.text_disabled       = QColor("#666666");

        t.accent              = QColor("#0e639c");
        t.accent_hover        = QColor("#1177bb");
        t.accent_pressed      = QColor("#094771");

        t.selection           = QColor("#094771");
        t.selection_text      = QColor("#ffffff");

        t.error               = QColor("#f44747");
        t.warning             = QColor("#cca700");
        t.success             = QColor("#89d185");
        t.info                = QColor("#3794ff");

        t.editor_background   = QColor("#1e1e1e");
        t.editor_text         = QColor("#d4d4d4");
        t.editor_selection    = QColor("#264f78");
        t.editor_current_line = QColor("#2a2d2e");
        t.editor_line_number  = QColor("#858585");
        t.editor_line_number_bg = QColor("#1e1e1e");

        t.tab_background      = QColor("#2d2d2d");
        t.tab_active          = QColor("#1e1e1e");
        t.tab_active_indicator = QColor("#007acc");
        t.tab_hover           = QColor("#383838");
        t.tab_text            = QColor("#969696");
        t.tab_active_text     = QColor("#ffffff");

        t.statusbar_background = QColor("#007acc");
        t.statusbar_text      = QColor("#ffffff");

        t.menu_background     = QColor("#252526");
        t.menu_text           = QColor("#cccccc");
        t.menu_hover          = QColor("#094771");
        t.menu_separator      = QColor("#454545");

        t.button_background   = QColor("#3c3c3c");
        t.button_text         = QColor("#cccccc");
        t.button_hover        = QColor("#505050");
        t.button_primary      = QColor("#0e639c");
        t.button_primary_text = QColor("#ffffff");
        t.button_primary_hover = QColor("#1177bb");

        t.input_background    = QColor("#3c3c3c");
        t.input_text          = QColor("#cccccc");
        t.input_border        = QColor("#555555");
        t.input_placeholder   = QColor("#888888");

        t.scrollbar_bg        = QColor("#1e1e1e");
        t.scrollbar_thumb     = QColor("#424242");
        t.scrollbar_thumb_hover = QColor("#4f4f4f");

        t.progress_background = QColor("#1e1e1e");
        t.progress_chunk      = QColor("#0e639c");

        themes_["Dark"] = t;
    }

    // ── Light ────────────────────────────────────────────────────────────
    {
        ThemeColors t;
        t.background          = QColor("#ffffff");
        t.surface             = QColor("#f3f3f3");
        t.surface_alt         = QColor("#e8e8e8");
        t.border              = QColor("#c8c8c8");

        t.text                = QColor("#333333");
        t.text_secondary      = QColor("#616161");
        t.text_disabled       = QColor("#a0a0a0");

        t.accent              = QColor("#0078d7");
        t.accent_hover        = QColor("#1a8cff");
        t.accent_pressed      = QColor("#005ba1");

        t.selection           = QColor("#cce5ff");
        t.selection_text      = QColor("#000000");

        t.error               = QColor("#e51400");
        t.warning             = QColor("#e6a200");
        t.success             = QColor("#16825d");
        t.info                = QColor("#0078d7");

        t.editor_background   = QColor("#ffffff");
        t.editor_text         = QColor("#1e1e1e");
        t.editor_selection    = QColor("#add6ff");
        t.editor_current_line = QColor("#f0f0f0");
        t.editor_line_number  = QColor("#999999");
        t.editor_line_number_bg = QColor("#ffffff");

        t.tab_background      = QColor("#ececec");
        t.tab_active          = QColor("#ffffff");
        t.tab_active_indicator = QColor("#0078d7");
        t.tab_hover           = QColor("#e0e0e0");
        t.tab_text            = QColor("#616161");
        t.tab_active_text     = QColor("#333333");

        t.statusbar_background = QColor("#0078d7");
        t.statusbar_text      = QColor("#ffffff");

        t.menu_background     = QColor("#f3f3f3");
        t.menu_text           = QColor("#333333");
        t.menu_hover          = QColor("#cce5ff");
        t.menu_separator      = QColor("#d4d4d4");

        t.button_background   = QColor("#e0e0e0");
        t.button_text         = QColor("#333333");
        t.button_hover        = QColor("#d0d0d0");
        t.button_primary      = QColor("#0078d7");
        t.button_primary_text = QColor("#ffffff");
        t.button_primary_hover = QColor("#1a8cff");

        t.input_background    = QColor("#ffffff");
        t.input_text          = QColor("#333333");
        t.input_border        = QColor("#c8c8c8");
        t.input_placeholder   = QColor("#999999");

        t.scrollbar_bg        = QColor("#f3f3f3");
        t.scrollbar_thumb     = QColor("#c1c1c1");
        t.scrollbar_thumb_hover = QColor("#a8a8a8");

        t.progress_background = QColor("#e8e8e8");
        t.progress_chunk      = QColor("#0078d7");

        themes_["Light"] = t;
    }

    // ── Monokai ──────────────────────────────────────────────────────────
    {
        ThemeColors t;
        t.background          = QColor("#272822");
        t.surface             = QColor("#3e3d32");
        t.surface_alt         = QColor("#2e2e25");
        t.border              = QColor("#55544c");

        t.text                = QColor("#f8f8f2");
        t.text_secondary      = QColor("#75715e");
        t.text_disabled       = QColor("#555555");

        t.accent              = QColor("#a6e22e");
        t.accent_hover        = QColor("#b8e853");
        t.accent_pressed      = QColor("#8bc021");

        t.selection           = QColor("#49483e");
        t.selection_text      = QColor("#f8f8f2");

        t.error               = QColor("#f92672");
        t.warning             = QColor("#e6db74");
        t.success             = QColor("#a6e22e");
        t.info                = QColor("#66d9ef");

        t.editor_background   = QColor("#272822");
        t.editor_text         = QColor("#f8f8f2");
        t.editor_selection    = QColor("#49483e");
        t.editor_current_line = QColor("#3e3d32");
        t.editor_line_number  = QColor("#75715e");
        t.editor_line_number_bg = QColor("#272822");

        t.tab_background      = QColor("#3e3d32");
        t.tab_active          = QColor("#272822");
        t.tab_active_indicator = QColor("#a6e22e");
        t.tab_hover           = QColor("#49483e");
        t.tab_text            = QColor("#75715e");
        t.tab_active_text     = QColor("#f8f8f2");

        t.statusbar_background = QColor("#a6e22e");
        t.statusbar_text      = QColor("#272822");

        t.menu_background     = QColor("#2e2e25");
        t.menu_text           = QColor("#f8f8f2");
        t.menu_hover          = QColor("#49483e");
        t.menu_separator      = QColor("#55544c");

        t.button_background   = QColor("#3e3d32");
        t.button_text         = QColor("#f8f8f2");
        t.button_hover        = QColor("#49483e");
        t.button_primary      = QColor("#a6e22e");
        t.button_primary_text = QColor("#272822");
        t.button_primary_hover = QColor("#b8e853");

        t.input_background    = QColor("#3e3d32");
        t.input_text          = QColor("#f8f8f2");
        t.input_border        = QColor("#55544c");
        t.input_placeholder   = QColor("#75715e");

        t.scrollbar_bg        = QColor("#272822");
        t.scrollbar_thumb     = QColor("#49483e");
        t.scrollbar_thumb_hover = QColor("#55544c");

        t.progress_background = QColor("#272822");
        t.progress_chunk      = QColor("#a6e22e");

        themes_["Monokai"] = t;
    }

    // ── Solarized Dark ───────────────────────────────────────────────────
    {
        ThemeColors t;
        t.background          = QColor("#002b36");
        t.surface             = QColor("#073642");
        t.surface_alt         = QColor("#003847");
        t.border              = QColor("#586e75");

        t.text                = QColor("#839496");
        t.text_secondary      = QColor("#657b83");
        t.text_disabled       = QColor("#586e75");

        t.accent              = QColor("#268bd2");
        t.accent_hover        = QColor("#2aa198");
        t.accent_pressed      = QColor("#2075b0");

        t.selection           = QColor("#073642");
        t.selection_text      = QColor("#93a1a1");

        t.error               = QColor("#dc322f");
        t.warning             = QColor("#b58900");
        t.success             = QColor("#859900");
        t.info                = QColor("#268bd2");

        t.editor_background   = QColor("#002b36");
        t.editor_text         = QColor("#839496");
        t.editor_selection    = QColor("#073642");
        t.editor_current_line = QColor("#073642");
        t.editor_line_number  = QColor("#586e75");
        t.editor_line_number_bg = QColor("#002b36");

        t.tab_background      = QColor("#073642");
        t.tab_active          = QColor("#002b36");
        t.tab_active_indicator = QColor("#268bd2");
        t.tab_hover           = QColor("#003847");
        t.tab_text            = QColor("#657b83");
        t.tab_active_text     = QColor("#93a1a1");

        t.statusbar_background = QColor("#268bd2");
        t.statusbar_text      = QColor("#fdf6e3");

        t.menu_background     = QColor("#073642");
        t.menu_text           = QColor("#839496");
        t.menu_hover          = QColor("#003847");
        t.menu_separator      = QColor("#586e75");

        t.button_background   = QColor("#073642");
        t.button_text         = QColor("#839496");
        t.button_hover        = QColor("#003847");
        t.button_primary      = QColor("#268bd2");
        t.button_primary_text = QColor("#fdf6e3");
        t.button_primary_hover = QColor("#2aa198");

        t.input_background    = QColor("#073642");
        t.input_text          = QColor("#839496");
        t.input_border        = QColor("#586e75");
        t.input_placeholder   = QColor("#657b83");

        t.scrollbar_bg        = QColor("#002b36");
        t.scrollbar_thumb     = QColor("#073642");
        t.scrollbar_thumb_hover = QColor("#586e75");

        t.progress_background = QColor("#002b36");
        t.progress_chunk      = QColor("#268bd2");

        themes_["Solarized Dark"] = t;
    }
}

// ============================================================================
// Stylesheet Generation Helpers
// ============================================================================

QString ThemeManager::MenuBarStylesheet() const {
    const auto &t = Active();
    return QString(
        "QMenuBar { background: %1; color: %2; padding: 2px; }"
        "QMenuBar::item { padding: 4px 10px; }"
        "QMenuBar::item:selected { background: %3; }"
        "QMenu { background: %4; color: %5; border: 1px solid %6; }"
        "QMenu::item { padding: 5px 30px 5px 20px; }"
        "QMenu::item:selected { background: %7; }"
        "QMenu::separator { height: 1px; background: %8; margin: 2px 10px; }")
        .arg(t.surface.name(), t.text.name(), t.button_hover.name(),
             t.menu_background.name(), t.menu_text.name(), t.border.name(),
             t.menu_hover.name(), t.menu_separator.name());
}

QString ThemeManager::ToolBarStylesheet() const {
    const auto &t = Active();
    return QString(
        "QToolBar { background: %1; border: none; spacing: 4px; padding: 2px; }"
        "QToolButton { background: transparent; color: %2; padding: 4px 8px; "
        "border-radius: 3px; font-size: 12px; }"
        "QToolButton:hover { background: %3; }"
        "QToolButton:pressed { background: %4; }"
        "QToolButton:disabled { color: %5; }")
        .arg(t.surface.name(), t.text.name(), t.button_hover.name(),
             t.accent_pressed.name(), t.text_disabled.name());
}

QString ThemeManager::StatusBarStylesheet() const {
    const auto &t = Active();
    return QString(
        "QStatusBar { background: %1; color: %2; font-size: 12px; }"
        "QStatusBar::item { border: none; }"
        "QLabel { color: %3; padding: 0px 8px; }")
        .arg(t.statusbar_background.name(), t.statusbar_text.name(),
             t.statusbar_text.name());
}

QString ThemeManager::TabWidgetStylesheet(bool bottom_tabs) const {
    const auto &t = Active();
    QString border_side = bottom_tabs ? "border-bottom" : "border-top";
    int font_sz = bottom_tabs ? 11 : 12;
    int min_width = bottom_tabs ? 80 : 100;
    return QString(
        "QTabWidget::pane { border: none; background: %1; }"
        "QTabBar::tab { background: %2; color: %3; padding: %4px 12px; "
        "border: none; min-width: %5px; font-size: %6px; }"
        "QTabBar::tab:selected { background: %7; color: %8; "
        "%9: 2px solid %10; }"
        "QTabBar::tab:hover { background: %11; }")
        .arg(t.background.name(), t.tab_background.name(), t.tab_text.name())
        .arg(bottom_tabs ? 4 : 6)
        .arg(min_width).arg(font_sz)
        .arg(t.tab_active.name(), t.tab_active_text.name(),
             border_side, t.tab_active_indicator.name(),
             t.tab_hover.name());
}

QString ThemeManager::TreeWidgetStylesheet() const {
    const auto &t = Active();
    return QString(
        "QTreeWidget { background: %1; color: %2; border: none; }"
        "QTreeWidget::item { padding: 2px; }"
        "QTreeWidget::item:selected { background: %3; }"
        "QHeaderView::section { background: %4; color: %5; border: none; "
        "padding: 4px; font-size: 11px; }")
        .arg(t.background.name(), t.text.name(), t.selection.name(),
             t.surface.name(), t.text.name());
}

QString ThemeManager::ComboBoxStylesheet() const {
    const auto &t = Active();
    return QString(
        "QComboBox { background: %1; color: %2; border: 1px solid %3; "
        "border-radius: 3px; padding: 2px 8px; min-width: 80px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background: %4; color: %5; "
        "selection-background-color: %6; }")
        .arg(t.input_background.name(), t.input_text.name(), t.input_border.name(),
             t.surface_alt.name(), t.text.name(), t.selection.name());
}

QString ThemeManager::LineEditStylesheet() const {
    const auto &t = Active();
    return QString(
        "QLineEdit { background: %1; color: %2; border: 1px solid %3; "
        "border-radius: 3px; padding: 4px 8px; }")
        .arg(t.input_background.name(), t.input_text.name(), t.input_border.name());
}

QString ThemeManager::PushButtonStylesheet() const {
    const auto &t = Active();
    return QString(
        "QPushButton { background: %1; color: %2; border: 1px solid %3; "
        "border-radius: 3px; padding: 6px 16px; }"
        "QPushButton:hover { background: %4; }"
        "QPushButton:pressed { background: %5; }")
        .arg(t.button_background.name(), t.button_text.name(), t.input_border.name(),
             t.button_hover.name(), t.accent_pressed.name());
}

QString ThemeManager::PushButtonPrimaryStylesheet() const {
    const auto &t = Active();
    return QString(
        "QPushButton { background: %1; color: %2; border: none; "
        "border-radius: 3px; padding: 6px 16px; font-weight: bold; }"
        "QPushButton:hover { background: %3; }"
        "QPushButton:pressed { background: %4; }")
        .arg(t.button_primary.name(), t.button_primary_text.name(),
             t.button_primary_hover.name(), t.accent_pressed.name());
}

QString ThemeManager::PlainTextEditStylesheet() const {
    const auto &t = Active();
    return QString(
        "QPlainTextEdit { background: %1; color: %2; border: none; "
        "font-family: Menlo, Consolas, 'Courier New', monospace; font-size: 11px; }")
        .arg(t.background.name(), t.text.name());
}

QString ThemeManager::EditorStylesheet() const {
    const auto &t = Active();
    return QString(
        "QPlainTextEdit { background: %1; color: %2; border: none; "
        "selection-background-color: %3; }")
        .arg(t.editor_background.name(), t.editor_text.name(),
             t.editor_selection.name());
}

QString ThemeManager::GroupBoxStylesheet() const {
    const auto &t = Active();
    return QString(
        "QGroupBox { color: %1; font-weight: bold; border: 1px solid %2; "
        "border-radius: 4px; margin-top: 8px; padding-top: 16px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; "
        "padding: 0 5px; }")
        .arg(t.text.name(), t.input_border.name());
}

QString ThemeManager::CheckBoxStylesheet() const {
    const auto &t = Active();
    return QString(
        "QCheckBox { color: %1; spacing: 6px; }"
        "QCheckBox::indicator { width: 16px; height: 16px; }")
        .arg(t.text.name());
}

QString ThemeManager::SpinBoxStylesheet() const {
    const auto &t = Active();
    return QString(
        "QSpinBox { background: %1; color: %2; border: 1px solid %3; "
        "border-radius: 3px; padding: 2px; }")
        .arg(t.input_background.name(), t.input_text.name(), t.input_border.name());
}

QString ThemeManager::LabelStylesheet() const {
    const auto &t = Active();
    return QString("QLabel { color: %1; }").arg(t.text.name());
}

QString ThemeManager::ProgressBarStylesheet() const {
    const auto &t = Active();
    return QString(
        "QProgressBar { background: %1; border: none; }"
        "QProgressBar::chunk { background: %2; }")
        .arg(t.progress_background.name(), t.progress_chunk.name());
}

QString ThemeManager::ScrollAreaStylesheet() const {
    return "QScrollArea { border: none; background: transparent; }";
}

QString ThemeManager::DialogStylesheet() const {
    const auto &t = Active();
    return QString("QDialog { background: %1; }").arg(t.surface.name());
}

QString ThemeManager::ListWidgetStylesheet() const {
    const auto &t = Active();
    return QString(
        "QListWidget { background: %1; color: %2; border: none; "
        "font-size: 13px; outline: none; }"
        "QListWidget::item { padding: 10px 15px; }"
        "QListWidget::item:selected { background: %3; color: %4; }"
        "QListWidget::item:hover { background: %5; }")
        .arg(t.surface_alt.name(), t.text.name(), t.selection.name(),
             t.selection_text.name(), t.tab_hover.name());
}

QString ThemeManager::SplitterStylesheet() const {
    const auto &t = Active();
    return QString("QSplitter::handle { background: %1; }").arg(t.border.name());
}

} // namespace polyglot::tools::ui
