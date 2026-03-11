// file_browser.h — Project file browser panel.
//
// Provides a tree view of the filesystem rooted at a user-chosen directory,
// with file-type icons and double-click-to-open behaviour.

#pragma once

#include <QFileSystemModel>
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

  signals:
    // Emitted when the user double-clicks a file
    void FileActivated(const QString &path);

  private slots:
    void OnItemDoubleClicked(const QModelIndex &index);
    void OnFilterTextChanged(const QString &text);

  private:
    void SetupUi();

    QVBoxLayout *layout_{nullptr};
    QLabel *title_label_{nullptr};
    QLineEdit *filter_edit_{nullptr};
    QTreeView *tree_view_{nullptr};
    QFileSystemModel *model_{nullptr};
};

} // namespace polyglot::tools::ui
