#!/usr/bin/env python3
"""
check_include_deps.py — Include dependency linter for PolyglotCompiler.

Enforces layer constraints in the codebase:
  - middle/ must NOT include common/include/ir/  (IR headers live in middle/)
  - backends/ must NOT include frontends/
  - frontends/ must NOT include backends/
  - tools/ may include anything

Usage:
    python scripts/check_include_deps.py              # check all
    python scripts/check_include_deps.py --ci          # exit 1 on violation

Exit codes:
    0 — no violations
    1 — violations found
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import List, Tuple


# Layer constraint rules: (source_dir_pattern, forbidden_include_pattern, description)
RULES: List[Tuple[str, str, str]] = [
    ("middle/", "common/include/ir/", "middle/ must not include common/include/ir/"),
    ("backends/", "frontends/", "backends/ must not include frontends/"),
    ("frontends/", "backends/", "frontends/ must not include backends/"),
]

INCLUDE_PATTERN = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.MULTILINE)


def find_project_root() -> Path:
    p = Path(__file__).resolve().parent
    while p != p.parent:
        if (p / "CMakeLists.txt").exists():
            return p
        p = p.parent
    return Path(__file__).resolve().parent.parent


def check_file(root: Path, filepath: Path) -> List[str]:
    """Check a single source file for layer violations."""
    violations = []
    rel = filepath.relative_to(root)
    rel_str = str(rel).replace("\\", "/")

    try:
        content = filepath.read_text(encoding="utf-8", errors="replace")
    except Exception:
        return violations

    for src_pattern, forbidden_pattern, description in RULES:
        if not rel_str.startswith(src_pattern):
            continue
        for m in INCLUDE_PATTERN.finditer(content):
            include_path = m.group(1)
            if forbidden_pattern in include_path:
                line_no = content[:m.start()].count("\n") + 1
                violations.append(
                    f"{rel_str}:{line_no}: includes '{include_path}' "
                    f"— violates rule: {description}"
                )

    return violations


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check include dependency layer constraints"
    )
    parser.add_argument("--ci", action="store_true",
                        help="Exit 1 on any violation")
    parser.add_argument("--root", type=str, default=None)
    args = parser.parse_args()

    root = Path(args.root) if args.root else find_project_root()

    # Scan C/C++ source and header files
    extensions = {".h", ".hpp", ".c", ".cpp", ".cxx"}
    scan_dirs = ["middle", "backends", "frontends", "common"]

    all_violations: List[str] = []
    file_count = 0

    for d in scan_dirs:
        base = root / d
        if not base.is_dir():
            continue
        for f in sorted(base.rglob("*")):
            if f.suffix not in extensions:
                continue
            file_count += 1
            all_violations.extend(check_file(root, f))

    print(f"check-include-deps: scanned {file_count} files\n")

    if all_violations:
        for v in all_violations:
            print(f"  ✗  {v}")
        print(f"\n{len(all_violations)} violation(s) found.")
        return 1 if args.ci else 1
    else:
        print("No layer constraint violations found. ✓")
        return 0


if __name__ == "__main__":
    sys.exit(main())
