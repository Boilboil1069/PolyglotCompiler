// action_manager.h — Centralized action and keybinding management.
//
// Owns all QAction instances, their default shortcuts, and supports
// runtime keybinding customization.  Extracted from MainWindow to
// decouple action lifecycle from the top-level window.

#pragma once

#include <QAction>
#include <QKeySequence>
#include <QMap>
#include <QObject>
#include <QString>

#include <functional>
#include <vector>

namespace polyglot::tools::ui {

// ============================================================================
// ActionManager — owns and indexes all IDE actions
// ============================================================================

class ActionManager : public QObject {
    Q_OBJECT

  public:
    explicit ActionManager(QObject *parent = nullptr);
    ~ActionManager() override;

    // Register a new action with a string id, display label, and default shortcut.
    QAction *RegisterAction(const QString &id,
                            const QString &label,
                            const QKeySequence &default_shortcut = {});

    // Retrieve an action by its string id (nullptr if not found).
    QAction *GetAction(const QString &id) const;

    // Return all registered action ids.
    QStringList ActionIds() const;

    // ── Keybinding management ────────────────────────────────────────────

    // Apply a custom shortcut to an action.  Overrides the default.
    void SetCustomShortcut(const QString &action_id,
                           const QKeySequence &shortcut);

    // Reset an action's shortcut to the default.
    void ResetShortcut(const QString &action_id);

    // Reset all shortcuts to their defaults.
    void ResetAllShortcuts();

    // Load custom keybindings from QSettings.
    void LoadKeybindings();

    // Save custom keybindings to QSettings.
    void SaveKeybindings() const;

    // Return the current shortcut for an action.
    QKeySequence GetShortcut(const QString &action_id) const;

    // Return the default shortcut for an action.
    QKeySequence GetDefaultShortcut(const QString &action_id) const;

    // ── Plugin-contributed actions ───────────────────────────────────────

    // Register a dynamic action contributed by a plugin.
    QAction *RegisterPluginAction(const QString &plugin_id,
                                  const QString &action_id,
                                  const QString &label,
                                  const QKeySequence &shortcut,
                                  std::function<void()> callback);

    // Remove all actions contributed by a plugin.
    void RemovePluginActions(const QString &plugin_id);

  signals:
    // Emitted when a plugin action is added, so the UI can update menus.
    void PluginActionRegistered(const QString &action_id, QAction *action);
    void PluginActionRemoved(const QString &action_id);

  private:
    struct ActionEntry {
        QAction      *action{nullptr};
        QKeySequence  default_shortcut;
        QString       plugin_id;   // Empty for built-in actions
    };

    QMap<QString, ActionEntry> actions_;
};

} // namespace polyglot::tools::ui
