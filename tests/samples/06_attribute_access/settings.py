# ============================================================================
# settings.py — Python settings manager for attribute access demo
# Compiled by PolyglotCompiler's frontend_python → shared IR
# ============================================================================

from typing import Any, Dict, Optional


class Settings:
    """Application settings manager."""

    def __init__(self, app_name: str):
        self.app_name = app_name
        self.debug = False
        self.log_level = "INFO"
        self.max_connections = 100
        self.cache_size = 1024
        self.version = "1.0.0"

    def enable_debug(self) -> None:
        """Enable debug mode."""
        self.debug = True
        self.log_level = "DEBUG"

    def summary(self) -> str:
        """Return a summary string of current settings."""
        return (f"{self.app_name} v{self.version} "
                f"[debug={self.debug}, log={self.log_level}]")


class Counter:
    """A simple counter with min/max bounds."""

    def __init__(self, initial: int = 0):
        self.value = initial
        self.min_value = 0
        self.max_value = 1000
        self.step = 1

    def increment(self) -> int:
        """Increment and return new value."""
        self.value = min(self.value + self.step, self.max_value)
        return self.value

    def decrement(self) -> int:
        """Decrement and return new value."""
        self.value = max(self.value - self.step, self.min_value)
        return self.value

    def reset(self) -> None:
        """Reset counter to zero."""
        self.value = 0
