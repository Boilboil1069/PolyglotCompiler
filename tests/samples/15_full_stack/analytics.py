# ============================================================================
# analytics.py — Python data analytics functions
# Compiled by PolyglotCompiler's frontend_python → shared IR
# ============================================================================

import json


def mean(values: list) -> float:
    """Compute the arithmetic mean of a numeric list."""
    if not values:
        return 0.0
    return sum(values) / len(values)


def std_dev(values: list) -> float:
    """Compute the sample standard deviation."""
    if len(values) < 2:
        return 0.0
    avg = mean(values)
    variance = sum((x - avg) ** 2 for x in values) / (len(values) - 1)
    return variance ** 0.5


def summary_json(values: list) -> str:
    """Return a JSON summary of a numeric list."""
    if not values:
        return json.dumps({"count": 0})
    return json.dumps({
        "count": len(values),
        "mean": round(mean(values), 4),
        "min": round(min(values), 4),
        "max": round(max(values), 4),
        "std_dev": round(std_dev(values), 4),
    })
