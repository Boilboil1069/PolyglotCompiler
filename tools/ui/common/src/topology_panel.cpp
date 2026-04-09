// topology_panel.cpp — Topology visualization panel implementation.

#include "tools/ui/common/include/topology_panel.h"

#include <QFileDialog>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsTextItem>
#include <QHeaderView>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QProcess>
#include <QScrollBar>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

#include "frontends/common/include/diagnostics.h"
#include "frontends/ploy/include/ploy_lexer.h"
#include "frontends/ploy/include/ploy_parser.h"
#include "frontends/ploy/include/ploy_sema.h"
#include "tools/polytopo/include/topology_analyzer.h"
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
// TopoNodeItem
// ============================================================================

static constexpr qreal kNodeWidth = 220.0;
static constexpr qreal kNodeHeaderHeight = 30.0;
static constexpr qreal kPortHeight = 20.0;
static constexpr qreal kPortDotRadius = 5.0;
static constexpr qreal kPortMargin = 12.0;

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
    PortVisual pv;
    pv.id = port_id;
    pv.name = name;
    pv.type_name = type_name;

    // Create port dot
    pv.dot = new QGraphicsEllipseItem(
        -kPortDotRadius, -kPortDotRadius,
        kPortDotRadius * 2, kPortDotRadius * 2, this);
    pv.dot->setBrush(QBrush(QColor("#2196F3")));
    pv.dot->setPen(QPen(Qt::white, 1));

    // Create label
    pv.label = new QGraphicsTextItem(name + ": " + type_name, this);
    pv.label->setDefaultTextColor(Qt::white);
    auto font = pv.label->font();
    font.setPointSize(8);
    pv.label->setFont(font);

    input_ports_.push_back(pv);
    LayoutPorts();

    return pv.dot->scenePos();
}

QPointF TopoNodeItem::AddOutputPort(uint64_t port_id, const QString &name,
                                     const QString &type_name) {
    PortVisual pv;
    pv.id = port_id;
    pv.name = name;
    pv.type_name = type_name;

    pv.dot = new QGraphicsEllipseItem(
        -kPortDotRadius, -kPortDotRadius,
        kPortDotRadius * 2, kPortDotRadius * 2, this);
    pv.dot->setBrush(QBrush(QColor("#4CAF50")));
    pv.dot->setPen(QPen(Qt::white, 1));

    pv.label = new QGraphicsTextItem(name + ": " + type_name, this);
    pv.label->setDefaultTextColor(Qt::white);
    auto font = pv.label->font();
    font.setPointSize(8);
    pv.label->setFont(font);

    output_ports_.push_back(pv);
    LayoutPorts();

    return pv.dot->scenePos();
}

QPointF TopoNodeItem::InputPortPos(uint64_t port_id) const {
    for (const auto &pv : input_ports_) {
        if (pv.id == port_id && pv.dot) {
            return mapToScene(pv.dot->pos());
        }
    }
    return scenePos();
}

QPointF TopoNodeItem::OutputPortPos(uint64_t port_id) const {
    for (const auto &pv : output_ports_) {
        if (pv.id == port_id && pv.dot) {
            return mapToScene(pv.dot->pos());
        }
    }
    return scenePos() + QPointF(kNodeWidth, 0);
}

void TopoNodeItem::SetHighlight(bool error) {
    highlight_error_ = error;
    update();
}

void TopoNodeItem::LayoutPorts() {
    size_t total_ports = input_ports_.size() + output_ports_.size();
    qreal total_height = kNodeHeaderHeight +
                         static_cast<qreal>(std::max(input_ports_.size(),
                                                      output_ports_.size())) * kPortHeight +
                         kPortMargin;
    setRect(0, 0, kNodeWidth, total_height);

    // Position input ports on the left
    for (size_t i = 0; i < input_ports_.size(); ++i) {
        qreal y = kNodeHeaderHeight + static_cast<qreal>(i) * kPortHeight + kPortHeight / 2;
        input_ports_[i].dot->setPos(0, y);
        input_ports_[i].label->setPos(kPortDotRadius + 4, y - 10);
    }

    // Position output ports on the right
    for (size_t i = 0; i < output_ports_.size(); ++i) {
        qreal y = kNodeHeaderHeight + static_cast<qreal>(i) * kPortHeight + kPortHeight / 2;
        output_ports_[i].dot->setPos(kNodeWidth, y);
        output_ports_[i].label->setPos(kNodeWidth - output_ports_[i].label->boundingRect().width() - kPortDotRadius - 4, y - 10);
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

    // Border
    QPen border_pen(highlight_error_ ? QColor("#F44336") : base_color, 2);
    if (isSelected()) {
        border_pen.setColor(Qt::white);
        border_pen.setWidth(3);
    }
    painter->setPen(border_pen);
    painter->drawRoundedRect(r, 6, 6);

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
}

// ============================================================================
// TopoEdgeItem
// ============================================================================

TopoEdgeItem::TopoEdgeItem(uint64_t edge_id,
                           const QPointF &start, const QPointF &end,
                           const QString &status,
                           QGraphicsItem *parent)
    : QGraphicsPathItem(parent), edge_id_(edge_id), status_(status) {
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

void TopoEdgeItem::RebuildPath(const QPointF &start, const QPointF &end) {
    QPainterPath path;
    path.moveTo(start);

    // Cubic bezier for smooth connection
    qreal dx = std::abs(end.x() - start.x()) * 0.5;
    QPointF c1(start.x() + dx, start.y());
    QPointF c2(end.x() - dx, end.y());
    path.cubicTo(c1, c2, end);

    setPath(path);

    // Arrow head at the end
    // (Arrow drawn via paint override would be more accurate, but this
    //  is sufficient for a first implementation)
}

// ============================================================================
// TopologyPanel
// ============================================================================

TopologyPanel::TopologyPanel(QWidget *parent)
    : QWidget(parent) {
    SetupUI();
}

TopologyPanel::~TopologyPanel() = default;

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
    layout_combo_->addItem("Top-Down");
    layout_combo_->addItem("Left-Right");
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
}

void TopologyPanel::SetupScene() {
    scene_ = new QGraphicsScene(this);
    scene_->setBackgroundBrush(QColor("#1E1E1E"));

    view_ = new QGraphicsView(scene_, this);
    view_->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    view_->setDragMode(QGraphicsView::ScrollHandDrag);
    view_->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    view_->setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
}

void TopologyPanel::LoadFromFile(const QString &ploy_file_path) {
    current_file_ = ploy_file_path;
    BuildGraphFromFile(ploy_file_path);
}

void TopologyPanel::Clear() {
    scene_->clear();
    node_items_.clear();
    edge_items_.clear();
    details_tree_->clear();
    diagnostics_output_->clear();
    status_label_->setText("No topology loaded");
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

    // Layout nodes
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
        scene_->addItem(edge_item);
        edge_items_.push_back(edge_item);
    }

    status_label_->setText(QString("Topology: %1 nodes, %2 edges — %3")
                               .arg(graph.NodeCount())
                               .arg(graph.EdgeCount())
                               .arg(path));
}

void TopologyPanel::LayoutNodes() {
    // Simple top-down grid layout
    bool horizontal = (layout_combo_ && layout_combo_->currentIndex() == 1);
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
}

void TopologyPanel::UpdateDetailsPanel(uint64_t node_id) {
    details_tree_->clear();
    auto it = node_items_.find(node_id);
    if (it == node_items_.end()) return;

    auto *item = it->second;
    auto *root = new QTreeWidgetItem(details_tree_, {"Node", ""});
    new QTreeWidgetItem(root, {"ID", QString::number(static_cast<qulonglong>(node_id))});
    root->setExpanded(true);
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

    // Re-analyze and export
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
    // Re-create edges since node positions changed
    if (!current_file_.isEmpty()) {
        BuildGraphFromFile(current_file_);
    }
}

} // namespace polyglot::tools::ui
