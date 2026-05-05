# Local Marketplace

## Goal

Distribute, install and update extensions without a centralised
service.  The marketplace reads an index from disk or HTTP, picks
the highest semver per id, and drives `ExtensionHost::Install` /
`Uninstall` while keeping a per-id history so the user can roll
back at any time.

## Components

| Component | Header | Purpose |
| --- | --- | --- |
| `MarketplaceEntry` | [`tools/ui/common/ext/marketplace.h`](../../tools/ui/common/ext/marketplace.h) | One installable artefact: id, name, version, publisher, download URL, optional signature, sha256, required capabilities. |
| `MarketplaceIndex` | same | Buckets entries by id (sorted highest version first). `Find(id)` returns the canonical newest entry; `FindVersion(id, v)` returns a specific version for rollback / pinning. |
| `ParseIndex(json)` | same | Parses an index JSON document (`{ "extensions": [...] }`) into a `MarketplaceIndex`. |
| `SignaturePolicy` | same | Optional install gate. When `required` is set, every install must carry a non-empty signature that matches the trusted value for the id. |
| `Marketplace` | same | Wires an index, a signing policy and per-id install history into the host. Exposes `Install`, `Update`, `Rollback`, `Uninstall`. |

## Pipelines

* **Install.** `Install(id)` resolves the canonical entry,
  verifies the signature, builds an `ExtensionManifest` from the
  entry and hands it to `ExtensionHost::Install`. The version is
  appended to the per-id history. `Install(id, version)` pins a
  specific version (used by `Rollback`).
* **Update.** `Update(id)` no-ops when the host already has the
  newest indexed version; otherwise it installs the newest entry
  and records the previous version on the result so the UI can
  show "updated A.B.C → X.Y.Z".
* **Rollback.** `Rollback(id)` consults the install history,
  picks the version preceding the most recent install,
  uninstalls the current record and re-installs the older entry.
  When the history holds fewer than two entries the call returns
  `no rollback target` without touching the host.
* **Signing.** `SignaturePolicy::Verify` short-circuits when
  signing is not required; otherwise the install fails fast when
  the signature is empty or does not match the trusted value.

## Tests

[`tests/unit/polyui/marketplace_test.cpp`](../../tests/unit/polyui/marketplace_test.cpp)
covers `ParseIndex` round-tripping a multi-entry document and
picking the newest version, the full install / update / rollback
flow with history tracking, the signature policy gating both
rejected and accepted installs, and the missing-id failure path.

## Workspace and multi-root

The same release ships
[`Workspace`](../../tools/ui/common/workspace/workspace.h),
which parses and serialises `polyui.code-workspace`, manages
multiple roots with per-root settings (folder value wins,
otherwise workspace value), implements cross-root search, and
exposes `LanguageServerPool` so polyls / DAP / task instances are
isolated per `(folder, language, version)`.  See
[`workspace_test.cpp`](../../tests/unit/polyui/workspace_test.cpp)
for the pinned behaviour.
