#!/usr/bin/env python3
"""
docs_generate.py — Documentation single-source generator for PolyglotCompiler.

Reads variables from docs/_variables.json and patches key sections in
README.md and other documentation files so that numbers, paths, and
version strings stay consistent across the entire doc tree.

This script does NOT rewrite files from scratch.  It finds well-known
marker comments in Markdown and replaces the content between them with
generated text.  Files without markers are left untouched.

Marker format (inside HTML comments so they are invisible in rendered MD):

    <!-- BEGIN:section_name -->
    ... auto-generated content ...
    <!-- END:section_name -->

Supported sections:
  - test_badge          — README badge with test count
  - dependencies_table  — README dependency table
  - qt_setup_paths      — Qt setup script path references
  - tools_table         — Toolchain table
  - version_footer      — Document version footer

Usage:
    python scripts/docs_generate.py              # dry-run (show diffs)
    python scripts/docs_generate.py --apply      # write changes
    python scripts/docs_generate.py --check      # CI mode: exit 1 if stale
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Dict, Optional


# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

def find_project_root() -> Path:
    """Walk upward from this script to find the repo root."""
    p = Path(__file__).resolve().parent
    while p != p.parent:
        if (p / "CMakeLists.txt").exists():
            return p
        p = p.parent
    return Path(__file__).resolve().parent.parent


def load_variables(root: Path) -> Dict:
    """Load the single-source variables file."""
    var_file = root / "docs" / "_variables.json"
    if not var_file.exists():
        print(f"Error: variables file not found: {var_file}", file=sys.stderr)
        sys.exit(1)
    with open(var_file, "r", encoding="utf-8") as f:
        return json.load(f)


# ---------------------------------------------------------------------------
# Section generators
# ---------------------------------------------------------------------------

def gen_test_badge(v: Dict) -> str:
    """Generate the test badge image tag."""
    total = v["test_total"]
    suites = v["test_suites"]
    return (
        f'  <img alt="Tests" '
        f'src="https://img.shields.io/badge/Tests-{total}_cases_|_{suites}_suites-brightgreen.svg"/>'
    )


def gen_dependencies_table(v: Dict) -> str:
    """Generate the dependency table."""
    lines = [
        "| Dependency | Purpose |",
        "|-----------|---------|",
    ]
    for dep in v["dependencies"]:
        lines.append(f'| [{dep["name"]}]({dep["url"]}) | {dep["purpose"]} |')
    return "\n".join(lines)


def gen_qt_setup_en(v: Dict) -> str:
    """Generate Qt setup instructions (English)."""
    return (
        f'  - If Qt is not installed, run `./{ v["qt_setup_sh"] }` '
        f'(macOS/Linux) or `.\\{ v["qt_setup_ps1"] }` (Windows) '
        f'to download pre-built Qt 6 binaries via `aqtinstall`.'
    )


def gen_qt_setup_zh(v: Dict) -> str:
    """Generate Qt setup instructions (Chinese)."""
    return (
        f'  - 如果未安装 Qt，运行 `./{ v["qt_setup_sh"] }` '
        f'（macOS/Linux）或 `.\\{ v["qt_setup_ps1"] }`（Windows）'
        f'以通过 `aqtinstall` 下载预编译的 Qt 6 二进制文件。'
    )


def gen_tools_table(v: Dict) -> str:
    """Generate the toolchain table."""
    lines = [
        "| Tool | Binary | Purpose |",
        "|------|--------|---------|",
    ]
    for t in v["tools"]:
        name = t["name"]
        purpose = t["purpose"]
        lines.append(f"| {purpose.split(':')[0]} | `{name}` | {purpose} |")
    return "\n".join(lines)


def gen_version_footer_en(v: Dict) -> str:
    """Generate English version footer."""
    return (
        f'*Maintained by {v["project_name"]} Team*  \n'
        f'*Last Updated: {v["last_updated"]}*  \n'
        f'*Document Version: v{v["doc_version"]}*'
    )


def gen_version_footer_zh(v: Dict) -> str:
    """Generate Chinese version footer."""
    return (
        f'*本文档由 {v["project_name"]} 团队维护*  \n'
        f'*最后更新: {v["last_updated"]}*  \n'
        f'*文档版本: v{v["doc_version"]}*'
    )


# Map section names to generator functions
SECTION_GENERATORS = {
    "test_badge": gen_test_badge,
    "dependencies_table": gen_dependencies_table,
    "qt_setup_en": gen_qt_setup_en,
    "qt_setup_zh": gen_qt_setup_zh,
    "tools_table": gen_tools_table,
    "version_footer_en": gen_version_footer_en,
    "version_footer_zh": gen_version_footer_zh,
}


# ---------------------------------------------------------------------------
# Patching engine
# ---------------------------------------------------------------------------

MARKER_PATTERN = re.compile(
    r'(<!-- BEGIN:(\w+) -->)\n(.*?)\n(<!-- END:\2 -->)',
    re.DOTALL,
)


def patch_content(content: str, variables: Dict) -> str:
    """Replace all marked sections with generated content."""
    def replacer(m: re.Match) -> str:
        begin_marker = m.group(1)
        section_name = m.group(2)
        end_marker = m.group(4)
        gen = SECTION_GENERATORS.get(section_name)
        if gen is None:
            # Unknown section, leave as-is
            return m.group(0)
        new_content = gen(variables)
        return f"{begin_marker}\n{new_content}\n{end_marker}"

    return MARKER_PATTERN.sub(replacer, content)


def patch_inline_variables(content: str, variables: Dict) -> str:
    """Replace {{variable}} placeholders with values from variables dict."""
    def replacer(m: re.Match) -> str:
        key = m.group(1).strip()
        val = variables.get(key)
        if val is None:
            return m.group(0)  # leave unknown placeholders
        return str(val)

    return re.sub(r'\{\{(\w+)\}\}', replacer, content)


# ---------------------------------------------------------------------------
# Sync checker — compare bilingual heading structures
# ---------------------------------------------------------------------------

HEADING_RE = re.compile(r'^(#{1,6})\s+(.+)$', re.MULTILINE)


def extract_headings(content: str):
    """Extract (level, title) list from Markdown."""
    return [(len(m.group(1)), m.group(2).strip()) for m in HEADING_RE.finditer(content)]


def check_bilingual_sync(root: Path) -> int:
    """Compare heading structures between EN and ZH docs.  Returns issue count."""
    docs_dir = root / "docs"
    issues = 0
    for en_file in sorted(docs_dir.rglob("*.md")):
        if en_file.stem.endswith("_zh"):
            continue
        zh_file = en_file.with_stem(en_file.stem + "_zh")
        if not zh_file.exists():
            continue
        en_headings = extract_headings(en_file.read_text(encoding="utf-8"))
        zh_headings = extract_headings(zh_file.read_text(encoding="utf-8"))
        # Compare heading levels (titles may differ due to translation)
        en_levels = [h[0] for h in en_headings]
        zh_levels = [h[0] for h in zh_headings]
        if en_levels != zh_levels:
            print(f"  SYNC MISMATCH: {en_file.name} ({len(en_headings)} headings) "
                  f"vs {zh_file.name} ({len(zh_headings)} headings)")
            issues += 1
    return issues


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(
        description="PolyglotCompiler documentation single-source generator"
    )
    parser.add_argument("--apply", action="store_true",
                        help="Write changes to files")
    parser.add_argument("--check", action="store_true",
                        help="CI mode: exit 1 if any file is stale")
    parser.add_argument("--sync", action="store_true",
                        help="Check bilingual heading synchronisation")
    parser.add_argument("--root", type=str, default=None,
                        help="Project root directory")
    args = parser.parse_args()

    root = Path(args.root) if args.root else find_project_root()
    variables = load_variables(root)

    if args.sync:
        print("Checking bilingual heading synchronisation...")
        issues = check_bilingual_sync(root)
        if issues:
            print(f"\n{issues} sync issue(s) found.")
            return 1
        print("All bilingual pairs are in sync.")
        return 0

    # Scan for files with markers
    target_files = [
        root / "README.md",
        root / "docs" / "USER_GUIDE.md",
        root / "docs" / "USER_GUIDE_zh.md",
    ]
    # Also scan any file with markers
    for md in (root / "docs").rglob("*.md"):
        if md not in target_files:
            content = md.read_text(encoding="utf-8")
            if "<!-- BEGIN:" in content:
                target_files.append(md)

    stale_count = 0
    for f in target_files:
        if not f.exists():
            continue
        original = f.read_text(encoding="utf-8")
        patched = patch_content(original, variables)
        patched = patch_inline_variables(patched, variables)

        if patched != original:
            rel = f.relative_to(root)
            stale_count += 1
            if args.apply:
                f.write_text(patched, encoding="utf-8")
                print(f"  UPDATED: {rel}")
            else:
                print(f"  STALE:   {rel}")

    if stale_count == 0:
        print("All documentation is up to date.")
    elif not args.apply:
        print(f"\n{stale_count} file(s) would be updated. "
              f"Run with --apply to write changes.")

    if args.check and stale_count > 0:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
