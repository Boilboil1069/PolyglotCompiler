// git_panel.cpp — Git integration panel implementation.
//
// Provides full source control: status, staging, commit, branch management,
// diff viewing, log display, push/pull/fetch, and stash operations.
// All git commands are executed via QProcess using the system's git binary.

#include "tools/ui/common/include/git_panel.h"
#include "tools/ui/common/include/theme_manager.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QHeaderView>
#include <QInputDialog>
#include <QMessageBox>
#include <QRegularExpression>

namespace polyglot::tools::ui {

// ============================================================================
// Construction / Destruction
// ============================================================================

GitPanel::GitPanel(QWidget *parent) : QWidget(parent) {
    SetupUi();
}

GitPanel::~GitPanel() {
    if (async_process_) {
        async_process_->kill();
        async_process_->waitForFinished(1000);
    }
}

// ============================================================================
// UI Setup
// ============================================================================

void GitPanel::SetupUi() {
    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(0);

    SetupToolBar();

    // Branch label and combo
    auto *branch_row = new QHBoxLayout();
    branch_row->setContentsMargins(8, 4, 8, 4);
    branch_label_ = new QLabel("Branch:");
    branch_label_->setStyleSheet(ThemeManager::Instance().LabelStylesheet() +
        " QLabel { font-size: 12px; }");
    branch_row->addWidget(branch_label_);

    branch_combo_ = new QComboBox();
    branch_combo_->setStyleSheet(ThemeManager::Instance().ComboBoxStylesheet() +
        " QComboBox { min-width: 120px; }");
    connect(branch_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &GitPanel::OnBranchChanged);
    branch_row->addWidget(branch_combo_, 1);
    layout_->addLayout(branch_row);

    // Main splitter
    splitter_ = new QSplitter(Qt::Vertical);
    splitter_->setStyleSheet(ThemeManager::Instance().SplitterStylesheet());

    SetupStatusView();
    SetupCommitArea();

    // Bottom tabs: Log and Diff
    bottom_tabs_ = new QTabWidget();
    bottom_tabs_->setTabPosition(QTabWidget::South);
    bottom_tabs_->setStyleSheet(ThemeManager::Instance().TabWidgetStylesheet(true));

    SetupLogView();
    SetupDiffView();

    splitter_->addWidget(bottom_tabs_);
    splitter_->setSizes({200, 100, 200});

    layout_->addWidget(splitter_, 1);
}

void GitPanel::SetupToolBar() {
    toolbar_ = new QToolBar();
    toolbar_->setMovable(false);
    toolbar_->setIconSize(QSize(16, 16));
    toolbar_->setStyleSheet(ThemeManager::Instance().ToolBarStylesheet());

    action_refresh_ = toolbar_->addAction("Refresh");
    connect(action_refresh_, &QAction::triggered, this, &GitPanel::OnRefreshClicked);

    toolbar_->addSeparator();

    action_stage_all_ = toolbar_->addAction("+All");
    connect(action_stage_all_, &QAction::triggered, this, &GitPanel::OnStageAll);

    action_unstage_all_ = toolbar_->addAction("-All");
    connect(action_unstage_all_, &QAction::triggered, this, &GitPanel::OnUnstageAll);

    toolbar_->addSeparator();

    action_pull_ = toolbar_->addAction("Pull");
    connect(action_pull_, &QAction::triggered, this, &GitPanel::OnPull);

    action_push_ = toolbar_->addAction("Push");
    connect(action_push_, &QAction::triggered, this, &GitPanel::OnPush);

    action_fetch_ = toolbar_->addAction("Fetch");
    connect(action_fetch_, &QAction::triggered, this, &GitPanel::OnFetch);

    toolbar_->addSeparator();

    action_stash_ = toolbar_->addAction("Stash");
    connect(action_stash_, &QAction::triggered, this, &GitPanel::OnStash);

    action_stash_pop_ = toolbar_->addAction("Pop");
    connect(action_stash_pop_, &QAction::triggered, this, &GitPanel::OnStashPop);

    toolbar_->addSeparator();

    action_create_branch_ = toolbar_->addAction("+Branch");
    connect(action_create_branch_, &QAction::triggered, this, &GitPanel::OnCreateBranch);

    layout_->addWidget(toolbar_);
}

void GitPanel::SetupStatusView() {
    status_tree_ = new QTreeWidget();
    status_tree_->setHeaderLabels({"File", "Status"});
    status_tree_->setRootIsDecorated(true);
    status_tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    status_tree_->setStyleSheet(ThemeManager::Instance().TreeWidgetStylesheet() +
        " QTreeWidget { alternate-background-color: " +
        ThemeManager::Instance().Active().surface_alt.name() + "; }");

    staged_root_ = new QTreeWidgetItem(status_tree_, {"Staged Changes", ""});
    staged_root_->setExpanded(true);
    staged_root_->setForeground(0, QColor("#73c991"));

    unstaged_root_ = new QTreeWidgetItem(status_tree_, {"Changes", ""});
    unstaged_root_->setExpanded(true);
    unstaged_root_->setForeground(0, QColor("#e8a838"));

    untracked_root_ = new QTreeWidgetItem(status_tree_, {"Untracked", ""});
    untracked_root_->setExpanded(true);
    untracked_root_->setForeground(0, QColor("#cccccc"));

    status_tree_->header()->setStretchLastSection(false);
    status_tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    status_tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    connect(status_tree_, &QTreeWidget::itemDoubleClicked,
            this, &GitPanel::OnStatusItemDoubleClicked);
    connect(status_tree_, &QTreeWidget::customContextMenuRequested,
            this, &GitPanel::OnStatusContextMenu);

    splitter_->addWidget(status_tree_);
}

void GitPanel::SetupCommitArea() {
    auto *commit_widget = new QWidget();
    auto *commit_layout = new QVBoxLayout(commit_widget);
    commit_layout->setContentsMargins(4, 4, 4, 4);
    commit_layout->setSpacing(4);

    commit_message_edit_ = new QLineEdit();
    commit_message_edit_->setPlaceholderText("Commit message (required)");
    commit_message_edit_->setStyleSheet(ThemeManager::Instance().LineEditStylesheet() +
        " QLineEdit { padding: 6px; }");
    commit_layout->addWidget(commit_message_edit_);

    commit_body_edit_ = new QPlainTextEdit();
    commit_body_edit_->setPlaceholderText("Extended description (optional)");
    commit_body_edit_->setMaximumHeight(80);
    {
        const auto &tc = ThemeManager::Instance().Active();
        commit_body_edit_->setStyleSheet(
            QString("QPlainTextEdit { background: %1; color: %2; border: 1px solid %3; "
                    "border-radius: 3px; padding: 4px; }")
                .arg(tc.input_background.name(), tc.input_text.name(),
                     tc.input_border.name()));
    }
    commit_layout->addWidget(commit_body_edit_);

    auto *commit_row = new QHBoxLayout();
    amend_check_ = new QCheckBox("Amend");
    amend_check_->setStyleSheet(ThemeManager::Instance().CheckBoxStylesheet());
    commit_row->addWidget(amend_check_);

    commit_button_ = new QPushButton("Commit");
    commit_button_->setStyleSheet(ThemeManager::Instance().PushButtonPrimaryStylesheet());
    connect(commit_button_, &QPushButton::clicked, this, &GitPanel::OnCommit);
    commit_row->addStretch();
    commit_row->addWidget(commit_button_);
    commit_layout->addLayout(commit_row);

    splitter_->addWidget(commit_widget);
}

void GitPanel::SetupLogView() {
    log_list_ = new QListWidget();
    log_list_->setStyleSheet(
        ThemeManager::Instance().ListWidgetStylesheet() +
        " QListWidget { font-family: Menlo, Consolas, monospace; font-size: 11px; }"
        " QListWidget::item { padding: 3px; }");
    connect(log_list_, &QListWidget::itemClicked, this, &GitPanel::OnLogEntryClicked);
    bottom_tabs_->addTab(log_list_, "Log");
}

void GitPanel::SetupDiffView() {
    diff_view_ = new QPlainTextEdit();
    diff_view_->setReadOnly(true);
    diff_view_->setStyleSheet(ThemeManager::Instance().PlainTextEditStylesheet());
    bottom_tabs_->addTab(diff_view_, "Diff");
}

// ============================================================================
// Public Interface
// ============================================================================

void GitPanel::SetRepoPath(const QString &path) {
    repo_path_ = path;

    // Check if this is a git repo
    auto result = RunGit({"rev-parse", "--is-inside-work-tree"});
    is_git_repo_ = result.success && result.output.trimmed() == "true";

    if (is_git_repo_) {
        Refresh();
    }
}

void GitPanel::Refresh() {
    if (!is_git_repo_) return;
    RefreshStatus();
    RefreshBranches();
    RefreshLog();
}

// ============================================================================
// Git Command Execution
// ============================================================================

GitPanel::GitResult GitPanel::RunGit(const QStringList &args) const {
    GitResult result;
    QProcess process;
    process.setWorkingDirectory(repo_path_);
    process.start("git", args);

    if (!process.waitForFinished(15000)) {
        result.error = "Git command timed out";
        return result;
    }

    result.exit_code = process.exitCode();
    result.success = (result.exit_code == 0);
    result.output = QString::fromUtf8(process.readAllStandardOutput());
    result.error = QString::fromUtf8(process.readAllStandardError());
    return result;
}

void GitPanel::RunGitAsync(const QStringList &args,
                           std::function<void(const GitResult &)> callback) {
    if (async_process_ && async_process_->state() != QProcess::NotRunning) {
        async_process_->kill();
        async_process_->waitForFinished(2000);
    }

    async_process_ = new QProcess(this);
    async_process_->setWorkingDirectory(repo_path_);

    connect(async_process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, callback](int exit_code, QProcess::ExitStatus) {
        GitResult result;
        result.exit_code = exit_code;
        result.success = (exit_code == 0);
        result.output = QString::fromUtf8(async_process_->readAllStandardOutput());
        result.error = QString::fromUtf8(async_process_->readAllStandardError());
        callback(result);
        async_process_->deleteLater();
        async_process_ = nullptr;
    });

    async_process_->start("git", args);
}

// ============================================================================
// Status
// ============================================================================

void GitPanel::RefreshStatus() {
    auto result = RunGit({"status", "--porcelain=v1"});
    if (!result.success) return;

    ParseStatusOutput(result.output);
    UpdateStatusTree();
}

void GitPanel::ParseStatusOutput(const QString &output) {
    file_statuses_.clear();
    const QStringList lines = output.split('\n', Qt::SkipEmptyParts);

    for (const QString &line : lines) {
        if (line.length() < 4) continue;

        GitFileStatus fs;
        QChar index_status = line[0];
        QChar worktree_status = line[1];
        fs.path = line.mid(3).trimmed();

        // Handle renames: "R  old -> new"
        if (fs.path.contains(" -> ")) {
            fs.path = fs.path.split(" -> ").last();
        }

        if (index_status != ' ' && index_status != '?') {
            // Staged change
            GitFileStatus staged_fs;
            staged_fs.path = fs.path;
            staged_fs.status = index_status;
            staged_fs.staged = true;
            file_statuses_.push_back(staged_fs);
        }

        if (worktree_status != ' ') {
            // Unstaged or untracked change
            fs.status = (index_status == '?' && worktree_status == '?') ? "?" : QString(worktree_status);
            fs.staged = false;
            file_statuses_.push_back(fs);
        }
    }
}

void GitPanel::UpdateStatusTree() {
    // Clear children of root items
    while (staged_root_->childCount() > 0)
        delete staged_root_->takeChild(0);
    while (unstaged_root_->childCount() > 0)
        delete unstaged_root_->takeChild(0);
    while (untracked_root_->childCount() > 0)
        delete untracked_root_->takeChild(0);

    int staged_count = 0, unstaged_count = 0, untracked_count = 0;

    for (const auto &fs : file_statuses_) {
        auto *item = new QTreeWidgetItem({fs.path, fs.status});
        item->setData(0, Qt::UserRole, fs.path);
        item->setData(1, Qt::UserRole, fs.staged);

        // Colour coding by status
        QColor color;
        if (fs.status == "M") color = QColor("#e8a838");      // Modified
        else if (fs.status == "A") color = QColor("#73c991");  // Added
        else if (fs.status == "D") color = QColor("#f44747");  // Deleted
        else if (fs.status == "R") color = QColor("#569cd6");  // Renamed
        else if (fs.status == "?") color = QColor("#999999");  // Untracked
        else color = QColor("#cccccc");

        item->setForeground(0, color);
        item->setForeground(1, color);

        if (fs.staged) {
            staged_root_->addChild(item);
            ++staged_count;
        } else if (fs.status == "?") {
            untracked_root_->addChild(item);
            ++untracked_count;
        } else {
            unstaged_root_->addChild(item);
            ++unstaged_count;
        }
    }

    staged_root_->setText(0, QString("Staged Changes (%1)").arg(staged_count));
    unstaged_root_->setText(0, QString("Changes (%1)").arg(unstaged_count));
    untracked_root_->setText(0, QString("Untracked (%1)").arg(untracked_count));
}

// ============================================================================
// Branches
// ============================================================================

void GitPanel::RefreshBranches() {
    // Get current branch
    auto head_result = RunGit({"rev-parse", "--abbrev-ref", "HEAD"});
    if (head_result.success) {
        current_branch_ = head_result.output.trimmed();
    }

    // Get all local branches
    auto branch_result = RunGit({"branch", "--list", "--no-color"});
    if (!branch_result.success) return;

    branch_combo_->blockSignals(true);
    branch_combo_->clear();

    const QStringList lines = branch_result.output.split('\n', Qt::SkipEmptyParts);
    int current_idx = 0;
    for (const QString &line : lines) {
        QString name = line.trimmed();
        if (name.startsWith("* ")) {
            name = name.mid(2);
        }
        if (!name.isEmpty()) {
            branch_combo_->addItem(name);
            if (name == current_branch_) {
                current_idx = branch_combo_->count() - 1;
            }
        }
    }
    branch_combo_->setCurrentIndex(current_idx);
    branch_combo_->blockSignals(false);
}

// ============================================================================
// Log
// ============================================================================

void GitPanel::RefreshLog() {
    auto result = RunGit({"log", "--oneline", "--no-color", "-50",
                          "--format=%H|%h|%an|%ad|%s", "--date=short"});
    if (!result.success) return;

    log_entries_.clear();
    log_list_->clear();

    const QStringList lines = result.output.split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        QStringList parts = line.split('|');
        if (parts.size() < 5) continue;

        GitLogEntry entry;
        entry.hash = parts[0];
        entry.short_hash = parts[1];
        entry.author = parts[2];
        entry.date = parts[3];
        entry.message = parts.mid(4).join('|'); // message may contain |

        log_entries_.push_back(entry);

        QString display = QString("%1  %2  %3  %4")
            .arg(entry.short_hash, -8)
            .arg(entry.date, -12)
            .arg(entry.author, -16)
            .arg(entry.message);
        auto *item = new QListWidgetItem(display);
        item->setData(Qt::UserRole, static_cast<int>(log_entries_.size() - 1));
        log_list_->addItem(item);
    }
}

// ============================================================================
// Diff
// ============================================================================

QString GitPanel::GetFileDiff(const QString &path, bool staged) const {
    QStringList args = {"diff"};
    if (staged) args << "--staged";
    args << "--" << path;

    auto result = RunGit(args);
    return result.success ? result.output : result.error;
}

// ============================================================================
// Status Slots
// ============================================================================

void GitPanel::OnRefreshClicked() {
    Refresh();
    emit OperationCompleted("Git status refreshed");
}

void GitPanel::OnStageFile() {
    auto *item = status_tree_->currentItem();
    if (!item || !item->parent()) return;

    QString path = item->data(0, Qt::UserRole).toString();
    auto result = RunGit({"add", "--", path});

    if (result.success) {
        RefreshStatus();
        emit OperationCompleted("Staged: " + path);
    } else {
        emit OperationFailed("Failed to stage: " + result.error);
    }
}

void GitPanel::OnUnstageFile() {
    auto *item = status_tree_->currentItem();
    if (!item || !item->parent()) return;

    QString path = item->data(0, Qt::UserRole).toString();
    auto result = RunGit({"reset", "HEAD", "--", path});

    if (result.success) {
        RefreshStatus();
        emit OperationCompleted("Unstaged: " + path);
    } else {
        emit OperationFailed("Failed to unstage: " + result.error);
    }
}

void GitPanel::OnStageAll() {
    auto result = RunGit({"add", "-A"});
    if (result.success) {
        RefreshStatus();
        emit OperationCompleted("All changes staged");
    } else {
        emit OperationFailed("Failed to stage all: " + result.error);
    }
}

void GitPanel::OnUnstageAll() {
    auto result = RunGit({"reset", "HEAD"});
    if (result.success) {
        RefreshStatus();
        emit OperationCompleted("All changes unstaged");
    } else {
        emit OperationFailed("Failed to unstage: " + result.error);
    }
}

void GitPanel::OnDiscardChanges() {
    auto *item = status_tree_->currentItem();
    if (!item || !item->parent()) return;

    QString path = item->data(0, Qt::UserRole).toString();
    auto confirm = QMessageBox::question(
        this, "Discard Changes",
        QString("Discard changes to '%1'?\nThis cannot be undone.").arg(path),
        QMessageBox::Yes | QMessageBox::No);
    if (confirm != QMessageBox::Yes) return;

    auto result = RunGit({"checkout", "--", path});
    if (result.success) {
        RefreshStatus();
        emit OperationCompleted("Discarded changes: " + path);
    } else {
        emit OperationFailed("Failed to discard: " + result.error);
    }
}

// ============================================================================
// Commit Slots
// ============================================================================

void GitPanel::OnCommit() {
    QString message = commit_message_edit_->text().trimmed();
    if (message.isEmpty()) {
        QMessageBox::warning(this, "Commit", "Please enter a commit message.");
        return;
    }

    // Append extended body if provided
    QString body = commit_body_edit_->toPlainText().trimmed();
    if (!body.isEmpty()) {
        message += "\n\n" + body;
    }

    QStringList args = {"commit", "-m", message};
    if (amend_check_->isChecked()) {
        args.insert(1, "--amend");
    }

    auto result = RunGit(args);
    if (result.success) {
        commit_message_edit_->clear();
        commit_body_edit_->clear();
        amend_check_->setChecked(false);
        Refresh();
        emit OperationCompleted("Commit successful");
    } else {
        emit OperationFailed("Commit failed: " + result.error);
    }
}

void GitPanel::OnAmendCommit() {
    amend_check_->setChecked(true);
    OnCommit();
}

// ============================================================================
// Branch Slots
// ============================================================================

void GitPanel::OnBranchChanged(int index) {
    if (index < 0) return;
    QString branch = branch_combo_->itemText(index);
    if (branch == current_branch_) return;

    auto result = RunGit({"checkout", branch});
    if (result.success) {
        current_branch_ = branch;
        Refresh();
        emit OperationCompleted("Switched to branch: " + branch);
    } else {
        emit OperationFailed("Failed to switch branch: " + result.error);
        RefreshBranches(); // reset combo
    }
}

void GitPanel::OnCreateBranch() {
    bool ok = false;
    QString name = QInputDialog::getText(
        this, "New Branch", "Branch name:", QLineEdit::Normal, QString(), &ok);
    if (!ok || name.isEmpty()) return;

    auto result = RunGit({"checkout", "-b", name});
    if (result.success) {
        current_branch_ = name;
        RefreshBranches();
        emit OperationCompleted("Created and switched to: " + name);
    } else {
        emit OperationFailed("Failed to create branch: " + result.error);
    }
}

void GitPanel::OnDeleteBranch() {
    QString branch = branch_combo_->currentText();
    if (branch == current_branch_) {
        QMessageBox::warning(this, "Delete Branch",
                             "Cannot delete the current branch.");
        return;
    }

    auto confirm = QMessageBox::question(
        this, "Delete Branch",
        QString("Delete branch '%1'?").arg(branch),
        QMessageBox::Yes | QMessageBox::No);
    if (confirm != QMessageBox::Yes) return;

    auto result = RunGit({"branch", "-d", branch});
    if (result.success) {
        RefreshBranches();
        emit OperationCompleted("Deleted branch: " + branch);
    } else {
        emit OperationFailed("Failed to delete branch: " + result.error);
    }
}

void GitPanel::OnMergeBranch() {
    bool ok = false;
    QStringList branches;
    for (int i = 0; i < branch_combo_->count(); ++i) {
        if (branch_combo_->itemText(i) != current_branch_) {
            branches << branch_combo_->itemText(i);
        }
    }
    if (branches.isEmpty()) {
        QMessageBox::information(this, "Merge", "No other branches to merge.");
        return;
    }

    QString branch = QInputDialog::getItem(
        this, "Merge Branch", "Merge into " + current_branch_ + ":",
        branches, 0, false, &ok);
    if (!ok || branch.isEmpty()) return;

    auto result = RunGit({"merge", branch});
    if (result.success) {
        Refresh();
        emit OperationCompleted("Merged " + branch + " into " + current_branch_);
    } else {
        emit OperationFailed("Merge failed: " + result.error);
    }
}

// ============================================================================
// Remote Slots
// ============================================================================

void GitPanel::OnPull() {
    emit OperationCompleted("Pulling...");
    RunGitAsync({"pull"}, [this](const GitResult &result) {
        if (result.success) {
            Refresh();
            emit OperationCompleted("Pull completed");
        } else {
            emit OperationFailed("Pull failed: " + result.error);
        }
    });
}

void GitPanel::OnPush() {
    emit OperationCompleted("Pushing...");
    RunGitAsync({"push"}, [this](const GitResult &result) {
        if (result.success) {
            emit OperationCompleted("Push completed");
        } else {
            emit OperationFailed("Push failed: " + result.error);
        }
    });
}

void GitPanel::OnFetch() {
    emit OperationCompleted("Fetching...");
    RunGitAsync({"fetch", "--all"}, [this](const GitResult &result) {
        if (result.success) {
            RefreshBranches();
            emit OperationCompleted("Fetch completed");
        } else {
            emit OperationFailed("Fetch failed: " + result.error);
        }
    });
}

// ============================================================================
// Log Slots
// ============================================================================

void GitPanel::OnLogEntryClicked(QListWidgetItem *item) {
    int idx = item->data(Qt::UserRole).toInt();
    if (idx < 0 || idx >= static_cast<int>(log_entries_.size())) return;

    const auto &entry = log_entries_[idx];
    auto result = RunGit({"show", "--stat", "--format=full", entry.hash});
    if (result.success) {
        diff_view_->setPlainText(result.output);
        bottom_tabs_->setCurrentWidget(diff_view_);
    }
}

// ============================================================================
// Diff / Context Menu Slots
// ============================================================================

void GitPanel::OnStatusItemDoubleClicked(QTreeWidgetItem *item, int /*column*/) {
    if (!item || !item->parent()) return;

    QString path = item->data(0, Qt::UserRole).toString();
    bool staged = item->data(1, Qt::UserRole).toBool();

    QString diff = GetFileDiff(path, staged);
    if (!diff.isEmpty()) {
        diff_view_->setPlainText(diff);
        bottom_tabs_->setCurrentWidget(diff_view_);
    }

    emit DiffRequested(path);
}

void GitPanel::OnStatusContextMenu(const QPoint &pos) {
    auto *item = status_tree_->itemAt(pos);
    if (!item || !item->parent()) return;

    bool is_staged = item->data(1, Qt::UserRole).toBool();
    QString path = item->data(0, Qt::UserRole).toString();

    QMenu menu(this);
    {
        const auto &tc = ThemeManager::Instance().Active();
        menu.setStyleSheet(
            QString("QMenu { background: %1; color: %2; border: 1px solid %3; }"
                    "QMenu::item { padding: 5px 30px 5px 20px; }"
                    "QMenu::item:selected { background: %4; }")
                .arg(tc.menu_background.name(), tc.menu_text.name(),
                     tc.border.name(), tc.menu_hover.name()));
    }

    if (is_staged) {
        menu.addAction("Unstage", this, &GitPanel::OnUnstageFile);
    } else {
        menu.addAction("Stage", this, &GitPanel::OnStageFile);
        menu.addAction("Discard Changes", this, &GitPanel::OnDiscardChanges);
    }
    menu.addSeparator();
    menu.addAction("Open File", [this, path]() {
        emit FileOpenRequested(repo_path_ + "/" + path);
    });
    menu.addAction("Show Diff", [this, item]() {
        OnStatusItemDoubleClicked(item, 0);
    });

    menu.exec(status_tree_->viewport()->mapToGlobal(pos));
}

// ============================================================================
// Stash Slots
// ============================================================================

void GitPanel::OnStash() {
    auto result = RunGit({"stash", "push", "-m",
                          "IDE stash " + QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss")});
    if (result.success) {
        Refresh();
        emit OperationCompleted("Changes stashed");
    } else {
        emit OperationFailed("Stash failed: " + result.error);
    }
}

void GitPanel::OnStashPop() {
    auto result = RunGit({"stash", "pop"});
    if (result.success) {
        Refresh();
        emit OperationCompleted("Stash popped");
    } else {
        emit OperationFailed("Stash pop failed: " + result.error);
    }
}

} // namespace polyglot::tools::ui
