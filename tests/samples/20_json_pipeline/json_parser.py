"""json_parser.py — Thin JSON parser facade with sane defaults."""

from __future__ import annotations

import json


def parse(payload: str):
    if not payload:
        return {}
    return json.loads(payload)


def parse_safe(payload: str):
    try:
        return parse(payload)
    except json.JSONDecodeError:
        return {}

