# Dependency Cache (Offline-First Builds)

> Status: shipped in PolyglotCompiler 1.4.1.

## Why

Prior to 1.4.1 every CMake configure (and therefore every invocation of
`scripts/package_*.{ps1,sh}`) re-ran `git clone` for `fmt`, `nlohmann/json`,
`Catch2` and `mimalloc` through CMake `FetchContent`. On flaky or
firewalled networks the clone would time out and abort the whole packaging
pipeline.

Version 1.4.1 introduces a **persistent on-disk cache** for those four
dependencies, populated by a dedicated script and consumed transparently
by `Dependencies.cmake`.

## Layout

```
<repo-root>/
  .cache/
    deps/
      fmt/                # extracted source tree (contains CMakeLists.txt)
      nlohmann_json/
      Catch2/
      mimalloc/
      manifest.json       # bookkeeping (tag, source, fetched_at) per dep
```

The cache root can be relocated via the `--cache-root` (Bash) or
`-CacheRoot` (PowerShell) flag of `fetch_deps`, or by the
`POLYGLOT_DEPS_CACHE_ROOT` environment variable (planned).

`.cache/` is git-ignored: caches are private to a workstation.

## How it works

`Dependencies.cmake` defines a helper `_polyglot_use_cached_dep(<dir> <name>)`
that, **for every declared dependency**, sets the standard CMake variable
`FETCHCONTENT_SOURCE_DIR_<UPPER>` to the matching cache directory **iff**
that directory exists and contains a top-level `CMakeLists.txt`. When set,
`FetchContent_Declare` skips git/HTTPS access entirely and uses the local
copy.

If the cache directory is empty, the variable is left unset and CMake
falls back to its normal network behaviour — i.e. the offline cache is
**purely additive** and never breaks an existing online workflow.

To enforce strict offline mode (fail instead of silently going online):

```bash
cmake -S . -B build -DFETCHCONTENT_FULLY_DISCONNECTED=ON
```

The packaging scripts expose this through their `--offline` / `-Offline`
flag.

## Populating the cache

### Windows / PowerShell

```powershell
# Default: fetch only what is missing, retry up to 3 times per transport.
powershell -ExecutionPolicy Bypass -File scripts/fetch_deps.ps1

# Force re-fetch of every dependency (e.g. after bumping a tag).
powershell -ExecutionPolicy Bypass -File scripts/fetch_deps.ps1 -Refresh

# Use a GitHub mirror when github.com is unreachable.
powershell -ExecutionPolicy Bypass -File scripts/fetch_deps.ps1 `
    -Mirror "https://gitclone.com/github.com/"
```

### Linux / macOS

```bash
scripts/fetch_deps.sh
scripts/fetch_deps.sh --refresh
scripts/fetch_deps.sh --mirror https://gitclone.com/github.com/
```

Both scripts try, in order:

1. `git clone --depth 1 --branch <tag>` (3 attempts, 5s/15s/45s backoff)
2. HTTPS tarball download from
   `https://github.com/<owner>/<repo>/archive/refs/tags/<tag>.tar.gz`
   followed by local `tar -xzf` extraction (3 attempts, same backoff)

A successful run records the chosen transport and the resolved tag into
`manifest.json`, so a subsequent run with the same tag is a no-op.

If the cache root is empty *and* `build/_deps/<name>-src/` from a previous
configuration is present, the script first **imports** that directory
(no network access required) — convenient when migrating an existing
checkout to 1.4.1.

## Packaging script integration

All three packaging scripts gained four parallel options:

| Flag (Bash) | Flag (PowerShell) | Meaning |
|---|---|---|
| `--refresh-deps` | `-RefreshDeps` | Force `fetch_deps` to re-fetch everything |
| `--offline` | `-Offline` | Pass `-DFETCHCONTENT_FULLY_DISCONNECTED=ON` to CMake |
| `--skip-deps` | `-SkipDeps` | Do not invoke `fetch_deps` at all (CI pre-cached) |
| `--deps-mirror <url>` | `-DepsMirror <url>` | Forward a GitHub mirror URL |

By default the packaging scripts call `fetch_deps` once before the CMake
configure step. With a primed cache that call is essentially free
(`[OK] cache hit` for every dependency) and no network is touched.

## Pinned versions

The dependency table is duplicated in three places that **must stay in
lock-step**:

- `Dependencies.cmake` (canonical)
- `scripts/fetch_deps.ps1` (`$DepSpecs`)
- `scripts/fetch_deps.sh` (`DEPS=()`)

Current pins (1.4.1):

| Name | Tag |
|---|---|
| `fmt` | `11.2.0` |
| `nlohmann_json` | `v3.11.3` |
| `Catch2` | `v3.5.4` |
| `mimalloc` | `v2.1.7` |

When bumping a tag, update all three files and run `fetch_deps -Refresh`.

## Troubleshooting

- **Both transports time out.** Re-run with `-Mirror` / `--mirror` against
  a regional GitHub mirror, or manually populate
  `<repo>/.cache/deps/<name>/` from any other machine and copy it over.
- **`tar` not found on Windows.** Modern Windows 10/11 ships BSD `tar.exe`
  in `System32`. If yours does not, install Git for Windows (which bundles
  `tar`) or use WSL.
- **Strict offline build still touches the network.** Make sure every cache
  directory contains a top-level `CMakeLists.txt`; otherwise the helper
  refuses to redirect FetchContent and falls back to the network.
