#!/usr/bin/env python3
"""Install the Poly CodeMirror mode into a local Typora bundle.

Typora does not expose an official custom-language directory. Its CodeMirror
modes and language-name mapping are bundled into application resources, so a
working custom mode needs two small, reversible injections. This installer
refuses unknown layouts before writing and preserves content-addressed backups.
"""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import stat
import sys
import tempfile


MODE_START = "/* POLYGLOT_COMPILER_POLY_MODE_START */"
MODE_END = "/* POLYGLOT_COMPILER_POLY_MODE_END */"
LANGUAGE_NAMES = ',"poly","ploy"'
MODE_MAPPING = 'case"poly":case"ploy":return t?e:"text/x-poly";'


class InstallError(RuntimeError):
    """An expected Typora layout or patch anchor was not found."""


def type_mark_files(root: Path) -> tuple[Path, Path]:
    return (
        root / "appsrc" / "main.js",
        root / "lib" / "codemirror" / "mode.min.js",
    )


def is_type_mark_root(root: Path) -> bool:
    return all(path.is_file() for path in type_mark_files(root))


def expand_root(candidate: Path) -> list[Path]:
    """Accept a TypeMark directory, a Typora.app, or an install root."""
    return [
        candidate,
        candidate / "Contents" / "Resources" / "TypeMark",
        candidate / "resources" / "TypeMark",
        candidate / "Resources" / "TypeMark",
    ]


def default_candidates() -> list[Path]:
    home = Path.home()
    candidates = [
        Path("/Applications/Typora.app/Contents/Resources/TypeMark"),
        home / "Applications" / "Typora.app" / "Contents" / "Resources" / "TypeMark",
        Path("/usr/share/typora/resources/TypeMark"),
        Path("/opt/Typora/resources/TypeMark"),
        home / ".local" / "share" / "typora" / "resources" / "TypeMark",
    ]
    for env_name in ("ProgramFiles", "ProgramFiles(x86)", "LOCALAPPDATA"):
        base = os.environ.get(env_name)
        if base:
            candidates.extend(expand_root(Path(base) / "Typora"))
    return candidates


def find_type_mark_root(requested: Path | None) -> Path:
    candidates: list[Path] = []
    if requested is not None:
        candidates.extend(expand_root(requested.expanduser()))
    else:
        candidates.extend(default_candidates())

    for candidate in candidates:
        candidate = candidate.resolve()
        if is_type_mark_root(candidate):
            return candidate

    rendered = "\n  - ".join(str(path) for path in candidates)
    raise InstallError(
        "Typora's TypeMark resource directory was not found. Checked:\n"
        f"  - {rendered}\n"
        "Pass --typora-root PATH to either TypeMark or the Typora application directory."
    )


def read_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as exc:
        raise InstallError(f"cannot read {path}: {exc}") from exc


def content_backup(path: Path, content: str) -> Path:
    digest = hashlib.sha256(content.encode("utf-8")).hexdigest()[:12]
    backup = path.with_name(f"{path.name}.poly-highlight-backup-{digest}")
    if not backup.exists():
        try:
            shutil.copy2(path, backup)
        except OSError as exc:
            raise InstallError(f"cannot create backup {backup}: {exc}") from exc
    return backup


def atomic_write(path: Path, content: str) -> None:
    original_mode = stat.S_IMODE(path.stat().st_mode)
    temporary_name: str | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            newline="",
            dir=path.parent,
            prefix=f".{path.name}.poly-",
            delete=False,
        ) as temporary:
            temporary.write(content)
            temporary.flush()
            os.fsync(temporary.fileno())
            temporary_name = temporary.name
        os.chmod(temporary_name, original_mode)
        os.replace(temporary_name, path)
    except OSError as exc:
        if temporary_name:
            try:
                Path(temporary_name).unlink(missing_ok=True)
            except OSError:
                pass
        raise InstallError(f"cannot update {path}: {exc}") from exc


def patch_main(source: str) -> str:
    if MODE_MAPPING in source and LANGUAGE_NAMES in source:
        return source
    if MODE_MAPPING in source or LANGUAGE_NAMES in source:
        raise InstallError("Typora main.js contains a partial Poly patch; run --uninstall first")

    list_start = source.find("var u=[")
    list_end = source.find("].sort(", list_start)
    if list_start < 0 or list_end < 0:
        raise InstallError("unsupported Typora main.js: language autocomplete list was not found")

    source = source[:list_end] + LANGUAGE_NAMES + source[list_end:]

    mode_function_start = source.find("function g(e,t,n){", list_end)
    mode_function_end = source.find("}var v=function", mode_function_start)
    if mode_function_start < 0 or mode_function_end < 0:
        raise InstallError("unsupported Typora main.js: code-mode mapper was not found")

    default_anchor = source.rfind('default:return""', mode_function_start, mode_function_end)
    if default_anchor < 0:
        raise InstallError("unsupported Typora main.js: mode mapper default branch was not found")
    return source[:default_anchor] + MODE_MAPPING + source[default_anchor:]


def patch_mode_bundle(source: str, mode_source: str) -> str:
    if MODE_START in source and MODE_END in source:
        return source
    if MODE_START in source or MODE_END in source:
        raise InstallError("Typora mode.min.js contains a partial Poly patch; run --uninstall first")

    return (
        source
        + "\n"
        + MODE_START
        + "\n"
        + mode_source.rstrip()
        + "\n"
        + MODE_END
        + "\n"
    )


def unpatch_main(source: str) -> str:
    return source.replace(LANGUAGE_NAMES, "", 1).replace(MODE_MAPPING, "", 1)


def unpatch_mode_bundle(source: str) -> str:
    pattern = re.compile(
        r"\n" + re.escape(MODE_START) + r"\n.*?\n" + re.escape(MODE_END) + r"\n",
        flags=re.DOTALL,
    )
    return pattern.sub("", source, count=1)


def installation_state(main_source: str, mode_source: str) -> tuple[bool, bool]:
    main_ready = MODE_MAPPING in main_source and LANGUAGE_NAMES in main_source
    mode_ready = MODE_START in mode_source and MODE_END in mode_source
    return main_ready, mode_ready


def print_state(root: Path, main_source: str, mode_source: str) -> None:
    main_ready, mode_ready = installation_state(main_source, mode_source)
    print(f"Typora resources: {root}")
    print(f"language aliases: {'installed' if main_ready else 'not installed'}")
    print(f"CodeMirror mode:  {'installed' if mode_ready else 'not installed'}")
    if main_ready != mode_ready:
        print("state: partial installation; use --uninstall before installing again")
    elif main_ready:
        print("state: ready for ```poly and ```ploy")
    else:
        print("state: unchanged")


def install(root: Path, mode_path: Path) -> None:
    main_path, bundle_path = type_mark_files(root)
    original_main = read_text(main_path)
    original_bundle = read_text(bundle_path)
    mode_source = read_text(mode_path)
    patched_main = patch_main(original_main)
    patched_bundle = patch_mode_bundle(original_bundle, mode_source)

    if patched_main == original_main and patched_bundle == original_bundle:
        print("Poly highlighting is already installed; no files changed.")
        return

    backups = [
        content_backup(main_path, original_main),
        content_backup(bundle_path, original_bundle),
    ]

    try:
        atomic_write(main_path, patched_main)
        atomic_write(bundle_path, patched_bundle)
    except InstallError:
        # Keep the pair consistent if the second atomic replacement fails.
        if read_text(main_path) == patched_main:
            atomic_write(main_path, original_main)
        raise

    print("Installed Poly syntax highlighting (including the legacy ploy alias).")
    print("Backups preserved:")
    for backup in backups:
        print(f"  - {backup}")


def uninstall(root: Path) -> None:
    main_path, bundle_path = type_mark_files(root)
    original_main = read_text(main_path)
    original_bundle = read_text(bundle_path)
    clean_main = unpatch_main(original_main)
    clean_bundle = unpatch_mode_bundle(original_bundle)

    if clean_main == original_main and clean_bundle == original_bundle:
        print("Poly highlighting is not installed; no files changed.")
        return

    atomic_write(main_path, clean_main)
    atomic_write(bundle_path, clean_bundle)
    print("Removed the injected Poly mode and aliases. Backup files were kept.")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Check, install, or uninstall Typora highlighting for Poly."
    )
    action = parser.add_mutually_exclusive_group()
    action.add_argument("--install", action="store_true", help="inject the mode and aliases")
    action.add_argument("--uninstall", action="store_true", help="remove only this tool's injections")
    action.add_argument("--check", action="store_true", help="show installation state (default)")
    parser.add_argument(
        "--typora-root",
        type=Path,
        help="TypeMark directory, Typora.app, or a Typora installation root",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        root = find_type_mark_root(args.typora_root)
        main_path, bundle_path = type_mark_files(root)
        if args.install:
            install(root, Path(__file__).resolve().with_name("poly.js"))
        elif args.uninstall:
            uninstall(root)
        else:
            print_state(root, read_text(main_path), read_text(bundle_path))

        if sys.platform == "darwin" and ".app" in str(root):
            print(
                "Note: editing application resources invalidates the vendor code signature. "
                "See README.md for the recommended patched-copy workflow."
            )
        return 0
    except InstallError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
