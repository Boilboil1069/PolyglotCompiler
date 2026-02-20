# ============================================================================
# stats_utils.py — Python statistics utilities for .NET interop demo
# Compiled by PolyglotCompiler's frontend_python → shared IR
# ============================================================================

import math


def standard_deviation(values: list) -> float:
    """Compute the standard deviation of a list of numbers."""
    if len(values) == 0:
        return 0.0
    mean = sum(values) / len(values)
    variance = sum((x - mean) ** 2 for x in values) / len(values)
    return math.sqrt(variance)


def normalize(values: list) -> list:
    """Normalize a list of numbers to the range [0, 1]."""
    if len(values) == 0:
        return []
    lo = min(values)
    hi = max(values)
    span = hi - lo
    if span == 0:
        return [0.0] * len(values)
    return [(x - lo) / span for x in values]


def percentile(values: list, p: float) -> float:
    """Compute the p-th percentile (0-100) of a sorted list."""
    if len(values) == 0:
        return 0.0
    sorted_vals = sorted(values)
    k = (len(sorted_vals) - 1) * (p / 100.0)
    f = int(k)
    c = f + 1 if f + 1 < len(sorted_vals) else f
    d = k - f
    return sorted_vals[f] + d * (sorted_vals[c] - sorted_vals[f])


def moving_average(values: list, window: int) -> list:
    """Compute the moving average with the given window size."""
    if window <= 0 or len(values) == 0:
        return []
    result = []
    for i in range(len(values)):
        start = max(0, i - window + 1)
        chunk = values[start:i + 1]
        result.append(sum(chunk) / len(chunk))
    return result
