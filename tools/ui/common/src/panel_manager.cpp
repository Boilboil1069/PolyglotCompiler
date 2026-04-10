/**
 * @file     panel_manager.cpp
 * @brief    PanelManager implementation
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include "tools/ui/common/include/panel_manager.h"

#include <algorithm>

namespace polyglot::tools::ui {

// ============================================================================
// Construction / Destruction
// ============================================================================

PanelManager::PanelManager(QTabWidget *tab_widget, QObject *parent)
    : QObject(parent), tab_widget_(tab_widget) {
    // Track tab changes to emit ActivePanelChanged
    if (tab_widget_) {
        connect(tab_widget_, &QTabWidget::currentChanged,
                this, [this](int index) {
            if (index < 0) return;
            QWidget *w = tab_widget_->widget(index);
            for (const auto &entry : panels_) {
                if (entry.widget == w) {
                    emit ActivePanelChanged(entry.id);
                    break;
                }
            }
        });
    }
}

PanelManager::~PanelManager() = default;

// ============================================================================
// Panel Registration
// ============================================================================

void PanelManager::RegisterPanel(const QString &id,
                                 QWidget *widget,
                                 const QString &title) {
    PanelEntry entry;
    entry.id = id;
    entry.widget = widget;
    entry.title = title;
    panels_.push_back(entry);

    if (tab_widget_ && widget) {
        tab_widget_->addTab(widget, title);
    }
}

// ============================================================================
// Panel Visibility
// ============================================================================

void PanelManager::ShowPanel(const QString &id) {
    if (!tab_widget_) return;
    auto *entry = FindPanel(id);
    if (!entry || !entry->widget) return;

    tab_widget_->setVisible(true);
    tab_widget_->setCurrentWidget(entry->widget);
}

void PanelManager::TogglePanel(const QString &id) {
    if (!tab_widget_) return;
    auto *entry = FindPanel(id);
    if (!entry || !entry->widget) return;

    bool visible = tab_widget_->isVisible();
    if (visible && tab_widget_->currentWidget() == entry->widget) {
        // Already active — hide the panel area
        tab_widget_->setVisible(false);
    } else {
        tab_widget_->setVisible(true);
        tab_widget_->setCurrentWidget(entry->widget);
    }
}

void PanelManager::HideAll() {
    if (tab_widget_) {
        tab_widget_->setVisible(false);
    }
}

bool PanelManager::IsPanelActive(const QString &id) const {
    if (!tab_widget_ || !tab_widget_->isVisible()) return false;
    auto *entry = FindPanel(id);
    if (!entry || !entry->widget) return false;
    return tab_widget_->currentWidget() == entry->widget;
}

bool PanelManager::IsVisible() const {
    return tab_widget_ && tab_widget_->isVisible();
}

QWidget *PanelManager::GetPanel(const QString &id) const {
    auto *entry = FindPanel(id);
    return entry ? entry->widget : nullptr;
}

// ============================================================================
// Plugin Panel Contributions
// ============================================================================

void PanelManager::RegisterPluginPanel(const QString &plugin_id,
                                       const QString &panel_id,
                                       QWidget *widget,
                                       const QString &title) {
    PanelEntry entry;
    entry.id = panel_id;
    entry.widget = widget;
    entry.title = title;
    entry.plugin_id = plugin_id;
    panels_.push_back(entry);

    if (tab_widget_ && widget) {
        tab_widget_->addTab(widget, title);
    }
}

void PanelManager::RemovePluginPanels(const QString &plugin_id) {
    if (!tab_widget_) return;

    auto it = std::remove_if(panels_.begin(), panels_.end(),
        [&](const PanelEntry &entry) {
            if (entry.plugin_id == plugin_id) {
                int idx = tab_widget_->indexOf(entry.widget);
                if (idx >= 0) {
                    tab_widget_->removeTab(idx);
                }
                if (entry.widget) {
                    entry.widget->deleteLater();
                }
                return true;
            }
            return false;
        });
    panels_.erase(it, panels_.end());
}

QStringList PanelManager::PanelIds() const {
    QStringList ids;
    for (const auto &entry : panels_) {
        ids.append(entry.id);
    }
    return ids;
}

// ============================================================================
// Internal Helpers
// ============================================================================

PanelManager::PanelEntry *PanelManager::FindPanel(const QString &id) {
    for (auto &entry : panels_) {
        if (entry.id == id) return &entry;
    }
    return nullptr;
}

const PanelManager::PanelEntry *PanelManager::FindPanel(const QString &id) const {
    for (const auto &entry : panels_) {
        if (entry.id == id) return &entry;
    }
    return nullptr;
}

} // namespace polyglot::tools::ui
