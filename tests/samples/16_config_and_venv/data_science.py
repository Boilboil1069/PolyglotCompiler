# ============================================================================
# data_science.py — Python data-science utility functions
# Compiled by PolyglotCompiler's frontend_python → shared IR
# ============================================================================

import json
import math
import random


def random_matrix(rows: int, cols: int) -> list:
    """Generate a rows×cols matrix of random floats in [0, 1)."""
    random.seed(42)  # deterministic for reproducibility
    return [[random.random() for _ in range(cols)] for _ in range(rows)]


def normalize(matrix: list) -> list:
    """Min-max normalize each column to [0, 1]."""
    if not matrix or not matrix[0]:
        return matrix
    cols = len(matrix[0])
    result = [row[:] for row in matrix]
    for c in range(cols):
        col_vals = [row[c] for row in matrix]
        mn, mx = min(col_vals), max(col_vals)
        rng = mx - mn if mx != mn else 1.0
        for r in range(len(result)):
            result[r][c] = (result[r][c] - mn) / rng
    return result


def describe(matrix: list) -> dict:
    """Return column-wise statistics (like pandas describe)."""
    if not matrix or not matrix[0]:
        return {}
    cols = len(matrix[0])
    stats = {}
    for c in range(cols):
        col = [row[c] for row in matrix]
        avg = sum(col) / len(col)
        var = sum((x - avg) ** 2 for x in col) / max(len(col) - 1, 1)
        stats[f"col_{c}"] = {
            "mean": round(avg, 4),
            "std": round(math.sqrt(var), 4),
            "min": round(min(col), 4),
            "max": round(max(col), 4),
        }
    return stats


def mean(values: list) -> float:
    """Arithmetic mean of a flat list."""
    if not values:
        return 0.0
    flat = []
    for v in values:
        if isinstance(v, list):
            flat.extend(v)
        else:
            flat.append(v)
    return sum(flat) / len(flat) if flat else 0.0


def to_json(obj) -> str:
    """Serialize an arbitrary object to a JSON string."""
    return json.dumps(obj, default=str)


def from_json(s: str):
    """Deserialize a JSON string."""
    return json.loads(s)
