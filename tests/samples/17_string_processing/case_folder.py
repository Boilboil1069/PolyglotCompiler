"""case_folder.py — Lowercases tokens and removes duplicates while preserving order."""

from __future__ import annotations

def fold(tokens):
    seen = set()
    result = []
    for raw in tokens:
        low = raw.casefold()
        if low not in seen:
            seen.add(low)
            result.append(low)
    return result


def join(tokens, sep: str = " "):
    return sep.join(fold(tokens))

