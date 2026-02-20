# ============================================================================
# data_processor.py — Python data processing for type mapping demo
# Compiled by PolyglotCompiler's frontend_python → shared IR
# ============================================================================

from typing import List, Tuple, Dict, Optional


def normalize(data: List[float]) -> List[float]:
    """Normalize a list of floats to [0, 1] range."""
    if not data:
        return []
    min_val = min(data)
    max_val = max(data)
    if max_val == min_val:
        return [0.0] * len(data)
    return [(x - min_val) / (max_val - min_val) for x in data]


def split_data(data: List[float], ratio: float) -> Tuple[List[float], List[float]]:
    """Split data into two parts at the given ratio."""
    split_idx = int(len(data) * ratio)
    return data[:split_idx], data[split_idx:]


def stats(data: List[float]) -> Dict[str, float]:
    """Compute basic statistics for a list of floats."""
    if not data:
        return {"mean": 0.0, "min": 0.0, "max": 0.0, "count": 0.0}
    return {
        "mean": sum(data) / len(data),
        "min": min(data),
        "max": max(data),
        "count": float(len(data)),
    }


def filter_above(data: List[float], threshold: float) -> List[float]:
    """Filter values above a threshold."""
    return [x for x in data if x > threshold]


def zip_lists(a: List[float], b: List[float]) -> List[Tuple[float, float]]:
    """Zip two lists into a list of tuples."""
    return list(zip(a, b))
