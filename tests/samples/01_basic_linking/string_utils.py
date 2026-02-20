# ============================================================================
# string_utils.py — Python string utilities for cross-language linking demo
# Compiled by PolyglotCompiler's frontend_python → shared IR
# ============================================================================


def format_result(value: int) -> str:
    """Format a numeric result as a human-readable string."""
    return f"Result: {value}"


def concat(a: str, b: str) -> str:
    """Concatenate two strings with a separator."""
    return f"{a} | {b}"


def to_upper(text: str) -> str:
    """Convert a string to uppercase."""
    return text.upper()


def string_length(text: str) -> int:
    """Return the length of a string."""
    return len(text)
