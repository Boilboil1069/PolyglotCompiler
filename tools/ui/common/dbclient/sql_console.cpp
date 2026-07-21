/**
 * @file     sql_console.cpp
 * @brief    SQL console / pager / CSV exporter implementation.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/dbclient/sql_console.h"

#include <sstream>

namespace polyglot::tools::ui::dbclient {

ResultPager::ResultPager(ResultSet rs, size_t page_size)
    : rs_(std::move(rs)), page_size_(page_size == 0 ? 50 : page_size) {}

size_t ResultPager::page_count() const {
  if (rs_.rows.empty()) return 0;
  return (rs_.rows.size() + page_size_ - 1) / page_size_;
}

std::vector<Row> ResultPager::Page(size_t page_index) const {
  std::vector<Row> out;
  size_t begin = page_index * page_size_;
  if (begin >= rs_.rows.size()) return out;
  size_t end = std::min(begin + page_size_, rs_.rows.size());
  out.assign(rs_.rows.begin() + begin, rs_.rows.begin() + end);
  return out;
}

namespace {

std::string QuoteCsv(const std::string &v) {
  bool need = false;
  for (char c : v) {
    if (c == ',' || c == '"' || c == '\n' || c == '\r') { need = true; break; }
  }
  if (!need) return v;
  std::string out = "\"";
  for (char c : v) {
    if (c == '"') out += "\"\"";
    else out += c;
  }
  out += "\"";
  return out;
}

}  // namespace

std::string ExportCsv(const ResultSet &rs) {
  std::ostringstream os;
  for (size_t i = 0; i < rs.columns.size(); ++i) {
    if (i) os << ',';
    os << QuoteCsv(rs.columns[i].name);
  }
  os << '\n';
  for (const auto &r : rs.rows) {
    for (size_t i = 0; i < r.values.size(); ++i) {
      if (i) os << ',';
      os << QuoteCsv(r.values[i]);
    }
    os << '\n';
  }
  return os.str();
}

SqlConsole::SqlConsole(std::shared_ptr<SqlDriver> d, size_t cap)
    : driver_(std::move(d)),
      history_capacity_(cap == 0 ? 1 : cap) {}

ResultSet SqlConsole::Execute(const std::string &sql) {
  ResultSet rs = driver_->Execute(sql);
  history_.push_back({sql, rs});
  if (history_.size() > history_capacity_) {
    history_.erase(history_.begin(),
                   history_.begin() +
                       (history_.size() - history_capacity_));
  }
  return rs;
}

}  // namespace polyglot::tools::ui::dbclient
