/**
 * @file     lsp_bridge.cpp
 * @brief    Implementation of @ref polyglot::tools::ui::IdeLspBridge
 * @ingroup  Tool / polyui / LSP
 * @author   Manning Cyrus
 * @date     2026-05-04
 */
#include "tools/ui/common/include/lsp_bridge.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QMainWindow>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStringList>
#include <QUrl>

#include <utility>

#include "tools/ui/common/include/code_editor.h"
#include "tools/ui/common/include/compiler_service.h"
#include "tools/ui/common/include/problems_aggregator.h"
#include "tools/ui/common/include/settings_service.h"
#include "tools/ui/common/lsp/lsp_log_panel.h"

namespace polyglot::tools::ui {

// ============================================================================
// StdioTransport
// ============================================================================

StdioTransport::StdioTransport(QObject *parent) : QObject(parent) {}

StdioTransport::~StdioTransport() { Close(); }

bool StdioTransport::Start(const QString &command, const QStringList &args,
                           const QProcessEnvironment &env) {
  if (process_) {
    return false;
  }
  // Resolve the command on PATH so we can fail fast (and emit a friendly
  // notification) when the user-configured server is missing.
  const QString resolved = QStandardPaths::findExecutable(command);
  if (resolved.isEmpty()) {
    emit StartFailed(QStringLiteral("Executable not found on PATH: %1").arg(command));
    return false;
  }
  process_ = new QProcess(this);
  process_->setProcessEnvironment(env);
  process_->setProcessChannelMode(QProcess::SeparateChannels);
  connect(process_, &QProcess::readyReadStandardOutput, this,
          &StdioTransport::OnReadyReadStdout);
  connect(process_, &QProcess::errorOccurred, this,
          [this, command](QProcess::ProcessError) {
            emit StartFailed(QStringLiteral("Process error launching %1: %2")
                                 .arg(command, process_ ? process_->errorString() : QString()));
          });
  process_->start(resolved, args);
  if (!process_->waitForStarted(5000)) {
    emit StartFailed(QStringLiteral("Process did not start within 5s: %1").arg(command));
    process_->deleteLater();
    process_ = nullptr;
    return false;
  }
  return true;
}

void StdioTransport::Send(const std::string &bytes) {
  if (!process_ || process_->state() != QProcess::Running) {
    return;
  }
  process_->write(bytes.data(), static_cast<qint64>(bytes.size()));
}

void StdioTransport::SetOnReceive(ReceiveCallback cb) {
  on_receive_ = std::move(cb);
}

bool StdioTransport::IsOpen() const {
  return process_ && process_->state() == QProcess::Running;
}

void StdioTransport::Close() {
  if (!process_) return;
  if (process_->state() == QProcess::Running) {
    process_->closeWriteChannel();
    if (!process_->waitForFinished(2000)) {
      process_->kill();
      process_->waitForFinished(1000);
    }
  }
  process_->deleteLater();
  process_ = nullptr;
}

void StdioTransport::OnReadyReadStdout() {
  if (!process_ || !on_receive_) return;
  const QByteArray chunk = process_->readAllStandardOutput();
  if (chunk.isEmpty()) return;
  on_receive_(std::string(chunk.constData(), static_cast<std::size_t>(chunk.size())));
}

// ============================================================================
// IdeLspBridge
// ============================================================================

IdeLspBridge::IdeLspBridge(SettingsService *settings, lsp::LspLogPanel *log_panel,
                           QObject *parent)
    : QObject(parent), settings_(settings), log_panel_(log_panel) {}

IdeLspBridge::~IdeLspBridge() { Shutdown(); }

void IdeLspBridge::Shutdown() {
  for (auto it = editors_.begin(); it != editors_.end(); ++it) {
    delete it.value();
  }
  editors_.clear();
  sessions_.DropAll();
}

QString IdeLspBridge::FilePathToUri(const QString &path) {
  if (path.isEmpty()) return {};
  return QUrl::fromLocalFile(path).toString();
}

std::shared_ptr<lsp::LspSession> IdeLspBridge::EnsureSession(const QString &language_id) {
  if (!settings_ || !settings_->GetBool(QStringLiteral("languageServers.enabled"), true)) {
    return nullptr;
  }
  const QString workspace_root = settings_ ? settings_->WorkspaceRoot() : QString();
  const QString workspace_uri = workspace_root.isEmpty()
                                    ? QStringLiteral("file:///")
                                    : FilePathToUri(workspace_root);

  lsp::SessionKey key{workspace_uri.toStdString(), language_id.toStdString()};
  if (auto existing = sessions_.Find(key)) {
    return existing;
  }

  // Lookup configured server for this language.
  const QString cfg_key = QStringLiteral("languageServers.servers.%1").arg(language_id);
  const QJsonValue cfg = settings_->GetJson(cfg_key);
  if (!cfg.isObject()) {
    return nullptr;
  }
  const QJsonObject obj = cfg.toObject();
  const QString command = obj.value("command").toString();
  if (command.isEmpty()) return nullptr;
  QStringList args;
  for (const QJsonValue &v : obj.value("args").toArray()) {
    args << v.toString();
  }
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  const QJsonObject env_obj = obj.value("env").toObject();
  for (auto eit = env_obj.constBegin(); eit != env_obj.constEnd(); ++eit) {
    env.insert(eit.key(), eit.value().toString());
  }

  // Build transport; bail (with one-shot user notification) if it can't start.
  auto transport = std::make_shared<StdioTransport>();
  bool start_failed = false;
  QString start_error;
  connect(transport.get(), &StdioTransport::StartFailed, this,
          [&start_failed, &start_error](const QString &reason) {
            start_failed = true;
            start_error = reason;
          });
  if (!transport->Start(command, args, env) || start_failed) {
    if (auto *parent_widget = qobject_cast<QWidget *>(parent())) {
      if (auto *mw = qobject_cast<QMainWindow *>(parent_widget->window())) {
        if (mw->statusBar()) {
          mw->statusBar()->showMessage(
              QStringLiteral("[LSP] %1 — server disabled for this session.")
                  .arg(start_error.isEmpty() ? command : start_error),
              8000);
        }
      }
    }
    return nullptr;
  }

  // Build the session.
  auto session_id_str = workspace_uri.toStdString() + "|" + language_id.toStdString();
  auto session = sessions_.GetOrCreate(key, [transport]() {
    return std::make_shared<lsp::LspClient>(transport);
  });
  if (!session) return nullptr;

  // Wire log panel.
  if (log_panel_) {
    auto panel = log_panel_;
    auto sid = session->id;
    session->client->SetLogHandler([panel, sid](const std::string &dir, const lsp::Json &payload) {
      lsp::LogEntry e;
      e.direction = dir;
      e.session_id = sid;
      if (payload.contains("method") && payload["method"].is_string()) {
        e.method = payload["method"].get<std::string>();
      }
      if (payload.contains("id")) {
        e.kind = payload.contains("method") ? "request" : "response";
      } else {
        e.kind = "notification";
      }
      e.payload = payload;
      panel->Append(e);
    });
  }

  // Wire publishDiagnostics → editor gutter (resolved by uri at dispatch time).
  QPointer<IdeLspBridge> self(this);
  session->client->OnPublishDiagnostics(
      [self](const lsp::PublishDiagnosticsParams &params) {
        if (!self) return;
        // Find the editor whose uri matches.
        for (auto it = self->editors_.constBegin(); it != self->editors_.constEnd(); ++it) {
          if (it.value()->uri.toStdString() == params.uri && it.value()->editor) {
            self->PublishDiagnosticsToEditor(it.value()->editor, params);
            break;
          }
        }
      });

  // Initialize handshake.
  lsp::InitializeParams ip;
  ip.process_id = static_cast<int>(QCoreApplication::applicationPid());
  ip.root_uri = workspace_uri.toStdString();
  auto sess_ptr = session;
  session->client->Initialize(ip, [sess_ptr](const lsp::Json &result, const lsp::Json &error) {
    (void)result;
    (void)error;
    if (error.is_null()) {
      sess_ptr->initialized = true;
      sess_ptr->client->Initialized();
    }
  });

  return session;
}

void IdeLspBridge::PublishDiagnosticsToEditor(CodeEditor *editor,
                                              const lsp::PublishDiagnosticsParams &params) {
  if (!editor) return;
  std::vector<DiagnosticInfo> ui_diags;
  ui_diags.reserve(params.diagnostics.size());
  for (const auto &d : params.diagnostics) {
    DiagnosticInfo info;
    info.line = static_cast<size_t>(d.range.start.line) + 1;
    info.column = static_cast<size_t>(d.range.start.character) + 1;
    info.end_line = static_cast<size_t>(d.range.end.line) + 1;
    info.end_column = static_cast<size_t>(d.range.end.character) + 1;
    switch (d.severity) {
      case lsp::DiagnosticSeverity::kError:       info.severity = "error";   break;
      case lsp::DiagnosticSeverity::kWarning:     info.severity = "warning"; break;
      case lsp::DiagnosticSeverity::kInformation: info.severity = "note";    break;
      case lsp::DiagnosticSeverity::kHint:        info.severity = "note";    break;
    }
    info.message = d.message;
    info.code = d.code;
    ui_diags.push_back(std::move(info));
  }
  editor->SetDiagnostics(ui_diags);

  // Mirror to the workspace-wide Problems aggregator (if installed).
  if (problems_aggregator_) {
    QString file_key;
    QString language_label = QStringLiteral("lsp");
    if (auto it = editors_.find(editor); it != editors_.end() && it.value()) {
      file_key = it.value()->uri;
      if (!it.value()->language_id.isEmpty()) {
        language_label = it.value()->language_id;
      }
    }
    if (file_key.isEmpty()) {
      file_key = editor->FilePath();
    }
    const std::string source_label =
        std::string("polyls:") + language_label.toStdString();
    problems_aggregator_->ReplaceFromDiagnosticInfo(
        file_key.toStdString(), source_label, ui_diags);
  }
}

void IdeLspBridge::TrackEditor(CodeEditor *editor, const QString &language_id) {
  if (!editor) return;
  if (editors_.contains(editor)) return;
  auto session = EnsureSession(language_id);
  if (!session) return;

  auto *state = new EditorState();
  state->editor = editor;
  state->language_id = language_id;
  state->uri = FilePathToUri(editor->FilePath());
  state->session = session;
  state->debounce = new QTimer(this);
  state->debounce->setSingleShot(true);
  const int debounce_ms = settings_
                              ? settings_->GetInt(QStringLiteral("languageServers.changeDebounceMs"), 200)
                              : 200;
  state->debounce->setInterval(debounce_ms);
  editors_.insert(editor, state);

  // Send didOpen with current text (synthesised "untitled" uri when path empty).
  if (state->uri.isEmpty()) {
    state->uri = QStringLiteral("untitled:%1").arg(reinterpret_cast<quintptr>(editor));
  }
  lsp::DidOpenParams open;
  open.text_document.uri = state->uri.toStdString();
  open.text_document.language_id = language_id.toStdString();
  open.text_document.version = state->version;
  open.text_document.text = editor->toPlainText().toStdString();
  session->client->DidOpen(open);

  // Debounced didChange (full sync — textDocumentSync=1).
  QPointer<CodeEditor> editor_ptr(editor);
  connect(state->debounce, &QTimer::timeout, this, [this, editor_ptr]() {
    if (!editor_ptr) return;
    auto it = editors_.find(editor_ptr);
    if (it == editors_.end()) return;
    EditorState *s = it.value();
    if (!s->session || !s->session->client) return;
    lsp::DidChangeParams ch;
    ch.text_document.uri = s->uri.toStdString();
    ch.text_document.version = ++s->version;
    lsp::TextDocumentContentChangeEvent ev;
    ev.text = editor_ptr->toPlainText().toStdString();
    ch.content_changes.push_back(std::move(ev));
    s->session->client->DidChange(ch);
  });

  connect(editor->document(), &QTextDocument::contentsChanged, state->debounce,
          QOverload<>::of(&QTimer::start));
}

void IdeLspBridge::NotifySaved(CodeEditor *editor) {
  auto it = editors_.find(editor);
  if (it == editors_.end()) return;
  EditorState *s = it.value();
  if (!s->session || !s->session->client) return;
  lsp::DidSaveParams sv;
  sv.text_document.uri = s->uri.toStdString();
  sv.text = editor->toPlainText().toStdString();
  s->session->client->DidSave(sv);
}

void IdeLspBridge::Untrack(CodeEditor *editor) {
  auto it = editors_.find(editor);
  if (it == editors_.end()) return;
  EditorState *s = it.value();
  if (s->session && s->session->client) {
    lsp::DidCloseParams cl;
    cl.text_document.uri = s->uri.toStdString();
    s->session->client->DidClose(cl);
  }
  delete s;
  editors_.erase(it);
}

// ---------------------------------------------------------------------------
// Language feature requests (demand 2026-04-28-21).  These wrap
// LspClient::SendRequest with a Qt-thread hop so callbacks always run on
// the GUI thread.  Empty / null replies are normalised so that the
// caller never has to guard against missing fields.
// ---------------------------------------------------------------------------

namespace {

lsp::Json MakePositionParams(const QString &uri, int line, int character) {
  return lsp::Json{
      {"textDocument", {{"uri", uri.toStdString()}}},
      {"position",
       {{"line", std::max(0, line)}, {"character", std::max(0, character)}}},
  };
}

}  // namespace

void IdeLspBridge::RequestCompletion(CodeEditor *editor, int line,
                                     int character, CompletionCallback cb) {
  auto it = editors_.find(editor);
  if (it == editors_.end() || !cb) {
    if (cb) cb(lsp::Json::array());
    return;
  }
  EditorState *s = it.value();
  if (!s->session || !s->session->client) {
    cb(lsp::Json::array());
    return;
  }
  const lsp::Json params = MakePositionParams(s->uri, line, character);
  CompletionCallback bridged = std::move(cb);
  // QPointer guards against the editor disappearing while the request
  // is in flight; we only forward the result when both the bridge and
  // the editor are still alive.
  QPointer<CodeEditor> guard(editor);
  QPointer<IdeLspBridge> self(this);
  s->session->client->SendRequest(
      "textDocument/completion", params,
      [self, guard, bridged](const lsp::Json &result, const lsp::Json & /*err*/) {
        if (!self || !guard) return;
        QMetaObject::invokeMethod(self.data(),
                                  [bridged, result]() {
                                    if (result.is_array()) bridged(result);
                                    else bridged(lsp::Json::array());
                                  },
                                  Qt::QueuedConnection);
      });
}

void IdeLspBridge::RequestHover(CodeEditor *editor, int line, int character,
                                HoverCallback cb) {
  auto it = editors_.find(editor);
  if (it == editors_.end() || !cb) {
    if (cb) cb(lsp::Json());
    return;
  }
  EditorState *s = it.value();
  if (!s->session || !s->session->client) {
    cb(lsp::Json());
    return;
  }
  const lsp::Json params = MakePositionParams(s->uri, line, character);
  HoverCallback bridged = std::move(cb);
  QPointer<CodeEditor> guard(editor);
  QPointer<IdeLspBridge> self(this);
  s->session->client->SendRequest(
      "textDocument/hover", params,
      [self, guard, bridged](const lsp::Json &result, const lsp::Json & /*err*/) {
        if (!self || !guard) return;
        QMetaObject::invokeMethod(self.data(),
                                  [bridged, result]() { bridged(result); },
                                  Qt::QueuedConnection);
      });
}

void IdeLspBridge::RequestSignatureHelp(CodeEditor *editor, int line,
                                        int character,
                                        SignatureHelpCallback cb) {
  auto it = editors_.find(editor);
  if (it == editors_.end() || !cb) {
    if (cb) cb(lsp::Json());
    return;
  }
  EditorState *s = it.value();
  if (!s->session || !s->session->client) {
    cb(lsp::Json());
    return;
  }
  const lsp::Json params = MakePositionParams(s->uri, line, character);
  SignatureHelpCallback bridged = std::move(cb);
  QPointer<CodeEditor> guard(editor);
  QPointer<IdeLspBridge> self(this);
  s->session->client->SendRequest(
      "textDocument/signatureHelp", params,
      [self, guard, bridged](const lsp::Json &result, const lsp::Json & /*err*/) {
        if (!self || !guard) return;
        QMetaObject::invokeMethod(self.data(),
                                  [bridged, result]() { bridged(result); },
                                  Qt::QueuedConnection);
      });
}

}  // namespace polyglot::tools::ui
