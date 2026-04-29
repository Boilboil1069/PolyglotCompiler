"""text_decoder.py — UTF-8 text decoder with a deterministic fallback policy."""

from __future__ import annotations

def decode(buffer):
    if buffer is None:
        return ""
    if isinstance(buffer, str):
        return buffer
    try:
        return bytes(buffer).decode("utf-8")
    except UnicodeDecodeError:
        return bytes(buffer).decode("utf-8", errors="replace")


def line_count(buffer):
    return decode(buffer).count("\n") + 1

