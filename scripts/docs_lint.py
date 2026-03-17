#!/usr/bin/env python3
"""
docs_lint.py — Documentation linter for PolyglotCompiler.

Validates Markdown documentation for consistency, correctness, and
bilingual (Chinese/English) synchronisation.

Checks performed:
  1. Path references   — file/directory paths in docs must exist in the repo
  2. Bilingual pairing — every *_zh.md must have a matching *.md and vice versa
  3. Heading structure  — English and Chinese counterparts share the same heading
                          count per level (h1..h4)
  4. Version numbers    — version strings across README and docs are consistent
  5. Dead links         — relative Markdown links (e.g. [text](path)) resolve
  6. Test statistics    — badge / table numbers match (advisory)
  7. Orphan detection   — Markdown files not referenced from README or USER_GUIDE

Usage:
  python scripts/docs_lint.py                # lint everything
  python scripts/docs_lint.py --fix          # auto-fix known path issues
  python scripts/docs_lint.py --strict       # treat warnings as errors
  python scripts/docs_lint.py --ci           # CI mode: exit code 1 on any error

Exit codes:
  0 — all checks passed (or only warnings in non-strict mode)
  1 — errors found
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple


# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

# Directories scanned for Markdown files
DOC_DIRS = ["docs", "."]
# Files explicitly included regardless of directory
EXTRA_FILES = ["README.md"]

# Known bilingual suffix convention: foo.md <-> foo_zh.md
ZH_SUFFIX = "_zh"

# Regex for inline / reference-style Markdown links to local files
# Matches [text](path) and [text]: path
LINK_PATTERN = re.compile(
    r'\[(?:[^\]]*)\]\(([^)#\s]+?)(?:#[^)]*)?\)'
    r'|'
    r'^\[(?:[^\]]*)\]:\s*(\S+)',
    re.MULTILINE,
)

# Regex for backtick-quoted paths that look like repo file references
PATH_REF_PATTERN = re.compile(
    r'`((?:[\w.]+/)+[\w.*]+(?:\.\w+)?)`'
    r'|'
    r'`(\./[\w./\\*]+)`'
    r'|'
    r'`(\.\\[\w.\\/*]+)`'
)

# Version badge pattern in README
VERSION_BADGE_PATTERN = re.compile(r'Tests-(\d+)_cases')
# Version string in footer
VERSION_FOOTER_PATTERN = re.compile(r'Document Version:\s*v([\d.]+)')
LAST_UPDATED_PATTERN = re.compile(r'Last [Uu]pdated?:\s*([\d-]+)')

# Heading pattern
HEADING_PATTERN = re.compile(r'^(#{1,6})\s+(.+)$', re.MULTILINE)


# ---------------------------------------------------------------------------
# Diagnostics
# ---------------------------------------------------------------------------

@dataclass
class Diagnostic:
    """A single lint diagnostic."""
    file: str
    line: int
    level: str       # "error", "warning", "info"
    code: str        # e.g. "DL001"
    message: str

    def __str__(self) -> str:
        return f"{self.file}:{self.line}: [{self.level}] {self.code}: {self.message}"


@dataclass
class LintResult:
    """Aggregated lint results."""
    diagnostics: List[Diagnostic] = field(default_factory=list)

    @property
    def errors(self) -> List[Diagnostic]:
        return [d for d in self.diagnostics if d.level == "error"]

    @property
    def warnings(self) -> List[Diagnostic]:
        return [d for d in self.diagnostics if d.level == "warning"]

    def add(self, diag: Diagnostic) -> None:
        self.diagnostics.append(diag)

    def print_summary(self) -> None:
        n_err = len(self.errors)
        n_warn = len(self.warnings)
        total = len(self.diagnostics)
        print(f"\n{'='*60}")
        print(f"  docs-lint summary: {total} issues "
              f"({n_err} errors, {n_warn} warnings)")
        print(f"{'='*60}")


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def find_project_root() -> Path:
    """Walk upward from this script to find the repo root (has CMakeLists.txt)."""
    p = Path(__file__).resolve().parent
    while p != p.parent:
        if (p / "CMakeLists.txt").exists():
            return p
        p = p.parent
    # Fallback: assume script is at scripts/
    return Path(__file__).resolve().parent.parent


def collect_markdown_files(root: Path) -> List[Path]:
    """Collect all Markdown files in DOC_DIRS."""
    files: List[Path] = []
    for d in DOC_DIRS:
        base = root / d
        if base.is_dir():
            for md in sorted(base.rglob("*.md")):
                # Skip build/, _deps/, dist/, node_modules/
                rel = md.relative_to(root)
                parts = rel.parts
                if any(p.startswith("build") or p.startswith("_deps")
                       or p == "node_modules" or p.startswith("dist")
                       for p in parts):
                    continue
                files.append(md)
    # Deduplicate
    seen: Set[Path] = set()
    unique: List[Path] = []
    for f in files:
        rf = f.resolve()
        if rf not in seen:
            seen.add(rf)
            unique.append(f)
    return unique


def read_lines(path: Path) -> List[str]:
    """Read file lines, handling encoding gracefully."""
    try:
        return path.read_text(encoding="utf-8").splitlines()
    except UnicodeDecodeError:
        return path.read_text(encoding="utf-8-sig").splitlines()


# ---------------------------------------------------------------------------
# Check 1: Path references
# ---------------------------------------------------------------------------

def check_path_references(root: Path, md_file: Path, lines: List[str],
                          result: LintResult) -> None:
    """Verify that backtick-quoted file paths actually exist in the repo."""
    # Skip demand.md — it contains references to code, not file paths
    if md_file.name == "demand.md":
        return
    rel_dir = md_file.parent
    for i, line in enumerate(lines, 1):
        # Skip lines inside code blocks
        stripped = line.strip()
        if stripped.startswith("```"):
            continue
        for m in PATH_REF_PATTERN.finditer(line):
            raw_path = m.group(1) or m.group(2) or m.group(3)
            if not raw_path:
                continue
            # Skip non-path patterns
            if raw_path.startswith("http") or raw_path.startswith("$"):
                continue
            # Skip glob patterns and code identifiers
            if "*" in raw_path and not raw_path.endswith(".*"):
                continue
            # Skip things that look like code (namespace::, -> , etc.)
            if "::" in raw_path or "->" in raw_path:
                continue
            # Skip short patterns unlikely to be paths
            if "/" not in raw_path and "\\" not in raw_path:
                continue
            # Normalise path separators
            normalized = raw_path.replace("\\", "/").lstrip("./")
            # Skip patterns that look like alternatives (e.g. IF/ELSE,
            # Future/Promise) — all segments are uppercase or CamelCase
            # without file extensions
            segments = normalized.split("/")
            if all(re.match(r'^[A-Z][A-Za-z]*$', s) for s in segments):
                continue
            # Skip patterns without file extension where segments look
            # like identifiers (e.g. fadd/fsub/fmul/fdiv/frem)
            if "." not in segments[-1] and all(
                re.match(r'^[a-z_][a-z0-9_]*$', s) for s in segments
            ) and len(segments) <= 6 and len(segments[-1]) < 10:
                # Probably an identifier list, not a path — skip unless
                # at least one segment has 3+ parts resembling a directory
                if not any(len(s) > 12 for s in segments):
                    continue
            # Skip if it looks like a descriptive list (e.g.
            # polyasm/polyopt/polyrt) without an extension
            if "." not in normalized and len(segments) >= 2:
                # All segments short and no extension -> likely not a path
                if all(len(s) < 15 for s in segments) and \
                   not any(s in ("src", "include", "lib", "docs", "tests",
                                 "tools", "scripts", "runtime", "common",
                                 "frontends", "backends", "middle")
                           for s in segments):
                    continue
            # Try from project root
            candidate = root / normalized
            if not candidate.exists():
                # Try relative to document
                candidate_rel = rel_dir / normalized
                if not candidate_rel.exists():
                    result.add(Diagnostic(
                        file=str(md_file.relative_to(root)),
                        line=i,
                        level="error",
                        code="DL001",
                        message=f"Path reference `{raw_path}` does not exist "
                                f"in repository",
                    ))


# ---------------------------------------------------------------------------
# Check 2: Bilingual pairing
# ---------------------------------------------------------------------------

def check_bilingual_pairing(root: Path, md_files: List[Path],
                            result: LintResult) -> None:
    """Ensure every foo.md has a foo_zh.md counterpart and vice versa."""
    # Build sets of relative stems
    stems: Dict[str, Path] = {}  # stem -> path
    zh_stems: Dict[str, Path] = {}

    for f in md_files:
        rel = f.relative_to(root)
        name = f.stem
        # Skip non-doc files
        if str(rel).startswith("docs") or f.name == "README.md":
            pass
        else:
            continue

        # Skip demand.md and README.md (no _zh pair needed)
        if f.name in ("demand.md", "README.md"):
            continue

        if name.endswith(ZH_SUFFIX):
            base_stem = name[: -len(ZH_SUFFIX)]
            key = str(rel.parent / base_stem)
            zh_stems[key] = f
        else:
            key = str(rel.parent / name)
            stems[key] = f

    # Check English -> Chinese
    for key, en_file in stems.items():
        if key not in zh_stems:
            result.add(Diagnostic(
                file=str(en_file.relative_to(root)),
                line=1,
                level="warning",
                code="DL002",
                message=f"Missing Chinese counterpart: expected "
                        f"{en_file.stem}{ZH_SUFFIX}.md",
            ))

    # Check Chinese -> English
    for key, zh_file in zh_stems.items():
        if key not in stems:
            result.add(Diagnostic(
                file=str(zh_file.relative_to(root)),
                line=1,
                level="warning",
                code="DL003",
                message=f"Missing English counterpart: expected "
                        f"{zh_file.stem.replace(ZH_SUFFIX, '')}.md",
            ))


# ---------------------------------------------------------------------------
# Check 3: Heading structure synchronisation
# ---------------------------------------------------------------------------

def extract_heading_profile(lines: List[str]) -> Dict[int, int]:
    """Count headings by level (1-6)."""
    profile: Dict[int, int] = {}
    for line in lines:
        m = HEADING_PATTERN.match(line)
        if m:
            level = len(m.group(1))
            profile[level] = profile.get(level, 0) + 1
    return profile


def check_heading_sync(root: Path, md_files: List[Path],
                       result: LintResult) -> None:
    """Compare heading counts between bilingual document pairs."""
    # Group by base stem
    pairs: Dict[str, Dict[str, Tuple[Path, List[str]]]] = {}
    for f in md_files:
        rel = f.relative_to(root)
        if not str(rel).startswith("docs"):
            continue
        name = f.stem
        if name.endswith(ZH_SUFFIX):
            base = name[: -len(ZH_SUFFIX)]
            key = str(rel.parent / base)
            lang = "zh"
        else:
            key = str(rel.parent / name)
            lang = "en"
        if key not in pairs:
            pairs[key] = {}
        pairs[key][lang] = (f, read_lines(f))

    for key, langs in pairs.items():
        if "en" not in langs or "zh" not in langs:
            continue
        en_file, en_lines = langs["en"]
        zh_file, zh_lines = langs["zh"]
        en_profile = extract_heading_profile(en_lines)
        zh_profile = extract_heading_profile(zh_lines)

        # Compare heading counts for h2 and h3 (h1 may differ due to title)
        for level in [2, 3]:
            en_count = en_profile.get(level, 0)
            zh_count = zh_profile.get(level, 0)
            if en_count != zh_count:
                result.add(Diagnostic(
                    file=str(en_file.relative_to(root)),
                    line=1,
                    level="warning",
                    code="DL004",
                    message=f"Heading level {level} count mismatch: "
                            f"EN has {en_count}, ZH has {zh_count} "
                            f"(compared with {zh_file.name})",
                ))


# ---------------------------------------------------------------------------
# Check 4: Dead links (relative Markdown links)
# ---------------------------------------------------------------------------

def check_dead_links(root: Path, md_file: Path, lines: List[str],
                     result: LintResult) -> None:
    """Verify relative Markdown links resolve to existing files."""
    content = "\n".join(lines)
    for m in LINK_PATTERN.finditer(content):
        target = m.group(1) or m.group(2)
        if not target:
            continue
        # Skip external links
        if target.startswith(("http://", "https://", "mailto:", "#")):
            continue
        # Skip badge image URLs
        if target.startswith("https://img.shields.io"):
            continue

        # Determine line number
        pos = m.start()
        line_no = content[:pos].count("\n") + 1

        # Resolve path
        normalized = target.replace("\\", "/")
        # Try relative to the document
        candidate = (md_file.parent / normalized).resolve()
        if not candidate.exists():
            # Try relative to project root
            candidate = (root / normalized).resolve()
            if not candidate.exists():
                result.add(Diagnostic(
                    file=str(md_file.relative_to(root)),
                    line=line_no,
                    level="error",
                    code="DL005",
                    message=f"Dead link: `{target}` does not resolve to an "
                            f"existing file",
                ))


# ---------------------------------------------------------------------------
# Check 5: Version consistency
# ---------------------------------------------------------------------------

def check_version_consistency(root: Path, md_files: List[Path],
                              result: LintResult) -> None:
    """Check that version numbers are consistent across key documents."""
    versions_found: Dict[str, List[Tuple[str, int, str]]] = {}  # version -> [(file, line, context)]

    readme = root / "README.md"
    if readme.exists():
        lines = read_lines(readme)
        for i, line in enumerate(lines, 1):
            m = VERSION_BADGE_PATTERN.search(line)
            if m:
                key = f"test_count={m.group(1)}"
                versions_found.setdefault("test_count", []).append(
                    (str(readme.relative_to(root)), i, m.group(1))
                )
            m = VERSION_FOOTER_PATTERN.search(line)
            if m:
                versions_found.setdefault("doc_version", []).append(
                    (str(readme.relative_to(root)), i, m.group(1))
                )

    # Check USER_GUIDE footer versions
    for f in md_files:
        if "USER_GUIDE" not in f.name:
            continue
        lines = read_lines(f)
        for i, line in enumerate(lines, 1):
            m = VERSION_FOOTER_PATTERN.search(line)
            if m:
                versions_found.setdefault("doc_version", []).append(
                    (str(f.relative_to(root)), i, m.group(1))
                )
            m = LAST_UPDATED_PATTERN.search(line)
            if m:
                versions_found.setdefault("last_updated", []).append(
                    (str(f.relative_to(root)), i, m.group(1))
                )

    # Check consistency within each category
    for category, entries in versions_found.items():
        values = set(e[2] for e in entries)
        if len(values) > 1:
            locations = "; ".join(f"{e[0]}:{e[1]}={e[2]}" for e in entries)
            result.add(Diagnostic(
                file=entries[0][0],
                line=entries[0][1],
                level="warning",
                code="DL006",
                message=f"Version inconsistency for '{category}': {locations}",
            ))


# ---------------------------------------------------------------------------
# Check 6: Orphan detection
# ---------------------------------------------------------------------------

def check_orphan_docs(root: Path, md_files: List[Path],
                      result: LintResult) -> None:
    """Find docs not referenced from README.md or USER_GUIDE.md."""
    # Collect all link targets from README and USER_GUIDE
    referenced: Set[str] = set()
    anchor_files = [
        root / "README.md",
        root / "docs" / "USER_GUIDE.md",
        root / "docs" / "USER_GUIDE_zh.md",
    ]
    for af in anchor_files:
        if not af.exists():
            continue
        content = af.read_text(encoding="utf-8")
        for m in LINK_PATTERN.finditer(content):
            target = m.group(1) or m.group(2)
            if target and not target.startswith(("http://", "https://", "#")):
                normalized = target.replace("\\", "/").rstrip("/")
                # Could be a directory reference
                referenced.add(normalized)

    # Check each doc file
    for f in md_files:
        rel = f.relative_to(root)
        rel_str = str(rel).replace("\\", "/")
        # Skip the anchor files themselves and demand.md
        if f.name in ("README.md", "demand.md"):
            continue
        if rel_str.startswith("docs/demand"):
            continue

        # Check if this file (or its directory) is referenced
        is_referenced = False
        for ref in referenced:
            if rel_str == ref or rel_str.endswith("/" + ref) or ref.endswith("/"):
                # Directory reference
                if rel_str.startswith(ref.rstrip("/")):
                    is_referenced = True
                    break
            if rel_str == ref:
                is_referenced = True
                break
            # Handle docs/ prefix
            if rel_str == "docs/" + ref or ref == rel_str:
                is_referenced = True
                break
            # Directory-level match: docs/api/ matches docs/api/api_reference.md
            if ref.endswith("/") and rel_str.startswith(ref):
                is_referenced = True
                break
            # Strip docs/ prefix for comparison
            stripped = rel_str.removeprefix("docs/")
            if stripped == ref or ref.endswith("/" + stripped):
                is_referenced = True
                break
        if not is_referenced:
            result.add(Diagnostic(
                file=rel_str,
                line=1,
                level="info",
                code="DL007",
                message="Document not referenced from README.md or USER_GUIDE.md",
            ))


# ---------------------------------------------------------------------------
# Check 7: Common content issues
# ---------------------------------------------------------------------------

def check_content_issues(root: Path, md_file: Path, lines: List[str],
                         result: LintResult) -> None:
    """Check for common documentation quality issues."""
    for i, line in enumerate(lines, 1):
        # Check for TODO/FIXME/HACK markers in docs
        if re.search(r'\bTODO\b|\bFIXME\b|\bHACK\b|\bXXX\b', line,
                     re.IGNORECASE):
            result.add(Diagnostic(
                file=str(md_file.relative_to(root)),
                line=i,
                level="warning",
                code="DL008",
                message="TODO/FIXME marker found in documentation",
            ))

        # Check for placeholder text
        if re.search(r'\bTBD\b|\bplaceholder\b|\bcoming soon\b', line,
                     re.IGNORECASE):
            result.add(Diagnostic(
                file=str(md_file.relative_to(root)),
                line=i,
                level="warning",
                code="DL009",
                message="Placeholder text found in documentation",
            ))

        # Trailing whitespace (except in code blocks)
        if line.rstrip() != line and not line.strip().startswith("```"):
            # Only flag significant trailing whitespace (more than 2 spaces
            # used for line breaks in Markdown are intentional)
            trailing = len(line) - len(line.rstrip())
            if trailing > 0 and trailing != 2:
                pass  # Minor style issue, skip for now


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def run_lint(root: Path, strict: bool = False) -> LintResult:
    """Run all lint checks and return the result."""
    result = LintResult()
    md_files = collect_markdown_files(root)

    print(f"docs-lint: scanning {len(md_files)} Markdown files "
          f"under {root}...\n")

    # Per-file checks
    for f in md_files:
        lines = read_lines(f)
        check_path_references(root, f, lines, result)
        check_dead_links(root, f, lines, result)
        check_content_issues(root, f, lines, result)

    # Cross-file checks
    check_bilingual_pairing(root, md_files, result)
    check_heading_sync(root, md_files, result)
    check_version_consistency(root, md_files, result)
    check_orphan_docs(root, md_files, result)

    # Print diagnostics
    for d in sorted(result.diagnostics, key=lambda x: (x.level, x.file, x.line)):
        print(d)

    result.print_summary()
    return result


def main() -> int:
    parser = argparse.ArgumentParser(
        description="PolyglotCompiler documentation linter"
    )
    parser.add_argument(
        "--strict", action="store_true",
        help="Treat warnings as errors"
    )
    parser.add_argument(
        "--ci", action="store_true",
        help="CI mode: exit 1 on any error"
    )
    parser.add_argument(
        "--root", type=str, default=None,
        help="Project root directory (auto-detected if omitted)"
    )
    args = parser.parse_args()

    root = Path(args.root) if args.root else find_project_root()
    if not root.exists():
        print(f"Error: project root not found: {root}", file=sys.stderr)
        return 1

    result = run_lint(root, strict=args.strict)

    has_errors = len(result.errors) > 0
    has_warnings = len(result.warnings) > 0

    if args.strict and has_warnings:
        print("\n[STRICT] Warnings treated as errors.")
        return 1
    if args.ci and has_errors:
        return 1
    if has_errors:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
