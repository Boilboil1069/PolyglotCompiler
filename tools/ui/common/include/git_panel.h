// git_panel.h — Git integration panel for the PolyglotCompiler IDE.
//
// Provides source control features: status view, staging, commit, branch
// management, diff viewing, log display, push/pull, and blame.

#pragma once

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <functional>
#include <string>
#include <vector>

namespace polyglot::tools::ui {

// ============================================================================
// GitFileStatus — single file status entry
// ============================================================================

struct GitFileStatus {
    QString path;
    QString status;       // "M", "A", "D", "?", "R", "C", "U"
    bool staged{false};
};

// ============================================================================
// GitLogEntry — single commit from log
// ============================================================================

struct GitLogEntry {
    QString hash;
    QString short_hash;
    QString author;
    QString date;
    QString message;
};

// ============================================================================
// GitPanel — dock-able source control panel
// ============================================================================

class GitPanel : public QWidget {
    Q_OBJECT

  public:
    explicit GitPanel(QWidget *parent = nullptr);
    ~GitPanel() override;

    // Set the repository root path.
    void SetRepoPath(const QString &path);
    QString RepoPath() const { return repo_path_; }

    // Refresh the entire panel (status, branch, etc.).
    void Refresh();

    // Return true if the current path is inside a git repository.
    bool IsGitRepo() const { return is_git_repo_; }

    // Return the current branch name.
    QString CurrentBranch() const { return current_branch_; }

  signals:
    // Emitted when the user wants to open a file diff.
    void DiffRequested(const QString &file_path);

    // Emitted when the user wants to open a file from the status list.
    void FileOpenRequested(const QString &file_path);

    // Emitted when a git operation completes (for status bar update).
    void OperationCompleted(const QString &message);

    // Emitted when an error occurs during a git command.
    void OperationFailed(const QString &message);

  private slots:
    // Status actions
    void OnRefreshClicked();
    void OnStageFile();
    void OnUnstageFile();
    void OnStageAll();
    void OnUnstageAll();
    void OnDiscardChanges();

    // Commit actions
    void OnCommit();
    void OnAmendCommit();

    // Branch actions
    void OnBranchChanged(int index);
    void OnCreateBranch();
    void OnDeleteBranch();
    void OnMergeBranch();

    // Remote actions
    void OnPull();
    void OnPush();
    void OnFetch();

    // Log actions
    void OnLogEntryClicked(QListWidgetItem *item);

    // Diff actions
    void OnStatusItemDoubleClicked(QTreeWidgetItem *item, int column);
    void OnStatusContextMenu(const QPoint &pos);

    // Stash actions
    void OnStash();
    void OnStashPop();

  private:
    void SetupUi();
    void SetupToolBar();
    void SetupStatusView();
    void SetupCommitArea();
    void SetupLogView();
    void SetupDiffView();

    // Git command execution
    struct GitResult {
        bool success{false};
        QString output;
        QString error;
        int exit_code{-1};
    };
    GitResult RunGit(const QStringList &args) const;
    void RunGitAsync(const QStringList &args,
                     std::function<void(const GitResult &)> callback);

    // Status parsing
    void RefreshStatus();
    void RefreshBranches();
    void RefreshLog();
    void ParseStatusOutput(const QString &output);
    void UpdateStatusTree();

    // Diff retrieval
    QString GetFileDiff(const QString &path, bool staged) const;

    // ── UI Components ────────────────────────────────────────────────────
    QVBoxLayout *layout_{nullptr};
    QToolBar *toolbar_{nullptr};
    QSplitter *splitter_{nullptr};

    // Status section
    QLabel *branch_label_{nullptr};
    QComboBox *branch_combo_{nullptr};
    QTreeWidget *status_tree_{nullptr};
    QTreeWidgetItem *staged_root_{nullptr};
    QTreeWidgetItem *unstaged_root_{nullptr};
    QTreeWidgetItem *untracked_root_{nullptr};

    // Commit section
    QLineEdit *commit_message_edit_{nullptr};
    QPlainTextEdit *commit_body_edit_{nullptr};
    QPushButton *commit_button_{nullptr};
    QCheckBox *amend_check_{nullptr};

    // Log section
    QListWidget *log_list_{nullptr};

    // Diff section
    QPlainTextEdit *diff_view_{nullptr};

    // Bottom tabs (Log / Diff)
    QTabWidget *bottom_tabs_{nullptr};

    // Toolbar actions
    QAction *action_refresh_{nullptr};
    QAction *action_stage_all_{nullptr};
    QAction *action_unstage_all_{nullptr};
    QAction *action_pull_{nullptr};
    QAction *action_push_{nullptr};
    QAction *action_fetch_{nullptr};
    QAction *action_stash_{nullptr};
    QAction *action_stash_pop_{nullptr};
    QAction *action_create_branch_{nullptr};

    // ── State ────────────────────────────────────────────────────────────
    QString repo_path_;
    QString current_branch_;
    bool is_git_repo_{false};
    std::vector<GitFileStatus> file_statuses_;
    std::vector<GitLogEntry> log_entries_;
    QProcess *async_process_{nullptr};
};

} // namespace polyglot::tools::ui
