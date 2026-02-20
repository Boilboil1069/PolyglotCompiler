# ============================================================================
# collection_utils.py — Python list/dict utility functions
# Compiled by PolyglotCompiler's frontend_python → shared IR
# ============================================================================


def count_items(items: list) -> int:
    """Return the number of items in a list."""
    return len(items)


def running_sum(values: list) -> list:
    """Compute a running (prefix) sum of a numeric list."""
    result = []
    total = 0.0
    for v in values:
        total += v
        result.append(total)
    return result


def invert_dict(d: dict) -> dict:
    """Swap keys and values of a dictionary."""
    return {v: k for k, v in d.items()}


def dict_summary(d: dict) -> str:
    """Return a human-readable summary of a dictionary."""
    parts = [f"{k}={v}" for k, v in sorted(d.items(), key=lambda x: str(x[0]))]
    return "{ " + ", ".join(parts) + " }"
