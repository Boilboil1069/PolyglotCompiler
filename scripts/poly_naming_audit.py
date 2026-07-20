#!/usr/bin/env python3
"""Reject legacy Poly spellings in current-facing documentation.

Run from any directory with::

    python3 scripts/poly_naming_audit.py

Historical changelogs, the requirements log, and the separately maintained
Typora compatibility fixture are excluded. Documented 1.x compatibility
policy and retained implementation/ABI identifiers are allowed.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TEXT_SUFFIXES = {".md", ".json", ".ndjson"}
EXCLUDED_PATHS = {
    Path("docs/CHANGELOG.md"),
    Path("docs/CHANGELOG_zh.md"),
}
EXCLUDED_PREFIXES = (
    Path("docs/demand"),
    Path("docs/editors/typora"),
)

LEGACY_SPELLING = re.compile(
    r"(?<![A-Za-z0-9_])(?:Ploy|ploy|\.ploy)(?![A-Za-z0-9_])"
)
# Catch public compound names such as `ploy_diagnostics.md` and user-defined
# example symbols such as `ploy_run`; the standalone-spelling regex above
# intentionally stops at underscores so it can distinguish identifiers.
LEGACY_COMPOUND = re.compile(
    r"(?<![A-Za-z0-9_])ploy_[a-z][A-Za-z0-9_.-]*"
)
LEGACY_FENCE = re.compile(r"^\s*```ploy(?:\s|$)")
COMPATIBILITY_CONTEXT = re.compile(
    r"legacy|histor(?:y|ic|ical)|compatib|alias|deprecated|remov|"
    r"internal|implementation|\bABI\b|历史|兼容|旧称|旧拼写|旧别名|"
    r"弃用|移除|内部|实现标识|保留",
    re.IGNORECASE,
)

# These identifiers pre-date the public-name correction and remain part of
# source layout, source compatibility, or ABI. Remove them before checking the
# rest of a line so an unrelated legacy public spelling on that line still
# fails the audit.
INTERNAL_IDENTIFIERS = (
    re.compile(r"(?<!\.)\bfrontends/ploy(?:/[A-Za-z0-9_./{}*+-]+)?"),
    re.compile(r"(?<!\.)\btests/unit/frontends/ploy(?:/[A-Za-z0-9_./{}*+-]+)?"),
    re.compile(r"(?<!\.)\bploy/src(?:/[A-Za-z0-9_./{}*+-]+)?"),
    re.compile(r"\bpolyglot::(?:frontends::)?ploy\b"),
    re.compile(r"\b(?:frontend|test_frontend)_ploy\b"),
    re.compile(r"\b__ploy_[A-Za-z0-9_]*\b"),
    re.compile(r"\b(?:F?Ploy)[A-Z][A-Za-z0-9_]*\b"),
    re.compile(r"\bPloy\*"),
    re.compile(r"\bploy_(?:ast|lexer|parser|sema|lowering)\.(?:h|cpp)\b"),
    re.compile(r"\bploy_e2e_real_exit_code_test\.cpp\b"),
)


def is_excluded(relative_path: Path) -> bool:
    if relative_path in EXCLUDED_PATHS:
        return True
    return any(
        relative_path == prefix or prefix in relative_path.parents
        for prefix in EXCLUDED_PREFIXES
    )


def candidate_files() -> list[Path]:
    candidates = {ROOT / "README.md"}
    for base in (ROOT / "docs", ROOT / "tests" / "samples"):
        for path in base.rglob("*"):
            if path.is_file() and path.suffix.lower() in TEXT_SUFFIXES:
                candidates.add(path)
    return sorted(candidates)


def audit_line(line: str) -> bool:
    if LEGACY_FENCE.search(line):
        return False
    if COMPATIBILITY_CONTEXT.search(line):
        return True
    checked = line
    for internal_identifier in INTERNAL_IDENTIFIERS:
        checked = internal_identifier.sub("", checked)
    return (LEGACY_SPELLING.search(checked) is None and
            LEGACY_COMPOUND.search(checked) is None)


def main() -> int:
    failures: list[str] = []
    checked_files = 0
    for path in candidate_files():
        relative_path = path.relative_to(ROOT)
        if is_excluded(relative_path):
            continue
        checked_files += 1
        for line_number, line in enumerate(
            path.read_text(encoding="utf-8").splitlines(), start=1
        ):
            if not audit_line(line):
                failures.append(
                    f"{relative_path}:{line_number}: legacy public spelling: "
                    f"{line.strip()}"
                )

    if failures:
        print("Poly naming audit failed:", file=sys.stderr)
        print("\n".join(failures), file=sys.stderr)
        return 1

    print(f"Poly naming audit passed ({checked_files} files checked).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
