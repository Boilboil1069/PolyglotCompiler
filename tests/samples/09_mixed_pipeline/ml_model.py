# ============================================================================
# ml_model.py — Python ML model for full pipeline demo
# Compiled by PolyglotCompiler's frontend_python → shared IR
# ============================================================================

from typing import List, Dict, Optional


class NeuralNetwork:
    """A simple feed-forward neural network."""

    def __init__(self, input_size: int, hidden_size: int, output_size: int):
        self.input_size = input_size
        self.hidden_size = hidden_size
        self.output_size = output_size
        self.training = True
        self.learning_rate = 0.01
        # Simplified weight initialization
        self.w1 = [[0.01 * (i + j) for j in range(hidden_size)]
                    for i in range(input_size)]
        self.w2 = [[0.01 * (i + j) for j in range(output_size)]
                    for i in range(hidden_size)]

    def forward(self, x: List[float]) -> List[float]:
        """Forward pass through the network."""
        # Hidden layer
        hidden = [0.0] * self.hidden_size
        for j in range(self.hidden_size):
            for i in range(min(len(x), self.input_size)):
                hidden[j] += x[i] * self.w1[i][j]
            # ReLU activation
            hidden[j] = max(0.0, hidden[j])

        # Output layer
        output = [0.0] * self.output_size
        for j in range(self.output_size):
            for i in range(self.hidden_size):
                output[j] += hidden[i] * self.w2[i][j]
        return output

    def predict(self, data: List[float]) -> float:
        """Predict a single value from input data."""
        output = self.forward(data)
        return output[0] if output else 0.0

    def parameters(self) -> int:
        """Return total parameter count."""
        return (self.input_size * self.hidden_size +
                self.hidden_size * self.output_size)

    def save(self, path: str) -> None:
        """Save model weights (placeholder)."""
        pass

    def load(self, path: str) -> None:
        """Load model weights (placeholder)."""
        pass


def predict(data: List[float]) -> float:
    """Standalone prediction function (weighted average)."""
    if not data:
        return 0.0
    weights = [1.0 / (i + 1) for i in range(len(data))]
    total = sum(d * w for d, w in zip(data, weights))
    return total / sum(weights)


def batch_classify(samples: List[List[float]], threshold: float) -> List[int]:
    """Classify a batch of samples."""
    return [1 if predict(s) > threshold else 0 for s in samples]


def evaluate(predictions: List[float], targets: List[float]) -> Dict[str, float]:
    """Compute evaluation metrics."""
    n = min(len(predictions), len(targets))
    if n == 0:
        return {"mse": 0.0, "mae": 0.0}
    mse = sum((predictions[i] - targets[i]) ** 2 for i in range(n)) / n
    mae = sum(abs(predictions[i] - targets[i]) for i in range(n)) / n
    return {"mse": mse, "mae": mae}
