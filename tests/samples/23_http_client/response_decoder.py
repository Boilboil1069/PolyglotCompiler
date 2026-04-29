"""response_decoder.py — Parses an HTTP/1.1 response into a status/header/body dict."""

from __future__ import annotations

def decode(raw: str) -> dict:
    if not raw:
        return {"status": 0, "headers": {}, "body": ""}
    head, _, body = raw.partition("\r\n\r\n")
    lines = head.split("\r\n")
    status = int(lines[0].split()[1]) if lines and len(lines[0].split()) >= 2 else 0
    headers = {}
    for line in lines[1:]:
        if ": " in line:
            k, v = line.split(": ", 1)
            headers[k] = v
    return {"status": status, "headers": headers, "body": body}

