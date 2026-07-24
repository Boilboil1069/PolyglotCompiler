# Deterministic customer telemetry compiled by PolyglotCompiler's Python frontend.


def telemetry_sessions(row: int, start: int, step: int) -> int:
    return start - row * step


def telemetry_purchases(row: int, start: int) -> int:
    return start - row


def telemetry_complaints(row: int) -> int:
    return row


def telemetry_days_since_login(row: int, step: int, offset: int) -> int:
    return row * step + offset

