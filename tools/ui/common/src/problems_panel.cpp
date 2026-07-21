/**
 * @file     problems_panel.cpp
 * @brief    Implementation of @ref polyglot::tools::ui::ProblemsPanel
 * @ingroup  Tool / polyui / problems
 * @author   Manning Cyrus
 * @date     2026-05-04
 */
#include "tools/ui/common/include/problems_panel.h"

#include <QApplication>
#include <QBrush>
#include <QFileInfo>
#include <QHeaderView>
#include <QIcon>
#include <QMetaObject>
#include <QPointer>
#include <QSet>
#include <QSignalBlocker>
#include <QStringList>
#include <QStyle>

#include <algorithm>

namespace polyglot::tools::ui {

namespace {

// Column layout in the tree: location (line:col) | severity | code | message | source.
constexpr int kColLocation = 0;
constexpr int kColSeverity = 1;
constexpr int kColCode     = 2;
constexpr int kColMessage  = 3;
constexpr int kColSource   = 4;

// Keep the file path in the file-group item, the entry payload in the
// child item.  Item type identifies which is which.
constexpr int kItemTypeFile  = QTreeWidgetItem::UserType + 1;
constexpr int kItemTypeEntry = QTreeWidgetItem::UserType + 2;

constexpr int kRoleFile     = Qt::UserRole + 1;
constexpr int kRoleLine     = Qt::UserRole + 2;
constexpr int kRoleColumn   = Qt::UserRole + 3;
constexpr int kRoleSeverity = Qt::UserRole + 4;

QString SeverityLabel(Severity s) {
  switch (s) {
    case Severity::kError:       return QStringLiteral("error");
    case Severity::kWarning:     return QStringLiteral("warning");
    case Severity::kInformation: return QStringLiteral("info");
    case Severity::kHint:        return QStringLiteral("hint");
  }
  return QStringLiteral("error");
}

QIcon SeverityIcon(Severity s) {
  // Use built-in style icons so the panel works without bundled
  // resources; the host application can override via stylesheet.
  QStyle *style = QApplication::style();
  if (!style) return QIcon();
  switch (s) {
    case Severity::kError:       return style->standardIcon(QStyle::SP_MessageBoxCritical);
    case Severity::kWarning:     return style->standardIcon(QStyle::SP_MessageBoxWarning);
    case Severity::kInformation: return style->standardIcon(QStyle::SP_MessageBoxInformation);
    case Severity::kHint:        return style->standardIcon(QStyle::SP_MessageBoxQuestion);
  }
  return QIcon();
}

QBrush SeverityBrush(Severity s) {
  switch (s) {
    case Severity::kError:       return QBrush(QColor(0xD3, 0x2F, 0x2F));
    case Severity::kWarning:     return QBrush(QColor(0xC0, 0x8B, 0x00));
    case Severity::kInformation: return QBrush(QColor(0x1B, 0x6F, 0xB8));
    case Severity::kHint:        return QBrush(QColor(0x6A, 0x6A, 0x6A));
  }
  return QBrush();
}

}  // namespace

// ============================================================================
// Construction / destruction
// ============================================================================

ProblemsPanel::ProblemsPanel(ProblemsAggregator *aggregator, QWidget *parent)
    : QWidget(parent), aggregator_(aggregator) {
  BuildUi();
  if (aggregator_) {
    // The change callback may run on a worker thread (e.g. an LSP read
    // thread).  Marshal onto the GUI thread before touching widgets.
    QPointer<ProblemsPanel> self(this);
    aggregator_->SetChangeCallback([self]() {
      if (!self) return;
      QMetaObject::invokeMethod(self.data(), "Refresh", Qt::QueuedConnection);
    });
  }
  Refresh();
}

ProblemsPanel::~ProblemsPanel() {
  if (aggregator_) {
    aggregator_->SetChangeCallback({});
  }
}

// ============================================================================
// UI construction
// ============================================================================

void ProblemsPanel::BuildUi() {
  auto *outer = new QVBoxLayout(this);
  outer->setContentsMargins(4, 4, 4, 4);
  outer->setSpacing(4);

  // -------------------------------------------------------------------- Filters
  auto *filter_row = new QHBoxLayout();
  filter_row->setSpacing(4);

  auto make_severity_btn = [this](const QString &label, const QIcon &icon) {
    auto *btn = new QToolButton(this);
    btn->setText(label);
    btn->setIcon(icon);
    btn->setCheckable(true);
    btn->setChecked(true);
    btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(btn, &QToolButton::toggled, this, &ProblemsPanel::OnSeverityToggled);
    return btn;
  };
  btn_errors_   = make_severity_btn(tr("Errors"),   SeverityIcon(Severity::kError));
  btn_warnings_ = make_severity_btn(tr("Warnings"), SeverityIcon(Severity::kWarning));
  btn_info_     = make_severity_btn(tr("Info"),     SeverityIcon(Severity::kInformation));
  btn_hints_    = make_severity_btn(tr("Hints"),    SeverityIcon(Severity::kHint));
  filter_row->addWidget(btn_errors_);
  filter_row->addWidget(btn_warnings_);
  filter_row->addWidget(btn_info_);
  filter_row->addWidget(btn_hints_);

  file_edit_ = new QLineEdit(this);
  file_edit_->setPlaceholderText(tr("Filter by file (substring)"));
  file_edit_->setClearButtonEnabled(true);
  connect(file_edit_, &QLineEdit::textChanged, this, &ProblemsPanel::OnFileFilterChanged);
  filter_row->addWidget(file_edit_, 1);

  source_combo_ = new QComboBox(this);
  source_combo_->addItem(tr("All sources"), QString());
  connect(source_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this,
          &ProblemsPanel::OnSourceFilterChanged);
  filter_row->addWidget(source_combo_);

  regex_edit_ = new QLineEdit(this);
  regex_edit_->setPlaceholderText(tr("Message regex"));
  regex_edit_->setClearButtonEnabled(true);
  connect(regex_edit_, &QLineEdit::textChanged, this, &ProblemsPanel::OnRegexFilterChanged);
  filter_row->addWidget(regex_edit_, 1);

  btn_clear_ = new QToolButton(this);
  btn_clear_->setText(tr("Clear"));
  btn_clear_->setToolTip(tr("Reset all filters"));
  connect(btn_clear_, &QToolButton::clicked, this, &ProblemsPanel::OnClearFiltersClicked);
  filter_row->addWidget(btn_clear_);

  outer->addLayout(filter_row);

  // -------------------------------------------------------------------- Tree
  tree_ = new QTreeWidget(this);
  tree_->setColumnCount(5);
  tree_->setHeaderLabels({tr("Location"), tr("Severity"), tr("Code"), tr("Message"), tr("Source")});
  tree_->setRootIsDecorated(true);
  tree_->setAllColumnsShowFocus(true);
  tree_->setUniformRowHeights(true);
  tree_->setAlternatingRowColors(true);
  tree_->setSortingEnabled(false);
  tree_->setSelectionBehavior(QAbstractItemView::SelectRows);
  if (auto *hh = tree_->header()) {
    hh->setSectionResizeMode(kColMessage, QHeaderView::Stretch);
    hh->setSectionResizeMode(kColLocation, QHeaderView::ResizeToContents);
    hh->setSectionResizeMode(kColSeverity, QHeaderView::ResizeToContents);
    hh->setSectionResizeMode(kColCode, QHeaderView::ResizeToContents);
    hh->setSectionResizeMode(kColSource, QHeaderView::ResizeToContents);
  }
  connect(tree_, &QTreeWidget::itemActivated, this, &ProblemsPanel::OnItemActivated);
  connect(tree_, &QTreeWidget::itemDoubleClicked, this, &ProblemsPanel::OnItemActivated);
  outer->addWidget(tree_, 1);

  // -------------------------------------------------------------------- Summary
  summary_label_ = new QLabel(this);
  summary_label_->setText(tr("0 problems"));
  outer->addWidget(summary_label_);
}

// ============================================================================
// Slot handlers
// ============================================================================

void ProblemsPanel::OnSeverityToggled() { RebuildTree(); }
void ProblemsPanel::OnFileFilterChanged(const QString &) { RebuildTree(); }
void ProblemsPanel::OnSourceFilterChanged(int) { RebuildTree(); }
void ProblemsPanel::OnRegexFilterChanged(const QString &) { RebuildTree(); }

void ProblemsPanel::OnClearFiltersClicked() {
  // Block signals while we mutate state to avoid triggering N rebuilds.
  const QSignalBlocker b1(btn_errors_), b2(btn_warnings_), b3(btn_info_), b4(btn_hints_);
  const QSignalBlocker b5(file_edit_), b6(source_combo_), b7(regex_edit_);
  btn_errors_->setChecked(true);
  btn_warnings_->setChecked(true);
  btn_info_->setChecked(true);
  btn_hints_->setChecked(true);
  file_edit_->clear();
  if (source_combo_->count() > 0) source_combo_->setCurrentIndex(0);
  regex_edit_->clear();
  RebuildTree();
}

void ProblemsPanel::OnItemActivated(QTreeWidgetItem *item, int /*column*/) {
  if (!item) return;
  if (item->type() != kItemTypeEntry) {
    item->setExpanded(!item->isExpanded());
    return;
  }
  const QString file = item->data(kColLocation, kRoleFile).toString();
  const int line = item->data(kColLocation, kRoleLine).toInt();
  const int column = item->data(kColLocation, kRoleColumn).toInt();
  if (!file.isEmpty()) {
    emit OpenFileRequested(file, line, column);
  }
}

// ============================================================================
// Refresh / rebuild
// ============================================================================

void ProblemsPanel::Refresh() {
  RebuildSourceComboKeepingSelection();
  RebuildTree();
}

ProblemFilter ProblemsPanel::CurrentFilter() const {
  ProblemFilter f;
  std::uint32_t mask = 0;
  if (btn_errors_   && btn_errors_->isChecked())   mask |= static_cast<std::uint32_t>(SeverityMask::kError);
  if (btn_warnings_ && btn_warnings_->isChecked()) mask |= static_cast<std::uint32_t>(SeverityMask::kWarning);
  if (btn_info_     && btn_info_->isChecked())     mask |= static_cast<std::uint32_t>(SeverityMask::kInformation);
  if (btn_hints_    && btn_hints_->isChecked())    mask |= static_cast<std::uint32_t>(SeverityMask::kHint);
  f.severity_mask = mask;
  if (file_edit_)  f.file_substring   = file_edit_->text().toStdString();
  if (regex_edit_) f.message_regex    = regex_edit_->text().toStdString();
  if (source_combo_) {
    f.source_substring = source_combo_->currentData().toString().toStdString();
  }
  return f;
}

void ProblemsPanel::RebuildSourceComboKeepingSelection() {
  if (!aggregator_ || !source_combo_) return;
  const QString previous = source_combo_->currentData().toString();
  const QSignalBlocker block(source_combo_);
  source_combo_->clear();
  source_combo_->addItem(tr("All sources"), QString());
  for (const auto &src : aggregator_->KnownSources()) {
    const QString q = QString::fromStdString(src);
    source_combo_->addItem(q, q);
  }
  // Restore the previous selection if it still exists.
  int restore = 0;
  for (int i = 0; i < source_combo_->count(); ++i) {
    if (source_combo_->itemData(i).toString() == previous) {
      restore = i;
      break;
    }
  }
  source_combo_->setCurrentIndex(restore);
}

void ProblemsPanel::RebuildTree() {
  if (!tree_) return;
  tree_->setUpdatesEnabled(false);
  tree_->clear();

  ProblemsAggregator::Counts unfiltered_counts;
  std::vector<ProblemEntry> snapshot;
  if (aggregator_) {
    unfiltered_counts = aggregator_->CountAll();
    snapshot = aggregator_->Snapshot(CurrentFilter());
  }

  // Group by file in iteration order (snapshot is already sorted by file).
  QTreeWidgetItem *current_group = nullptr;
  QString current_file;
  int current_group_count = 0;

  auto finish_group = [&]() {
    if (current_group) {
      current_group->setText(kColMessage,
                             tr("%1 problem(s)").arg(current_group_count));
    }
  };

  for (const auto &e : snapshot) {
    const QString file_q = QString::fromStdString(e.file);
    if (!current_group || file_q != current_file) {
      finish_group();
      current_file = file_q;
      current_group_count = 0;
      current_group = new QTreeWidgetItem(tree_, kItemTypeFile);
      const QFileInfo fi(file_q);
      const QString display = fi.fileName().isEmpty() ? file_q : fi.fileName();
      current_group->setText(kColLocation, display);
      current_group->setToolTip(kColLocation, file_q);
      current_group->setFirstColumnSpanned(false);
      current_group->setExpanded(true);
      current_group->setData(kColLocation, kRoleFile, file_q);
    }

    auto *child = new QTreeWidgetItem(current_group, kItemTypeEntry);
    const QString loc =
        (e.line > 0)
            ? (e.column > 0 ? QStringLiteral("%1:%2").arg(e.line).arg(e.column)
                            : QString::number(static_cast<qulonglong>(e.line)))
            : QStringLiteral("?");
    child->setText(kColLocation, loc);
    child->setIcon(kColLocation, SeverityIcon(e.severity));
    child->setText(kColSeverity, SeverityLabel(e.severity));
    child->setText(kColCode, QString::fromStdString(e.code));
    child->setText(kColMessage, QString::fromStdString(e.message));
    child->setText(kColSource, QString::fromStdString(e.source));
    const QBrush brush = SeverityBrush(e.severity);
    if (brush.style() != Qt::NoBrush) {
      child->setForeground(kColSeverity, brush);
    }
    if (!e.suggestion.empty()) {
      child->setToolTip(kColMessage,
                        QString::fromStdString(e.message) + QStringLiteral("\n") +
                            tr("Suggestion: ") + QString::fromStdString(e.suggestion));
    }
    child->setData(kColLocation, kRoleFile, file_q);
    child->setData(kColLocation, kRoleLine, static_cast<int>(e.line));
    child->setData(kColLocation, kRoleColumn, static_cast<int>(e.column));
    child->setData(kColLocation, kRoleSeverity, static_cast<int>(e.severity));
    ++current_group_count;
  }
  finish_group();

  if (summary_label_) {
    summary_label_->setText(
        tr("Showing %1 of %2 (E:%3 W:%4 I:%5 H:%6)")
            .arg(static_cast<qulonglong>(snapshot.size()))
            .arg(static_cast<qulonglong>(unfiltered_counts.Total()))
            .arg(static_cast<qulonglong>(unfiltered_counts.errors))
            .arg(static_cast<qulonglong>(unfiltered_counts.warnings))
            .arg(static_cast<qulonglong>(unfiltered_counts.information))
            .arg(static_cast<qulonglong>(unfiltered_counts.hints)));
  }

  tree_->setUpdatesEnabled(true);
  emit AggregatorChanged();
}

}  // namespace polyglot::tools::ui
