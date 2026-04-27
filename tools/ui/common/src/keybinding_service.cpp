/**
 * @file     keybinding_service.cpp
 * @brief    KeybindingService implementation
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-04-27
 */
#include "tools/ui/common/include/keybinding_service.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>

#include "tools/common/include/effective_settings_loader.h"

namespace polyglot::tools::ui {

namespace pcommon = polyglot::tools::common;

KeybindingService &KeybindingService::Instance() {
  static KeybindingService instance;
  return instance;
}

KeybindingService::KeybindingService() : QObject(nullptr) {}
KeybindingService::~KeybindingService() = default;

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

void KeybindingService::RegisterCommand(const QString &command,
                                        std::function<void()> handler,
                                        const QString &title) {
  CommandInfo info;
  info.handler = std::move(handler);
  info.title = title.isEmpty() ? command : title;
  commands_.insert(command, info);
}

QString KeybindingService::CommandTitle(const QString &command) const {
  auto it = commands_.find(command);
  return it == commands_.end() ? command : it->title;
}

QStringList KeybindingService::AllCommands() const { return commands_.keys(); }

bool KeybindingService::HasCommand(const QString &command) const {
  return commands_.contains(command);
}

void KeybindingService::Run(const QString &command) const {
  auto it = commands_.find(command);
  if (it != commands_.end() && it->handler) it->handler();
}

// ---------------------------------------------------------------------------
// Bindings
// ---------------------------------------------------------------------------

void KeybindingService::AddDefaultBinding(const QString &command, const QString &key,
                                          const QString &when) {
  defaults_.push_back({command, key, when, "default"});
  emit bindingsChanged();
}

void KeybindingService::AddUserBinding(const QString &command, const QString &key,
                                       const QString &when) {
  users_.push_back({command, key, when, "user"});
  emit bindingsChanged();
}

QVector<Keybinding> KeybindingService::EffectiveBindings() const {
  QVector<Keybinding> out = defaults_;
  // User bindings override / append.
  for (const auto &u : users_) out.push_back(u);
  return out;
}

QString KeybindingService::KeyForCommand(const QString &command) const {
  // User wins over default; later wins within a layer.
  QString key;
  for (const auto &b : defaults_) {
    if (b.command == command && (b.when.isEmpty() || EvaluateWhen(b.when))) key = b.key;
  }
  for (const auto &b : users_) {
    if (b.command == command && (b.when.isEmpty() || EvaluateWhen(b.when))) key = b.key;
  }
  return key;
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

void KeybindingService::LoadUserKeybindings() {
  users_.clear();
  const QString path = QString::fromStdString(pcommon::UserKeybindingsPath().string());
  if (!QFileInfo::exists(path)) {
    emit bindingsChanged();
    return;
  }
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
  QJsonParseError err;
  const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
  if (err.error != QJsonParseError::NoError || !doc.isArray()) return;
  for (const auto &v : doc.array()) {
    if (!v.isObject()) continue;
    const auto o = v.toObject();
    Keybinding kb;
    kb.key = o.value("key").toString();
    kb.command = o.value("command").toString();
    kb.when = o.value("when").toString();
    kb.source = "user";
    if (!kb.command.isEmpty() && !kb.key.isEmpty()) users_.push_back(kb);
  }
  emit bindingsChanged();
}

void KeybindingService::SaveUserKeybindings() const {
  const QString path = QString::fromStdString(pcommon::UserKeybindingsPath().string());
  QDir().mkpath(QFileInfo(path).absolutePath());
  QJsonArray arr;
  for (const auto &b : users_) {
    QJsonObject o;
    o.insert("key", b.key);
    o.insert("command", b.command);
    if (!b.when.isEmpty()) o.insert("when", b.when);
    arr.append(o);
  }
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return;
  f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
}

// ---------------------------------------------------------------------------
// Chord parsing
// ---------------------------------------------------------------------------

QStringList KeybindingService::ParseChord(const QString &combo) {
  QStringList parts = combo.split(' ', Qt::SkipEmptyParts);
  for (auto &p : parts) p = p.trimmed();
  return parts;
}

// ---------------------------------------------------------------------------
// Context + when-expression evaluation
// ---------------------------------------------------------------------------

void KeybindingService::SetContext(const QString &name, bool value) {
  context_[name] = value;
}
bool KeybindingService::Context(const QString &name) const {
  return context_.value(name, false);
}

namespace {

// Tiny recursive-descent evaluator for: expr := or; or := and ('||' and)*;
// and := unary ('&&' unary)*; unary := '!'? primary; primary := IDENT | '(' expr ')'.
struct WhenParser {
  QString src;
  int pos{0};
  const QHash<QString, bool> *ctx;

  void Skip() { while (pos < src.size() && src[pos].isSpace()) ++pos; }
  bool Match(QChar c) {
    Skip();
    if (pos < src.size() && src[pos] == c) { ++pos; return true; }
    return false;
  }
  bool Match2(QChar a, QChar b) {
    Skip();
    if (pos + 1 < src.size() && src[pos] == a && src[pos + 1] == b) { pos += 2; return true; }
    return false;
  }
  bool ParseExpr() { return ParseOr(); }
  bool ParseOr() {
    bool v = ParseAnd();
    while (Match2('|', '|')) v = ParseAnd() || v;  // short-circuit OK in either order
    return v;
  }
  bool ParseAnd() {
    bool v = ParseUnary();
    while (Match2('&', '&')) v = ParseUnary() && v;
    return v;
  }
  bool ParseUnary() {
    Skip();
    if (Match('!')) return !ParseUnary();
    return ParsePrimary();
  }
  bool ParsePrimary() {
    Skip();
    if (Match('(')) {
      bool v = ParseExpr();
      Match(')');
      return v;
    }
    QString id;
    while (pos < src.size() && (src[pos].isLetterOrNumber() || src[pos] == '.' || src[pos] == '_')) {
      id += src[pos++];
    }
    if (id.isEmpty()) return false;
    return ctx->value(id, false);
  }
};

}  // namespace

bool KeybindingService::EvaluateWhen(const QString &expr) const {
  if (expr.trimmed().isEmpty()) return true;
  WhenParser p{expr, 0, &context_};
  return p.ParseExpr();
}

bool KeybindingService::Dispatch(const QString &chord) const {
  // Walk effective bindings from the back so user bindings win.
  const auto bindings = EffectiveBindings();
  for (int i = bindings.size() - 1; i >= 0; --i) {
    const auto &b = bindings[i];
    if (b.key == chord && (b.when.isEmpty() || EvaluateWhen(b.when))) {
      if (HasCommand(b.command)) {
        Run(b.command);
        return true;
      }
    }
  }
  return false;
}

}  // namespace polyglot::tools::ui
