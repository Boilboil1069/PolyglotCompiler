// file_browser.h — Project file browser panel.
//
// Provides a tree view of the filesystem rooted at a user-chosen directory,
// with file-type icons, double-click-to-open behaviour, and a rich
// right-click context menu for common file operations.

#pragma once

#include <QFileSystemModel>
#include <QMenu>
#include <QTreeView>
#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>

namespace polyglot::tools::ui {

// ============================================================================
// FileBrowser — dock-able project file browser
// ============================================================================

class FileBrowser : public QWidget {
    Q_OBJECT

  public:
    explicit FileBrowser(QWidget *parent = nullptr);
    ~FileBrowser() override;

    // Set root directory for the file tree
    void SetRootPath(const QString &path);
    QString RootPath() const;

    // Re-apply theme colors to all child widgets
    void ApplyTheme();

  signals:
    // Emitted when the user double-clicks a file
    void FileActivated(const QString &path);

    // Emitted when the user requests to open a file from the context menu
    void OpenFileRequested(const QString &path);

    // Emitted when the user requests to create a new file in a directory
    void NewFileRequested(const QString &parent_dir);

    // Emitted when the user requests to create a new folder in a directory
    void NewFolderRequested(const QString &parent_dir);

    // Emitted when an item has been renamed (old_path → new_path)
    void ItemRenamed(const QString &old_path, const QString &new_path);

    // Emitted when an item has been deleted
    void ItemDeleted(const QString &path);

    // Emitted when the user requests to open a terminal at a directory
    void OpenTerminalRequested(const QString &directory);

    // Emitted when the user requests to generate a topology graph for a .ploy file
    void GenerateTopologyRequested(const QString &ploy_file_path);

  private slots:
    void OnItemDoubleClicked(const QModelIndex &index);
    void OnFilterTextChanged(const QString &text);
    void OnContextMenuRequested(const QPoint &pos);

  private:
    void SetupUi();
    void SetupContextMenu();

    // Context menu action handlers
    void ContextNewFile(const QModelIndex &index);
    void ContextNewFolder(const QModelIndex &index);
    void ContextRename(const QModelIndex &index);
    void ContextDelete(const QModelIndex &index);
    void ContextCopyPath(const QModelIndex &index);
    void ContextCopyRelativePath(const QModelIndex &index);
    void ContextRevealInExplorer(const QModelIndex &index);
    void ContextOpenTerminal(const QModelIndex &index);
    void ContextGenerateTopology(const QModelIndex &index);

    // Returns the directory path for the given index (the item itself if
    // it is a directory, otherwise its parent).
    QString DirectoryForIndex(const QModelIndex &index) const;

    QVBoxLayout *layout_{nullptr};
    QLabel *title_label_{nullptr};
    QLineEdit *filter_edit_{nullptr};
    QTreeView *tree_view_{nullptr};
    QFileSystemModel *model_{nullptr};

    // Context menu
    QMenu *context_menu_{nullptr};
    QAction *action_open_{nullptr};
    QAction *action_new_file_{nullptr};
    QAction *action_new_folder_{nullptr};
    QAction *action_rename_{nullptr};
    QAction *action_delete_{nullptr};
    QAction *action_copy_path_{nullptr};
    QAction *action_copy_relative_path_{nullptr};
    QAction *action_reveal_explorer_{nullptr};
    QAction *action_open_terminal_{nullptr};
    QAction *action_generate_topology_{nullptr};
};

} // namespace polyglot::tools::ui
