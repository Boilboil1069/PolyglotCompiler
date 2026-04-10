/**
 * @file     topology_panel.cpp
 * @brief    Topology visualization panel implementation
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include "tools/ui/common/include/topology_panel.h"

#include <QApplication>
#include <QFile>
#include <QFileDialog>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsTextItem>
#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QProcess>
#include <QScrollBar>
#include <QTextStream>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

#include "frontends/common/include/diagnostics.h"
#include "frontends/ploy/include/ploy_lexer.h"
#include "frontends/ploy/include/ploy_parser.h"
#include "frontends/ploy/include/ploy_sema.h"
#include "tools/polyc/src/foreign_signature_extractor.h"
#include "tools/polytopo/include/topology_analyzer.h"
#include "tools/polytopo/include/topology_codegen.h"
#include "tools/polytopo/include/topology_graph.h"
#include "tools/polytopo/include/topology_printer.h"
#include "tools/polytopo/include/topology_validator.h"

namespace polyglot::tools::ui {

// ============================================================================
// Color scheme per language
// ============================================================================

static QColor LanguageColor(const QString &lang) {
    if (lang == "cpp")    return QColor("#4CAF50");
    if (lang == "python") return QColor("#2196F3");
    if (lang == "rust")   return QColor("#FF5722");
    if (lang == "java")   return QColor("#FF9800");
    if (lang == "dotnet") return QColor("#9C27B0");
    if (lang == "ploy")   return QColor("#00BCD4");
    return QColor("#607D8B");
}

static QColor StatusColor(const QString &status) {
    if (status == "valid")            return QColor("#4CAF50");
    if (status == "implicit_convert") return QColor("#FFC107");
    if (status == "explicit_convert") return QColor("#FF9800");
    if (status == "incompatible")     return QColor("#F44336");
    return QColor("#9E9E9E"); // unknown
}

// ============================================================================
// Node geometry constants
// ============================================================================

static constexpr qreal kNodeWidth = 220.0;
static constexpr qreal kNodeHeaderHeight = 30.0;
static constexpr qreal kPortHeight = 20.0;
static constexpr qreal kPortDotRadius = 5.0;
static constexpr qreal kPortMargin = 12.0;

// ============================================================================
// TopoPortItem — port dot with hover tooltip and drag-to-connect
// ============================================================================

TopoPortItem::TopoPortItem(uint64_t port_id, Direction direction,
                           const QString &name, const QString &type_name,
                           TopoNodeItem *parent_node)
    : QGraphicsEllipseItem(-kPortDotRadius, -kPortDotRadius,
                           kPortDotRadius * 2, kPortDotRadius * 2,
                           parent_node),
      port_id_(port_id), direction_(direction),
      name_(name), type_name_(type_name),
      parent_node_(parent_node) {
    setAcceptHoverEvents(true);
    setAcceptedMouseButtons(Qt::LeftButton);

    QColor dot_color = (direction == Direction::kInput)
                           ? QColor("#2196F3") : QColor("#4CAF50");
    setBrush(QBrush(dot_color));
    setPen(QPen(Qt::white, 1));
    setZValue(2);  // Above node body and edges
}

void TopoPortItem::hoverEnterEvent(QGraphicsSceneHoverEvent *event) {
    // Show tooltip with type, language, and connection status information
    QString tip = QString("<b>%1</b> %2<br>"
                          "<i>Type:</i> <code>%3</code><br>"
                          "<i>Direction:</i> %4<br>"
                          "<i>Node:</i> %5<br>"
                          "<i>Language:</i> %6")
                      .arg(direction_ == Direction::kInput ? "Input" : "Output",
                           name_, type_name_,
                           direction_ == Direction::kInput ? "→ In" : "Out →",
                           parent_node_->NodeName(),
                           parent_node_->Language());

    // Look up connection status from edges touching this port
    auto *gv = qobject_cast<TopoGraphicsView *>(scene()->views().value(0));
    if (gv && gv->Panel()) {
        // Access edge items to find connections to this port
        bool found_connection = false;
        const auto &edges = gv->scene()->items();
        for (auto *item : edges) {
            auto *edge = dynamic_cast<TopoEdgeItem *>(item);
            if (!edge) continue;
            bool matches = false;
            if (direction_ == Direction::kOutput &&
                edge->SourceNodeId() == parent_node_->NodeId() &&
                edge->SourcePortId() == port_id_) {
                matches = true;
            } else if (direction_ == Direction::kInput &&
                       edge->TargetNodeId() == parent_node_->NodeId() &&
                       edge->TargetPortId() == port_id_) {
                matches = true;
            }
            if (matches) {
                if (!found_connection) {
                    tip += "<br><i>Connections:</i>";
                    found_connection = true;
                }
                // Describe the edge status
                QString status_desc;
                const QString &s = edge->Status();
                if (s == "valid")
                    status_desc = "✔ Valid";
                else if (s == "implicit_convert")
                    status_desc = "⚠ Implicit Convert";
                else if (s == "explicit_convert")
                    status_desc = "⚠ Explicit Convert";
                else if (s == "incompatible")
                    status_desc = "✘ Incompatible";
                else
                    status_desc = "? Unknown";
                tip += "<br>&nbsp;&nbsp;" + status_desc;
            }
        }
        if (!found_connection) {
            tip += "<br><i>Connection:</i> Not connected";
        }
    }

    QToolTip::showText(event->screenPos(), tip);

    // Visual feedback: enlarge on hover
    setScale(1.5);
    QGraphicsEllipseItem::hoverEnterEvent(event);
}

void TopoPortItem::hoverLeaveEvent(QGraphicsSceneHoverEvent *event) {
    QToolTip::hideText();
    setScale(1.0);
    QGraphicsEllipseItem::hoverLeaveEvent(event);
}

void TopoPortItem::mousePressEvent(QGraphicsSceneMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        auto *gv = qobject_cast<TopoGraphicsView *>(scene()->views().value(0));
        if (gv) {
            gv->BeginPortDrag(this, event->scenePos());
            event->accept();
            return;
        }
    }
    QGraphicsEllipseItem::mousePressEvent(event);
}

void TopoPortItem::mouseMoveEvent(QGraphicsSceneMouseEvent *event) {
    auto *gv = qobject_cast<TopoGraphicsView *>(scene()->views().value(0));
    if (gv && gv->IsDraggingPort()) {
        gv->UpdatePortDrag(event->scenePos());
        event->accept();
        return;
    }
    QGraphicsEllipseItem::mouseMoveEvent(event);
}

void TopoPortItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event) {
    auto *gv = qobject_cast<TopoGraphicsView *>(scene()->views().value(0));
    if (gv && gv->IsDraggingPort()) {
        gv->EndPortDrag(event->scenePos());
        event->accept();
        return;
    }
    QGraphicsEllipseItem::mouseReleaseEvent(event);
}

// ============================================================================
// TopoNodeItem
// ============================================================================

TopoNodeItem::TopoNodeItem(uint64_t node_id, const QString &name,
                           const QString &language, const QString &kind,
                           QGraphicsItem *parent)
    : QGraphicsRectItem(parent),
      node_id_(node_id), name_(name), language_(language), kind_(kind) {
    setFlag(QGraphicsItem::ItemIsMovable);
    setFlag(QGraphicsItem::ItemIsSelectable);
    setFlag(QGraphicsItem::ItemSendsGeometryChanges);
    setAcceptHoverEvents(true);

    // Initial size (will be adjusted when ports are added)
    setRect(0, 0, kNodeWidth, kNodeHeaderHeight);
}

QPointF TopoNodeItem::AddInputPort(uint64_t port_id, const QString &name,
                                    const QString &type_name) {
    auto *port = new TopoPortItem(port_id, TopoPortItem::Direction::kInput,
                                  name, type_name, this);
    input_ports_.push_back(port);

    // Label as child text item
    PortLabel pl;
    pl.label = new QGraphicsTextItem(name + ": " + type_name, this);
    pl.label->setDefaultTextColor(Qt::white);
    auto font = pl.label->font();
    font.setPointSize(8);
    pl.label->setFont(font);
    input_labels_.push_back(pl);

    LayoutPorts();
    return port->scenePos();
}

QPointF TopoNodeItem::AddOutputPort(uint64_t port_id, const QString &name,
                                     const QString &type_name) {
    auto *port = new TopoPortItem(port_id, TopoPortItem::Direction::kOutput,
                                  name, type_name, this);
    output_ports_.push_back(port);

    PortLabel pl;
    pl.label = new QGraphicsTextItem(name + ": " + type_name, this);
    pl.label->setDefaultTextColor(Qt::white);
    auto font = pl.label->font();
    font.setPointSize(8);
    pl.label->setFont(font);
    output_labels_.push_back(pl);

    LayoutPorts();
    return port->scenePos();
}

QPointF TopoNodeItem::InputPortPos(uint64_t port_id) const {
    for (const auto *port : input_ports_) {
        if (port->PortId() == port_id) {
            return mapToScene(port->pos());
        }
    }
    return scenePos();
}

QPointF TopoNodeItem::OutputPortPos(uint64_t port_id) const {
    for (const auto *port : output_ports_) {
        if (port->PortId() == port_id) {
            return mapToScene(port->pos());
        }
    }
    return scenePos() + QPointF(kNodeWidth, 0);
}

TopoPortItem *TopoNodeItem::InputPort(uint64_t port_id) const {
    for (auto *port : input_ports_) {
        if (port->PortId() == port_id) return port;
    }
    return nullptr;
}

TopoPortItem *TopoNodeItem::OutputPort(uint64_t port_id) const {
    for (auto *port : output_ports_) {
        if (port->PortId() == port_id) return port;
    }
    return nullptr;
}

void TopoNodeItem::SetHighlight(bool error) {
    highlight_error_ = error;
    update();
}

void TopoNodeItem::SetDebugHighlight(bool active) {
    debug_active_ = active;
    if (active) {
        StartPulseAnimation();
    } else {
        StopPulseAnimation();
    }
    update();
}

void TopoNodeItem::StartPulseAnimation() {
    if (!pulse_timer_) {
        pulse_timer_ = new QTimer();
        QObject::connect(pulse_timer_, &QTimer::timeout, [this]() {
            // Oscillate pulse_opacity_ between 0.4 and 1.0
            constexpr qreal kStep = 0.06;
            if (pulse_rising_) {
                pulse_opacity_ += kStep;
                if (pulse_opacity_ >= 1.0) {
                    pulse_opacity_ = 1.0;
                    pulse_rising_ = false;
                }
            } else {
                pulse_opacity_ -= kStep;
                if (pulse_opacity_ <= 0.4) {
                    pulse_opacity_ = 0.4;
                    pulse_rising_ = true;
                }
            }
            update();
        });
    }
    pulse_opacity_ = 1.0;
    pulse_rising_ = false;
    pulse_timer_->start(50);  // ~20 fps pulse
}

void TopoNodeItem::StopPulseAnimation() {
    if (pulse_timer_) {
        pulse_timer_->stop();
        delete pulse_timer_;
        pulse_timer_ = nullptr;
    }
    pulse_opacity_ = 1.0;
}

void TopoNodeItem::SetSourceLocation(const QString &file, int line) {
    source_file_ = file;
    source_line_ = line;
}

QVariant TopoNodeItem::itemChange(GraphicsItemChange change,
                                  const QVariant &value) {
    if (change == ItemPositionHasChanged) {
        // Notify the panel so that edges connected to this node are
        // re-routed to the new port positions in real time.
        if (scene()) {
            for (auto *v : scene()->views()) {
                auto *tgv = qobject_cast<TopoGraphicsView *>(v);
                if (tgv && tgv->Panel()) {
                    tgv->Panel()->RefreshEdgePositions();
                    break;
                }
            }
        }
    }
    return QGraphicsRectItem::itemChange(change, value);
}

void TopoNodeItem::LayoutPorts() {
    qreal total_height = kNodeHeaderHeight +
                         static_cast<qreal>(std::max(input_ports_.size(),
                                                      output_ports_.size())) * kPortHeight +
                         kPortMargin;
    setRect(0, 0, kNodeWidth, total_height);

    // Position input ports on the left
    for (size_t i = 0; i < input_ports_.size(); ++i) {
        qreal y = kNodeHeaderHeight + static_cast<qreal>(i) * kPortHeight + kPortHeight / 2;
        input_ports_[i]->setPos(0, y);
        if (i < input_labels_.size() && input_labels_[i].label) {
            input_labels_[i].label->setPos(kPortDotRadius + 4, y - 10);
        }
    }

    // Position output ports on the right
    for (size_t i = 0; i < output_ports_.size(); ++i) {
        qreal y = kNodeHeaderHeight + static_cast<qreal>(i) * kPortHeight + kPortHeight / 2;
        output_ports_[i]->setPos(kNodeWidth, y);
        if (i < output_labels_.size() && output_labels_[i].label) {
            qreal lw = output_labels_[i].label->boundingRect().width();
            output_labels_[i].label->setPos(kNodeWidth - lw - kPortDotRadius - 4, y - 10);
        }
    }
}

void TopoNodeItem::paint(QPainter *painter,
                          const QStyleOptionGraphicsItem *option,
                          QWidget *widget) {
    Q_UNUSED(option);
    Q_UNUSED(widget);

    QRectF r = rect();
    QColor base_color = LanguageColor(language_);

    // Background
    painter->setRenderHint(QPainter::Antialiasing);
    QColor bg = base_color.darker(300);
    bg.setAlpha(220);
    painter->setBrush(QBrush(bg));

    // Determine border style
    QPen border_pen(base_color, 2);
    if (debug_active_) {
        // Bright yellow glow with pulse animation for active-debug node
        QColor pulse_yellow(255, 235, 59, static_cast<int>(255 * pulse_opacity_));
        border_pen.setColor(pulse_yellow);
        border_pen.setWidth(4);
    } else if (highlight_error_) {
        border_pen.setColor(QColor("#F44336"));
        border_pen.setWidth(3);
    }
    if (isSelected()) {
        border_pen.setColor(Qt::white);
        border_pen.setWidth(3);
    }
    painter->setPen(border_pen);
    painter->drawRoundedRect(r, 6, 6);

    // Debug-active overlay: translucent yellow tint with pulse
    if (debug_active_) {
        QColor overlay(255, 235, 59, static_cast<int>(30 * pulse_opacity_));
        painter->setBrush(QBrush(overlay));
        painter->setPen(Qt::NoPen);
        painter->drawRoundedRect(r, 6, 6);
    }

    // Header background
    QRectF header(r.x(), r.y(), r.width(), kNodeHeaderHeight);
    painter->setBrush(QBrush(base_color));
    painter->setPen(Qt::NoPen);
    painter->drawRoundedRect(header, 6, 6);
    // Fill bottom corners of header
    painter->drawRect(header.adjusted(0, header.height() / 2, 0, 0));

    // Header text
    painter->setPen(Qt::white);
    QFont header_font;
    header_font.setPointSize(9);
    header_font.setBold(true);
    painter->setFont(header_font);
    QString header_text = "[" + kind_ + "] " + name_;
    painter->drawText(header.adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft,
                      header_text);

    // Language badge
    QFont badge_font;
    badge_font.setPointSize(7);
    painter->setFont(badge_font);
    QString badge = language_;
    QFontMetrics fm(badge_font);
    qreal badge_w = fm.horizontalAdvance(badge) + 8;
    QRectF badge_rect(r.width() - badge_w - 6, 5, badge_w, 18);
    painter->setBrush(QBrush(base_color.lighter(130)));
    painter->setPen(Qt::NoPen);
    painter->drawRoundedRect(badge_rect, 4, 4);
    painter->setPen(Qt::white);
    painter->drawText(badge_rect, Qt::AlignCenter, badge);

    // Debug indicator icon in header
    if (debug_active_) {
        painter->setBrush(QBrush(QColor("#FFEB3B")));
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(QPointF(r.width() - badge_w - 18, kNodeHeaderHeight / 2), 4, 4);
    }
}

void TopoNodeItem::contextMenuEvent(QGraphicsSceneContextMenuEvent *event) {
    // Find the TopologyPanel through the view hierarchy
    TopologyPanel *panel = nullptr;
    for (auto *v : scene()->views()) {
        auto *tgv = qobject_cast<TopoGraphicsView *>(v);
        if (tgv && tgv->Panel()) {
            panel = tgv->Panel();
            break;
        }
    }

    QMenu menu;

    // Go to Source — navigate to the source location for this node
    QAction *goto_source = menu.addAction("Go to Source");
    goto_source->setEnabled(!source_file_.isEmpty() && source_line_ > 0);

    // Show Details — select this node and show its details in the side panel
    QAction *show_details = menu.addAction("Show Details");

    // Highlight Connections — visually emphasise all edges connected to this node
    QAction *highlight_conn = menu.addAction("Highlight Connections");

    menu.addSeparator();

    // Delete Node — remove this node and all its connected edges
    QAction *delete_node = menu.addAction("Delete Node");
    delete_node->setEnabled(panel != nullptr);

    QAction *chosen = menu.exec(event->screenPos());
    if (!chosen) return;

    if (chosen == goto_source && panel) {
        emit panel->NodeDoubleClicked(source_file_, source_line_);
    } else if (chosen == show_details && panel) {
        // Select this node so the details panel updates
        scene()->clearSelection();
        setSelected(true);
    } else if (chosen == highlight_conn) {
        // Temporarily highlight all edges connected to this node
        for (auto *item : scene()->items()) {
            auto *edge = dynamic_cast<TopoEdgeItem *>(item);
            if (edge && (edge->SourceNodeId() == node_id_ ||
                         edge->TargetNodeId() == node_id_)) {
                // Brighten the edge pen colour
                QPen p = edge->pen();
                p.setColor(QColor("#FFC107"));
                p.setWidth(3);
                edge->setPen(p);
            }
        }
    } else if (chosen == delete_node && panel) {
        // Remove all edges connected to this node first
        std::vector<TopoEdgeItem *> connected;
        for (auto *item : scene()->items()) {
            auto *edge = dynamic_cast<TopoEdgeItem *>(item);
            if (edge && (edge->SourceNodeId() == node_id_ ||
                         edge->TargetNodeId() == node_id_)) {
                connected.push_back(edge);
            }
        }
        for (auto *edge : connected) {
            panel->RemoveEdge(edge);
        }

        // Remove the node from the scene
        scene()->removeItem(this);
        delete this;
    }
}

void TopoNodeItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) {
    Q_UNUSED(event);
    if (!source_file_.isEmpty() && source_line_ > 0) {
        for (auto *v : scene()->views()) {
            auto *tgv = qobject_cast<TopoGraphicsView *>(v);
            if (tgv && tgv->Panel()) {
                emit tgv->Panel()->NodeDoubleClicked(source_file_, source_line_);
                return;
            }
        }
    }
    QGraphicsRectItem::mouseDoubleClickEvent(event);
}

// ============================================================================
// TopoEdgeItem
// ============================================================================

TopoEdgeItem::TopoEdgeItem(uint64_t edge_id,
                           const QPointF &start, const QPointF &end,
                           const QString &status,
                           QGraphicsItem *parent)
    : QGraphicsPathItem(parent), edge_id_(edge_id), status_(status) {
    setAcceptHoverEvents(true);
    SetStatus(status);
    RebuildPath(start, end);
}

void TopoEdgeItem::SetStatus(const QString &status) {
    status_ = status;
    QColor color = StatusColor(status);
    QPen pen(color, 2);
    if (status == "unknown") {
        pen.setStyle(Qt::DashLine);
    } else if (status == "incompatible") {
        pen.setWidth(3);
    }
    setPen(pen);
}

void TopoEdgeItem::UpdateEndpoints(const QPointF &start, const QPointF &end) {
    RebuildPath(start, end);
}

void TopoEdgeItem::SetEndpointIds(uint64_t src_node, uint64_t src_port,
                                  uint64_t tgt_node, uint64_t tgt_port) {
    source_node_id_ = src_node;
    source_port_id_ = src_port;
    target_node_id_ = tgt_node;
    target_port_id_ = tgt_port;
}

void TopoEdgeItem::contextMenuEvent(QGraphicsSceneContextMenuEvent *event) {
    QMenu menu;
    QAction *remove_action = menu.addAction("Delete Edge");
    QAction *chosen = menu.exec(event->screenPos());
    if (chosen == remove_action) {
        // Find the panel through the TopoGraphicsView and request proper removal
        // which also syncs the .ploy file and updates the edge_items_ vector.
        for (auto *v : scene()->views()) {
            auto *tgv = qobject_cast<TopoGraphicsView *>(v);
            if (tgv && tgv->Panel()) {
                tgv->Panel()->RemoveEdge(this);
                return;
            }
        }
        // Fallback: direct removal (should not normally reach here)
        scene()->removeItem(this);
        delete this;
    }
}

void TopoEdgeItem::RebuildPath(const QPointF &start, const QPointF &end) {
    QPainterPath path;
    path.moveTo(start);

    // Cubic bezier for smooth connection
    qreal dx = std::abs(end.x() - start.x()) * 0.5;
    dx = std::max(dx, 40.0);  // Minimum curvature for close nodes
    QPointF c1(start.x() + dx, start.y());
    QPointF c2(end.x() - dx, end.y());
    path.cubicTo(c1, c2, end);

    setPath(path);
}

// ============================================================================
// TopoGraphicsView — custom view for drag-to-connect and wheel zoom
// ============================================================================

TopoGraphicsView::TopoGraphicsView(QGraphicsScene *scene, QWidget *parent)
    : QGraphicsView(scene, parent) {
    setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
}

void TopoGraphicsView::BeginPortDrag(TopoPortItem *source_port,
                                     const QPointF &scene_pos) {
    drag_source_port_ = source_port;
    drag_line_ = new QGraphicsLineItem();
    drag_line_->setPen(QPen(QColor("#FFC107"), 2, Qt::DashLine));
    drag_line_->setLine(QLineF(source_port->scenePos(), scene_pos));
    drag_line_->setZValue(100);
    scene()->addItem(drag_line_);
}

void TopoGraphicsView::UpdatePortDrag(const QPointF &scene_pos) {
    if (drag_line_ && drag_source_port_) {
        drag_line_->setLine(QLineF(drag_source_port_->scenePos(), scene_pos));
    }
}

void TopoGraphicsView::EndPortDrag(const QPointF &scene_pos) {
    if (!drag_source_port_) return;

    // Find the port item under the cursor
    auto items = scene()->items(scene_pos, Qt::IntersectsItemBoundingRect,
                                Qt::DescendingOrder);
    TopoPortItem *target_port = nullptr;
    for (auto *item : items) {
        target_port = dynamic_cast<TopoPortItem *>(item);
        if (target_port && target_port != drag_source_port_) break;
        target_port = nullptr;
    }

    if (target_port && panel_) {
        panel_->TryCreateEdge(drag_source_port_, target_port);
    }

    CancelPortDrag();
}

void TopoGraphicsView::CancelPortDrag() {
    if (drag_line_) {
        scene()->removeItem(drag_line_);
        delete drag_line_;
        drag_line_ = nullptr;
    }
    drag_source_port_ = nullptr;
}

void TopoGraphicsView::wheelEvent(QWheelEvent *event) {
    // Zoom with Ctrl+wheel, scroll without Ctrl
    if (event->modifiers() & Qt::ControlModifier) {
        double factor = (event->angleDelta().y() > 0) ? 1.15 : (1.0 / 1.15);
        scale(factor, factor);
        event->accept();
    } else {
        QGraphicsView::wheelEvent(event);
    }
}

// ============================================================================
// TopologyPanel
// ============================================================================

TopologyPanel::TopologyPanel(QWidget *parent)
    : QWidget(parent) {
    SetupUI();

    // File watcher for live reload
    file_watcher_ = new QFileSystemWatcher(this);
    connect(file_watcher_, &QFileSystemWatcher::fileChanged,
            this, &TopologyPanel::OnFileChanged);

    // Debounce timer for file reload (avoids rapid successive reloads)
    reload_debounce_ = new QTimer(this);
    reload_debounce_->setSingleShot(true);
    reload_debounce_->setInterval(200);  // 200 ms debounce
    connect(reload_debounce_, &QTimer::timeout, this, [this]() {
        if (!current_file_.isEmpty()) {
            BuildGraphFromFile(current_file_);
            status_label_->setText("Reloaded");
            diagnostics_output_->appendPlainText(
                "[LiveReload] Reloaded due to file change");
        }
    });

    // Force-directed layout timer
    force_timer_ = new QTimer(this);
    force_timer_->setInterval(16);  // ~60 fps
    connect(force_timer_, &QTimer::timeout,
            this, &TopologyPanel::OnForceLayoutTick);
}

TopologyPanel::~TopologyPanel() {
    StopForceLayout();

    // Sever the view → panel back-pointer so that item callbacks
    // (TopoNodeItem::itemChange → view→Panel()) do not route back to this
    // panel during scene teardown.
    if (view_) {
        view_->SetPanel(nullptr);
    }

    // Disconnect all signals from the scene BEFORE Qt's QWidget::~QWidget()
    // triggers QObjectPrivate::deleteChildren().  Without this, destroying
    // QGraphicsScene items can fire selectionChanged / itemChange signals
    // that route back to this panel after the TopologyPanel sub-object has
    // already been destroyed, causing an assertObjectType crash.
    if (scene_) {
        scene_->disconnect(this);
        scene_->clear();          // remove all items before scene dtor
    }
    if (file_watcher_) {
        file_watcher_->disconnect(this);
    }
    if (reload_debounce_) {
        reload_debounce_->stop();
    }

    // Clear tracking maps so no stale pointers are used.
    node_items_.clear();
    edge_items_.clear();
}

void TopologyPanel::SetupUI() {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Toolbar
    SetupToolbar();
    layout->addWidget(toolbar_);

    // Main splitter: scene + details
    splitter_ = new QSplitter(Qt::Horizontal, this);

    // Graphics view
    SetupScene();
    splitter_->addWidget(view_);

    // Right panel (details + diagnostics)
    auto *right_panel = new QWidget(this);
    auto *right_layout = new QVBoxLayout(right_panel);
    right_layout->setContentsMargins(4, 4, 4, 4);

    auto *details_label = new QLabel("Node Details", right_panel);
    details_label->setStyleSheet("font-weight: bold; padding: 4px;");
    right_layout->addWidget(details_label);

    details_tree_ = new QTreeWidget(right_panel);
    details_tree_->setHeaderLabels({"Property", "Value"});
    details_tree_->header()->setStretchLastSection(true);
    right_layout->addWidget(details_tree_);

    auto *diag_label = new QLabel("Diagnostics", right_panel);
    diag_label->setStyleSheet("font-weight: bold; padding: 4px;");
    right_layout->addWidget(diag_label);

    diagnostics_output_ = new QPlainTextEdit(right_panel);
    diagnostics_output_->setReadOnly(true);
    diagnostics_output_->setMaximumBlockCount(500);
    right_layout->addWidget(diagnostics_output_);

    splitter_->addWidget(right_panel);
    splitter_->setStretchFactor(0, 3);
    splitter_->setStretchFactor(1, 1);
    layout->addWidget(splitter_);

    // Status bar
    status_label_ = new QLabel("No topology loaded", this);
    status_label_->setStyleSheet("padding: 4px; color: #888;");
    layout->addWidget(status_label_);
}

void TopologyPanel::SetupToolbar() {
    toolbar_ = new QToolBar(this);
    toolbar_->setIconSize(QSize(16, 16));

    refresh_btn_ = new QPushButton("Refresh", toolbar_);
    connect(refresh_btn_, &QPushButton::clicked, this, &TopologyPanel::OnRefresh);
    toolbar_->addWidget(refresh_btn_);

    validate_btn_ = new QPushButton("Validate", toolbar_);
    connect(validate_btn_, &QPushButton::clicked, this, &TopologyPanel::OnValidate);
    toolbar_->addWidget(validate_btn_);

    toolbar_->addSeparator();

    auto *zoom_in = new QPushButton("+", toolbar_);
    connect(zoom_in, &QPushButton::clicked, this, &TopologyPanel::OnZoomIn);
    toolbar_->addWidget(zoom_in);

    auto *zoom_out = new QPushButton("-", toolbar_);
    connect(zoom_out, &QPushButton::clicked, this, &TopologyPanel::OnZoomOut);
    toolbar_->addWidget(zoom_out);

    auto *zoom_fit = new QPushButton("Fit", toolbar_);
    connect(zoom_fit, &QPushButton::clicked, this, &TopologyPanel::OnZoomFit);
    toolbar_->addWidget(zoom_fit);

    toolbar_->addSeparator();

    layout_combo_ = new QComboBox(toolbar_);
    layout_combo_->addItem("Force-Directed");
    layout_combo_->addItem("Top-Down Grid");
    layout_combo_->addItem("Left-Right Grid");
    connect(layout_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TopologyPanel::OnLayoutChanged);
    toolbar_->addWidget(layout_combo_);

    toolbar_->addSeparator();

    auto *export_dot = new QPushButton("Export DOT", toolbar_);
    connect(export_dot, &QPushButton::clicked, this, &TopologyPanel::OnExportDot);
    toolbar_->addWidget(export_dot);

    auto *export_json = new QPushButton("Export JSON", toolbar_);
    connect(export_json, &QPushButton::clicked, this, &TopologyPanel::OnExportJson);
    toolbar_->addWidget(export_json);

    auto *export_png = new QPushButton("Export PNG", toolbar_);
    connect(export_png, &QPushButton::clicked, this, &TopologyPanel::OnExportPng);
    toolbar_->addWidget(export_png);

    toolbar_->addSeparator();

    auto *gen_ploy = new QPushButton("Generate .ploy", toolbar_);
    connect(gen_ploy, &QPushButton::clicked, this, &TopologyPanel::OnGeneratePloy);
    toolbar_->addWidget(gen_ploy);
}

void TopologyPanel::SetupScene() {
    scene_ = new QGraphicsScene(this);
    scene_->setBackgroundBrush(QColor("#1E1E1E"));

    view_ = new TopoGraphicsView(scene_, this);
    view_->SetPanel(this);

    // Connect selection changes to details panel
    connect(scene_, &QGraphicsScene::selectionChanged,
            this, &TopologyPanel::OnNodeSelected);
}

void TopologyPanel::LoadFromFile(const QString &ploy_file_path) {
    // Stop watching old file
    if (!current_file_.isEmpty()) {
        file_watcher_->removePath(current_file_);
    }

    current_file_ = ploy_file_path;
    BuildGraphFromFile(ploy_file_path);

    // Start watching the new file for live reload
    if (!ploy_file_path.isEmpty()) {
        file_watcher_->addPath(ploy_file_path);
    }
}

void TopologyPanel::Clear() {
    StopForceLayout();
    scene_->clear();
    node_items_.clear();
    edge_items_.clear();
    details_tree_->clear();
    diagnostics_output_->clear();
    status_label_->setText("No topology loaded");
}

void TopologyPanel::HighlightDebugNode(const QString &file, int line) {
    ClearDebugHighlights();
    for (auto &[id, item] : node_items_) {
        if (item->SourceFile() == file && item->SourceLine() == line) {
            item->SetDebugHighlight(true);
            // Scroll the view to center on the highlighted node
            view_->centerOn(item);
            return;
        }
    }
    // If exact line match fails, find the closest node in the same file
    TopoNodeItem *closest = nullptr;
    int min_dist = INT_MAX;
    for (auto &[id, item] : node_items_) {
        if (item->SourceFile() == file && item->SourceLine() > 0) {
            int dist = std::abs(item->SourceLine() - line);
            if (dist < min_dist) {
                min_dist = dist;
                closest = item;
            }
        }
    }
    if (closest && min_dist <= 10) {
        closest->SetDebugHighlight(true);
        view_->centerOn(closest);
    }
}

void TopologyPanel::ClearDebugHighlights() {
    for (auto &[id, item] : node_items_) {
        item->SetDebugHighlight(false);
    }
}

void TopologyPanel::HighlightExecutingNode(uint64_t node_id) {
    // Clear previous execution highlight
    ClearExecutionHighlight();

    auto it = node_items_.find(node_id);
    if (it != node_items_.end()) {
        execution_highlight_id_ = node_id;
        it->second->SetDebugHighlight(true);
        view_->centerOn(it->second);
    }
}

void TopologyPanel::ClearExecutionHighlight() {
    if (execution_highlight_id_ != 0) {
        auto it = node_items_.find(execution_highlight_id_);
        if (it != node_items_.end()) {
            it->second->SetDebugHighlight(false);
        }
        execution_highlight_id_ = 0;
    }
}

void TopologyPanel::TryCreateEdge(TopoPortItem *source, TopoPortItem *target) {
    if (!source || !target) return;

    // Validate: output → input or input → output
    bool valid = false;
    TopoPortItem *from = nullptr;
    TopoPortItem *to = nullptr;
    if (source->PortDirection() == TopoPortItem::Direction::kOutput &&
        target->PortDirection() == TopoPortItem::Direction::kInput) {
        from = source;
        to = target;
        valid = true;
    } else if (source->PortDirection() == TopoPortItem::Direction::kInput &&
               target->PortDirection() == TopoPortItem::Direction::kOutput) {
        from = target;
        to = source;
        valid = true;
    }

    if (!valid) {
        diagnostics_output_->appendPlainText(
            "[Edge] Cannot connect: must link an output port to an input port.");
        return;
    }

    // Prevent self-loops
    if (from->ParentNode() == to->ParentNode()) {
        diagnostics_output_->appendPlainText(
            "[Edge] Cannot connect: self-loop not allowed.");
        return;
    }

    // Check for duplicate edges
    for (const auto *existing : edge_items_) {
        if (existing->SourceNodeId() == from->ParentNode()->NodeId() &&
            existing->SourcePortId() == from->PortId() &&
            existing->TargetNodeId() == to->ParentNode()->NodeId() &&
            existing->TargetPortId() == to->PortId()) {
            diagnostics_output_->appendPlainText(
                "[Edge] Duplicate edge — already connected.");
            return;
        }
    }

    // Create the edge
    QPointF start_pos = from->ParentNode()->OutputPortPos(from->PortId());
    QPointF end_pos = to->ParentNode()->InputPortPos(to->PortId());

    uint64_t new_id = next_interactive_edge_id_++;
    auto *edge = new TopoEdgeItem(new_id, start_pos, end_pos, "unknown");
    edge->SetEndpointIds(from->ParentNode()->NodeId(), from->PortId(),
                         to->ParentNode()->NodeId(), to->PortId());
    scene_->addItem(edge);
    edge_items_.push_back(edge);

    diagnostics_output_->appendPlainText(
        QString("[Edge] Created edge %1: %2.%3 → %4.%5")
            .arg(new_id)
            .arg(from->ParentNode()->NodeName(), from->PortName(),
                 to->ParentNode()->NodeName(), to->PortName()));

    // Sync the new edge back to the .ploy file as a LINK declaration
    SyncEdgeToFile(edge);

    emit GraphModified();
}

void TopologyPanel::RemoveEdge(TopoEdgeItem *edge) {
    if (!edge) return;

    // Sync-remove the corresponding LINK/CALL line from the .ploy file
    RemoveEdgeFromFile(edge);

    auto it = std::find(edge_items_.begin(), edge_items_.end(), edge);
    if (it != edge_items_.end()) {
        edge_items_.erase(it);
    }
    diagnostics_output_->appendPlainText(
        QString("[Edge] Removed edge %1").arg(edge->EdgeId()));
    scene_->removeItem(edge);
    delete edge;
    emit GraphModified();
}

void TopologyPanel::SyncEdgeToFile(TopoEdgeItem *edge) {
    if (current_file_.isEmpty() || !edge) return;

    // Resolve node info for the LINK statement
    auto src_it = node_items_.find(edge->SourceNodeId());
    auto tgt_it = node_items_.find(edge->TargetNodeId());
    if (src_it == node_items_.end() || tgt_it == node_items_.end()) return;

    TopoNodeItem *src_node = src_it->second;
    TopoNodeItem *tgt_node = tgt_it->second;
    QString src_lang = src_node->Language();
    QString tgt_lang = tgt_node->Language();
    QString src_name = src_node->NodeName();
    QString tgt_name = tgt_node->NodeName();

    // Strip language prefix from node names (e.g. "cpp::math_ops::add" → "math_ops::add")
    if (src_name.startsWith(src_lang + "::"))
        src_name = src_name.mid(src_lang.length() + 2);
    if (tgt_name.startsWith(tgt_lang + "::"))
        tgt_name = tgt_name.mid(tgt_lang.length() + 2);

    if (src_name.isEmpty() || tgt_name.isEmpty()) return;

    // Resolve return type from source node's output ports
    QString returns_clause;
    for (auto *p : src_node->OutputPorts()) {
        if (p->PortId() == edge->SourcePortId() &&
            !p->TypeName().isEmpty() && p->TypeName() != "Any") {
            returns_clause = QString(" RETURNS %1::%2")
                                 .arg(src_lang, p->TypeName().toUpper());
            break;
        }
    }

    // Resolve MAP_TYPE entries from port types
    QStringList map_types;
    for (auto *sp : src_node->OutputPorts()) {
        if (sp->PortId() == edge->SourcePortId()) {
            for (auto *tp : tgt_node->InputPorts()) {
                if (tp->PortId() == edge->TargetPortId()) {
                    if (!sp->TypeName().isEmpty() && sp->TypeName() != "Any" &&
                        !tp->TypeName().isEmpty() && tp->TypeName() != "Any") {
                        map_types.append(QString("    MAP_TYPE(%1::%2, %3::%4);")
                                             .arg(src_lang, sp->TypeName(),
                                                  tgt_lang, tp->TypeName()));
                    }
                    break;
                }
            }
            break;
        }
    }

    // Build the LINK statement in correct ploy syntax:
    // LINK(src_lang, tgt_lang, src_func, tgt_func) RETURNS type { MAP_TYPE(...); }
    QString link_stmt = QString("\nLINK(%1, %2, %3, %4)%5 {\n%6}\n")
                            .arg(src_lang, tgt_lang, src_name, tgt_name)
                            .arg(returns_clause)
                            .arg(map_types.isEmpty() ? QString()
                                                     : map_types.join('\n') + "\n");

    // Temporarily remove the file watcher to avoid triggering a reload loop
    file_watcher_->removePath(current_file_);

    // Append the LINK statement to the .ploy file
    QFile file(current_file_);
    if (file.open(QIODevice::ReadWrite | QIODevice::Text)) {
        // Count existing lines to determine the appended line number
        QString existing = QTextStream(&file).readAll();
        int line_count = existing.count('\n') + 1;
        file.seek(file.size());
        QTextStream stream(&file);
        stream << link_stmt;
        file.close();
        diagnostics_output_->appendPlainText(
            "[Sync] Appended LINK statement to " + current_file_);
        // Notify the editor to highlight the newly appended line
        emit FileContentChanged(current_file_, line_count + 1);
    } else {
        diagnostics_output_->appendPlainText(
            "[Sync] Failed to write to " + current_file_);
    }

    // Re-add the file watcher
    file_watcher_->addPath(current_file_);
}

void TopologyPanel::RemoveEdgeFromFile(TopoEdgeItem *edge) {
    if (current_file_.isEmpty() || !edge) return;

    // Resolve node info to find the matching LINK block in the file
    auto src_it = node_items_.find(edge->SourceNodeId());
    auto tgt_it = node_items_.find(edge->TargetNodeId());
    if (src_it == node_items_.end() || tgt_it == node_items_.end()) return;

    QString src_lang = src_it->second->Language();
    QString tgt_lang = tgt_it->second->Language();
    QString src_name = src_it->second->NodeName();
    QString tgt_name = tgt_it->second->NodeName();

    // Strip language prefix from node names
    if (src_name.startsWith(src_lang + "::"))
        src_name = src_name.mid(src_lang.length() + 2);
    if (tgt_name.startsWith(tgt_lang + "::"))
        tgt_name = tgt_name.mid(tgt_lang.length() + 2);

    if (src_name.isEmpty() || tgt_name.isEmpty()) return;

    // Build patterns to match:
    //   New format: "LINK(src_lang, tgt_lang, src_func, tgt_func)"
    //   Legacy format: "LINK src_name.port -> tgt_name.port"
    QString new_pattern = QString("LINK(%1, %2, %3, %4)")
                              .arg(src_lang, tgt_lang, src_name, tgt_name);

    // Temporarily remove the file watcher to avoid triggering a reload loop
    file_watcher_->removePath(current_file_);

    // Read the file, remove matching LINK block (including body { ... }), write back
    QFile file(current_file_);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        QString content = in.readAll();
        file.close();

        QStringList lines = content.split('\n');
        QStringList filtered;
        bool removed = false;
        int removed_line = -1;
        bool inside_block = false;
        int brace_depth = 0;

        for (int i = 0; i < lines.size(); ++i) {
            QString trimmed = lines[i].trimmed();

            // If we're inside a block being removed, skip lines until closing brace
            if (inside_block) {
                for (QChar ch : lines[i]) {
                    if (ch == '{') ++brace_depth;
                    if (ch == '}') --brace_depth;
                }
                if (brace_depth <= 0) {
                    inside_block = false;
                }
                continue;  // Skip this line
            }

            if (!removed && trimmed.startsWith("LINK") &&
                (trimmed.contains(new_pattern) ||
                 (trimmed.contains(src_name) && trimmed.contains(tgt_name)))) {
                removed = true;
                removed_line = i + 1;

                // Check if this line opens a brace block
                brace_depth = 0;
                for (QChar ch : lines[i]) {
                    if (ch == '{') ++brace_depth;
                    if (ch == '}') --brace_depth;
                }
                if (brace_depth > 0) {
                    inside_block = true;  // Continue removing lines until closing brace
                }
                continue;
            }
            filtered.append(lines[i]);
        }

        if (removed) {
            QFile out_file(current_file_);
            if (out_file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream out(&out_file);
                out << filtered.join('\n');
                out_file.close();
                diagnostics_output_->appendPlainText(
                    "[Sync] Removed LINK/CALL statement from " + current_file_);
                // Notify the editor to highlight the area around the removed line
                emit FileContentChanged(current_file_,
                                        std::max(1, removed_line - 1));
            }
        }
    }

    // Re-add the file watcher
    file_watcher_->addPath(current_file_);
}

void TopologyPanel::BuildGraphFromFile(const QString &path) {
    Clear();

    // Read file
    std::ifstream ifs(path.toStdString());
    if (!ifs.is_open()) {
        diagnostics_output_->appendPlainText("Error: Cannot open " + path);
        return;
    }
    std::string source((std::istreambuf_iterator<char>(ifs)),
                        std::istreambuf_iterator<char>());
    ifs.close();

    std::string filename = path.toStdString();

    // Parse
    frontends::Diagnostics diagnostics;
    ploy::PloyLexer lexer(source, filename);
    ploy::PloyParser parser(lexer, diagnostics);
    parser.ParseModule();
    auto module = parser.TakeModule();

    if (!module) {
        diagnostics_output_->appendPlainText("Parse failed for " + path);
        return;
    }

    // Sema
    ploy::PloySemaOptions sema_opts;
    sema_opts.enable_package_discovery = false;
    sema_opts.strict_mode = false;
    ploy::PloySema sema(diagnostics, sema_opts);
    sema.Analyze(module);

    // Foreign signature extraction — read real types from external source files
    {
        tools::ForeignExtractionOptions feopts;
        feopts.base_directory =
            std::filesystem::path(filename).parent_path().string();
        tools::ForeignSignatureExtractor extractor(feopts);
        auto foreign_sigs = extractor.ExtractAll(*module);
        if (!foreign_sigs.empty())
            sema.InjectForeignSignatures(foreign_sigs);
    }

    // Build topology
    topo::TopologyAnalyzer analyzer(sema);
    if (!analyzer.Build(module)) {
        diagnostics_output_->appendPlainText("Topology analysis failed");
        return;
    }

    const auto &graph = analyzer.Graph();

    // Create node items
    for (const auto &node : graph.Nodes()) {
        QString kind_str;
        switch (node.kind) {
        case topo::TopologyNode::Kind::kFunction:     kind_str = "fn"; break;
        case topo::TopologyNode::Kind::kConstructor:  kind_str = "ctor"; break;
        case topo::TopologyNode::Kind::kMethod:       kind_str = "method"; break;
        case topo::TopologyNode::Kind::kPipeline:     kind_str = "pipe"; break;
        case topo::TopologyNode::Kind::kMapFunc:      kind_str = "map"; break;
        case topo::TopologyNode::Kind::kExternalCall: kind_str = "ext"; break;
        }

        auto *item = new TopoNodeItem(
            node.id,
            QString::fromStdString(node.name),
            QString::fromStdString(node.language),
            kind_str);

        // Store source location for debug mapping
        item->SetSourceLocation(
            QString::fromStdString(node.loc.file),
            static_cast<int>(node.loc.line));

        // Add ports
        for (const auto &port : node.inputs) {
            item->AddInputPort(port.id,
                               QString::fromStdString(port.name),
                               QString::fromStdString(
                                   port.type.name.empty() ? "Any" : port.type.name));
        }
        for (const auto &port : node.outputs) {
            item->AddOutputPort(port.id,
                                QString::fromStdString(port.name),
                                QString::fromStdString(
                                    port.type.name.empty() ? "Any" : port.type.name));
        }

        scene_->addItem(item);
        node_items_[node.id] = item;
    }

    // Initial layout (grid as starting positions, then force if selected)
    LayoutNodes();

    // Create edge items
    for (const auto &edge : graph.Edges()) {
        auto src_it = node_items_.find(edge.source_node_id);
        auto tgt_it = node_items_.find(edge.target_node_id);
        if (src_it == node_items_.end() || tgt_it == node_items_.end()) continue;

        QPointF start = src_it->second->OutputPortPos(edge.source_port_id);
        QPointF end = tgt_it->second->InputPortPos(edge.target_port_id);

        QString status;
        switch (edge.status) {
        case topo::TopologyEdge::Status::kValid:           status = "valid"; break;
        case topo::TopologyEdge::Status::kImplicitConvert: status = "implicit_convert"; break;
        case topo::TopologyEdge::Status::kExplicitConvert: status = "explicit_convert"; break;
        case topo::TopologyEdge::Status::kIncompatible:    status = "incompatible"; break;
        case topo::TopologyEdge::Status::kUnknown:         status = "unknown"; break;
        }

        auto *edge_item = new TopoEdgeItem(edge.id, start, end, status);
        edge_item->SetEndpointIds(edge.source_node_id, edge.source_port_id,
                                  edge.target_node_id, edge.target_port_id);
        scene_->addItem(edge_item);
        edge_items_.push_back(edge_item);
    }

    // Re-watch the file (QFileSystemWatcher may drop paths after changes)
    if (!current_file_.isEmpty() &&
        !file_watcher_->files().contains(current_file_)) {
        file_watcher_->addPath(current_file_);
    }

    status_label_->setText(QString("Topology: %1 nodes, %2 edges — %3")
                               .arg(graph.NodeCount())
                               .arg(graph.EdgeCount())
                               .arg(path));
}

void TopologyPanel::LayoutNodes() {
    int mode = layout_combo_ ? layout_combo_->currentIndex() : 0;

    if (mode == 0) {
        // Force-directed: seed with grid positions then run simulation
        qreal spacing_x = kNodeWidth + 80;
        qreal spacing_y = 140;
        size_t cols = std::max(static_cast<size_t>(1),
                               static_cast<size_t>(std::ceil(std::sqrt(
                                   static_cast<double>(node_items_.size())))));
        // Add slight random jitter so force layout can separate overlapping nodes
        std::mt19937 rng(42);
        std::uniform_real_distribution<double> jitter(-20.0, 20.0);
        size_t i = 0;
        for (auto &[id, item] : node_items_) {
            size_t col = i % cols;
            size_t row = i / cols;
            qreal x = static_cast<qreal>(col) * spacing_x + jitter(rng);
            qreal y = static_cast<qreal>(row) * spacing_y + jitter(rng);
            item->setPos(x, y);
            ++i;
        }
        StartForceLayout();
    } else {
        // Grid layout (top-down or left-right)
        StopForceLayout();
        bool horizontal = (mode == 2);
        qreal spacing_x = kNodeWidth + 60;
        qreal spacing_y = 120;
        size_t cols = std::max(static_cast<size_t>(1),
                               static_cast<size_t>(std::ceil(std::sqrt(
                                   static_cast<double>(node_items_.size())))));
        size_t i = 0;
        for (auto &[id, item] : node_items_) {
            size_t col = i % cols;
            size_t row = i / cols;
            qreal x, y;
            if (horizontal) {
                x = static_cast<qreal>(row) * spacing_x;
                y = static_cast<qreal>(col) * spacing_y;
            } else {
                x = static_cast<qreal>(col) * spacing_x;
                y = static_cast<qreal>(row) * spacing_y;
            }
            item->setPos(x, y);
            ++i;
        }
        RefreshEdgePositions();
    }
}

void TopologyPanel::StartForceLayout() {
    force_iterations_remaining_ = kForceMaxIterations;
    if (!force_timer_->isActive()) {
        force_timer_->start();
    }
}

void TopologyPanel::StopForceLayout() {
    force_timer_->stop();
    force_iterations_remaining_ = 0;
}

void TopologyPanel::OnForceLayoutTick() {
    if (force_iterations_remaining_ <= 0 || node_items_.size() < 2) {
        StopForceLayout();
        RefreshEdgePositions();
        return;
    }
    --force_iterations_remaining_;

    // Compute forces on each node
    struct NodeForce {
        double fx{0.0};
        double fy{0.0};
    };
    std::unordered_map<uint64_t, NodeForce> forces;
    for (auto &[id, item] : node_items_) {
        forces[id] = {0.0, 0.0};
    }

    /** @name Repulsion between all node pairs (Coulomb's law) */
    /** @{ */
    auto ids = std::vector<uint64_t>();
    ids.reserve(node_items_.size());
    for (auto &[id, item] : node_items_) ids.push_back(id);

    for (size_t a = 0; a < ids.size(); ++a) {
        for (size_t b = a + 1; b < ids.size(); ++b) {
            auto *na = node_items_[ids[a]];
            auto *nb = node_items_[ids[b]];
            double dx = na->x() - nb->x();
            double dy = na->y() - nb->y();
            double dist_sq = dx * dx + dy * dy;
            if (dist_sq < 1.0) dist_sq = 1.0;
            double dist = std::sqrt(dist_sq);
            double force = kRepulsionStrength / dist_sq;
            double fx = force * (dx / dist);
            double fy = force * (dy / dist);
            forces[ids[a]].fx += fx;
            forces[ids[a]].fy += fy;
            forces[ids[b]].fx -= fx;
            forces[ids[b]].fy -= fy;
        }
    }

    /** @} */

    /** @name Attraction along edges (Hooke's law) */
    /** @{ */
    for (const auto *edge : edge_items_) {
        auto src_it = node_items_.find(edge->SourceNodeId());
        auto tgt_it = node_items_.find(edge->TargetNodeId());
        if (src_it == node_items_.end() || tgt_it == node_items_.end()) continue;

        auto *ns = src_it->second;
        auto *nt = tgt_it->second;
        double dx = nt->x() - ns->x();
        double dy = nt->y() - ns->y();
        double dist = std::sqrt(dx * dx + dy * dy);
        if (dist < 1.0) dist = 1.0;
        double displacement = dist - kIdealEdgeLength;
        double force = kAttractionStrength * displacement;
        double fx = force * (dx / dist);
        double fy = force * (dy / dist);
        forces[src_it->first].fx += fx;
        forces[src_it->first].fy += fy;
        forces[tgt_it->first].fx -= fx;
        forces[tgt_it->first].fy -= fy;
    }

    /** @} */

    /** @name Apply forces with damping */
    /** @{ */
    double damping = kDamping;
    // Increase damping as iterations progress (simulated annealing)
    double progress = 1.0 - static_cast<double>(force_iterations_remaining_) /
                                static_cast<double>(kForceMaxIterations);
    damping *= (1.0 - progress * 0.5);

    double max_movement = 0.0;
    for (auto &[id, item] : node_items_) {
        if (item->isSelected()) continue;  // Don't move items being dragged
        double fx = forces[id].fx * damping;
        double fy = forces[id].fy * damping;
        // Clamp maximum displacement per tick
        double mag = std::sqrt(fx * fx + fy * fy);
        constexpr double kMaxDisplacement = 30.0;
        if (mag > kMaxDisplacement) {
            fx = fx / mag * kMaxDisplacement;
            fy = fy / mag * kMaxDisplacement;
        }
        item->moveBy(fx, fy);
        max_movement = std::max(max_movement, std::abs(fx) + std::abs(fy));
    }

    RefreshEdgePositions();

    // Early termination if the system has settled
    if (max_movement < kMinMovement) {
        StopForceLayout();
    }
}

void TopologyPanel::RefreshEdgePositions() {
    for (auto *edge : edge_items_) {
        auto src_it = node_items_.find(edge->SourceNodeId());
        auto tgt_it = node_items_.find(edge->TargetNodeId());
        if (src_it == node_items_.end() || tgt_it == node_items_.end()) continue;
        QPointF start = src_it->second->OutputPortPos(edge->SourcePortId());
        QPointF end = tgt_it->second->InputPortPos(edge->TargetPortId());
        edge->UpdateEndpoints(start, end);
    }
}

void TopologyPanel::UpdateDetailsPanel(uint64_t node_id) {
    details_tree_->clear();
    auto it = node_items_.find(node_id);
    if (it == node_items_.end()) return;

    auto *item = it->second;
    auto *root = new QTreeWidgetItem(details_tree_, {"Node", ""});
    new QTreeWidgetItem(root, {"ID", QString::number(static_cast<qulonglong>(node_id))});
    new QTreeWidgetItem(root, {"Name", item->NodeName()});
    new QTreeWidgetItem(root, {"Language", item->Language()});
    new QTreeWidgetItem(root, {"Kind", item->Kind()});
    new QTreeWidgetItem(root, {"Source", item->SourceFile() + ":" +
                                            QString::number(item->SourceLine())});
    root->setExpanded(true);

    // Input ports
    if (!item->InputPorts().empty()) {
        auto *in_root = new QTreeWidgetItem(details_tree_, {"Inputs", ""});
        for (const auto *port : item->InputPorts()) {
            new QTreeWidgetItem(in_root,
                {port->PortName(), port->TypeName()});
        }
        in_root->setExpanded(true);
    }
    // Output ports
    if (!item->OutputPorts().empty()) {
        auto *out_root = new QTreeWidgetItem(details_tree_, {"Outputs", ""});
        for (const auto *port : item->OutputPorts()) {
            new QTreeWidgetItem(out_root,
                {port->PortName(), port->TypeName()});
        }
        out_root->setExpanded(true);
    }

    details_tree_->expandAll();
}

// ============================================================================
// Slots
// ============================================================================

void TopologyPanel::OnRefresh() {
    if (!current_file_.isEmpty()) {
        BuildGraphFromFile(current_file_);
    }
}

void TopologyPanel::OnValidate() {
    if (current_file_.isEmpty()) return;

    // Re-parse and validate
    std::ifstream ifs(current_file_.toStdString());
    if (!ifs.is_open()) return;
    std::string source((std::istreambuf_iterator<char>(ifs)),
                        std::istreambuf_iterator<char>());
    ifs.close();

    frontends::Diagnostics diagnostics;
    ploy::PloyLexer lexer(source, current_file_.toStdString());
    ploy::PloyParser parser(lexer, diagnostics);
    parser.ParseModule();
    auto module = parser.TakeModule();
    if (!module) return;

    ploy::PloySemaOptions sema_opts;
    sema_opts.enable_package_discovery = false;
    ploy::PloySema sema(diagnostics, sema_opts);
    sema.Analyze(module);

    topo::TopologyAnalyzer analyzer(sema);
    analyzer.Build(module);

    topo::ValidationOptions val_opts;
    topo::TopologyValidator validator(val_opts);
    validator.Validate(analyzer.Graph());

    // Display diagnostics
    diagnostics_output_->clear();
    for (const auto &d : validator.Diagnostics()) {
        QString severity;
        switch (d.severity) {
        case topo::ValidationDiagnostic::Severity::kError:   severity = "ERROR"; break;
        case topo::ValidationDiagnostic::Severity::kWarning: severity = "WARN"; break;
        case topo::ValidationDiagnostic::Severity::kInfo:    severity = "INFO"; break;
        }
        diagnostics_output_->appendPlainText(
            "[" + severity + "] " + QString::fromStdString(d.message));
    }

    // Highlight error nodes
    for (auto &[id, item] : node_items_) {
        bool has_error = false;
        for (const auto &d : validator.Diagnostics()) {
            if (d.severity == topo::ValidationDiagnostic::Severity::kError &&
                d.node_id == id) {
                has_error = true;
                break;
            }
        }
        item->SetHighlight(has_error);
    }

    emit ValidationComplete(static_cast<int>(validator.ErrorCount()),
                            static_cast<int>(validator.WarningCount()));
}

void TopologyPanel::OnExportDot() {
    QString path = QFileDialog::getSaveFileName(this, "Export DOT", "", "DOT files (*.dot)");
    if (path.isEmpty()) return;

    if (current_file_.isEmpty()) return;

    std::ifstream ifs(current_file_.toStdString());
    if (!ifs.is_open()) return;
    std::string source((std::istreambuf_iterator<char>(ifs)),
                        std::istreambuf_iterator<char>());
    ifs.close();

    frontends::Diagnostics diagnostics;
    ploy::PloyLexer lexer(source, current_file_.toStdString());
    ploy::PloyParser parser(lexer, diagnostics);
    parser.ParseModule();
    auto module = parser.TakeModule();
    if (!module) return;

    ploy::PloySemaOptions sema_opts;
    sema_opts.enable_package_discovery = false;
    ploy::PloySema sema(diagnostics, sema_opts);
    sema.Analyze(module);

    topo::TopologyAnalyzer analyzer(sema);
    analyzer.Build(module);

    std::ofstream ofs(path.toStdString());
    topo::PrintOptions print_opts;
    print_opts.use_color = false;
    topo::TopologyPrinter printer(print_opts);
    printer.PrintDot(analyzer.Graph(), ofs);

    diagnostics_output_->appendPlainText("Exported DOT to " + path);
}

void TopologyPanel::OnExportJson() {
    QString path = QFileDialog::getSaveFileName(this, "Export JSON", "", "JSON files (*.json)");
    if (path.isEmpty()) return;

    if (current_file_.isEmpty()) return;

    std::ifstream ifs(current_file_.toStdString());
    if (!ifs.is_open()) return;
    std::string source((std::istreambuf_iterator<char>(ifs)),
                        std::istreambuf_iterator<char>());
    ifs.close();

    frontends::Diagnostics diagnostics;
    ploy::PloyLexer lexer(source, current_file_.toStdString());
    ploy::PloyParser parser(lexer, diagnostics);
    parser.ParseModule();
    auto module = parser.TakeModule();
    if (!module) return;

    ploy::PloySemaOptions sema_opts;
    sema_opts.enable_package_discovery = false;
    ploy::PloySema sema(diagnostics, sema_opts);
    sema.Analyze(module);

    topo::TopologyAnalyzer analyzer(sema);
    analyzer.Build(module);

    std::ofstream ofs(path.toStdString());
    topo::PrintOptions print_opts;
    print_opts.use_color = false;
    topo::TopologyPrinter printer(print_opts);
    printer.PrintJson(analyzer.Graph(), ofs);

    diagnostics_output_->appendPlainText("Exported JSON to " + path);
}

void TopologyPanel::OnExportPng() {
    QString path = QFileDialog::getSaveFileName(this, "Export PNG", "", "PNG files (*.png)");
    if (path.isEmpty()) return;

    // Render scene to image
    QRectF sr = scene_->sceneRect();
    if (sr.isEmpty()) return;
    QImage image(static_cast<int>(sr.width()), static_cast<int>(sr.height()),
                 QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor("#1E1E1E"));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    scene_->render(&painter);
    painter.end();
    image.save(path);

    diagnostics_output_->appendPlainText("Exported PNG to " + path);
}

void TopologyPanel::OnGeneratePloy() {
    if (current_file_.isEmpty()) {
        diagnostics_output_->appendPlainText(
            "[Generate] No topology loaded — open a .ploy file first.");
        return;
    }

    // Re-parse the current file to obtain a fresh TopologyGraph
    std::ifstream ifs(current_file_.toStdString());
    if (!ifs.is_open()) {
        diagnostics_output_->appendPlainText(
            "[Generate] Cannot open " + current_file_);
        return;
    }
    std::string source((std::istreambuf_iterator<char>(ifs)),
                        std::istreambuf_iterator<char>());
    ifs.close();

    frontends::Diagnostics diagnostics;
    ploy::PloyLexer lexer(source, current_file_.toStdString());
    ploy::PloyParser parser(lexer, diagnostics);
    parser.ParseModule();
    auto module = parser.TakeModule();
    if (!module) {
        diagnostics_output_->appendPlainText("[Generate] Parse failed.");
        return;
    }

    ploy::PloySemaOptions sema_opts;
    sema_opts.enable_package_discovery = false;
    ploy::PloySema sema(diagnostics, sema_opts);
    sema.Analyze(module);

    topo::TopologyAnalyzer analyzer(sema);
    if (!analyzer.Build(module)) {
        diagnostics_output_->appendPlainText(
            "[Generate] Topology analysis failed.");
        return;
    }

    auto &graph = analyzer.MutableGraph();
    graph.source_file = current_file_.toStdString();

    // Generate .ploy source
    std::string generated = topo::GeneratePloySrc(graph);

    // Verify the generated source is parseable
    {
        frontends::Diagnostics verify_diags;
        ploy::PloyLexer vlex(generated, "<generated>");
        ploy::PloyParser vparser(vlex, verify_diags);
        vparser.ParseModule();
        auto vmod = vparser.TakeModule();
        if (!vmod) {
            diagnostics_output_->appendPlainText(
                "[Generate] Warning: generated code has parse errors:");
            for (const auto &d : verify_diags.All()) {
                diagnostics_output_->appendPlainText(
                    "  " + QString::fromStdString(d.message));
            }
        }

        ploy::PloySemaOptions vsema_opts;
        vsema_opts.enable_package_discovery = false;
        vsema_opts.strict_mode = false;
        ploy::PloySema vsema(verify_diags, vsema_opts);
        if (vmod) {
            vsema.Analyze(vmod);
        }
    }

    // Write to <basename>_generated.ploy in the same directory
    namespace fs = std::filesystem;
    fs::path src_path(current_file_.toStdString());
    std::string stem = src_path.stem().string();
    fs::path gen_path = src_path.parent_path() / (stem + "_generated.ploy");

    std::ofstream ofs(gen_path.string());
    if (!ofs.is_open()) {
        diagnostics_output_->appendPlainText(
            "[Generate] Cannot write to " +
            QString::fromStdString(gen_path.string()));
        return;
    }
    ofs << generated;
    ofs.close();

    QString gen_path_q = QString::fromStdString(gen_path.string());
    diagnostics_output_->appendPlainText(
        "[Generate] Generated .ploy source: " + gen_path_q);

    // Request the editor to open the generated file
    emit OpenFileRequested(gen_path_q);
}

void TopologyPanel::OnZoomIn() {
    view_->scale(1.2, 1.2);
}

void TopologyPanel::OnZoomOut() {
    view_->scale(1.0 / 1.2, 1.0 / 1.2);
}

void TopologyPanel::OnZoomFit() {
    view_->fitInView(scene_->sceneRect(), Qt::KeepAspectRatio);
}

void TopologyPanel::OnNodeSelected() {
    auto items = scene_->selectedItems();
    if (items.isEmpty()) return;

    for (auto *item : items) {
        auto *node_item = dynamic_cast<TopoNodeItem *>(item);
        if (node_item) {
            UpdateDetailsPanel(node_item->NodeId());
            return;
        }
    }
}

void TopologyPanel::OnLayoutChanged(int index) {
    Q_UNUSED(index);
    LayoutNodes();
}

void TopologyPanel::OnFileChanged(const QString &path) {
    Q_UNUSED(path);
    // Start or restart the debounce timer.  The actual reload happens in
    // the timer callback to coalesce rapid successive writes.
    reload_debounce_->start();
}

} // namespace polyglot::tools::ui

/** @} */