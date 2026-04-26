/**
 * @file     file_browser.cpp
 * @brief    Project file browser implementation
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include "tools/ui/common/include/file_browser.h"
#include "tools/ui/common/include/theme_manager.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QProcess>
#include <QUrl>

namespace polyglot::tools::ui {

// ============================================================================
// Construction
// ============================================================================

FileBrowser::FileBrowser(QWidget *parent) : QWidget(parent) {
    SetupUi();
    SetupContextMenu();
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
    {
        const auto &tc = ThemeManager::Instance().Active();
        title_label_->setStyleSheet(
            QString("QLabel { color: %1; font-size: 11px; font-weight: bold; "
                    "padding: 4px 0px; }").arg(tc.text_secondary.name()));
    }
    layout_->addWidget(title_label_);

    // Filter input
    filter_edit_ = new QLineEdit(this);
    filter_edit_->setPlaceholderText("Filter files...");
    filter_edit_->setClearButtonEnabled(true);
    filter_edit_->setStyleSheet(ThemeManager::Instance().LineEditStylesheet());
    layout_->addWidget(filter_edit_);

    connect(filter_edit_, &QLineEdit::textChanged,
            this, &FileBrowser::OnFilterTextChanged);

    // File tree
    model_ = new QFileSystemModel(this);
    model_->setReadOnly(false);   // Allow rename/delete operations
    model_->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);

    // Filter out build artifacts by default
    QStringList name_filters;
    name_filters << "*.cpp" << "*.h" << "*.hpp" << "*.c" << "*.cc"
                 << "*.py" << "*.rs" << "*.java" << "*.cs" << "*.js" << "*.mjs" << "*.cjs" << "*.rb" << "*.go"
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

    // Hide size, type, date columns â€?only show name
    tree_view_->hideColumn(1);
    tree_view_->hideColumn(2);
    tree_view_->hideColumn(3);

    {
        const auto &tc = ThemeManager::Instance().Active();
        tree_view_->setStyleSheet(
            QString("QTreeView { background: %1; color: %2; border: none; "
                    "font-size: 13px; }"
                    "QTreeView::item { padding: 2px 0px; }"
                    "QTreeView::item:hover { background: %3; }"
                    "QTreeView::item:selected { background: %4; color: %5; }")
                .arg(tc.background.name(), tc.text.name(),
                     tc.surface_alt.name(), tc.selection.name(),
                     tc.selection_text.name()));
    }

    // Enable custom context menu
    tree_view_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tree_view_, &QTreeView::customContextMenuRequested,
            this, &FileBrowser::OnContextMenuRequested);

    layout_->addWidget(tree_view_);

    connect(tree_view_, &QTreeView::doubleClicked,
            this, &FileBrowser::OnItemDoubleClicked);
}

// ============================================================================
// Context Menu Setup
// ============================================================================

void FileBrowser::SetupContextMenu() {
    context_menu_ = new QMenu(this);

    action_open_ = context_menu_->addAction("Open");
    action_open_->setShortcut(QKeySequence(Qt::Key_Return));
    connect(action_open_, &QAction::triggered, this, [this]() {
        OnContextMenuRequested(tree_view_->mapFromGlobal(QCursor::pos()));
    });

    context_menu_->addSeparator();

    action_new_file_ = context_menu_->addAction("New File...");
    action_new_folder_ = context_menu_->addAction("New Folder...");

    context_menu_->addSeparator();

    action_rename_ = context_menu_->addAction("Rename...");
    action_rename_->setShortcut(QKeySequence(Qt::Key_F2));

    action_delete_ = context_menu_->addAction("Delete");
    action_delete_->setShortcut(QKeySequence::Delete);

    context_menu_->addSeparator();

    action_copy_path_ = context_menu_->addAction("Copy Path");
    action_copy_relative_path_ = context_menu_->addAction("Copy Relative Path");

    context_menu_->addSeparator();

    action_reveal_explorer_ = context_menu_->addAction("Reveal in File Explorer");
    action_open_terminal_ = context_menu_->addAction("Open in Terminal");

    context_menu_->addSeparator();

    action_generate_topology_ = context_menu_->addAction("Generate Topology Graph");

    context_menu_->addSeparator();

    action_new_from_template_ = context_menu_->addAction("New From Template...");
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
// Theme
// ============================================================================

void FileBrowser::ApplyTheme() {
    const auto &tc = ThemeManager::Instance().Active();

    title_label_->setStyleSheet(
        QString("QLabel { color: %1; font-size: 11px; font-weight: bold; "
                "padding: 4px 0px; }").arg(tc.text_secondary.name()));

    filter_edit_->setStyleSheet(ThemeManager::Instance().LineEditStylesheet());

    tree_view_->setStyleSheet(
        QString("QTreeView { background: %1; color: %2; border: none; "
                "font-size: 13px; }"
                "QTreeView::item { padding: 2px 0px; }"
                "QTreeView::item:hover { background: %3; }"
                "QTreeView::item:selected { background: %4; color: %5; }")
            .arg(tc.background.name(), tc.text.name(),
                 tc.surface_alt.name(), tc.selection.name(),
                 tc.selection_text.name()));
}

// ============================================================================
// Helpers
// ============================================================================

QString FileBrowser::DirectoryForIndex(const QModelIndex &index) const {
    if (!index.isValid()) {
        return model_->rootPath();
    }
    if (model_->isDir(index)) {
        return model_->filePath(index);
    }
    return model_->filePath(index.parent());
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
                     << "*.py" << "*.rs" << "*.java" << "*.cs" << "*.js" << "*.mjs" << "*.cjs" << "*.rb" << "*.go"
                     << "*.ploy" << "*.cmake" << "CMakeLists.txt"
                     << "*.json" << "*.yml" << "*.yaml" << "*.md"
                     << "*.txt" << "*.sh" << "*.ps1" << "*.bat";
        model_->setNameFilters(name_filters);
    } else {
        model_->setNameFilters(QStringList() << ("*" + text + "*"));
    }
}

void FileBrowser::OnContextMenuRequested(const QPoint &pos) {
    QModelIndex index = tree_view_->indexAt(pos);
    bool has_selection = index.isValid();
    bool is_dir = has_selection && model_->isDir(index);
    bool is_file = has_selection && !is_dir;
    bool is_ploy = is_file && model_->filePath(index).endsWith(".ploy",
                                                                Qt::CaseInsensitive);

    // Show/hide actions based on selection context
    action_open_->setVisible(is_file);
    action_rename_->setEnabled(has_selection);
    action_delete_->setEnabled(has_selection);
    action_copy_path_->setEnabled(has_selection);
    action_copy_relative_path_->setEnabled(has_selection);
    action_reveal_explorer_->setEnabled(has_selection);
    action_generate_topology_->setVisible(is_ploy);
    action_new_from_template_->setVisible(true);

    // Disconnect previous connections and reconnect with current index
    // to avoid stale captures.
    action_open_->disconnect();
    action_new_file_->disconnect();
    action_new_folder_->disconnect();
    action_rename_->disconnect();
    action_delete_->disconnect();
    action_copy_path_->disconnect();
    action_copy_relative_path_->disconnect();
    action_reveal_explorer_->disconnect();
    action_open_terminal_->disconnect();
    action_generate_topology_->disconnect();
    action_new_from_template_->disconnect();

    connect(action_open_, &QAction::triggered, this, [this, index]() {
        if (index.isValid() && !model_->isDir(index)) {
            emit OpenFileRequested(model_->filePath(index));
        }
    });
    connect(action_new_file_, &QAction::triggered, this, [this, index]() {
        ContextNewFile(index);
    });
    connect(action_new_folder_, &QAction::triggered, this, [this, index]() {
        ContextNewFolder(index);
    });
    connect(action_rename_, &QAction::triggered, this, [this, index]() {
        ContextRename(index);
    });
    connect(action_delete_, &QAction::triggered, this, [this, index]() {
        ContextDelete(index);
    });
    connect(action_copy_path_, &QAction::triggered, this, [this, index]() {
        ContextCopyPath(index);
    });
    connect(action_copy_relative_path_, &QAction::triggered, this, [this, index]() {
        ContextCopyRelativePath(index);
    });
    connect(action_reveal_explorer_, &QAction::triggered, this, [this, index]() {
        ContextRevealInExplorer(index);
    });
    connect(action_open_terminal_, &QAction::triggered, this, [this, index]() {
        ContextOpenTerminal(index);
    });
    connect(action_generate_topology_, &QAction::triggered, this, [this, index]() {
        ContextGenerateTopology(index);
    });
    connect(action_new_from_template_, &QAction::triggered, this, [this, index]() {
        emit NewFromTemplateRequested(DirectoryForIndex(index));
    });

    context_menu_->exec(tree_view_->viewport()->mapToGlobal(pos));
}

// ============================================================================
// Context Menu Actions
// ============================================================================

void FileBrowser::ContextNewFile(const QModelIndex &index) {
    QString dir = DirectoryForIndex(index);
    bool ok = false;
    QString name = QInputDialog::getText(
        this, "New File", "File name:", QLineEdit::Normal, "", &ok);
    if (!ok || name.isEmpty()) return;

    QString full_path = dir + "/" + name;
    QFile file(full_path);
    if (file.exists()) {
        QMessageBox::warning(this, "New File",
                             "A file with that name already exists.");
        return;
    }
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, "New File",
                              "Failed to create file: " + file.errorString());
        return;
    }
    file.close();

    emit NewFileRequested(full_path);
    emit OpenFileRequested(full_path);
}

void FileBrowser::ContextNewFolder(const QModelIndex &index) {
    QString dir = DirectoryForIndex(index);
    bool ok = false;
    QString name = QInputDialog::getText(
        this, "New Folder", "Folder name:", QLineEdit::Normal, "", &ok);
    if (!ok || name.isEmpty()) return;

    QDir parent(dir);
    if (parent.exists(name)) {
        QMessageBox::warning(this, "New Folder",
                             "A folder with that name already exists.");
        return;
    }
    if (!parent.mkdir(name)) {
        QMessageBox::critical(this, "New Folder",
                              "Failed to create folder.");
        return;
    }

    emit NewFolderRequested(dir + "/" + name);
}

void FileBrowser::ContextRename(const QModelIndex &index) {
    if (!index.isValid()) return;

    QString old_path = model_->filePath(index);
    QString old_name = model_->fileName(index);
    bool ok = false;
    QString new_name = QInputDialog::getText(
        this, "Rename", "New name:", QLineEdit::Normal, old_name, &ok);
    if (!ok || new_name.isEmpty() || new_name == old_name) return;

    QFileInfo fi(old_path);
    QString new_path = fi.absolutePath() + "/" + new_name;

    if (QFileInfo::exists(new_path)) {
        QMessageBox::warning(this, "Rename",
                             "An item with that name already exists.");
        return;
    }

    if (!QFile::rename(old_path, new_path)) {
        QMessageBox::critical(this, "Rename", "Failed to rename item.");
        return;
    }

    emit ItemRenamed(old_path, new_path);
}

void FileBrowser::ContextDelete(const QModelIndex &index) {
    if (!index.isValid()) return;

    QString path = model_->filePath(index);
    QString name = model_->fileName(index);
    bool is_dir = model_->isDir(index);

    auto result = QMessageBox::question(
        this, "Delete",
        QString("Are you sure you want to delete '%1'?").arg(name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (result != QMessageBox::Yes) return;

    bool success = false;
    if (is_dir) {
        success = QDir(path).removeRecursively();
    } else {
        success = QFile::remove(path);
    }

    if (!success) {
        QMessageBox::critical(this, "Delete", "Failed to delete item.");
        return;
    }

    emit ItemDeleted(path);
}

void FileBrowser::ContextCopyPath(const QModelIndex &index) {
    if (!index.isValid()) return;
    QString path = QDir::toNativeSeparators(model_->filePath(index));
    QApplication::clipboard()->setText(path);
}

void FileBrowser::ContextCopyRelativePath(const QModelIndex &index) {
    if (!index.isValid()) return;
    QString full_path = model_->filePath(index);
    QString root = model_->rootPath();
    QDir root_dir(root);
    QString relative = root_dir.relativeFilePath(full_path);
    QApplication::clipboard()->setText(QDir::toNativeSeparators(relative));
}

void FileBrowser::ContextRevealInExplorer(const QModelIndex &index) {
    if (!index.isValid()) return;
    QString path = model_->filePath(index);

#ifdef Q_OS_WIN
    // Select the item in Windows Explorer
    QProcess::startDetached("explorer.exe",
                            {"/select,", QDir::toNativeSeparators(path)});
#elif defined(Q_OS_MACOS)
    QProcess::startDetached("open", {"-R", path});
#else
    // Linux: open the containing folder
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
#endif
}

void FileBrowser::ContextOpenTerminal(const QModelIndex &index) {
    QString dir = DirectoryForIndex(index);
    emit OpenTerminalRequested(dir);
}

void FileBrowser::ContextGenerateTopology(const QModelIndex &index) {
    if (!index.isValid()) return;
    QString path = model_->filePath(index);
    if (path.endsWith(".ploy", Qt::CaseInsensitive)) {
        emit GenerateTopologyRequested(path);
    }
}

} // namespace polyglot::tools::ui
