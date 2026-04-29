"""dispatcher.py — Drains a FIFO of tasks and reports their resolved order."""

from __future__ import annotations

def drain(tasks):
    if not tasks:
        return []
    ordered = sorted(tasks, key=lambda t: (t.get("delay", 0), t.get("label", "")))
    return [t.get("label", "") for t in ordered]


def labels(tasks):
    return [t.get("label", "") for t in tasks]

