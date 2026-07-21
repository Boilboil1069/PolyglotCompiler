/**
 * @file     file_sync.h
 * @brief    Two-way file sync planner between local and remote.
 *
 * The sync planner diffs a local file index against a remote one
 * (both produced from `RemoteSession::ListDir`) and emits a plan
 * of upload / download / delete operations.  The planner is value-
 * only: the IDE applies the plan through the corresponding
 * sessions.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <string>
#include <vector>

#include "tools/ui/common/remote/remote_session.h"

namespace polyglot::tools::ui::remote {

enum class SyncAction {
  kUpload,        ///< local -> remote (newer or missing on remote).
  kDownload,      ///< remote -> local (newer or missing on local).
  kDeleteRemote,  ///< present on remote, deleted locally.
  kDeleteLocal,   ///< present locally, deleted on remote.
};

std::string SyncActionName(SyncAction a);

struct SyncOperation {
  SyncAction action{SyncAction::kUpload};
  std::string path;
  long long bytes{0};
};

struct SyncPlan {
  std::vector<SyncOperation> operations;
  long long total_bytes{0};
};

enum class SyncDirection {
  kBidirectional,
  kPushOnly,      ///< Only local -> remote.
  kPullOnly,      ///< Only remote -> local.
};

/// Build a sync plan from two file indexes.  Files are compared by
/// path and modification time.  Tombstones (entries present in
/// `tombstones_*` but not in the live index) become deletions on
/// the opposite side.
SyncPlan PlanSync(const std::vector<RemoteFileStat> &local_index,
                  const std::vector<RemoteFileStat> &remote_index,
                  SyncDirection direction = SyncDirection::kBidirectional);

}  // namespace polyglot::tools::ui::remote
