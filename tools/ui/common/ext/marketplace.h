/**
 * @file     marketplace.h
 * @brief    Local marketplace: filesystem / HTTP index, install /
 *           uninstall / update / rollback, optional signature
 *           verification.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "tools/ui/common/ext/extension_api.h"

namespace polyglot::tools::ui::ext {

enum class MarketplaceSource {
  kFilesystem,
  kHttp,
};

std::string MarketplaceSourceName(MarketplaceSource s);

struct MarketplaceEntry {
  std::string id;
  std::string name;
  std::string version;
  std::string publisher;
  std::string download_url;
  std::string signature;          ///< Optional detached signature.
  std::string sha256;             ///< Hex digest.
  std::vector<Capability> required_capabilities;
};

struct InstallResult {
  bool ok{false};
  std::string message;
  std::string previous_version;   ///< Set on update; used for rollback.
};

/// Source-of-truth for available extensions.  In tests the index
/// is populated directly; in production a JSON document fetched
/// from disk or HTTP is parsed by `ParseIndex`.
class MarketplaceIndex {
 public:
  void Add(MarketplaceEntry entry);
  std::vector<MarketplaceEntry> List() const;
  std::optional<MarketplaceEntry> Find(const std::string &id) const;
  std::optional<MarketplaceEntry> FindVersion(const std::string &id,
                                              const std::string &version) const;

 private:
  // entries_[id] is sorted highest-version first.
  std::unordered_map<std::string, std::vector<MarketplaceEntry>> entries_;
};

/// Parse a marketplace index JSON blob into an `MarketplaceIndex`.
std::optional<MarketplaceIndex> ParseIndex(const std::string &json);

/// Optional signing.  When `required` is true, every install must
/// carry a non-empty signature that matches `expected` for the
/// given extension id, otherwise the install is rejected.
class SignaturePolicy {
 public:
  void set_required(bool r) { required_ = r; }
  bool required() const { return required_; }
  void Trust(const std::string &extension_id,
             const std::string &signature);
  bool Verify(const std::string &extension_id,
              const std::string &signature) const;

 private:
  bool required_{false};
  std::unordered_map<std::string, std::string> trusted_;
};

/// The marketplace owns an index, a signature policy, the install
/// history (for rollback) and a back-pointer to the host so it can
/// install / uninstall manifests in one call.
class Marketplace {
 public:
  explicit Marketplace(ExtensionHost *host) : host_(host) {}

  MarketplaceIndex &index() { return index_; }
  SignaturePolicy &signing() { return signing_; }

  InstallResult Install(const std::string &id);
  InstallResult Install(const std::string &id, const std::string &version);
  InstallResult Update(const std::string &id);
  InstallResult Rollback(const std::string &id);
  bool Uninstall(const std::string &id);

  /// History of installed versions for `id`, oldest first.
  std::vector<std::string> History(const std::string &id) const;

 private:
  ExtensionHost *host_;
  MarketplaceIndex index_;
  SignaturePolicy signing_;
  std::unordered_map<std::string, std::vector<std::string>> history_;

  InstallResult InstallEntry(const MarketplaceEntry &entry);
  static ExtensionManifest ManifestFromEntry(const MarketplaceEntry &e);
};

}  // namespace polyglot::tools::ui::ext
