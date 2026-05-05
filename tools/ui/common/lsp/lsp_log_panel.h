/**
 * @file     lsp_log_panel.h
 * @brief    LSP traffic viewer — request / response / notification log
 *
 * Qt panel that subscribes to @ref LspClient::SetLogHandler and renders
 * each tx/rx envelope in a filterable list.  Filters: direction (tx/rx)
 * + JSON-RPC kind (request/response/notification) + free-text method
 * substring search.
 *
 * @ingroup  Tool / polyui / LSP
 * @author   Manning Cyrus
 * @date     2026-05-04
 */
#pragma once

#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QWidget>

#include <deque>
#include <string>

#include "tools/ui/common/lsp/lsp_message.h"

namespace polyglot::tools::ui::lsp {

/// Single captured envelope (tx or rx).
struct LogEntry {
  std::string direction;  ///< "tx" or "rx"
  std::string kind;       ///< "request" / "response" / "notification"
  std::string method;     ///< Empty for responses.
  std::string session_id;
  Json payload;
};

class LspLogPanel : public QWidget {
  Q_OBJECT

 public:
  explicit LspLogPanel(QWidget *parent = nullptr);

  /// Append one captured envelope.  Safe to call from any thread (the
  /// call is marshalled onto the GUI thread via QMetaObject).
  void Append(const LogEntry &entry);

 public slots:
  void Clear();

 private slots:
  void OnSelectionChanged();
  void RebuildList();

 private:
  void AddEntryRow(const LogEntry &entry);
  bool MatchesFilter(const LogEntry &entry) const;
  static std::string ClassifyEntry(const Json &payload);

  QListWidget *list_{nullptr};
  QPlainTextEdit *detail_{nullptr};
  QComboBox *direction_filter_{nullptr};
  QComboBox *kind_filter_{nullptr};
  QLineEdit *method_filter_{nullptr};
  QPushButton *clear_button_{nullptr};

  std::deque<LogEntry> entries_;
  static constexpr std::size_t kMaxEntries = 2000;
};

}  // namespace polyglot::tools::ui::lsp
