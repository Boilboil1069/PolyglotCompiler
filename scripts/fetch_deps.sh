#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# PolyglotCompiler third-party dependency cacher (Linux / macOS).
#
# Pre-downloads every FetchContent dependency declared in Dependencies.cmake
# into "<repo>/.cache/deps/<name>/" so subsequent CMake configurations (and
# the packaging scripts) do not need network access. Each dependency is
# fetched at the exact tag pinned in Dependencies.cmake; an existing cache
# entry already at the correct tag is left untouched.
#
# Per dependency two transport strategies are tried, in order:
#   1. shallow git clone --depth 1 --branch <tag>
#   2. https tarball from
#      https://codeload.github.com/<owner>/<repo>/tar.gz/refs/tags/<tag>
#
# Each strategy is retried with exponential backoff (5s / 15s / 45s).
#
# Usage:
#   scripts/fetch_deps.sh                         # offline-first, fetch missing
#   scripts/fetch_deps.sh --refresh               # re-fetch everything
#   scripts/fetch_deps.sh --mirror https://gitclone.com/github.com/
#   scripts/fetch_deps.sh --cache-root /opt/deps
#
# Exit codes:
#   0  success (cache is fully populated)
#   2  one or more dependencies failed every transport
# -----------------------------------------------------------------------------

set -euo pipefail

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------
REFRESH=0
MIRROR=""
QUIET=0
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
CACHE_ROOT="${PROJECT_ROOT}/.cache/deps"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --refresh)     REFRESH=1; shift ;;
        --mirror)      MIRROR="${2:-}"; shift 2 ;;
        --cache-root)  CACHE_ROOT="${2:-}"; shift 2 ;;
        --quiet)       QUIET=1; shift ;;
        -h|--help)
            sed -n '2,30p' "$0"
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            exit 64
            ;;
    esac
done

MANIFEST="${CACHE_ROOT}/manifest.json"

# ---------------------------------------------------------------------------
# Dependency table (must mirror Dependencies.cmake exactly).
# Format:  name|fc_name|owner|repo|tag
# ---------------------------------------------------------------------------
DEPS=(
    "fmt|fmt|fmtlib|fmt|11.2.0"
    "nlohmann_json|nlohmann_json|nlohmann|json|v3.11.3"
    "Catch2|Catch2|catchorg|Catch2|v3.5.4"
    "mimalloc|mimalloc|microsoft|mimalloc|v2.1.7"
)

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
log()  { [[ $QUIET -eq 1 ]] || echo "  $*"; }
ok()   { [[ $QUIET -eq 1 ]] || printf '  \033[32m[OK]\033[0m %s\n' "$*"; }
warn() { printf '  \033[33m[!]\033[0m %s\n' "$*" >&2; }
step() {
    [[ $QUIET -eq 1 ]] && return
    echo
    printf '\033[36m==========================================\033[0m\n'
    printf '\033[36m %s\033[0m\n' "$*"
    printf '\033[36m==========================================\033[0m\n'
}

apply_mirror() {
    local url="$1"
    if [[ -z "$MIRROR" ]]; then
        echo "$url"
        return
    fi
    local prefix="https://github.com/"
    if [[ "$url" == "$prefix"* ]]; then
        echo "${MIRROR%/}/${url#$prefix}"
    else
        echo "$url"
    fi
}

# Tiny json reader: extract a top-level "<name>": { "tag": "..." } pair.
# We do not depend on jq because the script must run on minimal images.
manifest_tag_for() {
    local name="$1"
    [[ -f "$MANIFEST" ]] || { echo ""; return; }
    python3 - "$name" "$MANIFEST" <<'PY' 2>/dev/null || true
import json, sys
name, path = sys.argv[1], sys.argv[2]
try:
    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)
    entry = data.get(name)
    if isinstance(entry, dict):
        print(entry.get("tag", ""))
except Exception:
    pass
PY
}

manifest_set() {
    local name="$1" tag="$2" source="$3"
    python3 - "$MANIFEST" "$name" "$tag" "$source" <<'PY'
import json, os, sys, datetime
path, name, tag, source = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
data = {}
if os.path.exists(path):
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
    except Exception:
        data = {}
data[name] = {
    "tag": tag,
    "source": source,
    "fetched_at": datetime.datetime.utcnow().isoformat() + "Z",
}
with open(path, "w", encoding="utf-8") as f:
    json.dump(data, f, indent=2, sort_keys=True)
PY
}

cache_has_dep() {
    local name="$1" tag="$2"
    local dst="${CACHE_ROOT}/${name}"
    [[ -f "${dst}/CMakeLists.txt" ]] || return 1
    local cur
    cur="$(manifest_tag_for "$name")"
    [[ "$cur" == "$tag" ]] || return 1
    return 0
}

remove_dir() {
    local p="$1"
    [[ -e "$p" ]] || return 0
    chmod -R u+w "$p" 2>/dev/null || true
    rm -rf "$p"
}

try_git_clone() {
    local owner="$1" repo="$2" tag="$3" dst="$4"
    local url
    url="$(apply_mirror "https://github.com/${owner}/${repo}.git")"
    log "git clone --depth 1 --branch ${tag} ${url}"
    local tmp="${dst}.partial.$$"
    remove_dir "$tmp"
    if git clone --depth 1 --single-branch --branch "$tag" \
        --config advice.detachedHead=false "$url" "$tmp" \
        >/dev/null 2>&1; then
        remove_dir "$dst"
        mv "$tmp" "$dst"
        return 0
    fi
    remove_dir "$tmp"
    return 1
}

try_tarball() {
    local owner="$1" repo="$2" tag="$3" dst="$4"
    local url
    url="$(apply_mirror "https://github.com/${owner}/${repo}/archive/refs/tags/${tag}.tar.gz")"
    log "https GET ${url}"

    local tmp="${dst}.partial.$$"
    remove_dir "$tmp"
    mkdir -p "$tmp"

    local tarball="${tmp}/src.tar.gz"
    if command -v curl >/dev/null 2>&1; then
        curl --fail --location --silent --show-error --max-time 180 \
            --output "$tarball" "$url" || { remove_dir "$tmp"; return 1; }
    elif command -v wget >/dev/null 2>&1; then
        wget --quiet --timeout=180 --output-document="$tarball" "$url" \
            || { remove_dir "$tmp"; return 1; }
    else
        warn "neither curl nor wget is available"
        remove_dir "$tmp"
        return 1
    fi

    local extract="${tmp}/extract"
    mkdir -p "$extract"
    if ! tar -xzf "$tarball" -C "$extract" 2>/dev/null; then
        warn "tar extraction failed"
        remove_dir "$tmp"
        return 1
    fi

    local inner
    inner="$(find "$extract" -mindepth 1 -maxdepth 1 -type d | head -n 1)"
    if [[ -z "$inner" || ! -f "${inner}/CMakeLists.txt" ]]; then
        warn "tarball missing top-level CMakeLists.txt"
        remove_dir "$tmp"
        return 1
    fi

    remove_dir "$dst"
    mv "$inner" "$dst"
    remove_dir "$tmp"
    return 0
}

fetch_dep() {
    local name="$1" owner="$2" repo="$3" tag="$4"
    local dst="${CACHE_ROOT}/${name}"
    local strategy
    for strategy in git tarball; do
        local delay=5
        local attempt
        for attempt in 1 2 3; do
            log "attempt ${attempt}/3 via ${strategy}"
            local rc=1
            if [[ "$strategy" == "git" ]]; then
                if command -v git >/dev/null 2>&1; then
                    try_git_clone "$owner" "$repo" "$tag" "$dst" && rc=0 || rc=1
                else
                    rc=1
                fi
            else
                try_tarball "$owner" "$repo" "$tag" "$dst" && rc=0 || rc=1
            fi
            if [[ $rc -eq 0 ]]; then
                echo "$strategy"
                return 0
            fi
            if [[ $attempt -lt 3 ]]; then
                warn "retrying in ${delay}s ..."
                sleep "$delay"
                delay=$(( delay * 3 ))
            fi
        done
        warn "strategy '${strategy}' exhausted, falling back"
    done
    return 1
}

# ---------------------------------------------------------------------------
# Pre-flight
# ---------------------------------------------------------------------------
step "Dependency cache"
log "cache root : ${CACHE_ROOT}"
[[ -n "$MIRROR"  ]] && log "mirror     : ${MIRROR}"
[[ $REFRESH -eq 1 ]] && log "mode       : refresh (re-fetch everything)"

if ! command -v python3 >/dev/null 2>&1; then
    warn "python3 is required for manifest bookkeeping; install python3 and retry."
    exit 64
fi
if ! command -v git >/dev/null 2>&1; then
    warn "'git' is not in PATH; only the tarball transport will be available."
fi

mkdir -p "$CACHE_ROOT"

# ---------------------------------------------------------------------------
# Migrate any existing build/_deps/<name>-src into the cache so a project that
# has already configured once does not need to download anything.
# ---------------------------------------------------------------------------
for entry in "${DEPS[@]}"; do
    IFS='|' read -r name fc_name owner repo tag <<< "$entry"
    cache_dir="${CACHE_ROOT}/${name}"
    if cache_has_dep "$name" "$tag" && [[ $REFRESH -eq 0 ]]; then continue; fi
    [[ -f "${cache_dir}/CMakeLists.txt" ]] && continue
    lower="$(echo "$name" | tr '[:upper:]' '[:lower:]')"
    for build_deps in "${PROJECT_ROOT}/build/_deps" "${PROJECT_ROOT}/build-release/_deps"; do
        candidate="${build_deps}/${lower}-src"
        if [[ -f "${candidate}/CMakeLists.txt" ]]; then
            log "[${name}] importing existing source from ${candidate}"
            mkdir -p "$cache_dir"
            cp -a "${candidate}/." "${cache_dir}/"
            manifest_set "$name" "$tag" "imported:${candidate}"
            break
        fi
    done
done

# ---------------------------------------------------------------------------
# Main loop
# ---------------------------------------------------------------------------
failures=()
for entry in "${DEPS[@]}"; do
    IFS='|' read -r name fc_name owner repo tag <<< "$entry"

    if [[ $REFRESH -eq 0 ]] && cache_has_dep "$name" "$tag"; then
        step "${name} @ ${tag}"
        ok "cache hit"
        continue
    fi

    step "${name} @ ${tag}"
    if used="$(fetch_dep "$name" "$owner" "$repo" "$tag")"; then
        manifest_set "$name" "$tag" "$used"
        ok "fetched via ${used}"
    else
        failures+=("$name")
        warn "FAILED to fetch ${name} at ${tag} via every transport"
    fi
done

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
step "Summary"
for entry in "${DEPS[@]}"; do
    IFS='|' read -r name fc_name owner repo tag <<< "$entry"
    cur="$(manifest_tag_for "$name")"
    if [[ "$cur" == "$tag" && -f "${CACHE_ROOT}/${name}/CMakeLists.txt" ]]; then
        ok "${name}  ${tag}"
    else
        warn "${name}  MISSING"
    fi
done

if [[ ${#failures[@]} -gt 0 ]]; then
    echo
    echo "One or more dependencies could not be fetched:" >&2
    for f in "${failures[@]}"; do echo "  - $f" >&2; done
    echo
    echo "Tips:"
    echo "  - Re-run with --mirror https://gitclone.com/github.com/ to use a mirror."
    echo "  - Check your proxy / firewall settings."
    echo "  - Manually clone the missing repo into '<repo>/.cache/deps/<name>/' and re-run."
    exit 2
fi

echo
printf '\033[32mDependency cache is ready at: %s\033[0m\n' "$CACHE_ROOT"
exit 0
