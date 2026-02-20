# ============================================================================
# ml_model.py — Python ML model for pipeline demo
# Compiled by PolyglotCompiler's frontend_python → shared IR
# ============================================================================

from typing import List


def predict(data: List[float]) -> float:
    """Simple prediction: compute weighted average of the input data."""
    if not data:
        return 0.0
    weights = [1.0 / (i + 1) for i in range(len(data))]
    total_weight = sum(weights)
    return sum(d * w for d, w in zip(data, weights)) / total_weight


def classify(data: List[float], threshold: float) -> int:
    """Binary classification: returns 1 if prediction exceeds threshold."""
    pred = predict(data)
    return 1 if pred > threshold else 0


def batch_predict(batch: List[List[float]]) -> List[float]:
    """Predict for each sample in the batch."""
    return [predict(sample) for sample in batch]


def evaluate(predictions: List[float], targets: List[float]) -> float:
    """Compute mean squared error between predictions and targets."""
    if not predictions or not targets:
        return 0.0
    n = min(len(predictions), len(targets))
    mse = sum((predictions[i] - targets[i]) ** 2 for i in range(n)) / n
    return mse
