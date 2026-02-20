# ============================================================================
# base_classes.py — Python base classes for DELETE/EXTEND demo
# Compiled by PolyglotCompiler's frontend_python → shared IR
# ============================================================================

from typing import List, Optional


class Component:
    """Base class for game components."""

    def __init__(self, name: str):
        self.name = name
        self.enabled = True

    def update(self, dt: float) -> None:
        """Update the component (to be overridden by subclasses)."""
        pass

    def render(self) -> str:
        """Render the component (to be overridden by subclasses)."""
        return f"[{self.name}]"

    def destroy(self) -> None:
        """Clean up the component."""
        self.enabled = False


class PhysicsBody:
    """A simple physics body with position and velocity."""

    def __init__(self, mass: float):
        self.mass = mass
        self.vx = 0.0
        self.vy = 0.0
        self.vz = 0.0
        self.gravity = -9.81

    def apply_force(self, fx: float, fy: float, fz: float) -> None:
        """Apply a force to the body (F = ma)."""
        self.vx += fx / self.mass
        self.vy += fy / self.mass
        self.vz += fz / self.mass

    def step(self, dt: float) -> None:
        """Advance the simulation by dt seconds."""
        self.vy += self.gravity * dt

    def kinetic_energy(self) -> float:
        """Compute kinetic energy: 0.5 * m * v^2."""
        v_sq = self.vx ** 2 + self.vy ** 2 + self.vz ** 2
        return 0.5 * self.mass * v_sq

    def reset(self) -> None:
        """Reset velocities to zero."""
        self.vx = 0.0
        self.vy = 0.0
        self.vz = 0.0


class EventSystem:
    """A simple event system for dispatching events."""

    def __init__(self):
        self.handlers = {}
        self.event_count = 0

    def register_handler(self, event_name: str, handler_id: int) -> None:
        """Register a handler for an event."""
        if event_name not in self.handlers:
            self.handlers[event_name] = []
        self.handlers[event_name].append(handler_id)

    def emit(self, event_name: str) -> int:
        """Emit an event, returns number of handlers invoked."""
        self.event_count += 1
        if event_name in self.handlers:
            return len(self.handlers[event_name])
        return 0

    def clear(self) -> None:
        """Clear all handlers."""
        self.handlers.clear()
        self.event_count = 0
