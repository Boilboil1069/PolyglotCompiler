/**
 * @file     lsp_bridge.h
 * @brief    Qt-side glue between polyui editors and LSP language servers
 *
 * Owns one @ref polyglot::tools::ui::lsp::LspSessionRegistry, builds
 * stdio-backed transports via QProcess, and connects @ref CodeEditor
 * change events to debounced `didChange` notifications.  Inbound
 * `publishDiagnostics` are routed to the corresponding editor's
 * @ref CodeEditor::SetDiagnostics gutter overlay.
 *
 * Implements demand 2026-04-28-19 §3 and the runtime side of §4.
 *
 * @ingroup  Tool / polyui / LSP
 * @author   Manning Cyrus
 * @date     2026-05-04
 */
#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QTimer>

#include <functional>
#include <memory>

#include "tools/ui/common/lsp/lsp_client.h"
#include "tools/ui/common/lsp/lsp_session.h"

namespace polyglot::tools::ui {

class CodeEditor;
class SettingsService;
class ProblemsAggregator;

namespace lsp {
class LspLogPanel;
}

/// QProcess-backed implementation of @ref lsp::ILspTransport.  Spawns
/// the configured language-server executable and wires its stdout/stdin
/// to the LSP receive callback / Send() entry point.
class StdioTransport : public QObject, public lsp::ILspTransport {
  Q_OBJECT

 public:
  explicit StdioTransport(QObject *parent = nullptr);
  ~StdioTransport() override;

  /// Spawn @p command with @p args.  Returns true on success.
  bool Start(const QString &command, const QStringList &args,
             const QProcessEnvironment &env);

  // ── ILspTransport ──────────────────────────────────────────────────
  void Send(const std::string &bytes) override;
  void SetOnReceive(ReceiveCallback cb) override;
  bool IsOpen() const override;
  void Close() override;

 signals:
  void StartFailed(const QString &reason);

 private slots:
  void OnReadyReadStdout();

 private:
  QProcess *process_{nullptr};
  ReceiveCallback on_receive_;
};

/// Per-editor change-debounce + LSP wiring.  One bridge instance
/// suffices for the whole IDE.
class IdeLspBridge : public QObject {
  Q_OBJECT

 public:
  IdeLspBridge(SettingsService *settings, lsp::LspLogPanel *log_panel,
               QObject *parent = nullptr);
  ~IdeLspBridge() override;

  /// Begin tracking @p editor.  The bridge issues `didOpen` immediately
  /// and forwards subsequent edits as debounced `didChange`s.  No-op
  /// when language servers are disabled or no server is configured for
  /// @p language_id.
  void TrackEditor(CodeEditor *editor, const QString &language_id);

  /// Forward `didSave` for @p editor.
  void NotifySaved(CodeEditor *editor);

  /// Stop tracking @p editor and emit `didClose`.
  void Untrack(CodeEditor *editor);

  /// Tear down all sessions (called from MainWindow destructor).
  void Shutdown();

  /// Optional sink for diagnostics published by language servers.  When
  /// set, every `publishDiagnostics` is mirrored into the aggregator
  /// under source `"polyls:<language_id>"` so the Problems Panel and
  /// status-bar counter stay in sync with the editor gutter.  The
  /// aggregator is owned by the host (MainWindow); the bridge stores
  /// only an observer pointer.
  void SetProblemsAggregator(ProblemsAggregator *aggregator) {
    problems_aggregator_ = aggregator;
  }

  // ── Language features (demand 2026-04-28-21) ─────────────────────────
  using CompletionCallback =
      std::function<void(const lsp::Json & /*items_array*/)>;
  using HoverCallback = std::function<void(const lsp::Json & /*hover_or_null*/)>;
  using SignatureHelpCallback =
      std::function<void(const lsp::Json & /*help_or_null*/)>;

  /// Issue `textDocument/completion` for @p editor at the given 0-based
  /// position.  The callback is invoked on the Qt main thread with the
  /// raw JSON array (CompletionItem[]) returned by the server, or with
  /// an empty array if no session is available.
  void RequestCompletion(CodeEditor *editor, int line, int character,
                         CompletionCallback cb);

  /// Issue `textDocument/hover`.  The callback receives a Hover JSON
  /// object or null when the server has nothing to say at the position.
  void RequestHover(CodeEditor *editor, int line, int character,
                    HoverCallback cb);

  /// Issue `textDocument/signatureHelp`.  The callback receives a
  /// SignatureHelp JSON object or null.
  void RequestSignatureHelp(CodeEditor *editor, int line, int character,
                            SignatureHelpCallback cb);

  lsp::LspSessionRegistry &Sessions() { return sessions_; }

 private:
  struct EditorState {
    QPointer<CodeEditor> editor;
    QString language_id;
    QString uri;
    std::int32_t version{1};
    QTimer *debounce{nullptr};
    std::shared_ptr<lsp::LspSession> session;
  };

  std::shared_ptr<lsp::LspSession> EnsureSession(const QString &language_id);
  static QString FilePathToUri(const QString &path);
  void PublishDiagnosticsToEditor(CodeEditor *editor,
                                  const lsp::PublishDiagnosticsParams &params);

  SettingsService *settings_{nullptr};
  lsp::LspLogPanel *log_panel_{nullptr};
  lsp::LspSessionRegistry sessions_;
  QHash<CodeEditor *, EditorState *> editors_;
  ProblemsAggregator *problems_aggregator_{nullptr};
};

}  // namespace polyglot::tools::ui
