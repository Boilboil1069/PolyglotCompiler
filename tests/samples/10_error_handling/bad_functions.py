# ============================================================================
# bad_functions.py — Python functions with known signatures for error demo
# Compiled by PolyglotCompiler's frontend_python → shared IR
# ============================================================================


def process(x: int, y: int) -> float:
    """Requires exactly 2 integer parameters."""
    return float(x + y)


def transform(data: list) -> list:
    """Requires a list parameter, not an int."""
    return [x * 2 for x in data]


def greet(name: str) -> str:
    """Requires a string parameter."""
    return f"Hello, {name}!"
