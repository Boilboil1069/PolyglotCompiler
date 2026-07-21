/**
 * @file     sql_console.h
 * @brief    Database client and SQL console value model.
 *           Driver-agnostic at this layer; the IDE binds the
 *           SQLite driver in the Qt build, while the unit tests
 *           plug in a fake driver that returns scripted results.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace polyglot::tools::ui::dbclient {

struct Column {
  std::string name;
  std::string type;       ///< Driver-reported type label.
};

struct Row {
  std::vector<std::string> values;
};

struct ResultSet {
  std::vector<Column> columns;
  std::vector<Row> rows;
  long long affected_rows{0};
  std::string error;       ///< Empty on success.
};

/// Abstract driver.  The Qt build links a SQLite implementation;
/// tests use an in-process fake.
class SqlDriver {
 public:
  virtual ~SqlDriver() = default;
  virtual ResultSet Execute(const std::string &sql) = 0;
  virtual std::vector<std::string> Tables() = 0;
  virtual std::vector<Column> ColumnsOf(const std::string &table) = 0;
};

struct ExecutedQuery {
  std::string sql;
  ResultSet result;
};

/// Pages a `ResultSet` into fixed-size pages for the table view.
class ResultPager {
 public:
  ResultPager(ResultSet rs, size_t page_size);

  size_t page_count() const;
  size_t page_size() const { return page_size_; }
  size_t total_rows() const { return rs_.rows.size(); }
  const std::vector<Column> &columns() const { return rs_.columns; }

  std::vector<Row> Page(size_t page_index) const;

 private:
  ResultSet rs_;
  size_t page_size_;
};

/// CSV exporter; quotes fields that contain comma / quote /
/// newline and escapes embedded quotes by doubling.
std::string ExportCsv(const ResultSet &rs);

/// SQL console: command history, current driver, last result.
class SqlConsole {
 public:
  explicit SqlConsole(std::shared_ptr<SqlDriver> driver,
                      size_t history_capacity = 100);

  ResultSet Execute(const std::string &sql);
  const std::vector<ExecutedQuery> &history() const { return history_; }
  void ClearHistory() { history_.clear(); }

  std::vector<std::string> Tables() { return driver_->Tables(); }
  std::vector<Column> ColumnsOf(const std::string &t) {
    return driver_->ColumnsOf(t);
  }

 private:
  std::shared_ptr<SqlDriver> driver_;
  size_t history_capacity_;
  std::vector<ExecutedQuery> history_;
};

}  // namespace polyglot::tools::ui::dbclient
