#!/usr/bin/env python3
"""
docs_sync_check.py — Bilingual documentation synchronisation checker.

Compares English and Chinese Markdown document pairs to detect structural
drift: missing sections, heading count mismatches, and significant content
length divergence.

Usage:
    python scripts/docs_sync_check.py              # check all pairs
    python scripts/docs_sync_check.py --verbose     # show heading details
    python scripts/docs_sync_check.py --ci          # exit 1 on mismatch

Output format:
    ✓  docs/api/api_reference.md ↔ api_reference_zh.md  (32 ↔ 32 headings)
    ✗  docs/USER_GUIDE.md ↔ USER_GUIDE_zh.md  (45 ↔ 43 headings)  MISMATCH
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import List, Tuple


HEADING_RE = re.compile(r'^(#{1,6})\s+(.+)$', re.MULTILINE)

# Threshold for content-length divergence warning (ratio)
LENGTH_DIVERGENCE_THRESHOLD = 0.30  # 30% difference


def find_project_root() -> Path:
    p = Path(__file__).resolve().parent
    while p != p.parent:
        if (p / "CMakeLists.txt").exists():
            return p
        p = p.parent
    return Path(__file__).resolve().parent.parent


def extract_headings(content: str) -> List[Tuple[int, str]]:
    """Extract (level, title) pairs from Markdown content."""
    return [(len(m.group(1)), m.group(2).strip())
            for m in HEADING_RE.finditer(content)]


def find_bilingual_pairs(root: Path) -> List[Tuple[Path, Path]]:
    """Find all (en, zh) Markdown file pairs under docs/."""
    docs = root / "docs"
    pairs = []
    seen = set()

    for en_file in sorted(docs.rglob("*.md")):
        if en_file.stem.endswith("_zh"):
            continue
        if "demand" in str(en_file):
            continue
        zh_file = en_file.with_stem(en_file.stem + "_zh")
        if zh_file.exists():
            pair_key = en_file.stem
            if pair_key not in seen:
                seen.add(pair_key)
                pairs.append((en_file, zh_file))

    return pairs


def check_pair(en_file: Path, zh_file: Path, root: Path,
               verbose: bool = False) -> bool:
    """Check a single bilingual pair.  Returns True if OK."""
    en_content = en_file.read_text(encoding="utf-8")
    zh_content = zh_file.read_text(encoding="utf-8")

    en_headings = extract_headings(en_content)
    zh_headings = extract_headings(zh_content)

    en_levels = [h[0] for h in en_headings]
    zh_levels = [h[0] for h in zh_headings]

    rel = en_file.relative_to(root)
    ok = True

    # Check heading structure match
    if en_levels != zh_levels:
        print(f"  ✗  {rel} ↔ {zh_file.name}  "
              f"({len(en_headings)} ↔ {len(zh_headings)} headings)  "
              f"HEADING MISMATCH")
        ok = False
        if verbose:
            # Show where they diverge
            max_len = max(len(en_levels), len(zh_levels))
            for i in range(max_len):
                en_h = en_headings[i] if i < len(en_headings) else None
                zh_h = zh_headings[i] if i < len(zh_headings) else None
                marker = " " if (en_h and zh_h and en_h[0] == zh_h[0]) else "!"
                en_str = f"h{en_h[0]}: {en_h[1][:40]}" if en_h else "(missing)"
                zh_str = f"h{zh_h[0]}: {zh_h[1][:40]}" if zh_h else "(missing)"
                print(f"    {marker} EN: {en_str}")
                print(f"    {marker} ZH: {zh_str}")
    else:
        print(f"  ✓  {rel} ↔ {zh_file.name}  "
              f"({len(en_headings)} ↔ {len(zh_headings)} headings)")

    # Check content length divergence
    en_len = len(en_content)
    zh_len = len(zh_content)
    if en_len > 0 and zh_len > 0:
        ratio = abs(en_len - zh_len) / max(en_len, zh_len)
        if ratio > LENGTH_DIVERGENCE_THRESHOLD:
            print(f"    ⚠  Content length divergence: EN={en_len} chars, "
                  f"ZH={zh_len} chars ({ratio:.0%} difference)")

    return ok


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check bilingual Markdown documentation synchronisation"
    )
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Show detailed heading comparison")
    parser.add_argument("--ci", action="store_true",
                        help="Exit 1 if any mismatch found")
    parser.add_argument("--root", type=str, default=None,
                        help="Project root directory")
    args = parser.parse_args()

    root = Path(args.root) if args.root else find_project_root()
    pairs = find_bilingual_pairs(root)

    print(f"docs-sync: checking {len(pairs)} bilingual pairs...\n")

    all_ok = True
    for en_file, zh_file in pairs:
        if not check_pair(en_file, zh_file, root, verbose=args.verbose):
            all_ok = False

    print()
    if all_ok:
        print(f"All {len(pairs)} bilingual pairs are synchronised. ✓")
        return 0
    else:
        print("Some bilingual pairs have structural differences. ✗")
        if args.ci:
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
