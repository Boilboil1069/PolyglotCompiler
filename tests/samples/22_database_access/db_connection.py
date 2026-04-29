"""db_connection.py — Lightweight in-memory key/value table standing in for a DB."""

from __future__ import annotations

class Connection:
    def __init__(self, path: str):
        self.path = path
        self._table = {}

    def insert(self, row_id: int, payload: dict) -> None:
        self._table[row_id] = dict(payload)

    def fetch_rows(self):
        return [
            {"id": rid, **payload}
            for rid, payload in sorted(self._table.items())
        ]

    def close(self) -> None:
        self._table.clear()

