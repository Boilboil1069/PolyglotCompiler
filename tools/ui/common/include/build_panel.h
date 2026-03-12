// build_panel.h — CMake and build tool integration for the PolyglotCompiler IDE.
//
// Provides project configuration, build target management, build execution
// with live output, and support for CMake, Make, and Ninja generators.

#pragma once

#include <QAction>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QTabWidget>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <string>
#include <vector>

namespace polyglot::tools::ui {

// ============================================================================
// BuildTarget — a single CMake build target
// ============================================================================

struct BuildTarget {
    QString name;
    QString type;        // "EXECUTABLE", "STATIC_LIBRARY", "SHARED_LIBRARY", etc.
    QString source_dir;
};

// ============================================================================
// BuildPanel — dock-able build management panel
// ============================================================================

class BuildPanel : public QWidget {
    Q_OBJECT

  public:
    explicit BuildPanel(QWidget *parent = nullptr);
    ~BuildPanel() override;

    // Set the project root (where CMakeLists.txt lives).
    void SetProjectPath(const QString &path);
    QString ProjectPath() const { return project_path_; }

    // Set the build directory.
    void SetBuildDir(const QString &path);
    QString BuildDir() const { return build_dir_; }

    // Return true if a build is currently in progress.
    bool IsBuilding() const { return build_process_ && build_process_->state() != QProcess::NotRunning; }

  signals:
    // Emitted when a build finishes.
    void BuildFinished(bool success, const QString &output);

    // Emitted when configure finishes.
    void ConfigureFinished(bool success);

    // Emitted when an error is parsed from build output.
    void BuildErrorFound(const QString &file, int line, const QString &message);

    // Status messages for the main status bar.
    void StatusMessage(const QString &message);

  public slots:
    // Configure
    void OnConfigure();
    void OnReconfigure();
    void OnClean();

    // Build
    void OnBuild();
    void OnBuildTarget();
    void OnRebuild();
    void OnStopBuild();

  private slots:
    // CMake targets
    void OnRefreshTargets();
    void OnTargetDoubleClicked(QTreeWidgetItem *item, int column);

    // Process I/O
    void OnConfigureReadyRead();
    void OnConfigureFinished(int exit_code, QProcess::ExitStatus status);
    void OnBuildReadyRead();
    void OnBuildFinished(int exit_code, QProcess::ExitStatus status);

    // Settings
    void OnGeneratorChanged(int index);
    void OnBuildTypeChanged(int index);
    void OnBrowseBuildDir();
    void OnBrowseCmakePath();

  private:
    void SetupUi();
    void SetupToolBar();
    void SetupConfigSection();
    void SetupTargetTree();
    void SetupOutputView();

    // Build system detection
    bool DetectCmake();
    void DetectTargets();
    void ParseBuildOutput(const QString &line);
    QString FindCmake() const;

    // ── UI Components ────────────────────────────────────────────────────
    QVBoxLayout *layout_{nullptr};
    QToolBar *toolbar_{nullptr};
    QTabWidget *tabs_{nullptr};

    // Config tab
    QWidget *config_widget_{nullptr};
    QLineEdit *cmake_path_edit_{nullptr};
    QLineEdit *build_dir_edit_{nullptr};
    QComboBox *generator_combo_{nullptr};
    QComboBox *build_type_combo_{nullptr};
    QLineEdit *extra_cmake_args_edit_{nullptr};
    QPushButton *configure_button_{nullptr};
    QPushButton *reconfigure_button_{nullptr};
    QPushButton *clean_button_{nullptr};

    // Targets tab
    QTreeWidget *target_tree_{nullptr};
    QPushButton *refresh_targets_button_{nullptr};

    // Build output tab
    QPlainTextEdit *build_output_{nullptr};
    QProgressBar *build_progress_{nullptr};
    QLabel *build_status_label_{nullptr};

    // Toolbar actions
    QAction *action_configure_{nullptr};
    QAction *action_build_{nullptr};
    QAction *action_rebuild_{nullptr};
    QAction *action_clean_{nullptr};
    QAction *action_stop_{nullptr};

    // ── State ────────────────────────────────────────────────────────────
    QString project_path_;
    QString build_dir_;
    QString cmake_path_;
    std::vector<BuildTarget> targets_;

    QProcess *configure_process_{nullptr};
    QProcess *build_process_{nullptr};

    int build_error_count_{0};
    int build_warning_count_{0};
};

} // namespace polyglot::tools::ui
