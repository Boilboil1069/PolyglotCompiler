/**
 * @file     theme_manager.h
 * @brief    Centralized theme management for the PolyglotCompiler IDE
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <QColor>
#include <QMap>
#include <QString>

namespace polyglot::tools::ui {

// ============================================================================
// ThemeColors — the complete set of colors that define a theme
// ============================================================================

/** @brief ThemeColors data structure. */
struct ThemeColors {
    // Base surfaces
    QColor background;            // main editor / panel background
    QColor surface;               // slightly raised surface (toolbars, tabs)
    QColor surface_alt;           // alternating row, secondary surface
    QColor border;                // borders & separators

    // Text
    QColor text;                  // primary text color
    QColor text_secondary;        // muted / secondary text
    QColor text_disabled;         // disabled UI elements

    // Accent
    QColor accent;                // primary accent (buttons, active indicators)
    QColor accent_hover;          // hover state on accent
    QColor accent_pressed;        // pressed state on accent

    // Selection
    QColor selection;             // selected item background
    QColor selection_text;        // selected item text

    // Status
    QColor error;                 // error indicators
    QColor warning;               // warning indicators
    QColor success;               // success indicators
    QColor info;                  // informational indicators

    // Editor-specific
    QColor editor_background;     // code editor background
    QColor editor_text;           // code editor foreground
    QColor editor_selection;      // code selection highlight
    QColor editor_current_line;   // current line highlight
    QColor editor_line_number;    // line number gutter text
    QColor editor_line_number_bg; // line number gutter background

    // Tab bar
    QColor tab_background;        // inactive tab
    QColor tab_active;            // active tab background
    QColor tab_active_indicator;  // active tab top/bottom border
    QColor tab_hover;             // tab hover background
    QColor tab_text;              // inactive tab text
    QColor tab_active_text;       // active tab text

    // Status bar
    QColor statusbar_background;
    QColor statusbar_text;

    // Menu
    QColor menu_background;
    QColor menu_text;
    QColor menu_hover;
    QColor menu_separator;

    // Buttons
    QColor button_background;     // secondary button
    QColor button_text;
    QColor button_hover;
    QColor button_primary;        // primary action button
    QColor button_primary_text;
    QColor button_primary_hover;

    // Input fields
    QColor input_background;
    QColor input_text;
    QColor input_border;
    QColor input_placeholder;

    // Scrollbar
    QColor scrollbar_bg;
    QColor scrollbar_thumb;
    QColor scrollbar_thumb_hover;

    // Progress bar
    QColor progress_background;
    QColor progress_chunk;
};

// ============================================================================
// ThemeManager — singleton that manages the current theme
// ============================================================================

/** @brief ThemeManager class. */
class ThemeManager {
  public:
    // Get the global instance.
    static ThemeManager &Instance();

    // Retrieve a named theme.  Returns Dark if not found.
    const ThemeColors &GetTheme(const QString &name) const;

    // Set the active theme by name.
    void SetActiveTheme(const QString &name);

    // Return the active theme colors.
    const ThemeColors &Active() const;

    // Return the active theme name.
    const QString &ActiveName() const { return active_name_; }

    // Return all registered theme names.
    QStringList AvailableThemes() const;

    // ── Stylesheet generation helpers ────────────────────────────────────
    QString MenuBarStylesheet() const;
    QString ToolBarStylesheet() const;
    QString StatusBarStylesheet() const;
    QString TabWidgetStylesheet(bool bottom_tabs = false) const;
    QString TreeWidgetStylesheet() const;
    QString ComboBoxStylesheet() const;
    QString LineEditStylesheet() const;
    QString PushButtonStylesheet() const;
    QString PushButtonPrimaryStylesheet() const;
    QString PlainTextEditStylesheet() const;
    QString EditorStylesheet() const;
    QString GroupBoxStylesheet() const;
    QString CheckBoxStylesheet() const;
    QString SpinBoxStylesheet() const;
    QString LabelStylesheet() const;
    QString ProgressBarStylesheet() const;
    QString ScrollAreaStylesheet() const;
    QString DialogStylesheet() const;
    QString ListWidgetStylesheet() const;
    QString SplitterStylesheet() const;

  private:
    ThemeManager();
    void RegisterBuiltinThemes();

    QMap<QString, ThemeColors> themes_;
    QString active_name_{"Dark"};
};

} // namespace polyglot::tools::ui
