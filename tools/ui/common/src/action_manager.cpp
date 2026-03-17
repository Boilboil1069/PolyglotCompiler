// action_manager.cpp — ActionManager implementation.
//
// Centralized action and keybinding management for the IDE.

#include "tools/ui/common/include/action_manager.h"

#include <QSettings>

namespace polyglot::tools::ui {

// ============================================================================
// Construction / Destruction
// ============================================================================

ActionManager::ActionManager(QObject *parent) : QObject(parent) {}

ActionManager::~ActionManager() = default;

// ============================================================================
// Action Registration
// ============================================================================

QAction *ActionManager::RegisterAction(const QString &id,
                                       const QString &label,
                                       const QKeySequence &default_shortcut) {
    if (actions_.contains(id)) {
        return actions_[id].action;
    }

    auto *action = new QAction(label, this);
    if (!default_shortcut.isEmpty()) {
        action->setShortcut(default_shortcut);
    }

    ActionEntry entry;
    entry.action = action;
    entry.default_shortcut = default_shortcut;
    actions_[id] = entry;

    return action;
}

QAction *ActionManager::GetAction(const QString &id) const {
    auto it = actions_.find(id);
    return it != actions_.end() ? it->action : nullptr;
}

QStringList ActionManager::ActionIds() const {
    return actions_.keys();
}

// ============================================================================
// Keybinding Management
// ============================================================================

void ActionManager::SetCustomShortcut(const QString &action_id,
                                      const QKeySequence &shortcut) {
    auto it = actions_.find(action_id);
    if (it != actions_.end() && it->action) {
        it->action->setShortcut(shortcut);
    }
}

void ActionManager::ResetShortcut(const QString &action_id) {
    auto it = actions_.find(action_id);
    if (it != actions_.end() && it->action) {
        it->action->setShortcut(it->default_shortcut);
    }
}

void ActionManager::ResetAllShortcuts() {
    for (auto it = actions_.begin(); it != actions_.end(); ++it) {
        if (it->action) {
            it->action->setShortcut(it->default_shortcut);
        }
    }
}

void ActionManager::LoadKeybindings() {
    QSettings settings("PolyglotCompiler", "IDE");
    int count = settings.beginReadArray("keybindings");
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        QString action_id = settings.value("action").toString();
        QString seq_str = settings.value("shortcut").toString();
        if (!action_id.isEmpty() && !seq_str.isEmpty()) {
            SetCustomShortcut(action_id, QKeySequence(seq_str));
        }
    }
    settings.endArray();
}

void ActionManager::SaveKeybindings() const {
    QSettings settings("PolyglotCompiler", "IDE");

    // Collect only actions whose shortcut differs from default
    QList<QPair<QString, QKeySequence>> customs;
    for (auto it = actions_.constBegin(); it != actions_.constEnd(); ++it) {
        if (!it->action) continue;
        if (it->action->shortcut() != it->default_shortcut) {
            customs.append({it.key(), it->action->shortcut()});
        }
    }

    settings.beginWriteArray("keybindings", customs.size());
    for (int i = 0; i < customs.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue("action", customs[i].first);
        settings.setValue("shortcut", customs[i].second.toString());
    }
    settings.endArray();
}

QKeySequence ActionManager::GetShortcut(const QString &action_id) const {
    auto it = actions_.find(action_id);
    if (it != actions_.end() && it->action) {
        return it->action->shortcut();
    }
    return {};
}

QKeySequence ActionManager::GetDefaultShortcut(const QString &action_id) const {
    auto it = actions_.find(action_id);
    return it != actions_.end() ? it->default_shortcut : QKeySequence{};
}

// ============================================================================
// Plugin-contributed Actions
// ============================================================================

QAction *ActionManager::RegisterPluginAction(const QString &plugin_id,
                                             const QString &action_id,
                                             const QString &label,
                                             const QKeySequence &shortcut,
                                             std::function<void()> callback) {
    // Remove existing action with same id if any
    if (actions_.contains(action_id)) {
        delete actions_[action_id].action;
        actions_.remove(action_id);
    }

    auto *action = new QAction(label, this);
    if (!shortcut.isEmpty()) {
        action->setShortcut(shortcut);
    }
    if (callback) {
        connect(action, &QAction::triggered, this, std::move(callback));
    }

    ActionEntry entry;
    entry.action = action;
    entry.default_shortcut = shortcut;
    entry.plugin_id = plugin_id;
    actions_[action_id] = entry;

    emit PluginActionRegistered(action_id, action);
    return action;
}

void ActionManager::RemovePluginActions(const QString &plugin_id) {
    QStringList to_remove;
    for (auto it = actions_.begin(); it != actions_.end(); ++it) {
        if (it->plugin_id == plugin_id) {
            to_remove.append(it.key());
        }
    }
    for (const auto &id : to_remove) {
        auto it = actions_.find(id);
        if (it != actions_.end()) {
            delete it->action;
            actions_.erase(it);
            emit PluginActionRemoved(id);
        }
    }
}

} // namespace polyglot::tools::ui
