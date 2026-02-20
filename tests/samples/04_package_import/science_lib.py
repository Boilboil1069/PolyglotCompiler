# ============================================================================
# science_lib.py — Python science library for package import demo
# Compiled by PolyglotCompiler's frontend_python → shared IR
# ============================================================================

from typing import List, Optional
import math


def mean(data: List[float]) -> float:
    """Compute arithmetic mean."""
    if not data:
        return 0.0
    return sum(data) / len(data)


def std_dev(data: List[float]) -> float:
    """Compute standard deviation."""
    if len(data) < 2:
        return 0.0
    m = mean(data)
    variance = sum((x - m) ** 2 for x in data) / (len(data) - 1)
    return math.sqrt(variance)


def linspace(start: float, stop: float, num: int) -> List[float]:
    """Generate evenly spaced values (like numpy.linspace)."""
    if num <= 1:
        return [start]
    step = (stop - start) / (num - 1)
    return [start + i * step for i in range(num)]


def dot(a: List[float], b: List[float]) -> float:
    """Compute dot product of two vectors."""
    return sum(x * y for x, y in zip(a, b))


def matrix_multiply(a: List[List[float]], b: List[List[float]]) -> List[List[float]]:
    """Simple matrix multiplication."""
    rows_a = len(a)
    cols_a = len(a[0]) if a else 0
    cols_b = len(b[0]) if b else 0
    result = [[0.0] * cols_b for _ in range(rows_a)]
    for i in range(rows_a):
        for j in range(cols_b):
            for k in range(cols_a):
                result[i][j] += a[i][k] * b[k][j]
    return result
