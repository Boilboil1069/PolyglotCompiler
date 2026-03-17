// panel_manager.h — Manages bottom panel tabs and plugin panel contributions.
//
// Extracted from MainWindow to decouple panel lifecycle, visibility toggling,
// and plugin-contributed panels from the top-level window.

#pragma once

#include <QObject>
#include <QTabWidget>
#include <QWidget>

#include <functional>
#include <string>
#include <vector>

namespace polyglot::tools::ui {

// ============================================================================
// PanelManager — manages the bottom panel area
// ============================================================================

class PanelManager : public QObject {
    Q_OBJECT

  public:
    explicit PanelManager(QTabWidget *tab_widget, QObject *parent = nullptr);
    ~PanelManager() override;

    // Register a built-in panel with a display name.
    void RegisterPanel(const QString &id, QWidget *widget, const QString &title);

    // Show a specific panel (makes bottom tabs visible and switches to it).
    void ShowPanel(const QString &id);

    // Toggle a panel's visibility.  If the panel is currently active and
    // visible, hides the entire bottom area.  Otherwise shows and switches
    // to the panel.
    void TogglePanel(const QString &id);

    // Hide the entire bottom panel area.
    void HideAll();

    // Check if a specific panel is currently the active tab and visible.
    bool IsPanelActive(const QString &id) const;

    // Check if the bottom panel area is visible.
    bool IsVisible() const;

    // Return the widget for a panel id (nullptr if not found).
    QWidget *GetPanel(const QString &id) const;

    // ── Plugin panel contributions ───────────────────────────────────────

    // Register a panel contributed by a plugin.
    void RegisterPluginPanel(const QString &plugin_id,
                             const QString &panel_id,
                             QWidget *widget,
                             const QString &title);

    // Remove all panels contributed by a plugin.
    void RemovePluginPanels(const QString &plugin_id);

    // Return all registered panel ids.
    QStringList PanelIds() const;

  signals:
    // Emitted when the active panel changes.
    void ActivePanelChanged(const QString &panel_id);

  private:
    struct PanelEntry {
        QString  id;
        QWidget *widget{nullptr};
        QString  title;
        QString  plugin_id;   // Empty for built-in panels
    };

    QTabWidget *tab_widget_{nullptr};
    std::vector<PanelEntry> panels_;

    // Find a panel entry by id.  Returns nullptr if not found.
    PanelEntry *FindPanel(const QString &id);
    const PanelEntry *FindPanel(const QString &id) const;
};

} // namespace polyglot::tools::ui
