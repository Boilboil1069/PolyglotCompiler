// topology_panel.h — Topology visualization panel for the PolyglotCompiler IDE.
//
// Provides an interactive visual representation of function-level I/O
// topology for .ploy files, similar to Simulink's block diagram view.
// Features: node rendering with ports, edge connections, type validation
// overlay, zoom/pan, and export capabilities.

#pragma once

#include <QAction>
#include <QComboBox>
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace polyglot::tools::ui {

// Forward declaration — the panel uses a lightweight subset of topology_graph.h
// data structures, converted from the core library at refresh time.

// ============================================================================
// TopoNodeItem — a graphical node in the topology scene
// ============================================================================

class TopoNodeItem : public QGraphicsRectItem {
  public:
    TopoNodeItem(uint64_t node_id, const QString &name,
                 const QString &language, const QString &kind,
                 QGraphicsItem *parent = nullptr);

    uint64_t NodeId() const { return node_id_; }

    // Add an input or output port; returns the port's scene-center position
    QPointF AddInputPort(uint64_t port_id, const QString &name,
                         const QString &type_name);
    QPointF AddOutputPort(uint64_t port_id, const QString &name,
                          const QString &type_name);

    // Get port center position (for drawing edges)
    QPointF InputPortPos(uint64_t port_id) const;
    QPointF OutputPortPos(uint64_t port_id) const;

    // Highlight the node (on selection or validation error)
    void SetHighlight(bool error);

  protected:
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;

  private:
    void LayoutPorts();

    uint64_t node_id_;
    QString name_;
    QString language_;
    QString kind_;

    struct PortVisual {
        uint64_t id;
        QString name;
        QString type_name;
        QGraphicsEllipseItem *dot{nullptr};
        QGraphicsTextItem *label{nullptr};
    };
    std::vector<PortVisual> input_ports_;
    std::vector<PortVisual> output_ports_;
    bool highlight_error_{false};
};

// ============================================================================
// TopoEdgeItem — a graphical connection between two ports
// ============================================================================

class TopoEdgeItem : public QGraphicsPathItem {
  public:
    TopoEdgeItem(uint64_t edge_id,
                 const QPointF &start, const QPointF &end,
                 const QString &status,
                 QGraphicsItem *parent = nullptr);

    uint64_t EdgeId() const { return edge_id_; }
    void SetStatus(const QString &status);

  private:
    void RebuildPath(const QPointF &start, const QPointF &end);
    uint64_t edge_id_;
    QString status_;
};

// ============================================================================
// TopologyPanel — main panel widget
// ============================================================================

class TopologyPanel : public QWidget {
    Q_OBJECT

  public:
    explicit TopologyPanel(QWidget *parent = nullptr);
    ~TopologyPanel() override;

    // Refresh the topology from a .ploy file path
    void LoadFromFile(const QString &ploy_file_path);

    // Clear the current graph
    void Clear();

  signals:
    // Emitted when a node is double-clicked (navigate to source)
    void NodeDoubleClicked(const QString &filename, int line);

    // Emitted when validation completes
    void ValidationComplete(int errors, int warnings);

  private slots:
    void OnRefresh();
    void OnValidate();
    void OnExportDot();
    void OnExportJson();
    void OnExportPng();
    void OnZoomIn();
    void OnZoomOut();
    void OnZoomFit();
    void OnNodeSelected();
    void OnLayoutChanged(int index);

  private:
    void SetupUI();
    void SetupToolbar();
    void SetupScene();
    void BuildGraphFromFile(const QString &path);
    void LayoutNodes();
    void UpdateDetailsPanel(uint64_t node_id);

    // UI components
    QToolBar *toolbar_{nullptr};
    QGraphicsView *view_{nullptr};
    QGraphicsScene *scene_{nullptr};
    QSplitter *splitter_{nullptr};
    QTreeWidget *details_tree_{nullptr};
    QPlainTextEdit *diagnostics_output_{nullptr};
    QComboBox *layout_combo_{nullptr};
    QLabel *status_label_{nullptr};
    QPushButton *refresh_btn_{nullptr};
    QPushButton *validate_btn_{nullptr};

    // Current file
    QString current_file_;

    // Scene items indexed by id
    std::unordered_map<uint64_t, TopoNodeItem *> node_items_;
    std::vector<TopoEdgeItem *> edge_items_;
};

} // namespace polyglot::tools::ui
