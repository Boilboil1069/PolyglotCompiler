/**
 * @file     keybinding_service.h
 * @brief    VS Code-style keybinding registry with chord & "when" support
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-04-27
 */
#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace polyglot::tools::ui {

struct Keybinding {
  QString command;            ///< e.g. "workbench.action.files.save"
  QString key;                ///< e.g. "Ctrl+S" or "Ctrl+K Ctrl+S"
  QString when;               ///< optional "when" expression (may be empty)
  QString source;             ///< "default" | "user"
};

/**
 * @brief Registry of commands + their keybindings + "when"-clause evaluator.
 */
class KeybindingService : public QObject {
  Q_OBJECT

 public:
  static KeybindingService &Instance();

  /// Register a runnable command (id → callable).
  void RegisterCommand(const QString &command, std::function<void()> handler,
                       const QString &title = {});
  /// Lookup a registered command's title (or its id if no title).
  QString CommandTitle(const QString &command) const;
  QStringList AllCommands() const;
  bool HasCommand(const QString &command) const;
  void Run(const QString &command) const;

  /// Apply a default-layer keybinding (lowest precedence).
  void AddDefaultBinding(const QString &command, const QString &key,
                         const QString &when = {});

  /// Apply a user-layer keybinding (overrides default).
  void AddUserBinding(const QString &command, const QString &key,
                      const QString &when = {});

  /// All effective keybindings (user merged on top of default).
  QVector<Keybinding> EffectiveBindings() const;

  /// Resolve the key string for a command (best match given context).
  QString KeyForCommand(const QString &command) const;

  /// Load user keybindings from <userConfigDir>/keybindings.json.
  void LoadUserKeybindings();
  /// Persist user keybindings to disk.
  void SaveUserKeybindings() const;

  /// Parse a chord string ("Ctrl+K Ctrl+S") into individual key sequences.
  static QStringList ParseChord(const QString &combo);

  /// Set a context flag (e.g. "editorTextFocus", "debugRunning").
  void SetContext(const QString &name, bool value);
  bool Context(const QString &name) const;

  /// Evaluate a "when" expression against the current context.
  /// Supported grammar: identifiers, '!', '&&', '||', parentheses.
  bool EvaluateWhen(const QString &expr) const;

  /// Try to dispatch a key chord; returns true if a command was run.
  bool Dispatch(const QString &chord) const;

 signals:
  void bindingsChanged();

 private:
  KeybindingService();
  ~KeybindingService() override;

  struct CommandInfo {
    QString title;
    std::function<void()> handler;
  };

  QHash<QString, CommandInfo> commands_;
  QVector<Keybinding> defaults_;
  QVector<Keybinding> users_;
  QHash<QString, bool> context_;
};

}  // namespace polyglot::tools::ui
