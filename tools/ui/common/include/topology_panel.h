/**
 * @file     topology_panel.h
 * @brief    Topology visualization panel for the PolyglotCompiler IDE
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <QAction>
#include <QComboBox>
#include <QFileSystemWatcher>
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace polyglot::tools::ui {

// Forward declarations
class TopoNodeItem;
class TopoEdgeItem;
class TopologyPanel;

// ============================================================================
// TopoPortItem — a port dot that supports hover tooltips and drag-connect
// ============================================================================

/** @brief TopoPortItem class. */
class TopoPortItem : public QGraphicsEllipseItem {
  public:
    /** @brief Direction enumeration. */
    enum class Direction { kInput, kOutput };

    TopoPortItem(uint64_t port_id, Direction direction,
                 const QString &name, const QString &type_name,
                 TopoNodeItem *parent_node);

    uint64_t PortId() const { return port_id_; }
    Direction PortDirection() const { return direction_; }
    TopoNodeItem *ParentNode() const { return parent_node_; }
    const QString &PortName() const { return name_; }
    const QString &TypeName() const { return type_name_; }

  protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent *event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent *event) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;

  private:
    uint64_t port_id_;
    Direction direction_;
    QString name_;
    QString type_name_;
    TopoNodeItem *parent_node_;
};

// ============================================================================
// TopoNodeItem — a graphical node in the topology scene
// ============================================================================

/** @brief TopoNodeItem class. */
class TopoNodeItem : public QGraphicsRectItem {
  public:
    TopoNodeItem(uint64_t node_id, const QString &name,
                 const QString &language, const QString &kind,
                 QGraphicsItem *parent = nullptr);

    uint64_t NodeId() const { return node_id_; }
    const QString &NodeName() const { return name_; }
    const QString &Language() const { return language_; }
    const QString &Kind() const { return kind_; }

    // Add an input or output port; returns the port's scene-center position
    QPointF AddInputPort(uint64_t port_id, const QString &name,
                         const QString &type_name);
    QPointF AddOutputPort(uint64_t port_id, const QString &name,
                          const QString &type_name);

    // Get port center position (for drawing edges)
    QPointF InputPortPos(uint64_t port_id) const;
    QPointF OutputPortPos(uint64_t port_id) const;

    // Get port item by id
    TopoPortItem *InputPort(uint64_t port_id) const;
    TopoPortItem *OutputPort(uint64_t port_id) const;

    // List all port items
    const std::vector<TopoPortItem *> &InputPorts() const { return input_ports_; }
    const std::vector<TopoPortItem *> &OutputPorts() const { return output_ports_; }

    // Highlight modes
    void SetHighlight(bool error);
    void SetDebugHighlight(bool active);

    // Pulse animation for execution highlighting
    void StartPulseAnimation();
    void StopPulseAnimation();
    qreal PulseOpacity() const { return pulse_opacity_; }

    // Source location for debug mapping
    void SetSourceLocation(const QString &file, int line);
    const QString &SourceFile() const { return source_file_; }
    int SourceLine() const { return source_line_; }

  protected:
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

  private:
    void LayoutPorts();

    uint64_t node_id_;
    QString name_;
    QString language_;
    QString kind_;

    std::vector<TopoPortItem *> input_ports_;
    std::vector<TopoPortItem *> output_ports_;
    // Parallel label storage (child text items for port labels)
    /** @brief PortLabel data structure. */
    struct PortLabel {
        QGraphicsTextItem *label{nullptr};
    };
    std::vector<PortLabel> input_labels_;
    std::vector<PortLabel> output_labels_;

    bool highlight_error_{false};
    bool debug_active_{false};

    // Pulse animation state for execution highlighting
    QTimer *pulse_timer_{nullptr};
    qreal pulse_opacity_{1.0};
    bool pulse_rising_{false};

    QString source_file_;
    int source_line_{0};
};

// ============================================================================
// TopoEdgeItem — a graphical connection between two ports
// ============================================================================

/** @brief TopoEdgeItem class. */
class TopoEdgeItem : public QGraphicsPathItem {
  public:
    TopoEdgeItem(uint64_t edge_id,
                 const QPointF &start, const QPointF &end,
                 const QString &status,
                 QGraphicsItem *parent = nullptr);

    uint64_t EdgeId() const { return edge_id_; }
    const QString &Status() const { return status_; }
    void SetStatus(const QString &status);
    void UpdateEndpoints(const QPointF &start, const QPointF &end);

    // Source/target info for deletion
    uint64_t SourceNodeId() const { return source_node_id_; }
    uint64_t TargetNodeId() const { return target_node_id_; }
    uint64_t SourcePortId() const { return source_port_id_; }
    uint64_t TargetPortId() const { return target_port_id_; }
    void SetEndpointIds(uint64_t src_node, uint64_t src_port,
                        uint64_t tgt_node, uint64_t tgt_port);

  protected:
    void contextMenuEvent(QGraphicsSceneContextMenuEvent *event) override;

  private:
    void RebuildPath(const QPointF &start, const QPointF &end);
    uint64_t edge_id_;
    QString status_;

    uint64_t source_node_id_{0};
    uint64_t source_port_id_{0};
    uint64_t target_node_id_{0};
    uint64_t target_port_id_{0};
};

// ============================================================================
// TopoGraphicsView — custom QGraphicsView that manages drag-to-connect
// ============================================================================

/** @brief TopoGraphicsView class. */
class TopoGraphicsView : public QGraphicsView {
    Q_OBJECT

  public:
    explicit TopoGraphicsView(QGraphicsScene *scene, QWidget *parent = nullptr);

    void SetPanel(TopologyPanel *panel) { panel_ = panel; }
    TopologyPanel *Panel() const { return panel_; }

    // Called by TopoPortItem when a drag starts / moves / ends
    void BeginPortDrag(TopoPortItem *source_port, const QPointF &scene_pos);
    void UpdatePortDrag(const QPointF &scene_pos);
    void EndPortDrag(const QPointF &scene_pos);
    void CancelPortDrag();

    bool IsDraggingPort() const { return drag_source_port_ != nullptr; }

  protected:
    void wheelEvent(QWheelEvent *event) override;

  private:
    TopologyPanel *panel_{nullptr};
    TopoPortItem *drag_source_port_{nullptr};
    QGraphicsLineItem *drag_line_{nullptr};
};

// ============================================================================
// TopologyPanel — main panel widget
// ============================================================================

/** @brief TopologyPanel class. */
class TopologyPanel : public QWidget {
    Q_OBJECT

  public:
    explicit TopologyPanel(QWidget *parent = nullptr);
    ~TopologyPanel() override;

    // Refresh the topology from a .ploy file path
    void LoadFromFile(const QString &ploy_file_path);

    // Clear the current graph
    void Clear();

    // Debug integration: highlight the node that corresponds to a breakpoint hit
    void HighlightDebugNode(const QString &file, int line);
    void ClearDebugHighlights();

    // Execution highlighting by node id with pulse animation
    void HighlightExecutingNode(uint64_t node_id);
    void ClearExecutionHighlight();

    // Called by TopoGraphicsView when a port-to-port drag completes
    void TryCreateEdge(TopoPortItem *source, TopoPortItem *target);

    // Called by edge context menu
    void RemoveEdge(TopoEdgeItem *edge);

    // Called by TopoNodeItem::itemChange to update edges when a node moves
    void RefreshEdgePositions();

  signals:
    // Emitted when a node is double-clicked (navigate to source)
    void NodeDoubleClicked(const QString &filename, int line);

    // Emitted when validation completes
    void ValidationComplete(int errors, int warnings);

    // Emitted when the graph is modified interactively (edge add/remove)
    void GraphModified();

    // Emitted when the .ploy source file is modified by edge sync operations.
    // The editor should highlight the affected line to show the change.
    void FileContentChanged(const QString &file_path, int line);

    // Emitted when a .ploy file is generated and should be opened in editor
    void OpenFileRequested(const QString &file_path);

  private slots:
    void OnRefresh();
    void OnValidate();
    void OnExportDot();
    void OnExportJson();
    void OnExportPng();
    void OnGeneratePloy();
    void OnZoomIn();
    void OnZoomOut();
    void OnZoomFit();
    void OnNodeSelected();
    void OnLayoutChanged(int index);
    void OnFileChanged(const QString &path);
    void OnForceLayoutTick();

  private:
    void SetupUI();
    void SetupToolbar();
    void SetupScene();
    void BuildGraphFromFile(const QString &path);
    void LayoutNodes();
    void UpdateDetailsPanel(uint64_t node_id);

    // .ploy file synchronization helpers
    void SyncEdgeToFile(TopoEdgeItem *edge);
    void RemoveEdgeFromFile(TopoEdgeItem *edge);

    // Force-directed layout helpers
    void StartForceLayout();
    void StopForceLayout();

    // UI components
    QToolBar *toolbar_{nullptr};
    TopoGraphicsView *view_{nullptr};
    QGraphicsScene *scene_{nullptr};
    QSplitter *splitter_{nullptr};
    QTreeWidget *details_tree_{nullptr};
    QPlainTextEdit *diagnostics_output_{nullptr};
    QComboBox *layout_combo_{nullptr};
    QLabel *status_label_{nullptr};
    QPushButton *refresh_btn_{nullptr};
    QPushButton *validate_btn_{nullptr};

    // Current file and live-reload watcher
    QString current_file_;
    QFileSystemWatcher *file_watcher_{nullptr};
    QTimer *reload_debounce_{nullptr};

    // Force-directed layout engine
    QTimer *force_timer_{nullptr};
    int force_iterations_remaining_{0};
    static constexpr int kForceMaxIterations = 300;
    static constexpr double kRepulsionStrength = 50000.0;
    static constexpr double kAttractionStrength = 0.005;
    static constexpr double kIdealEdgeLength = 250.0;
    static constexpr double kDamping = 0.85;
    static constexpr double kMinMovement = 0.5;

    // Scene items indexed by id
    std::unordered_map<uint64_t, TopoNodeItem *> node_items_;
    std::vector<TopoEdgeItem *> edge_items_;

    // Interactive edge id counter (for user-created edges)
    uint64_t next_interactive_edge_id_{100000};

    // Currently execution-highlighted node id (0 = none)
    uint64_t execution_highlight_id_{0};
};

} // namespace polyglot::tools::ui
