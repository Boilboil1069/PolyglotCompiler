# ============================================================================
# model.py — Python ML model class for class instantiation demo
# Compiled by PolyglotCompiler's frontend_python → shared IR
# ============================================================================

from typing import List


class LinearModel:
    """A simple linear regression model."""

    def __init__(self, input_dim: int, output_dim: int):
        """Initialize weights and bias."""
        self.input_dim = input_dim
        self.output_dim = output_dim
        # Initialize weights to small values
        self.weights = [[0.01 * (i + j) for j in range(output_dim)]
                        for i in range(input_dim)]
        self.bias = [0.0] * output_dim
        self.learning_rate = 0.01

    def forward(self, x: List[float]) -> List[float]:
        """Forward pass: compute output = x @ weights + bias."""
        output = [0.0] * self.output_dim
        for j in range(self.output_dim):
            for i in range(min(len(x), self.input_dim)):
                output[j] += x[i] * self.weights[i][j]
            output[j] += self.bias[j]
        return output

    def parameters(self) -> int:
        """Return total number of parameters."""
        return self.input_dim * self.output_dim + self.output_dim

    def train_step(self, x: List[float], target: List[float]) -> float:
        """One training step. Returns loss value."""
        pred = self.forward(x)
        loss = sum((p - t) ** 2 for p, t in zip(pred, target)) / len(pred)
        # Simple gradient update (gradient descent)
        for j in range(self.output_dim):
            error = pred[j] - target[j]
            for i in range(min(len(x), self.input_dim)):
                self.weights[i][j] -= self.learning_rate * error * x[i]
            self.bias[j] -= self.learning_rate * error
        return loss

    def save(self, path: str) -> None:
        """Save model to file (placeholder)."""
        pass

    def load(self, path: str) -> None:
        """Load model from file (placeholder)."""
        pass


class DataLoader:
    """A simple data loader for batching data."""

    def __init__(self, data: List[List[float]], batch_size: int):
        self.data = data
        self.batch_size = batch_size
        self.index = 0

    def next_batch(self) -> List[List[float]]:
        """Return the next batch of data."""
        end = min(self.index + self.batch_size, len(self.data))
        batch = self.data[self.index:end]
        self.index = end
        if self.index >= len(self.data):
            self.index = 0
        return batch

    def reset(self) -> None:
        """Reset the data loader index."""
        self.index = 0

    def total_batches(self) -> int:
        """Return total number of batches."""
        return (len(self.data) + self.batch_size - 1) // self.batch_size
