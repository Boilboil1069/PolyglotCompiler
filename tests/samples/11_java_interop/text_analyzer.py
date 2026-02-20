# ============================================================================
# text_analyzer.py — Python text analyzer for Java interop demo
# Compiled by PolyglotCompiler's frontend_python → shared IR
# ============================================================================


def word_count(text: str) -> int:
    """Count the number of words in a text string."""
    return len(text.split())


def to_uppercase(text: str) -> str:
    """Convert text to uppercase."""
    return text.upper()


def summarize(text: str, max_length: int) -> str:
    """Truncate text to max_length and append ellipsis if needed."""
    if len(text) <= max_length:
        return text
    return text[:max_length] + "..."


def char_frequency(text: str) -> dict:
    """Return a dictionary of character frequencies."""
    freq = {}
    for ch in text:
        freq[ch] = freq.get(ch, 0) + 1
    return freq
