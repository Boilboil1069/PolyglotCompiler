// build_panel.cpp — CMake build tool integration implementation.
//
// Handles CMake configuration, build execution, target discovery,
// build output parsing with error/warning extraction, and support
// for multiple generators (Makefiles, Ninja).

#include "tools/ui/common/include/build_panel.h"
#include "tools/ui/common/include/theme_manager.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStandardPaths>
#include <QTime>

namespace polyglot::tools::ui {

// ============================================================================
// Construction / Destruction
// ============================================================================

BuildPanel::BuildPanel(QWidget *parent) : QWidget(parent) {
    SetupUi();

    // Try to find cmake on the system
    cmake_path_ = FindCmake();
    if (cmake_path_edit_) {
        cmake_path_edit_->setText(cmake_path_);
    }
}

BuildPanel::~BuildPanel() {
    if (configure_process_ && configure_process_->state() != QProcess::NotRunning) {
        configure_process_->kill();
        configure_process_->waitForFinished(2000);
    }
    if (build_process_ && build_process_->state() != QProcess::NotRunning) {
        build_process_->kill();
        build_process_->waitForFinished(2000);
    }
}

// ============================================================================
// UI Setup
// ============================================================================

void BuildPanel::SetupUi() {
    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(0);

    SetupToolBar();

    tabs_ = new QTabWidget();
    tabs_->setTabPosition(QTabWidget::North);
    tabs_->setStyleSheet(ThemeManager::Instance().TabWidgetStylesheet(false));

    SetupConfigSection();
    SetupTargetTree();
    SetupOutputView();

    layout_->addWidget(tabs_, 1);

    // Progress bar at bottom
    build_progress_ = new QProgressBar();
    build_progress_->setRange(0, 0); // indeterminate
    build_progress_->setVisible(false);
    build_progress_->setMaximumHeight(4);
    build_progress_->setTextVisible(false);
    build_progress_->setStyleSheet(ThemeManager::Instance().ProgressBarStylesheet());
    layout_->addWidget(build_progress_);

    // Status label
    build_status_label_ = new QLabel("Ready");
    {
        const auto &tc = ThemeManager::Instance().Active();
        build_status_label_->setStyleSheet(
            QString("QLabel { color: %1; font-size: 11px; padding: 2px 8px; background: %2; }")
                .arg(tc.text_secondary.name(), tc.surface_alt.name()));
    }
    layout_->addWidget(build_status_label_);
}

void BuildPanel::SetupToolBar() {
    toolbar_ = new QToolBar();
    toolbar_->setMovable(false);
    toolbar_->setIconSize(QSize(16, 16));
    toolbar_->setStyleSheet(ThemeManager::Instance().ToolBarStylesheet());

    action_configure_ = toolbar_->addAction("Configure");
    connect(action_configure_, &QAction::triggered, this, &BuildPanel::OnConfigure);

    action_build_ = toolbar_->addAction("Build");
    connect(action_build_, &QAction::triggered, this, &BuildPanel::OnBuild);

    action_rebuild_ = toolbar_->addAction("Rebuild");
    connect(action_rebuild_, &QAction::triggered, this, &BuildPanel::OnRebuild);

    toolbar_->addSeparator();

    action_clean_ = toolbar_->addAction("Clean");
    connect(action_clean_, &QAction::triggered, this, &BuildPanel::OnClean);

    action_stop_ = toolbar_->addAction("Stop");
    action_stop_->setEnabled(false);
    connect(action_stop_, &QAction::triggered, this, &BuildPanel::OnStopBuild);

    layout_->addWidget(toolbar_);
}

void BuildPanel::SetupConfigSection() {
    config_widget_ = new QWidget();
    auto *form_layout = new QVBoxLayout(config_widget_);
    form_layout->setContentsMargins(8, 8, 8, 8);
    form_layout->setSpacing(8);

    const auto &tm = ThemeManager::Instance();
    const auto &tc = tm.Active();
    const QString label_style = tm.LabelStylesheet() +
        " QLabel { font-size: 12px; }";
    const QString input_style = tm.LineEditStylesheet() +
        " QLineEdit { font-size: 12px; }";
    const QString combo_style = tm.ComboBoxStylesheet() +
        " QComboBox { min-width: 120px; }";
    const QString button_style = tm.PushButtonStylesheet();
    const QString group_style = tm.GroupBoxStylesheet();

    // CMake path
    auto *cmake_group = new QGroupBox("CMake");
    cmake_group->setStyleSheet(group_style);
    auto *cmake_layout = new QFormLayout(cmake_group);

    cmake_path_edit_ = new QLineEdit();
    cmake_path_edit_->setStyleSheet(input_style);
    cmake_path_edit_->setPlaceholderText("Path to cmake executable");
    auto *cmake_row = new QHBoxLayout();
    cmake_row->addWidget(cmake_path_edit_);
    auto *cmake_browse = new QPushButton("Browse");
    cmake_browse->setStyleSheet(button_style);
    connect(cmake_browse, &QPushButton::clicked, this, &BuildPanel::OnBrowseCmakePath);
    cmake_row->addWidget(cmake_browse);

    auto *cmake_label = new QLabel("CMake:");
    cmake_label->setStyleSheet(label_style);
    cmake_layout->addRow(cmake_label, cmake_row);

    // Build directory
    build_dir_edit_ = new QLineEdit("build");
    build_dir_edit_->setStyleSheet(input_style);
    auto *build_dir_row = new QHBoxLayout();
    build_dir_row->addWidget(build_dir_edit_);
    auto *build_dir_browse = new QPushButton("Browse");
    build_dir_browse->setStyleSheet(button_style);
    connect(build_dir_browse, &QPushButton::clicked, this, &BuildPanel::OnBrowseBuildDir);
    build_dir_row->addWidget(build_dir_browse);

    auto *build_dir_label = new QLabel("Build Dir:");
    build_dir_label->setStyleSheet(label_style);
    cmake_layout->addRow(build_dir_label, build_dir_row);

    form_layout->addWidget(cmake_group);

    // Generator and Build Type
    auto *build_group = new QGroupBox("Build Settings");
    build_group->setStyleSheet(group_style);
    auto *build_settings_layout = new QFormLayout(build_group);

    generator_combo_ = new QComboBox();
    generator_combo_->setStyleSheet(combo_style);
    generator_combo_->addItems({"Default", "Unix Makefiles", "Ninja", "Ninja Multi-Config"});
    connect(generator_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &BuildPanel::OnGeneratorChanged);
    auto *gen_label = new QLabel("Generator:");
    gen_label->setStyleSheet(label_style);
    build_settings_layout->addRow(gen_label, generator_combo_);

    build_type_combo_ = new QComboBox();
    build_type_combo_->setStyleSheet(combo_style);
    build_type_combo_->addItems({"Debug", "Release", "RelWithDebInfo", "MinSizeRel"});
    connect(build_type_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &BuildPanel::OnBuildTypeChanged);
    auto *type_label = new QLabel("Build Type:");
    type_label->setStyleSheet(label_style);
    build_settings_layout->addRow(type_label, build_type_combo_);

    extra_cmake_args_edit_ = new QLineEdit();
    extra_cmake_args_edit_->setStyleSheet(input_style);
    extra_cmake_args_edit_->setPlaceholderText("-DSOME_OPTION=ON ...");
    auto *args_label = new QLabel("Extra Args:");
    args_label->setStyleSheet(label_style);
    build_settings_layout->addRow(args_label, extra_cmake_args_edit_);

    form_layout->addWidget(build_group);

    // Action buttons
    auto *button_row = new QHBoxLayout();
    configure_button_ = new QPushButton("Configure");
    configure_button_->setStyleSheet(tm.PushButtonPrimaryStylesheet());
    connect(configure_button_, &QPushButton::clicked, this, &BuildPanel::OnConfigure);

    reconfigure_button_ = new QPushButton("Reconfigure");
    reconfigure_button_->setStyleSheet(button_style);
    connect(reconfigure_button_, &QPushButton::clicked, this, &BuildPanel::OnReconfigure);

    clean_button_ = new QPushButton("Clean");
    clean_button_->setStyleSheet(button_style);
    connect(clean_button_, &QPushButton::clicked, this, &BuildPanel::OnClean);

    button_row->addWidget(configure_button_);
    button_row->addWidget(reconfigure_button_);
    button_row->addWidget(clean_button_);
    button_row->addStretch();
    form_layout->addLayout(button_row);

    form_layout->addStretch();

    tabs_->addTab(config_widget_, "Configure");
}

void BuildPanel::SetupTargetTree() {
    auto *target_widget = new QWidget();
    auto *target_layout = new QVBoxLayout(target_widget);
    target_layout->setContentsMargins(0, 0, 0, 0);
    target_layout->setSpacing(0);

    target_tree_ = new QTreeWidget();
    target_tree_->setHeaderLabels({"Target", "Type"});
    target_tree_->setRootIsDecorated(false);
    target_tree_->setStyleSheet(ThemeManager::Instance().TreeWidgetStylesheet() +
        " QTreeWidget { alternate-background-color: " +
        ThemeManager::Instance().Active().surface_alt.name() + "; }");
    target_tree_->header()->setStretchLastSection(false);
    target_tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    target_tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    connect(target_tree_, &QTreeWidget::itemDoubleClicked,
            this, &BuildPanel::OnTargetDoubleClicked);
    target_layout->addWidget(target_tree_, 1);

    // Refresh button row
    auto *btn_row = new QHBoxLayout();
    btn_row->setContentsMargins(4, 4, 4, 4);
    refresh_targets_button_ = new QPushButton("Refresh Targets");
    refresh_targets_button_->setStyleSheet(ThemeManager::Instance().PushButtonStylesheet());
    connect(refresh_targets_button_, &QPushButton::clicked, this, &BuildPanel::OnRefreshTargets);
    btn_row->addStretch();
    btn_row->addWidget(refresh_targets_button_);
    target_layout->addLayout(btn_row);

    tabs_->addTab(target_widget, "Targets");
}

void BuildPanel::SetupOutputView() {
    build_output_ = new QPlainTextEdit();
    build_output_->setReadOnly(true);
    build_output_->setMaximumBlockCount(10000);
    build_output_->setStyleSheet(ThemeManager::Instance().PlainTextEditStylesheet());

    tabs_->addTab(build_output_, "Output");
}

// ============================================================================
// Public Interface
// ============================================================================

void BuildPanel::SetProjectPath(const QString &path) {
    project_path_ = path;
    if (build_dir_.isEmpty()) {
        build_dir_ = project_path_ + "/build";
        if (build_dir_edit_)
            build_dir_edit_->setText("build");
    }
}

void BuildPanel::SetBuildDir(const QString &path) {
    build_dir_ = path;
    if (build_dir_edit_) {
        // Show relative path if inside project, otherwise absolute
        if (path.startsWith(project_path_)) {
            build_dir_edit_->setText(QDir(project_path_).relativeFilePath(path));
        } else {
            build_dir_edit_->setText(path);
        }
    }
}

void BuildPanel::SetCmakePath(const QString &path) {
    cmake_path_ = path;
    if (cmake_path_edit_) {
        cmake_path_edit_->setText(path);
    }
}

// ============================================================================
// CMake Detection
// ============================================================================

QString BuildPanel::FindCmake() const {
    // Check standard paths
    QStringList candidates = {
        "/opt/homebrew/bin/cmake",
        "/usr/local/bin/cmake",
        "/usr/bin/cmake",
    };
    for (const auto &path : candidates) {
        if (QFileInfo::exists(path))
            return path;
    }

    // Try PATH lookup
    QString found = QStandardPaths::findExecutable("cmake");
    if (!found.isEmpty()) return found;

    return "cmake"; // fallback
}

bool BuildPanel::DetectCmake() {
    cmake_path_ = cmake_path_edit_->text().trimmed();
    if (cmake_path_.isEmpty()) {
        cmake_path_ = FindCmake();
        cmake_path_edit_->setText(cmake_path_);
    }

    QProcess proc;
    proc.start(cmake_path_, {"--version"});
    if (!proc.waitForFinished(5000) || proc.exitCode() != 0) {
        emit StatusMessage("CMake not found at: " + cmake_path_);
        return false;
    }
    return true;
}

// ============================================================================
// Configure Slots
// ============================================================================

void BuildPanel::OnConfigure() {
    if (!DetectCmake()) return;

    // Resolve build dir
    QString build_rel = build_dir_edit_->text().trimmed();
    if (build_rel.isEmpty()) build_rel = "build";
    if (QDir::isRelativePath(build_rel)) {
        build_dir_ = project_path_ + "/" + build_rel;
    } else {
        build_dir_ = build_rel;
    }

    QDir().mkpath(build_dir_);

    QStringList args;
    args << "-S" << project_path_ << "-B" << build_dir_;

    // Generator
    QString generator = generator_combo_->currentText();
    if (generator != "Default") {
        args << "-G" << generator;
    }

    // Build type
    args << ("-DCMAKE_BUILD_TYPE=" + build_type_combo_->currentText());

    // Extra args
    QString extra = extra_cmake_args_edit_->text().trimmed();
    if (!extra.isEmpty()) {
        args << extra.split(' ', Qt::SkipEmptyParts);
    }

    // Clear output and switch to output tab
    build_output_->clear();
    tabs_->setCurrentWidget(build_output_);
    build_output_->appendPlainText(">>> " + cmake_path_ + " " + args.join(" ") + "\n");

    // Start configure
    if (configure_process_ && configure_process_->state() != QProcess::NotRunning) {
        configure_process_->kill();
        configure_process_->waitForFinished(2000);
    }

    configure_process_ = new QProcess(this);
    configure_process_->setWorkingDirectory(project_path_);
    configure_process_->setProcessChannelMode(QProcess::MergedChannels);

    connect(configure_process_, &QProcess::readyReadStandardOutput,
            this, &BuildPanel::OnConfigureReadyRead);
    connect(configure_process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &BuildPanel::OnConfigureFinished);

    build_progress_->setVisible(true);
    build_status_label_->setText("Configuring...");
    emit StatusMessage("Configure started");

    configure_process_->start(cmake_path_, args);
}

void BuildPanel::OnReconfigure() {
    // Delete CMake cache and re-configure
    QString cache_file = build_dir_ + "/CMakeCache.txt";
    if (QFileInfo::exists(cache_file)) {
        QFile::remove(cache_file);
    }
    // Also remove CMakeFiles directory
    QDir cmake_files(build_dir_ + "/CMakeFiles");
    if (cmake_files.exists()) {
        cmake_files.removeRecursively();
    }
    OnConfigure();
}

void BuildPanel::OnClean() {
    if (build_dir_.isEmpty() || !QDir(build_dir_).exists()) {
        emit StatusMessage("Build directory does not exist");
        return;
    }

    build_output_->clear();
    tabs_->setCurrentWidget(build_output_);
    build_output_->appendPlainText(">>> cmake --build " + build_dir_ + " --target clean\n");

    if (build_process_ && build_process_->state() != QProcess::NotRunning) {
        build_process_->kill();
        build_process_->waitForFinished(2000);
    }

    build_process_ = new QProcess(this);
    build_process_->setWorkingDirectory(build_dir_);
    build_process_->setProcessChannelMode(QProcess::MergedChannels);

    connect(build_process_, &QProcess::readyReadStandardOutput,
            this, &BuildPanel::OnBuildReadyRead);
    connect(build_process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exit_code, QProcess::ExitStatus) {
        build_progress_->setVisible(false);
        if (exit_code == 0) {
            build_status_label_->setText("Clean completed");
            emit StatusMessage("Clean completed");
        } else {
            build_status_label_->setText("Clean failed");
            emit StatusMessage("Clean failed");
        }
        build_process_->deleteLater();
        build_process_ = nullptr;
    });

    build_progress_->setVisible(true);
    build_status_label_->setText("Cleaning...");

    build_process_->start(cmake_path_, {"--build", build_dir_, "--target", "clean"});
}

// ============================================================================
// Build Slots
// ============================================================================

void BuildPanel::OnBuild() {
    if (build_dir_.isEmpty()) {
        emit StatusMessage("No build directory configured. Run Configure first.");
        return;
    }

    build_output_->clear();
    tabs_->setCurrentWidget(build_output_);
    build_error_count_ = 0;
    build_warning_count_ = 0;

    QStringList args = {"--build", build_dir_, "--parallel"};

    build_output_->appendPlainText(">>> cmake " + args.join(" ") + "\n");

    if (build_process_ && build_process_->state() != QProcess::NotRunning) {
        build_process_->kill();
        build_process_->waitForFinished(2000);
    }

    build_process_ = new QProcess(this);
    build_process_->setWorkingDirectory(build_dir_);
    build_process_->setProcessChannelMode(QProcess::MergedChannels);

    connect(build_process_, &QProcess::readyReadStandardOutput,
            this, &BuildPanel::OnBuildReadyRead);
    connect(build_process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &BuildPanel::OnBuildFinished);

    build_progress_->setVisible(true);
    action_stop_->setEnabled(true);
    action_build_->setEnabled(false);
    build_status_label_->setText("Building...");
    emit StatusMessage("Build started");

    build_process_->start(cmake_path_, args);
}

void BuildPanel::OnBuildTarget() {
    auto *item = target_tree_->currentItem();
    if (!item) {
        emit StatusMessage("Select a target first");
        return;
    }

    QString target_name = item->text(0);

    build_output_->clear();
    tabs_->setCurrentWidget(build_output_);
    build_error_count_ = 0;
    build_warning_count_ = 0;

    QStringList args = {"--build", build_dir_, "--target", target_name, "--parallel"};
    build_output_->appendPlainText(">>> cmake " + args.join(" ") + "\n");

    if (build_process_ && build_process_->state() != QProcess::NotRunning) {
        build_process_->kill();
        build_process_->waitForFinished(2000);
    }

    build_process_ = new QProcess(this);
    build_process_->setWorkingDirectory(build_dir_);
    build_process_->setProcessChannelMode(QProcess::MergedChannels);

    connect(build_process_, &QProcess::readyReadStandardOutput,
            this, &BuildPanel::OnBuildReadyRead);
    connect(build_process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &BuildPanel::OnBuildFinished);

    build_progress_->setVisible(true);
    action_stop_->setEnabled(true);
    action_build_->setEnabled(false);
    build_status_label_->setText("Building " + target_name + "...");
    emit StatusMessage("Building target: " + target_name);

    build_process_->start(cmake_path_, args);
}

void BuildPanel::OnRebuild() {
    OnClean();
    // Start build after clean; connect as one-shot
    if (build_process_) {
        connect(build_process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this]() { OnBuild(); }, Qt::SingleShotConnection);
    } else {
        OnBuild();
    }
}

void BuildPanel::OnStopBuild() {
    if (build_process_ && build_process_->state() != QProcess::NotRunning) {
        build_process_->kill();
        build_status_label_->setText("Build stopped");
        emit StatusMessage("Build stopped by user");
    }
}

// ============================================================================
// Target Slots
// ============================================================================

void BuildPanel::OnRefreshTargets() {
    DetectTargets();
}

void BuildPanel::OnTargetDoubleClicked(QTreeWidgetItem * /*item*/, int /*column*/) {
    OnBuildTarget();
}

void BuildPanel::DetectTargets() {
    targets_.clear();
    target_tree_->clear();

    if (build_dir_.isEmpty()) return;

    // Method 1: Use cmake --build --target help to list targets
    QProcess proc;
    proc.setWorkingDirectory(build_dir_);
    proc.start(cmake_path_, {"--build", build_dir_, "--target", "help"});

    if (!proc.waitForFinished(10000)) return;

    QString output = QString::fromUtf8(proc.readAllStandardOutput());
    const QStringList lines = output.split('\n', Qt::SkipEmptyParts);

    // Parse "... <target_name>" lines (Makefile generator format)
    static const QRegularExpression target_re(R"(^\.\.\.\s+(\S+))");

    for (const QString &line : lines) {
        auto match = target_re.match(line);
        if (match.hasMatch()) {
            QString name = match.captured(1);
            // Skip meta targets
            if (name == "all" || name == "clean" || name == "depend" ||
                name == "edit_cache" || name == "rebuild_cache" || name == "help")
                continue;

            BuildTarget target;
            target.name = name;

            // Infer type from common naming patterns
            if (name.endsWith("_autogen") || name.endsWith("_autogen_timestamp_deps"))
                continue; // Skip autogen targets
            if (name.startsWith("lib") || name.endsWith("_lib"))
                target.type = "LIBRARY";
            else
                target.type = "TARGET";

            targets_.push_back(target);

            auto *item = new QTreeWidgetItem({target.name, target.type});
            item->setForeground(0, QColor("#569cd6"));
            target_tree_->addTopLevelItem(item);
        }
    }

    // If no targets found from help, try parsing CMakeCache.txt
    if (targets_.empty()) {
        // Fallback: look for executables or libraries in build dir
        emit StatusMessage(QString("Found 0 targets. Run Configure first."));
    } else {
        emit StatusMessage(QString("Found %1 targets").arg(targets_.size()));
    }
}

// ============================================================================
// Process I/O Handlers
// ============================================================================

void BuildPanel::OnConfigureReadyRead() {
    if (!configure_process_) return;
    QString text = QString::fromUtf8(configure_process_->readAllStandardOutput());
    build_output_->appendPlainText(text);
}

void BuildPanel::OnConfigureFinished(int exit_code, QProcess::ExitStatus /*status*/) {
    build_progress_->setVisible(false);

    if (exit_code == 0) {
        build_status_label_->setText("Configure completed successfully");
        build_output_->appendPlainText("\n--- Configure completed successfully ---\n");
        emit ConfigureFinished(true);
        emit StatusMessage("Configure succeeded");
        // Auto-detect targets after successful configure
        DetectTargets();
    } else {
        build_status_label_->setText("Configure failed (exit code " + QString::number(exit_code) + ")");
        build_output_->appendPlainText("\n--- Configure FAILED ---\n");
        emit ConfigureFinished(false);
        emit StatusMessage("Configure failed");
    }

    configure_process_->deleteLater();
    configure_process_ = nullptr;
}

void BuildPanel::OnBuildReadyRead() {
    if (!build_process_) return;
    QString text = QString::fromUtf8(build_process_->readAllStandardOutput());

    // Parse each line for errors/warnings
    const QStringList lines = text.split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        ParseBuildOutput(line);
    }

    build_output_->appendPlainText(text);

    // Auto-scroll to bottom
    auto *scrollbar = build_output_->verticalScrollBar();
    if (scrollbar) scrollbar->setValue(scrollbar->maximum());
}

void BuildPanel::OnBuildFinished(int exit_code, QProcess::ExitStatus /*status*/) {
    build_progress_->setVisible(false);
    action_stop_->setEnabled(false);
    action_build_->setEnabled(true);

    QString summary = QString("\n--- Build %1 (%2 errors, %3 warnings) ---\n")
        .arg(exit_code == 0 ? "completed" : "FAILED")
        .arg(build_error_count_)
        .arg(build_warning_count_);

    build_output_->appendPlainText(summary);

    if (exit_code == 0) {
        build_status_label_->setText(
            QString("Build succeeded (%1 warnings)").arg(build_warning_count_));
    } else {
        build_status_label_->setText(
            QString("Build failed (%1 errors, %2 warnings)")
                .arg(build_error_count_).arg(build_warning_count_));
    }

    emit BuildFinished(exit_code == 0, build_output_->toPlainText());
    emit StatusMessage(exit_code == 0 ? "Build succeeded" : "Build failed");

    build_process_->deleteLater();
    build_process_ = nullptr;
}

void BuildPanel::ParseBuildOutput(const QString &line) {
    // Match GCC/Clang error format:  file:line:col: error: message
    static const QRegularExpression error_re(
        R"(^(.+?):(\d+):\d+:\s*error:\s*(.+)$)");
    static const QRegularExpression warning_re(
        R"(^(.+?):(\d+):\d+:\s*warning:\s*(.+)$)");

    auto error_match = error_re.match(line);
    if (error_match.hasMatch()) {
        ++build_error_count_;
        emit BuildErrorFound(error_match.captured(1),
                             error_match.captured(2).toInt(),
                             error_match.captured(3));
        return;
    }

    auto warning_match = warning_re.match(line);
    if (warning_match.hasMatch()) {
        ++build_warning_count_;
        return;
    }

    // Match MSVC format:  file(line): error Cxxxx: message
    static const QRegularExpression msvc_error_re(
        R"(^(.+?)\((\d+)\):\s*error\s+\w+:\s*(.+)$)");
    auto msvc_match = msvc_error_re.match(line);
    if (msvc_match.hasMatch()) {
        ++build_error_count_;
        emit BuildErrorFound(msvc_match.captured(1),
                             msvc_match.captured(2).toInt(),
                             msvc_match.captured(3));
    }
}

// ============================================================================
// Settings Slots
// ============================================================================

void BuildPanel::OnGeneratorChanged(int /*index*/) {
    // Nothing to do until next configure
}

void BuildPanel::OnBuildTypeChanged(int /*index*/) {
    // Nothing to do until next configure
}

void BuildPanel::OnBrowseBuildDir() {
    QString dir = QFileDialog::getExistingDirectory(
        this, "Select Build Directory",
        project_path_.isEmpty() ? QDir::homePath() : project_path_);
    if (!dir.isEmpty()) {
        build_dir_ = dir;
        if (project_path_.isEmpty() || !dir.startsWith(project_path_)) {
            build_dir_edit_->setText(dir);
        } else {
            build_dir_edit_->setText(QDir(project_path_).relativeFilePath(dir));
        }
    }
}

void BuildPanel::OnBrowseCmakePath() {
    QString file = QFileDialog::getOpenFileName(
        this, "Select CMake Executable",
        "/usr/local/bin", "Executables (*)");
    if (!file.isEmpty()) {
        cmake_path_ = file;
        cmake_path_edit_->setText(file);
    }
}

} // namespace polyglot::tools::ui
