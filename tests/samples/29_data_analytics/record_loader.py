"""record_loader.py — Builds a deterministic synthetic record set for the demo."""

from __future__ import annotations

def load(rows: int = 8):
    return [{"id": i, "value": float(i * 1.5)} for i in range(rows)]


def column(records, key):
    return [r.get(key) for r in records if key in r]

