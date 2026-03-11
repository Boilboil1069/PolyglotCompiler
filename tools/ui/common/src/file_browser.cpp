// file_browser.cpp — Project file browser implementation.
//
// Uses QFileSystemModel and QTreeView to display the project directory tree
// with filtering support.

#include "tools/ui/common/include/file_browser.h"

#include <QDir>

namespace polyglot::tools::ui {

// ============================================================================
// Construction
// ============================================================================

FileBrowser::FileBrowser(QWidget *parent) : QWidget(parent) {
    SetupUi();
}

FileBrowser::~FileBrowser() = default;

// ============================================================================
// UI Setup
// ============================================================================

void FileBrowser::SetupUi() {
    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(2);

    // Title
    title_label_ = new QLabel("  EXPLORER", this);
    title_label_->setStyleSheet(
        "QLabel { color: #bbbbbb; font-size: 11px; font-weight: bold; "
        "padding: 4px 0px; }");
    layout_->addWidget(title_label_);

    // Filter input
    filter_edit_ = new QLineEdit(this);
    filter_edit_->setPlaceholderText("Filter files...");
    filter_edit_->setClearButtonEnabled(true);
    filter_edit_->setStyleSheet(
        "QLineEdit { background: #3c3c3c; color: #cccccc; border: 1px solid #555; "
        "border-radius: 3px; padding: 3px 6px; font-size: 12px; }");
    layout_->addWidget(filter_edit_);

    connect(filter_edit_, &QLineEdit::textChanged,
            this, &FileBrowser::OnFilterTextChanged);

    // File tree
    model_ = new QFileSystemModel(this);
    model_->setReadOnly(true);
    model_->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);

    // Filter out build artifacts by default
    QStringList name_filters;
    name_filters << "*.cpp" << "*.h" << "*.hpp" << "*.c" << "*.cc"
                 << "*.py" << "*.rs" << "*.java" << "*.cs"
                 << "*.ploy" << "*.cmake" << "CMakeLists.txt"
                 << "*.json" << "*.yml" << "*.yaml" << "*.md"
                 << "*.txt" << "*.sh" << "*.ps1" << "*.bat"
                 << "*.proto" << "*.inl" << "*.ipp";
    model_->setNameFilters(name_filters);
    model_->setNameFilterDisables(false);

    tree_view_ = new QTreeView(this);
    tree_view_->setModel(model_);
    tree_view_->setHeaderHidden(true);
    tree_view_->setAnimated(true);
    tree_view_->setIndentation(16);
    tree_view_->setSortingEnabled(true);
    tree_view_->sortByColumn(0, Qt::AscendingOrder);

    // Hide size, type, date columns — only show name
    tree_view_->hideColumn(1);
    tree_view_->hideColumn(2);
    tree_view_->hideColumn(3);

    tree_view_->setStyleSheet(
        "QTreeView { background: #252526; color: #cccccc; border: none; "
        "font-size: 13px; }"
        "QTreeView::item { padding: 2px 0px; }"
        "QTreeView::item:hover { background: #2a2d2e; }"
        "QTreeView::item:selected { background: #37373d; color: #ffffff; }");

    layout_->addWidget(tree_view_);

    connect(tree_view_, &QTreeView::doubleClicked,
            this, &FileBrowser::OnItemDoubleClicked);
}

// ============================================================================
// Root Path
// ============================================================================

void FileBrowser::SetRootPath(const QString &path) {
    QModelIndex index = model_->setRootPath(path);
    tree_view_->setRootIndex(index);
    title_label_->setText("  " + QDir(path).dirName().toUpper());
}

QString FileBrowser::RootPath() const {
    return model_->rootPath();
}

// ============================================================================
// Slots
// ============================================================================

void FileBrowser::OnItemDoubleClicked(const QModelIndex &index) {
    if (!model_->isDir(index)) {
        QString path = model_->filePath(index);
        emit FileActivated(path);
    }
}

void FileBrowser::OnFilterTextChanged(const QString &text) {
    if (text.isEmpty()) {
        // Restore default filters
        QStringList name_filters;
        name_filters << "*.cpp" << "*.h" << "*.hpp" << "*.c" << "*.cc"
                     << "*.py" << "*.rs" << "*.java" << "*.cs"
                     << "*.ploy" << "*.cmake" << "CMakeLists.txt"
                     << "*.json" << "*.yml" << "*.yaml" << "*.md"
                     << "*.txt" << "*.sh" << "*.ps1" << "*.bat";
        model_->setNameFilters(name_filters);
    } else {
        model_->setNameFilters(QStringList() << ("*" + text + "*"));
    }
}

} // namespace polyglot::tools::ui
