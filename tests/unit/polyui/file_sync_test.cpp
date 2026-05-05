/**
 * @file     file_sync_test.cpp
 * @brief    Unit tests for `PlanSync`.
 *
 * @ingroup  Tool / polyui / Tests
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/remote/file_sync.h"

using namespace polyglot::tools::ui::remote;

namespace {
RemoteFileStat MakeStat(const std::string &p, long long mtime,
                        long long size) {
  RemoteFileStat s;
  s.path = p;
  s.mtime = mtime;
  s.size = size;
  return s;
}
}  // namespace

TEST_CASE("SyncAction names cover every variant",
          "[polyui][remote][sync]") {
  CHECK(SyncActionName(SyncAction::kUpload)       == "upload");
  CHECK(SyncActionName(SyncAction::kDownload)     == "download");
  CHECK(SyncActionName(SyncAction::kDeleteRemote) == "delete-remote");
  CHECK(SyncActionName(SyncAction::kDeleteLocal)  == "delete-local");
}

TEST_CASE("Bidirectional plan reflects newer-side wins",
          "[polyui][remote][sync]") {
  std::vector<RemoteFileStat> local = {
      MakeStat("a.ploy", 100, 10),     // newer locally  -> upload
      MakeStat("b.ploy",  50,  5),     // older locally  -> download
      MakeStat("c.ploy", 200, 20),     // missing remote -> upload
  };
  std::vector<RemoteFileStat> remote = {
      MakeStat("a.ploy",  90, 10),
      MakeStat("b.ploy", 150,  7),
      MakeStat("d.ploy", 200, 30),     // missing local  -> download
  };
  auto plan = PlanSync(local, remote);
  int up = 0, down = 0;
  for (const auto &op : plan.operations) {
    if (op.action == SyncAction::kUpload)   ++up;
    if (op.action == SyncAction::kDownload) ++down;
  }
  CHECK(up == 2);
  CHECK(down == 2);
  CHECK(plan.total_bytes > 0);
}

TEST_CASE("Push-only plan suppresses downloads",
          "[polyui][remote][sync]") {
  std::vector<RemoteFileStat> local = {MakeStat("a.ploy", 100, 10)};
  std::vector<RemoteFileStat> remote = {MakeStat("b.ploy", 100, 10)};
  auto plan = PlanSync(local, remote, SyncDirection::kPushOnly);
  REQUIRE(plan.operations.size() == 1);
  CHECK(plan.operations[0].action == SyncAction::kUpload);
  CHECK(plan.operations[0].path == "a.ploy");
}

TEST_CASE("Pull-only plan suppresses uploads",
          "[polyui][remote][sync]") {
  std::vector<RemoteFileStat> local = {MakeStat("a.ploy", 100, 10)};
  std::vector<RemoteFileStat> remote = {MakeStat("b.ploy", 100, 10)};
  auto plan = PlanSync(local, remote, SyncDirection::kPullOnly);
  REQUIRE(plan.operations.size() == 1);
  CHECK(plan.operations[0].action == SyncAction::kDownload);
  CHECK(plan.operations[0].path == "b.ploy");
}

TEST_CASE("Identical indexes produce an empty plan",
          "[polyui][remote][sync]") {
  std::vector<RemoteFileStat> local = {MakeStat("a", 1, 1), MakeStat("b", 2, 2)};
  auto plan = PlanSync(local, local);
  CHECK(plan.operations.empty());
  CHECK(plan.total_bytes == 0);
}
