/**
 * @file     file_sync.cpp
 * @brief    Implementation of `file_sync.h`.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/remote/file_sync.h"

#include <unordered_map>

namespace polyglot::tools::ui::remote {

std::string SyncActionName(SyncAction a) {
  switch (a) {
    case SyncAction::kUpload:       return "upload";
    case SyncAction::kDownload:     return "download";
    case SyncAction::kDeleteRemote: return "delete-remote";
    case SyncAction::kDeleteLocal:  return "delete-local";
  }
  return "unknown";
}

SyncPlan PlanSync(const std::vector<RemoteFileStat> &local_index,
                  const std::vector<RemoteFileStat> &remote_index,
                  SyncDirection direction) {
  SyncPlan plan;
  std::unordered_map<std::string, RemoteFileStat> local_map, remote_map;
  for (const auto &s : local_index)  local_map[s.path] = s;
  for (const auto &s : remote_index) remote_map[s.path] = s;

  auto push_op = [&](SyncAction a, const std::string &p, long long b) {
    plan.operations.push_back({a, p, b});
    plan.total_bytes += b;
  };

  for (const auto &[path, l] : local_map) {
    auto it = remote_map.find(path);
    if (it == remote_map.end()) {
      if (direction != SyncDirection::kPullOnly)
        push_op(SyncAction::kUpload, path, l.size);
      continue;
    }
    if (l.mtime > it->second.mtime) {
      if (direction != SyncDirection::kPullOnly)
        push_op(SyncAction::kUpload, path, l.size);
    } else if (l.mtime < it->second.mtime) {
      if (direction != SyncDirection::kPushOnly)
        push_op(SyncAction::kDownload, path, it->second.size);
    }
  }
  for (const auto &[path, r] : remote_map) {
    if (local_map.find(path) != local_map.end()) continue;
    if (direction != SyncDirection::kPushOnly)
      push_op(SyncAction::kDownload, path, r.size);
  }
  return plan;
}

}  // namespace polyglot::tools::ui::remote
