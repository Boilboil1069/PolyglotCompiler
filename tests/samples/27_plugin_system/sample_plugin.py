"""sample_plugin.py — Reference plugin implementation that returns the payload length."""

from __future__ import annotations

class Plugin:
    name = "sample"

    def handle(self, payload: str) -> int:
        return len(payload or "")


def make_plugin():
    return Plugin()


def handle(payload: str) -> int:
    return Plugin().handle(payload)

